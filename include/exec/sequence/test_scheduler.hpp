/*
 * Copyright (c) 2024 Maikel Nadolski
 * Copyright (c) 2024 NVIDIA Corporation
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

#include "../detail/intrusive_heap.hpp"
#include "../timed_scheduler.hpp"
#include "./marbles.hpp"

#include "../../stdexec/__detail/__atomic.hpp"
#include "../../stdexec/__detail/__intrusive_mpsc_queue.hpp"
#include "exec/finally.hpp"

#include "../detail/basic_sequence.hpp"
#include "exec/sequence.hpp"
#include "exec/sequence_senders.hpp"
#include "exec/variant_sender.hpp"
#include "stdexec/__detail/__completion_signatures.hpp"
#include "stdexec/__detail/__concepts.hpp"
#include "stdexec/__detail/__execution_fwd.hpp"
#include "stdexec/__detail/__meta.hpp"
#include "stdexec/__detail/__receivers.hpp"
#include "stdexec/__detail/__senders.hpp"
#include "stdexec/__detail/__stop_token.hpp"
#include "stdexec/__detail/__type_traits.hpp"

#include <chrono>
#include <cstddef>
#include <deque>

namespace experimental::execution {
  class test_scheduler;

  //
  // test_context implements a time-scheduler with additional test features.
  //
  // test_context is used to build tests using marble sequence senders
  // and marble recorders
  //
  // test_clock_context & test_clock are used to represent time for the tests
  //
  // test_scheduler is used to queue tasks on the test_context at virtual-time points
  //
  // One or more __test_sequence(s) are constructed from marble
  //   diagrams and each schedules marbles on the test_scheduler
  //   when connected and started
  //
  // Once one or more __test_sequence(s) have been composed into an expression,
  //   the test_context is used to record the expression results as a
  //   set of marbles. To support the testing of infinite sequences
  //   the recording will be requested to stop at a default of 1000ms from
  //   start() if the expression has not completed.
  //
  // The expression result marbles are then compared to an expected
  //   set of marbles generated from a separate marble diagram
  //
  // An example of usage:
  //
  // TEST_CASE(
  // "test_scheduler - test_context marble-sequence never",
  // "[sequence_senders][test_scheduler]") {
  //   test_context __test{};
  //   auto __clock = __test.get_clock();
  //   CHECK(test_clock::time_point{0ms} == __clock.now());
  //
  //   // a sequence that will produce '0' at 1ms from start()
  //   // and then never complete
  //   auto __sequence = __test.get_marble_sequence_from(
  //     "  -0-"_mstr);
  //
  //   // the set of marbles for a sequence that contains '5'
  //   // at 1ms from start() and then is externally stopped
  //   // after 1000ms have elapsed since start()
  //   auto expected = get_marbles_from(__clock,
  //     "=^-5 998ms $"_mstr);
  //
  //   // record an expression that is expected to turn '0' to '5'
  //   auto actual = __test.get_marbles_from(
  //                        __sequence
  //                        | then_each([](char c){ return c+5; }));
  //
  //   CHECK(test_clock::time_point{1000ms} == __clock.now());
  //   CAPTURE(__sequence.__marbles_);
  //   CHECK(expected == actual);
  // }
  //

  struct test_clock_context;

  //
  // test_clock and test_clock_context implement a
  // manually advanced virtual-time clock
  //
  // this is used to run tests that depend on time
  // without consuming actual real-time
  //
  // So tests using virtual-time will complete
  // fast in real-time
  //

  struct test_clock {
    using duration = std::chrono::milliseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<test_clock>;
    [[maybe_unused]]
    static const bool is_steady = false;

    const test_clock_context* __context_;

    [[maybe_unused, nodiscard]]
    time_point now() const noexcept;
  };

  struct test_clock_context {
    using duration = typename test_clock::duration;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = typename test_clock::time_point;
    [[maybe_unused]]
    static const bool is_steady = test_clock::is_steady;

    time_point __now_{};

    [[maybe_unused, nodiscard]]
    time_point now() const noexcept {
      return __now_;
    }

    auto advance_now_to(time_point __new_now) noexcept -> time_point {
      time_point __old_now = __now_;
      __now_ = __new_now;
      return __old_now;
    }
    auto advance_now_by(duration __by) noexcept -> time_point {
      time_point __old_now = __now_;
      __now_ += __by;
      return __old_now;
    }
  };

  inline typename test_clock::time_point test_clock::now() const noexcept {
    return __context_->now();
  }


  namespace _tst_sched {

    struct test_operation_base {
      enum class command_type {
        uninitialized,
        schedule,
        stop
      };
      enum class location {
        uninitialized,
        inert,
        in_command_queue,
        in_heap
      };

      ~test_operation_base() noexcept {
        STDEXEC_ASSERT(location_ == location::inert);
      }

      // Default ctor for __intrusive_mpsc_queue's internal stub node
      constexpr test_operation_base() = default;

      test_operation_base(
        void (*set_value)(test_operation_base*) noexcept,
        command_type command = command_type::schedule) noexcept
        : command_{command}
        , set_value_{set_value} {
      }

      STDEXEC::__std::atomic<test_operation_base*> next_{nullptr};
      location location_{location::inert};
      command_type command_;
      void (*set_value_)(test_operation_base*) noexcept;
    };

    template <class Tp>
    struct when_type {
      when_type() = default;

      explicit when_type(Tp tp, std::size_t n = 0) noexcept
        : time_point{std::move(tp)}
        , counter{n} {
      }

      Tp time_point{};
      std::size_t counter{};

      friend auto operator<(const when_type& lhs, const when_type& rhs) noexcept -> bool {
        return lhs.time_point < rhs.time_point
            || (!(rhs.time_point < lhs.time_point) && lhs.counter < rhs.counter);
      }
    };

    struct alignas(64) test_schedule_operation_base : test_operation_base {
      using time_point = test_clock::time_point;

      test_schedule_operation_base(
        time_point tp,
        void (*set_stopped)(test_operation_base*) noexcept,
        void (*set_value)(test_operation_base*) noexcept) noexcept
        : test_operation_base{set_value, command_type::schedule}
        , time_point_{tp}
        , set_stopped_{set_stopped} {
      }

      time_point time_point_;
      // we increase the when counter to ensure that the heap is stable
      // when two operations have the same time_point
      // We do so only when the operation is started, not when it is constructed
      when_type<time_point> when_{};
      test_schedule_operation_base* prev_ = nullptr;
      test_schedule_operation_base* left_ = nullptr;
      test_schedule_operation_base* right_ = nullptr;
      void (*set_stopped_)(test_operation_base*) noexcept;
    };

    struct alignas(64) test_stop_operation : test_operation_base {
      test_stop_operation(
        void (*set_value)(test_operation_base*) noexcept,
        test_schedule_operation_base* target) noexcept
        : test_operation_base{set_value, command_type::stop}
        , target_{target} {
      }

      test_schedule_operation_base* target_;
    };

    template <class Rcvr>
    struct test_schedule_at_op;

    struct __recording_receiver;
    struct __test_sequence;
    struct __test_sequence_operation_context_base;
  } // namespace _tst_sched

  class test_context {
   private:
    static constexpr std::ptrdiff_t context_closed = std::numeric_limits<std::ptrdiff_t>::min() / 2;
   public:
    using duration = test_clock::duration;
    using time_point = test_clock::time_point;

    auto get_scheduler() noexcept -> test_scheduler;
    auto get_clock() const noexcept -> test_clock;
    auto now() const noexcept -> time_point;

    // parse a marble diagram into a set of marbles
    template <std::size_t _Len>
    auto get_marbles_from(experimental::execution::__mstring<_Len> __diagram) noexcept
      -> std::vector<marble_t<test_clock>> {
      return experimental::execution::get_marbles_from(get_clock(), __diagram);
    }

    // record the results of a sequence-sender as a set of marbles
    template <class _Sequence>
    auto get_marbles_from(
      _Sequence&& __sequence,
      typename test_clock::duration __stop_after = std::chrono::milliseconds(1000)) noexcept
      -> std::vector<marble_t<test_clock>>;


    // return a sequence sender that will emit signals specified by the
    // set of marbles provided
    inline auto get_marble_sequence_from(std::vector<marble_t<test_clock>> __marbles) noexcept
      -> __well_formed_sequence_sender auto;

    // parse a marble diagram into a set of marbles and return a sequence
    // sender that will emit those marbles
    template <std::size_t _Len>
    auto get_marble_sequence_from(experimental::execution::__mstring<_Len> __diagram) noexcept
      -> __well_formed_sequence_sender auto;

   private:
    template <class _Receiver>
    friend struct _tst_sched::test_schedule_at_op;

    using command_type = _tst_sched::test_operation_base;
    using task_type = _tst_sched::test_schedule_operation_base;
    using stop_type = _tst_sched::test_stop_operation;

    void process_command_queue() {
      while (command_type* op = command_queue_.pop_front()) {
        STDEXEC_ASSERT(op->location_ == command_type::location::in_command_queue);
        std::exchange(op->location_, command_type::location::inert);
        if (op->command_ == command_type::command_type::schedule) {
          auto* task = static_cast<task_type*>(op);
          task->when_ = _tst_sched::when_type{task->time_point_, submission_counter_++};
          STDEXEC_ASSERT(task->location_ == command_type::location::inert);
          std::exchange(task->location_, command_type::location::in_heap);
          heap_.insert(task);
        } else {
          STDEXEC_ASSERT(op->command_ == command_type::command_type::stop);
          auto* stop_op = static_cast<stop_type*>(op);
          STDEXEC_ASSERT(stop_op->target_->location_ == command_type::location::in_heap);
          bool __erased = heap_.erase(stop_op->target_);
          std::exchange(stop_op->target_->location_, command_type::location::inert);
          if (__erased) {
            stop_op->target_->set_stopped_(stop_op->target_);
          }
          stop_op->set_value_(stop_op);
        }
      }
    }

    void clear_pending() {
      STDEXEC_ASSERT(stop_requested_);
      std::ptrdiff_t expected = 0;
      while (!n_submissions_in_flight_
                .compare_exchange_weak(expected, context_closed, std::memory_order_relaxed)
             && expected > 0) {
        expected = 0;
      }
      task_type* op = heap_.front();
      while (op) {
        STDEXEC_ASSERT(op->location_ == command_type::location::in_heap);
        heap_.pop_front();
        std::exchange(op->location_, command_type::location::inert);
        op->set_stopped_(op);
        op = heap_.front();
      }
    }

    void run() {
      while (true) {
        process_command_queue();
        task_type* op = heap_.front();
        if (!!op) {
          STDEXEC_ASSERT(op->location_ == command_type::location::in_heap);
          heap_.pop_front();
          std::exchange(op->location_, command_type::location::inert);
          if (__clock_.now() < op->time_point_) {
            __clock_.advance_now_to(op->time_point_);
          }
          op->set_value_(op);
          std::exchange(op, nullptr);
        }
        bool stop_requested = stop_requested_;
        ready_ = false;
        if (stop_requested) {
          clear_pending();
          break;
        }
      }
    }

    void schedule(command_type* op) {
      STDEXEC_ASSERT(op->location_ == command_type::location::inert);
      std::ptrdiff_t n = n_submissions_in_flight_.fetch_add(1, std::memory_order_relaxed);
      if (n < 0) {
        if (op->command_ == command_type::command_type::schedule) {
          static_cast<task_type*>(op)->set_stopped_(op);
        } else {
          STDEXEC_ASSERT(op->command_ == command_type::command_type::stop);
          static_cast<stop_type*>(op)->set_value_(op);
        }
        n_submissions_in_flight_
          .compare_exchange_strong(n, context_closed, std::memory_order_relaxed);
        return;
      }
      std::exchange(op->location_, command_type::location::in_command_queue);
      if (command_queue_.push_back(op)) {
        ready_ = true;
      }
      n_submissions_in_flight_.fetch_sub(1, std::memory_order_relaxed);
    }

    void request_stop() {
      stop_requested_ = true;
      process_command_queue();
      clear_pending();
    }

    friend struct _tst_sched::__recording_receiver;
    friend struct _tst_sched::__test_sequence_operation_context_base;

    STDEXEC::__intrusive_mpsc_queue<&command_type::next_> command_queue_;
    intrusive_heap<
      task_type,
      _tst_sched::when_type<time_point>,
      &task_type::when_,
      &task_type::prev_,
      &task_type::left_,
      &task_type::right_
    >
      heap_;
    std::atomic<std::ptrdiff_t> n_submissions_in_flight_{0};
    bool ready_{false};
    bool stop_requested_{false};
    std::size_t submission_counter_{1};
    test_clock_context __clock_;
  };

  namespace _tst_sched {
    template <class _Receiver>
    struct test_schedule_at_op : _tst_sched::test_schedule_operation_base {
     public:

      test_schedule_at_op(test_context& context, test_clock::time_point time_point, _Receiver receiver) noexcept
        : _tst_sched::test_schedule_operation_base{
            time_point,
            [](_tst_sched::test_operation_base* op) noexcept {
              auto* self = static_cast<test_schedule_at_op*>(op);
              int counter = self->ref_count_.fetch_sub(1, std::memory_order_relaxed);
              if (counter == 1) {
                self->stop_callback_.reset();
                STDEXEC::set_stopped(std::move(self->receiver_));
              }
            },
            [](_tst_sched::test_operation_base* op) noexcept {
              auto* self = static_cast<test_schedule_at_op*>(op);
              int counter = self->ref_count_.fetch_sub(1, std::memory_order_relaxed);
              if (counter == 1) {
                self->stop_callback_.reset();
                STDEXEC::set_value(std::move(self->receiver_));
              }
            }}
        , context_{context}
        , receiver_{std::move(receiver)}
        , stop_op_{
            [](_tst_sched::test_operation_base* op) noexcept {
              auto* stop = static_cast<_tst_sched::test_stop_operation*>(op);
              auto* self = static_cast<test_schedule_at_op*>(stop->target_);
              int counter = self->ref_count_.fetch_sub(1, std::memory_order_relaxed);
              if (counter == 1) {
                self->stop_callback_.reset();
                STDEXEC::set_stopped(std::move(self->receiver_));
              }
            },
            this} {
      }

      void start() & noexcept {
        stop_callback_
          .emplace(STDEXEC::get_stop_token(STDEXEC::get_env(receiver_)), on_stopped_t{*this});
        int expected = 0;
        if (ref_count_.compare_exchange_strong(expected, 1, std::memory_order_relaxed)) {
          schedule_this();
        } else {
          stop_callback_.reset();
          STDEXEC::set_stopped(std::move(receiver_));
        }
      }

     private:
      void schedule_this() noexcept {
        context_.schedule(this);
      }

      struct on_stopped_t {
        test_schedule_at_op& self_;

        void operator()() const noexcept {
          self_.request_stop();
        }
      };

      using callback_type = typename STDEXEC::stop_token_of_t<
        STDEXEC::env_of_t<_Receiver>
      >::template callback_type<on_stopped_t>;

      void request_stop() noexcept {
        if (ref_count_.fetch_add(1, std::memory_order_relaxed) == 1) {
          context_.schedule(&stop_op_);
        }
      }

      test_context& context_;
      _Receiver receiver_;
      _tst_sched::test_stop_operation stop_op_;
      std::optional<callback_type> stop_callback_;
      std::atomic<int> ref_count_{0};
    };

  } // namespace _tst_sched

  class test_scheduler {
   public:
    using time_point = test_clock::time_point;
    using duration = test_clock::duration;

    class schedule_at_sender {
     public:
      using sender_concept = STDEXEC::sender_t;
      using completion_signatures =
        STDEXEC::completion_signatures<STDEXEC::set_value_t(), STDEXEC::set_stopped_t()>;

      schedule_at_sender(test_context& context, test_clock::time_point time_point) noexcept
        : context_{&context}
        , time_point_{time_point} {
      }

      [[nodiscard]]
      auto get_env() const noexcept {
        return STDEXEC::prop{
          STDEXEC::get_completion_scheduler<STDEXEC::set_value_t>, test_scheduler{*context_}};
      }

      template <class _Receiver>
      auto connect(_Receiver receiver) const & noexcept ->
        _tst_sched::test_schedule_at_op<_Receiver> {
        return {*context_, time_point_, std::move(receiver)};
      }

     private:
      [[nodiscard]]
      auto get_scheduler() const noexcept -> test_scheduler;

      test_context* context_;
      test_clock::time_point time_point_;
    };

    explicit test_scheduler(test_context& context) noexcept
      : context_{&context} {
    }

    [[nodiscard]]
    auto now() const noexcept -> time_point {
      return context_->now();
    }

    [[nodiscard]]
    auto schedule_at(time_point tp) const noexcept -> schedule_at_sender {
      return schedule_at_sender{*context_, tp};
    }

    [[nodiscard]]
    auto schedule() const noexcept -> schedule_at_sender {
      return schedule_at(time_point());
    }

    auto operator==(const test_scheduler&) const noexcept -> bool = default;

   private:
    test_context* context_;
  };

  inline auto test_context::get_scheduler() noexcept -> test_scheduler {
    return test_scheduler{*this};
  }

  inline auto test_context::get_clock() const noexcept -> test_clock {
    return test_clock{&this->__clock_};
  }

  inline auto test_context::now() const noexcept -> test_context::time_point {
    return __clock_.now();
  }

  namespace _tst_sched {
    struct __recording_receiver {
      using receiver_concept = STDEXEC::receiver_t;

      test_context* __context_;
      STDEXEC::inplace_stop_source* __stop_source_;

      void set_value() noexcept {
        __stop_source_->request_stop();
        __context_->request_stop();
      }
      void set_error(std::exception_ptr) noexcept {
        __stop_source_->request_stop();
        __context_->request_stop();
      }
      void set_stopped() noexcept {
        __stop_source_->request_stop();
        __context_->request_stop();
      }

      using env_t = decltype(STDEXEC::__env::__join(
        STDEXEC::prop{STDEXEC::get_scheduler, STDEXEC::__declval<test_scheduler>()},
        STDEXEC::prop{
          STDEXEC::get_stop_token,
          STDEXEC::__declval<STDEXEC::inplace_stop_token&>()}));
      [[nodiscard]]
      auto get_env() const noexcept -> env_t {
        return STDEXEC::__env::__join(
          STDEXEC::prop{STDEXEC::get_scheduler, __context_->get_scheduler()},
          STDEXEC::prop{STDEXEC::get_stop_token, __stop_source_->get_token()});
      }
    };

    template <class _Receiver>
    struct __next_receiver {
      using receiver_concept = STDEXEC::receiver_t;

      _Receiver* __receiver_;

      void set_value() noexcept {
      }
      template <class _Error>
      void set_error(_Error&&) noexcept {
      }
      void set_stopped() noexcept {
      }

      [[nodiscard]]
      auto get_env() const noexcept -> STDEXEC::env_of_t<_Receiver> {
        return STDEXEC::get_env(*__receiver_);
      }
    };

    //
    // __test_sequence.. is a sequence-sender that produces a set of marbles as signals
    //

    struct __test_sequence_operation_context_base {
      test_context* __context_;
      STDEXEC::inplace_stop_source __stop_source_{};

      void request_stop() {
        __stop_source_.request_stop();
      }
      void stop_context() {
        __context_->request_stop();
      }
    };

    template <class _Receiver>
    struct __test_sequence_operation_base : public __test_sequence_operation_context_base {

      using __marble_t = marble_t<test_clock>;
      using __marble_sender_t = __marble_t::__marble_sender_t;
      using __time_point_t = typename test_scheduler::time_point;

      std::vector<__marble_t> __marbles_;
      _Receiver __receiver_;
      __marble_t* __end_marble_{nullptr};
      int __active_ops_ = 0;

      __marble_t __requested_stop_marble_{__time_point_t{}, sequence_stopped};

      struct __stop_callback_fn_t {
        __test_sequence_operation_base* __self_;
        void operator()() const noexcept {
          auto& __self = *__self_;
          __self.__requested_stop_marble_.set_origin_frame(__self.__context_->now());
          // cancel all pending ops
          __self_->request_stop();
          if (__self.__active_ops_ == 0) {
            if (!__self.__end_marble_) {
              __self.__requested_stop_marble_
                .visit_sequence_receiver(static_cast<_Receiver&&>(__self.__receiver_));
            }
          }
        }
      };

      using __stop_callback_t = STDEXEC::stop_callback_for_t<
        STDEXEC::stop_token_of_t<STDEXEC::env_of_t<_Receiver>>,
        __stop_callback_fn_t
      >;

      std::optional<__stop_callback_t> __on_stop_;

      __test_sequence_operation_base(
        std::vector<__marble_t> __marbles,
        test_context* __context,
        _Receiver&& __receiver) noexcept
        : __test_sequence_operation_context_base{__context}
        , __marbles_{static_cast<std::vector<__marble_t>&&>(__marbles)}
        , __receiver_{static_cast<_Receiver&&>(__receiver)} {
      }
    };

    template <class _Receiver>
    struct __test_sequence_operation : __test_sequence_operation_base<_Receiver> {

      using __marble_t = marble_t<test_clock>;
      using __marble_sender_t = __marble_t::__marble_sender_t;
      using __time_point_t = typename test_scheduler::time_point;

      template <class _Completion>
      static auto __schedule_at(
        __test_sequence_operation_base<_Receiver>& __self,
        __marble_t& __marble,
        _Completion&& __completion) noexcept {
        return STDEXEC::write_env(
                 // schedule the marble completion at the specified frame
                 experimental::execution::sequence(
                   experimental::execution::schedule_at(__self.__context_->get_scheduler(), __marble.frame()),
                   static_cast<_Completion&&>(__completion)),
                 STDEXEC::prop{STDEXEC::get_stop_token, __self.__stop_source_.get_token()})
             | experimental::execution::finally(STDEXEC::just() | STDEXEC::then([&__self, &__marble]() noexcept {
                               // after each completion, update the __test_sequence_operation state
                               STDEXEC_ASSERT(__self.__active_ops_ > 0);
                               if (
                                 __marble.error_notification() || __marble.stopped_notification()
                                 || __marble.sequence_error() || __marble.sequence_stopped()
                                 || __marble.sequence_end()) {
                                 // these marbles trigger the whole sequence
                                 // to complete with no more items
                                 if (!__self.__end_marble_) {
                                   // set as the end marble
                                   // this determines the signal that will be used to
                                   // complete the sequence after all remaining active
                                   // operations have completed
                                   __self.__end_marble_ = &__marble;
                                 }
                                 // cancel all pending ops
                                 __self.request_stop();
                               }
                               if (--__self.__active_ops_ == 0) {
                                 // all ops are complete,
                                 if (!!__self.__end_marble_) {
                                   __self.__on_stop_.reset();
                                   __self.__end_marble_->visit_sequence_receiver(
                                     static_cast<_Receiver&&>(__self.__receiver_));
                                 }
                                 // else this sequence never completes -
                                 // this sequence must be stopped externally
                               }
                             }));
      }
      using __receiver_t = __next_receiver<_Receiver>;
      static auto __schedule_marble(
        __test_sequence_operation_base<_Receiver>& __self,
        __marble_t& __marble) noexcept {
        using __next_sender_t = decltype(__schedule_at(
          __self, __marble, experimental::execution::set_next(__self.__receiver_, __marble.visit_sender())));
        using __end_sender_t = decltype(__schedule_at(__self, __marble, STDEXEC::just()));

        using __result_t = variant_sender<__next_sender_t, __end_sender_t>;
        if (__marble.__notification_.has_value()) {
          return __result_t{__next_sender_t{{__schedule_at(
            __self, __marble, experimental::execution::set_next(__self.__receiver_, __marble.visit_sender()))}}};
        } else {
          return __result_t{
            __end_sender_t{{__schedule_at(__self, __marble, STDEXEC::just())}}};
        }
      }

      using __scheduled_marble_t = STDEXEC::__call_result_t<
        decltype(&__schedule_marble),
        __test_sequence_operation_base<_Receiver>&,
        __marble_t&
      >;
      using __marble_op_t = STDEXEC::connect_result_t<__scheduled_marble_t, __receiver_t>;

      std::deque<STDEXEC::__optional<__marble_op_t>> __marble_ops_{};

      __test_sequence_operation(
        std::vector<__marble_t> __marbles,
        test_context* __context,
        _Receiver&& __receiver) noexcept
        : __test_sequence_operation_base<_Receiver>{
            static_cast<std::vector<__marble_t>&&>(__marbles),
            __context,
            static_cast<_Receiver&&>(__receiver)} {
      }

      void start() noexcept {
        this->__on_stop_.emplace(
          STDEXEC::get_stop_token(STDEXEC::get_env(this->__receiver_)),
          typename __test_sequence_operation_base<_Receiver>::__stop_callback_fn_t{this});
        for (auto& __marble: this->__marbles_) {
          __marble.set_origin_frame(this->__context_->now());
          auto& __op = __marble_ops_.emplace_back();
          __op.__emplace_from([this, &__marble]() {
            return STDEXEC::connect(
              __schedule_marble(*this, __marble), __receiver_t{&this->__receiver_});
          });
        }

        this->__active_ops_ = __marble_ops_.size();
        for (auto& __op: __marble_ops_) {
          STDEXEC::start(__op.value());
        }
      }
    };

    struct __test_sequence {
      using sender_concept = experimental::execution::sequence_sender_t;

      using marble_t = marble_t<test_clock>;
      using marble_sender_t = marble_t::__marble_sender_t;

      test_context* __context_;
      std::vector<marble_t> __marbles_;

      template <STDEXEC::__decays_to<__test_sequence> _Self, class... _Env>
      static consteval auto get_item_types() noexcept -> item_types<marble_sender_t> {
        return {};
      }

      template <STDEXEC::__decays_to<__test_sequence> _Self, class... _Env>
      static consteval auto get_completion_signatures() noexcept -> STDEXEC::completion_signatures<
        STDEXEC::set_value_t(),
        STDEXEC::set_error_t(std::error_code),
        STDEXEC::set_error_t(std::exception_ptr),
        STDEXEC::set_stopped_t()
      > {
        return {};
      }

      template <STDEXEC::__decays_to<__test_sequence> _Self, STDEXEC::receiver _Receiver>
      STDEXEC_EXPLICIT_THIS_BEGIN(auto subscribe)(this _Self&& __self, _Receiver __receiver)
        -> __test_sequence_operation<_Receiver> {
        return {
          static_cast<_Self&&>(__self).__marbles_,
          static_cast<_Self&&>(__self).__context_,
          static_cast<_Receiver&&>(__receiver)};
      }
      STDEXEC_EXPLICIT_THIS_END(subscribe)
    };
  } // namespace _tst_sched

  template <class _Sequence>
  inline auto test_context::get_marbles_from(
    _Sequence&& __sequence,
    typename test_clock::duration __stop_after) noexcept -> std::vector<marble_t<test_clock>> {

    std::vector<marble_t<test_clock>> __recording;
    STDEXEC::inplace_stop_source __source;
    auto __clock = get_clock();

    auto __op = STDEXEC::connect(
      STDEXEC::when_all(
        // record the sequence
        experimental::execution::sequence(
          // schedule connect and start of the sequence being recorded
          // on the test scheduler
          experimental::execution::schedule_at(get_scheduler(), __clock.now()),
          record_marbles(&__recording, __clock, static_cast<_Sequence&&>(__sequence)),
          // always complete with set_stopped to prevent the following
          // scheduled request_stop from affecting the clock
          STDEXEC::just_stopped()),
        // this is used to stop a 'never' sequence
        experimental::execution::schedule_at(get_scheduler(), __clock.now() + __stop_after)
          | STDEXEC::then([&__source]() noexcept { __source.request_stop(); })),
      _tst_sched::__recording_receiver{this, &__source});
    STDEXEC::start(__op);

    // dispatch the test context queues
    run();

    return __recording;
  }

  inline auto
    test_context::get_marble_sequence_from(std::vector<marble_t<test_clock>> __marbles) noexcept
    -> __well_formed_sequence_sender auto {
    return _tst_sched::__test_sequence{this, static_cast<std::vector<marble_t<test_clock>>&&>(__marbles)};
  }

  template <std::size_t _Len>
  inline auto test_context::get_marble_sequence_from(experimental::execution::__mstring<_Len> __diagram) noexcept
    -> __well_formed_sequence_sender auto {
    return get_marble_sequence_from(get_marbles_from(__diagram));
  }
} // namespace experimental::execution

namespace exec = experimental::execution;
