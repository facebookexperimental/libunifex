/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/io_concepts.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/pipe_concepts.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/span.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/cstddef.hpp>

#include <unifex/detail/atomic_intrusive_queue.hpp>
#include <unifex/detail/intrusive_list.hpp>
#include <unifex/detail/intrusive_stack.hpp>

#include <unifex/win32/detail/ntapi.hpp>
#include <unifex/win32/detail/safe_handle.hpp>
#include <unifex/win32/detail/types.hpp>

#include <array>
#include <cstdint>
#include <system_error>
#include <thread>
#include <tuple>

#include <unifex/detail/prologue.hpp>

namespace unifex::win32
{
  class low_latency_iocp_context {
    class scheduler;

    class readable_byte_stream;
    class writable_byte_stream;

    class schedule_sender;

    template <typename Receiver>
    struct _schedule_op {
      class type;
    };
    template <typename Receiver>
    using schedule_op = typename _schedule_op<Receiver>::type;

    template <typename Buffer>
    struct _read_file_sender {
      class type;
    };
    template <typename Buffer>
    using read_file_sender = typename _read_file_sender<Buffer>::type;

    template <typename Buffer>
    struct _write_file_sender {
      class type;
    };
    template <typename Buffer>
    using write_file_sender = typename _write_file_sender<Buffer>::type;

    template <typename Buffer, typename Receiver>
    struct _read_file_op {
      class type;
    };
    template <typename Buffer, typename Receiver>
    using read_file_op = typename _read_file_op<Buffer, Receiver>::type;

    template <typename Buffer, typename Receiver>
    struct _write_file_op {
      class type;
    };
    template <typename Buffer, typename Receiver>
    using write_file_op = typename _write_file_op<Buffer, Receiver>::type;

  public:
    // Initialise the IOCP context and pre-allocate storage for I/O
    // state needed to support at most 'maxIoOperations' concurrent I/O
    // operations.
    explicit low_latency_iocp_context(std::size_t maxIoOperations);

    low_latency_iocp_context(low_latency_iocp_context&&) = delete;
    low_latency_iocp_context(const low_latency_iocp_context&) = delete;
    low_latency_iocp_context&
    operator=(const low_latency_iocp_context&) = delete;
    low_latency_iocp_context& operator=(low_latency_iocp_context&&) = delete;

    ~low_latency_iocp_context();

    // Drive the event loop until a request to stop is communicated via
    // the passed StopToken.
    template <typename StopToken>
    void run(StopToken st);

    // Obtain a handle to this execution context that can be used to
    // schedule work and open I/O resources.
    scheduler get_scheduler() noexcept;

  private:
    struct vectored_io_state;

    // This value chosen so that vectored_io_state is 512 bytes on 64-bit
    // architectures and 256 bytes on 32-bit architectures.
    static constexpr std::size_t max_vectored_io_size = 30;

    struct operation_base {
      explicit operation_base(low_latency_iocp_context& ctx) noexcept
        : context(ctx) {}

      using callback_t = void(operation_base*) noexcept;
      low_latency_iocp_context& context;
      callback_t* callback = nullptr;
      operation_base* next = nullptr;
      operation_base* prev = nullptr;
    };

    using operation_queue = intrusive_list<
        operation_base,
        &operation_base::next,
        &operation_base::prev>;

    struct io_operation : operation_base {
      explicit io_operation(
          low_latency_iocp_context& context,
          handle_t fileHandle,
          bool skipNotificationOnSuccess) noexcept
        : operation_base(context)
        , fileHandle(fileHandle)
        , skipNotificationOnSuccess(skipNotificationOnSuccess)
        , ioState(nullptr) {}

      const handle_t fileHandle;
      const bool skipNotificationOnSuccess;
      vectored_io_state* ioState;

      // Cancel outstanding I/O operations (if any)
      void cancel_io() noexcept;

      // Poll for whether or not the operation has completed.
      // Returns 'true' if the operation is completed (in which case the
      // operation has 'acquire' semantics), 'false' otherwise.
      bool is_complete() noexcept;

