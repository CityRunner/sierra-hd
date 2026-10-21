// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files the scripts open (kFOpen, kFGets, kFPuts, kFClose) over a store
// in memory, as ScummVM's engines/sci/engine/file.cpp does them: the open
// modes, the handle numbers, fgets_wrapper's lines and what close writes.

#include "files.hpp"
#include "sci/vm/files.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using sci::vm::Files;
using sci::vm::MemoryFileStore;

namespace {

constexpr std::uint16_t failed = 0xFFFF; // SIGNAL_REG.

std::vector<std::uint8_t> bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

MemoryFileStore store_with(std::string_view name, std::string_view text) {
    return MemoryFileStore(std::map<std::string, std::vector<std::uint8_t>>{{std::string(name), bytes(text)}});
}

std::uint16_t open(Files& files, const std::string& name, int mode) {
    std::string error;
    const std::uint16_t handle = files.open(name, mode, error);
    SCI_CHECK(error.empty());
    return handle;
}

std::optional<std::string> line(Files& files, std::uint16_t handle, int max_size) {
    std::string error;
    auto read = files.read_line(handle, max_size, error);
    SCI_CHECK(error.empty());
    if (!read) {
        return std::nullopt;
    }
    return read->text;
}

} // namespace

SCI_TEST(open_or_fail_reads_a_written_file_else_a_game_file) {
    MemoryFileStore store = store_with("game.txt", "from the game\n");
    Files files;
    files.set_store(&store);
    SCI_CHECK(open(files, "missing.txt", sci::vm::open_or_fail) == failed);
    const std::uint16_t game = open(files, "game.txt", sci::vm::open_or_fail);
    SCI_CHECK(game == 1);
    SCI_CHECK(line(files, game, 100) == "from the game");
    // The save file comes before the game file of the same name.
    SCI_CHECK(store.write("game.txt", bytes("saved\n")));
    const std::uint16_t saved = open(files, "game.txt", sci::vm::open_or_fail);
    SCI_CHECK(saved == 2);
    SCI_CHECK(line(files, saved, 100) == "saved");
}

SCI_TEST(no_store_opens_no_file) {
    Files files;
    SCI_CHECK(open(files, "game.txt", sci::vm::open_or_fail) == failed);
    SCI_CHECK(open(files, "new.txt", sci::vm::create) == failed);
    SCI_CHECK(open(files, "new.txt", sci::vm::open_or_create) == failed);
}

SCI_TEST(an_unsupported_mode_is_an_error) {
    MemoryFileStore store;
    Files files;
    files.set_store(&store);
    std::string error;
    SCI_CHECK(files.open("game.txt", 3, error) == failed);
    SCI_CHECK(error == "file_open: unsupported mode 3 (filename 'game.txt')");
}

SCI_TEST(create_and_open_or_create_start_an_empty_file) {
    // Both are openForSaving: what the file held is gone once it closes,
    // and the handle is for writing only.
    for (const int mode : {sci::vm::create, sci::vm::open_or_create}) {
        MemoryFileStore store;
        SCI_CHECK(store.write("quiz.dat", bytes("old answers\n")));
        Files files;
        files.set_store(&store);
        const std::uint16_t handle = open(files, "quiz.dat", mode);
        SCI_CHECK(handle == 1);
        std::string error;
        SCI_CHECK(!files.read_line(handle, 100, error).has_value());
        SCI_CHECK(error == "fgets_wrapper: Trying to read from file 'quiz.dat' opened for writing");
        SCI_CHECK(files.close(handle));
        SCI_CHECK(store.read("quiz.dat") == std::vector<std::uint8_t>{});
    }
}

SCI_TEST(what_is_written_is_in_the_store_after_close) {
    MemoryFileStore store;
    Files files;
    files.set_store(&store);
    const std::uint16_t handle = open(files, "larry3.drv", sci::vm::create);
    SCI_CHECK(files.write(handle, "ab") == 2);
    SCI_CHECK(files.write(handle, "c\n") == 2);
    SCI_CHECK(files.close(handle));
    SCI_CHECK(store.written().size() == 1);
    SCI_CHECK(store.read("larry3.drv") == bytes("abc\n"));
    // A file open for reading takes no writes.
    const std::uint16_t reading = open(files, "larry3.drv", sci::vm::open_or_fail);
    SCI_CHECK(!files.write(reading, "x").has_value());
    SCI_CHECK(line(files, reading, 100) == "abc");
}

SCI_TEST(handles_start_at_1_and_reuse_the_first_free_one) {
    MemoryFileStore store;
    Files files;
    files.set_store(&store);
    // findFreeFileHandle: handle 0 is never used; past the five slots the
    // table grows.
    for (std::uint16_t expected = 1; expected <= 6; ++expected) {
        SCI_CHECK(open(files, "f" + std::to_string(expected), sci::vm::create) == expected);
    }
    SCI_CHECK(files.close(2));
    SCI_CHECK(files.close(5));
    SCI_CHECK(open(files, "again", sci::vm::create) == 2);
    SCI_CHECK(open(files, "again", sci::vm::create) == 5);
    SCI_CHECK(open(files, "again", sci::vm::create) == 7);
}

