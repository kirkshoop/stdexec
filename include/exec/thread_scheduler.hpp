
#pragma once

#include "../stdexec/execution.hpp"
#include "../stdexec/__detail/__intrusive_queue.hpp"

#include "async_resource.hpp"
#include "time_scheduler.hpp"

#include <atomic>
#include <array>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>

namespace exec {

  namespace __thread_scheduler {
    using namespace stdexec;

    template<class Clock>
    struct when {
      static uint32_t get_next_seqnum() {
        static std::atomic<uint32_t> next_seqnum;
        return next_seqnum++;
      }
      using clock = Clock;
      using time_point = typename clock::time_point;
      when(time_point at) : at(at), seqnum(get_next_seqnum()) {}
      time_point at;
      uint32_t seqnum;

      friend bool operator<(when::time_point lhs, when rhs) {
        return lhs < rhs.at;
      }
      friend bool operator<(when lhs, when::time_point rhs) {
        return lhs.at < rhs;
      }
      friend bool operator<(when lhs, when rhs) {
        return lhs.at < rhs.at || (lhs.at == rhs.at && lhs.seqnum < rhs.seqnum);
      }

    };
    template<class Base>
    struct self_from_void {
      template <class Self>
      static Self& self_from(void* v) {return *static_cast<Self*>(static_cast<Base*>(reinterpret_cast<self_from_void*>(v)));}
    };
    template<class Clock>
    struct item_op_base : self_from_void<item_op_base<Clock>> {
      using clock = Clock;
      using time_point = typename clock::time_point;
      template<class Complete>
      explicit item_op_base(Complete&& complete, time_point at) : complete(complete), at(at) {}
      void(*complete)(void*, time_point) noexcept;
      when<Clock> at;
      struct less {
        bool operator()(item_op_base* lhs, item_op_base* rhs) const {
          STDEXEC_ASSERT(lhs != nullptr);
          STDEXEC_ASSERT(rhs != nullptr);
          return lhs->at < rhs->at;
        }
      };
    };

    enum class thread_phase {INVALID = 0, Constructed, Running, Closing, Closed};
    template<class Clock>
    struct thread_impl {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      void loop() noexcept {
        std::unique_lock<std::mutex> guard(lock);
        // wait for startup
        while(phase != thread_phase::Running && phase != thread_phase::Closing) {
          // wait for phase change
          wake.wait(guard);
        }
        void* opener = std::exchange(open_impl, nullptr);
        auto open = std::exchange(complete_open, nullptr);
        if (opener && open) {
          guard.unlock();
          // complete pending open operation
          open(opener, this);
          guard.lock();
        }
        // loop until closed
        while(!pending.empty() || phase == thread_phase::Running) {
          if (!pending.empty()) {
            when<clock> next_at = pending.top()->at;
            // only advance now() when starting a batch
            time_point now = clock::now();
            // pop items from the queue and complete on this thread
            while (!pending.empty() && next_at < now) {
              // collect batch of ready items under lock
              std::array<item_op_base<Clock>*, 10> local;
              auto end = local.begin();
              while (next_at < now && !pending.empty() && end != local.end()) {
                *end++ = pending.top();
                pending.pop();
                if (!pending.empty()) {
                  next_at = pending.top()->at;
                }
              }
              if (local.begin() != end) {
                guard.unlock();
                // complete batch of ready items while unlocked
                for(auto cursor = local.begin() ; cursor != end ; ++cursor) {
                  (*cursor)->complete(*cursor, now);
                }
                guard.lock();
              }
            }
            if (!pending.empty() && now < pending.top()->at) {
              // wait for next item
              wake.wait_until(guard, pending.top()->at.at);
            }
          } else {
            // wait for new item or for phase change
            wake.wait(guard);
          }
        }
      }
      static void thread_loop(thread_impl<clock>* self) noexcept {
        self->loop();
        std::unique_lock<std::mutex> guard(self->lock);
        std::exchange(self->phase, thread_phase::Closed);
        void* closer = std::exchange(self->close_impl, nullptr);
        auto close = std::exchange(self->complete_close, nullptr);
        void* runner = std::exchange(self->run_impl, nullptr);
        auto exit = std::exchange(self->complete_run, nullptr);
        self->exec.detach();
        guard.unlock();
        if (closer && close) {
          close(closer);
        }
        if (runner && exit) {
          exit(runner);
          // this is no longer valid
        }
      }
      thread_impl() : exec{thread_loop, this} {}

