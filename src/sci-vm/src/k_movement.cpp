// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kmovement.cpp (kInitBresen, kDoBresen; SCI0)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "kernel_functions.hpp"
#include "machine.hpp"

#include <cstdlib>
#include <vector>

namespace sci::vm {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to kInitBresen.
Reg k_init_bresen(Machine& machine, std::span<Reg> args) {
    const SelectorCache& s = machine.selector();
    const Reg mover = args[0];
    const Reg client = machine.read_selector(mover, s.client);
    const auto step_factor = static_cast<std::int16_t>(args.size() >= 2 ? args[1].to_uint16() : 1);
    const std::int16_t mover_x = machine.read_selector_value(mover, s.x);
    const std::int16_t mover_y = machine.read_selector_value(mover, s.y);
    auto x_step = static_cast<std::int16_t>(machine.read_selector_value(client, s.x_step) * step_factor);
    const auto y_step = static_cast<std::int16_t>(machine.read_selector_value(client, s.y_step) * step_factor);
    auto step = static_cast<std::int16_t>(x_step < y_step ? y_step * 2 : x_step * 2);
    const auto delta_x = static_cast<std::int16_t>(mover_x - machine.read_selector_value(client, s.x));
    const auto delta_y = static_cast<std::int16_t>(mover_y - machine.read_selector_value(client, s.y));
    std::int16_t dx = 0;
    std::int16_t dy = 0;
    std::int16_t i1 = 0;
    std::int16_t i2 = 0;
    std::int16_t di = 0;
    std::int16_t incr = 0;
    std::int16_t x_axis = 0;
    const auto i16 = [](int value) { return static_cast<std::int16_t>(value); };
    while (true) {
        dx = x_step;
        dy = y_step;
        incr = 1;
        if (std::abs(delta_x) >= std::abs(delta_y)) {
            x_axis = 1;
            if (delta_x < 0) {
                dx = i16(-dx);
            }
            dy = delta_x != 0 ? i16(dx * delta_y / delta_x) : std::int16_t{0};
            i1 = i16(((dx * delta_y) - (dy * delta_x)) * 2);
            if (delta_y < 0) {
                incr = -1;
                i1 = i16(-i1);
            }
            i2 = i16(i1 - (delta_x * 2));
            di = i16(i1 - delta_x);
            if (delta_x < 0) {
                i1 = i16(-i1);
                i2 = i16(-i2);
                di = i16(-di);
            }
        } else {
            x_axis = 0;
            if (delta_y < 0) {
                dy = i16(-dy);
            }
            dx = delta_y != 0 ? i16(dy * delta_x / delta_y) : std::int16_t{0};
            i1 = i16(((dy * delta_x) - (dx * delta_y)) * 2);
            if (delta_x < 0) {
                incr = -1;
                i1 = i16(-i1);
            }
            i2 = i16(i1 - (delta_y * 2));
            di = i16(i1 - delta_y);
            if (delta_y < 0) {
                i1 = i16(-i1);
                i2 = i16(-i2);
                di = i16(-di);
            }
            break;
        }
        if (x_step <= y_step || x_step == 0 || y_step >= std::abs(dy + incr)) {
            break;
        }
        --step;
        if (step == 0) {
            machine.faults().raise("kInitBresen failed");
            return machine.acc();
        }
        --x_step;
    }
    machine.write_selector_value(mover, s.dx, dx);
    machine.write_selector_value(mover, s.dy, dy);
    machine.write_selector_value(mover, s.b_i1, i1);
    machine.write_selector_value(mover, s.b_i2, i2);
    machine.write_selector_value(mover, s.b_di, di);
    machine.write_selector_value(mover, s.b_incr, incr);
    machine.write_selector_value(mover, s.b_x_axis, x_axis);
    return machine.acc();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to kDoBresen.
Reg k_do_bresen(Machine& machine, std::span<Reg> args) {
    const SelectorCache& s = machine.selector();
    const Reg mover = args[0];
    const Reg client = machine.read_selector(mover, s.client);
    // SCI0 increments the move count.
    auto move_count = static_cast<std::int16_t>(machine.read_selector_value(mover, s.b_mov_cnt) + 1);
    const std::int16_t move_speed = machine.read_selector_value(client, s.move_speed);
    if (move_speed < move_count) {
        move_count = 0;
        std::int16_t client_x = machine.read_selector_value(client, s.x);
        std::int16_t client_y = machine.read_selector_value(client, s.y);
        const std::int16_t mover_x = machine.read_selector_value(mover, s.x);
        const std::int16_t mover_y = machine.read_selector_value(mover, s.y);
        const std::int16_t x_axis = machine.read_selector_value(mover, s.b_x_axis);
        const std::int16_t dx = machine.read_selector_value(mover, s.dx);
        const std::int16_t dy = machine.read_selector_value(mover, s.dy);
        const std::int16_t incr = machine.read_selector_value(mover, s.b_incr);
        std::int16_t i1 = machine.read_selector_value(mover, s.b_i1);
        std::int16_t i2 = machine.read_selector_value(mover, s.b_i2);
        std::int16_t di = machine.read_selector_value(mover, s.b_di);
        const std::int16_t original_i1 = i1;
        const std::int16_t original_i2 = i2;
        const std::int16_t original_di = di;

        // A backup of the client's variables, restored on a collision.
        Object* client_object = machine.segments().object(client);
        if (client_object == nullptr) {
            machine.faults().raise("kDoBresen: the client is not an object");
            return machine.acc();
        }
        std::vector<Reg> backup;
        backup.reserve(client_object->var_count());
        for (std::size_t i = 0; i < client_object->var_count(); ++i) {
            backup.push_back(client_object->variable(i));
        }
        // SCI0: completed when less than one step is left.
        const bool completed =
            x_axis != 0 ? std::abs(mover_x - client_x) < std::abs(dx) : std::abs(mover_y - client_y) < std::abs(dy);
        if (completed) {
            client_x = mover_x;
            client_y = mover_y;
        } else {
            client_x = static_cast<std::int16_t>(client_x + dx);
            client_y = static_cast<std::int16_t>(client_y + dy);
            if (di < 0) {
                di = static_cast<std::int16_t>(di + i1);
            } else {
                di = static_cast<std::int16_t>(di + i2);
                if (x_axis == 0) {
                    client_x = static_cast<std::int16_t>(client_x + incr);
                } else {
                    client_y = static_cast<std::int16_t>(client_y + incr);
                }
            }
        }
        machine.write_selector_value(client, s.x, client_x);
        machine.write_selector_value(client, s.y, client_y);

        // Collisions: client::cantBeHere, or client::canBeHere.
        bool collision = false;
        machine.set_acc(null_reg);
        if (s.cant_be_here != no_selector) {
            machine.invoke_selector(client, s.cant_be_here, {});
            collision = !machine.acc().is_null();
        } else {
            machine.invoke_selector(client, s.can_be_here, {});
            collision = machine.acc().is_null();
        }
        if (machine.faults().raised()) {
            return machine.acc();
        }
        if (collision) {
            client_object = machine.segments().object(client);
            if (client_object != nullptr) {
                for (std::size_t i = 0; i < backup.size() && i < client_object->var_count(); ++i) {
                    client_object->variable_ref(i) = backup[i];
                }
            }
            i1 = original_i1;
            i2 = original_i2;
            di = original_di;
            const auto signal = static_cast<std::uint16_t>(machine.read_selector_value(client, s.signal));
            machine.write_selector_value(client, s.signal, static_cast<int>(signal | 0x0400U)); // kSignalHitObstacle
        }
        machine.write_selector_value(mover, s.b_i1, i1);
        machine.write_selector_value(mover, s.b_i2, i2);
        machine.write_selector_value(mover, s.b_di, di);
    }
    machine.write_selector_value(mover, s.b_mov_cnt, move_count);
    return machine.acc();
}

} // namespace sci::vm