      // Start reading the next 'buffer.size()' bytes into 'buffer'.
      //
      // Returns 'true' if a additional read-operations can be submitted.
      // This will only be the case if all of the following are true:
      // - a read of the entirety of 'buffer' was successfully started
      // - we haven't reached the maximum number of I/O operations in
      //   this batch.
      //
      // Once all I/O operations have been started, the caller should
      // schedule a 'poll' of this operation by setting this->callback
      // and calling context.schedule_poll(this). This will schedule the
      // callback to be run immediately (if it completed synchronously).
      bool start_read(span<unifex::byte> buffer) noexcept;

      // Start writing the context of 'buffer' to fileHandle.
      bool start_write(span<const unifex::byte> buffer) noexcept;

      std::size_t get_result(std::error_code& ec) noexcept;
    };

    struct vectored_io_state {
      // Parent operation.
      //
      // May be nullptr if this operation already completed due to a poll.
      // If this is the case then when all pending completion notifications
      // are received then this will just go straight back on the free-list.
      io_operation* parent = nullptr;

      // Intrusive list ptr.
      vectored_io_state* next = nullptr;
      vectored_io_state* prev = nullptr;

      // Total number of operations started
      std::uint8_t operationCount = 0;

      // Number of operations not yet received completion-notification
      // via the IOCP. The vectored_io_state structure is not free to
      // be reused until this number reaches zero.
      std::uint8_t pendingCompletionNotifications = 0;

      // Whether or not the 'parent' has already been notified of completion.
      bool completed = false;

      ntapi::IO_STATUS_BLOCK operations[max_vectored_io_size];
    };

    struct stop_operation : operation_base {
      explicit stop_operation(low_latency_iocp_context& ctx) noexcept
        : operation_base(ctx) {
        this->callback = &request_stop_callback;
      }

      ~stop_operation() {
        if (isEnqueued) {
          // Flush any items in the remote-queue into the ready queue
          // just in case this operation is still in the remote queue.
          (void)context.try_dequeue_remote_work();

          // This operation should now be in the ready queue, remove it.
          context.readyQueue_.remove(this);
        }
      }

      void start() noexcept {
        if (context.is_running_on_io_thread()) {
          stopRequestedFlag = true;
        } else {
          isEnqueued = true;
          context.schedule_remote(this);
        }
      }

      static void request_stop_callback(operation_base* op) noexcept {
        auto& self = *static_cast<stop_operation*>(op);
        self.stopRequestedFlag = true;
        self.isEnqueued = false;
      }

      bool stopRequestedFlag = false;

    private:
      bool isEnqueued = false;
    };

    struct stop_callback {
      stop_operation& op;

      void operator()() noexcept { op.start(); }
    };

    void run_impl(bool& stopFlag);

    // Dequeue items from remote queue and move them to the
    // ready-to-run queue.
    // Returns true if any items were dequeued.
    bool try_dequeue_remote_work() noexcept;

    bool poll_is_complete(vectored_io_state& state) noexcept;

    // Obtain the I/O state that contains a given io_status_block structure.
    vectored_io_state* to_io_state(ntapi::IO_STATUS_BLOCK* io) noexcept;

    bool is_running_on_io_thread() const noexcept {
      return activeThreadId_.load(std::memory_order_relaxed) ==
          std::this_thread::get_id();
    }

    void schedule(operation_base* op) noexcept;
    void schedule_local(operation_base* op) noexcept;
    void schedule_remote(operation_base* op) noexcept;

    // If an I/O state is available, attaches an unused I/O state to
    // op->ioState and returns true, otherwise returns false if no
    // I/O state is currently available.
    //
    // If 'true' is returned then the caller can populate op->ioState
    // and initiate the I/O using its IO_STATUS_BLOCK structures.
    [[nodiscard]] bool try_allocate_io_state_for(io_operation* op) noexcept;

    // Schedule the specified 'op' to be called back (calling op->callback)
    // when an I/O state becomes available and has been allocated to 'op'.
    //
    // Do this only after an unsuccessful call to try_allocate_io_state_for()
    // asynchronously wait until some other I/O operation completes and frees
    // up an other I/O state.
    void schedule_when_io_state_available(io_operation* op) noexcept;

