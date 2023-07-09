/*
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

#include "../stdexec/execution.hpp"
#include "../exec/sequence_senders.hpp"

namespace exec {
  using namespace stdexec; 

  namespace __run {
    struct run_t {
      template <class _Resource>
        requires tag_invocable<run_t, _Resource>
      STDEXEC_DETAIL_CUDACC_HOST_DEVICE //
        auto
        operator()(_Resource&& __rsrc) const
        noexcept(nothrow_tag_invocable<run_t, _Resource>) {
        static_assert(sender<tag_invoke_result_t<run_t, _Resource>>);
        return tag_invoke(run_t{}, (_Resource&&) __rsrc);
      }

      friend constexpr bool tag_invoke(forwarding_query_t, run_t) {
        return false;
      }
    };
  }

  using __run::run_t;
  inline constexpr run_t run;

  template <class _Resource>
  concept __has_run = //
    requires(_Resource&& __rsrc) {
      { run((_Resource&&) __rsrc) } -> sequence_sender;
    };

  template <class _Resource>
  concept resource =                                //
    __has_run<_Resource> &&                         //
    copy_constructible<__decay_t<_Resource>>;

  namespace __async_scope {

    struct nest_t {
      template <class _Scope, sender _Sender>
        requires tag_invocable<nest_t, _Scope, _Sender>
      auto operator()(_Scope&& __scp, _Sender&& __sndr) const
        noexcept(nothrow_tag_invocable<nest_t, _Scope, _Sender>)
          -> tag_invoke_result_t<nest_t, _Scope, _Sender> {
        return tag_invoke(*this, (_Scope&&) __scp, (_Sender&&) __sndr);
      }
    };

    template <class _Scope, class _Sender>
    using nest_result_t = __call_result_t<nest_t, _Scope, _Sender>;

    struct spawn_t {
      template <class _Scope, sender _Sender>
        requires tag_invocable<spawn_t, _Scope, _Sender>
      auto operator()(_Scope&& __scp, _Sender&& __sndr) const
        noexcept(nothrow_tag_invocable<spawn_t, _Scope, _Sender>)
          -> tag_invoke_result_t<spawn_t, _Scope, _Sender> {
        return tag_invoke(*this, (_Scope&&) __scp, (_Sender&&) __sndr);
      }
    };

    template <class _Scope, class _Sender>
    using spawn_result_t = __call_result_t<spawn_t, _Scope, _Sender>;

    struct spawn_future_t {
      template <class _Scope, sender _Sender>
        requires tag_invocable<spawn_future_t, _Scope, _Sender>
      auto operator()(_Scope&& __scp, _Sender&& __sndr) const
        noexcept(nothrow_tag_invocable<spawn_future_t, _Scope, _Sender>)
          -> tag_invoke_result_t<spawn_future_t, _Scope, _Sender> {
        return tag_invoke(*this, (_Scope&&) __scp, (_Sender&&) __sndr);
      }
    };

    template <class _Scope, class _Sender>
    using spawn_future_result_t = __call_result_t<spawn_future_t, _Scope, _Sender>;
  } // namespace __async_scope

  using __async_scope::nest_t;
  inline constexpr nest_t nest;

  using __async_scope::spawn_t;
  inline constexpr spawn_t spawn;

  using __async_scope::spawn_future_t;
  inline constexpr spawn_future_t spawn_future;

}  // namespace exec