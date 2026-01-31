/*
 * Copyright (c) 2021-2024 NVIDIA Corporation
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
#pragma once

#include "../../stdexec/execution.hpp"
#include "../../stdexec/__detail/__meta.hpp"

#include <compare>
#include <cstddef>

namespace exec {
namespace __mstng {
  using namespace STDEXEC;

  template <std::size_t _Len>
  struct __mstring {
    constexpr __mstring() = default;

#if STDEXEC_EDG()
    template <std::size_t _Ny, std::size_t... _Is>
    constexpr __mstring(const char (&__str)[_Ny], __indices<_Is...>) noexcept
      : __what_{(_Is < _Ny ? __str[_Is] : '\0')...} {
    }

    template <std::size_t _Ny>
    constexpr __mstring(const char (&__str)[_Ny]) noexcept
      : __mstring{__str, __make_indices<_Len>{}} {
    }
#else
    template <std::size_t _Ny>
    constexpr __mstring(const char (&__str)[_Ny]) noexcept {
      for (auto __i = 0ull; char __ch: __str) {
        __what_[__i++] = __ch;
      }
    }
#endif

    static constexpr auto __length() noexcept -> std::size_t {
      return _Len;
    }

    constexpr auto operator==(const __mstring &) const noexcept -> bool = default;

    template <std::size_t _OtherLen>
    constexpr auto operator==(const __mstring<_OtherLen> &) const noexcept -> bool {
      return false;
    }

#if !STDEXEC_EDG()
    constexpr auto operator<=>(const __mstring &) const noexcept -> std::strong_ordering = default;
#endif

    template <std::size_t _OtherLen>
    constexpr auto
      operator<=>(const __mstring<_OtherLen> &__other) const noexcept -> std::strong_ordering {
      constexpr std::size_t __len = _Len < _OtherLen ? _Len : _OtherLen;
      for (std::size_t __i = 0; __i < __len; ++__i) {
        auto __cmp = (__what_[__i] <=> __other.__what_[__i]);
        if (__cmp != 0) {
          return __cmp;
        }
      }
      if constexpr (_Len == _OtherLen) {
        return std::strong_ordering::equal;
      }
      return (_Len < _OtherLen) ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    char __what_[_Len]{};
  };

  template <std::size_t _Len>
  STDEXEC_HOST_DEVICE_DEDUCTION_GUIDE __mstring(const char (&__str)[_Len]) -> __mstring<_Len>;

  // Use a standard user-defined string literal template
  template <__mstring _Str>
  constexpr auto operator""_mstr() noexcept -> __mtypeof<_Str> {
    return _Str;
  }

} // namespace __mstng

using __mstng::__mstring;
using __mstng::operator""_mstr;

} // namespace exec
