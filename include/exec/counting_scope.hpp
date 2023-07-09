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
#include "../exec/async_resource.hpp"
#include "../stdexec/__detail/__intrusive_queue.hpp"
#include "env.hpp"

namespace exec {

  /////////////////////////////////////////////////////////////////////////////
  // counting_scope
  namespace __counting_scope {
    using namespace stdexec;

    enum class __phase {
      INVALID = 0,
      Constructed,
      Running,
      Unused,
      End
    };

    struct __object {
      struct __t {
        using __id = __object;
        std::mutex __lock_{};
        std::ptrdiff_t __active_;
        __phase __phase_;

        virtual void unused() noexcept = 0;
        virtual void end() noexcept = 0;

        virtual ~__t() {
          std::unique_lock __guard{__lock_};
          STDEXEC_ASSERT(__active_ == 0);
        }

        __t() : __active_(0), __phase_(__phase::Constructed) {}
      };
    };

    namespace __nest {

      struct __object {
        struct __t {
          using __id = __object;
          stdexec::__t<__counting_scope::__object>* __obj_;
          explicit __t(stdexec::__t<__counting_scope::__object>* __obj) : __obj_(__obj) {}
        };
      };

      template <class _ReceiverId>
      struct __receiver {
        using _Receiver = stdexec::__t<_ReceiverId>;

        struct __t {
          using __id = __receiver;

          using is_receiver = void;

          stdexec::__t<__object>* __obj_;
          _Receiver* __rcvr_;

          void complete(stdexec::__t<__counting_scope::__object>* __obj) noexcept {
            std::unique_lock __guard{__obj->__lock_};
            if (--__obj->__active_ == 0 && __obj->__phase_ == __phase::Unused) {
              __guard.unlock();
              __obj->end();
            }
          }

          template<class... _Vn> 
          friend void tag_invoke(set_value_t, __t&& __self, _Vn... __vn) noexcept {
            stdexec::set_value(static_cast<_Receiver&&>(*__self.__rcvr_), static_cast<_Vn&&>(__vn)...);
            __self.complete(__self.__obj_->__obj_);
          }

          template<class _Error> 
          friend void tag_invoke(set_error_t, __t&& __self, _Error __e) noexcept {
            stdexec::set_error(static_cast<_Receiver&&>(*__self.__rcvr_), static_cast<_Error&&>(__e));
            __self.complete(__self.__obj_->__obj_);
          }

          friend void tag_invoke(set_stopped_t, __t&& __self) noexcept {
            stdexec::set_stopped(static_cast<_Receiver&&>(*__self.__rcvr_));
            __self.complete(__self.__obj_->__obj_);
          }
        };
      };

      template <class _SenderId, class _ReceiverId>
      struct __operation {
        using _Sender = stdexec::__t<_SenderId>;
        using _Receiver = stdexec::__t<_ReceiverId>;
        using __rcvr = stdexec::__t<__receiver<_ReceiverId>>;
        using _Op = connect_result_t<_Sender, __rcvr>;

        struct __t : stdexec::__t<__object> {
          using __id = __operation;
          STDEXEC_NO_UNIQUE_ADDRESS _Receiver __rcvr_;
          STDEXEC_NO_UNIQUE_ADDRESS _Op __op_;

          template<__decays_to<_Sender> _S, __decays_to<_Receiver> _R>
          __t(stdexec::__t<__counting_scope::__object>* __obj, _S __s, _R __r) : 
            stdexec::__t<__object>(__obj), 
            __rcvr_(static_cast<_R&&>(__r)),
            __op_(stdexec::connect(static_cast<_S&&>(__s), __rcvr{this, &this->__rcvr_})) {}

          friend void tag_invoke(start_t, __t& __self) noexcept {
            std::unique_lock __guard{__self.__obj_->__lock_};
            ++__self.__obj_->__active_;
            __guard.unlock();
            stdexec::start(__self.__op_);
          }
        };
      };

      template <class _SenderId>
      struct __sender {
        using _Sender = stdexec::__t<_SenderId>;

        struct __t {
          using __id = __sender;
          using is_sender = void;
          using completion_signatures = stdexec::completion_signatures_of_t<_Sender>;
          stdexec::__t<__counting_scope::__object>* __obj_;
          STDEXEC_NO_UNIQUE_ADDRESS _Sender __sndr_;

          template <__decays_to<__t> _Self, receiver_of<completion_signatures> _Rcvr>
          friend stdexec::__t<__operation<_SenderId, stdexec::__id<__decay_t<_Rcvr>>>>
            tag_invoke(connect_t, _Self&& __self, _Rcvr&& __rcvr) noexcept(__nothrow_decay_copyable<_Rcvr>) {
            return stdexec::__t<__operation<_SenderId, stdexec::__id<__decay_t<_Rcvr>>>>{
              static_cast<_Self&&>(__self).__obj_, static_cast<_Self&&>(__self).__sndr_, static_cast<_Rcvr&&>(__rcvr)};
          }
        };
      };
    }  // namespace __nest

