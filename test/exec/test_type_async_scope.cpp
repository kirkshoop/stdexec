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

namespace {
void expect_empty(exec::async_scope_context& context) {
  ex::run_loop loop;
  ex::scheduler auto sch = loop.get_scheduler();
  CHECK_FALSE(stdexec::execute_may_block_caller(sch));
  auto op = ex::connect(
    ex::then(exec::async_resource.close(context), [&](){ loop.finish(); }),
    expect_void_receiver{exec::make_env(exec::with(ex::get_scheduler, sch))});
  ex::start(op);
  loop.run();
}
} // namespace

TEST_CASE("async_scope_context will complete", "[types][type_async_scope_context]") {
  exec::static_thread_pool ctx{1};

  ex::scheduler auto sch = ctx.get_scheduler();

  SECTION("after construction") {
    exec::async_scope_context context;
    expect_empty(context);
  }

  SECTION("after spawn") {
    exec::async_scope_context context;
    exec::satisfies<exec::async_scope> auto scope = exec::async_resource.get_resource_token(context);
    ex::sender auto begin = ex::schedule(sch);
    exec::async_scope.spawn(scope, begin);
    stdexec::sync_wait(exec::async_resource.close(context));
    expect_empty(context);
  }

  SECTION("after nest result discarded") {
    exec::async_scope_context context;
    exec::satisfies<exec::async_scope> auto scope = exec::async_resource.get_resource_token(context);
    ex::sender auto begin = ex::schedule(sch);
    {
      ex::sender auto nst = exec::async_scope.nest(scope, begin); 
      (void)nst;
    }
    stdexec::sync_wait(exec::async_resource.close(context));
    expect_empty(context);
  }

  SECTION("after nest result started") {
    exec::async_scope_context context;
    exec::satisfies<exec::async_scope> auto scope = exec::async_resource.get_resource_token(context);
    ex::sender auto begin = ex::schedule(sch);
    ex::sender auto nst = exec::async_scope.nest(scope, begin);
    auto op = ex::connect(std::move(nst), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context));
    expect_empty(context);
  }

  SECTION("after spawn_future result discarded") {
    exec::static_thread_pool ctx{1};
    exec::async_scope_context context;
    exec::satisfies<exec::async_scope> auto scope = exec::async_resource.get_resource_token(context);
    std::atomic_bool produced{false};
    ex::sender auto begin = ex::schedule(sch);
    {
      ex::sender auto ftr = exec::async_scope.spawn_future(
        scope, 
        begin | stdexec::then([&](){produced = true;})); 
      (void)ftr;
    }
    stdexec::sync_wait(exec::async_resource.close(context) | stdexec::then([&](){STDEXEC_ASSERT(produced.load());}));
    expect_empty(context);
  }

  SECTION("after spawn_future result started") {
    exec::static_thread_pool ctx{1};
    exec::async_scope_context context;
    exec::satisfies<exec::async_scope> auto scope = exec::async_resource.get_resource_token(context);
    std::atomic_bool produced{false};
    ex::sender auto begin = ex::schedule(sch);
    ex::sender auto ftr = exec::async_scope.spawn_future(
      scope, 
      begin | stdexec::then([&](){produced = true;}));
    stdexec::sync_wait(exec::async_resource.close(context) 
    | stdexec::then([&](){STDEXEC_ASSERT(produced.load());}));
    auto op = ex::connect(std::move(ftr), expect_void_receiver{});
    ex::start(op);
    stdexec::sync_wait(exec::async_resource.close(context));
    expect_empty(context);
  }
}

