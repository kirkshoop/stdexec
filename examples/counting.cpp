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
#include <exec/counting_scope.hpp>

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

  void set_stopped() noexcept {
  }

  auto get_env() const & noexcept {
    return exec::make_env(exec::with(get_stop_token, stdexec::never_stop_token{}));
  }
};

struct app_t {
  struct result {
    using self_t = result;

    using is_receiver = void;

    decltype(exec::static_thread_pool{1}.get_scheduler()) __sch_;
    bool& done;
    std::mutex& lock;
    std::condition_variable& wake;

    using scope_t = stdexec::__t<exec::__counting_scope::__token>;
    static_assert(stdexec::__callable<stdexec::__just::__just_t, scope_t>);
    using item_s = stdexec::__call_result_t<stdexec::__just::__just_t, scope_t>;

    friend auto tag_invoke(exec::set_next_t, self_t& __self, auto __item) noexcept {
      (void)__self;
      (void)__item;
      if constexpr (stdexec::__callable<stdexec::connect_t, decltype(__item), noop_receiver>) {
          return stdexec::then(__item, [&__self](scope_t scope) noexcept {    //
            stdexec::scheduler auto sch = __self.__sch_;                      // 1
                                                                              //
            sender auto begin = stdexec::schedule(sch);                       // 2
                                                                              //
            sender auto printVoid = stdexec::then(begin,                      //
              []() noexcept { printf("void\n"); });                           // 3
                                                                              //
            stdexec::sender auto fortyTwo = stdexec::then(                    //
              begin,                                                          //
              []() noexcept { return 42; });                                  // 4
                                                                              //
            printf(                                                           //
              "\n"                                                            //
              "spawn void, void and 42\n"                                     //
              "=======================\n");                                   //
                                                                              //
            exec::spawn(scope, printVoid);                                    // 5
                                                                              //
            exec::spawn(scope, printVoid);                                    // 6
                                                                              //
            stdexec::sender auto fortyTwoFuture =                             //
              exec::spawn_future(scope, fortyTwo);                            // 7
                                                                              //
            stdexec::sender auto printFortyTwo = stdexec::then(               //
              std::move(fortyTwoFuture),                                      //
              [](int fortyTwo) noexcept {                                     // 8
                printf("%d\n", fortyTwo);                                     //
              });                                                             //
                                                                              //
            exec::spawn(scope, std::move(printFortyTwo));                     // 9
                                                                              //
            {                                                                 //
              sender auto nest = exec::nest(scope, begin);                    // 10
              (void) nest;                                                    //
            }                                                                 //
                                                                              //
            {                                                                 //
              sender auto nest = exec::nest(scope, begin);                    // 11
              auto op = connect(std::move(nest), noop_receiver{});            //
              (void) op;                                                      //
            }                                                                 //
          });
      } else {
        std::terminate();
        return stdexec::just();
      }
    }

    friend void tag_invoke(stdexec::set_value_t, self_t&& __self) noexcept {
      printf("\nsuccess\n");
      std::unique_lock guard{__self.lock};
      __self.done = true;
      __self.wake.notify_one();
    }
    friend void tag_invoke(stdexec::set_stopped_t, self_t&& __self) noexcept {
      printf("\nstopped\n");
      std::unique_lock guard{__self.lock};
      __self.done = true;
      __self.wake.notify_one();
    }

    friend empty_env tag_invoke(stdexec::get_env_t, const self_t&) noexcept {
      return {};
    }
  }; 

  void operator()() const noexcept {
    exec::static_thread_pool ctx{1};
    bool done = false;
    std::mutex lock;
    std::condition_variable wake;
    auto op = exec::subscribe(exec::use(exec::counting_scope()), result{ctx.get_scheduler(), done, lock, wake});
    printf("start counting\n");
    stdexec::start(op);
    std::unique_lock guard{lock};
    wake.wait(guard, [&]{return done;});
    printf("start counting exit\n");
  }
}; 
inline constexpr app_t app;

int main() {
  app();
}
