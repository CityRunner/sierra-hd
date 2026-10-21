// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replay files, the event queue, the virtual clock and the random source of
// the deterministic mode (docs/formats/replay-and-trace.md).

#include "events.hpp"
#include "gfx_rect.hpp"
#include "sci/vm/events.hpp"
#include "sci_test.hpp"
#include "timing.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

using sci::vm::Clock;
using sci::vm::EventQueue;
using sci::vm::InputEvent;
using sci::vm::parse_replay;
using sci::vm::RandomSource;
using sci::vm::Rect;

namespace {

std::vector<InputEvent> parsed(std::string_view text) {
    auto events = parse_replay(text);
    SCI_CHECK(events.has_value());
    return events ? *events : std::vector<InputEvent>{};
}

} // namespace

SCI_TEST(replay_needs_the_version_header) {
    SCI_CHECK(!parse_replay("").has_value());
    SCI_CHECK(!parse_replay("sierra-hd replay 2\n").has_value());
    SCI_CHECK(!parse_replay("10 key 13\n").has_value());
    SCI_CHECK(parse_replay("# comment\n\nsierra-hd replay 1\r\n").has_value());
}

SCI_TEST(replay_text_types_each_character_then_enter) {
    const auto events = parsed("sierra-hd replay 1\n600 text ab\n");
    SCI_CHECK(events.size() == 3);
    SCI_CHECK(events[0].tick == 600 && events[0].event.type == sci::vm::event_key_down);
    SCI_CHECK(events[0].event.character == 'a' && events[1].event.character == 'b');
    SCI_CHECK(events[2].event.character == sci::vm::key_enter);
}

SCI_TEST(replay_mouse_events_carry_their_position) {
    const auto events = parsed("sierra-hd replay 1\n5 move 50 60\n9 down 120 150 3\n10 up 120 150\n12 key 27\n");
    SCI_CHECK(events.size() == 4);
    SCI_CHECK(events[0].move_only && events[0].event.mouse == sci::vm::Point{.x = 50, .y = 60});
    SCI_CHECK(events[1].event.type == sci::vm::event_mouse_press && events[1].event.modifiers == 3);
    SCI_CHECK(events[2].event.type == sci::vm::event_mouse_release && events[2].event.modifiers == 0);
    SCI_CHECK(events[3].event.character == sci::vm::key_escape && events[3].event.mouse.x == 120);
}

SCI_TEST(replay_rejects_bad_lines) {
    SCI_CHECK(!parse_replay("sierra-hd replay 1\n-1 key 13\n").has_value());
    SCI_CHECK(!parse_replay("sierra-hd replay 1\n10 jump 13\n").has_value());
    SCI_CHECK(!parse_replay("sierra-hd replay 1\n10 down 5\n").has_value());
    SCI_CHECK(!parse_replay("sierra-hd replay 1\nten key 13\n").has_value());
}

SCI_TEST(queue_delivers_input_when_due_and_peeks_without_removing) {
    EventQueue queue;
    for (const InputEvent& event : parsed("sierra-hd replay 1\n10 key 97\n20 down 30 40 0\n")) {
        queue.schedule(event);
    }
    SCI_CHECK(queue.get(0x7FFF, 9).type == sci::vm::event_none);
    SCI_CHECK(queue.get(0x7FFF, 9).mouse == sci::vm::Point{.x = 160, .y = 100});
    const auto peeked = queue.get(sci::vm::event_key_down | sci::vm::event_peek, 10);
    SCI_CHECK(peeked.type == sci::vm::event_key_down && peeked.character == 'a');
    // A mouse mask leaves the key in the queue.
    SCI_CHECK(queue.get(sci::vm::event_mouse_press, 25).type == sci::vm::event_mouse_press);
    SCI_CHECK(queue.mouse() == sci::vm::Point{.x = 30, .y = 40});
    SCI_CHECK(queue.get(0x7FFF, 25).character == 'a');
    SCI_CHECK(queue.get(0x7FFF, 25).type == sci::vm::event_none);
}

SCI_TEST(pushed_keys_take_the_mouse_position_and_mouse_events_set_it) {
    EventQueue queue;
    queue.push(sci::vm::SciEvent{
        .type = sci::vm::event_key_down,
        .modifiers = sci::vm::modifier_none,
        .character = 'a',
        .mouse = {.x = 1, .y = 2},
    });
    queue.push(sci::vm::SciEvent{
        .type = sci::vm::event_mouse_press,
        .modifiers = sci::vm::modifier_shift,
        .character = 0,
        .mouse = {.x = 30, .y = 40},
    });
    SCI_CHECK(queue.mouse() == sci::vm::Point{.x = 30, .y = 40});
    queue.push(sci::vm::SciEvent{
        .type = sci::vm::event_key_up,
        .modifiers = sci::vm::modifier_none,
        .character = 'a',
        .mouse = {.x = 5, .y = 6},
    });
    queue.push(sci::vm::SciEvent{
        .type = sci::vm::event_mouse_release,
        .modifiers = sci::vm::modifier_none,
        .character = 0,
        .mouse = {.x = 50, .y = 60},
    });
    SCI_CHECK(queue.mouse() == sci::vm::Point{.x = 50, .y = 60});
    // The key down came before any mouse event: the start position.
    const auto key_down = queue.get(0x7FFF, 0);
    SCI_CHECK(key_down.type == sci::vm::event_key_down && key_down.character == 'a');
    SCI_CHECK(key_down.mouse == sci::vm::Point{.x = 160, .y = 100});
    const auto press = queue.get(0x7FFF, 0);
    SCI_CHECK(press.type == sci::vm::event_mouse_press && press.modifiers == sci::vm::modifier_shift);
    SCI_CHECK(press.mouse == sci::vm::Point{.x = 30, .y = 40});
    // The key up came after the press: its position, not the key's own.
    const auto key_up = queue.get(0x7FFF, 0);
    SCI_CHECK(key_up.type == sci::vm::event_key_up && key_up.mouse == sci::vm::Point{.x = 30, .y = 40});
    const auto release = queue.get(0x7FFF, 0);
    SCI_CHECK(release.type == sci::vm::event_mouse_release && release.mouse == sci::vm::Point{.x = 50, .y = 60});
}

