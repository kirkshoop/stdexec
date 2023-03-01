#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include <exec/env.hpp>
#include "test_common/schedulers.hpp"
#include "test_common/receivers.hpp"
#include "test_common/type_helpers.hpp"

namespace ex = stdexec;
using exec::counting_scope;
using stdexec::sync_wait;

//! Sender that throws exception when connected
struct throwing_sender {
  using is_sender = void;
  using completion_signatures = ex::completion_signatures<ex::set_value_t()>;

  template <class Receiver>
  struct operation {
    Receiver rcvr_;

    friend void tag_invoke(ex::start_t, operation& self) noexcept {
      ex::set_value(std::move(self.rcvr_));
    }
  };

  template <class Receiver>
  friend auto tag_invoke(ex::connect_t, throwing_sender&& self, Receiver&& rcvr)
    -> operation<std::decay_t<Receiver>> {
    throw std::logic_error("cannot connect");
    return {std::forward<Receiver>(rcvr)};
  }

  friend empty_env tag_invoke(stdexec::get_env_t, const throwing_sender&) noexcept {
    return {};
  }
};

TEST_CASE("spawn_future will execute its work", "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call
      {
        ex::sender auto snd = exec::async_scope.spawn_future(
          scope, 
          ex::on(sch, ex::just() 
            | ex::then([&] { executed = true; }))); 
        (void)snd;
      }
      REQUIRE_FALSE(executed);
      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Run the operation on the scheduler
  sch.start_next();
  // Now the spawn work should be completed
  REQUIRE(executed);
}

TEST_CASE("spawn_future sender will complete", "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed1{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call
      ex::sender auto snd = exec::async_scope.spawn_future(
        scope, 
        ex::on(sch, ex::just() 
          | ex::then([&] { executed1 = true; })));

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);
  REQUIRE_FALSE(executed1);
  // Run the operation on the scheduler
  sch.start_next();
  // Now the work from `snd` should be completed
  REQUIRE(executed1);
}

TEST_CASE(
  "spawn_future sender will complete after the given sender completes",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call
      ex::sender auto snd = exec::async_scope.spawn_future(
        scope, 
        ex::on(sch, ex::just() 
          | ex::then([&] { executed = true; })));
      return std::move(snd) | ex::let_value([=] { return exec::async_scope.close(scope); });
    });
  ex::sender auto snd2 = use | ex::then([&] { 
    REQUIRE(executed); 
  });

  auto op = ex::connect(ex::when_all(std::move(snd2), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Execute the given work
  sch.start_next();
}

TEST_CASE("spawn_future returned sender can be dropped", "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  std::atomic_bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call; simply ignore the returned sender
      (void)exec::async_scope.spawn_future(
        scope, 
        ex::on(sch, ex::just() 
          | ex::then([&] { executed = true; })));
      REQUIRE_FALSE(executed.load());

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Execute the given work
  sch.start_next();
  REQUIRE(executed.load());
}

TEST_CASE(
  "spawn_future returned sender can be captured and dropped", 
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call; simply ignore the returned sender
      {
        ex::sender auto snd = exec::async_scope.spawn_future(
          scope, 
          ex::on(sch, ex::just() 
            | ex::then([&] { executed = true; }))); 
        (void)snd;
      }
      REQUIRE_FALSE(executed);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Execute the given work
  sch.start_next();
  REQUIRE(executed);
}

TEST_CASE(
  "spawn_future returned sender can be connected but not started",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  bool executed2{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call; simply ignore the returned sender
      ex::sender auto snd = exec::async_scope.spawn_future(
        scope, 
        ex::on(sch, ex::just() 
          | ex::then([&] { executed = true; })));

      auto op = ex::connect(std::move(snd), expect_void_receiver_ex{executed2});
      REQUIRE_FALSE(executed);
      REQUIRE_FALSE(executed2);
      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Execute the given work
  sch.start_next();
  REQUIRE(executed);
  // Our final receiver will not be notified (as `op` was not started)
  REQUIRE_FALSE(executed2);
}

TEST_CASE("spawn_future will start sender before returning", "[counting_scope][spawn_future]") {
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      bool executed{false};
      // This will be a blocking call
      {
        ex::sender auto snd = exec::async_scope.spawn_future(
          scope, 
          ex::just() 
            | ex::then([&] { executed = true; })); 
        (void)snd;
      }
      REQUIRE(executed);

      return exec::async_scope.close(scope);
    });
  REQUIRE(ex::sync_wait(ex::when_all(exec::async_resource.run(context), std::move(use))).has_value());
}

