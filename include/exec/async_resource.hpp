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

    struct async_resource_t {
      /// @brief the async-resource concept definition
      template<class _T>
        requires 
          requires (_T& __t){
            open_t{}(__t);
            run_t{}(__t);
            close_t{}(__t);
          }
        inline constexpr bool satisfies() const {return true;}
      using open_t = __resource::open_t;
      /// @brief open() provides a sender that will complete with a resource-token. 
      /// @details The resource-token will be valid until the first sender provided 
      /// by close() is started. 
      /// The sender provided by open() will complete after the sender provided by 
      /// run() has completed any async-operation needed to open the resource.
      /// @param resource-context&  
      /// @returns sender<resource-token>
      inline static constexpr open_t open{};
      using run_t = __resource::run_t;
      /// @brief run() provides a sender-of-void. 
      /// @details The sender provided by run() will start any async-operation 
      /// needed to open the resource and when all the operations complete will 
      /// complete the sender provided by open().
      //  The sender provided by run() will complete after the sender provided 
      /// by close() is started and all the async-operations needed to close 
      /// the async-resource complete. 
      /// @param resource-context&  
      /// @returns sender<>
      inline static constexpr run_t run{};
      using close_t = __resource::close_t;
      /// @brief close() provides a sender-of-void. 
      /// @details The sender provided by close() will trigger the sender provided 
      /// by run() to begin any async-operation needed to close the resource and 
      /// will complete when all the async-operations complete. 
      /// @param resource-context&  
      /// @returns sender<>
      inline static constexpr close_t close{};
    };
  } // namespace __resource

  using __resource::async_resource_t;
  inline constexpr async_resource_t async_resource{};
} // namespace exec
