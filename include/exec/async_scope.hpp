/*
 * Copyright (c) 2021-2022 NVIDIA Corporation
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

#include "../stdexec/execution.hpp"
#include "../stdexec/__detail/__intrusive_queue.hpp"
#include "env.hpp"
#include "async_resource.hpp"

namespace exec {

  /////////////////////////////////////////////////////////////////////////////
  // counting_scope
  namespace __scope {
    using namespace stdexec;

    struct nest_t {
      template <class _Fn, class _NestedSender>
        using __f = __minvoke<_Fn, _NestedSender>;

      template <class _AsyncScopeToken, class _NestedSender>
        requires tag_invocable<nest_t, const _AsyncScopeToken&, _NestedSender>
      [[nodiscard]] auto operator()(const _AsyncScopeToken& __tkn, _NestedSender&& __ns) const {
        return tag_invoke(nest_t{}, __tkn, (_NestedSender&&) __ns);
      }
    };

    struct spawn_t {
      template <class _Fn, class _Env, class _NestedSender>
        using __f = __minvoke<_Fn, _Env, _NestedSender>;

      template <class _AsyncScopeToken, __movable_value _Env = empty_env, sender_in<_Env> _NestedSender>
        requires tag_invocable<spawn_t, const _AsyncScopeToken&, _NestedSender, _Env>
      void operator()(const _AsyncScopeToken& __tkn, _NestedSender&& __ns, _Env __env = {}) const {
        (void) tag_invoke(spawn_t{}, __tkn, (_NestedSender&&) __ns, (_Env&&) __env);
      }
    };

    struct spawn_future_t {
      template <class _Fn, class _Env, class _NestedSender>
        using __f = __minvoke<_Fn, _Env, _NestedSender>;

      template <class _AsyncScopeToken, __movable_value _Env = empty_env, sender_in<_Env> _NestedSender>
        requires tag_invocable<spawn_future_t, const _AsyncScopeToken&, _NestedSender, _Env>
      auto operator()(const _AsyncScopeToken& __tkn, _NestedSender&& __ns, _Env __env = {}) const {
        return tag_invoke(spawn_future_t{}, __tkn, (_NestedSender&&) __ns, (_Env&&) __env);
      }
    };

    struct async_scope_t : async_resource_token_t {
      template<class _T>
        requires 
          requires (const _T& __t_clv){
            nest_t{}(__t_clv, just());
            spawn_t{}(__t_clv, just());
            spawn_future_t{}(__t_clv, just());
          } //&&
          // exec::satisfies<_T, exec::async_resource.token>
        inline constexpr bool satisfies() const {return true;}

      using nest_t = __scope::nest_t;
      /// @brief nest() provides a sender that wraps the given sender 
      /// @details The sender provided by nest() wraps the the given sender,  
      /// such that once the returned sender is started, the async_scope 
      /// is not closed until the given sender completes. 
      /// @param resource-token&  
      /// @returns sender
      inline static constexpr nest_t nest{};

      using spawn_t = __scope::spawn_t;
      /// @brief spawn() eagerly starts the given sender 
      /// @details The sender given to spawn() is wrapped using nest() and 
      /// then that sender is connected to a receiver that provides the
      /// environment returned by get_env(resource-token). The operation-state 
      /// is stored on the heap using the allocator returned from get_allocator() 
      /// provided on the environment returned by get_env(resource-token). 
      /// @param resource-token&  
      /// @returns void
      inline static constexpr spawn_t spawn{};

      using spawn_future_t = __scope::spawn_future_t;
      /// @brief spawn_future() eagerly starts the given sender and returns a sender
      /// @details The sender given to spawn_future() is wrapped using nest() and 
      /// then that sender is connected to a receiver that provides the environment 
      /// returned by get_env(*this). The operation-state and the state required to 
      /// coordinate the completion of the given sender and the start of the returned 
      /// sender, is stored on the heap using the allocator returned from get_allocator() 
      /// on the environment returned by get_env(resource-token). 
      /// @param resource-token&  
      /// @returns sender
      inline static constexpr spawn_future_t spawn_future{};
    };

    enum class __phase {
      __invalid = 0,
      __constructed,
      __pending,
      __opening,
      __running,
      __closing,
      __closed,
    };

    struct __ctx;
    struct __impl;
    struct counting_scope;

    struct __task : __immovable {
      mutable __ctx* __context_ = nullptr;
      void (*__notify_waiter)(__task*, __phase) noexcept = nullptr;
      __task* __next_ = nullptr;

      ~__task() {
        printf("~__task this(%p)\n", this);
      }
    };

    struct alock {
      mutable std::mutex __lock_{};
      mutable bool __locked_ = false;
      bool try_lock() const {
        return __lock_.try_lock();
      }
      void lock() const {
        __lock_.lock();
        printf("   locked\n");
        bool __old = std::exchange(__locked_, true);
        STDEXEC_ASSERT(!__old);
      }
      void unlock() const {
        bool __old = std::exchange(__locked_, false);
        STDEXEC_ASSERT(__old);
        __lock_.unlock();
        printf("un-locked\n");
      }
    };
    using scope_lock_t = alock;
    struct __ctx : __immovable {
      using __phase = __scope::__phase;
      mutable alock __lock_{};
      mutable __phase __phase_ = __phase::__constructed;
      mutable __task* __open_ = nullptr;
      mutable __impl* __impl_ = nullptr;

      ~__ctx() {
        printf("~__ctx this(%p)\n", this);
        std::unique_lock __guard{__lock_};
        // these help with debugging
        STDEXEC_ASSERT(__phase_ != __phase::__opening);
        STDEXEC_ASSERT(__phase_ != __phase::__running);
        STDEXEC_ASSERT(__phase_ != __phase::__closing);
        // actual constraint
        STDEXEC_ASSERT(
          __phase_ == __phase::__constructed || 
          __phase_ == __phase::__closed);
        STDEXEC_ASSERT(__open_ == nullptr);
        STDEXEC_ASSERT(__impl_ == nullptr);
      }
    };

    struct __impl : __immovable {
      static void __shutdown(std::unique_lock<scope_lock_t>& __guard, const __impl* __scope) noexcept {
        STDEXEC_ASSERT(__guard.owns_lock());
        STDEXEC_ASSERT(__scope != nullptr);
        STDEXEC_ASSERT(__scope->__context_ != nullptr);
        STDEXEC_ASSERT(__scope->__context_->__impl_ != nullptr);

        __task* __close = nullptr;        
        if (__scope->__context_->__phase_ == __ctx::__phase::__closing){
          __scope->__context_->__phase_ = __ctx::__phase::__closed;
          __close = std::exchange(__scope->__close_, nullptr);
        }

        __task* __run = nullptr;
        if (__scope->__context_->__phase_ == __ctx::__phase::__closed){
          __run = std::exchange(__scope->__run_, nullptr);
        }

        auto __phase = __scope->__context_->__phase_;
        __scope->__context_->__impl_ = nullptr;
        __scope->__context_ = nullptr;
        __scope = nullptr;
        __guard.unlock();
        // do not access __scope 
        // do not access __context_ 
        if (__close != nullptr) {
          __close->__notify_waiter(__close, __phase);
        }
        if (__run != nullptr) {
          __run->__notify_waiter(__run, __phase);
        }
        // __scope must be considered deleted
        // __context_ must be considered deleted
      }

      static void __complete(const __impl* __scope, std::unique_lock<scope_lock_t>& __guard) noexcept {
        STDEXEC_ASSERT(__guard.owns_lock());
        STDEXEC_ASSERT(__scope != nullptr);
        STDEXEC_ASSERT(__scope->__context_ != nullptr);
        STDEXEC_ASSERT(__scope->__context_->__impl_ != nullptr);
        auto& __phase = __scope->__context_->__phase_;
        auto& __active = __scope->__active_;
        if (--__active == 0 && __phase == __ctx::__phase::__closing) {
          __shutdown(__guard, __scope);
          // do not access __scope 
          // do not access __context 
        }
      }

      mutable __ctx* __context_ = nullptr;
      mutable std::ptrdiff_t __active_ = 0;
      mutable __task* __run_ = nullptr;
      mutable __task* __close_ = nullptr;

      ~__impl() {
        printf("~__impl this(%p)\n", this);
        STDEXEC_ASSERT(__context_ == 0);
        STDEXEC_ASSERT(__active_ == 0);
        STDEXEC_ASSERT(__run_ == nullptr);
        STDEXEC_ASSERT(__close_ == nullptr);
      }
    };

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::close implementation
    template <class _ReceiverId>
    struct __close_op : __task {
      using _Receiver = __t<_ReceiverId>;

      explicit __close_op(__ctx* __context, _Receiver __rcvr)
        : __task{{}, __context, __notify_waiter}, __rcvr_((_Receiver&&)__rcvr) {
        printf("__close_op this(%p), __notify_waiter(%p)\n", this, &__notify_waiter);
      }

    private:
      static void __notify_waiter(__task* __self, __ctx::__phase __new_phase) noexcept {
        __close_op& __that = *static_cast<__close_op*>(__self);
        switch (__new_phase) {
        case __ctx::__phase::__constructed: 
          // fallthrough
        case __ctx::__phase::__pending: 
          // fallthrough
        case __ctx::__phase::__opening: 
          // fallthrough
        case __ctx::__phase::__running: 
          // fallthrough
        case __ctx::__phase::__closing: {
            constexpr bool phase_must_be_closed = false;
            STDEXEC_ASSERT(phase_must_be_closed);
            std::terminate();
          }
          break;
        case __ctx::__phase::__closed: {
            // closed
            _Receiver __rcvr{(_Receiver&&)__that.__rcvr_};
            set_value((_Receiver&&)__rcvr);
          }
          break;
        default: {
            constexpr bool phase_must_be_closed = false;
            STDEXEC_ASSERT(phase_must_be_closed);
            std::terminate();
          }
        };
      }

      void __start_() noexcept {
        STDEXEC_ASSERT(this->__context_ != nullptr);
        STDEXEC_ASSERT(this->__context_->__impl_ != nullptr);
        STDEXEC_ASSERT(this->__context_->__impl_->__context_ != nullptr);
        printf("__close_op start this(%p), __context_(%p), __phase(%d)\n", 
            this, this->__context_, this->__context_->__phase_);
        std::unique_lock __guard{this->__context_->__lock_};
        auto& __phase = this->__context_->__phase_;
        switch (__phase) {
        case __ctx::__phase::__constructed: 
          // fallthrough
        case __ctx::__phase::__pending: 
          // fallthrough
        case __ctx::__phase::__opening: {
            auto __open = std::exchange(this->__context_->__open_, nullptr);
            if (__open != nullptr) {
              __phase = __ctx::__phase::__closing;
              __guard.unlock();
              __open->__notify_waiter(__open, __ctx::__phase::__closing);
              __guard.lock();
            }
          }
          // fallthrough
        case __ctx::__phase::__running: 
          // fallthrough
        case __ctx::__phase::__closing: {
            __phase = __ctx::__phase::__closing;

            auto& __active = this->__context_->__impl_->__active_;
            auto& __close = this->__context_->__impl_->__close_;
            STDEXEC_ASSERT(__close == nullptr);
            __close = this;
            if (__active != 0) {
              return;
            }
          }
          // fallthrough
        case __ctx::__phase::__closed: {
            auto& __active = this->__context_->__impl_->__active_;
            if (__active == 0) {
              __impl::__shutdown(__guard, __context_->__impl_);
              // do not access __context_
              // __context must be considered deleted
            }
          }
          break;
        default: {
            __guard.unlock();
            constexpr bool phase_must_be_valid = false;
            STDEXEC_ASSERT(phase_must_be_valid);
            std::terminate();
          }
          break;
        };
      }
      friend void tag_invoke(start_t, __close_op& __self) noexcept {
        return __self.__start_();
      }
      _Receiver __rcvr_;
    };

    template <class _Receiver>
    using __close_op_t =
      __close_op<__x<remove_cvref_t<_Receiver>>>;

    struct __close_sender {
      using is_sender = void;

      template <__decays_to<__close_sender> _Self, receiver _Receiver>
      [[nodiscard]] friend __close_op_t<_Receiver>
      tag_invoke(connect_t, _Self&& __self, _Receiver __rcvr) {
        return __close_op_t<_Receiver>{
          __self.__context_,
          (_Receiver&&) __rcvr};
      }

      template <__decays_to<__close_sender> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures<set_value_t()>;

      friend empty_env tag_invoke(get_env_t, const __close_sender& __self) noexcept {
        return {};
      }

      mutable __ctx* __context_;
    };

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::nest implementation
    template <class _ReceiverId>
    struct __nest_op_base : __immovable {
      using _Receiver = __t<_ReceiverId>;
      const __ctx* __context_;
      [[no_unique_address]] _Receiver __rcvr_;
    };

    template <class _ReceiverId>
    struct __nest_rcvr {
      using is_receiver = void;
      using _Receiver = __t<_ReceiverId>;
      __nest_op_base<_ReceiverId>* __op_;

      template < __one_of<set_value_t, set_error_t, set_stopped_t> _Tag, class... _As>
        requires __callable<_Tag, _Receiver, _As...>
      friend void tag_invoke(_Tag, __nest_rcvr&& __self, _As&&... __as) noexcept {
        auto __context = __self.__op_->__context_;
        _Tag{}(std::move(__self.__op_->__rcvr_), (_As&&) __as...);
        std::unique_lock __guard{__context->__lock_};
        __impl::__complete(__context->__impl_, __guard);
        // do not access __op_
        // do not access this
        // do not access __context_
        // do not access __impl_
      }

      friend env_of_t<_Receiver>
        tag_invoke(get_env_t, const __nest_rcvr& __self) noexcept {
        return get_env(__self.__op_->__rcvr_);
      }
    };

    template <class _Constrained, class _ReceiverId>
      struct __nest_op : __nest_op_base<_ReceiverId> {
        using _Receiver = __t<_ReceiverId>;
        STDEXEC_IMMOVABLE_NO_UNIQUE_ADDRESS
          connect_result_t<_Constrained, __nest_rcvr<_ReceiverId>> __op_;
        template <__same_as<_Constrained> _Sender, __decays_to<_Receiver> _Rcvr>
          explicit __nest_op(const __ctx* __context, _Sender&& __c, _Rcvr&& __rcvr)
            : __nest_op_base<_ReceiverId>{{}, __context, (_Rcvr&&) __rcvr}
            , __op_(connect((_Sender&&) __c, __nest_rcvr<_ReceiverId>{this})) {}
      private:
        void __check_start_(std::unique_lock<scope_lock_t>& __guard) noexcept {
          STDEXEC_ASSERT(__guard.owns_lock());
          switch (this->__context_->__phase_) {
          case __ctx::__phase::__constructed: 
            // fallthrough
          case __ctx::__phase::__pending: 
            // fallthrough
          case __ctx::__phase::__opening: {
              __guard.unlock();
              constexpr bool phase_must_be_running_or_closing = false;
              STDEXEC_ASSERT(phase_must_be_running_or_closing);
              std::terminate();
            }
            break;
          case __ctx::__phase::__running: 
            // fallthrough
          case __ctx::__phase::__closing: {
              // safe to start 
            }
            break;
          case __ctx::__phase::__closed: 
            // fallthrough
          default: {
              __guard.unlock();
              constexpr bool phase_must_be_running_or_closing = false;
              STDEXEC_ASSERT(phase_must_be_running_or_closing);
              std::terminate();
            }
          };
          STDEXEC_ASSERT(__guard.owns_lock());
        }

        void __start_() noexcept {
          STDEXEC_ASSERT(this->__context_);
          STDEXEC_ASSERT(this->__context_->__impl_);
          std::unique_lock __guard{this->__context_->__lock_};
          __check_start_(__guard);
          STDEXEC_ASSERT(__guard.owns_lock());
          auto& __active = this->__context_->__impl_->__active_;
          ++__active;
          __guard.unlock();
          start(__op_);
          __guard.lock();
        }
        friend void tag_invoke(start_t, __nest_op& __self) noexcept {
          return __self.__start_();
        }
      };

    template <class _ConstrainedId>
    struct __nest_sender {
      using _Constrained = __t<_ConstrainedId>;
      using is_sender = void;

      const __ctx* __context_;
      [[no_unique_address]] _Constrained __c_;
    private:
      template <class _Self, class _Receiver>
      using __nest_operation_t = __nest_op<__copy_cvref_t<_Self, _Constrained>, __x<_Receiver>>;
      template <class _Receiver>
      using __nest_receiver_t = __nest_rcvr<__x<_Receiver>>;

      template <__decays_to<__nest_sender> _Self, receiver _Receiver>
        requires sender_to<__copy_cvref_t<_Self, _Constrained>, __nest_receiver_t<_Receiver>>
      [[nodiscard]] friend __nest_operation_t<_Self, _Receiver>
      tag_invoke(connect_t, _Self&& __self, _Receiver __rcvr) {
        return __nest_operation_t<_Self, _Receiver>{
          __self.__context_,
          ((_Self&&) __self).__c_,
          (_Receiver&&) __rcvr};
      }
      template <__decays_to<__nest_sender> _Self, class _Env>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
        -> completion_signatures_of_t<__copy_cvref_t<_Self, _Constrained>, _Env>;

      friend empty_env tag_invoke(get_env_t, const __nest_sender& __self) noexcept {
        return {};
      }
    };

    template <class _Constrained>
    using __nest_sender_t = __nest_sender<__x<remove_cvref_t<_Constrained>>>;

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::spawn_future implementation
    enum class __future_step {
      __invalid = 0,
      __created,
      __future,
      __no_future,
      __deleted
    };

    template <class _Sender, class _Env>
    struct __future_state;

    struct __forward_stopped {
      in_place_stop_source* __stop_source_;

      void operator()() noexcept {
        __stop_source_->request_stop();
      }
    };

    struct __subscription : __immovable {
      void (*__complete_)(__subscription*, std::unique_lock<scope_lock_t>& __guard) noexcept = nullptr;

      void __complete(std::unique_lock<scope_lock_t>& __guard) noexcept {
        __complete_(this, __guard);
      }

      __subscription* __next_ = nullptr;
    };

    template <class _SenderId, class _EnvId, class _ReceiverId>
    class __future_op : __subscription {
      using _Sender = __t<_SenderId>;
      using _Env = __t<_EnvId>;
      using _Receiver = __t<_ReceiverId>;

      using __forward_consumer =
        typename stop_token_of_t<env_of_t<_Receiver>>::template callback_type<__forward_stopped>;

      friend void tag_invoke(start_t, __future_op& __self) noexcept {
        __self.__start_();
      }

      void __complete_(std::unique_lock<scope_lock_t>& __guard) noexcept try {
        STDEXEC_ASSERT(__guard.owns_lock());
        auto __state = std::exchange(__state_, nullptr);
        STDEXEC_ASSERT(__state != nullptr);
        // either the future is still in use or it has passed ownership to __state->__no_future_
        if (__state->__no_future_.get() != nullptr || __state->__step_ != __future_step::__future) {
          // invalid state - there is a code bug in the state machine
          __guard.unlock();
          std::terminate();
        } else if (get_stop_token(get_env(__rcvr_)).stop_requested()) {
          __guard.unlock();
          set_stopped((_Receiver&&) __rcvr_);
          __guard.lock();
        } else {
          std::visit(
            [this, &__guard]<class _Tup>(_Tup& __tup) {
              if constexpr (same_as<_Tup, std::monostate>) {
                __guard.unlock();
                std::terminate();
              } else {
                std::apply(
                  [this, &__guard]<class... _As>(auto tag, _As&... __as) {
                    __guard.unlock();
                    tag((_Receiver&&) __rcvr_, (_As&&) __as...);
                    __guard.lock();
                  },
                  __tup);
              }
            },
            __state->__data_);
        }
      } catch (...) {
        if (__guard.owns_lock()) {__guard.unlock();}
        set_error((_Receiver&&) __rcvr_, std::current_exception());
        __guard.lock();
      }

      void __start_() noexcept try {
        if (!!__state_) {
          std::unique_lock __guard{__state_->__mutex_};
          if (__state_->__data_.index() != 0) {
            __complete_(__guard);
            //do not access this
          } else {
            __state_->__subscribers_.push_back(this);
          }
        }
      } catch (...) {
        set_error((_Receiver&&) __rcvr_, std::current_exception());
      }

      [[no_unique_address]] _Receiver __rcvr_;
      std::unique_ptr<__future_state<_Sender, _Env>> __state_;
      [[no_unique_address]] __forward_consumer __forward_consumer_;

     public:
      ~__future_op() noexcept {
        if (__state_ != nullptr) {
          auto __raw_state = __state_.get();
          std::unique_lock __guard{__raw_state->__mutex_};
          if (__raw_state->__data_.index() > 0) {
            // completed given sender
            // state is no longer needed
            return;
          }
          __raw_state->__no_future_ = std::move(__state_);
          __raw_state->__step_from_to_(
            __guard, __future_step::__future, __future_step::__no_future);
        }
      }

      template <class _Receiver2>
      explicit __future_op(
        _Receiver2&& __rcvr, std::unique_ptr<__future_state<_Sender, _Env>> __state)
        : __subscription{{},
          [](__subscription* __self, std::unique_lock<scope_lock_t>& __guard) noexcept -> void {
            static_cast<__future_op*>(__self)->__complete_(__guard);
          }}
        , __rcvr_((_Receiver2&&) __rcvr)
        , __state_(std::move(__state))
        , __forward_consumer_(get_stop_token(get_env(__rcvr_)),
            __forward_stopped{&__state_->__stop_source_}) {
      }
    };

#if STDEXEC_NVHPC()
    template <class _Fn>
    struct __completion_as_tuple2_;

    template <class _Tag, class... _Ts>
    struct __completion_as_tuple2_<_Tag(_Ts&&...)> {
      using __t = std::tuple<_Tag, _Ts...>;
    };
    template <class _Fn>
    using __completion_as_tuple_t = __t<__completion_as_tuple2_<_Fn>>;

#else

    template <class _Tag, class... _Ts>
    std::tuple<_Tag, _Ts...> __completion_as_tuple_(_Tag (*)(_Ts&&...));
    template <class _Fn>
    using __completion_as_tuple_t = decltype(__scope::__completion_as_tuple_((_Fn*) nullptr));
#endif

    template <class... _Ts>
    using __decay_values_t = completion_signatures<set_value_t(decay_t<_Ts>&&...)>;

    template <class _Ty>
    using __decay_error_t = completion_signatures<set_error_t(decay_t<_Ty>&&)>;

    template <class _Sender, class _Env>
    using __future_completions_t = //
      make_completion_signatures<
        _Sender,
        _Env,
        completion_signatures<set_stopped_t(), set_error_t(std::exception_ptr&&)>,
        __decay_values_t,
        __decay_error_t>;

    template <class _Completions>
    using __completions_as_variant = //
      __mapply<
        __transform< __q<__completion_as_tuple_t>, __mbind_front_q<std::variant, std::monostate>>,
        _Completions>;

    template <class _Ty>
    struct __dynamic_delete {
      __dynamic_delete()
        : __delete_([](_Ty* __p) { delete __p; }) {
      }

      template <class _Uy>
        requires convertible_to<_Uy*, _Ty*>
      __dynamic_delete(std::default_delete<_Uy>)
        : __delete_([](_Ty* __p) { delete static_cast<_Uy*>(__p); }) {
      }

      template <class _Uy>
        requires convertible_to<_Uy*, _Ty*>
      __dynamic_delete& operator=(std::default_delete<_Uy> __d) {
        __delete_ = __dynamic_delete{__d}.__delete_;
        return *this;
      }

      void operator()(_Ty* __p) {
        __delete_(__p);
      }

      void (*__delete_)(_Ty*);
    };

    template <class _Completions, class _Env>
    struct __future_state_base {
      __future_state_base(_Env __env, const __impl* __scope) 
          requires same_as<in_place_stop_token, stop_token_of_t<_Env>>
        : __forward_scope_{std::in_place, get_stop_token(__env), __forward_stopped{&__stop_source_}}
        , __env_((_Env&&) __env) {
      }
      __future_state_base(_Env __env, const __impl* __scope) 
          requires (!same_as<in_place_stop_token, stop_token_of_t<_Env>>)
        : __forward_scope_{}
        , __env_((_Env&&) __env) {
      }

      ~__future_state_base() {
        std::unique_lock __guard{__mutex_};
        if (__step_ == __future_step::__created) {
          // exception during connect() will end up here
          __step_from_to_(__guard, __future_step::__created, __future_step::__deleted);
        } else if (__step_ != __future_step::__deleted) {
          // completing the given sender before the future is dropped will end here
          __step_from_to_(__guard, __future_step::__future, __future_step::__deleted);
        }
      }

      void __step_from_to_(
        std::unique_lock<scope_lock_t>& __guard,
        __future_step __from,
        __future_step __to) {
        STDEXEC_ASSERT(__guard.owns_lock());
        auto actual = std::exchange(__step_, __to);
        STDEXEC_ASSERT(actual == __from);
      }

      in_place_stop_source __stop_source_;
      std::optional<in_place_stop_callback<__forward_stopped>> __forward_scope_;
      scope_lock_t __mutex_;
      __future_step __step_ = __future_step::__created;
      std::unique_ptr<__future_state_base, __dynamic_delete<__future_state_base>> __no_future_;
      __completions_as_variant<_Completions> __data_;
      __intrusive_queue<&__subscription::__next_> __subscribers_;
      _Env __env_;
    };

    template <class _CompletionsId, class _EnvId>
    struct __future_rcvr {
      using is_receiver = void;
      using _Completions = __t<_CompletionsId>;
      using _Env = __t<_EnvId>;
      __future_state_base<_Completions, _Env>* __state_;
      const __impl* __scope_;

      void __dispatch_result_(std::unique_lock<scope_lock_t>& __guard) noexcept {
        STDEXEC_ASSERT(__guard.owns_lock());
        auto& __state = *__state_;
        auto __local = std::move(__state.__subscribers_);
        __state.__forward_scope_ = std::nullopt;
        if (__state.__no_future_.get() != nullptr) {
          // nobody is waiting for the results
          // delete this and return
          __state.__step_from_to_(__guard, __future_step::__no_future, __future_step::__deleted);
          __guard.unlock();
          __state.__no_future_.reset();
          __guard.lock();
          return;
        }
        __guard.unlock();
        while (!__local.empty()) {
          auto* __sub = __local.pop_front();
          __sub->__complete(__guard);
          //do not access this
        }
        __guard.lock();
      }

      template < __one_of<set_value_t, set_error_t, set_stopped_t> _Tag, __movable_value... _As>
      friend void tag_invoke(_Tag, __future_rcvr&& __self, _As&&... __as) noexcept {
        auto& __state = *__self.__state_;
        try {
          std::unique_lock __guard{__state.__mutex_};
          using _Tuple = __decayed_tuple<_Tag, _As...>;
          __state.__data_.template emplace<_Tuple>(_Tag{}, (_As&&) __as...);
          __self.__dispatch_result_(__guard);
        } catch (...) {
          using _Tuple = std::tuple<set_error_t, std::exception_ptr>;
          __state.__data_.template emplace<_Tuple>(set_error_t{}, std::current_exception());
        }
      }

      friend const _Env& tag_invoke(get_env_t, const __future_rcvr& __self) noexcept {
        return __self.__state_->__env_;
      }
    };

    template <class _Sender, class _Env>
    using __future_receiver_t =
      __future_rcvr<__x<__future_completions_t<_Sender, _Env>>, __x<_Env>>;

    template <class _Sender, class _Env>
    struct __future_state : __future_state_base<__future_completions_t<_Sender, _Env>, _Env> {
      using _Completions = __future_completions_t<_Sender, _Env>;

      __future_state(_Sender __sndr, _Env __env, const __impl* __scope)
        : __future_state_base<_Completions, _Env>((_Env&&) __env, __scope)
        , __op_(connect((_Sender&&) __sndr, __future_receiver_t<_Sender, _Env>{this, __scope})) {
      }

      connect_result_t<_Sender, __future_receiver_t<_Sender, _Env>> __op_;
    };

    template <class _Sender, class _Env>
    using __future_state_t =
      __future_state<remove_cvref_t<_Sender>, remove_cvref_t<_Env>>;

    template <class _SenderId, class _EnvId>
      class __future {
        using _Sender = __t<_SenderId>;
        using _Env = __t<_EnvId>;
        template<class _EnvId2>
          friend struct __async_scope;
      public:
        using is_sender = void;

      __future(__future&&) = default;
      __future& operator=(__future&&) = default;
      ~__future() noexcept {
        if (__state_ != nullptr) {
          auto __raw_state = __state_.get();
          std::unique_lock __guard{__raw_state->__mutex_};
          if (__raw_state->__data_.index() != 0) {
            // completed given sender
            // state is no longer needed
            return;
          }
          __raw_state->__no_future_ = std::move(__state_);
          __raw_state->__step_from_to_(
            __guard, __future_step::__future, __future_step::__no_future);
        }
      }
    private:
      template <class _Self>
      using __completions_t = __future_completions_t<__make_dependent_on<_Sender, _Self>, _Env>;

      explicit __future(std::unique_ptr<__future_state_t<_Sender, _Env>> __state) noexcept
        : __state_(std::move(__state)) {
        std::unique_lock __guard{__state_->__mutex_};
        __state_->__step_from_to_(__guard, __future_step::__created, __future_step::__future);
      }

      template <__decays_to<__future> _Self, receiver _Receiver>
        requires receiver_of<_Receiver, __completions_t<_Self>>
      friend __future_op<_SenderId, _EnvId, __x<_Receiver>>
        tag_invoke(connect_t, _Self&& __self, _Receiver __rcvr) {
        return __future_op<_SenderId, _EnvId, __x<_Receiver>>{
          (_Receiver&&) __rcvr, std::move(__self.__state_)};
      }

      template <__decays_to<__future> _Self, class _OtherEnv>
      friend auto tag_invoke(get_completion_signatures_t, _Self&&, _OtherEnv)
        -> __completions_t<_Self>;

      friend empty_env tag_invoke(get_env_t, const __future& __self) noexcept {
        return {};
      }

      std::unique_ptr<__future_state_t<_Sender, _Env>> __state_;
    };

    template <class _Sender, class _Env>
    using __future_t = __future<__x<__nest_sender_t<_Sender>>, __x<remove_cvref_t<_Env>>>;

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::spawn implementation
    template <class _EnvId>
    struct __spawn_op_base {
      using _Env = __t<_EnvId>;
      _Env __env_;
      void (*__delete_)(__spawn_op_base*);
    };

    template <class _EnvId>
    struct __spawn_rcvr {
      using is_receiver = void;
      using _Env = __t<_EnvId>;
      __spawn_op_base<_EnvId>* __op_;
      const __impl* __scope_;

      template <__one_of<set_value_t, set_stopped_t> _Tag>
      friend void tag_invoke(_Tag, __spawn_rcvr&& __self) noexcept {
        __self.__op_->__delete_(__self.__op_);
      }

      // BUGBUG NOT TO SPEC spawn shouldn't accept senders that can fail.
      [[noreturn]] friend void
        tag_invoke(set_error_t, __spawn_rcvr&&, const std::exception_ptr&) noexcept {
        std::terminate();
      }

      friend const _Env& tag_invoke(get_env_t, const __spawn_rcvr& __self) noexcept {
        return __self.__op_->__env_;
      }
    };

    template <class _Env>
    using __spawn_receiver_t = __spawn_rcvr<__x<_Env>>;

    template <class _SenderId, class _EnvId>
    struct __spawn_op : __spawn_op_base<_EnvId> {
      using _Env = __t<_EnvId>;
      using _Sender = __t<_SenderId>;

      template <__decays_to<_Sender> _Sndr>
      __spawn_op(_Sndr&& __sndr, _Env __env, const __impl* __scope)
        : __spawn_op_base<_EnvId>{(_Env&&) __env,
          [](__spawn_op_base<_EnvId>* __op) {
            delete static_cast<__spawn_op*>(__op);
          }}
        , __op_(connect((_Sndr&&) __sndr, __spawn_receiver_t<_Env>{this, __scope})) {
      }

      void __start_() noexcept {
        start(__op_);
      }

      friend void tag_invoke(start_t, __spawn_op& __self) noexcept {
        return __self.__start_();
      }

      connect_result_t<_Sender, __spawn_receiver_t<_Env>> __op_;
    };

    template <class _Sender, class _Env>
    using __spawn_operation_t = __spawn_op<__x<_Sender>, __x<_Env>>;

    ////////////////////////////////////////////////////////////////////////////
    // __async_scope

    template<class _EnvId>
    struct __async_scope {
      template <sender _Constrained>
      using nest_result_t = __nest_sender_t<_Constrained>;

      using _Env = __t<_EnvId>;
      ~__async_scope() {
        printf("~__async_scope __ctx this(%p), __phase(%d)\n", this->__context_, this->__context_->__phase_);
      }
      __async_scope() = delete;
      __async_scope(const __async_scope&) = default;
      __async_scope(__async_scope&&) = default;
      __async_scope& operator=(const __async_scope&) = default;
      __async_scope& operator=(__async_scope&&) = default;
    private:
      __ctx* __context_;
      _Env __env_;

      template <class _ReceiverId>
        friend struct __open_op;
      explicit __async_scope(__ctx* __context, _Env __env) : __context_(__context), __env_((_Env&&)__env) {
        printf("__async_scope __ctx this(%p), __phase(%d)\n", this->__context_, this->__context_->__phase_);
      }

      friend __close_sender
      tag_invoke(async_resource_token_t::close_t, const __async_scope& __self) {
        printf("__async_scope close __ctx this(%p), __phase(%d)\n", __self.__context_, __self.__context_->__phase_);
        return __close_sender{__self.__context_};
      }

      template <sender _Constrained>
        friend nest_result_t<_Constrained>
        tag_invoke(nest_t, const __async_scope& __self, _Constrained&& __c) {
          return nest_result_t<_Constrained>{__self.__context_, (_Constrained&&) __c};
        }

      template <__decays_to<__async_scope> _Self, __movable_value _Env2, sender_in<_Env> _Sender>
          requires sender_to<nest_result_t<_Sender>, __spawn_receiver_t<_Env>>
        friend void tag_invoke(spawn_t, _Self&& __self, _Sender&& __sndr, _Env2&&) {
          using __op_t = __spawn_operation_t<nest_result_t<_Sender>, _Env>;
          // start is noexcept so we can assume that the operation will complete
          // after this, which means we can rely on its self-ownership to ensure
          // that it is eventually deleted
          stdexec::start(*new __op_t{nest_t{}(__self, (_Sender&&) __sndr), __self.__env_, __self.__context_->__impl_});
        }
      template <__decays_to<__async_scope> _Self, __movable_value _Env2, sender_in<_Env> _Sender>
          requires 
            sender_to<
              nest_result_t<_Sender>, 
              __future_receiver_t<nest_result_t<_Sender>, _Env>>
        friend __future_t<_Sender, _Env>
        tag_invoke(spawn_future_t, _Self&& __self, _Sender&& __sndr, _Env2&&) {
          using __state_t = __future_state_t<nest_result_t<_Sender>, _Env>;
          auto __state =
            std::make_unique<__state_t>(nest_t{}(__self, (_Sender&&) __sndr), __self.__env_, __self.__context_->__impl_);
          stdexec::start(__state->__op_);
          return __future_t<_Sender, _Env>{std::move(__state)};
        }
    };

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::open implementation
    template <class _ReceiverId>
      struct __open_op : __task {
        using _Receiver = __t<_ReceiverId>;

        explicit __open_op(__ctx* __context, _Receiver __rcvr)
          : __task{{}, __context, __notify_waiter}
          , __rcvr_((_Receiver&&)__rcvr) {
          printf("__open_op this(%p), __notify_waiter(%p)\n", this, &__notify_waiter);
          printf("__open_op this(%p), this->__context_(%p), this->__notify_waiter(%p), this->__next_(%p)\n"
                 "  this->__context_->__impl_(%p), this->__context_->__impl_->__context_(%p)\n", 
                 this, this->__context_, this->__notify_waiter, this->__next_, this->__context_->__impl_, this->__context_->__impl_->__context_);
          printf("__open_op __impl_->__active_(%ld), __impl_->__run_(%p), __impl_->__close_(%p)\n"
                 "  __context_->__phase_(%d), __context_->__open_(%p)\n", 
                 this->__context_->__impl_->__active_, this->__context_->__impl_->__run_, this->__context_->__impl_->__close_,
                 this->__context_->__phase_, this->__context_->__open_);
        }

      private:
        static void __notify_waiter(__task* __self, __ctx::__phase __new_phase) noexcept {
          __open_op& __that = *static_cast<__open_op*>(__self);
          switch (__new_phase) {
          case __ctx::__phase::__constructed: 
            // fallthrough
          case __ctx::__phase::__pending:
            // fallthrough
          case __ctx::__phase::__opening: {
              constexpr bool phase_must_be_running_or_closing = false;
              STDEXEC_ASSERT(phase_must_be_running_or_closing);
              std::terminate();
            }
            break;
          case __ctx::__phase::__running: {
              // open
              __async_scope<__x<env_of_t<_Receiver>>> __tkn{__that.__context_, get_env(__that.__rcvr_)};
              _Receiver __rcvr{(_Receiver&&)__that.__rcvr_};
              set_value((_Receiver&&)__rcvr, std::move(__tkn));
            }
            break;
          case __ctx::__phase::__closing: 
            // fallthrough
          case __ctx::__phase::__closed: 
            // fallthrough
          default: {
              constexpr bool phase_must_be_running = false;
              STDEXEC_ASSERT(phase_must_be_running);
              std::terminate();
            }
          };
        }

        void __start_() noexcept {
          STDEXEC_ASSERT(this->__context_ != nullptr);
          STDEXEC_ASSERT(this->__context_->__impl_ != nullptr);
          STDEXEC_ASSERT(this->__context_->__impl_->__context_ != nullptr);
          printf("__open_op start this(%p), this->__context_(%p), this->__notify_waiter(%p), this->__next_(%p)\n"
                 "  this->__context_->__impl_(%p), this->__context_->__impl_->__context_(%p)\n", 
                 this, this->__context_, this->__notify_waiter, this->__next_, this->__context_->__impl_, this->__context_->__impl_->__context_);
          printf("__open_op start __impl_->__active_(%ld), __impl_->__run_(%p), __impl_->__close_(%p)\n"
                 "  __context_->__phase_(%d), __context_->__open_(%p)\n", 
                 this->__context_->__impl_->__active_, this->__context_->__impl_->__run_, this->__context_->__impl_->__close_,
                 this->__context_->__phase_, this->__context_->__open_);
          fflush(stdout);
          std::unique_lock __guard{this->__context_->__lock_};
          auto& __phase = this->__context_->__phase_;
          switch (__phase) {
          case __ctx::__phase::__constructed: {
              __phase = __ctx::__phase::__opening;
              auto& __open = this->__context_->__open_;
              STDEXEC_ASSERT(__open == nullptr);
              __open = this;
            }
            return;
          case __ctx::__phase::__opening: {
              __guard.unlock();
              constexpr bool phase_must_be_constructed_or_pending_or_running = false;
              STDEXEC_ASSERT(phase_must_be_constructed_or_pending_or_running);
              std::terminate();
            }
            break;
          case __ctx::__phase::__pending: {
              __phase = __ctx::__phase::__running;
            }
            // fallthrough
          case __ctx::__phase::__running: {
              // already open
              __async_scope<__x<env_of_t<_Receiver>>> __tkn{this->__context_, get_env(this->__rcvr_)};
              _Receiver __rcvr{(_Receiver&&)this->__rcvr_};
              __guard.unlock();
              set_value((_Receiver&&)__rcvr, std::move(__tkn));
              __guard.lock();
            }
            break;
          case __ctx::__phase::__closing: 
            // fallthrough
          case __ctx::__phase::__closed: 
            // fallthrough
          default: {
              __guard.unlock();
              constexpr bool phase_must_be_constructed_or_opening_or_running = false;
              STDEXEC_ASSERT(phase_must_be_constructed_or_opening_or_running);
              std::terminate();
            }
          };
          STDEXEC_ASSERT(__guard.owns_lock());
        }
        friend void tag_invoke(start_t, __open_op& __self) noexcept {
          return __self.__start_();
        }
        _Receiver __rcvr_;
      };

      template <class _Receiver>
        using __open_op_t =
          __open_op<__x<remove_cvref_t<_Receiver>>>;

      struct __open_sender {
        using is_sender = void;
        using __id = __open_sender;
        using __t = __open_sender;
        template <class _Env>
          using __completions =
            completion_signatures<set_value_t(__async_scope<__x<_Env>>), set_stopped_t()>;

        template <__decays_to<__open_sender> _Self, receiver _Receiver>
            requires receiver_of<_Receiver, __completions<env_of_t<_Receiver>>>
          [[nodiscard]] friend __open_op_t<_Receiver>
          tag_invoke(connect_t, _Self&& __self, _Receiver __rcvr) {
            return __open_op_t<_Receiver>{
              __self.__context_,
              (_Receiver&&) __rcvr};
          }

        template <__decays_to<__open_sender> _Self, class _Env>
        friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
          -> dependent_completion_signatures<_Env>;
        template <__decays_to<__open_sender> _Self, __none_of<no_env> _Env>
        friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
          -> __completions<_Env>;

        friend empty_env tag_invoke(get_env_t, const __open_sender& __self) noexcept {
          return {};
        }

        mutable __ctx* __context_;
      };

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope::run implementation
    template <class _ReceiverId>
      struct __run_op : __task {
        using _Receiver = __t<_ReceiverId>;

        explicit __run_op(__ctx* __context, _Receiver __rcvr)
          : __task{{}, __context, __notify_waiter}
          , __rcvr_((_Receiver&&)__rcvr) {
            printf("__run_op this(%p), __notify_waiter(%p)\n", this, &__notify_waiter);
            printf("__impl this(%p)\n", &this->__impl_);
            printf("context this(%p), __phase_(%d)\n", this->__context_, this->__context_->__phase_);
            // cross-link __ctx and __impl
            __context->__impl_ = &this->__impl_;
            this->__impl_.__context_ = __context;

          printf("__run_op this(%p), this->__context_(%p), this->__notify_waiter(%p), this->__next_(%p)\n"
                 "  this->__context_->__impl_(%p), this->__context_->__impl_->__context_(%p)\n", 
                 this, this->__context_, this->__notify_waiter, this->__next_, this->__context_->__impl_, this->__context_->__impl_->__context_);
          printf("__run_op __impl_->__active_(%ld), __impl_->__run_(%p), __impl_->__close_(%p)\n"
                 "  __context_->__phase_(%d), __context_->__open_(%p)\n", 
                 this->__context_->__impl_->__active_, this->__context_->__impl_->__run_, this->__context_->__impl_->__close_,
                 this->__context_->__phase_, this->__context_->__open_);
          fflush(stdout);
        }

      private:
        static void __notify_waiter(__task* __self, __ctx::__phase __new_phase) noexcept {
          __run_op& __that = *static_cast<__run_op*>(__self);
          switch (__new_phase) {
          case __ctx::__phase::__constructed: 
            // fallthrough
          case __ctx::__phase::__pending: 
            // fallthrough
          case __ctx::__phase::__opening: 
            // fallthrough
          case __ctx::__phase::__running: 
            // fallthrough
          case __ctx::__phase::__closing: {
              constexpr bool phase_must_be_closed = false;
              STDEXEC_ASSERT(phase_must_be_closed);
              std::terminate();
            }
            break;
          case __ctx::__phase::__closed: {
              // closed
              _Receiver __rcvr{(_Receiver&&)__that.__rcvr_};
              set_value((_Receiver&&)__rcvr);
            }
            break;
          default: {
              constexpr bool phase_must_be_closed = false;
              STDEXEC_ASSERT(phase_must_be_closed);
              std::terminate();
            }
          };
        }

        void __start_() noexcept {
          STDEXEC_ASSERT(this->__context_ != nullptr);
          STDEXEC_ASSERT(this->__context_->__impl_ != nullptr);
          STDEXEC_ASSERT(this->__context_->__impl_->__context_ != nullptr);
          printf("__run_op start this(%p), __context_(%p), __phase(%d)\n", 
            this, this->__context_, this->__context_->__phase_);
          std::unique_lock __guard{this->__context_->__lock_};
          auto& __phase = this->__context_->__phase_;
          STDEXEC_ASSERT(this->__impl_.__run_ == nullptr);
          this->__impl_.__run_ = this;
          switch (__phase) {
          case __ctx::__phase::__constructed: {
              __phase = __ctx::__phase::__pending;
              // run won the race,
              // open() will enter the __opening phase
            }
            break;
          case __ctx::__phase::__pending: {
              __guard.unlock();
              constexpr bool phase_must_be_constructed_or_opening_or_running = false;
              STDEXEC_ASSERT(phase_must_be_constructed_or_opening_or_running);
              std::terminate();
            }
            break;
          case __ctx::__phase::__opening: 
            // fallthrough
          case __ctx::__phase::__running: {
              __phase = __ctx::__phase::__running;

              auto __open = std::exchange(this->__context_->__open_, nullptr);

              if (__open != nullptr) {
                __guard.unlock();
                __open->__notify_waiter(__open, __ctx::__phase::__running);
                __guard.lock();
              }
            }
            break;
          case __ctx::__phase::__closing:
            // fallthrough
          case __ctx::__phase::__closed: {
              // run will not change this state here.
            }
            break;
          default: {
              __guard.unlock();
              constexpr bool phase_must_be_constructed_or_opening_or_running = false;
              STDEXEC_ASSERT(phase_must_be_constructed_or_opening_or_running);
              std::terminate();
            }
            break;
          };
        }
        friend void tag_invoke(start_t, __run_op& __self) noexcept {
          return __self.__start_();
        }
        _Receiver __rcvr_;
        mutable __impl __impl_{};
      };

    template <class _Receiver>
      using __run_op_t =
        __run_op<__x<remove_cvref_t<_Receiver>>>;

      struct __run_sender {
        using is_sender = void;

        template <__decays_to<__run_sender> _Self, receiver _Receiver>
          [[nodiscard]] friend __run_op_t<_Receiver>
          tag_invoke(connect_t, _Self&& __self, _Receiver __rcvr) {
            return __run_op_t<_Receiver>{
              __self.__context_,
              (_Receiver&&) __rcvr};
          }

        template <__decays_to<__run_sender> _Self, class _Env>
          friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env)
            -> completion_signatures<set_value_t()>;

        friend empty_env tag_invoke(get_env_t, const __run_sender& __self) noexcept {
          return {};
        }

        mutable __ctx* __context_;
      };

    ////////////////////////////////////////////////////////////////////////////
    // counting_scope
    struct counting_scope : __immovable {
      ~counting_scope() {
        printf("~counting_scope (%p)\n", this);
      }
      counting_scope() {
        printf("counting_scope (%p)\n", this);
        printf("__ctx this(%p), __phase(%d)\n", &this->__ctx_, this->__ctx_.__phase_);
        // std::unique_lock __guard{this->__ctx_.__lock_};
      }

      template<class _Env>
      using token_t = __async_scope<__x<remove_cvref_t<_Env>>>;

    private:
      friend __open_sender
      tag_invoke(async_resource_t::open_t, const counting_scope& __self) {
        printf("counting_scope open this(%p)\n", &__self);
        printf("__ctx this(%p), __phase(%d)\n", &__self.__ctx_, __self.__ctx_.__phase_);
        return __open_sender{&__self.__ctx_};
      }

      friend __run_sender
      tag_invoke(async_resource_t::run_t, counting_scope& __self) {
        printf("counting_scope run this(%p)\n", &__self);
        printf("__ctx this(%p), __phase(%d)\n", &__self.__ctx_, __self.__ctx_.__phase_);
        return __run_sender{&__self.__ctx_};
      }

      mutable __ctx __ctx_{};
    };

    template <class _T>
    struct __value {
      ~__value() {
        printf("~__value (%p)\n", &value());
        value().~_T();
      }
      __value() requires constructible_from<_T> {
        printf("__value default placement new(%p) _T\n", &value());
        ::new (&value()) _T();
      }
      template <class _C0, class... _Cn>
        requires constructible_from<_T, _C0, _Cn...> && (!__decays_to<_Cn, __value> && ...) && (!__decays_to<_Cn, _T> && ...)
      explicit __value(_C0&& __c0, _Cn&&... __cn) {
        printf("__value placement new(%p) _T\n", &value());
        ::new (&value()) _T((_C0&&) __c0, (_Cn&&) __cn...);
      }

      // moved failure to runtime so that __deferred::__value_ variant can 
      // move around before _T is constructed, but not after
      __value(const __value&) {std::terminate();}
      __value(__value&&) {std::terminate();}
      __value& operator=(const __value&) {std::terminate(); return *this;}
      __value& operator=(__value&&) {std::terminate(); return *this;}

      using __storage_t = std::aligned_storage<sizeof(_T), alignof(_T)>;
      __storage_t __storage_;
      _T& value() {
        return *std::launder(reinterpret_cast<_T*>(&__storage_));
      }
      const _T& value() const {
        return *std::launder(reinterpret_cast<const _T*>(&__storage_));
      }
    };

    template <class _T, class... _An>
    struct __deferred {
      std::optional<std::tuple<_An...>> __args_;
      std::optional<__value<_T>> __value_;
      ~__deferred() {
        printf("~__deferred this(%p)\n", this);
      }
      template <class... _Cn>
        requires constructible_from<std::tuple<_An...>, _Cn...> && constructible_from<_T, _An&&...> && 
          (!__decays_to<_Cn, __deferred> && ...) && (!__decays_to<_Cn, _T> && ...)
      explicit __deferred(_Cn&&... __cn) : __args_(std::tuple<_An...>{(_Cn&&) __cn...}), __value_(std::nullopt) {
        printf("__deferred this(%p)\n", this);
        STDEXEC_ASSERT(!__value_.has_value());
      }
      __deferred(const __deferred& o) : __args_(o.__args_) { STDEXEC_ASSERT(!__value_.has_value()); }
      __deferred(__deferred&& o) : __args_(std::move(o.__args_)) { STDEXEC_ASSERT(!__value_.has_value()); }
      __deferred& operator=(const __deferred& o)  = delete;
      __deferred& operator=(__deferred&& o) = delete;
      void operator()() {
        STDEXEC_ASSERT(__args_.has_value());
        STDEXEC_ASSERT(!__value_.has_value());
        printf("__deferred construct _T, this(%p)\n", this);
        std::apply(
          [this](_An&&... __an){
            __value_.emplace((_An&&) __an...);
          }, 
          std::move(__args_.value()));
        __args_.reset();
        printf("__deferred value %d, (%p), this(%p)\n", __value_.has_value(), &__value_.value(), this);
        fflush(stdout);
        STDEXEC_ASSERT(!__args_.has_value());
        STDEXEC_ASSERT(__value_.has_value());
      }
      _T& value() {
        printf("__deferred::value() value %d, (%p), this(%p)\n", __value_.has_value(), __value_.has_value() ? &__value_.value() : nullptr, this);
        fflush(stdout);
        STDEXEC_ASSERT(!__args_.has_value());
        STDEXEC_ASSERT(__value_.has_value());
        return __value_.value().value();
      }
      const _T& value() const {
        printf("__deferred::value() value %d, (%p), this(%p)\n", __value_.has_value(), __value_.has_value() ? &__value_.value() : nullptr, this);
        fflush(stdout);
        STDEXEC_ASSERT(!__args_.has_value());
        STDEXEC_ASSERT(__value_.has_value());
        return __value_.value().value();
      }
      _T& operator*() {return value();}
      const _T& operator*() const {return value();}
      _T* operator->() {return &value();}
      const _T* operator->() const {return &value();}
    };
    template <class _T>
    struct make_deferred_t {
      template <class... _An>
      using deferred_t = __deferred<_T, remove_cvref_t<_An>...>;
      template <class... _An>
      deferred_t<_An...> operator()(_An&&... __an) const {
        return deferred_t<_An...>{(_An&&) __an...};
      }
    };

    struct use_resources_t {
      template <class _UseFn, class... _Rn>
        requires (__callable<_Rn> && ...)
      auto operator()(_UseFn __usefn, _Rn... __rn) const {
        return 
          // store the values in a stable location
          let_value(
            just((_UseFn&&) __usefn, (_Rn&&) __rn...), 
            [](auto& __usefn, auto&... __rn){
              printf("use_resources_t __usefn at (%p) end at (%p)\n", &__usefn, &__usefn + 1);
              // construct the deferred resources in 
              // the stable location
              (__rn(), ...);
              (printf("use_resources_t constructed _T at (%p)\n", &__rn.value()), ...);

              auto __open = let_value(just(), [&__rn...]{ 
                (printf("use_resources_t open _T at (%p)\n", &__rn.value()), ...);
                return when_all(async_resource_t{}.open(*__rn)...); });
              auto __use = 
                let_value(
                  std::move(__open), 
                  [&__usefn, &__rn...](auto... __tkn){
                    (printf("use_resources_t use _T at (%p)\n", &__rn.value()), ...);
                    auto __close = let_value(just(__tkn...), [&__rn...](auto... __tkn){
                      (printf("use_resources_t close _T at (%p)\n", &__rn.value()), ...);
                      return when_all(async_resource_token_t{}.close(__tkn)...); });
                    auto __final = [__close = std::move(__close)](auto&&...){ return __close; };
                    auto __use = __usefn(__tkn...);
                    auto __use_and_close = let_value(let_error(let_stopped(std::move(__use), __final), __final), __final);
                    return __use_and_close;
                  });
              auto __run = let_value(just(), [&__rn..., __use = std::move(__use)]{ 
                (printf("use_resources_t run _T at (%p)\n", &__rn.value()), ...);
                return when_all(async_resource_t{}.run(*__rn)..., __use);});

              return __run;
            });
      }
    };

  } // namespace __scope

  using __scope::async_scope_t;
  inline constexpr async_scope_t async_scope{};

  using __scope::counting_scope;

  using __scope::make_deferred_t;
  template <class _T>
  inline constexpr make_deferred_t<_T> make_deferred{};

  using __scope::use_resources_t;
  inline constexpr use_resources_t use_resources{};
} // namespace exec
