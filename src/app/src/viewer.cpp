// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer.hpp"

#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/colors.hpp"
#include "sci/render/original.hpp"
#include "sci/vector/scene.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace sci::app {

namespace {

using platform::GraphicsMode;

constexpr std::size_t cached_backgrounds = 4;
constexpr int cel_milliseconds = 100; // Animation: ten cels a second.
constexpr std::uint8_t black = 0;
constexpr std::uint8_t white = 15;
constexpr int view_x = 160; // Where the views browser places a cel's foot, in 1x screen coordinates.
constexpr int view_y = 160;

std::vector<std::uint16_t> numbers_of(const resource::ResourceManager& manager, resource::ResourceType type) {
    std::vector<std::uint16_t> numbers;
    for (const resource::ResourceId id : manager.list()) {
        if (id.type == type) {
            numbers.push_back(id.number);
        }
    }
    std::ranges::stable_sort(numbers);
    return numbers;
}

Browser next_browser(Browser browser) {
    switch (browser) {
    case Browser::Pictures:
        return Browser::Views;
    case Browser::Views:
        return Browser::Fonts;
    case Browser::Fonts:
        return Browser::Pictures;
    }
    return Browser::Pictures;
}

std::string_view mode_name(GraphicsMode mode) {
    return mode == GraphicsMode::Hd ? "HD" : "original";
}

Rect unite(Rect a, Rect b) {
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x + a.width, b.x + b.width);
    const int bottom = std::max(a.y + a.height, b.y + b.height);
    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

} // namespace

Viewer::Viewer(const resource::ResourceManager& manager, platform::AnalysisCache& cache, core::Executor& executor,
               GraphicsMode mode, int scale)
    : manager_(&manager), analysis_(&cache), executor_(&executor), mode_(mode), chosen_scale_(scale),
      pictures_(numbers_of(manager, resource::ResourceType::Pic)),
      views_(numbers_of(manager, resource::ResourceType::View)),
      fonts_(numbers_of(manager, resource::ResourceType::Font)) {
    if (!fonts_.empty()) {
        if (const auto resource =
                manager.load(resource::ResourceId{.type = resource::ResourceType::Font, .number = fonts_.front()})) {
            if (auto font = text::decode_font(resource->data)) {
                status_font_ = std::move(*font);
            }
        }
    }
}

void Viewer::set_window_size(int width, int height) {
    fit_scale_ = std::clamp(std::min(width / screen_width, height / screen_height), 1, platform::max_scale);
    const int scale = chosen_scale_ == 0 ? fit_scale_ : std::min(chosen_scale_, fit_scale_);
    if (scale != scale_) {
        scale_ = scale;
        full_redraw_ = true;
    }
}

void Viewer::step(const std::vector<std::uint16_t>& numbers, std::size_t& index, int direction) {
    if (numbers.empty()) {
        return;
    }
    const auto count = numbers.size();
    index = direction > 0 ? (index + 1) % count : (index + count - 1) % count;
}

void Viewer::handle(Command command) {
    switch (command) {
    case Command::Next:
    case Command::Previous: {
        const int direction = command == Command::Next ? 1 : -1;
        if (browser_ == Browser::Pictures) {
            step(pictures_, picture_, direction);
        } else if (browser_ == Browser::Views) {
            step(views_, view_, direction);
            view_data_.reset();
            loop_ = 0;
            cel_ = 0;
        } else {
            step(fonts_, font_, direction);
            font_data_.reset();
        }
        break;
    }
    case Command::NextLoop:
    case Command::PreviousLoop:
        if (view_data_ && !view_data_->loops.empty()) {
            const std::size_t count = view_data_->loops.size();
            loop_ = command == Command::NextLoop ? (loop_ + 1) % count : (loop_ + count - 1) % count;
            cel_ = 0;
        }
        break;
    case Command::NextBrowser:
        browser_ = next_browser(browser_);
        break;
    case Command::ToggleMode:
        mode_ = mode_ == GraphicsMode::Hd ? GraphicsMode::Original : GraphicsMode::Hd;
        break;
    case Command::TogglePriority:
        priority_ = !priority_;
        break;
    case Command::ScaleUp:
        chosen_scale_ = std::min(scale_ + 1, fit_scale_);
        break;
    case Command::ScaleDown:
        chosen_scale_ = std::max(scale_ - 1, 1);
        break;
    case Command::ScaleAuto:
        chosen_scale_ = 0;
        break;
    case Command::ToggleAnimation:
        animate_ = !animate_;
        break;
    }
    const int scale = chosen_scale_ == 0 ? fit_scale_ : std::min(chosen_scale_, fit_scale_);
    scale_ = scale;
    full_redraw_ = true;
}

