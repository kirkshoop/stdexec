#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include "test_common/schedulers.hpp"
#include "test_common/receivers.hpp"

namespace ex = stdexec;
using exec::async_scope_context;
using stdexec::sync_wait;

TEST_CASE("empty will complete immediately on an empty async_scope_context", "[async_scope_context][empty]") {
  async_scope_context context;
  bool is_empty{false};

  ex::sender auto snd = exec::async_resource.close(context) | ex::then([&] { is_empty = true; });
  sync_wait(std::move(snd));
  REQUIRE(is_empty);
}

TEST_CASE("empty sender can properly connect a void receiver", "[async_scope_context][empty]") {
  async_scope_context context;
  bool is_empty{false};
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      exec::async_scope.spawn(scope, ex::just());

      ex::sender auto snd = exec::async_resource.close(context) | ex::then([&] { is_empty = true; });

      return snd;
    });
  sync_wait(use);
  REQUIRE(is_empty);
}

TEST_CASE("empty will complete after the work is done", "[async_scope_context][empty]") {
  impulse_scheduler sch;
  bool is_done{false};
  async_scope_context context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){

      // Add some work
      exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { is_done = true; })));
      // The close() sender cannot notify until the work completes
    });
  auto op = ex::connect(std::move(use), expect_void_receiver{});
  ex::start(op);
  REQUIRE_FALSE(is_done);

  // TODO: refactor this test
  sch.start_next();

  // We should be done now
  REQUIRE(is_done);

  sync_wait(exec::async_resource.close(context));
}

TEST_CASE("TODO: empty can be used multiple times", "[async_scope_context][empty]") {
  impulse_scheduler sch;
  bool is_done{false};
  async_scope_context context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){

      // Add some work
      exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { is_done = true; })));
      // The close() sender cannot notify until the work completes
    });
  auto op = ex::connect(std::move(use), expect_void_receiver{});
  ex::start(op);
  REQUIRE_FALSE(is_done);

  // TODO: refactor this test
  sch.start_next();
  // We should be done now
  REQUIRE(is_done);

  bool is_done2{false};
  auto use2 = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){

      // Add some work
      exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { is_done2 = true; })));
      // The close() sender cannot notify until the work completes
    });
  auto op2 = ex::connect(std::move(use2), expect_void_receiver{});
  ex::start(op2);
  REQUIRE_FALSE(is_done2);

  // TODO: refactor this test
  sch.start_next();
  // We should be done now
  REQUIRE(is_done2);

  sync_wait(exec::async_resource.close(context));
}

// TODO: GCC-11 generates warnings (treated as errors) for the following test
#if defined(__clang__) || !defined(__GNUC__)
TEST_CASE("waiting on work that spawns more work", "[async_scope_context][empty]") {
  impulse_scheduler sch;

  bool work1_done{false};
  auto work1 = [&] {
    work1_done = true;
  };
  bool work2_done{false};
  auto work2 = [&] (exec::satisfies<exec::async_scope> auto scope) {
    // Spawn work 1
    exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then(work1)));
    // We are done
    work2_done = true;
  };

  async_scope_context context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){

      // Spawn work 2
      // No work is executed until the impulse scheduler dictates
      exec::async_scope.spawn(scope, ex::on(sch, ex::just(scope) | ex::then(work2)));
    });
  auto op = ex::connect(std::move(use), expect_void_receiver{});
  ex::start(op);
  REQUIRE_FALSE(work1_done);
  REQUIRE_FALSE(work2_done);

  // start an on_empty() sender
  bool is_empty{false};
  ex::sender auto snd = ex::on(inline_scheduler{}, exec::async_resource.close(context)) //
    | ex::then([&] { is_empty = true; });
  auto op2 = ex::connect(std::move(snd), expect_void_receiver{});
  ex::start(op2);
  REQUIRE_FALSE(work1_done);
  REQUIRE_FALSE(work2_done);
  REQUIRE_FALSE(is_empty);

  // Trigger the execution of work2
  // When work2 is done, work1 is not yet started
  sch.start_next();
  REQUIRE_FALSE(work1_done);
  REQUIRE(work2_done);
  REQUIRE_FALSE(is_empty);

  // Trigger the execution of work1
  // This will complete the on_empty() sender
  sch.start_next();
  REQUIRE(work1_done);
  REQUIRE(work2_done);
  REQUIRE(is_empty);
}
#endif

// TODO: GCC-11 generates warnings (treated as errors) for the following test
#if defined(__clang__) || !defined(__GNUC__)
TEST_CASE(
  "async_scope_context is empty after adding work when in cancelled state",
  "[async_scope_context][empty]") {
  impulse_scheduler sch;
  bool work_executed{false};
  bool is_empty{false};
  async_scope_context context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){

      // cancel & add work
      context.request_stop();
      exec::async_scope.spawn(scope, ex::on(sch, ex::just())
        | ex::upon_stopped([&] { work_executed = true; printf(".\n");}));
      // note that we don't tell impulse sender to start the work

      ex::sender auto snd = ex::on(inline_scheduler{}, exec::async_resource.close(context)) //
        | ex::then([&] { is_empty = true; });
      return snd;
    });
  auto op = ex::connect(std::move(use), expect_void_receiver{});
  ex::start(op);
  REQUIRE_FALSE(is_empty);

  REQUIRE_FALSE(work_executed);
  sch.start_next();
  REQUIRE(work_executed);
  REQUIRE(is_empty);

  sync_wait(exec::async_resource.close(context));
}
#endif
