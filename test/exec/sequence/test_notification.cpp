/*
 * Copyright (c) 2023 Maikel Nadolski
 * Copyright (c) 2023 NVIDIA Corporation
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

#include "exec/sequence/notification.hpp"

#include <system_error>
#include <exception>
#include "stdexec/__detail/__meta.hpp"

#include <catch2/catch_all.hpp>

#include <test_common/schedulers.hpp>
#include <test_common/receivers.hpp>
#include <test_common/senders.hpp>
#include <test_common/sequences.hpp>
#include <test_common/type_helpers.hpp>

namespace {

  using __test_notification_t = exec::notification_t<
    ex::completion_signatures<
      ex::set_value_t(int), 
      ex::set_error_t(std::error_code), 
      ex::set_stopped_t()>>;

  TEST_CASE(
  "notification - void notification",
  "[sequence_senders][notification]") {
    auto notification = __test_notification_t{ex::set_value, 42};
    CHECK(notification == __test_notification_t{ex::set_value, 42});
    CHECK(notification != __test_notification_t{ex::set_value, 24});
    CHECK(true == notification.value());
    CHECK(false == notification.error());
    CHECK(false == notification.stopped());
    auto result = ex::sync_wait(notification.visit_sender());
    CHECK(true == result.has_value());
    CHECK(42 == std::get<0>(result.value()));
  }

  TEST_CASE(
  "notification - error notification",
  "[sequence_senders][notification]") {
    auto notification = __test_notification_t{ex::set_error, std::make_error_code(std::errc::not_supported)};
    CHECK(notification == __test_notification_t{ex::set_error, std::make_error_code(std::errc::not_supported)});
    CHECK(notification != __test_notification_t{ex::set_error, std::make_error_code(std::errc::protocol_not_supported)});
    CHECK(false == notification.value());
    CHECK(true == notification.error());
    CHECK(false == notification.stopped());
    try {
      auto result = ex::sync_wait(notification.visit_sender());
      CHECK(false == result.has_value());
    } catch(const std::system_error& ex) {
      CHECK(std::make_error_code(std::errc::not_supported) == ex.code());
    }
  }

  TEST_CASE(
  "notification - stopped notification",
  "[sequence_senders][notification]") {
    auto notification = __test_notification_t{ex::set_stopped};
    CHECK(notification == __test_notification_t{ex::set_stopped});
    CHECK(notification != __test_notification_t{ex::set_value, 24});
    CHECK(false == notification.value());
    CHECK(false == notification.error());
    CHECK(true == notification.stopped());
    auto result = ex::sync_wait(notification.visit_sender());
    CHECK(false == result.has_value());
  }

} // namespace
