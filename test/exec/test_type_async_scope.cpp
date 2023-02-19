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

#include <catch2/catch.hpp>

#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>

#include "exec/env.hpp"
#include "exec/static_thread_pool.hpp"

#include <test_common/schedulers.hpp>
#include <test_common/receivers.hpp>
#include <test_common/type_helpers.hpp>

namespace ex = stdexec;

TEST_CASE("async_scope_context will complete", "[types][type_async_scope_context]") {
  exec::static_thread_pool ctx{1};

  ex::scheduler auto sch = ctx.get_scheduler();

  SECTION("after construction") {
    exec::async_scope_context context;
  }

  SECTION("after spawn") {
    exec::async_scope_context context;
    auto use = exec::async_resource.open(context) | 
      ex::then([&](exec::satisfies<exec::async_scope> auto scope){
        ex::sender auto begin = ex::schedule(sch);
        exec::async_scope.spawn(scope, begin);
      });
    auto op = ex::connect(std::move(use), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context));
  }

  SECTION("after nest result discarded") {
    exec::async_scope_context context;
    auto use = exec::async_resource.open(context) | 
      ex::then([&](exec::satisfies<exec::async_scope> auto scope){
        ex::sender auto begin = ex::schedule(sch);
        {
          ex::sender auto nst = exec::async_scope.nest(scope, begin); 
          (void)nst;          
        }
      });
    auto op = ex::connect(std::move(use), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context));
  }

  SECTION("after nest result started") {
    exec::async_scope_context context;
    auto use = exec::async_resource.open(context) | 
      ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){
        ex::sender auto begin = ex::schedule(sch);
        ex::sender auto nst = exec::async_scope.nest(scope, begin);
        return nst;
      });
    auto op = ex::connect(std::move(use), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context));
  }

  SECTION("after spawn_future result discarded") {
    exec::static_thread_pool ctx{1};
    exec::async_scope_context context;
    std::atomic_bool produced{false};
    auto use = exec::async_resource.open(context) | 
      ex::then([&](exec::satisfies<exec::async_scope> auto scope){
        std::atomic_bool produced{false};
        ex::sender auto begin = ex::schedule(sch);
        {
          ex::sender auto ftr = exec::async_scope.spawn_future(
            scope, 
            begin | stdexec::then([&](){produced = true;})); 
          (void)ftr;
        }
      });
    auto op = ex::connect(std::move(use), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context) | stdexec::then([&](){STDEXEC_ASSERT(produced.load());}));
  }

  SECTION("after spawn_future result started") {
    exec::static_thread_pool ctx{1};
    exec::async_scope_context context;
    std::atomic_bool produced{false};
    auto use = exec::async_resource.open(context) | 
      ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){
        ex::sender auto begin = ex::schedule(sch);
        ex::sender auto ftr = exec::async_scope.spawn_future(
          scope, 
          begin | stdexec::then([&](){produced = true;}));
        return ftr;
      });
    auto op = ex::connect(std::move(use), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context) | stdexec::then([&](){STDEXEC_ASSERT(produced.load());}));
  }
}

