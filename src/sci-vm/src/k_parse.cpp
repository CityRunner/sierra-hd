// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kparse.cpp (kSaid, kParse),
//   engines/sci/engine/state.cpp (SciEngine::checkVocabularySwitch),
//   engines/sci/parser/vocabulary.cpp (the Vocabulary constructor's
//       choice of resources)
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

#include "kernel_functions.hpp"
#include "machine.hpp"
#include "parser.hpp"
#include "subsystems.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sci::vm {

namespace {

// The SCI0 vocabularies: words, suffixes and grammar branches.
constexpr int vocab_words = 0;
constexpr int vocab_branches = 900;
constexpr int vocab_suffixes = 901;
constexpr int foreign_offset = 10;

// The parser file (docs/formats/replay-and-trace.md) quotes the input with
// \", \\ and \xHH for bytes outside 32 to 126.
std::string quoted(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        const auto byte = static_cast<std::uint8_t>(c);
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (byte < 32 || byte > 126) {
            std::format_to(std::back_inserter(result), "\\x{:02x}", byte);
        } else {
            result += c;
        }
    }
    return result + "\"";
}

// A parse tree node and its children, as the reference writes them: a branch
// as (<left>,<right>), a leaf as its value, a word as w<value>[/<right>], a
// missing child as -.
// NOLINTNEXTLINE(misc-no-recursion): the tree is at most 500 nodes, cut at depth 100.
void append_tree(std::string& out, std::span<const text::ParseNode> nodes, int index, int depth) {
    constexpr int max_depth = 100;
    if (index < 0 || std::cmp_greater_equal(index, nodes.size())) {
        out += '-';
        return;
    }
    if (depth > max_depth) {
        out += '?';
        return;
    }
    const text::ParseNode& node = nodes[static_cast<std::size_t>(index)];
    switch (node.type) {
    case text::NodeType::Leaf:
        std::format_to(std::back_inserter(out), "{:x}", node.value);
        break;
    case text::NodeType::Word:
        std::format_to(std::back_inserter(out), "w{:x}", node.value);
        if (node.right >= 0) {
            out += '/';
            append_tree(out, nodes, node.right, depth + 1);
        }
        break;
    case text::NodeType::Branch:
        out += '(';
        append_tree(out, nodes, node.left, depth + 1);
        out += ',';
        append_tree(out, nodes, node.right, depth + 1);
        out += ')';
        break;
    }
}

// The words parseGNF got: <class>:<group> in hex, meanings separated by |,
// words by commas.
std::string words_text(const text::Sentence& words) {
    std::string out;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        for (std::size_t j = 0; j < words[i].size(); ++j) {
            std::format_to(std::back_inserter(out), "{}{:03x}:{:03x}", j != 0 ? "|" : "", words[i][j].word_class,
                           words[i][j].group);
        }
    }
    return out;
}

// kSaid's line: the spec's bytes up to its 0xff (- when there is none), how
// it matched and the claimed flag it leaves (-1 without an event).
void record_said(Parser& parser, std::span<const std::uint8_t> spec, std::string_view outcome, int claimed) {
    ParserRecord* record = parser.record();
    if (record == nullptr) {
        return;
    }
    record->said = "said ";
    if (spec.empty()) {
        record->said += '-';
    }
    for (const std::uint8_t byte : spec) {
        std::format_to(std::back_inserter(record->said), "{:02x}", byte);
        if (byte == 0xFF) {
            break;
        }
    }
    std::format_to(std::back_inserter(record->said), " {} claimed=", outcome);
    record->said += claimed < 0 ? std::string("-") : std::to_string(claimed);
}

} // namespace

void Parser::attach(Machine& machine) {
    load(machine, false);
}

void Parser::load(Machine& machine, bool foreign) {
    const int offset = foreign ? foreign_offset : 0;
    const auto resource = [&](int number) -> std::optional<std::span<const std::uint8_t>> {
        if (const auto* data = machine.resources().find(ResourceType::Vocab, number + offset)) {
            return std::span<const std::uint8_t>(*data);
        }
        return std::nullopt;
    };
    // SCI0 late: one meaning per word, numbers recognized.
    vocabulary_.emplace(resource(vocab_words), resource(vocab_suffixes), resource(vocab_branches),
                        text::ParserOptions{.sci0 = true, .sci0_early = false});
    for (const std::string& warning : vocabulary_->warnings()) {
        warn(warning);
    }
    parser_event_ = null_reg;
    parser_valid_ = false;
}

