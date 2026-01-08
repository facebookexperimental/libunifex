/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if __cplusplus >= 201911L

#  include <unifex/create_basic_sender.hpp>
#  include <unifex/on.hpp>
#  include <unifex/single_thread_context.hpp>
#  include <unifex/stop_when.hpp>
#  include <unifex/sync_wait.hpp>
#  include <unifex/when_all_range.hpp>
#  include <unifex/when_any.hpp>

#  include <algorithm>
#  include <array>
#  include <initializer_list>
#  include <iostream>
#  include <mutex>
#  include <ranges>
#  include <set>
#  include <sstream>
#  include <string>

// Low-level asynchronous API to be wrapped

class C_MessageBrokerApi {
public:
  using slot_id = int;
  using on_sent = void (*)(void* context, slot_id slot, bool success);
  using on_received = void (*)(
      void* context, slot_id slot, const void* message /* failed = nullptr */);

  void send(slot_id slot, const void* message, on_sent sent, void* ctx);
  void stop_send(slot_id slot, void* context);
  void receive(slot_id slot, on_received received, void* ctx);
  void stop_receive(slot_id slot, void* context);

private:
  struct Message {
    slot_id slot;
    void* context{nullptr};
    const void* payload{nullptr};
    on_sent callback{nullptr};

    friend auto operator<=>(const Message& lhs, const Message& rhs) noexcept {
      if (auto c = lhs.slot <=> rhs.slot; c != 0) {
        return c;
      }
      return lhs.context <=> rhs.context;
    }
  };

  struct Receiver {
    slot_id slot;
    void* context{nullptr};
    on_received callback{nullptr};

    friend auto operator<=>(const Receiver& lhs, const Receiver& rhs) noexcept {
      if (auto c = lhs.slot <=> rhs.slot; c != 0) {
        return c;
      }
      return lhs.context <=> rhs.context;
    }
  };

  std::multiset<Message> messages_;
  std::multiset<Receiver> receivers_;
  std::mutex mutex_;
};

void C_MessageBrokerApi::send(
    slot_id slot, const void* message, on_sent sent, void* ctx) {
  std::unique_lock lock{mutex_};
  if (auto ready = receivers_.lower_bound({slot});
      ready != receivers_.end() && ready->slot == slot) {
    auto context{ready->context};
    auto callback{ready->callback};
    receivers_.erase(ready);
    lock.unlock();
    (*callback)(context, slot, message);
    (*sent)(ctx, slot, true);
  } else {
    messages_.emplace(Message{slot, ctx, message, sent});
  }
}

void C_MessageBrokerApi::stop_send(slot_id slot, void* context) {
  std::unique_lock lock{mutex_};
  if (auto waiting = messages_.find({slot, context});
      waiting != messages_.end()) {
    auto callback{waiting->callback};
    messages_.erase(waiting);
    lock.unlock();
    (*callback)(context, slot, false);
  }
}

void C_MessageBrokerApi::receive(
    slot_id slot, on_received received, void* ctx) {
  std::unique_lock lock{mutex_};
  if (auto waiting = messages_.lower_bound({slot});
      waiting != messages_.end() && waiting->slot == slot) {
    auto context{waiting->context};
    auto callback{waiting->callback};
    auto payload{waiting->payload};
    messages_.erase(waiting);
    lock.unlock();
    (*received)(ctx, slot, payload);
    (*callback)(context, slot, true);
  } else {
    receivers_.emplace(Receiver{slot, ctx, received});
  }
}

void C_MessageBrokerApi::stop_receive(slot_id slot, void* context) {
  std::unique_lock lock{mutex_};
  if (auto ready = receivers_.find({slot, context});
      ready != receivers_.end()) {
    auto callback{ready->callback};
    receivers_.erase(ready);
    lock.unlock();
    (*callback)(context, slot, nullptr);
  }
}

