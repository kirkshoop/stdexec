/*
 * Copyright (c) 2023 Maikel Nadolski
 * Copyright (c) 2023 NVIDIA Corporation
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

#include "../../stdexec/concepts.hpp"
#include "../../stdexec/execution.hpp"
#include "stdexec/__detail/__concepts.hpp"
#include "stdexec/__detail/__config.hpp"
#include "stdexec/__detail/__storage.hpp"
#include <concepts>

namespace experimental::execution {
  namespace __notification {
    using namespace STDEXEC;

    //
    // notification_t provides storage for any one of the
    // completions in the specified completion_signatures<>
    //
    // notification_t can be compared, visited by a receiver
    // to emit the stored signal, and provide a sender that will
    // complete with the stored signal
    //

    template <class... Args>
    using __as_rvalues = set_value_t (*)(__decay_t<Args>...);

    template <class... E>
    using __as_error = set_error_t (*)(__decay_t<E>...);

    // Here we convert all set_value(Args...) and set_error(Args...) to use decayed arguments and
    // unconditionally add set_stopped(). The indirection through the __completions_fn is to avoid
    // a pack expansion bug in nvc++.
    struct __completions_fn
    {
      template <class _CompletionSignatures>
      using __decayed_completions_t =
        __transform_reduce_completion_signatures_t<_CompletionSignatures,
                                                   __as_rvalues,
                                                   __as_error,
                                                   set_stopped_t (*)(),
                                                   __completion_signature_ptrs_t>;

      template <class _CompletionSignatures>
      using __all_nothrow_decay_copyable_results =
        __mand<__nothrow_decay_copyable_results_t<_CompletionSignatures>,
               __nothrow_decay_copyable_results_t<__decayed_completions_t<_CompletionSignatures>>>;

      template <class _CompletionSignatures>
      using __f = __mtry_q<__concat_completion_signatures_t>::__f<
        __eptr_completion_unless_t<__all_nothrow_decay_copyable_results<_CompletionSignatures>>,
        completion_signatures<set_stopped_t()>,
        __decayed_completions_t<_CompletionSignatures>>;
    };

    template <class _CompletionSignatures>
    using __notification_storage_t =
      __mapply_q<__results_storage, __minvoke<__completions_fn, _CompletionSignatures>>;

    template <class _CompletionSignatures>
    struct __notification_sender;

    template <class _CompletionSignatures>
    struct notification_t {
      using __notification_t = __notification_storage_t<_CompletionSignatures>;
      using __notification_sender_t = __notification_sender<_CompletionSignatures>;

      __notification_t __notification_{};

      template <class _Notification>
      using __tag_of_t =
        STDEXEC::__mapply<STDEXEC::__q<STDEXEC::__mfront>, STDEXEC_REMOVE_REFERENCE(_Notification)>;

      template <class _Tag, class... _Args>
      notification_t(_Tag __tag, _Args&&... __args) noexcept(noexcept(
        __notification_.template emplace<__decayed_tuple<_Tag, STDEXEC_REMOVE_REFERENCE(_Args)...>>(
          __tag,
          static_cast<_Args&&>(__args)...))) {
        __notification_.template emplace<__decayed_tuple<_Tag, STDEXEC_REMOVE_REFERENCE(_Args)...>>(
          __tag, static_cast<_Args&&>(__args)...);
      }

      template <class _Fn>
      auto visit(_Fn&& __fn) const noexcept {
        return STDEXEC::__visit(
          [&__fn](auto&& __tuple) noexcept {
            return STDEXEC::__apply(
              [&__fn](auto __tag, auto&&... __args) noexcept {
                return static_cast<_Fn&&>(__fn)(__tag, __args...);
              },
              __tuple);
          },
          __notification_);
      }
      template <class _Receiver>
      void visit_receiver(_Receiver&& __receiver) noexcept {
        STDEXEC::__visit(
          [&__receiver](auto&& __tuple) noexcept {
            STDEXEC::__apply(
              [&__receiver](auto __tag, auto&&... __args) noexcept {
                __tag(
                  static_cast<_Receiver&&>(__receiver), static_cast<decltype(__args)&&>(__args)...);
              },
              static_cast<decltype(__tuple)&&>(__tuple));
          },
          static_cast<__notification_t&&>(__notification_));
      }
      auto visit_sender() noexcept -> __notification_sender_t;

      [[nodiscard]]
      bool value() const noexcept {
        return STDEXEC::__visit(
          []<class _Tuple>(const _Tuple&) noexcept {
            return STDEXEC::__decays_to<STDEXEC::set_value_t, __tag_of_t<_Tuple>>;
          },
          __notification_);
      }
      [[nodiscard]]
      bool error() const noexcept {
        return STDEXEC::__visit(
          []<class _Tuple>(const _Tuple&) noexcept {
            return STDEXEC::__decays_to<STDEXEC::set_error_t, __tag_of_t<_Tuple>>;
          },
          __notification_);
      }
      [[nodiscard]]
      bool stopped() const noexcept {
        return STDEXEC::__visit(
          []<class _Tuple>(const _Tuple&) noexcept {
            return STDEXEC::__decays_to<STDEXEC::set_stopped_t, __tag_of_t<_Tuple>>;
          },
          __notification_);
      }

      friend auto
        operator==(const notification_t& __lhs, const notification_t& __rhs) noexcept -> bool {
        return STDEXEC::__visit(
          []<class _Lhs>(const __notification_t& __rstg, const _Lhs& __ltpl) noexcept -> bool {
            return STDEXEC::__visit(
              []<class _Rhs>(const _Lhs& __ltpl, const _Rhs& __rtpl){
                return STDEXEC::__apply(
                  [&]<class _LTag, class... _LArgs>(_LTag, const _LArgs&... __l_args) {
                    return STDEXEC::__apply(
                      [&]<class _RTag, class... _RArgs>(_RTag, const _RArgs&... __r_args) {
                        if constexpr (!std::same_as<_LTag, _RTag> || sizeof...(_LArgs) != sizeof...(_RArgs)) {
                          return false;
                        } else if constexpr ((std::equality_comparable_with<const _LArgs&, const _RArgs&>
                                      && ... && true)) {
                          return ((__l_args == __r_args) && ... && true);
                        } else {
                          return false;
                        }
                      },
                      __rtpl);
                  },
                  __ltpl);
              }, 
              __rstg, 
              __ltpl);
          },
          __lhs.__notification_,
          __rhs.__notification_);
      }

      friend std::string to_string(const notification_t& __self) noexcept {
        using std::to_string;
        return __self.visit([](auto __tag, const auto&... __args) {
          int count = 0;
          return to_string(__tag) + "("
               + (((count++ > 0 ? ", " : "") + to_string(__args)) + ... + std::string{}) + ")";
        });
      }
    };

    template <class _Receiver, class _CompletionSignatures>
    struct __notification_op {
      using __notification_t = notification_t<_CompletionSignatures>;


      _Receiver __receiver_;
      __notification_t* __notification_;

      void start() & noexcept {
        __notification_->visit_receiver(static_cast<_Receiver&&>(__receiver_));
      }
    };

    template <class _CompletionSignatures>
    struct __notification_sender {
      using __notification_t = notification_t<_CompletionSignatures>;
      using sender_concept = STDEXEC::sender_t;

      template <class _Receiver>
      using __notification_op_t =
        __notification_op<_Receiver, _CompletionSignatures>;

      __notification_t* __notification_;

      template <STDEXEC::__decays_to<__notification_sender> _Self, class... _Env>
      static consteval auto get_completion_signatures() noexcept -> _CompletionSignatures {
        return {};
      }

      template <STDEXEC::__decays_to<__notification_sender> _Self, receiver _Receiver>
      STDEXEC_EXPLICIT_THIS_BEGIN(auto connect)(this _Self&& __self, _Receiver __rcvr)
        noexcept(__nothrow_move_constructible<_Receiver>) {
        return __notification_op_t<_Receiver>{
          static_cast<_Receiver&&>(__rcvr), __self.__notification_};
      }
      STDEXEC_EXPLICIT_THIS_END(connect)
    };

    template <class _CompletionSignatures>
    auto notification_t<_CompletionSignatures>::visit_sender() noexcept
      -> notification_t<_CompletionSignatures>::__notification_sender_t {
      return {this};
    }

  } // namespace __notification
  template <class _CompletionSignatures>
  using notification_t = __notification::notification_t<_CompletionSignatures>;

  namespace __notification {

  } // namespace __notification
} // namespace experimental::execution

namespace exec = experimental::execution;