void Viewer::advance(int milliseconds) {
    if (browser_ != Browser::Views || !animate_ || !view_data_ || loop_ >= view_data_->loops.size()) {
        return;
    }
    const std::size_t cels = view_data_->loops[loop_].cels.size();
    if (cels < 2) {
        return;
    }
    cel_time_ += milliseconds;
    if (cel_time_ < cel_milliseconds) {
        return;
    }
    cel_time_ %= cel_milliseconds;
    cel_ = (cel_ + 1) % cels;
    if (!full_redraw_) {
        draw_view_cel(true);
        draw_status();
        mark({.x = 0, .y = 0, .width = screen_width * scale_, .height = menu_height * scale_});
    }
}

const std::vector<std::uint8_t>& Viewer::frame() {
    if (full_redraw_) {
        redraw();
    }
    return frame_;
}

std::vector<Rect> Viewer::take_dirty() {
    if (full_redraw_) {
        redraw();
    }
    std::vector<Rect> dirty;
    dirty.swap(dirty_);
    return dirty;
}

void Viewer::mark(Rect rect) {
    const Rect frame{.x = 0, .y = 0, .width = screen_width * scale_, .height = screen_height * scale_};
    const int left = std::max(rect.x, 0);
    const int top = std::max(rect.y, 0);
    const int right = std::min(rect.x + rect.width, frame.width);
    const int bottom = std::min(rect.y + rect.height, frame.height);
    if (right > left && bottom > top) {
        dirty_.push_back({.x = left, .y = top, .width = right - left, .height = bottom - top});
    }
}

void Viewer::redraw() {
    full_redraw_ = false;
    frame_.assign(
        static_cast<std::size_t>(screen_width * scale_) * static_cast<std::size_t>(screen_height * scale_) * 4, 0);
    message_.clear();
    cel_rect_.reset();
    draw_background();
    if (browser_ == Browser::Views) {
        draw_view_cel(false);
    } else if (browser_ == Browser::Fonts) {
        draw_font();
    }
    draw_status();
    dirty_.clear();
    mark({.x = 0, .y = 0, .width = screen_width * scale_, .height = screen_height * scale_});
}

void Viewer::fill(Rect rect, std::uint8_t color) {
    const render::Rgb8 rgb = render::ega_palette[color & 0x0FU];
    const int width = screen_width * scale_;
    for (int y = std::max(rect.y, 0); y < std::min(rect.y + rect.height, screen_height * scale_); ++y) {
        for (int x = std::max(rect.x, 0); x < std::min(rect.x + rect.width, width); ++x) {
            const std::size_t i =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4;
            frame_[i] = rgb.r;
            frame_[i + 1] = rgb.g;
            frame_[i + 2] = rgb.b;
            frame_[i + 3] = 0xFF;
        }
    }
}

// Draws 1x EGA colour indices at 1x screen position (x, y), scaled N x N.
void Viewer::blit_indices(const std::vector<std::uint8_t>& indices, int width, int height, int x, int y,
                          std::optional<std::uint8_t> transparent) {
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const std::uint8_t color = indices[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) +
                                               static_cast<std::size_t>(column)];
            if (transparent && color == *transparent) {
                continue;
            }
            fill({.x = (x + column) * scale_, .y = (y + row) * scale_, .width = scale_, .height = scale_}, color);
        }
    }
}

std::string Viewer::status_text() const {
    const std::string scale = std::format("N {}{}", scale_, chosen_scale_ == 0 ? " auto" : "");
    if (!message_.empty()) {
        return message_;
    }
    switch (browser_) {
    case Browser::Pictures:
        if (pictures_.empty()) {
            return "No pictures";
        }
        return std::format("Picture {}  {}  {}  {}", pictures_[picture_], mode_name(mode_),
                           priority_ ? "priority" : "visual", scale);
    case Browser::Views: {
        if (views_.empty()) {
            return "No views";
        }
        const std::size_t loops = view_data_ ? view_data_->loops.size() : 0;
        const std::size_t cels = view_data_ && loop_ < loops ? view_data_->loops[loop_].cels.size() : 0;
        return std::format("View {}  loop {}/{}  cel {}/{}  {}", views_[view_], loop_ + 1, loops, cel_ + 1, cels,
                           scale);
    }
    case Browser::Fonts:
        if (fonts_.empty()) {
            return "No fonts";
        }
        return std::format("Font {}  {}", fonts_[font_], scale);
    }
    return {};
}