    namespace __future {
      enum class __phase {
        INVALID = 0,
        Constructed,
        Started,
        ConsumingFirst,
        AbandonedFirst,
        StoredFirst,
        End,
      };

      template <class _CvrefSenderId>
      struct __state {
        using _CvrefSender = stdexec::__cvref_t<_CvrefSenderId>;

        template <class... _Ts>
        using __bind_tuples = //
          __mbind_front_q<
            __variant,
            std::tuple<set_stopped_t>, // Initial state of the variant is set_stopped
            _Ts...>;

        using __bound_values_t = //
          __value_types_of_t<
            _CvrefSender,
            empty_env,
            __mbind_front_q<__decayed_tuple, set_value_t>,
            __q<__bind_tuples>>;

        using __variant_t = //
          __error_types_of_t<
            _CvrefSender,
            empty_env,
            __transform< __mbind_front_q<__decayed_tuple, set_error_t>, __bound_values_t>>;

        struct __t;
      };

      template <class _CvrefSenderId>
      struct __receiver {
        using _CvrefSender = stdexec::__cvref_t<_CvrefSenderId>;

        struct __t {
          using __id = __receiver;

          using is_receiver = void;

          stdexec::__t<__state<_CvrefSenderId>>* __st_;

          template<__completion_tag _Tag, class... _Vn> 
          friend void tag_invoke(_Tag tag, __t&& __self, _Vn... __vn) noexcept {
            std::unique_lock __guard{__self.__st_->__obj_->__lock_};
            if (__self.__st_->__phase_ == __phase::AbandonedFirst) {
              __guard.unlock();
              __self.__st_->__abandoned_self_.reset(); 
            } else if (__self.__st_->__phase_ == __phase::ConsumingFirst) {
              __self.__st_->__data_.template emplace<std::tuple<_Tag, _Vn...>>(tag, static_cast<_Vn&&>(__vn)...);
              auto __ender = std::exchange(__self.__st_->__ender_, nullptr);
              __guard.unlock();
              __ender->end();
            } else if (__self.__st_->__phase_ == __phase::Started) {
              std::exchange(__self.__st_->__phase_, __phase::StoredFirst);
              __self.__st_->__data_.template emplace<std::tuple<_Tag, _Vn...>>(tag, static_cast<_Vn&&>(__vn)...);
            } else {
              std::terminate();
            }
          }
        };
      };

      struct __ender {
        virtual ~__ender() {}
        virtual void end() noexcept = 0;
      };

      template <class _CvrefSenderId>
      struct __state<_CvrefSenderId>::__t {
        using __id = __state;

        using __rcvr = stdexec::__t<__receiver<_CvrefSenderId>>;
        using __op = connect_result_t<_CvrefSender, __rcvr>;

        __t(stdexec::__t<__counting_scope::__object>* __obj, _CvrefSender __s) : 
          __abandoned_self_(nullptr),
          __ender_(nullptr),
          __obj_(__obj),
          __phase_(__phase::Constructed),
          __data_(),
          __op_(stdexec::connect(__s, __rcvr{this})) {
            std::exchange(__phase_, __phase::Started);
            stdexec::start(__op_);
          }

        std::unique_ptr<__t> __abandoned_self_;
        __ender* __ender_;
        stdexec::__t<__counting_scope::__object>* __obj_;
        __phase __phase_;
        STDEXEC_NO_UNIQUE_ADDRESS __variant_t __data_;
        STDEXEC_NO_UNIQUE_ADDRESS __op __op_;
      };

      template <class _CvrefSenderId, class _ReceiverId>
      struct __operation {
        using _CvrefSender = stdexec::__cvref_t<_CvrefSenderId>;
        using _Receiver = stdexec::__t<_ReceiverId>;
        using _Nest = stdexec::__t<__nest::__sender<stdexec::__id<__decay_t<_CvrefSender>>>>;
        using _State = stdexec::__t<__state<stdexec::__id<_Nest>>>;
        using __env = env_of_t<_Receiver>;

        struct __t : __ender {
          using __id = __operation;

          std::unique_ptr<_State> __st_;
          STDEXEC_NO_UNIQUE_ADDRESS _Receiver __rcvr_;

          ~__t() {
            if (!__st_) { return; }
            std::unique_lock __guard{__st_->__obj_->__lock_};
            if (__st_->__phase_ == __phase::Started) {
              std::exchange(__st_->__phase_, __phase::AbandonedFirst);
              __st_->__abandoned_self_ = std::move(__st_);
            } else if (__st_->__phase_ == __phase::StoredFirst) {
              std::exchange(__st_->__phase_, __phase::End);
            } else {
              std::terminate();
            }
          }

