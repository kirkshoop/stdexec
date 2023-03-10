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

// Pull in the reference implementation of P2300:
#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>

#include "exec/env.hpp"
#include "exec/static_thread_pool.hpp"

#include <cstdio>

///////////////////////////////////////////////////////////////////////////////
// Example code:
using namespace stdexec;
using stdexec::sync_wait;

class noop_receiver : receiver_adaptor<noop_receiver> {
  friend receiver_adaptor<noop_receiver>;

  template <class... _As>
  void set_value(_As&&...) noexcept {
  }

  void set_error(std::exception_ptr) noexcept {
  }

  void set_stopped() noexcept {
  }

  auto get_env() const & {
    return exec::make_env(exec::with(get_stop_token, stdexec::never_stop_token{}));
  }
};

int main() {
  exec::static_thread_pool ctx{1};

  sync_wait(
    exec::use_resources(
      [sch = ctx.get_scheduler()](
        exec::satisfies<exec::async_scope> auto&&){

        // sender auto begin = schedule(sch); // 2

        // sender auto printVoid = then(begin,
        //   []()noexcept { printf("void\n"); }); // 3

        printf(
          "\n"
          "spawn void\n"
          "==========\n");

        // exec::async_scope.spawn(scope, printVoid); // 5

        // printf(
        //   "\n"
        //   "spawn void and 42\n"
        //   "=================\n");

        // sender auto fortyTwo = then(begin, []()noexcept {return 42;}); // 6

        // exec::async_scope.spawn(scope, printVoid); // 7

        // sender auto fortyTwoFuture = exec::async_scope.spawn_future(scope, fortyTwo); // 8

        // sender auto printFortyTwo = then(std::move(fortyTwoFuture),
        //   [](int fortyTwo)noexcept{ printf("%d\n", fortyTwo); }); // 9

        // sender auto allDone = then(
        //   when_all(printVoid, std::move(printFortyTwo)),
        //   [](auto&&...)noexcept{printf("\nall done\n");}); // 10

        // sync_wait(std::move(allDone));

        // {
        //   sender auto nest = exec::async_scope.nest(scope, begin);
        //   (void)nest;
        // }

        // {
        //   sender auto nest = exec::async_scope.nest(scope, begin);
        //   auto op = connect(std::move(nest), noop_receiver{});
        //   (void)op;
        // }

        // {
        //   sender auto nest = exec::async_scope.nest(scope, begin);
        //   sync_wait(std::move(nest));
        // }

        // return exec::async_scope.nest(scope, begin);
        return just();
      },
      exec::make_deferred<exec::counting_scope>()));
}