// The menu bar row: the status in the game's first font, black on white.
void Viewer::draw_status() {
    fill({.x = 0, .y = 0, .width = screen_width * scale_, .height = menu_height * scale_}, white);
    const text::Font* font = status_font_ ? &*status_font_ : nullptr;
    if (font == nullptr) {
        return;
    }
    int x = 4;
    for (const char c : status_text()) {
        const auto code = static_cast<unsigned char>(c);
        if (code >= font->glyphs.size()) {
            continue;
        }
        const text::Glyph& glyph = font->glyphs[code];
        if (x + glyph.width > screen_width) {
            break;
        }
        const int height = std::min<int>(glyph.height, menu_height - 1);
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < int{glyph.width}; ++column) {
                if (glyph.pixels[(static_cast<std::size_t>(row) * glyph.width) + static_cast<std::size_t>(column)] !=
                    0) {
                    fill({.x = (x + column) * scale_, .y = (row + 1) * scale_, .width = scale_, .height = scale_},
                         black);
                }
            }
        }
        x += glyph.width;
    }
}

render::RgbaImage Viewer::hd_background(std::uint16_t number, const pic::PictureMaps& maps) {
    const int width = vector::picture_width * scale_;
    const int height = vector::picture_height * scale_;
    const auto plane = priority_ ? vector::Plane::Priority : vector::Plane::Visual;
    const auto scene = analysis_->scene(platform::SceneVariant{.picture = number, .plane = plane});
    if (!scene) {
        message_ = std::format("Picture {}: {}", number, scene.error().message);
        return render::indices_to_rgba(render::render_original(maps, scale_), width, height);
    }
    return priority_ ? render::indices_to_rgba(render::render_priority(**scene, scale_, *executor_), width, height)
                     : render::render_hd(**scene, scale_, *executor_);
}

const render::RgbaImage& Viewer::background() {
    const std::uint16_t number = pictures_.empty() ? 0 : pictures_[picture_];
    const auto same = [&](const CachedBackground& b) {
        return b.picture == number && b.mode == mode_ && b.priority == priority_ && b.scale == scale_;
    };
    if (const auto it = std::ranges::find_if(cache_, same); it != cache_.end()) {
        cache_.splice(cache_.begin(), cache_, it);
        return cache_.front().image;
    }
    CachedBackground entry{.picture = number, .mode = mode_, .priority = priority_, .scale = scale_, .image = {}};
    const int width = vector::picture_width * scale_;
    const int height = vector::picture_height * scale_;
    pic::PictureMaps maps;
    const pic::DrawOptions options;
    const auto resource = manager_->load(resource::ResourceId{.type = resource::ResourceType::Pic, .number = number});
    const auto commands = resource ? pic::parse_pic(resource->data)
                                   : core::Result<std::vector<pic::PicCommand>>(std::unexpected(resource.error()));
    const auto drawn = commands ? pic::draw_picture(*commands, maps, options)
                                : core::Result<pic::DrawResult>(std::unexpected(commands.error()));
    if (!drawn) {
        message_ = std::format("Picture {}: {}", number, drawn.error().message);
        entry.image = render::indices_to_rgba(
            std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0), width,
            height);
    } else if (mode_ == GraphicsMode::Original) {
        if (priority_) {
            std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    indices[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                            static_cast<std::size_t>(x)] =
                        maps.priority[pic::PictureMaps::index(x / scale_, (y / scale_) + pic::picture_top)];
                }
            }
            entry.image = render::indices_to_rgba(indices, width, height);
        } else {
            entry.image = render::indices_to_rgba(render::render_original(maps, scale_), width, height);
        }
    } else {
        entry.image = hd_background(number, maps);
    }
    cache_.push_front(std::move(entry));
    if (cache_.size() > cached_backgrounds) {
        cache_.pop_back();
    }
    return cache_.front().image;
}

