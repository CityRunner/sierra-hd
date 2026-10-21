// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/parser/vocabulary.cpp (SCI0: loading, lookupWord,
//       tokenizeString, synonymizeTokens, pronouns)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "parser_internal.hpp"
#include "sci/text/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace sci::text {

namespace {

constexpr std::size_t max_word_length = 256; // VOCAB_MAX_WORDLENGTH

std::uint8_t byte_at(std::span<const std::uint8_t> data, std::size_t at) {
    return at < data.size() ? data[at] : std::uint8_t{0};
}

std::uint16_t u16le(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(byte_at(data, at)) |
                                      (static_cast<unsigned>(byte_at(data, at + 1)) << 8U));
}

std::uint16_t u16be(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(byte_at(data, at)) << 8U) |
                                      static_cast<unsigned>(byte_at(data, at + 1)));
}

// The C string at `at`, and whether it ends inside the data.
std::pair<std::string, bool> c_string(std::span<const std::uint8_t> data, std::size_t at) {
    std::string text;
    for (std::size_t i = at; i < data.size(); ++i) {
        if (data[i] == 0) {
            return {text, true};
        }
        text += static_cast<char>(data[i]);
    }
    return {text, false};
}

// lowerCaseMap: the ASCII capitals, and the accented capitals of code page
// 437.
constexpr std::array<std::uint8_t, 256> lower_case_map = [] {
    std::array<std::uint8_t, 256> map{};
    for (std::size_t i = 0; i < map.size(); ++i) {
        map[i] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t c = 'A'; c <= 'Z'; ++c) {
        map[c] = static_cast<std::uint8_t>(c - 'A' + 'a');
    }
    map[0x80] = 0x87;
    map[0x8e] = 0x84;
    map[0x8f] = 0x86;
    map[0x90] = 0x82;
    map[0x92] = 0x91;
    map[0x99] = 0x94;
    map[0x9a] = 0x81;
    map[0xa5] = 0xa4;
    return map;
}();

// Common::isAlnum: ASCII only.
bool is_ascii_alnum(std::uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

} // namespace

Vocabulary::Vocabulary(std::optional<std::span<const std::uint8_t>> words,
                       std::optional<std::span<const std::uint8_t>> suffixes,
                       std::optional<std::span<const std::uint8_t>> branches, ParserOptions options)
    : options_(options), nodes_(tree_nodes) {
    nodes_[0] = ParseNode{.type = NodeType::Leaf, .value = 0, .left = -1, .right = -1};
    if (!words) {
        warnings_.emplace_back("Could not find a main vocabulary");
        return;
    }
    if (!load_words(*words)) {
        return; // Not usable: no suffixes, no rules.
    }
    if (suffixes) {
        load_suffixes(*suffixes);
    }
    if (branches && load_branches(*branches)) {
        build_gnf();
    }
}