TEST_CASE(
  "spawn_future returned sender can be started after given sender completed",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      ex::sender auto snd = exec::async_scope.spawn_future(
        scope, 
        ex::on(sch, ex::just() 
          | ex::then([&] { executed = true; })));
      REQUIRE_FALSE(executed);

      return ex::just(std::move(snd), scope);
    });
  auto op = ex::connect(exec::async_resource.run(context), expect_void_receiver{});
  ex::start(op);

  auto [snd, scope] = ex::sync_wait(std::move(use)).value();

  // Execute the work given to spawn_future
  sch.start_next();
  REQUIRE(executed);

  // Now connect the returned sender
  REQUIRE(ex::sync_wait(std::move(snd)).has_value());
  REQUIRE(executed);

  REQUIRE(ex::sync_wait(exec::async_scope.close(scope)).has_value());
}

#if !NO_TESTS_WITH_EXCEPTIONS
TEST_CASE(
  "spawn_future will propagate exceptions encountered during op creation",
  "[counting_scope][spawn_future]") {
  counting_scope context;
  std::optional<typename counting_scope::token_t> scp;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){
      scp.emplace(scope);
      try {
        ex::sender auto snd = exec::async_scope.spawn_future(
          scope, 
          throwing_sender{} 
            | ex::then([&] { FAIL("work should not be executed"); }));
        (void)snd;
        FAIL("Exceptions should have been thrown");
      } catch (const std::logic_error& e) {
        SUCCEED("correct exception caught");
      } catch (...) {
        FAIL("invalid exception caught");
      }
    });
  auto op = ex::connect(ex::when_all(exec::async_resource.run(context), std::move(use)), expect_void_receiver{});
  ex::start(op);
  ex::sync_wait(exec::async_scope.close(scp.value()));
}
#endif

TEST_CASE(
  "TODO: spawn_future will keep the scope non-empty until the work is executed",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Before adding any operations, the scope is empty
      // TODO: reenable this
      // REQUIRE(P2519::__scope::empty(scope));

      // Non-blocking call
      {
        ex::sender auto snd = exec::async_scope.spawn_future(
          scope, 
          ex::on(sch, ex::just() 
            | ex::then([&] { executed = true; }))); 
        (void)snd;
      }
      REQUIRE_FALSE(executed);

      // The scope is now non-empty
      // TODO: reenable this
      // REQUIRE_FALSE(P2519::__scope::empty(scope));
      // REQUIRE(P2519::__scope::op_count(scope) == 1);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(exec::async_resource.run(context), std::move(use)), expect_void_receiver{});
  ex::start(op);

  // Run the operation on the scheduler; blocking call
  sch.start_next();

  // Now the scope should again be empty
  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));
  REQUIRE(executed);
}

TEST_CASE(
  "TODO: spawn_future will keep track on how many operations are in flight",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  // std::size_t num_executed{0};
  counting_scope context;

  // Before adding any operations, the scope is empty
  // TODO: reenable this
  // REQUIRE(P2519::__scope::op_count(scope) == 0);
  // REQUIRE(P2519::__scope::empty(scope));

  // TODO: this will fail when running multiple iterations
  // constexpr std::size_t num_oper = 10;
  // for (std::size_t i = 0; i < num_oper; i++) {
  //     ex::sender auto snd =
  //             exec::async_scope.spawn_future(scope, ex::on(sch, ex::just() | ex::then([&] { num_executed++; })));
  //     (void)snd;
  //     size_t num_expected_ops = i + 1;
  //     REQUIRE(P2519::__scope::op_count(scope) == num_expected_ops);
  // }
  //
  // // Now execute the operations
  // for (std::size_t i = 0; i < num_oper; i++) {
  //     sch.start_next();
  //     size_t num_expected_ops = num_oper - i - 1;
  //     REQUIRE(P2519::__scope::op_count(scope) == num_expected_ops);
  // }
  //
  // // The scope is empty after all the operations are executed
  // REQUIRE(P2519::__scope::empty(scope));
  // REQUIRE(num_executed == num_oper);
}

