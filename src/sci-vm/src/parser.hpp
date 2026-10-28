// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parser state of the VM (ScummVM's Vocabulary object as SciEngine
// holds it, engines/sci/parser/vocabulary.h): the vocabulary from
// sci-text, the synonyms kSetSynonyms sets, the event kParse saw and
// whether its parse tree is valid for kSaid.

#pragma once

#include "sci/text/parser.hpp"
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class Machine;

using text::Synonym;

// What the last kParse and kSaid calls did, as the lines of the parser file
// (docs/formats/replay-and-trace.md) without tick and call number. The
// interpreter writes them with the call's trace line; they are only kept
// while recording.
struct ParserRecord {
    std::string parse; // Without the event's type and claimed flag,
    Reg parse_event;   // which are read from it when the call returns.
    std::string said;
};

class Parser {
public:
    // Creates the English vocabulary (SciEngine::run).
    void attach(Machine& machine);

    // Vocabulary::reset: the synonyms stay.
    void reset() {
        parser_event_ = null_reg;
        parser_valid_ = false;
    }

    void invalidate_event() { parser_event_ = null_reg; }

    void clear_synonyms() { synonyms_.clear(); }

    void add_synonym(std::uint16_t replaceant, std::uint16_t replacement) {
        synonyms_.push_back(Synonym{.replaceant = replaceant, .replacement = replacement});
    }

    [[nodiscard]] const std::vector<Synonym>& synonyms() const { return synonyms_; }

    // SciEngine::checkVocabularySwitch: a new vocabulary when the game's
    // parseLang changes (2 and up: the foreign vocabulary, resources + 10).
    void check_vocabulary_switch(Machine& machine);

    [[nodiscard]] text::Vocabulary* vocabulary() { return vocabulary_ ? &*vocabulary_ : nullptr; }

    [[nodiscard]] Reg parser_event() const { return parser_event_; }

    void set_parser_event(Reg event) { parser_event_ = event; }

    [[nodiscard]] bool parser_valid() const { return parser_valid_; }

    void set_parser_valid(bool valid) { parser_valid_ = valid; }

    // said(): the spec bytes against the last valid parse; said_no_match
    // without one. ScummVM's warnings go through warn().
    [[nodiscard]] int match_said(std::span<const std::uint8_t> spec) const;

    // Whether the last parsed input matches the said spec at `spec`, as
    // Menu::kernelSelect asks (a partial match counts).
    [[nodiscard]] bool said(Machine& machine, Reg spec) const;

    void set_recording(bool on) { recording_ = on; }

    // The record kParse and kSaid fill, or none when not recording.
    [[nodiscard]] ParserRecord* record() { return recording_ ? &record_ : nullptr; }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;

private:
    void load(Machine& machine, bool foreign);

    std::optional<text::Vocabulary> vocabulary_;
    int vocabulary_language_ = 1; // English is loaded at start.
    Reg parser_event_;
    bool parser_valid_ = false;
    std::vector<Synonym> synonyms_;
    bool recording_ = false;
    ParserRecord record_;
};

} // namespace sci::vm