      std::mutex lock;
      std::condition_variable wake;
      std::priority_queue<item_op_base<Clock>*, std::vector<item_op_base<Clock>*>, typename item_op_base<Clock>::less> pending{};
      thread_phase phase = thread_phase::Constructed;
      void* close_impl = nullptr;
      void (*complete_close)(void*) noexcept = nullptr;
      void* run_impl = nullptr;
      void (*complete_run)(void*) noexcept = nullptr;
      void* open_impl = nullptr;
      void (*complete_open)(void*, thread_impl<clock>*) noexcept = nullptr;
      std::thread exec;
    };
    template<class Clock, class Receiver>
    struct item_at_impl : item_op_base<Clock> {
      using clock = Clock;
      using time_point = typename clock::time_point;

      template<class R>
      explicit item_at_impl(thread_impl<clock>* rsrc, time_point at, R&& r) : item_op_base<Clock>(&complete_impl, at), r((R&&)r), rsrc(rsrc) {}
      Receiver r;
      thread_impl<clock>* rsrc;

      static void complete_impl(void* v, time_point now) noexcept {
        item_at_impl& self = item_op_base<Clock>::template self_from<item_at_impl>(v);
        self.complete = nullptr;
        self.rsrc = nullptr;
        set_value(std::move(self.r), self.at, now);
      }

      template<__decays_to<item_at_impl> Self>
      friend void tag_invoke(start_t, Self&& self) noexcept {
        std::unique_lock<std::mutex> guard(self.rsrc->lock);
        self.rsrc->pending.push(&self);
        self.rsrc->wake.notify_one();
      }
    };
    template<class Clock>
    struct item_at_sender {
      using clock = Clock;
      using time_point = typename clock::time_point;
      thread_impl<clock>* rsrc;
      time_point at;
      template <__decays_to<item_at_sender> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t(time_point, time_point)>;
      template<__decays_to<item_at_sender> Self, class Receiver>
      friend item_at_impl<Clock, remove_cvref_t<Receiver>> tag_invoke(connect_t, Self&& self, Receiver&& r) {
        return item_at_impl<Clock, remove_cvref_t<Receiver>>{self.rsrc, self.at, (Receiver&&) r};
      }
    };
    template<class Clock, class Receiver>
    struct item_after_impl : item_op_base<Clock> {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      template<class R>
      explicit item_after_impl(thread_impl<clock>* rsrc, duration after, R&& r) : item_op_base<Clock>(&complete_impl, {}), r((R&&)r), rsrc(rsrc), after(after) {}
      Receiver r;
      thread_impl<clock>* rsrc;
      duration after;

      static void complete_impl(void* v, time_point now) noexcept {
        item_after_impl& self = item_op_base<Clock>::template self_from<item_after_impl>(v);
        self.complete = nullptr;
        self.rsrc = nullptr;
        set_value(std::move(self.r), self.at, now);
      }

      template<__decays_to<item_after_impl> Self>
      friend void tag_invoke(start_t, Self&& self) noexcept {
        self.at = self.after + clock::now();
        std::unique_lock<std::mutex> guard(self.rsrc->lock);
        self.rsrc->pending.push(&self);
        self.rsrc->wake.notify_one();
      }
    };
    template<class Clock>
    struct item_after_sender {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;
      thread_impl<clock>* rsrc;
      duration after;
      template <__decays_to<item_after_sender> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t(time_point, time_point)>;
      template<__decays_to<item_after_sender> Self, class Receiver>
      friend item_after_impl<Clock, remove_cvref_t<Receiver>> tag_invoke(connect_t, Self&& self, Receiver&& r) {
        return item_after_impl<Clock, remove_cvref_t<Receiver>>{self.rsrc, self.after, (Receiver&&) r};
      }
    };
    template<class Clock, class Receiver>
    struct thread_close_impl : self_from_void<thread_close_impl<Clock, Receiver>> {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      template<class R>
      explicit thread_close_impl(thread_impl<clock>* rsrc, R&& r) : r((R&&)r), rsrc(rsrc) {}
      Receiver r;
      thread_impl<clock>* rsrc;

      static void complete_impl(void* v) noexcept {
        thread_close_impl<Clock, Receiver>& self = thread_close_impl<Clock, Receiver>::template self_from<thread_close_impl<Clock, Receiver>>(v);
        self.rsrc->complete_close = nullptr;
        self.rsrc->close_impl = nullptr;
        self.rsrc = nullptr;
        set_value(std::move(self.r));
      }