    // Mark the io state as released and let it return to the pool.
    void release_io_state(vectored_io_state* state) noexcept;

    // Schedule this operation to be polled next time we run out of work
    // in the ready-queue before going back to the OS for completion-events.
    void schedule_poll_io(io_operation* op) noexcept;

    // Attempt to associate the specified file-handle with this I/O context
    // so that its I/O completion events are posted to this context's IOCP.
    void associate_file_handle(handle_t fileHandle);

  private:
    ////
    // State that won't change after initialisation or that change rarely
    // e.g. only on each call to run().

    std::atomic<std::thread::id> activeThreadId_;
    safe_handle iocp_;
    std::size_t ioPoolSize_;
    std::unique_ptr<vectored_io_state[]> ioPool_;

    /////
    // State that is accessed/modified by the I/O thread only.

    alignas(64) intrusive_stack<
        vectored_io_state,
        &vectored_io_state::next> ioFreeList_;

    // Newly launched operations that we want to poll for completion as soon as
    // we run out of 'ready-to-run' work to do.
    operation_queue pollQueue_;

    // Operations waiting to acquire a vectored_io_operation.
    // These will be resumed in-order as vectored_io_operations are returned to
    // the pool.
    operation_queue pendingIoQueue_;

    // Operations that are ready to run.
    operation_queue readyQueue_;

    /////
    // State that may be accessed/modified by other threads.

    alignas(64) atomic_intrusive_queue<
        operation_base,
        &operation_base::next> remoteQueue_;
  };

  template <typename StopToken>
  void low_latency_iocp_context::run(StopToken stopToken) {
    stop_operation op{*this};
    typename StopToken::template callback_type<stop_callback> cb{
        std::move(stopToken), stop_callback{op}};
    run_impl(op.stopRequestedFlag);
  }

  ///////////////////////////
  // schedule

  template <typename Receiver>
  class low_latency_iocp_context::_schedule_op<Receiver>::type
    : private low_latency_iocp_context::operation_base {
  public:
    template <typename Receiver2>
    explicit type(low_latency_iocp_context& context, Receiver2&& r)
      : operation_base(context)
      , receiver_((Receiver2 &&) r) {
      this->callback = &execute_callback;
    }

    void start() & noexcept { this->context.schedule(this); }

  private:
    static void execute_callback(operation_base* op) noexcept {
      auto& self = *static_cast<type*>(op);

      if constexpr (!is_stop_never_possible_v<stop_token_type_t<Receiver>>) {
        if (get_stop_token(self.receiver_).stop_requested()) {
          unifex::set_done(std::move(self.receiver_));
          return;
        }
      }

      if constexpr (is_nothrow_callable_v<
                        decltype(unifex::set_value),
                        Receiver>) {
        unifex::set_value(std::move(self.receiver_));
      } else {
        UNIFEX_TRY {
          unifex::set_value(std::move(self.receiver_));
        } UNIFEX_CATCH (...) {
          unifex::set_error(
              std::move(self.receiver_), std::current_exception());
        }
      }
    }

    Receiver receiver_;
  };

  class low_latency_iocp_context::schedule_sender {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    explicit schedule_sender(low_latency_iocp_context& context) noexcept
      : context_(context) {}

    template(typename Receiver)(requires receiver_of<Receiver>) friend auto tag_invoke(
        tag_t<connect>,
        const schedule_sender& s,
        Receiver&& r) noexcept(std::
                                   is_nothrow_constructible_v<
                                       remove_cvref_t<Receiver>,
                                       Receiver>)
        -> schedule_op<remove_cvref_t<Receiver>> {
      return schedule_op<remove_cvref_t<Receiver>>{s.context_, (Receiver &&) r};
    }

  private:
    low_latency_iocp_context& context_;
  };

  ///////////////////////////
  // read_file

