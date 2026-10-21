// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parser's state (engines/sci/parser/vocabulary.h): the synonyms
// kSetSynonyms sets and the parser event. The parser itself (kParse, kSaid)
// comes with milestone 4b.

#pragma once

#include "sci/vm/reg.hpp"

#include <cstdint>
#include <vector>

namespace sci::vm {

class Machine;

struct Synonym {
    std::uint16_t replaceant = 0;
    std::uint16_t replacement = 0;
};

class Parser {
public:
    void attach(Machine& /*machine*/) {}

    // Vocabulary::reset: the synonyms stay.
    void reset() { parser_event_ = null_reg; }

    void invalidate_event() { parser_event_ = null_reg; }

    void clear_synonyms() { synonyms_.clear(); }

    void add_synonym(std::uint16_t replaceant, std::uint16_t replacement) {
        synonyms_.push_back(Synonym{.replaceant = replaceant, .replacement = replacement});
    }

    [[nodiscard]] const std::vector<Synonym>& synonyms() const { return synonyms_; }

    // Whether the last parsed input matches the said spec at `spec`
    // (ScummVM's said()). No match until the parser is ported, as ScummVM
    // answers without a valid parse.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): the parser port needs its state here.
    [[nodiscard]] bool said(Machine& /*machine*/, Reg /*spec*/) const { return false; }

private:
    Reg parser_event_;
    std::vector<Synonym> synonyms_;
};

} // namespace sci::vm
