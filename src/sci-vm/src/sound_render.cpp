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

core::Result<RenderedSound> render_sound(std::span<const std::uint8_t> sound, int number, audio::AdlibDriver& driver,
                                         const SoundRenderOptions& options) {
    Faults faults;
    Music music(faults);
    music.set_device(&driver);
    music.set_master_volume(options.master_volume);

    auto entry = std::make_unique<MusicEntry>();
    entry->resource_id = static_cast<std::uint16_t>(number);
    entry->loop = options.loop;
    entry->resource = SoundResource::parse(sound, number);
    if (!entry->resource) {
        return core::make_error(core::ErrorCode::InvalidData, std::format("sound.{:03} is too short", number));
    }
    // Cleared again before `music` goes.
    if (options.on_opl) {
        driver.set_write_hook(
            [&](std::uint16_t reg, std::uint8_t value) { options.on_opl(music.timer_calls(), reg, value); });
    }
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
        music.on_timer();
        ++calls;
        // The frames up to the next timer call, without drift.
        const std::uint64_t next = std::min<std::uint64_t>(
            calls * audio::Opl3::native_rate / audio::AdlibDriver::timer_frequency, options.max_frames);
        const auto count = static_cast<std::size_t>(next - frames);
        const std::size_t offset = rendered.samples.size();
        rendered.samples.resize(offset + (count * 2));
        driver.opl().generate(std::span(rendered.samples).subspan(offset));
        frames = next;
    }
    rendered.timer_calls = static_cast<std::uint32_t>(calls);
    // The player's last note-offs go to the driver before it is detached.
    music.clear_play_list();
    music.set_device(nullptr);
    driver.set_write_hook({});
    if (faults.raised()) {
        return core::make_error(core::ErrorCode::Unsupported, faults.message());
    }
    return rendered;
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

} // namespace sci::vm