  template <typename Buffer, typename Receiver>
  class low_latency_iocp_context::_read_file_op<Buffer, Receiver>::type
    : private low_latency_iocp_context::io_operation {
  public:
    template <typename Receiver2, typename Buffer2>
    explicit type(
        low_latency_iocp_context& ctx,
        handle_t fileHandle,
        bool skipNotificationOnSuccess,
        Buffer2&& buffer,
        Receiver2&& r)
      : io_operation(ctx, fileHandle, skipNotificationOnSuccess)
      , receiver_((Receiver2 &&) r)
      , buffer_((Buffer2 &&) buffer) {}

    void start() & noexcept {
      if (this->context.is_running_on_io_thread()) {
        acquire_io_state(this);
      } else {
        this->callback = &acquire_io_state;
        this->context.schedule_remote(this);
      }
    }

  private:
    struct cancel_callback {
      type& op;
      void operator()() noexcept { op.cancel_io(); }
    };

    static void acquire_io_state(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);
      if (self.context.try_allocate_io_state_for(&self)) {
        start_io(op);
      } else {
        // TODO: Add support for cancellation while waiting in the
        // pendingIoQueue_.

        self.callback = &start_io;
        self.context.schedule_when_io_state_available(&self);
      }
    }

    static void start_io(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);
      UNIFEX_ASSERT(self.context.is_running_on_io_thread());

      // TODO: Add support for a BufferSequence concept to allow
      // vectorised I/O here.
      // For now, just assume that 'Buffer' is convertible to a
      // 'span<unifex::byte>'.

      self.start_read(self.buffer_);