      template<__decays_to<thread_close_impl> Self>
      friend void tag_invoke(start_t, Self&& self) noexcept {
        std::unique_lock<std::mutex> guard(self.rsrc->lock);
        STDEXEC_ASSERT(self.rsrc->phase == thread_phase::Running);
        std::exchange(self.rsrc->phase, thread_phase::Closing);
        self.rsrc->close_impl = &self;
        self.rsrc->complete_close = &complete_impl;
        self.rsrc->wake.notify_one();
      }
    };
    template<class Clock>
    struct thread_close {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;
      explicit thread_close(thread_impl<clock>* rsrc) : rsrc(rsrc) {}
      thread_impl<clock>* rsrc;
      template <__decays_to<thread_close> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t()>;
      template<__decays_to<thread_close> Self, class Receiver>
      friend thread_close_impl<Clock, remove_cvref_t<Receiver>> tag_invoke(connect_t, Self&& self, Receiver&& r) {
        return thread_close_impl<Clock, remove_cvref_t<Receiver>>{self.rsrc, (Receiver&&) r};
      }
    };
    template<class Clock>
    struct thread_starting {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      std::mutex lock;
      void* open_impl = nullptr;
      void (*complete_open)(void*, thread_impl<clock>*) noexcept = nullptr;
      thread_impl<clock>* rsrc = nullptr;
    };
    template<class Clock, class Receiver>
    struct thread_run_impl : self_from_void<thread_run_impl<Clock, Receiver>> {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      template<class R>
      explicit thread_run_impl(thread_starting<clock>* strt, R&& r) : r((R&&)r), strt(strt) {}
      Receiver r;
      thread_starting<clock>* strt;
      typename std::aligned_storage<sizeof(thread_impl<clock>), alignof(thread_impl<clock>)>::type storage;
      thread_impl<clock>* rsrc;

      static void complete_impl(void* v) noexcept {
        thread_run_impl<Clock, Receiver>& self = thread_run_impl<Clock, Receiver>::template self_from<thread_run_impl<Clock, Receiver>>(v);
        self.rsrc->complete_run = nullptr;
        self.rsrc->run_impl = nullptr;
        self.rsrc->~thread_impl<clock>();
        self.rsrc = nullptr;
        set_value(std::move(self.r));
      }

      template<__decays_to<thread_run_impl> Self>
      friend void tag_invoke(start_t, Self&& self) noexcept {
        self.rsrc = reinterpret_cast<thread_impl<clock>*>(&self.storage);
        new (self.rsrc) thread_impl<clock>();
        std::unique_lock<std::mutex> strt_guard(self.strt->lock);
        self.strt->rsrc = self.rsrc;
        auto opener = self.strt->open_impl;
        auto open = self.strt->complete_open;
        strt_guard.unlock();
        {
          std::unique_lock<std::mutex> guard(self.rsrc->lock);
          STDEXEC_ASSERT(self.rsrc->phase != thread_phase::Running);
          STDEXEC_ASSERT(self.rsrc->phase != thread_phase::Closing);
          STDEXEC_ASSERT(self.rsrc->phase != thread_phase::Closed);
          self.rsrc->run_impl = &self;
          self.rsrc->complete_run = &complete_impl;
          if (opener && open) {
            std::exchange(self.rsrc->phase, thread_phase::Running);
            self.rsrc->open_impl = opener;
            self.rsrc->complete_open = open;
            self.rsrc->wake.notify_one();
          }
        }
      }
    };
    template<class Clock>
    struct thread_run {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;
      explicit thread_run(thread_starting<clock>* strt) : strt(strt) {}
      thread_starting<clock>* strt;
      template <__decays_to<thread_run> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t()>;
      template<__decays_to<thread_run> Self, class Receiver>
      friend thread_run_impl<Clock, remove_cvref_t<Receiver>> tag_invoke(connect_t, Self&& self, Receiver&& r) {
        return thread_run_impl<Clock, remove_cvref_t<Receiver>>{self.strt, (Receiver&&) r};
      }
    };
    template<class Clock>
    struct thread_token {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      mutable thread_impl<clock>* rsrc;

      template<__decays_to<thread_token> Self>
      friend thread_close<clock> tag_invoke(async_resource_token_t::close_t, Self&& self) {
        return thread_close<clock>{self.rsrc};
      }

      template<__decays_to<thread_token> Self>
      friend time_point tag_invoke(time_scheduler_t::now_t, Self&&) {
        return Clock::now();
      }

