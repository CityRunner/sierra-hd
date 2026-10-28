// SPDX-License-Identifier: GPL-3.0-or-later
//
// The canonical serialisation of the VM state (milestone 8, docs/ASSUMPTIONS.md,
// "Milestone 8: the VM state"). Each part of the state writes itself with a
// save_state(StateWriter&) member, as ScummVM's saveLoadWithSerializer does
// for saving: integers little-endian at a fixed width, containers as a count
// and their elements in a defined order, pointers as the ids or indices of
// what they point to. The stream is split into named components; the save
// format (next task) stores the same stream and reads it back with
// StateReader and a load_state per part.

#pragma once

#include "sci/vm/reg.hpp"
#include "sci/vm/state_hash.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

class Machine;

class StateWriter {
public:
    // Starts the next component; what follows belongs to it.
    void begin(std::string_view name);

    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void boolean(bool value) { u8(value ? 1 : 0); }

    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);

    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }

    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

    // A size or count, as u32.
    void size(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }

    void reg(Reg value) {
        u16(value.segment);
        u16(value.offset);
    }

    void bytes(std::span<const std::uint8_t> data);
    void string(std::string_view text);

    void regs(std::span<const Reg> values) {
        size(values.size());
        for (const Reg value : values) {
            reg(value);
        }
    }

    struct Component {
        std::string name;
        std::size_t begin = 0; // Offsets in data().
        std::size_t end = 0;
    };

    [[nodiscard]] const std::vector<std::uint8_t>& data() const { return bytes_; }

    // The components, the last one ended.
    [[nodiscard]] std::vector<Component> components() const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<Component> components_;
};

// Reads what StateWriter wrote, in the same order. Reading past the end
// yields zeros and marks the reader failed.
class StateReader {
public:
    explicit StateReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8();

    bool boolean() { return u8() != 0; }

    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();

    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    std::size_t size() { return u32(); }

    Reg reg() {
        const std::uint16_t segment = u16();
        return Reg{.segment = segment, .offset = u16()};
    }

    std::vector<std::uint8_t> bytes();
    std::string string();

    [[nodiscard]] bool failed() const { return failed_; }

    [[nodiscard]] bool at_end() const { return position_ == data_.size(); }

    // Marks the content inconsistent; the first message is kept.
    void fail(std::string message);

    // Reads a component's name and fails unless it is `name`.
    void begin(std::string_view name);

    // A count of elements of at least `element_bytes` each: fails (and
    // gives 0) when the rest of the data cannot hold that many.
    std::size_t count(std::size_t element_bytes);

    // Why reading failed: the first inconsistency, or running out of data.
    [[nodiscard]] std::string message() const {
        return message_.empty() && failed_ ? std::string("the state ends early") : message_;
    }

private:
    std::span<const std::uint8_t> take(std::size_t count);

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    bool failed_ = false;
    std::string message_;
};

// Writes the whole VM state, component by component.
void write_state(Machine& machine, StateWriter& out);

// Reads what write_state wrote into a machine that is attached to its
// subsystems and initialised (init_game) on the same game files, replacing
// its state and rebuilding what the stream leaves out
// (docs/formats/save-format.md). Of the execution component only the
// counters are taken: a restore re-enters the game through its replay
// method. False, with the reason in `in`, if the data is inconsistent; the
// machine must then not run.
bool read_state(Machine& machine, StateReader& in);

// The hashes of the state write_state writes.
[[nodiscard]] StateHash state_hash(Machine& machine);

} // namespace sci::vm
