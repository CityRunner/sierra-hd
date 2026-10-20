// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tools' implementation of the task-execution interface: tasks are
// spread over worker threads. Results do not depend on the number of
// threads, as tasks write disjoint outputs (spec section 4.3).
#pragma once

#include "sci/core/executor.hpp"

#include <cstddef>
#include <functional>

namespace sci::tools {

class ThreadExecutor final : public core::Executor {
public:
    // threads == 0: one per hardware thread.
    explicit ThreadExecutor(std::size_t threads = 0);

    void run(std::size_t count, const std::function<void(std::size_t)>& task) override;

private:
    std::size_t threads_;
};

} // namespace sci::tools