TEST_CASE(
  "TODO: spawn_future work can be cancelled by cancelling the scope",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  bool cancelled1{false};
  bool cancelled2{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){
      return std::move(scope);
    });
  auto op = ex::connect(exec::async_resource.run(context), expect_void_receiver{});
  ex::start(op);

  auto [scope] = ex::sync_wait(std::move(use)).value();

  {
    ex::sender auto snd1 = exec::async_scope.spawn_future(
      scope, 
      ex::on(sch, ex::just() 
        | ex::let_stopped([&] {
            cancelled1 = true;
            return ex::just();
          })));
    ex::sender auto snd2 = exec::async_scope.spawn_future(
      scope, 
      ex::on(sch, ex::just() 
        | ex::let_stopped([&] {
            cancelled2 = true;
            return ex::just();
          })));
    (void)snd1;
    (void)snd2;
  }

  // TODO: reenable this
  // REQUIRE(P2519::__scope::op_count(scope) == 2);

  // Execute the first operation, before cancelling
  sch.start_next();
  REQUIRE_FALSE(cancelled1);
  REQUIRE_FALSE(cancelled2);

  // Cancel the counting_scope object

  // context.request_stop();

  // TODO: reenable this
  // REQUIRE(P2519::__scope::op_count(scope) == 1);

  // Execute the first operation, after cancelling
  sch.start_next();
  REQUIRE_FALSE(cancelled1);
  // TODO: second operation should be cancelled
  // REQUIRE(cancelled2);
  REQUIRE_FALSE(cancelled2);

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));

  ex::sync_wait(exec::async_scope.close(scope));
}

template <typename S>
concept is_spawn_future_worthy =
    requires(
      S&& snd, 
      exec::counting_scope& context, 
      exec::counting_scope::token_t scope) { 
        exec::async_scope.spawn_future(scope, std::move(snd)); 
      };

TEST_CASE("spawn_future accepts void senders", "[counting_scope][spawn_future]") {
  static_assert(is_spawn_future_worthy<decltype(ex::just())>);
}
TEST_CASE(
  "spawn_future accepts non-void senders", 
  "[counting_scope][spawn_future]") {
  static_assert(is_spawn_future_worthy<decltype(ex::just(13))>);
  static_assert(is_spawn_future_worthy<decltype(ex::just(3.14))>);
  static_assert(is_spawn_future_worthy<decltype(ex::just("hello"))>);
}
TEST_CASE(
  "spawn_future accepts senders of errors", 
  "[counting_scope][spawn_future]") {
  static_assert(is_spawn_future_worthy<decltype(ex::just_error(std::exception_ptr{}))>);
  static_assert(is_spawn_future_worthy<decltype(ex::just_error(std::error_code{}))>);
  static_assert(is_spawn_future_worthy<decltype(ex::just_error(-1))>);
}

TEST_CASE(
  "spawn_future should accept senders that send stopped signal", 
  "[counting_scope][spawn_future]") {
  static_assert(is_spawn_future_worthy<decltype(ex::just_stopped())>);
}
TEST_CASE(
  "TODO: spawn_future works with senders that complete with stopped signal",
  "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  counting_scope context;

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));

  // TODO: make this work
  // ex::sender auto snd = exec::async_scope.spawn_future(scope, ex::on(sch, ex::just_stopped()));
  // (void)snd;
  //
  // // The scope is now non-empty
  // REQUIRE_FALSE(P2519::__scope::empty(scope));
  // REQUIRE(P2519::__scope::op_count(scope) == 1);
  //
  // // Run the operation on the scheduler; blocking call
  // sch.start_next();
  //
  // // Now the scope should again be empty
  // REQUIRE(P2519::__scope::empty(scope));
}

TEST_CASE("spawn_future forwards value to returned sender", "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::then([&](exec::satisfies<exec::async_scope> auto scope){
      return std::move(scope);
    });
  auto op = ex::connect(exec::async_resource.run(context), expect_void_receiver{});
  ex::start(op);

  auto [scope] = ex::sync_wait(std::move(use)).value();

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));

  ex::sender auto snd = exec::async_scope.spawn_future(scope, ex::on(sch, ex::just(13)));

  sch.start_next();
  wait_for_value(std::move(snd), 13);

  ex::sync_wait(exec::async_scope.close(scope));
}

TEST_CASE("TODO: spawn_future forwards error to returned sender", "[counting_scope][spawn_future]") {
  impulse_scheduler sch;
  counting_scope context;

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));

  // TODO: fix this
  // ex::sender auto snd = exec::async_scope.spawn_future(scope, ex::on(sch, ex::just_error(-1)));
  // sch.start_next();
  // try
  // {
  //     sync_wait(std::move(snd));
  //     FAIL("Should not reach this point");
  // }
  // catch(int error)
  // {
  //     REQUIRE(error == -1);
  // }
}

TEST_CASE(
  "TODO: spawn_future forwards stopped signal to returned sender",
  "[counting_scope][spawn_future]") {
  // impulse_scheduler sch;
  counting_scope context;

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));

  // TODO: fix this
  // ex::sender auto snd = exec::async_scope.spawn_future(scope, ex::on(sch, ex::just_stopped()));
  // sch.start_next();
  // auto op = ex::connect(std::move(snd), expect_stopped_receiver{});
  // ex::start(op);
}
