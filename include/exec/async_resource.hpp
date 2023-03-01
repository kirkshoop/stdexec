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
  /// @brief utility that enables objects to provide concepts
  template<class _T0, auto _Satisfier, class... _Tn>
  concept satisfies = requires {
    _Satisfier.template satisfies<_T0, _Tn...>();
  };

  /// @brief there will only be one valid instance of _T at a time.
  template<class _T>
  concept unique_location = !stdexec::copy_constructible<_T>;

  struct __unique {
    __unique() = default;
    __unique(const __unique&) = delete;
    __unique(__unique&&) = default;
    __unique& operator=(const __unique&) = delete;
    __unique& operator=(__unique&&) = default;
  };

  /// @brief raw pointers, references, span/_view/_ref, etc.. of _T are safe to 
  /// use within the current async expression.
  template<class _T>
  concept stable_location = unique_location<_T> && !stdexec::move_constructible<_T>;

  /////////////////////////////////////////////////////////////////////////////
  // async_resource
  namespace __resource {
    using namespace stdexec;

    struct open_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<open_t, const _AsyncResource&>
      auto operator()(const _AsyncResource& __rsrc) const {
        return tag_invoke(open_t{}, __rsrc);
      }
    };

    struct run_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<run_t, _AsyncResource&>
      auto operator()(_AsyncResource& __rsrc) const {
        return tag_invoke(run_t{}, __rsrc);
      }
    };

    struct close_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _AsyncResource>
        requires tag_invocable<close_t, _AsyncResource&>
      auto operator()(_AsyncResource& __rsrc) const {
        return tag_invoke(close_t{}, __rsrc);
      }
    };

    struct async_resource_token_t {
      /// @brief the async-resource-token concept definition
      template<class _T>
        requires 
          requires (const _T& __t_clv){
            get_env_t{}(__t_clv);
            close_t{}(__t_clv);
          } 
        inline constexpr bool satisfies() const {return true;}

      using get_env_t = stdexec::get_env_t;
      /// @brief get_env() provides an environment that contains the resource
      /// @details returns the environment that the receiver connected to 
      /// the sender returned from run() provided. 
      /// @param resource-token&
      /// @returns environment
      inline static constexpr get_env_t get_env{};

      using close_t = __resource::close_t;
      /// @brief close() provides a sender-of-void. 
      /// @details The sender provided by close() will trigger the sender provided 
      /// by run() to begin any async-operation needed to close the resource and 
      /// will complete when all the async-operation complete. 
      /// @param resource-token&  
      /// @returns sender<>
      inline static constexpr close_t close{};
    };

    struct async_resource_t {
      /// @brief the async-resource concept definition
      template<class _T>
        requires 
          requires (const _T& __t_clv, _T& __t_lv){
            open_t{}(__t_clv);
            run_t{}(__t_lv);
          } &&
          exec::stable_location<_T>
        inline constexpr bool satisfies() const {return true;}

      using open_t = __resource::open_t;
      /// @brief open() provides a sender that will complete with a resource-token. 
      /// @details The resource-token will be valid until the sender provided 
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
      /// by close() is started and all the async-operation needed to close 
      /// the async-resource complete and the sender provided by close() is completed. 
      /// @param resource-context&  
      /// @returns sender<>
      inline static constexpr run_t run{};

      using token_t = __resource::async_resource_token_t;
      /// @brief child concept for the token with which open() completes
      inline static constexpr token_t token{};
    };

  } // namespace __resource

  using __resource::async_resource_t;
  /// @brief async_resource defines a concept for objects that need to
  /// hold state within the scope of an async-expression
  /// @details async_resource is a concept and a set of the CPOs that are
  /// available on objects that satisfy the concept.
  /// Examples of async-resource include async-scope, thread-pool, 
  /// async-allocator, async-socket, etc..
  ///
  /// The resource usage pattern is
  /// 
  /// static_thread_pool thrd{1};
  /// sync_wait(when_all(
  ///   async_resource.open(thrd) |
  ///     let_value([](satisfies<scheduler> auto schd){
  ///         return on(
  ///           get_scheduler(async_scope.get_env(schd)), 
  ///           finally(
  ///             scheduler.schedule(schd) | .., 
  ///             async_resource.close(schd)));
  ///     }),
  ///   async_resource.run(thrd)));
  ///
  /// The resource composition pattern is
  /// 
  /// static_thread_pool thrd{1};
  /// counting_scope scp;
  /// sync_wait(when_all(
  ///   // compose opens
  ///   when_all(async_resource.open(thrd), async_resource.open(scp)) |
  ///     let_value([](
  ///       satisfies<scheduler> auto schd, 
  ///       satisfies<async_scope> auto scope){
  ///         async_scope.spawn(scope, on(schd, ..));
  ///         return on(
  ///           get_scheduler(scheduler.get_env(schd)), 
  ///           finally(
  ///             scheduler.schedule(schd) | .., 
  ///             // compose closes
  ///             when_all(async_resource.close(schd), async_resource.close(scope))));
  ///       }),
  ///   // compose runs
  ///   async_resource.run(thrd),
  ///   async_resource.run(scp)));
  ///  
  inline constexpr async_resource_t async_resource{};

  using __resource::async_resource_token_t;
} // namespace exec
