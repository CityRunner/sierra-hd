// SPDX-License-Identifier: GPL-3.0-or-later
//
// The path matcher of kDeviceInfo (K_DEVICE_INFO_PATHS_EQUAL), which is
// ScummVM's Common::matchString(str, pattern, false, "/"): the wildcards,
// escapes, and that no wildcard crosses a slash.

#include "path_match.hpp"
#include "sci_test.hpp"

using sci::vm::match_path;

SCI_TEST(literal_paths_match_only_themselves) {
    SCI_CHECK(match_path("", ""));
    SCI_CHECK(match_path("abc", "abc"));
    SCI_CHECK(!match_path("abc", "abd"));
    SCI_CHECK(!match_path("ab", "abc"));
    SCI_CHECK(!match_path("abc", "ab"));
    SCI_CHECK(!match_path("ABC", "abc")); // Case matters.
}

SCI_TEST(star_matches_any_run_within_a_path_part) {
    SCI_CHECK(match_path("", "*"));
    SCI_CHECK(match_path("abc", "*"));
    SCI_CHECK(match_path("abc", "a*"));
    SCI_CHECK(match_path("abc", "*c"));
    SCI_CHECK(match_path("abc", "abc*"));
    SCI_CHECK(match_path("abc", "a*b*c"));
    SCI_CHECK(match_path("abcbd", "*bd")); // Backtracks past the first b.
    SCI_CHECK(match_path("xaby", "*ab*"));
    SCI_CHECK(!match_path("abc", "*d"));
    SCI_CHECK(!match_path("abc", "*b"));
}

SCI_TEST(star_does_not_cross_a_slash) {
    SCI_CHECK(!match_path("a/b", "*"));
    SCI_CHECK(!match_path("a/b", "*b"));
    SCI_CHECK(!match_path("a/b", "a*"));
    SCI_CHECK(!match_path("ab/cb", "*b"));
    SCI_CHECK(!match_path("a/b/c", "*/*"));
    SCI_CHECK(match_path("a/b", "*/*"));
    SCI_CHECK(match_path("a/", "*/"));
    SCI_CHECK(match_path("dir/file.txt", "*/*.txt"));
    SCI_CHECK(match_path("dir/file.txt", "dir/*"));
}

SCI_TEST(question_mark_matches_one_character_but_not_a_slash) {
    SCI_CHECK(match_path("abc", "a?c"));
    SCI_CHECK(match_path("abc", "???"));
    SCI_CHECK(!match_path("abc", "??"));
    SCI_CHECK(!match_path("ab", "ab?"));
    SCI_CHECK(!match_path("a/c", "a?c"));
    SCI_CHECK(!match_path("", "?"));
}

SCI_TEST(hash_matches_one_digit) {
    SCI_CHECK(match_path("a1", "a#"));
    SCI_CHECK(match_path("save12", "save##"));
    SCI_CHECK(!match_path("ab", "a#"));
    SCI_CHECK(!match_path("a", "a#"));
    SCI_CHECK(!match_path("a/", "a#"));
}

SCI_TEST(backslash_escapes_hash_and_backslash_only) {
    SCI_CHECK(match_path("a#", "a\\#"));
    SCI_CHECK(!match_path("a1", "a\\#"));
    SCI_CHECK(match_path("c:\\games", "c:\\\\games"));
    // A lone backslash escapes the letter after it, so a DOS path is not
    // equal to itself.
    SCI_CHECK(!match_path("c:\\games", "c:\\games"));
    // As in Common::matchString, * and ? stay wildcards after a backslash.
    SCI_CHECK(match_path("a*", "a\\*"));
    SCI_CHECK(match_path("ab", "a\\*"));
    SCI_CHECK(match_path("ab", "a\\?"));
    SCI_CHECK(!match_path("a/b", "a\\*"));
}
