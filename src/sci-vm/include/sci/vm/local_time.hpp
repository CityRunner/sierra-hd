// SPDX-License-Identifier: GPL-3.0-or-later
//
// The date and time of day kGetTime reports.
#pragma once

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

} // namespace sci::vm
