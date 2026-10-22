// SPDX-License-Identifier: GPL-3.0-or-later
//
// The date and time of day kGetTime reports.
#pragma once

#include <cstdint>

namespace sci::vm {

// As the fields of TimeDate (struct tm) that kGetTime reads.
struct LocalTime {
    int year = 90;  // Since 1900.
    int month = 0;  // 0 to 11.
    int day = 1;    // 1 to 31.
    int hour = 12;  // 0 to 23.
    int minute = 0; // 0 to 59.
    int second = 0; // 0 to 59.
};

// The time of day of the deterministic mode (docs/formats/replay-and-trace.md):
// Monday 1990-01-01, 12:00:00, plus the virtual time passed. Days roll over
// within January 1990, which no deterministic run leaves.
[[nodiscard]] constexpr LocalTime deterministic_local_time(std::uint64_t virtual_microseconds) {
    constexpr std::uint64_t seconds_per_day = 24ULL * 60 * 60;
    const std::uint64_t seconds = (12ULL * 60 * 60) + (virtual_microseconds / 1000000);
    const std::uint64_t of_day = seconds % seconds_per_day;
    return LocalTime{
        .year = 90,
        .month = 0,
        .day = 1 + static_cast<int>(seconds / seconds_per_day),
        .hour = static_cast<int>(of_day / 3600),
        .minute = static_cast<int>((of_day / 60) % 60),
        .second = static_cast<int>(of_day % 60),
    };
}

} // namespace sci::vm
