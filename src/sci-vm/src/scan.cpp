// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vm/scan.hpp"

#include "bytecode.hpp"
#include "kernel.hpp"
#include "script.hpp"
#include "script_patcher.hpp"
#include "selectors.hpp"

#include <format>

namespace sci::vm {

namespace {

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t at) {
    return at + 1 < data.size() ? static_cast<std::uint16_t>(static_cast<unsigned>(data[at]) |
                                                             (static_cast<unsigned>(data[at + 1]) << 8U))
                                : std::uint16_t{0};
}

} // namespace

core::Result<std::vector<KernelReference>> scan_kernel_calls(const resource::ResourceManager& resources) {
    using resource::ResourceId;
    using resource::ResourceType;
    auto vocab = resources.load(ResourceId{.type = ResourceType::Vocab, .number = 997});
    if (!vocab) {
        return std::unexpected(vocab.error());
    }
    SelectorTable selectors;
    if (!selectors.load(vocab->data)) {
        return core::make_error(core::ErrorCode::InvalidData, "vocab.997: cannot read the selector names");
    }
    Kernel kernel;
    kernel.init(selectors.cache().cant_be_here != no_selector);
    ScriptPatcher patcher;
    std::string fault;
    patcher.attach([&](std::string_view name) { return selectors.find(name); },
                   [&](std::string message) { fault = std::move(message); });

    std::vector<KernelReference> references;
    for (const ResourceId id : resources.list()) {
        if (id.type != ResourceType::Script) {
            continue;
        }
        auto script = resources.load(id);
        if (!script) {
            return std::unexpected(script.error());
        }
        std::vector<std::uint8_t> data = std::move(script->data);
        patcher.process(id.number, data);
        if (!fault.empty()) {
            return core::make_error(core::ErrorCode::InvalidData, std::format("script.{:03}: {}", id.number, fault));
        }
        // SCI0 blocks: type and size (with the 4-byte header), until type 0.
        std::size_t block = 0;
        while (block + 4 <= data.size()) {
            const std::uint16_t type = read16(data, block);
            const std::uint16_t size = read16(data, block + 2);
            if (type == 0 || size < 4) {
                break;
            }
            if (type == static_cast<std::uint16_t>(ScriptBlock::Code)) {
                const std::span<const std::uint8_t> code(data);
                std::uint32_t pc = static_cast<std::uint32_t>(block) + 4;
                const std::uint32_t end = static_cast<std::uint32_t>(std::min(block + size, data.size()));
                while (pc < end) {
                    const Instruction instruction = read_instruction(code, pc);
                    if ((instruction.ext >> 1U) == op_callk) {
                        const int number = static_cast<std::uint16_t>(instruction.params[0]);
                        const bool known = static_cast<std::size_t>(number) < kernel.size();
                        references.push_back(KernelReference{
                            .name = known ? kernel.function(static_cast<std::size_t>(number)).name : "?",
                            .number = number,
                            .script = id.number,
                            .offset = pc,
                        });
                    }
                    pc += instruction.length;
                }
            }
            block += size;
        }
    }
    return references;
}

} // namespace sci::vm