      if (self.ioState->pendingCompletionNotifications == 0) {
        self.ioState->completed = true;
        self.callback = &on_complete;
        self.context.readyQueue_.push_front(op);
      } else {
        if constexpr (!is_stop_never_possible_v<stop_token_type_t<Receiver>>) {
          self.stopCallback_.construct(
              get_stop_token(self.receiver_), cancel_callback{self});
          self.callback = &on_cancellable_complete;
        } else {
          self.callback = &on_complete;
        }

        self.context.schedule_poll_io(&self);
      }
    }

    static void on_cancellable_complete(operation_base* op) noexcept {
      auto& self = *static_cast<type*>(op);
      UNIFEX_ASSERT(self.context.is_running_on_io_thread());

      self.stopCallback_.destruct();

      on_complete(op);
    }

    static void on_complete(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);

      std::error_code ec;
      std::size_t totalBytesTransferred = self.get_result(ec);

      self.context.release_io_state(self.ioState);

      // Treat partial failure as a success case.
      // TODO: Should we be sending tuple of (bytesTransferred, ec) here
      // instead?
      if (!ec || totalBytesTransferred > 0) {
        if constexpr (is_nothrow_callable_v<
                          decltype(set_value),
                          Receiver,
                          std::size_t>) {
          unifex::set_value(std::move(self.receiver_), totalBytesTransferred);
        } else {
          UNIFEX_TRY {
            unifex::set_value(std::move(self.receiver_), totalBytesTransferred);
          } UNIFEX_CATCH (...) {
            unifex::set_error(
                std::move(self.receiver_), std::current_exception());
          }
        }
      } else if (ec == std::errc::operation_canceled) {
        unifex::set_done(std::move(self.receiver_));
      } else {
        unifex::set_error(std::move(self.receiver_), std::move(ec));
      }
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS Buffer buffer_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver>::template callback_type<cancel_callback>>
        stopCallback_;
  };

  template <typename Buffer>
  class low_latency_iocp_context::_read_file_sender<Buffer>::type {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<std::size_t>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr, std::error_code>;

    static constexpr bool sends_done = true;

    template <typename Buffer2>
    explicit type(
        low_latency_iocp_context& context,
        handle_t fileHandle,
        bool skipNotificationsOnSuccess,
        Buffer2&& buffer)
      : context_(context)
      , fileHandle_(fileHandle)
      , skipNotificationsOnSuccess_(skipNotificationsOnSuccess)
      , buffer_((Buffer2 &&) buffer) {}

    template(typename Receiver)(requires receiver_of<Receiver, std::size_t>) friend auto tag_invoke(
        tag_t<unifex::connect>,
        type&& self,
        Receiver&& r) noexcept(is_nothrow_move_constructible_v<Buffer>&&
                                   is_nothrow_constructible_v<
                                       remove_cvref_t<Receiver>,
                                       Receiver>)
        -> read_file_op<Buffer, remove_cvref_t<Receiver>> {
      return read_file_op<Buffer, remove_cvref_t<Receiver>>{
          self.context_,
          self.fileHandle_,
          self.skipNotificationsOnSuccess_,
          std::move(self.buffer_),
          (Receiver &&) r};
    }

  private:
    low_latency_iocp_context& context_;
    handle_t fileHandle_;
    bool skipNotificationsOnSuccess_;
    Buffer buffer_;
  };

  //////////////////////////
  // write_file

  template <typename Buffer, typename Receiver>
  class low_latency_iocp_context::_write_file_op<Buffer, Receiver>::type
    : private low_latency_iocp_context::io_operation {
  public:
    template <typename Receiver2, typename Buffer2>
    explicit type(
        low_latency_iocp_context& ctx,
        handle_t fileHandle,
        bool skipNotificationOnSuccess,
        Buffer2&& buffer,
        Receiver2&& r)
      : io_operation(ctx, fileHandle, skipNotificationOnSuccess)
      , receiver_((Receiver2 &&) r)
      , buffer_((Buffer2 &&) buffer) {}

    void start() & noexcept {
      if (this->context.is_running_on_io_thread()) {
        acquire_io_state(this);
      } else {
        this->callback = &acquire_io_state;
        this->context.schedule_remote(this);
      }
    }

  private:
    struct cancel_callback {
      type& op;
      void operator()() noexcept { op.cancel_io(); }
    };

    static void acquire_io_state(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);
      UNIFEX_ASSERT(self.context.is_running_on_io_thread());

      if (self.context.try_allocate_io_state_for(&self)) {
        start_io(op);
      } else {
        // TODO: Add support for cancellation while waiting in the
        // pendingIoQueue_.

        self.callback = &start_io;
        self.context.schedule_when_io_state_available(&self);
      }
    }

    static void start_io(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);
      UNIFEX_ASSERT(self.context.is_running_on_io_thread());

      // TODO: Add support for a BufferSequence concept to allow
      // vectorised I/O here.
      // For now, just assume that 'Buffer' is convertible to a
      // 'span<unifex::byte>'.

      self.start_write(self.buffer_);

      if (self.ioState->pendingCompletionNotifications == 0) {
        self.ioState->completed = true;
        self.callback = &on_complete;
        self.context.readyQueue_.push_front(op);
      } else {
        if constexpr (!is_stop_never_possible_v<stop_token_type_t<Receiver>>) {
          self.stopCallback_.construct(
              get_stop_token(self.receiver_), cancel_callback{self});
          self.callback = &on_cancellable_complete;
        } else {
          self.callback = &on_complete;
        }

        self.context.schedule_poll_io(&self);
      }
    }

    static void on_cancellable_complete(operation_base* op) noexcept {
      auto& self = *static_cast<type*>(op);
      UNIFEX_ASSERT(self.context.is_running_on_io_thread());

      self.stopCallback_.destruct();

      on_complete(op);
    }

    static void on_complete(operation_base* op) noexcept {
      type& self = *static_cast<type*>(op);

      std::error_code ec;
      std::size_t totalBytesTransferred = self.get_result(ec);

      self.context.release_io_state(self.ioState);

      // Treat partial failure as a success case.
      // TODO: Should we be sending tuple of (bytesTransferred, ec) here
      // instead?
      if (!ec || totalBytesTransferred > 0) {
        if constexpr (is_nothrow_callable_v<
                          decltype(set_value),
                          Receiver,
                          std::size_t>) {
          unifex::set_value(std::move(self.receiver_), totalBytesTransferred);
        } else {
          UNIFEX_TRY {
            unifex::set_value(std::move(self.receiver_), totalBytesTransferred);
          } UNIFEX_CATCH (...) {
            unifex::set_error(
                std::move(self.receiver_), std::current_exception());
          }
        }
      } else if (ec == std::errc::operation_canceled) {
        unifex::set_done(std::move(self.receiver_));
      } else {
        unifex::set_error(std::move(self.receiver_), std::move(ec));
      }
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS Buffer buffer_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
        Receiver>::template callback_type<cancel_callback>>
        stopCallback_;
  };

  template <typename Buffer>
  class low_latency_iocp_context::_write_file_sender<Buffer>::type {
  public:
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<std::size_t>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr, std::error_code>;

    static constexpr bool sends_done = true;

    template <typename Buffer2>
    explicit type(
        low_latency_iocp_context& context,
        handle_t fileHandle,
        bool skipNotificationsOnSuccess,
        Buffer2&& buffer)
      : context_(context)
      , fileHandle_(fileHandle)
      , skipNotificationsOnSuccess_(skipNotificationsOnSuccess)
      , buffer_((Buffer2 &&) buffer) {}

    template(typename Receiver)(requires receiver_of<Receiver, std::size_t>) friend auto tag_invoke(
        tag_t<unifex::connect>,
        type&& self,
        Receiver&& r) noexcept(is_nothrow_move_constructible_v<Buffer>&&
                                   is_nothrow_constructible_v<
                                       remove_cvref_t<Receiver>,
                                       Receiver>)
        -> write_file_op<Buffer, remove_cvref_t<Receiver>> {
      return write_file_op<Buffer, remove_cvref_t<Receiver>>{
          self.context_,
          self.fileHandle_,
          self.skipNotificationsOnSuccess_,
          std::move(self.buffer_),
          (Receiver &&) r};
    }

  private:
    low_latency_iocp_context& context_;
    handle_t fileHandle_;
    bool skipNotificationsOnSuccess_;
    Buffer buffer_;
  };

  class low_latency_iocp_context::readable_byte_stream {
  public:
    explicit readable_byte_stream(
        low_latency_iocp_context& context, safe_handle fileHandle) noexcept
      : context_(context)
      , fileHandle_(std::move(fileHandle)) {}

    template(typename Buffer)(requires convertible_to<Buffer, span<unifex::byte>>) friend read_file_sender<
        remove_cvref_t<
            Buffer>> tag_invoke(tag_t<async_read_some>, readable_byte_stream& stream, Buffer&& buffer) {
      return read_file_sender<remove_cvref_t<Buffer>>{
          stream.context_, stream.fileHandle_.get(), true, (Buffer &&) buffer};
    }

  private:
    low_latency_iocp_context& context_;
    safe_handle fileHandle_;
  };

  class low_latency_iocp_context::writable_byte_stream {
  public:
    explicit writable_byte_stream(
        low_latency_iocp_context& context, safe_handle fileHandle) noexcept
      : context_(context)
      , fileHandle_(std::move(fileHandle)) {}

    template(typename Buffer)(requires convertible_to<Buffer, span<const unifex::byte>>) friend write_file_sender<
        remove_cvref_t<
            Buffer>> tag_invoke(tag_t<async_write_some>, writable_byte_stream& stream, Buffer&& buffer) {
      return write_file_sender<remove_cvref_t<Buffer>>{
          stream.context_, stream.fileHandle_.get(), true, (Buffer &&) buffer};
    }

  private:
    low_latency_iocp_context& context_;
    safe_handle fileHandle_;
  };

  class low_latency_iocp_context::scheduler {
  public:
    explicit scheduler(low_latency_iocp_context& context) noexcept
      : context_(&context) {}

    schedule_sender schedule() const noexcept {
      return schedule_sender{*context_};
    }

    friend std::tuple<readable_byte_stream, writable_byte_stream>
    tag_invoke(tag_t<unifex::open_pipe>, scheduler s) {
      return open_pipe_impl(*s.context_);
    }

    friend bool operator==(scheduler a, scheduler b) noexcept {
      return a.context_ == b.context_;
    }

    friend bool operator!=(scheduler a, scheduler b) noexcept {
      return !(a == b);
    }

  private:
    static std::tuple<readable_byte_stream, writable_byte_stream>
    open_pipe_impl(low_latency_iocp_context& ctx);

    low_latency_iocp_context* context_;
  };

  inline low_latency_iocp_context::scheduler
  low_latency_iocp_context::get_scheduler() noexcept {
    return scheduler{*this};
  }

}  // namespace unifex::win32

#include <unifex/detail/epilogue.hpp>
