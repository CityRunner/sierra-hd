// SPDX-License-Identifier: GPL-3.0-or-later
//
// The code that runs on worker threads, on worker threads: the app's
// ThreadExecutor and BackgroundTask, and the rendering and checks that
// libraries spread over an executor (spec sections 4.2 and 4.3). Their
// results must equal the serial ones. These are the tests with the label "threads", which the
// work-in-progress CI runs under TSan (docs/ASSUMPTIONS.md, "CI cost
// policy").

#include "pic_builder.hpp"
#include "sci/core/executor.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/platform/background_task.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/render/integrity.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci_test.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using sci::test::PicBuilder;

namespace {

// Outlines, a curve, dithered fills, a leak and priority areas.
std::vector<std::uint8_t> picture() {
    PicBuilder pic;
    pic.color(0).priority(4).rectangle(10, 10, 300, 180);
    pic.line(10, 120, 300, 120).line(150, 10, 150, 119).line(40, 60, 149, 60);
    pic.line(160, 60, 299, 61);
    pic.color(12).line(100, 80, 120, 90).line(120, 90, 110, 110).line(110, 110, 90, 105).line(90, 105, 100, 80);
    pic.color(17).fill(105, 95);
    pic.color(3).priority(9).fill(50, 30);
    pic.color(25).fill(200, 30).color(6).fill(200, 100);
    pic.color(2).priority(11).fill(50, 150);
    return pic.end();
}

} // namespace

SCI_TEST(thread_executor_runs_every_task_once) {
    for (const std::size_t threads : {std::size_t{1}, std::size_t{4}, std::size_t{0}}) {
        sci::platform::ThreadExecutor executor(threads);
        constexpr std::size_t count = 1000;
        std::vector<std::atomic<int>> calls(count);
        std::mutex mutex;
        std::set<std::thread::id> ids;
        executor.run(count, [&](std::size_t i) {
            calls[i].fetch_add(1, std::memory_order_relaxed);
            const std::scoped_lock lock(mutex);
            ids.insert(std::this_thread::get_id());
        });
        bool once = true;
        for (const auto& call : calls) {
            once = once && call.load() == 1;
        }
        SCI_CHECK(once);
        SCI_CHECK(!ids.empty());
        executor.run(0, [&](std::size_t) { once = false; });
        SCI_CHECK(once);
    }
}

SCI_TEST(background_task_hands_its_result_to_the_owner) {
    sci::platform::BackgroundTask<std::vector<int>> task;
    SCI_CHECK(!task.busy() && !task.take().has_value());
    // The owner polls while the work runs; the work's input is its own copy.
    std::vector<int> input{1, 2, 3};
    std::atomic<bool> go{false};
    task.start([input, &go] {
        while (!go.load()) {
            std::this_thread::yield();
        }
        std::vector<int> doubled;
        for (const int value : input) {
            doubled.push_back(value * 2);
        }
        return doubled;
    });
    input.clear();
    SCI_CHECK(task.busy() && !task.take().has_value());
    go.store(true);
    std::optional<std::vector<int>> result;
    while (!(result = task.take())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SCI_CHECK(*result == (std::vector<int>{2, 4, 6}));
    SCI_CHECK(!task.busy());
    // Free again; a task still running when it goes is waited for.
    task.start([] { return std::vector<int>{7}; });
    SCI_CHECK(task.busy());
}

SCI_TEST(rendering_on_threads_equals_rendering_in_order) {
    const auto commands = sci::pic::parse_pic(picture());
    SCI_CHECK(commands.has_value());
    if (!commands) {
        return;
    }
    sci::pic::PictureMaps maps;
    sci::pic::DrawTrace trace;
    SCI_CHECK(sci::pic::draw_picture(*commands, maps, {}, &trace).has_value());
    const auto visual = sci::vector::build_scene(maps, trace, sci::vector::Plane::Visual);
    const auto priority = sci::vector::build_scene(maps, trace, sci::vector::Plane::Priority);
    sci::core::SerialExecutor serial;
    sci::platform::ThreadExecutor threads(4);
    for (const int scale : {1, 3}) {
        SCI_CHECK(sci::render::render_hd(visual.scene, scale, threads).pixels ==
                  sci::render::render_hd(visual.scene, scale, serial).pixels);
    }
    SCI_CHECK(sci::render::render_priority(priority.scene, 2, threads) ==
              sci::render::render_priority(priority.scene, 2, serial));
    const auto on_threads = sci::render::check_integrity(visual.scene, maps, trace, threads);
    const auto in_order = sci::render::check_integrity(visual.scene, maps, trace, serial);
    SCI_CHECK(on_threads.contacts == in_order.contacts && on_threads.joints == in_order.joints &&
              on_threads.line_groups == in_order.line_groups && on_threads.overshoots == in_order.overshoots &&
              on_threads.depth_violations == in_order.depth_violations && on_threads.breaks == in_order.breaks &&
              on_threads.cases.size() == in_order.cases.size());
}