namespace {

using namespace unifex;

// S&R-based wrapper API

class MessageBrokerApi {
public:
  using slot_id = C_MessageBrokerApi::slot_id;
  static constexpr void* no_context{nullptr};

#  if defined(__clang_major__) && __clang_major__ < 18
  // https://github.com/llvm/llvm-project/issues/58434
  static constexpr auto sender_traits{with_sender_traits<
      _make_traits::sender_traits_literal{.is_always_scheduler_affine = true}>};
#  else
  static constexpr auto sender_traits{
      with_sender_traits<{.is_always_scheduler_affine = true}>};
#  endif

  auto send(slot_id slot, std::string message) noexcept {
    return create_basic_sender<>(
        [this, slot, message{std::move(message)}, context{no_context}](
            auto event,
            auto& op,
            slot_id on_slot = -1,
            bool success = false) mutable {
          if constexpr (event.is_start) {
            auto [ctx, callback] = unsafe_callback<slot_id, bool>(op).opaque();
            context = ctx;
            c_api_.send(slot, message.c_str(), callback, ctx);
          } else if constexpr (event.is_stop) {
            c_api_.stop_send(slot, context);
          } else if constexpr (event.is_callback) {
            assert(slot == on_slot);
#  ifdef NDEBUG
            (void)on_slot;
#  endif
            if (success) {
              op.set_value();
            } else {
              op.set_done();
            }
          }
        },
        sender_traits);
  }

  auto receive(slot_id slot) noexcept {
    return create_basic_sender<std::string>(
        [this, slot, context{no_context}](
            auto event,
            auto& op,
            slot_id on_slot = -1,
            const void* payload = nullptr) mutable {
          if constexpr (event.is_start) {
            auto [ctx, callback] =
                unsafe_callback<slot_id, const void*>(op).opaque();
            context = ctx;
            c_api_.receive(slot, callback, ctx);
          } else if constexpr (event.is_stop) {
            c_api_.stop_receive(slot, context);
          } else if constexpr (event.is_callback) {
            assert(slot == on_slot);
#  ifdef NDEBUG
            (void)on_slot;
#  endif
            if (payload != nullptr) {
              op.set_value(reinterpret_cast<const char*>(payload));
            } else {
              op.set_done();
            }
          }
        },
        sender_traits);
  }

private:
  C_MessageBrokerApi c_api_;
};

auto to_vector(const auto& list, auto&& fn) {
  using sender_t = decltype(fn(*list.begin()));
  std::vector<sender_t> r;
  r.reserve(list.size());
  std::transform(
      list.begin(),
      list.end(),
      std::back_inserter(r),
      std::forward<decltype(fn)>(fn));
  return r;
}

// NOT a serious master election algorithm because it ultimately uses
// cancellation to select the winner. But it does involve a lot of
// cancellations  :-)

using namespace std::string_literals;
const auto names = std::array{"Alice"s, "Bob"s, "Charlie"s};

auto elector(
    MessageBrokerApi& api,
    MessageBrokerApi::slot_id me,
    std::initializer_list<MessageBrokerApi::slot_id> others) {
  auto send_all{
      when_all_range(to_vector(
          others,
          [&api, me](MessageBrokerApi::slot_id other) {
            return api.send(other, names[me]);
          })) |
      let_value([](auto&&) noexcept { return just(); })};
  auto receive_all{
      when_all_range(to_vector(
          others,
          [&api, me](MessageBrokerApi::slot_id /* other */) {
            return api.receive(me);
          })) |
      let_value([me](auto&& others) noexcept {
        std::ostringstream s;
        s << names[me] << " is a master over ";
        for (const auto& other : others) {
          s << other << ' ';
        }
        return just(s.str());
      })};
  return stop_when(std::move(receive_all), std::move(send_all));
}

void elect() {
  MessageBrokerApi api;
  single_thread_context alice, bob, charlie;

  auto result{sync_wait(when_any(
      on(alice.get_scheduler(), elector(api, 0, {1, 2})),
      on(bob.get_scheduler(), elector(api, 1, {0, 2})),
      on(charlie.get_scheduler(), elector(api, 2, {0, 1}))))};

  if (result.has_value()) {
    std::cout << *result << std::endl;
  } else {
    std::cout << "Failed to elect master\n";
  }
}

}  // namespace

int main() {
  for (auto i = 0; i < 10; ++i) {
    elect();
  }
}

#else

int main() {
}

#endif