Vocabulary::~Vocabulary() = default;
Vocabulary::Vocabulary(Vocabulary&&) noexcept = default;
Vocabulary& Vocabulary::operator=(Vocabulary&&) noexcept = default;

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to Vocabulary::loadParserWords.
bool Vocabulary::load_words(std::span<const std::uint8_t> data) {
    if (data.size() < 26 * 2) {
        warnings_.emplace_back("Invalid main vocabulary encountered: Much too small");
        return false;
    }
    std::size_t letter = 0;
    while (letter < 26 && u16le(data, letter * 2) == 0) {
        ++letter;
    }
    const bool sci1_format = letter == 26;
    if (sci1_format) {
        warnings_.emplace_back("SCI0: Found SCI01 vocabulary in disguise");
    }
    // vocab.000 starts with 26 word pointers (SCI01: 255), which are not used.
    std::size_t seeker = sci1_format ? 255 * 2 : 26 * 2;
    if (data.size() < seeker) {
        warnings_.emplace_back("Invalid main vocabulary encountered: Too small");
        return false;
    }
    words_.clear();
    // Each word reuses the start of the previous one in this buffer.
    std::array<char, max_word_length> buffer{};
    while (seeker < data.size()) {
        std::size_t position = data[seeker++];
        if (sci1_format) {
            std::uint8_t c = 1;
            while (seeker < data.size() && position < max_word_length - 1 && c != 0) {
                c = data[seeker++];
                buffer[position++] = static_cast<char>(c);
            }
            if (seeker == data.size()) {
                warnings_.emplace_back("SCI1: Vocabulary not usable, disabling");
                words_.clear();
                return false;
            }
        } else {
            std::uint8_t c = 0;
            do {
                if (seeker == data.size() || position >= max_word_length - 1) {
                    warnings_.emplace_back("SCI0: Vocabulary not usable, disabling");
                    return false;
                }
                c = data[seeker++];
                buffer[position++] = static_cast<char>(c & 0x7FU); // 0x80 ends the word.
            } while (c < 0x80);
        }
        if (seeker == data.size()) {
            warnings_.emplace_back("Vocabulary not usable, disabling");
            return false;
        }
        buffer[position] = 0;
        const auto end = std::ranges::find(buffer, '\0');
        const std::string word(buffer.begin(), end);
        const std::uint8_t c = byte_at(data, seeker + 1);
        const ResultWord meaning{
            .word_class = static_cast<int>((static_cast<unsigned>(byte_at(data, seeker)) << 4U) | ((c & 0xF0U) >> 4U)),
            .group = static_cast<int>(static_cast<unsigned>(byte_at(data, seeker + 2)) | ((c & 0x0FU) << 8U)),
        };
        WordMeanings& meanings = words_[word];
        if (options_.sci0) {
            meanings.clear(); // SCI0 keeps only the last meaning.
        }
        meanings.push_back(meaning);
        seeker += 3;
    }
    return true;
}

void Vocabulary::load_suffixes(std::span<const std::uint8_t> data) {
    std::size_t seeker = 1;
    while (seeker + 1 < data.size() && data[seeker + 1] != 0xFF) {
        Suffix suffix;
        auto [alt, alt_terminated] = c_string(data, seeker);
        if (!alt_terminated) {
            warnings_.push_back(
                std::format("Vocabulary alt appears truncated for suffix {} at {}", suffixes_.size(), seeker));
            return;
        }
        seeker += alt.size() + 1;
        suffix.alt_suffix = std::move(alt);
        suffix.result_class = static_cast<std::int16_t>(u16be(data, seeker));
        seeker += 3; // The class and a separator.
        if (seeker >= data.size()) {
            warnings_.push_back(
                std::format("Vocabulary word is truncated for suffix {} at {}", suffixes_.size(), seeker));
            break;
        }
        auto [word, word_terminated] = c_string(data, seeker);
        if (!word_terminated) {
            warnings_.push_back(
                std::format("Vocabulary word appears truncated for suffix {} at {}", suffixes_.size(), seeker));
            return;
        }
        seeker += word.size() + 1;
        suffix.word_suffix = std::move(word);
        suffix.class_mask = u16be(data, seeker);
        seeker += 3; // The class mask and a separator.
        suffixes_.push_back(std::move(suffix));
    }
}

bool Vocabulary::load_branches(std::span<const std::uint8_t> data) {
    branches_.clear();
    const std::size_t count = data.size() / 20;
    if (count == 0) {
        warnings_.emplace_back("Parser tree data is empty");
        return false;
    }
    branches_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        ParseTreeBranch& branch = branches_[i];
        branch.id = static_cast<std::int16_t>(u16le(data, i * 20));
        for (std::size_t k = 0; k < 9; ++k) {
            branch.data[k] = u16le(data, (i * 20) + 2 + (2 * k));
        }
        branch.data[9] = 0;
    }
    if (branches_.back().id == 0) { // The list may end with an empty rule.
        branches_.pop_back();
    }
    return true;
}