      template<__decays_to<thread_token> Self>
      friend item_at_sender<Clock> tag_invoke(time_scheduler_t::schedule_at_t, Self&& self, time_point at) {
        return item_at_sender<Clock>{((Self&&)self).rsrc, at};
      }

      template<__decays_to<thread_token> Self>
      friend item_after_sender<Clock> tag_invoke(time_scheduler_t::schedule_after_t, Self&& self, duration after) {
        return item_after_sender<Clock>{((Self&&)self).rsrc, after};
      }
    };
    template<class Clock, class Receiver>
    struct thread_open_impl : self_from_void<thread_open_impl<Clock, Receiver>> {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      template<class R>
      explicit thread_open_impl(thread_starting<clock>* strt, R&& r) : r((R&&)r), strt(strt) {}
      Receiver r;
      thread_starting<clock>* strt;

      static void complete_impl(void* v, thread_impl<clock>* rsrc) noexcept {
        thread_open_impl<Clock, Receiver>& self = thread_open_impl<Clock, Receiver>::template self_from<thread_open_impl<Clock, Receiver>>(v);
        rsrc->complete_open = nullptr;
        rsrc->open_impl = nullptr;
        set_value(std::move(self.r), thread_token<clock>{rsrc});
      }

      template<__decays_to<thread_open_impl> Self>
      friend void tag_invoke(start_t, Self&& self) noexcept {
        std::unique_lock<std::mutex> strt_guard(self.strt->lock);
        auto rsrc = self.strt->rsrc;
        if (!rsrc) {
          self.strt->open_impl = &self;
          self.strt->complete_open = &complete_impl;
        } else {
          strt_guard.unlock();
          std::unique_lock<std::mutex> guard(rsrc->lock);
          std::exchange(rsrc->phase, thread_phase::Running);
          rsrc->open_impl = &self;
          rsrc->complete_open = &complete_impl;
          rsrc->wake.notify_one();
        }
      }
    };
    template<class Clock>
    struct thread_open {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;
      explicit thread_open(thread_starting<clock>* strt) : strt(strt) {}
      thread_starting<clock>* strt;
      template <__decays_to<thread_open> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t(thread_token<clock>)>;
      template<__decays_to<thread_open> Self, class Receiver>
      friend thread_open_impl<Clock, remove_cvref_t<Receiver>> tag_invoke(connect_t, Self&& self, Receiver&& r) {
        return thread_open_impl<Clock, remove_cvref_t<Receiver>>{self.strt, (Receiver&&) r};
      }
    };
    template<class Clock>
    struct thread_resource {
      using clock = Clock;
      using time_point = typename clock::time_point;
      using duration = typename clock::duration;

      thread_resource() = default;
      thread_resource(const thread_resource&) = delete;
      thread_resource(thread_resource&&) = delete;

      mutable thread_starting<clock> strt;

      template<__decays_to<thread_resource> Self>
      friend thread_open<clock> tag_invoke(async_resource_t::open_t, Self&& self) {
        return thread_open<clock>{&self.strt};
      }

      template<__decays_to<thread_resource> Self>
      friend thread_run<clock> tag_invoke(async_resource_t::run_t, Self&& self) {
        return thread_run<clock>{&self.strt};
      }
    };
  } // namespace __thread_scheduler
  template<class Clock>
  using thread_resource = __thread_scheduler::thread_resource<Clock>;
} // namespace exec

struct thread_scheduler_check {
  static_assert(stdexec::__callable<exec::async_resource_t::open_t, exec::thread_resource<std::chrono::steady_clock>>);
  static_assert(stdexec::__callable<exec::async_resource_t::run_t, exec::thread_resource<std::chrono::steady_clock>>);
  static_assert(stdexec::__callable<exec::async_resource_token_t::close_t, exec::__thread_scheduler::thread_token<std::chrono::steady_clock>>);

  static_assert(stdexec::__callable<exec::time_scheduler_t::now_t, exec::__thread_scheduler::thread_token<std::chrono::steady_clock>>);
  static_assert(stdexec::__callable<exec::time_scheduler_t::schedule_at_t, exec::__thread_scheduler::thread_token<std::chrono::steady_clock>, std::chrono::steady_clock::time_point>);
  static_assert(stdexec::__callable<exec::time_scheduler_t::schedule_after_t, exec::__thread_scheduler::thread_token<std::chrono::steady_clock>, std::chrono::steady_clock::duration>);
};
