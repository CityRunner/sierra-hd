// SPDX-License-Identifier: GPL-3.0-or-later
//
// One piece of work at a time on a thread of its own, started and collected
// by the owner's thread, which never waits for it (spec section 4.3: the
// main thread keeps the interpreter, the music and the frames going while
// an HD picture is made; docs/ASSUMPTIONS.md, "Milestone 7: gaps in the
// music"). The work shares nothing with the owner while it runs but what it
// was given; the result is handed over through a std::future.
#pragma once

#include "sci/core/assert.hpp"
#include "sci/core/fp_profile.hpp"

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <utility>

namespace sci::platform {

template <typename T>
class BackgroundTask {
public:
    BackgroundTask() = default;
    // Waits for work still running.
    ~BackgroundTask() = default;
    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;
    BackgroundTask(BackgroundTask&&) = delete;
    BackgroundTask& operator=(BackgroundTask&&) = delete;

    // Started and not yet taken.
    [[nodiscard]] bool busy() const { return result_.valid(); }

    void start(std::function<T()> work) {
        SCI_ASSERT(!busy());
        result_ = std::async(std::launch::async, [work = std::move(work)] {
            // Spec section 5.3: every rendering and analysis thread checks
            // the floating-point profile when it starts.
            SCI_ASSERT(core::check_fp_profile().has_value());
            return work();
        });
    }

    // The result once the work is done (the task is then free again);
    // nothing while it runs or when none was started.
    [[nodiscard]] std::optional<T> take() {
        if (!busy() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return std::nullopt;
        }
        return result_.get();
    }

private:
    std::future<T> result_;
};

} // namespace sci::platform
