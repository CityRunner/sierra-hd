// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/thread_executor.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/fp_profile.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace sci::platform {

ThreadExecutor::ThreadExecutor(std::size_t threads)
    : threads_(threads != 0 ? threads : std::max<std::size_t>(1, std::thread::hardware_concurrency())) {
}

void ThreadExecutor::run(std::size_t count, const std::function<void(std::size_t)>& task) {
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        // Spec section 5.3: every rendering and analysis thread checks the
        // floating-point profile when it starts.
        SCI_ASSERT(core::check_fp_profile().has_value());
        for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
            task(i);
        }
    };
    std::vector<std::jthread> workers;
    const std::size_t helpers = std::min(threads_, count) > 0 ? std::min(threads_, count) - 1 : 0;
    workers.reserve(helpers);
    for (std::size_t i = 0; i < helpers; ++i) {
        workers.emplace_back(work);
    }
    work();
}

} // namespace sci::platform
