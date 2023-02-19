#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include "exec/static_thread_pool.hpp"

namespace ex = stdexec;
using exec::async_scope_context;
using stdexec::sync_wait;

TEST_CASE(
  "async_scope_context can be created and them immediately destructed", 
  "[async_scope_context][dtor]") {
  async_scope_context scope;
  (void)scope;
}

TEST_CASE("async_scope_context destruction after spawning work into it", "[async_scope_context][dtor]") {
  exec::static_thread_pool pool{4};
  ex::scheduler auto sch = pool.get_scheduler();
  std::atomic<int> counter{0};
  {
    async_scope_context context;
    auto use = exec::async_resource.open(context) | 
      ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

        // Add some work into the scope
        for (int i = 0; i < 10; i++)
          exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { counter++; })));

        return exec::async_resource.close(context);
      });

    // Wait on the work, before calling destructor
    sync_wait(use);
  }
  // We should have all the work executed
  REQUIRE(counter == 10);
}