WordMeanings Vocabulary::lookup_word(std::string_view word) const {
    std::string without_dashes;
    for (const char c : word) {
        if (c != '-') {
            without_dashes += c;
        }
    }
    if (const auto found = words_.find(without_dashes); found != words_.end()) {
        return found->second;
    }
    WordMeanings result;
    for (const Suffix& suffix : suffixes_) {
        if (suffix.alt_suffix.size() > word.size()) {
            continue;
        }
        const std::size_t index = word.size() - suffix.alt_suffix.size();
        if (word.substr(index) != suffix.alt_suffix) {
            continue;
        }
        const std::string stem = std::string(word.substr(0, index)) + suffix.word_suffix;
        const auto found = words_.find(stem);
        if (found == words_.end()) {
            continue;
        }
        for (const ResultWord& meaning : found->second) {
            if ((static_cast<unsigned>(meaning.word_class) & static_cast<unsigned>(suffix.class_mask)) != 0) {
                result.push_back(ResultWord{.word_class = suffix.result_class, .group = meaning.group});
                if (options_.sci0) {
                    return result; // SCI0 takes the first match.
                }
            }
        }
    }
    if (!result.empty()) {
        return result;
    }
    // A number, except in SCI0 early: strtol reads the whole word and gives
    // a value of 0 or more.
    if (!options_.sci0_early && !without_dashes.empty() &&
        std::ranges::all_of(without_dashes, [](char c) { return c >= '0' && c <= '9'; })) {
        result.push_back(ResultWord{.word_class = class_noun << 4, .group = magic_number_group});
    }
    return result;
}

std::optional<Sentence> Vocabulary::tokenize(std::string_view sentence, std::string& error) const {
    error.clear();
    Sentence result;
    std::string word;
    // The sentence and its terminating NUL.
    for (std::size_t i = 0; i <= sentence.size(); ++i) {
        const auto c = i < sentence.size() ? static_cast<std::uint8_t>(sentence[i]) : std::uint8_t{0};
        if (is_ascii_alnum(c) || (c == '-' && !word.empty()) || c >= 0x80) {
            word += static_cast<char>(lower_case_map[c]);
        } else if (c == ' ' || c == 0) {
            if (!word.empty()) {
                WordMeanings meanings = lookup_word(word);
                if (meanings.empty()) {
                    error = word;
                    return std::nullopt;
                }
                result.push_back(std::move(meanings));
            }
            word.clear();
        }
        // Other characters are skipped; they do not separate words.
    }
    return result;
}

void Vocabulary::synonymize(Sentence& sentence, std::span<const Synonym> synonyms) {
    for (WordMeanings& meanings : sentence) {
        for (ResultWord& meaning : meanings) {
            for (const Synonym& synonym : synonyms) {
                if (meaning.group == synonym.replaceant) {
                    meaning.group = synonym.replacement;
                }
            }
        }
    }
}

void Vocabulary::replace_pronouns(Sentence& sentence) const {
    if (pronoun_reference_ == no_pronoun_reference) {
        return;
    }
    for (WordMeanings& meanings : sentence) {
        for (ResultWord& meaning : meanings) {
            if ((static_cast<unsigned>(meaning.word_class) & (class_pronoun << 4U)) != 0) {
                meaning.word_class = class_noun << 4;
                meaning.group = pronoun_reference_;
            }
        }
    }
}

bool Vocabulary::store_pronoun_reference() {
    const TreeView tree{nodes_};
    int found = scan_for_major(tree, 0, 0x142); // 0x142: the object.
    while (found >= 0 && !tree.is_terminal(found)) {
        found = scan_for_major(tree, found, 0x141);
    }
    if (found < 0) {
        return false;
    }
    pronoun_reference_ = tree.terminal_value(found);
    return true;
}

} // namespace sci::text
