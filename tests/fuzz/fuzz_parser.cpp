// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads an arbitrary vocabulary, then tokenizes, parses and matches a Said
// spec the way kParse and kSaid do. The input is three blobs (vocab.000,
// 901 and 900), each preceded by a little-endian 16-bit length, then the
// sentence up to a NUL, then the Said spec.

#include "sci/core/assert.hpp"
#include "sci/text/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

// The next length-prefixed blob, or what is left if the input is short.
std::span<const std::uint8_t> next_blob(std::span<const std::uint8_t>& input) {
    if (input.size() < 2) {
        input = {};
        return {};
    }
    const std::size_t size = std::min<std::size_t>(input[0] | (std::size_t{input[1]} << 8U), input.size() - 2);
    const std::span<const std::uint8_t> blob = input.subspan(2, size);
    input = input.subspan(2 + size);
    return blob;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::span<const std::uint8_t> input(data, size);
    const std::span<const std::uint8_t> words = next_blob(input);
    const std::span<const std::uint8_t> suffixes = next_blob(input);
    const std::span<const std::uint8_t> branches = next_blob(input);
    std::size_t sentence_end = 0;
    while (sentence_end < input.size() && input[sentence_end] != 0) {
        ++sentence_end;
    }
    const std::string sentence(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(sentence_end));
    const std::span<const std::uint8_t> spec = input.subspan(std::min(sentence_end + 1, input.size()));

    sci::text::Vocabulary vocabulary(words, suffixes, branches);
    std::string error;
    std::optional<sci::text::Sentence> tokens = vocabulary.tokenize(sentence, error);
    if (!tokens) {
        SCI_ASSERT(!error.empty());
        return 0;
    }
    for (const sci::text::WordMeanings& meanings : *tokens) {
        SCI_ASSERT(!meanings.empty());
    }
    vocabulary.replace_pronouns(*tokens);
    if (!tokens->empty() && vocabulary.parse(*tokens)) {
        SCI_ASSERT(vocabulary.nodes().size() == sci::text::tree_nodes);
        vocabulary.store_pronoun_reference();
        const int result = sci::text::said(spec, vocabulary.nodes());
        SCI_ASSERT(result == sci::text::said_full_match || result == sci::text::said_partial_match ||
                   result == sci::text::said_no_match);
    }
    return 0;
}
