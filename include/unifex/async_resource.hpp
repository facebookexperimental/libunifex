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
#pragma once

#include <unifex/config.hpp>
#include <unifex/any_sender_of.hpp>
#include <unifex/async_destroy.hpp>
#include <unifex/async_manual_reset_event.hpp>
#include <unifex/async_resource_ptr.hpp>
#include <unifex/defer.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_error.hpp>
#include <unifex/just_from.hpp>
#include <unifex/just_void_or_done.hpp>
#include <unifex/let_done.hpp>
#include <unifex/let_error.hpp>
#include <unifex/let_value.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/nest.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <unifex/unstoppable.hpp>
#include <unifex/v2/async_scope.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _async_resource {
using Scope = v2::async_scope;
/*
* `ResourceFactory` takes an `async_scope_ref` that refers to the *inner* scope
* `ResourceFactory(async_scope_ref)` returns a `Resource`
* the `Resource` is stored next to the *inner* scope in a heap-allocated
  `_container`
* `async_resource` takes an *outer* scope ref, returns a move-only
  `async_resource_ptr<Resource>`
* the returned `async_resource_ptr<Resource>` is a *unique_ptr*-like object
* creation of the `Resource`, it's asynchronous destruction and `join()` of
  the *inner* scope is spawned on the *outer* scope. Lifetime is controlled by
  `async_resource_ptr<Resource>`.
* `async_destroy` is a CPO algorithm that by default does nothing. Customizable
  through `tag_invoke` to do async destruction if `Resource` provides a
  `destroy() -> Sender`
* TBD customizations:
  - for a range of T, async_destroy(range) is just async_destroy on each T,
  followed by clearing the range
  - for an optional<T>, async_destroy(opt<T>) is a no-op on empty
  - async_destroy(*opt) on non-empty, followed by .reset()
  - for a unique_ptr<T>, async_destroy is async_destroy(*ptr) followed by
  .reset(), unless it's nullptr, in which case no-op
*/

class _container_base {
protected:
  enum class state_t : char {
    ALLOCATED,
    DESTRUCT_SPAWNED,
    CONSTRUCTED,
    DESTROYING,
  };
  // type-erased _container<Resource> func pointers
  void (*destruct_this_resource_)(_container_base*) noexcept;
  any_sender_of<> (*schedule_destruct_)(_container_base*, _container_base*);
  any_sender_of<> (*destroy_this_resource_)(_container_base*);
  void (*deleter_)(_container_base*) noexcept;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<Scope> scope_;
  // doubly linked list
  _container_base* children_;
  _container_base* next_;
  _container_base* parent_;
  // means destruction should start
  async_manual_reset_event destructionEvt_;
  std::mutex mutex_;
  std::atomic<state_t> state_;

  task<void> close_child_scopes() {
    auto* adopted = [&]() noexcept {
      // terminate when mutex_ fails to lock
      std::lock_guard lock{mutex_};
      return std::exchange(children_, nullptr);
    }();
    {
      for (auto* child = adopted; child != nullptr; child = child->next_) {
        co_await child->close_scope();
      }
    }
    {
      std::lock_guard lock{mutex_};
      auto** tail = &children_;

      while (*tail) {
        tail = &(*tail)->next_;
      }

      *tail = adopted;
    }
  }

  // asynchronous part of `Resource` & children destruction
  task<void> destroy_resource() {
    co_await destroy_this_resource_(this);
    for (auto* child = children_; child != nullptr; child = child->next_) {
      co_await child->destroy_resource();
    }
  }

  // synchronous part of `Resource` & children destruction: deallocation
  void destruct_container() noexcept {
    if (state() == state_t::DESTROYING) {
      destruct_this_resource_(this);
      scope_.destruct();
      _container_base* child = children_;
      while (child != nullptr) {
        auto next = child->next_;
        child->destruct_container();
        child = next;
      }
    }
    deleter_(this);
  }

  void deregister_child(_container_base* child) noexcept {
    std::lock_guard lock{mutex_};
    for (_container_base** head = &children_; *head; head = &((*head)->next_)) {
      if (*head == child) {
        *head = (*head)->next_;
        return;
      }
    }
    UNIFEX_ASSERT(false);  // not found
  }