          __t(std::unique_ptr<_State> __st, _Receiver __r) : 
            __st_(std::move(__st)), 
            __rcvr_(static_cast<_Receiver&&>(__r)) {}

          void deliver_result() noexcept {
            std::unique_lock __guard{__st_->__obj_->__lock_};
            auto __st = std::move(__st_);
            std::visit(
              [&](const auto& __tupl) noexcept -> void {
                std::apply(
                  [&](auto __tag, const auto&... __args) noexcept -> void {
                    __guard.unlock();
                    __tag((_Receiver&&) __rcvr_, __args...);
                    // 'this' is invalid
                  },
                  __tupl);
              },
              __st->__data_);
            std::exchange(__st->__phase_, __phase::End);
          }

          void end() noexcept override {
            deliver_result();
          }

          friend void tag_invoke(start_t, __t& __self) noexcept {
            std::unique_lock __guard{__self.__st_->__obj_->__lock_};
            if (__self.__st_->__phase_ == __phase::Started) {
              std::exchange(__self.__st_->__phase_, __phase::ConsumingFirst);
              __self.__st_->__ender_ = &__self;
            } else if (__self.__st_->__phase_ == __phase::StoredFirst) {
              __guard.unlock();
              __self.deliver_result();
            } else {
              std::terminate();
            }
          }
        };
      };

      template <class _CvrefSenderId>
      struct __sender {
        using _CvrefSender = stdexec::__cvref_t<_CvrefSenderId>;

        struct __t {
          using __id = __sender;
          using is_sender = void;
          using completion_signatures = stdexec::completion_signatures_of_t<_CvrefSender>;

          using _Nest = stdexec::__t<__nest::__sender<stdexec::__id<__decay_t<_CvrefSender>>>>;
          using _State = stdexec::__t<__state<stdexec::__id<_Nest>>>;

          std::unique_ptr<_State> __st_;

          ~__t() {
            if (!__st_) { return; }
            std::unique_lock __guard{__st_->__obj_->__lock_};
            if (__st_->__phase_ == __phase::Started) {
              std::exchange(__st_->__phase_, __phase::AbandonedFirst);
              __st_->__abandoned_self_ = std::move(__st_);
            } else if (__st_->__phase_ == __phase::StoredFirst) {
              std::exchange(__st_->__phase_, __phase::End);
            } else {
              std::terminate();
            }
          }

          explicit __t(std::unique_ptr<_State> st) noexcept : __st_(std::move(st)) {}

          __t(__t&& o) noexcept : __st_(std::move(o.__st_)) {}
          __t& operator=(__t&& o) noexcept {
            __st_ = std::move(o.__st_);
            return *this;
          }

          template <__decays_to<__t> _Self, receiver_of<completion_signatures> _Rcvr>
          friend stdexec::__t<__operation<_CvrefSenderId, stdexec::__id<__decay_t<_Rcvr>>>>
            tag_invoke(connect_t, _Self&& __self, _Rcvr&& __rcvr) noexcept(__nothrow_decay_copyable<_Rcvr>) {
            // transfer ownership to the operation
            return stdexec::__t<__operation<_CvrefSenderId, stdexec::__id<__decay_t<_Rcvr>>>>{
              std::move(static_cast<_Self&&>(__self).__st_), static_cast<_Rcvr&&>(__rcvr)};
          }
        };
      };

    }  // namespace __future

    struct __ignore {
      struct __t {
        using __id = __ignore;

        using is_receiver = void;

        friend void tag_invoke(set_value_t, __t&& __self) noexcept {
        }
        friend void tag_invoke(set_stopped_t, __t&& __self) noexcept {
        }
      };
    };

    struct __token {
      struct __t {
        using __id = __token;
        stdexec::__t<__object>* __obj_;

        template <__decays_to<__t> _Self, sender _Sender>
        friend stdexec::__t<__nest::__sender<stdexec::__id<__decay_t<_Sender>>>>
          tag_invoke(nest_t, _Self&& __self, _Sender&& __sndr) noexcept(__nothrow_decay_copyable<_Sender>) {
          return {__self.__obj_, static_cast<_Sender&&>(__sndr)};
        }

        template <__decays_to<__t> _Self, sender _Sender>
        friend void
          tag_invoke(spawn_t, _Self&& __self, _Sender&& __sndr) noexcept(__nothrow_decay_copyable<_Sender>) {
          using _Nest = __call_result_t<nest_t, _Self, _Sender>;
          using _Op = connect_result_t<_Nest, stdexec::__t<__ignore>>;

          _Nest __s = exec::nest(
            static_cast<_Self&&>(__self), 
            static_cast<_Sender&&>(__sndr));
          std::unique_ptr<_Op> __op{std::make_unique<_Op>(__conv{
            [&]() {
              return stdexec::connect(std::move(__s), stdexec::__t<__ignore>{});
            }
          })};
          stdexec::start(*__op.release());
          return;
        }

