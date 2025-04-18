/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Timestamp conversion code inspired by DuckDB's date/time/timestamp conversion
// libraries. License below:

/*
 * Copyright 2018 DuckDB Contributors
 * (see https://github.com/cwida/duckdb/graphs/contributors)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "velox/type/TimestampConversion.h"
#include <folly/Expected.h>
#include "velox/common/base/CheckedArithmetic.h"
#include "velox/common/base/Exceptions.h"
#include "velox/type/HugeInt.h"
#include "velox/type/tz/TimeZoneMap.h"

namespace facebook::velox::util {

constexpr int32_t kLeapDays[] =
    {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
constexpr int32_t kNormalDays[] =
    {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
constexpr int32_t kCumulativeDays[] =
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
constexpr int32_t kCumulativeLeapDays[] =
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366};

constexpr int32_t kCumulativeYearDays[] = {
    0,      365,    730,    1096,   1461,   1826,   2191,   2557,   2922,
    3287,   3652,   4018,   4383,   4748,   5113,   5479,   5844,   6209,
    6574,   6940,   7305,   7670,   8035,   8401,   8766,   9131,   9496,
    9862,   10227,  10592,  10957,  11323,  11688,  12053,  12418,  12784,
    13149,  13514,  13879,  14245,  14610,  14975,  15340,  15706,  16071,
    16436,  16801,  17167,  17532,  17897,  18262,  18628,  18993,  19358,
    19723,  20089,  20454,  20819,  21184,  21550,  21915,  22280,  22645,
    23011,  23376,  23741,  24106,  24472,  24837,  25202,  25567,  25933,
    26298,  26663,  27028,  27394,  27759,  28124,  28489,  28855,  29220,
    29585,  29950,  30316,  30681,  31046,  31411,  31777,  32142,  32507,
    32872,  33238,  33603,  33968,  34333,  34699,  35064,  35429,  35794,
    36160,  36525,  36890,  37255,  37621,  37986,  38351,  38716,  39082,
    39447,  39812,  40177,  40543,  40908,  41273,  41638,  42004,  42369,
    42734,  43099,  43465,  43830,  44195,  44560,  44926,  45291,  45656,
    46021,  46387,  46752,  47117,  47482,  47847,  48212,  48577,  48942,
    49308,  49673,  50038,  50403,  50769,  51134,  51499,  51864,  52230,
    52595,  52960,  53325,  53691,  54056,  54421,  54786,  55152,  55517,
    55882,  56247,  56613,  56978,  57343,  57708,  58074,  58439,  58804,
    59169,  59535,  59900,  60265,  60630,  60996,  61361,  61726,  62091,
    62457,  62822,  63187,  63552,  63918,  64283,  64648,  65013,  65379,
    65744,  66109,  66474,  66840,  67205,  67570,  67935,  68301,  68666,
    69031,  69396,  69762,  70127,  70492,  70857,  71223,  71588,  71953,
    72318,  72684,  73049,  73414,  73779,  74145,  74510,  74875,  75240,
    75606,  75971,  76336,  76701,  77067,  77432,  77797,  78162,  78528,
    78893,  79258,  79623,  79989,  80354,  80719,  81084,  81450,  81815,
    82180,  82545,  82911,  83276,  83641,  84006,  84371,  84736,  85101,
    85466,  85832,  86197,  86562,  86927,  87293,  87658,  88023,  88388,
    88754,  89119,  89484,  89849,  90215,  90580,  90945,  91310,  91676,
    92041,  92406,  92771,  93137,  93502,  93867,  94232,  94598,  94963,
    95328,  95693,  96059,  96424,  96789,  97154,  97520,  97885,  98250,
    98615,  98981,  99346,  99711,  100076, 100442, 100807, 101172, 101537,
    101903, 102268, 102633, 102998, 103364, 103729, 104094, 104459, 104825,
    105190, 105555, 105920, 106286, 106651, 107016, 107381, 107747, 108112,
    108477, 108842, 109208, 109573, 109938, 110303, 110669, 111034, 111399,
    111764, 112130, 112495, 112860, 113225, 113591, 113956, 114321, 114686,
    115052, 115417, 115782, 116147, 116513, 116878, 117243, 117608, 117974,
    118339, 118704, 119069, 119435, 119800, 120165, 120530, 120895, 121260,
    121625, 121990, 122356, 122721, 123086, 123451, 123817, 124182, 124547,
    124912, 125278, 125643, 126008, 126373, 126739, 127104, 127469, 127834,
    128200, 128565, 128930, 129295, 129661, 130026, 130391, 130756, 131122,
    131487, 131852, 132217, 132583, 132948, 133313, 133678, 134044, 134409,
    134774, 135139, 135505, 135870, 136235, 136600, 136966, 137331, 137696,
    138061, 138427, 138792, 139157, 139522, 139888, 140253, 140618, 140983,
    141349, 141714, 142079, 142444, 142810, 143175, 143540, 143905, 144271,
    144636, 145001, 145366, 145732, 146097,
};

namespace {

inline bool characterIsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
      c == '\r';
}

inline bool characterIsDigit(char c) {
  return c >= '0' && c <= '9';
}

bool parseDoubleDigit(
    const char* buf,
    size_t len,
    size_t& pos,
    int32_t& result) {
  if (pos < len && characterIsDigit(buf[pos])) {
    result = buf[pos++] - '0';
    if (pos < len && characterIsDigit(buf[pos])) {
      result = (buf[pos++] - '0') + result * 10;
    }
    return true;
  }
  return false;
}

bool isValidWeekDate(int32_t weekYear, int32_t weekOfYear, int32_t dayOfWeek) {
  if (dayOfWeek < 1 || dayOfWeek > 7) {
    return false;
  }
  if (weekOfYear < 1 || weekOfYear > 52) {
    return false;
  }
  if (weekYear < kMinYear || weekYear > kMaxYear) {
    return false;
  }
  return true;
}

bool isValidWeekOfMonthDate(
    int32_t year,
    int32_t month,
    int32_t weekOfMonth,
    int32_t dayOfWeek) {
  if (year < 1 || year > kMaxYear) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }

  Expected<int64_t> daysSinceEpochOfFirstDayOfMonth =
      daysSinceEpochFromDate(year, month, 1);
  if (daysSinceEpochOfFirstDayOfMonth.hasError()) {
    return false;
  }
  daysSinceEpochOfFirstDayOfMonth = daysSinceEpochOfFirstDayOfMonth.value();

  // Calculates the actual number of week of month and validates if it is in the
  // valid range.
  const int32_t firstDayOfWeek =
      extractISODayOfTheWeek(daysSinceEpochOfFirstDayOfMonth.value());
  const int32_t firstWeekLength = 7 - firstDayOfWeek + 1;
  const int32_t monthLength =
      isLeapYear(year) ? kLeapDays[month] : kNormalDays[month];
  const int32_t actualWeeks = 1 + ceil((monthLength - firstWeekLength) / 7.0);
  if (weekOfMonth < 1 || weekOfMonth > actualWeeks) {
    return false;
  }

  // Validate day of week.
  // If dayOfWeek is before the first day of week, it is considered invalid.
  if (weekOfMonth == 1 && dayOfWeek < firstDayOfWeek) {
    return false;
  }
  const int32_t lastWeekLength = (monthLength - firstWeekLength) % 7;
  // If dayOfWeek is after the last day of the last week of the month, it is
  // considered invalid.
  if (weekOfMonth == actualWeeks && lastWeekLength != 0 &&
      dayOfWeek > lastWeekLength) {
    return false;
  }

  return true;
}

inline bool validDate(int64_t daysSinceEpoch) {
  return daysSinceEpoch >= std::numeric_limits<int32_t>::min() &&
      daysSinceEpoch <= std::numeric_limits<int32_t>::max();
}

// Skip leading spaces.
inline void skipSpaces(const char* buf, size_t len, size_t& pos) {
  while (pos < len && characterIsSpace(buf[pos])) {
    pos++;
  }
}

bool tryParseDateString(
    const char* buf,
    size_t len,
    size_t& pos,
    int64_t& daysSinceEpoch,
    ParseMode mode) {
  pos = 0;
  if (len == 0) {
    return false;
  }

  int32_t day = 0;
  int32_t month = -1;
  int32_t year = 0;
  bool yearneg = false;
  // Whether a sign is included in the date string.
  bool sign = false;
  int sep;
  if (mode != ParseMode::kIso8601) {
    skipSpaces(buf, len, pos);
  }

  if (pos >= len) {
    return false;
  }
  if (buf[pos] == '-') {
    sign = true;
    yearneg = true;
    pos++;
    if (pos >= len) {
      return false;
    }
  } else if (buf[pos] == '+') {
    sign = true;
    pos++;
    if (pos >= len) {
      return false;
    }
  }

  if (!characterIsDigit(buf[pos])) {
    return false;
  }
  // First parse the year.
  for (; pos < len && characterIsDigit(buf[pos]); pos++) {
    year = checkedPlus((buf[pos] - '0'), checkedMultiply(year, 10));
    if (year > kMaxYear) {
      break;
    }
  }
  /// Spark digits of year must >= 4. The following formats are allowed:
  /// `[+-]yyyy*`
  /// `[+-]yyyy*-[m]m`
  /// `[+-]yyyy*-[m]m-[d]d`
  /// `[+-]yyyy*-[m]m-[d]d `
  /// `[+-]yyyy*-[m]m-[d]d *`
  /// `[+-]yyyy*-[m]m-[d]dT*`
  if (mode == ParseMode::kSparkCast && pos - sign < 4) {
    return false;
  }
  if (yearneg) {
    year = checkedNegate(year);
    if (year < kMinYear) {
      return false;
    }
  }

  // No month or day.
  if ((mode == ParseMode::kSparkCast || mode == ParseMode::kIso8601) &&
      (pos == len || buf[pos] == 'T')) {
    Expected<int64_t> expected = daysSinceEpochFromDate(year, 1, 1);
    if (expected.hasError()) {
      return false;
    }
    daysSinceEpoch = expected.value();
    return validDate(daysSinceEpoch);
  }

  if (pos >= len) {
    return false;
  }

  // Fetch the separator.
  sep = buf[pos++];
  if (mode == ParseMode::kPrestoCast || mode == ParseMode::kSparkCast ||
      mode == ParseMode::kIso8601) {
    // Only '-' is valid for cast.
    if (sep != '-') {
      return false;
    }
  } else {
    if (sep != ' ' && sep != '-' && sep != '/' && sep != '\\') {
      // Invalid separator.
      return false;
    }
  }

  // Parse the month.
  if (!parseDoubleDigit(buf, len, pos, month)) {
    return false;
  }

  // No day.
  if ((mode == ParseMode::kSparkCast || mode == ParseMode::kIso8601) &&
      (pos == len || buf[pos] == 'T')) {
    Expected<int64_t> expected = daysSinceEpochFromDate(year, month, 1);
    if (expected.hasError()) {
      return false;
    }
    daysSinceEpoch = expected.value();
    return validDate(daysSinceEpoch);
  }

  if (pos >= len) {
    return false;
  }

  if (buf[pos++] != sep) {
    return false;
  }

  if (pos >= len) {
    return false;
  }

  // Now parse the day.
  if (!parseDoubleDigit(buf, len, pos, day)) {
    return false;
  }

  if (mode == ParseMode::kPrestoCast || mode == ParseMode::kIso8601) {
    Expected<int64_t> expected = daysSinceEpochFromDate(year, month, day);
    if (expected.hasError()) {
      return false;
    }
    daysSinceEpoch = expected.value();

    if (mode == ParseMode::kPrestoCast) {
      skipSpaces(buf, len, pos);
    }

    if (pos == len) {
      return validDate(daysSinceEpoch);
    }
    return false;
  }

  // In non-standard cast mode, an optional trailing 'T' or space followed
  // by any optional characters are valid patterns.
  if (mode == ParseMode::kSparkCast) {
    Expected<int64_t> expected = daysSinceEpochFromDate(year, month, day);
    if (expected.hasError()) {
      return false;
    }
    daysSinceEpoch = expected.value();

    if (!validDate(daysSinceEpoch)) {
      return false;
    }

    if (pos == len) {
      return true;
    }

    if (buf[pos] == 'T' || buf[pos] == ' ') {
      return true;
    }
    return false;
  }

  // Check for an optional trailing " (BC)".
  if (len - pos >= 5 && characterIsSpace(buf[pos]) && buf[pos + 1] == '(' &&
      buf[pos + 2] == 'B' && buf[pos + 3] == 'C' && buf[pos + 4] == ')') {
    if (yearneg || year == 0) {
      return false;
    }
    year = -year + 1;
    pos += 5;

    if (year < kMinYear) {
      return false;
    }
  }

  // In strict mode, check remaining string for non-space characters.
  if (mode == ParseMode::kStrict || mode == ParseMode::kIso8601) {
    skipSpaces(buf, len, pos);

    // Check position. if end was not reached, non-space chars remaining.
    if (pos < len) {
      return false;
    }
  } else {
    // In non-strict mode, check for any direct trailing digits.
    if (pos < len && characterIsDigit(buf[pos])) {
      return false;
    }
  }
  Expected<int64_t> expected = daysSinceEpochFromDate(year, month, day);
  if (expected.hasError()) {
    return false;
  }
  daysSinceEpoch = expected.value();
  return true;
}

void parseTimeSeparator(
    const char* buf,
    size_t& pos,
    TimestampParseMode parseMode) {
  switch (parseMode) {
    case TimestampParseMode::kIso8601:
      if (buf[pos] == 'T') {
        pos++;
      }
      break;
    case TimestampParseMode::kPrestoCast:
      if (buf[pos] == ' ') {
        pos++;
      }
      break;
    case TimestampParseMode::kLegacyCast:
    case TimestampParseMode::kSparkCast:
      if (buf[pos] == ' ' || buf[pos] == 'T') {
        pos++;
      }
      break;
  }
}

// String format is hh:mm:ss.microseconds (seconds and microseconds are
// optional).
// ISO 8601
bool tryParseTimeString(
    const char* buf,
    size_t len,
    size_t& pos,
    int64_t& result,
    TimestampParseMode parseMode) {
  static constexpr int sep = ':';
  int32_t hour = 0, min = 0, sec = 0, micros = 0;
  pos = 0;

  if (len == 0) {
    return false;
  }

  if (parseMode != TimestampParseMode::kIso8601) {
    skipSpaces(buf, len, pos);
  }

  if (pos >= len) {
    return false;
  }

  if (!characterIsDigit(buf[pos])) {
    return false;
  }

  // Read the hours.
  if (!parseDoubleDigit(buf, len, pos, hour)) {
    return false;
  }
  if (hour < 0 || hour >= 24) {
    return false;
  }

  if (pos >= len || buf[pos] != sep) {
    if (parseMode == TimestampParseMode::kIso8601) {
      result = fromTime(hour, 0, 0, 0);
      return true;
    }
    return false;
  }

  // Fetch the separator.
  if (buf[pos++] != sep) {
    // Invalid separator.
    return false;
  }

  // Read the minutes.
  if (!parseDoubleDigit(buf, len, pos, min)) {
    return false;
  }
  if (min < 0 || min >= 60) {
    return false;
  }

  // Try to read seconds.
  if (pos < len && buf[pos] == sep) {
    ++pos;
    if (!parseDoubleDigit(buf, len, pos, sec)) {
      return false;
    }
    if (sec < 0 || sec > 60) {
      return false;
    }

    // Try to read microseconds.
    if (pos < len) {
      if (buf[pos] == '.') {
        ++pos;
      } else if (parseMode == TimestampParseMode::kIso8601 && buf[pos] == ',') {
        ++pos;
      }

      if (pos >= len) {
        return false;
      }

      // We expect microseconds.
      int32_t mult = 100000;
      for (; pos < len && characterIsDigit(buf[pos]); pos++, mult /= 10) {
        if (mult > 0) {
          micros += (buf[pos] - '0') * mult;
        }
      }
    }
  }

  result = fromTime(hour, min, sec, micros);
  return true;
}

// String format is [+/-]hh:mm:ss.MMM
// * minutes, seconds, and milliseconds are optional.
// * all separators are optional.
// * . may be replaced with ,
bool tryParsePrestoTimeOffsetString(
    const char* buf,
    size_t len,
    size_t& pos,
    int64_t& result) {
  static constexpr int sep = ':';
  int32_t hour = 0, min = 0, sec = 0, millis = 0;
  pos = 0;
  result = 0;

  if (len == 0) {
    return false;
  }

  if (buf[pos] != '+' && buf[pos] != '-') {
    return false;
  }

  bool positive = buf[pos++] == '+';

  if (pos >= len) {
    return false;
  }

  // Read the hours.
  if (!parseDoubleDigit(buf, len, pos, hour)) {
    return false;
  }
  if (hour < 0 || hour >= 24) {
    return false;
  }

  result += hour * kMillisPerHour;

  if (pos >= len || (buf[pos] != sep && !characterIsDigit(buf[pos]))) {
    result *= positive ? 1 : -1;
    return pos == len;
  }

  // Skip the separator.
  if (buf[pos] == sep) {
    pos++;
  }

  // Read the minutes.
  if (!parseDoubleDigit(buf, len, pos, min)) {
    return false;
  }
  if (min < 0 || min >= 60) {
    return false;
  }

  result += min * kMillisPerMinute;

  if (pos >= len || (buf[pos] != sep && !characterIsDigit(buf[pos]))) {
    result *= positive ? 1 : -1;
    return pos == len;
  }

  // Skip the separator.
  if (buf[pos] == sep) {
    pos++;
  }

  // Try to read seconds.
  if (!parseDoubleDigit(buf, len, pos, sec)) {
    return false;
  }
  if (sec < 0 || sec >= 60) {
    return false;
  }

  result += sec * kMillisPerSecond;

  if (pos >= len ||
      (buf[pos] != '.' && buf[pos] != ',' && !characterIsDigit(buf[pos]))) {
    result *= positive ? 1 : -1;
    return pos == len;
  }

  // Skip the decimal.
  if (buf[pos] == '.' || buf[pos] == ',') {
    pos++;
  }

  // Try to read microseconds.
  if (pos >= len) {
    return false;
  }

  // We expect milliseconds.
  int32_t mult = 100;
  for (; pos < len && mult > 0 && characterIsDigit(buf[pos]);
       pos++, mult /= 10) {
    millis += (buf[pos] - '0') * mult;
  }

  result += millis;
  result *= positive ? 1 : -1;
  return pos == len;
}

// Parses a variety of timestamp strings, depending on the value of
// `parseMode`. Consumes as much of the string as it can and sets `result` to
// the timestamp from whatever it successfully parses. `pos` is set to the
// position of first character that was not consumed. Returns true if it
// successfully parsed at least a date, `result` is only set if true is
// returned.
bool tryParseTimestampString(
    const char* buf,
    size_t len,
    size_t& pos,
    Timestamp& result,
    TimestampParseMode parseMode) {
  int64_t daysSinceEpoch = 0;
  int64_t microsSinceMidnight = 0;

  if (parseMode == TimestampParseMode::kIso8601) {
    // Leading spaces are not allowed.
    size_t startPos = pos;
    skipSpaces(buf, len, pos);
    if (pos > startPos) {
      return false;
    }
  }

  if (parseMode == TimestampParseMode::kIso8601 && pos < len &&
      buf[pos] == 'T') {
    if (pos == len - 1) {
      // If the string is just 'T'.
      return false;
    }
    // No date. Assume 1970-01-01.
  } else if (!tryParseDateString(
                 buf,
                 len,
                 pos,
                 daysSinceEpoch,
                 parseMode == TimestampParseMode::kIso8601 ||
                         parseMode == TimestampParseMode::kSparkCast
                     ? ParseMode::kSparkCast
                     : ParseMode::kNonStrict)) {
    return false;
  }

  if (pos == len) {
    // No time: only a date.
    result = fromDatetime(daysSinceEpoch, 0);
    return true;
  }

  // Try to parse a time field.
  parseTimeSeparator(buf, pos, parseMode);

  size_t timePos = 0;
  if (!tryParseTimeString(
          buf + pos, len - pos, timePos, microsSinceMidnight, parseMode)) {
    // The rest of the string is not a valid time, but it could be relevant to
    // the caller (e.g. it could be a time zone), return the date we parsed
    // and let them decide what to do with the rest.
    result = fromDatetime(daysSinceEpoch, 0);
    return true;
  }

  pos += timePos;
  result = fromDatetime(daysSinceEpoch, microsSinceMidnight);
  return true;
}

} // namespace

bool isLeapYear(int32_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool isValidDate(int32_t year, int32_t month, int32_t day) {
  if (month < 1 || month > 12) {
    return false;
  }
  if (year < kMinYear || year > kMaxYear) {
    return false;
  }
  if (day < 1) {
    return false;
  }
  return isLeapYear(year) ? day <= kLeapDays[month] : day <= kNormalDays[month];
}

bool isValidDayOfYear(int32_t year, int32_t dayOfYear) {
  if (year < kMinYear || year > kMaxYear) {
    return false;
  }
  if (dayOfYear < 1 || dayOfYear > 365 + isLeapYear(year)) {
    return false;
  }
  return true;
}

Expected<int64_t> lastDayOfMonthSinceEpochFromDate(const std::tm& dateTime) {
  auto year = dateTime.tm_year + 1900;
  auto month = dateTime.tm_mon + 1;
  auto day = util::getMaxDayOfMonth(year, month);
  return util::daysSinceEpochFromDate(year, month, day);
}

int32_t getMaxDayOfMonth(int32_t year, int32_t month) {
  return isLeapYear(year) ? kLeapDays[month] : kNormalDays[month];
}

Expected<int64_t>
daysSinceEpochFromDate(int32_t year, int32_t month, int32_t day) {
  int64_t daysSinceEpoch = 0;

  if (!isValidDate(year, month, day)) {
    if (threadSkipErrorDetails()) {
      return folly::makeUnexpected(Status::UserError());
    }
    return folly::makeUnexpected(
        Status::UserError("Date out of range: {}-{}-{}", year, month, day));
  }
  while (year < 1970) {
    year += kYearInterval;
    daysSinceEpoch -= kDaysPerYearInterval;
  }
  while (year >= 2370) {
    year -= kYearInterval;
    daysSinceEpoch += kDaysPerYearInterval;
  }
  daysSinceEpoch += kCumulativeYearDays[year - 1970];
  daysSinceEpoch += isLeapYear(year) ? kCumulativeLeapDays[month - 1]
                                     : kCumulativeDays[month - 1];
  daysSinceEpoch += day - 1;
  return daysSinceEpoch;
}

Expected<int64_t> daysSinceEpochFromWeekDate(
    int32_t weekYear,
    int32_t weekOfYear,
    int32_t dayOfWeek) {
  if (!isValidWeekDate(weekYear, weekOfYear, dayOfWeek)) {
    if (threadSkipErrorDetails()) {
      return folly::makeUnexpected(Status::UserError());
    }
    return folly::makeUnexpected(Status::UserError(
        "Date out of range: {}-{}-{}", weekYear, weekOfYear, dayOfWeek));
  }

  return daysSinceEpochFromDate(weekYear, 1, 4)
      .then([&weekOfYear, &dayOfWeek](int64_t daysSinceEpochOfJanFourth) {
        int32_t firstDayOfWeekYear =
            extractISODayOfTheWeek(daysSinceEpochOfJanFourth);

        return daysSinceEpochOfJanFourth - (firstDayOfWeekYear - 1) +
            7 * (weekOfYear - 1) + dayOfWeek - 1;
      });
}

Expected<int64_t> daysSinceEpochFromWeekOfMonthDate(
    int32_t year,
    int32_t month,
    int32_t weekOfMonth,
    int32_t dayOfWeek,
    bool lenient) {
  if (!lenient &&
      !isValidWeekOfMonthDate(year, month, weekOfMonth, dayOfWeek)) {
    if (threadSkipErrorDetails()) {
      return folly::makeUnexpected(Status::UserError());
    }
    return folly::makeUnexpected(Status::UserError(
        "Date out of range: {}-{}-{}-{}", year, month, weekOfMonth, dayOfWeek));
  }

  // Adjusts the year and month to ensure month is within the range 1-12,
  // accounting for overflow or underflow.
  int32_t additionYears = 0;
  if (month < 1) {
    additionYears = month / 12 - 1;
    month = 12 - abs(month) % 12;
  } else if (month > 12) {
    additionYears = (month - 1) / 12;
    month = (month - 1) % 12 + 1;
  }
  year += additionYears;

  return daysSinceEpochFromDate(year, month, 1)
      .then(
          [&dayOfWeek, &weekOfMonth](int64_t daysSinceEpochOfFirstDayOfMonth) {
            const int32_t firstDayOfWeek =
                extractISODayOfTheWeek(daysSinceEpochOfFirstDayOfMonth);
            int32_t days;
            if (dayOfWeek < 1) {
              days = 7 - abs(dayOfWeek - 1) % 7;
            } else if (dayOfWeek > 7) {
              days = (dayOfWeek - 1) % 7;
            } else {
              days = dayOfWeek % 7;
            }
            return daysSinceEpochOfFirstDayOfMonth - (firstDayOfWeek - 1) +
                7 * (weekOfMonth - 1) + days - 1;
          });
}

Expected<int64_t> daysSinceEpochFromDayOfYear(int32_t year, int32_t dayOfYear) {
  if (!isValidDayOfYear(year, dayOfYear)) {
    if (threadSkipErrorDetails()) {
      return folly::makeUnexpected(Status::UserError());
    }
    return folly::makeUnexpected(
        Status::UserError("Day of year out of range: {}", dayOfYear));
  }
  return daysSinceEpochFromDate(year, 1, 1)
      .then([&dayOfYear](int64_t startOfYear) {
        return startOfYear + (dayOfYear - 1);
      });
}

Expected<int32_t> fromDateString(const char* str, size_t len, ParseMode mode) {
  int64_t daysSinceEpoch;
  size_t pos = 0;

  if (!tryParseDateString(str, len, pos, daysSinceEpoch, mode)) {
    if (threadSkipErrorDetails()) {
      return folly::makeUnexpected(Status::UserError());
    }

    switch (mode) {
      case ParseMode::kPrestoCast:
        return folly::makeUnexpected(Status::UserError(
            "Unable to parse date value: \"{}\". "
            "Valid date string pattern is (YYYY-MM-DD), "
            "and can be prefixed with [+-]",
            std::string(str, len)));
      case ParseMode::kSparkCast:
        return folly::makeUnexpected(Status::UserError(
            "Unable to parse date value: \"{}\". "
            "Valid date string patterns include "
            "([y]y*, [y]y*-[m]m*, [y]y*-[m]m*-[d]d*, "
            "[y]y*-[m]m*-[d]d* *, [y]y*-[m]m*-[d]d*T*), "
            "and any pattern prefixed with [+-]",
            std::string(str, len)));
      case ParseMode::kIso8601:
        return folly::makeUnexpected(Status::UserError(
            "Unable to parse date value: \"{}\". "
            "Valid date string patterns include "
            "([y]y*, [y]y*-[m]m*, [y]y*-[m]m*-[d]d*, "
            "[y]y*-[m]m*-[d]d* *), "
            "and any pattern prefixed with [+-]",
            std::string(str, len)));
      default:
        VELOX_UNREACHABLE();
    }
  }
  return daysSinceEpoch;
}

int32_t extractISODayOfTheWeek(int64_t daysSinceEpoch) {
  // date of 0 is 1970-01-01, which was a Thursday (4)
  // -7 = 4
  // -6 = 5
  // -5 = 6
  // -4 = 7
  // -3 = 1
  // -2 = 2
  // -1 = 3
  // 0  = 4
  // 1  = 5
  // 2  = 6
  // 3  = 7
  // 4  = 1
  // 5  = 2
  // 6  = 3
  // 7  = 4
  if (daysSinceEpoch < 0) {
    // negative date: start off at 4 and cycle downwards
    return (7 - ((-int128_t(daysSinceEpoch) + 3) % 7));
  } else {
    // positive date: start off at 4 and cycle upwards
    return ((int128_t(daysSinceEpoch) + 3) % 7) + 1;
  }
}

int64_t
fromTime(int32_t hour, int32_t minute, int32_t second, int32_t microseconds) {
  int64_t result;
  result = hour; // hours
  result = result * kMinsPerHour + minute; // hours -> minutes
  result = result * kSecsPerMinute + second; // minutes -> seconds
  result = result * kMicrosPerSec + microseconds; // seconds -> microseconds
  return result;
}

Timestamp fromDatetime(int64_t daysSinceEpoch, int64_t microsSinceMidnight) {
  int64_t secondsSinceEpoch =
      static_cast<int64_t>(daysSinceEpoch) * kSecsPerDay;
  secondsSinceEpoch += microsSinceMidnight / kMicrosPerSec;
  return Timestamp(
      secondsSinceEpoch,
      (microsSinceMidnight % kMicrosPerSec) * kNanosPerMicro);
}

namespace {

Status parserError(const char* str, size_t len) {
  if (threadSkipErrorDetails()) {
    return Status::UserError();
  }
  return Status::UserError(
      "Unable to parse timestamp value: \"{}\", "
      "expected format is (YYYY-MM-DD HH:MM:SS[.MS])",
      std::string(str, len));
}

} // namespace

Expected<Timestamp>
fromTimestampString(const char* str, size_t len, TimestampParseMode parseMode) {
  size_t pos = 0;
  Timestamp resultTimestamp;

  if (!tryParseTimestampString(str, len, pos, resultTimestamp, parseMode)) {
    return folly::makeUnexpected(parserError(str, len));
  }
  skipSpaces(str, len, pos);

  // If not all input was consumed.
  if (pos < len) {
    return folly::makeUnexpected(parserError(str, len));
  }
  VELOX_CHECK_EQ(pos, len);
  return resultTimestamp;
}

Expected<ParsedTimestampWithTimeZone> fromTimestampWithTimezoneString(
    const char* str,
    size_t len,
    TimestampParseMode parseMode) {
  size_t pos = 0;
  Timestamp resultTimestamp;

  if (!tryParseTimestampString(str, len, pos, resultTimestamp, parseMode)) {
    return folly::makeUnexpected(parserError(str, len));
  }

  const tz::TimeZone* timeZone = nullptr;
  std::optional<int64_t> offset = std::nullopt;

  if (pos < len && parseMode != TimestampParseMode::kIso8601 &&
      characterIsSpace(str[pos])) {
    pos++;
  }

  // If there is anything left to parse, it must be a timezone definition.
  if (pos < len) {
    if (parseMode == TimestampParseMode::kIso8601) {
      // Only +HH:MM and -HH:MM are supported. Minutes, seconds, etc. in the
      // offset are optional.
      if (str[pos] != 'Z' && str[pos] != '+' && str[pos] != '-') {
        return folly::makeUnexpected(parserError(str, len));
      }
    }

    size_t timezonePos = pos;
    while (timezonePos < len && !characterIsSpace(str[timezonePos])) {
      timezonePos++;
    }

    std::string_view timeZoneName(str + pos, timezonePos - pos);

    if ((timeZone = tz::locateZone(timeZoneName, false)) == nullptr) {
      int64_t offsetMillis = 0;
      size_t offsetPos = 0;
      if (parseMode == TimestampParseMode::kPrestoCast &&
          tryParsePrestoTimeOffsetString(
              str + pos, timezonePos - pos, offsetPos, offsetMillis)) {
        offset = offsetMillis;
      } else {
        return folly::makeUnexpected(
            Status::UserError("Unknown timezone value: \"{}\"", timeZoneName));
      }
    }

    // Skip any spaces at the end.
    pos = timezonePos;
    if (parseMode != TimestampParseMode::kIso8601) {
      skipSpaces(str, len, pos);
    }

    if (pos < len) {
      return folly::makeUnexpected(parserError(str, len));
    }
  }
  return {{resultTimestamp, timeZone, offset}};
}

Timestamp fromParsedTimestampWithTimeZone(
    ParsedTimestampWithTimeZone parsed,
    const tz::TimeZone* sessionTimeZone) {
  if (parsed.timeZone) {
    parsed.timestamp.toGMT(*parsed.timeZone);
  } else if (parsed.offsetMillis.has_value()) {
    auto seconds = parsed.timestamp.getSeconds();
    // use int128_t to avoid overflow.
    __int128_t nanos = parsed.timestamp.getNanos();
    seconds -= parsed.offsetMillis.value() / util::kMillisPerSecond;
    nanos -= (parsed.offsetMillis.value() % util::kMillisPerSecond) *
        util::kNanosPerMicro * util::kMicrosPerMsec;
    if (nanos < 0) {
      seconds -= 1;
      nanos += Timestamp::kNanosInSecond;
    } else if (nanos > Timestamp::kMaxNanos) {
      seconds += 1;
      nanos -= Timestamp::kNanosInSecond;
    }
    parsed.timestamp = Timestamp(seconds, nanos);
  } else {
    if (sessionTimeZone) {
      parsed.timestamp.toGMT(*sessionTimeZone);
    }
  }
  return parsed.timestamp;
}

int32_t toDate(const Timestamp& timestamp, const tz::TimeZone* timeZone_) {
  auto convertToDate = [](const Timestamp& t) -> int32_t {
    static const int32_t kSecsPerDay{86'400};
    auto seconds = t.getSeconds();
    if (seconds >= 0 || seconds % kSecsPerDay == 0) {
      return seconds / kSecsPerDay;
    }
    // For division with negatives, minus 1 to compensate the discarded
    // fractional part. e.g. -1/86'400 yields 0, yet it should be considered
    // as -1 day.
    return seconds / kSecsPerDay - 1;
  };

  if (timeZone_ != nullptr) {
    Timestamp copy = timestamp;
    copy.toTimezone(*timeZone_);
    return convertToDate(copy);
  }

  return convertToDate(timestamp);
}

} // namespace facebook::velox::util