  // pre-spawned async_destroy CPO prior to _container construction
  auto destruct_impl() noexcept {
    return let_value(destructionEvt_.async_wait(), [this]() noexcept {
      bool value = true;
      switch (state()) {
        case state_t::ALLOCATED:
          // scheduled destruct moved the state from ALLOCATED
          UNIFEX_ASSERT(false);
          break;
        case state_t::CONSTRUCTED: value = false; break;
        case state_t::DESTRUCT_SPAWNED:
          if (!parent_) {
            deleter_(this);
          }
          break;
        case state_t::DESTROYING:
          // parent tearing down
          break;
      }
      return just_void_or_done(value) |
          let_done(
                 [this]() noexcept(noexcept(let_done(
                     async_destroy(UNIFEX_DECLVAL(_container_base&)), just))) {
                   // TODO remove let_done once any_sender_of<>(s) are removed
                   return let_done(async_destroy(*this), just);
                 });
    });
  }

public:
  _container_base(
      void (*destruct_this_resource)(_container_base*) noexcept,
      any_sender_of<> (*schedule_destruct)(_container_base*, _container_base*),
      any_sender_of<> (*destroy_this_resource)(_container_base*),
      void (*deleter)(_container_base*) noexcept) noexcept
    : destruct_this_resource_(destruct_this_resource)
    , schedule_destruct_(schedule_destruct)
    , destroy_this_resource_(destroy_this_resource)
    , deleter_(deleter)
    , children_(nullptr)
    , next_(nullptr)
    , parent_(nullptr)
    , state_(state_t::ALLOCATED) {}

  state_t state() const noexcept {
    // TODO optimize `memory_order_seq_cst`
    return state_.load(std::memory_order_seq_cst);
  }

  // recursively join() self and child scopes
  task<void> close_scope() noexcept {
    if (auto expected = state_t::CONSTRUCTED; state_.compare_exchange_strong(
            expected, state_t::DESTROYING, std::memory_order_seq_cst)) {
      destructionEvt_.set();
      co_await when_all(scope_.get().join(), close_child_scopes());
    }
  }

  // happens when _ptr is dropped (non-nullptr)
  // destroys from the root of tree post-order (objects created later might have
  // references to objects created earlier, opposite is not true):
  // 1. close all scopes
  // 2. async destroy all resources
  // 3. delete all containers
  task<void> destroy() noexcept {
    if (parent_) {
      parent_->deregister_child(this);
    }
    co_await close_scope();
    co_await destroy_resource();
    destruct_container();
  }

  // accessor to async_destroy CPO, essentially:
  // on(async_destroy) via type erased schedule_destruct_
  any_sender_of<> destruct(_container_base* parent) {
    static_assert(
        !sender_traits<decltype(destruct_impl())>::sends_done,
        "destruct() must be unstoppable");
    return schedule_destruct_(this, parent);
  }

  void register_child(_container_base* child) noexcept {
    // terminate if cannot lock mutex_
    std::lock_guard lock{mutex_};
    child->parent_ = this;
    child->next_ = std::exchange(children_, child);
  }

  task<void> handle_construction_failure() noexcept {
    switch (state()) {
      case state_t::ALLOCATED: deleter_(this); break;
      case state_t::DESTRUCT_SPAWNED: destructionEvt_.set(); break;
      case state_t::CONSTRUCTED:
        if (parent_ && parent_->state() == state_t::DESTROYING) {
          co_await close_scope();
          // bubble up cancellation, prevent returning an empty _ptr
          co_await just_done();
        }
        break;
      case state_t::DESTROYING:
        // do nothing, ownership handed over to destroy()
        break;
    }
  }
};

class async_scope_ref final {
  Scope* scope_;
  _container_base* container_;

public:
  /*implicit*/ async_scope_ref(Scope& scope) noexcept
    : async_scope_ref(scope, nullptr) {}  // unmanaged scope

  explicit async_scope_ref(Scope& scope, _container_base* container) noexcept
    : scope_(&scope)
    , container_(container) {}

  void register_child(_container_base* child) {
    UNIFEX_ASSERT(child != nullptr);
    spawn_detached(child->destruct(container_), *scope_);
  }

  template <typename Sender>
  [[nodiscard]] auto nest(Sender&& sender) noexcept(
      noexcept(scope_->nest(static_cast<Sender&&>(sender))))
      -> decltype(scope_->nest(static_cast<Sender&&>(sender))) {
    return scope_->nest(static_cast<Sender&&>(sender));
  }

  friend bool operator==(async_scope_ref lhs, async_scope_ref rhs) noexcept {
    return lhs.scope_ == rhs.scope_ && lhs.container_ == rhs.container_;
  }

  friend bool operator!=(async_scope_ref lhs, async_scope_ref rhs) noexcept {
    return !(lhs == rhs);
  }
};

template <typename Resource, typename Scheduler>
struct _container final : _container_base {
  explicit _container(Scheduler scheduler) noexcept(
    std::is_nothrow_move_constructible_v<Scheduler>)
      : _container_base{destruct_this_resource,  //
                        schedule_destruct,       //
                        destroy_this_resource,   //
                        deleter},
        scheduler_(std::move(scheduler)) {}

  template <typename Sender>
  auto schedule_construct(Sender&& factory) noexcept {
    return on(
        scheduler_,
        sequence(
            just_from([&]() noexcept { scope_.construct(); }),
            std::move(factory),
            just_from([&]() noexcept { return set_constructed(); })));
  }

