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

namespace exec {
  // utility that enables objects to provide concepts
  template<class _T0, auto _Satisfier, class... _Tn>
  concept satisfies = _Satisfier.template satisfies<_T0, _Tn...>();

  /////////////////////////////////////////////////////////////////////////////
  // async_resource
  namespace __resource {
    using namespace stdexec;

    struct open_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<open_t, _AsyncResource>
      auto operator()(_AsyncResource&& __rsrc) const {
        return tag_invoke(open_t{}, (_AsyncResource&&) __rsrc);
      }
    };

    struct run_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<run_t, _AsyncResource>
      auto operator()(_AsyncResource&& __rsrc) const {
        return tag_invoke(run_t{}, (_AsyncResource&&) __rsrc);
      }
    };

    struct close_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<close_t, _AsyncResource>
      auto operator()(_AsyncResource&& __rsrc) const {
        return tag_invoke(close_t{}, (_AsyncResource&&) __rsrc);
      }
    };

    struct get_resource_token_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<get_resource_token_t, const _AsyncResource&>
      auto operator()(const _AsyncResource& __rsrc) const {
        return tag_invoke(get_resource_token_t{}, __rsrc);
      }
    };

    struct async_resource_t {
      template<class _T>
        requires 
          requires (_T& __t){
            open_t{}(__t);
            run_t{}(__t);
            close_t{}(__t);
          } &&
          requires (const _T& __t){
            get_resource_token_t{}(__t);
          }
        inline constexpr bool satisfies() const {return true;}
      using open_t = __resource::open_t;
      inline static constexpr open_t open{};
      using run_t = __resource::run_t;
      inline static constexpr run_t run{};
      using close_t = __resource::close_t;
      inline static constexpr close_t close{};
      using get_resource_token_t = __resource::get_resource_token_t;
      inline static constexpr get_resource_token_t get_resource_token{};
    };
  } // namespace __resource

  using __resource::async_resource_t;
  inline constexpr async_resource_t async_resource{};
} // namespace exec
