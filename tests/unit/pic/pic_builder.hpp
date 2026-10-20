// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds synthetic SCI0 picture data for the sci-pic tests.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace sci::test {

class PicBuilder {
public:
    PicBuilder& bytes(std::initializer_list<std::uint8_t> values) {
        data_.insert(data_.end(), values);
        return *this;
    }

    // An absolute coordinate: x bits 8-11 and y bits 8-11 in the prefix.
    PicBuilder& absolute(int x, int y) {
        const auto ux = static_cast<unsigned>(x);
        const auto uy = static_cast<unsigned>(y);
        const auto prefix = static_cast<std::uint8_t>(((ux >> 4U) & 0xF0U) | ((uy >> 8U) & 0x0FU));
        return bytes({prefix, static_cast<std::uint8_t>(ux & 0xFFU), static_cast<std::uint8_t>(uy & 0xFFU)});
    }

    PicBuilder& color(std::uint8_t index) { return bytes({0xF0, index}); }

    PicBuilder& no_visual() { return bytes({0xF1}); }

    PicBuilder& priority(std::uint8_t value) { return bytes({0xF2, value}); }

    PicBuilder& no_priority() { return bytes({0xF3}); }

    PicBuilder& control(std::uint8_t value) { return bytes({0xFB, value}); }

    // A long line (absolute end points) from (x0, y0) to (x1, y1).
    PicBuilder& line(int x0, int y0, int x1, int y1) {
        bytes({0xF6});
        absolute(x0, y0);
        return absolute(x1, y1);
    }

    PicBuilder& fill(int x, int y) {
        bytes({0xF8});
        return absolute(x, y);
    }

    PicBuilder& pattern_code(std::uint8_t code) { return bytes({0xF9, code}); }

    // An absolute pattern point (without texture byte).
    PicBuilder& pattern(int x, int y) {
        bytes({0xFA});
        return absolute(x, y);
    }

    // A closed rectangle outline with corners (x0, y0) and (x1, y1).
    PicBuilder& rectangle(int x0, int y0, int x1, int y1) {
        line(x0, y0, x1, y0);
        line(x1, y0, x1, y1);
        line(x1, y1, x0, y1);
        return line(x0, y1, x0, y0);
    }

    [[nodiscard]] std::vector<std::uint8_t> end() {
        bytes({0xFF});
        return data_;
    }

private:
    std::vector<std::uint8_t> data_;
};

} // namespace sci::test