  template <typename ResourceFactory>
  auto construct(ResourceFactory&& factory) noexcept {
    if constexpr (noexcept(
                      factory(std::declval<async_scope_ref>(), scheduler_))) {
      return schedule_construct(
          just_from([factory = static_cast<ResourceFactory&&>(factory),
                     this]() mutable noexcept {
            resource_.construct_with([&]() noexcept {
              return factory(async_scope_ref{scope_.get(), this}, scheduler_);
            });
          }));
    } else {
      return schedule_construct(let_error(
          just_from([factory = static_cast<ResourceFactory&&>(factory),
                     this]() mutable {
            resource_.construct_with([&]() {
              return factory(async_scope_ref{scope_.get(), this}, scheduler_);
            });
          }),
          [&](auto e) {
            return sequence(
                scope_.get().join(),
                just_from([&]() noexcept { scope_.destruct(); }),
                just_error(e));
          }));
    }
  }

  template <typename ResourceFactory>
  auto construct_as_sender(ResourceFactory&& factory) noexcept {
    return schedule_construct(let_error(
        defer(
            [factory = static_cast<ResourceFactory&&>(factory), this]() mutable noexcept(
                noexcept(
                    factory(std::declval<async_scope_ref>(), scheduler_))) {
              return then(
                  factory(async_scope_ref{scope_.get(), this}, scheduler_),
                  [&](auto&&... args) noexcept(std::is_nothrow_constructible_v<
                                               Resource,
                                               decltype(args)...>) {
                    resource_.construct(static_cast<decltype(args)>(args)...);
                  });
            }),
        [&](auto e) {
          return sequence(
              scope_.get().join(),
              just_from([&]() noexcept { scope_.destruct(); }),
              just_error(e));
        }));
  }

  async_resource_ptr<Resource> ptr() noexcept {
    return {&resource_.get(), &destructionEvt_};
  }

private:
  _container* set_constructed() noexcept {
    [[maybe_unused]] auto old =
        state_.exchange(state_t::CONSTRUCTED, std::memory_order_seq_cst);
    UNIFEX_ASSERT(old == state_t::DESTRUCT_SPAWNED);
    return this;
  }

  UNIFEX_NO_UNIQUE_ADDRESS Scheduler scheduler_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<Resource> resource_;

  //
  // passed to _container_base as func pointers
  //
  static void destruct_this_resource(_container_base* base) noexcept {
    UNIFEX_ASSERT(base != nullptr);
    auto& self = static_cast<_container&>(*base);
    self.resource_.destruct();
  }

  // TODO potential improvmement to avoid the any_sender_of<> static method:
  // 1. add operation states to the _container
  // 2. maybe a union
  static any_sender_of<>
  schedule_destruct(_container_base* base, _container_base* parent) {
    UNIFEX_ASSERT(base != nullptr);
    auto& self = static_cast<_container&>(*base);
    return sequence(
        just_from([&self, parent]() noexcept {
          [[maybe_unused]] auto old = self.state_.exchange(
              state_t::DESTRUCT_SPAWNED, std::memory_order_seq_cst);
          UNIFEX_ASSERT(old == state_t::ALLOCATED);
          if (parent) {
            parent->register_child(&self);
          }
        }),
        unstoppable(on(self.scheduler_, self.destruct_impl())));
  }

  static any_sender_of<> destroy_this_resource(_container_base* base) {
    UNIFEX_ASSERT(base != nullptr);
    auto& self = static_cast<_container&>(*base);
    return with_query_value(
        let_done(async_destroy(self.resource_.get()), just),
        get_scheduler,
        self.scheduler_);
  }

  static void deleter(_container_base* base) noexcept {
    UNIFEX_ASSERT(base != nullptr);
    auto* self = static_cast<_container*>(base);
    delete self;
  }
};  // namespace unifex

template <
    typename Resource = void,
    typename Scheduler,
    typename ResourceFactory>
auto make_async_resource(
    Scheduler scheduler,
    async_scope_ref outerScope,
    ResourceFactory&& factory) {
  using factory_result_t =
      decltype(factory(std::declval<async_scope_ref>(), scheduler));
  auto result = []() noexcept {
    if constexpr (sender<factory_result_t>) {
      static_assert(
          !std::is_void_v<Resource>,
          "Sender returning ResourceFactory must specify Resource");
      return single_overload<Resource>{};
    } else {
      static_assert(same_as<Resource, void>, "must not specify Resource");
      return single_overload<factory_result_t>{};
    }
  };
  using resource_t = typename decltype(result())::type;
  // TODO support passing allocators
  auto* container = new _container<resource_t, Scheduler>{scheduler};
  auto construct = [&]() {
    if constexpr (sender<factory_result_t>) {
      return container->construct_as_sender(
          static_cast<ResourceFactory&&>(factory));
    } else {
      return container->construct(static_cast<ResourceFactory&&>(factory));
    }
  }();
  outerScope.register_child(container);
  return finally(
             nest(std::move(construct), outerScope),
             container->handle_construction_failure()) |
      then([](auto* container) noexcept { return container->ptr(); });
}
}  // namespace _async_resource
using _async_resource::async_scope_ref;
using _async_resource::make_async_resource;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
