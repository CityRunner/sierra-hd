// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vm/sound_render.hpp"

#include "faults.hpp"
#include "music.hpp"
#include "sci/audio/opl3.hpp"
#include "sci/resource/resource_manager.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace sci::vm {

namespace {

// Plays `sound` on `device`, calling `generate` after each timer call with
// the samples up to the next one at `rate`. `attach` is called with the
// player once the device is attached, `detach` once it is detached.
template <typename Generate, typename Attach, typename Detach>
core::Result<RenderedSound> render_on(std::span<const std::uint8_t> sound, int number, audio::MidiDevice& device,
                                      std::uint32_t rate, const Generate& generate, const Attach& attach,
                                      const Detach& detach, const SoundRenderOptions& options) {
    Faults faults;
    Music music(faults);
    music.set_device(&device);
    attach(music);
    music.set_master_volume(options.master_volume);

    auto entry = std::make_unique<MusicEntry>();
    entry->resource_id = static_cast<std::uint16_t>(number);
    entry->loop = options.loop;
    entry->resource = SoundResource::parse(sound, number);
    if (!entry->resource) {
        return core::make_error(core::ErrorCode::InvalidData, std::format("sound.{:03} is too short", number));
    }
    // Cleared again before `music` goes.
    music.sound_init(*entry);
    MusicEntry& song = music.add(std::move(entry));
    music.sound_play(song);

    RenderedSound rendered;
    rendered.samples.reserve(static_cast<std::size_t>(options.max_frames) * 2);
    std::uint64_t frames = 0;
    std::uint64_t calls = 0;
    while (frames < options.max_frames && !faults.raised()) {
        if (!song.parser || !song.parser->is_tracking()) {
            rendered.ended = true;
            break;
        }
        const std::uint16_t cue = song.data_inc;
        music.on_timer();
        ++calls;
        if (song.signal != 0 || song.data_inc != cue) {
            rendered.events.push_back({.timer_call = static_cast<std::uint32_t>(calls),
                                       .signal = song.signal,
                                       .cue = song.data_inc});
            song.signal = 0;
        }
        // The frames up to the next timer call, without drift.
        const std::uint64_t next = std::min<std::uint64_t>(
            calls * rate / audio::AdlibDriver::timer_frequency, options.max_frames);
        const auto count = static_cast<std::size_t>(next - frames);
        const std::size_t offset = rendered.samples.size();
        rendered.samples.resize(offset + (count * 2));
        generate(std::span(rendered.samples).subspan(offset));
        frames = next;
    }
    rendered.timer_calls = static_cast<std::uint32_t>(calls);
    // The player's last note-offs go to the driver before it is detached.
    music.clear_play_list();
    music.set_device(nullptr);
    detach();
    if (faults.raised()) {
        return core::make_error(core::ErrorCode::Unsupported, faults.message());
    }
    return rendered;
}

} // namespace

// Both devices run the music timer at the same rate.
static_assert(audio::AdlibDriver::timer_frequency == audio::Mt32Driver::timer_frequency);

core::Result<RenderedSound> render_sound(std::span<const std::uint8_t> sound, int number, audio::AdlibDriver& driver,
                                         const SoundRenderOptions& options) {
    return render_on(
        sound, number, driver, audio::Opl3::native_rate,
        [&](std::span<std::int16_t> samples) { driver.opl().generate(samples); },
        [&](const Music& music) {
            if (options.on_opl) {
                driver.set_write_hook(
                    [&](std::uint16_t reg, std::uint8_t value) { options.on_opl(music.timer_calls(), reg, value); });
            }
        },
        [&] { driver.set_write_hook({}); }, options);
}

core::Result<RenderedSound> render_sound(std::span<const std::uint8_t> sound, int number, audio::Mt32Driver& driver,
                                         const SoundRenderOptions& options) {
    return render_on(
        sound, number, driver, audio::Mt32::native_rate,
        [&](std::span<std::int16_t> samples) { driver.synth().render(samples); }, [](const Music& /*music*/) {},
        [] {}, options);
}

core::Result<RenderedSound> render_sound(const resource::ResourceManager& resources, std::uint16_t number,
                                         const SoundRenderOptions& options) {
    auto patches = audio::load_adlib_patches(resources);
    if (!patches) {
        return std::unexpected(std::move(patches.error()));
    }
    auto sound = resources.load({.type = resource::ResourceType::Sound, .number = number});
    if (!sound) {
        return std::unexpected(std::move(sound.error()));
    }
    audio::AdlibDriver::WriteHook hook;
    if (options.on_opl) {
        hook = [&](std::uint16_t reg, std::uint8_t value) { options.on_opl(0, reg, value); };
    }
    const auto driver = std::make_unique<audio::AdlibDriver>(std::move(*patches), std::move(hook));
    return render_sound(sound->data, number, *driver, options);
}

core::Result<RenderedSound> render_sound_mt32(const resource::ResourceManager& resources, std::uint16_t number,
                                              std::span<const std::uint8_t> control_rom,
                                              std::span<const std::uint8_t> pcm_rom,
                                              const SoundRenderOptions& options) {
    auto patch = audio::load_mt32_patch(resources);
    if (!patch) {
        return std::unexpected(std::move(patch.error()));
    }
    auto sound = resources.load({.type = resource::ResourceType::Sound, .number = number});
    if (!sound) {
        return std::unexpected(std::move(sound.error()));
    }
    auto synth = audio::Mt32::open(control_rom, pcm_rom);
    if (!synth) {
        return std::unexpected(std::move(synth.error()));
    }
    const auto driver = std::make_unique<audio::Mt32Driver>(std::move(*synth), *patch);
    return render_sound(sound->data, number, *driver, options);
}

} // namespace sci::vm
