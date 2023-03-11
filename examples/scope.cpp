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
        exec::satisfies<exec::async_scope> auto scope0,
        exec::satisfies<exec::async_scope> auto scope1){

        sender auto begin = schedule(sch); // 2

        auto print = [&](const char* msg){
          return then(begin, [msg]()noexcept { printf("%s\n", msg); }); // 3
        };

        exec::async_scope.spawn(scope0, print("spawn - void - 5")); // 5

        exec::async_scope.spawn(scope1, print("spawn - void - 6")); // 6

        sender auto fortyTwo = then(begin, []()noexcept {return 42;}); // 7

        sender auto fortyTwoFuture = exec::async_scope.spawn_future(scope0, fortyTwo); // 8

        sender auto printFortyTwo = then(std::move(fortyTwoFuture),
          [](int fortyTwo)noexcept{ printf("future - %d - 9\n", fortyTwo); }); // 9

        sender auto allDone = then(
          when_all(print("future - void"), std::move(printFortyTwo)),
          [](auto&&...)noexcept{printf("\nall done - 10\n");}); // 10

        {
          sender auto nest = exec::async_scope.nest(scope0, print("nest - void - discarded"));
          (void)nest;
        }

        {
          sender auto nest = exec::async_scope.nest(scope0, print("nest - void - connected"));
          auto op = connect(std::move(nest), noop_receiver{});
          (void)op;
        }

        return when_all(exec::async_scope.nest(scope1, std::move(print("nest - void - started"))), std::move(allDone));
      },
      exec::make_deferred<exec::counting_scope>(),
      exec::make_deferred<exec::counting_scope>()));
}
