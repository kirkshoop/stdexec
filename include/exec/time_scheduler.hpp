
#pragma once

#include "../stdexec/execution.hpp"

namespace exec {

  /////////////////////////////////////////////////////////////////////////////
  // time_scheduler
  namespace __time {
    using namespace stdexec;

    struct now_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _TimeScheduler>
        requires tag_invocable<now_t, const _TimeScheduler&>
      auto operator()(const _TimeScheduler& __schd) const {
        return tag_invoke(now_t{}, __schd);
      }
    };

    struct schedule_at_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _TimeScheduler, class _TimePoint>
        requires tag_invocable<schedule_at_t, const _TimeScheduler&, _TimePoint>
      auto operator()(const _TimeScheduler& __schd, _TimePoint&& at) const {
        return tag_invoke(schedule_at_t{}, __schd, (_TimePoint&&) at);
      }
    };

    struct schedule_after_t {
      template <class _Fn>
        using __f = __minvoke<_Fn>;

      template <class _TimeScheduler, class _Duration>
        requires tag_invocable<schedule_after_t, const _TimeScheduler&, _Duration>
      auto operator()(const _TimeScheduler& __schd, _Duration&& after) const {
        return tag_invoke(schedule_after_t{}, __schd, (_Duration&&) after);
      }
    };

    struct time_scheduler_t {
      /// @brief the time-scheduler concept definition
      template<class _T>
        requires 
          requires (const _T& __t_clv, typename _T::time_point __at, typename _T::duration __after){
            now_t{}(__t_clv);
            schedule_at_t{}(__t_clv, __at);
            schedule_after_t{}(__t_clv, __after);
          }
        inline constexpr bool satisfies() const {return true;}

      using now_t = __time::now_t;
      /// @brief now() returns a time_point that designates the current time of this scheduler. 
      /// @details 
      /// @param time-scheduler 
      /// @returns time_point
      inline static constexpr now_t now{};

      using schedule_at_t = __time::schedule_at_t;
      /// @brief schedule_at() 
      /// @details 
      /// @param time-scheduler 
      /// @returns sender<>
      inline static constexpr schedule_at_t schedule_at{};

      using schedule_after_t = __time::schedule_after_t;
      /// @brief time_scheduler() 
      /// @details 
      /// @param time-scheduler 
      /// @returns sender<>
      inline static constexpr schedule_after_t schedule_after{};
    };

  } // namespace __time

  using __time::time_scheduler_t;
  static constexpr time_scheduler_t time_scheduler{};

} // namespace exec
