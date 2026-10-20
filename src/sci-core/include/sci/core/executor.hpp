// SPDX-License-Identifier: GPL-3.0-or-later
//
// The task-execution interface through which libraries parallelise work
// (spec section 4.2). app supplies an implementation with worker threads;
// libraries never create threads themselves. Results must not depend on the
// implementation: tasks write disjoint outputs and are combined in index
// order.
#pragma once

#include <cstddef>
#include <functional>

namespace sci::core {

class Executor {
public:
    Executor() = default;
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&) = delete;
    Executor& operator=(Executor&&) = delete;
    virtual ~Executor() = default;

    // Calls task(i) once for every i in [0, count), in any order and on any
    // threads, and returns when all calls have returned.
    virtual void run(std::size_t count, const std::function<void(std::size_t)>& task) = 0;
};

// Runs every task on the calling thread, in index order.
class SerialExecutor final : public Executor {
public:
    void run(std::size_t count, const std::function<void(std::size_t)>& task) override {
        for (std::size_t i = 0; i < count; ++i) {
            task(i);
        }
    }
};

} // namespace sci::core
