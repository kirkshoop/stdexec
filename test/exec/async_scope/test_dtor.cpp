#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include "exec/static_thread_pool.hpp"

namespace ex = stdexec;
using exec::counting_scope;
using stdexec::sync_wait;

TEST_CASE(
  "counting_scope can be created and them immediately destructed", 
  "[counting_scope][dtor]") {
  counting_scope scope;
  (void)scope;
}

TEST_CASE("counting_scope destruction after spawning work into it", "[counting_scope][dtor]") {
  exec::static_thread_pool pool{4};
  ex::scheduler auto sch = pool.get_scheduler();
  std::atomic<int> counter{0};
  {
    counting_scope context;
    auto use = exec::async_resource.open(context) | 
      ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

        // Add some work into the scope
        for (int i = 0; i < 10; i++)
          exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { counter++; })));

        return exec::async_scope.close(scope);
      });

    // Wait on the work, before calling destructor
    sync_wait(stdexec::when_all(use, exec::async_resource.run(context)));
  }
  // We should have all the work executed
  REQUIRE(counter == 10);
}
