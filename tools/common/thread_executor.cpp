// SPDX-License-Identifier: GPL-3.0-or-later

#include "thread_executor.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace sci::tools {

ThreadExecutor::ThreadExecutor(std::size_t threads)
    : threads_(threads != 0 ? threads : std::max<std::size_t>(1, std::thread::hardware_concurrency())) {
}

void ThreadExecutor::run(std::size_t count, const std::function<void(std::size_t)>& task) {
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
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

} // namespace sci::tools
