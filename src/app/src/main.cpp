// SPDX-License-Identifier: GPL-3.0-or-later
//
// Milestone 0 stub of the program: it links every library and reports them.

#include "sci/audio/component.hpp"
#include "sci/core/component.hpp"
#include "sci/pic/component.hpp"
#include "sci/render/component.hpp"
#include "sci/text/component.hpp"
#include "sci/vector/component.hpp"
#include "sci/view/component.hpp"
#include "sci/vm/component.hpp"

#include <array>
#include <print>
#include <span>
#include <string_view>

namespace {

void print_version() {
    std::println("sierra-hd {}", SCI_VERSION);
    const std::array components{
        sci::core::component_name(),   sci::pic::component_name(),  sci::vector::component_name(),
        sci::view::component_name(),   sci::text::component_name(), sci::audio::component_name(),
        sci::render::component_name(), sci::vm::component_name(),
    };
    for (const std::string_view name : components) {
        std::println("  {}", name);
    }
}

void print_usage() {
    std::println("usage: sierra-hd --version\n"
                 "The interpreter itself is not implemented yet (milestone 0).");
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 2 && std::string_view(args[1]) == "--version") {
        print_version();
        return 0;
    }
    print_usage();
    return args.size() == 1 ? 0 : 2;
}