        template <__decays_to<__t> _Self, sender _Sender>
        friend stdexec::__t<__future::__sender<stdexec::__cvref_id<_Sender>>>
          tag_invoke(spawn_future_t, _Self&& __self, _Sender&& __sndr) noexcept(__nothrow_decay_copyable<_Sender>) {
          using _Nest = __call_result_t<nest_t, _Self, _Sender>;
          using __state = stdexec::__t<__future::__state<stdexec::__cvref_id<_Nest>>>;
          std::unique_ptr<__state> __st{
            std::make_unique<__state>(
              __self.__obj_, 
              exec::nest(
                static_cast<_Self&&>(__self), 
                static_cast<_Sender&&>(__sndr)))};
          return stdexec::__t<__future::__sender<stdexec::__cvref_id<_Sender>>>{std::move(__st)};
        }
      };
    };

    struct __unused_rcvr {
      struct __t {
        using __id = __unused_rcvr;

        using is_receiver = void;

        stdexec::__t<__object>* __obj_;

        friend void tag_invoke(set_value_t, __t&& __self) noexcept {
          __self.__obj_->unused();
        }
        friend void tag_invoke(set_stopped_t, __t&& __self) noexcept {
          __self.__obj_->unused();
        }
      };
    };

    template <class _ReceiverId>
    struct __operation {
      using _Receiver = stdexec::__t<_ReceiverId>;
      using _Just = __call_result_t<__just::__just_t, stdexec::__t<__token>>;
      using _Inuse = __next_sender_of_t<_Receiver, _Just>;
      using _Op = connect_result_t<_Inuse, stdexec::__t<__unused_rcvr>>;

      struct __t : stdexec::__t<__object> {
        using __id = __operation;
        STDEXEC_NO_UNIQUE_ADDRESS _Receiver __rcvr_;
        STDEXEC_NO_UNIQUE_ADDRESS std::optional<_Op> __op_;

        explicit __t(_Receiver __r) : 
          stdexec::__t<__object>(),
          __rcvr_(static_cast<_Receiver&&>(__r)) {}

        friend void tag_invoke(start_t, __t& __self) noexcept {
          _Inuse s = exec::set_next(__self.__rcvr_, just(stdexec::__t<__token>{&__self}));
          (void)s;

          auto cnct = [&] {
              return connect(static_cast<_Inuse&&>(s), stdexec::__t<__unused_rcvr>{&__self});
            };
          static_assert(std::is_constructible_v<std::optional<_Op>, decltype(__conv{cnct})>);
          __self.__op_.emplace(__conv{cnct});
          start(*__self.__op_);
        }

        void unused() noexcept override {
          std::unique_lock __guard{__lock_};
          std::exchange(__phase_, __phase::Unused);
          if (__active_ == 0) {
            std::exchange(__phase_, __phase::End);
            __guard.unlock();
            stdexec::set_value(static_cast<_Receiver&&>(__rcvr_));
          }
        }

        void end() noexcept override {
          std::unique_lock __guard{__lock_};
          std::exchange(__phase_, __phase::End);
          __guard.unlock();
          stdexec::set_value(static_cast<_Receiver&&>(__rcvr_));
        }
      };
    };

    struct __sender {
      struct __t {
        using __id = __sender;
        using is_sender = sequence_tag;
        using completion_signatures = stdexec::completion_signatures<set_value_t(stdexec::__t<__token>), set_stopped_t()>;

        template <__decays_to<__t> _Self, sequence_receiver_of<completion_signatures> _Rcvr>
        friend auto
          tag_invoke(subscribe_t, _Self&&, _Rcvr&& __rcvr) noexcept(__nothrow_decay_copyable<_Rcvr>) {
          return stdexec::__t<__operation<stdexec::__id<__decay_t<_Rcvr>>>>{
            static_cast<_Rcvr&&>(__rcvr)};
        }
      };
    };

    struct __resource {
      struct __t {
        using __id = __resource;

        template<class _Env>
        using token = stdexec::__t<__token>;

        template <__decays_to<__t> _Self>
        friend stdexec::__t<__sender>
          tag_invoke(run_t, _Self&&) noexcept {
          return stdexec::__t<__sender>{};
        }
      };
    };

    struct counting_scope_t {
      stdexec::__t<__resource> operator()() const noexcept {
        return {};
      }
    };
  } // namespace __counting_scope

  using __counting_scope::counting_scope_t;
  inline constexpr counting_scope_t counting_scope{};
} // namespace exec