void Parser::check_vocabulary_switch(Machine& machine) {
    int language = 1;
    if (machine.selector().parse_lang != no_selector) {
        language = machine.read_selector(machine.game_object(), machine.selector().parse_lang).to_uint16();
    }
    if (language != vocabulary_language_) {
        // A new Vocabulary in ScummVM: its synonyms and pronoun go too.
        synonyms_.clear();
        load(machine, language > 1);
        vocabulary_language_ = language;
    }
}

int Parser::match_said(std::span<const std::uint8_t> spec) const {
    if (!parser_valid_ || !vocabulary_) {
        return text::said_no_match;
    }
    std::vector<std::string> warnings;
    const int result = text::said(spec, vocabulary_->nodes(), &warnings);
    for (const std::string& warning : warnings) {
        warn(warning);
    }
    return result;
}

bool Parser::said(Machine& machine, Reg spec) const {
    const std::span<const std::uint8_t> bytes = machine.segments().deref_bulk(spec, 0);
    if (bytes.empty()) {
        warn("Could not dereference saidSpec");
        return false;
    }
    return match_said(bytes) != text::said_no_match;
}

Reg k_said(Machine& machine, std::span<Reg> args) {
    const Reg spec_pointer = args[0];
    Parser& parser = machine.subsystems().parser();
    if (spec_pointer.segment == 0) {
        record_said(parser, {}, "null", -1);
        return null_reg;
    }
    const std::span<const std::uint8_t> spec = machine.segments().deref_bulk(spec_pointer, 0);
    if (spec.empty()) {
        record_said(parser, {}, "invalid", -1);
        warn(std::format("Said on non-string, pointer {:04x}:{:04x}", spec_pointer.segment, spec_pointer.offset));
        return null_reg;
    }
    const Reg event = parser.parser_event();
    if (event.is_null() || machine.read_selector_value(event, machine.selector().claimed) != 0) {
        record_said(parser, spec, "skipped", event.is_null() ? -1 : 1);
        return null_reg;
    }
    const int result = parser.match_said(spec);
    if (result == text::said_no_match) {
        record_said(parser, spec, "none", 0);
    } else if (result == text::said_partial_match) {
        record_said(parser, spec, "partial", 0);
    } else {
        record_said(parser, spec, "full", 1);
    }
    if (result == text::said_no_match) {
        return null_reg;
    }
    machine.set_acc(number(1));
    if (result != text::said_partial_match) {
        machine.write_selector_value(event, machine.selector().claimed, 1);
    }
    return machine.acc();
}

Reg k_parse(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    const SelectorCache& s = machine.selector();
    const Reg string_pointer = args[0];
    const Reg event = args[1];
    const std::string sentence = segments.get_string(string_pointer);
    Parser& parser = machine.subsystems().parser();
    parser.check_vocabulary_switch(machine);
    text::Vocabulary* vocabulary = parser.vocabulary();
    parser.set_parser_event(event);
    const std::array<Reg, 2> params{segments.parser_pointer(), string_pointer};

    std::string error;
    std::optional<text::Sentence> words =
        vocabulary != nullptr ? vocabulary->tokenize(sentence, error) : std::optional<text::Sentence>{};
    parser.set_parser_valid(false);
    ParserRecord* record = parser.record();
    if (record != nullptr) {
        record->parse = "parse " + quoted(sentence);
        record->parse_event = event;
    }

    if (words && !words->empty()) {
        text::Vocabulary::synonymize(*words, parser.synonyms());
        machine.set_acc(number(1));
        vocabulary->replace_pronouns(*words);
        const bool parsed = vocabulary->parse(*words);
        if (record != nullptr) {
            record->parse += (parsed ? " ok words=" : " syntax words=") + words_text(*words);
            if (parsed) {
                record->parse += " tree=";
                append_tree(record->parse, vocabulary->nodes(), 0, 0);
            }
        }
        if (!parsed) {
            machine.set_acc(number(1));
            machine.write_selector_value(event, s.claimed, 1);
            machine.invoke_selector(machine.game_object(), s.syntax_fail, params);
        } else {
            parser.set_parser_valid(true);
            vocabulary->store_pronoun_reference();
            machine.write_selector_value(event, s.claimed, 0);
        }
        return machine.acc();
    }
    machine.set_acc(number(0));
    machine.write_selector_value(event, s.claimed, 1);
    if (record != nullptr) {
        record->parse += error.empty() ? std::string(" empty") : " unknown word=" + quoted(error);
    }
    if (!error.empty()) {
        segments.strcpy(segments.parser_pointer(), error);
        machine.invoke_selector(machine.game_object(), s.word_fail, params);
        return number(1); // It did not work.
    }
    return machine.acc();
}

} // namespace sci::vm