void Viewer::draw_background() {
    if (browser_ == Browser::Fonts) {
        fill(
            {
                .x = 0,
                .y = menu_height * scale_,
                .width = screen_width * scale_,
                .height = (screen_height - menu_height) * scale_,
            },
            white);
        return;
    }
    const render::RgbaImage& image = background();
    const auto row_bytes = static_cast<std::size_t>(image.width) * 4;
    for (int y = 0; y < image.height; ++y) {
        std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * row_bytes),
                    row_bytes,
                    frame_.begin() +
                        static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y + (menu_height * scale_)) * row_bytes));
    }
}

// The current cel at the foot position. With erase_previous, the area of
// the previous cel is restored from the background first, and both areas
// are marked dirty.
void Viewer::draw_view_cel(bool erase_previous) {
    if (views_.empty()) {
        return;
    }
    if (!view_data_) {
        const auto resource =
            manager_->load(resource::ResourceId{.type = resource::ResourceType::View, .number = views_[view_]});
        auto decoded =
            resource ? view::decode_view(resource->data) : core::Result<view::View>(std::unexpected(resource.error()));
        if (!decoded) {
            message_ = std::format("View {}: {}", views_[view_], decoded.error().message);
            return;
        }
        view_data_ = std::move(*decoded);
    }
    if (loop_ >= view_data_->loops.size() || cel_ >= view_data_->loops[loop_].cels.size()) {
        return;
    }
    const view::Cel& cel = view_data_->loops[loop_].cels[cel_];
    const int x = view_x - (cel.width / 2) + cel.displace_x;
    const int y = view_y - cel.height + 1 + cel.displace_y;
    const Rect rect{.x = x * scale_, .y = y * scale_, .width = cel.width * scale_, .height = cel.height * scale_};
    const std::optional<Rect> previous = cel_rect_;
    if (erase_previous && previous) {
        // Restore the background under the previous cel.
        const Rect old = *previous;
        const render::RgbaImage& image = background();
        const int frame_width = screen_width * scale_;
        for (int fy = std::max(old.y, menu_height * scale_); fy < std::min(old.y + old.height, screen_height * scale_);
             ++fy) {
            for (int fx = std::max(old.x, 0); fx < std::min(old.x + old.width, frame_width); ++fx) {
                const std::size_t from =
                    ((static_cast<std::size_t>(fy - (menu_height * scale_)) * static_cast<std::size_t>(image.width)) +
                     static_cast<std::size_t>(fx)) *
                    4;
                const std::size_t to = ((static_cast<std::size_t>(fy) * static_cast<std::size_t>(frame_width)) +
                                        static_cast<std::size_t>(fx)) *
                                       4;
                std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(from), 4,
                            frame_.begin() + static_cast<std::ptrdiff_t>(to));
            }
        }
    }
    blit_indices(cel.pixels, cel.width, cel.height, x, y, cel.clear_key);
    if (erase_previous) {
        mark(previous ? unite(*previous, rect) : rect);
    }
    cel_rect_ = rect;
}

// All glyphs of the font, 16 per row, black on white.
void Viewer::draw_font() {
    if (fonts_.empty()) {
        return;
    }
    if (!font_data_) {
        const auto resource =
            manager_->load(resource::ResourceId{.type = resource::ResourceType::Font, .number = fonts_[font_]});
        auto decoded =
            resource ? text::decode_font(resource->data) : core::Result<text::Font>(std::unexpected(resource.error()));
        if (!decoded) {
            message_ = std::format("Font {}: {}", fonts_[font_], decoded.error().message);
            return;
        }
        font_data_ = std::move(*decoded);
    }
    const text::Font& font = *font_data_; // NOLINT(bugprone-unchecked-optional-access): set just above.
    int cell_width = 1;
    for (const text::Glyph& glyph : font.glyphs) {
        cell_width = std::max<int>(cell_width, glyph.width);
    }
    cell_width += 2;
    const int cell_height = font.line_height + 2;
    for (std::size_t code = 0; code < font.glyphs.size(); ++code) {
        const text::Glyph& glyph = font.glyphs[code];
        const int x = 4 + (static_cast<int>(code % 16) * cell_width);
        const int y = menu_height + 4 + (static_cast<int>(code / 16) * cell_height);
        if (y + glyph.height > screen_height) {
            break;
        }
        std::vector<std::uint8_t> indices(glyph.pixels.size());
        std::ranges::transform(glyph.pixels, indices.begin(), [](std::uint8_t p) { return p != 0 ? black : white; });
        blit_indices(indices, glyph.width, glyph.height, x, y, white);
    }
}

} // namespace sci::app
