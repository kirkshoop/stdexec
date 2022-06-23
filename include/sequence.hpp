/*
 * Copyright (c) NVIDIA
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

#include <execution.hpp>

namespace std::execution::P0TBD {

  /////////////////////////////////////////////////////////////////////////////
  // [execution.senders.sequence_connect]
  namespace __sequence_connect {
    struct sequence_connect_t;

    template <class _Sender, class _Receiver, class _ValueAdapter>
      concept __sequence_connectable_sender_with =
        sender<_Sender, env_of_t<_Receiver>> &&
        __receiver_from<_Receiver, _Sender> &&
        tag_invocable<sequence_connect_t, _Sender, _Receiver, _ValueAdapter>;

    struct sequence_connect_t {
      template <class _Sender, class _Receiver, class _ValueAdapter>
        requires __sequence_connectable_sender_with<_Sender, _Receiver, _ValueAdapter>
      auto operator()(_Sender&& __sndr, _Receiver&& __rcvr, _ValueAdapter&& __adapt) const
        noexcept(nothrow_tag_invocable<sequence_connect_t, _Sender, _Receiver, _ValueAdapter>)
        -> tag_invoke_result_t<sequence_connect_t, _Sender, _Receiver, _ValueAdapter> {
        static_assert(
          operation_state<tag_invoke_result_t<sequence_connect_t, _Sender, _Receiver, _ValueAdapter>>,
          "execution::sequence_connect(sender, receiver, value_adapter) must return a type that "
          "satisfies the operation_state concept");
        return tag_invoke(sequence_connect_t{}, (_Sender&&) __sndr, (_Receiver&&) __rcvr, (_ValueAdapter&&) __adapt);
      }
#if 0
      template <class _Awaitable, class _Receiver>
        requires (!__sequence_connectable_sender_with<_Awaitable, _Receiver>) &&
          __callable<__connect_awaitable_t, _Awaitable, _Receiver>
      auto operator()(_Awaitable&& __await, _Receiver&& __rcvr) const
        -> __connect_awaitable_::__operation_t<_Receiver> {
        return __connect_awaitable((_Awaitable&&) __await, (_Receiver&&) __rcvr);
      }
#endif
#if 0
      // This overload is purely for the purposes of debugging why a
      // sender will not connect. Use the __debug_sender function below.
      template <class _Sender, class _Receiver>
        requires (!__sequence_connectable_sender_with<_Sender, _Receiver>) &&
           (!__callable<__connect_awaitable_t, _Sender, _Receiver>) &&
           tag_invocable<__is_debug_env_t, env_of_t<_Receiver>>
      auto operator()(_Sender&& __sndr, _Receiver&& __rcvr) const
        -> __debug_op_state {
        // This should generate an instantiate backtrace that contains useful
        // debugging information.
        using std::__tag_invoke::tag_invoke;
        return tag_invoke(*this, (_Sender&&) __sndr, (_Receiver&&) __rcvr);
      }
#endif
    };

  } // namespace __connect

  using __sequence_connect::sequence_connect_t;
  inline constexpr __sequence_connect::sequence_connect_t sequence_connect {};

    /////////////////////////////////////////////////////////////////////////////
  // [execution.senders.factories]
  namespace __itos {

    template <class _CPO, class... _Ts>
    using __completion_signatures_ = completion_signatures<_CPO(_Ts...)>;

    template <class _CPO, class... _Ts>
    struct __value_sender {
        using completion_signatures = __completion_signatures_<_CPO, _Ts...>;

        template <class _ReceiverId, class _ValueAdapterId>
          struct __operation : __immovable {
            using _Receiver = __t<_ReceiverId>;
            using _ValueAdapter = __t<_ValueAdapterId>;
            tuple<_Ts...> __vals_;
            _Receiver __rcvr_;
            _ValueAdapter __adapt_;

            friend void tag_invoke(start_t, __operation& __op_state) noexcept {
              static_assert(__nothrow_callable<_CPO, _Receiver, _Ts...>);
              std::apply([&__op_state](_Ts&... __ts) {
                _CPO{}((_Receiver&&) __op_state.__rcvr_, (_Ts&&) __ts...);
              }, __op_state.__vals_);
            }
          };

        template <class _Receiver, class _ValueAdapter>
          requires (copy_constructible<_Ts> &&...)
        friend auto tag_invoke(sequence_connect_t, const __value_sender& __sndr, _Receiver&& __rcvr, const _ValueAdapter& __adapt)
          noexcept((is_nothrow_copy_constructible_v<_Ts> &&...))
          -> __operation<__x<remove_cvref_t<_Receiver>>, __x<remove_cvref_t<_ValueAdapter>>> {
          return {{}, __sndr.__vals_, (_Receiver&&) __rcvr, (_ValueAdapter&&) __adapt};
        }

        template <class _Receiver, class _ValueAdapter>
        friend auto tag_invoke(sequence_connect_t, __value_sender&& __sndr, _Receiver&& __rcvr, _ValueAdapter&& __adapt)
          noexcept((is_nothrow_move_constructible_v<_Ts> &&...))
          -> __operation<__x<remove_cvref_t<_Receiver>>, __x<remove_cvref_t<_ValueAdapter>>> {
          return {{}, ((__value_sender&&) __sndr).__vals_, (_Receiver&&) __rcvr, (_ValueAdapter&&) __adapt};
        }
    };

    template <class _CPO>
      struct __sender {
        using completion_signatures = __completion_signatures_<_CPO()>;
        template <class _ReceiverId, class _ValueAdapterId>
          struct __operation : __immovable {
            using _Receiver = __t<_ReceiverId>;
            using _ValueAdapter = __t<_ValueAdapterId>;
            _Receiver __rcvr_;
            _ValueAdapter __adapt_;

            friend void tag_invoke(start_t, __operation& __op_state) noexcept {
              static_assert(__nothrow_callable<_CPO, _Receiver, _Ts...>);
              std::apply([&__op_state](_Ts&... __ts) {
                _CPO{}((_Receiver&&) __op_state.__rcvr_, (_Ts&&) __ts...);
              }, __op_state.__vals_);
            }
          };

        template <class _Receiver, class _ValueAdapter>
          requires (copy_constructible<_Ts> &&...)
        friend auto tag_invoke(sequence_connect_t, const __sender& __sndr, _Receiver&& __rcvr, const _ValueAdapter& __adapt)
          noexcept((is_nothrow_copy_constructible_v<_Ts> &&...))
          -> __operation<__x<remove_cvref_t<_Receiver>>, __x<remove_cvref_t<_ValueAdapter>>> {
          return {{}, __sndr.__vals_, (_Receiver&&) __rcvr, (_ValueAdapter&&) __adapt};
        }

        template <class _Receiver, class _ValueAdapter>
        friend auto tag_invoke(sequence_connect_t, __sender&& __sndr, _Receiver&& __rcvr, _ValueAdapter&& __adapt)
          noexcept((is_nothrow_move_constructible_v<_Ts> &&...))
          -> __operation<__x<remove_cvref_t<_Receiver>>, __x<remove_cvref_t<_ValueAdapter>>> {
          return {{}, ((__sender&&) __sndr).__vals_, (_Receiver&&) __rcvr, (_ValueAdapter&&) __adapt};
        }
      };

    inline constexpr struct __itos_t {
      template <__movable_value... _Ts>
      __sender<set_value_t, decay_t<_Ts>...> operator()(_Ts&&... __ts) const
        noexcept((is_nothrow_constructible_v<decay_t<_Ts>, _Ts> &&...)) {
        return {{(_Ts&&) __ts...}};
      }
    } itos {};
  }
  using __itos::itos;

  /////////////////////////////////////////////////////////////////////////////
  // [execution.senders.adaptors.ignore_all]
  namespace __ignore_all {
    template <class _ReceiverId, class _FunId>
      class __receiver
        : receiver_adaptor<__receiver<_ReceiverId, _FunId>, __t<_ReceiverId>> {
        using _Receiver = __t<_ReceiverId>;
        using _Fun = __t<_FunId>;
        friend receiver_adaptor<__receiver, _Receiver>;
        [[no_unique_address]] _Fun __f_;

        // Customize set_value by invoking the invocable and passing the result
        // to the base class
        template <class... _As>
          requires invocable<_Fun, _As...> &&
            tag_invocable<set_value_t, _Receiver, invoke_result_t<_Fun, _As...>>
        void set_value(_As&&... __as) && noexcept try {
          execution::set_value(
              ((__receiver&&) *this).base(),
              std::invoke((_Fun&&) __f_, (_As&&) __as...));
        } catch(...) {
          execution::set_error(
              ((__receiver&&) *this).base(),
              current_exception());
        }
        // Handle the case when the invocable returns void
        template <class _R2 = _Receiver, class... _As>
          requires invocable<_Fun, _As...> &&
            same_as<void, invoke_result_t<_Fun, _As...>> &&
            tag_invocable<set_value_t, _R2>
        void set_value(_As&&... __as) && noexcept try {
          std::invoke((_Fun&&) __f_, (_As&&) __as...);
          execution::set_value(((__receiver&&) *this).base());
        } catch(...) {
          execution::set_error(
              ((__receiver&&) *this).base(),
              current_exception());
        }

       public:
        explicit __receiver(_Receiver __rcvr, _Fun __fun)
          : receiver_adaptor<__receiver, _Receiver>((_Receiver&&) __rcvr)
          , __f_((_Fun&&) __fun)
        {}
      };

    template <class _SenderId, class _FunId>
      struct __sender {
        using _Sender = __t<_SenderId>;
        using _Fun = __t<_FunId>;
        template <receiver _Receiver>
          using __receiver = __receiver<__x<remove_cvref_t<_Receiver>>, _FunId>;

        [[no_unique_address]] _Sender __sndr_;
        [[no_unique_address]] _Fun __fun_;

        template <class... _Args>
            requires invocable<_Fun, _Args...>
          using __set_value =
            completion_signatures<
              __minvoke1<
                __remove<void, __qf<set_value_t>>,
                invoke_result_t<_Fun, _Args...>>>;

        template <class _Self, class _Env>
          using __completion_signatures =
            make_completion_signatures<
              __member_t<_Self, _Sender>, _Env, __with_exception_ptr, __set_value>;

        template <__decays_to<__sender> _Self, receiver _Receiver>
          requires sender_to<__member_t<_Self, _Sender>, __receiver<_Receiver>>
        friend auto tag_invoke(connect_t, _Self&& __self, _Receiver&& __rcvr)
          noexcept(__has_nothrow_connect<__member_t<_Self, _Sender>, __receiver<_Receiver>>)
          -> connect_result_t<__member_t<_Self, _Sender>, __receiver<_Receiver>> {
          return execution::connect(
              ((_Self&&) __self).__sndr_,
              __receiver<_Receiver>{(_Receiver&&) __rcvr, ((_Self&&) __self).__fun_});
        }

        template <__decays_to<__sender> _Self, class _Env>
        friend auto tag_invoke(get_completion_signatures_t, _Self&&, _Env) ->
          __completion_signatures<_Self, _Env>;

        // forward sender queries:
        template <__sender_queries::__sender_query _Tag, class... _As>
          requires __callable<_Tag, const _Sender&, _As...>
        friend auto tag_invoke(_Tag __tag, const __sender& __self, _As&&... __as)
          noexcept(__nothrow_callable<_Tag, const _Sender&, _As...>)
          -> __call_result_if_t<__sender_queries::__sender_query<_Tag>, _Tag, const _Sender&, _As...> {
          return ((_Tag&&) __tag)(__self.__sndr_, (_As&&) __as...);
        }
      };

    struct ignore_all_t {
      template <class _Sender, class _Fun>
        using __sender = __sender<__x<remove_cvref_t<_Sender>>, __x<remove_cvref_t<_Fun>>>;

      template <sender _Sender, __movable_value _Fun>
        requires __tag_invocable_with_completion_scheduler<ignore_all_t, set_value_t, _Sender, _Fun>
      sender auto operator()(_Sender&& __sndr, _Fun __fun) const
        noexcept(nothrow_tag_invocable<ignore_all_t, __completion_scheduler_for<_Sender, set_value_t>, _Sender, _Fun>) {
        auto __sched = get_completion_scheduler<set_value_t>(__sndr);
        return tag_invoke(ignore_all_t{}, std::move(__sched), (_Sender&&) __sndr, (_Fun&&) __fun);
      }
      template <sender _Sender, __movable_value _Fun>
        requires (!__tag_invocable_with_completion_scheduler<ignore_all_t, set_value_t, _Sender, _Fun>) &&
          tag_invocable<ignore_all_t, _Sender, _Fun>
      sender auto operator()(_Sender&& __sndr, _Fun __fun) const
        noexcept(nothrow_tag_invocable<ignore_all_t, _Sender, _Fun>) {
        return tag_invoke(ignore_all_t{}, (_Sender&&) __sndr, (_Fun&&) __fun);
      }
      template <sender _Sender, __movable_value _Fun>
        requires
          (!__tag_invocable_with_completion_scheduler<ignore_all_t, set_value_t, _Sender, _Fun>) &&
          (!tag_invocable<ignore_all_t, _Sender, _Fun>) &&
          sender<__sender<_Sender, _Fun>>
      __sender<_Sender, _Fun> operator()(_Sender&& __sndr, _Fun __fun) const {
        return __sender<_Sender, _Fun>{(_Sender&&) __sndr, (_Fun&&) __fun};
      }
      template <class _Fun>
      __binder_back<ignore_all_t, _Fun> operator()(_Fun __fun) const {
        return {{}, {}, {(_Fun&&) __fun}};
      }
    };
  }
  using __ignore_all::ignore_all_t;
  inline constexpr ignore_all_t ignore_all{};


} // namespace std::execution::P0TBD