SCI_TEST(a_quit_request_comes_with_the_next_poll) {
    EventQueue queue;
    queue.request_quit();
    SCI_CHECK(queue.get(0x7FFF, 0).type == sci::vm::event_quit);
}

SCI_TEST(wait_sleeps_the_rest_of_the_ticks_since_the_last_wait) {
    Clock clock;
    clock.reset_wait();
    clock.advance(5);
    // 6 ticks are 100 ms: 95 ms are left to sleep.
    SCI_CHECK(clock.wait(6) == 6);
    SCI_CHECK(clock.millis() == 100);
    clock.advance(200);
    SCI_CHECK(clock.wait(1) == 12); // No sleep; 200 ms passed.
    SCI_CHECK(clock.millis() == 300);
}

SCI_TEST(an_elapsed_time_read_right_after_another_lets_1_ms_pass) {
    Clock clock;
    // The first read never advances, whatever its call number.
    clock.read_elapsed(5);
    SCI_CHECK(clock.millis() == 0);
    // Kernel calls 6 and 7 read again with nothing between.
    clock.read_elapsed(6);
    SCI_CHECK(clock.millis() == 1);
    clock.read_elapsed(7);
    SCI_CHECK(clock.millis() == 2);
    // Call 8 was something else: the read in call 9 is not a busy wait.
    clock.read_elapsed(9);
    SCI_CHECK(clock.millis() == 2);
    clock.read_elapsed(10);
    SCI_CHECK(clock.millis() == 3);
    // The same call again is no later read either.
    clock.read_elapsed(10);
    SCI_CHECK(clock.millis() == 3);

    Clock first;
    first.read_elapsed(1);
    SCI_CHECK(first.millis() == 0);
    first.read_elapsed(3);
    SCI_CHECK(first.millis() == 0);
    first.read_elapsed(4);
    SCI_CHECK(first.millis() == 1);
}

SCI_TEST(throttler_sleeps_only_when_triggered) {
    Clock clock;
    clock.speed_throttler(30);
    SCI_CHECK(clock.millis() == 0);
    clock.set_throttle_trigger(true);
    clock.speed_throttler(30);
    SCI_CHECK(clock.millis() == 30);
    SCI_CHECK(!clock.throttle_trigger());
}

SCI_TEST(music_timer_runs_once_per_tempo_and_sleep_polls_first) {
    Clock clock;
    int timer_calls = 0;
    int polls = 0;
    clock.set_music_timer([&] { ++timer_calls; }, 4000);
    clock.set_sleep_poll([&] {
        ++polls;
        SCI_CHECK(timer_calls == 0); // The poll comes before time passes.
    });
    clock.sleep(17);
    SCI_CHECK(polls == 1 && timer_calls == 4);
    clock.sleep(0);
    SCI_CHECK(polls == 1);
    SCI_CHECK(clock.ticks() == 1);
}

SCI_TEST(random_source_matches_the_reference_trace) {
    // Seed 0 becomes 1; the reference's first kRandom calls in the game are
    // Random(0,599) = 483 and Random(0,999) = 883 (build/traces, seed 0).
    RandomSource random;
    random.set_seed(0);
    SCI_CHECK(random.seed() == 1);
    SCI_CHECK(random.next(599) == 483);
    SCI_CHECK(random.next(999) == 883);
}

SCI_TEST(rect_clip_and_extend_follow_common_rect) {
    Rect rect = Rect::from(-5, 5, 400, 300);
    rect.clip(Rect::from(0, 0, 320, 190));
    SCI_CHECK(rect == Rect::from(0, 5, 320, 190));
    Rect outside = Rect::from(330, 10, 340, 20);
    outside.clip(Rect::from(0, 0, 320, 190));
    SCI_CHECK(outside.is_empty() && outside.left == 320 && outside.right == 320);
    Rect extended = Rect::from(10, 10, 20, 20);
    extended.extend(Rect::from(5, 15, 12, 30));
    SCI_CHECK(extended == Rect::from(5, 10, 20, 30));
    SCI_CHECK(!Rect::from(0, 0, 1, 1).contains(1, 0));
}
