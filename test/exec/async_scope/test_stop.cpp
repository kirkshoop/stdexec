#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include "test_common/schedulers.hpp"
#include "test_common/receivers.hpp"
#include "exec/single_thread_context.hpp"
#if 0
namespace ex = stdexec;
using exec::counting_scope;
using stdexec::sync_wait;

TEST_CASE("calling request_stop will be visible in stop_source", "[counting_scope][stop]") {
  counting_scope scope;

  scope.request_stop();
  REQUIRE(scope.get_stop_source().stop_requested());
}
TEST_CASE(
  "calling request_stop will be visible in stop_token", 
  "[counting_scope][stop]") {
  counting_scope scope;

  scope.request_stop();
  REQUIRE(scope.get_stop_token().stop_requested());
}

TEST_CASE(
  "cancelling the associated stop_source will cancel the counting_scope object",
  "[counting_scope][stop]") {
  bool empty = false;

  {
    impulse_scheduler sch;
    bool called = false;
    counting_scope context;
    auto use = exec::async_resource.open(context) | 
      ex::then([&](exec::satisfies<exec::async_scope> auto scope){

        // put work in the scope
        exec::async_scope.spawn(
          scope, 
          ex::on(
            sch, 
            ex::just())
              | ex::upon_stopped([&]{ called = true; }));
        REQUIRE_FALSE(called);
        return scope;
      });

    auto op = ex::connect(exec::async_resource.run(context), expect_void_receiver{});
    ex::start(op);

    auto [scope] = ex::sync_wait(std::move(use)).value();

    // start a thread waiting on when the scope is empty:
    exec::single_thread_context thread;
    auto thread_sch = thread.get_scheduler();
    ex::start_detached(
      ex::on(
        thread_sch, 
        exec::async_scope.close(scope))
          | ex::then([&]{ empty = true; }));
    REQUIRE_FALSE(empty);

    // request the scope stop
    context.request_stop();

    // execute the work in the scope
    sch.start_next();

    // Should have completed with a stopped signal
    REQUIRE(called);
  } // blocks until the separate thread is joined

  REQUIRE(empty);
}

TEST_CASE(
  "cancelling the associated stop_source will be visible in stop_token", 
  "[counting_scope][stop]") {
  counting_scope scope;

  scope.get_stop_source().request_stop();
  REQUIRE(scope.get_stop_token().stop_requested());
}
#endif