SCI_TEST(unused_handles_do_nothing) {
    MemoryFileStore store = store_with("game.txt", "line\n");
    Files files;
    files.set_store(&store);
    SCI_CHECK(!files.close(0));
    SCI_CHECK(!files.close(1));
    SCI_CHECK(!files.close(100));
    SCI_CHECK(!line(files, 1, 100).has_value());
    SCI_CHECK(!files.write(1, "x").has_value());
    const std::uint16_t handle = open(files, "game.txt", sci::vm::open_or_fail);
    SCI_CHECK(files.close(handle));
    SCI_CHECK(!files.close(handle)); // Closed twice.
    SCI_CHECK(!line(files, handle, 100).has_value());
}

SCI_TEST(read_line_ends_at_lf_cr_lf_or_cr) {
    // SeekableReadStream::readLine with CR handling, then fgets_wrapper drops
    // the line feed.
    MemoryFileStore store = store_with("lines.txt", "one\ntwo\r\nthree\rfour\r\r\nfive");
    Files files;
    files.set_store(&store);
    const std::uint16_t handle = open(files, "lines.txt", sci::vm::open_or_fail);
    SCI_CHECK(line(files, handle, 100) == "one");
    SCI_CHECK(line(files, handle, 100) == "two");
    SCI_CHECK(line(files, handle, 100) == "three");
    // A CR before a CR ends a line; the CR LF after it ends an empty one.
    SCI_CHECK(line(files, handle, 100) == "four");
    SCI_CHECK(line(files, handle, 100) == "");
    // The last line needs no line feed.
    SCI_CHECK(line(files, handle, 100) == "five");
    // At the end of the file the buffer stays cleared.
    SCI_CHECK(line(files, handle, 100) == "");
    SCI_CHECK(line(files, handle, 100) == "");
}

SCI_TEST(read_line_counts_the_line_feed_so_only_the_end_reads_nothing) {
    // kFGets returns NULL only when fgets_wrapper read no bytes: an empty
    // line still counts its line feed.
    MemoryFileStore store = store_with("empty.txt", "\nlast");
    Files files;
    files.set_store(&store);
    const std::uint16_t handle = open(files, "empty.txt", sci::vm::open_or_fail);
    std::string error;
    const auto empty = files.read_line(handle, 100, error);
    SCI_CHECK(empty.has_value() && empty->text.empty() && empty->bytes == 1);
    const auto last = files.read_line(handle, 100, error);
    SCI_CHECK(last.has_value() && last->text == "last" && last->bytes == 4);
    const auto end = files.read_line(handle, 100, error);
    SCI_CHECK(end.has_value() && end->text.empty() && end->bytes == 0);
    SCI_CHECK(error.empty());
}

SCI_TEST(read_line_takes_at_most_max_size_minus_1_bytes) {
    MemoryFileStore store = store_with("long.txt", "abcdef\nxy\n");
    Files files;
    files.set_store(&store);
    const std::uint16_t handle = open(files, "long.txt", sci::vm::open_or_fail);
    SCI_CHECK(line(files, handle, 4) == "abc");
    SCI_CHECK(line(files, handle, 4) == "def");
    // The line feed left behind is a line of its own.
    SCI_CHECK(line(files, handle, 4) == "");
    // A size of 1 or less reads nothing and leaves the position.
    SCI_CHECK(line(files, handle, 1) == "");
    SCI_CHECK(line(files, handle, 0) == "");
    // The line feed counts toward the size: "xy\n" needs 4 bytes.
    SCI_CHECK(line(files, handle, 3) == "xy");
    SCI_CHECK(line(files, handle, 3) == "");
    SCI_CHECK(line(files, handle, 3) == "");
}

SCI_TEST(read_line_stops_at_a_nul) {
    // strnlen counts up to the NUL; the rest of that line is read, not kept.
    MemoryFileStore store = store_with("nul.txt", std::string_view("a\0b\nc\r", 6));
    Files files;
    files.set_store(&store);
    const std::uint16_t handle = open(files, "nul.txt", sci::vm::open_or_fail);
    SCI_CHECK(line(files, handle, 100) == "a");
    // A CR at the end of the file ends the line.
    SCI_CHECK(line(files, handle, 100) == "c");
    SCI_CHECK(line(files, handle, 100) == "");
}

SCI_TEST(close_all_writes_the_open_files_and_frees_every_handle) {
    MemoryFileStore store = store_with("game.txt", "line\n");
    Files files;
    files.set_store(&store);
    const std::uint16_t reading = open(files, "game.txt", sci::vm::open_or_fail);
    const std::uint16_t writing = open(files, "out.txt", sci::vm::create);
    for (int i = 0; i < 5; ++i) {
        SCI_CHECK(open(files, "more.txt", sci::vm::create) == 3 + i);
    }
    SCI_CHECK(files.write(writing, "kept") == 4);
    files.close_all();
    SCI_CHECK(store.read("out.txt") == bytes("kept"));
    SCI_CHECK(store.read("more.txt") == std::vector<std::uint8_t>{});
    SCI_CHECK(!files.close(reading));
    SCI_CHECK(!files.close(7));
    SCI_CHECK(open(files, "game.txt", sci::vm::open_or_fail) == 1);
}
