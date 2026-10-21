// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/parser/grammar.cpp (buildGNF, parseGNF and their helpers),
//   engines/sci/parser/vocabulary.cpp (scanForMajor)
// and translated into this project's style, types and error handling. The
// linked rule lists become vectors with the same order and the same
// duplicate checks; tree pointers become node indices.
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
#include <optional>
#include <utility>

namespace sci::text {

namespace {

constexpr std::uint32_t token_oparen = 0xff000000U;
constexpr std::uint32_t token_cparen = 0xfe000000U;
constexpr std::uint32_t token_terminal_class = 0x10000U;
constexpr std::uint32_t token_terminal_group = 0x20000U;
constexpr std::uint32_t token_stuffing_leaf = 0x40000U;
constexpr std::uint32_t token_stuffing_word = 0x80000U;
constexpr std::uint32_t token_non_nt =
    token_oparen | token_terminal_class | token_terminal_group | token_stuffing_leaf | token_stuffing_word;
constexpr std::uint32_t token_terminal = token_terminal_class | token_terminal_group;

constexpr int node_last_word_storage = 0x140;
constexpr int node_compare_type = 0x146;
constexpr int node_compare_group = 0x14d;
constexpr int node_force_storage = 0x154;

// Guards against grammars that never stop producing rules, which ScummVM
// expands until memory runs out (its limit of 30 iterations only counts in
// verbose mode). Real grammars stay far below both.
constexpr int max_gnf_iterations = 1000;
constexpr std::size_t max_gnf_rules = 4000;

using RuleList = std::vector<ParseRule>;

// ParseRuleList::terminal: the first special if it is a terminal, else 0.
std::uint32_t terminal_of(const ParseRule& rule) {
    const std::uint32_t term = rule.first_special < rule.data.size() ? rule.data[rule.first_special] : 0;
    return (term & token_terminal) != 0 ? term : 0;
}

// _vocab_add_rule: appends, unless a later entry with the same terminal is
// the same rule (the first entry is never compared, as in ScummVM).
void add_rule(RuleList& list, std::optional<ParseRule> rule) {
    if (!rule || rule->data.empty()) {
        return;
    }
    const std::uint32_t term = terminal_of(*rule);
    for (std::size_t i = 1; i < list.size(); ++i) {
        if (terminal_of(list[i]) == term && list[i] == *rule) {
            return;
        }
    }
    list.push_back(std::move(*rule));
}

// _vinsert: `stuffing` put in place of the first non-terminal of `turkey`.
std::optional<ParseRule> insert_rule(const ParseRule& turkey, const ParseRule& stuffing) {
    std::size_t first = turkey.first_special;
    while (first < turkey.data.size() && (turkey.data[first] & token_non_nt) != 0) {
        ++first;
    }
    const auto stuffing_id = static_cast<std::uint32_t>(stuffing.id); // As ScummVM's int compare.
    if (first == turkey.data.size() || turkey.data[first] != stuffing_id) {
        return std::nullopt;
    }
    ParseRule rule = turkey;
    rule.num_specials += stuffing.num_specials - 1; // Unsigned, as in ScummVM.
    rule.first_special = static_cast<std::uint32_t>(first) + stuffing.first_special;
    rule.data.resize(turkey.data.size() - 1 + stuffing.data.size());
    std::ranges::copy(stuffing.data, rule.data.begin() + static_cast<std::ptrdiff_t>(first));
    if (first < turkey.data.size() - 1) {
        std::copy(turkey.data.begin() + static_cast<std::ptrdiff_t>(first) + 1, turkey.data.end(),
                  rule.data.begin() + static_cast<std::ptrdiff_t>(first + stuffing.data.size()));
    }
    return rule;
}

// _vbuild_rule.
std::optional<ParseRule> build_rule(const ParseTreeBranch& branch) {
    std::size_t tokens = 0;
    std::size_t position = 0;
    while (position < 10 && branch.data[position] != 0) {
        const int type = branch.data[position];
        position += 2;
        if (type == node_compare_type || type == node_compare_group || type == node_force_storage) {
            ++tokens;
        } else if (type > node_last_word_storage) {
            tokens += 5;
        } else {
            return std::nullopt;
        }
    }
    ParseRule rule;
    rule.id = branch.id;
    rule.num_specials = static_cast<std::uint32_t>(position / 2);
    rule.data.resize(tokens);
    rule.first_special = 0;
    tokens = 0;
    for (std::size_t i = 0; i < position; i += 2) {
        const auto type = static_cast<std::uint32_t>(branch.data[i]);
        const auto value = static_cast<std::uint32_t>(branch.data[i + 1]);
        if (type == node_compare_type) {
            rule.data[tokens++] = value | token_terminal_class;
        } else if (type == node_compare_group) {
            rule.data[tokens++] = value | token_terminal_group;
        } else if (type == node_force_storage) {
            rule.data[tokens++] = value | token_stuffing_word;
        } else { // An inductive rule.
            rule.data[tokens++] = token_oparen;
            rule.data[tokens++] = type | token_stuffing_leaf;
            rule.data[tokens++] = value | token_stuffing_leaf;
            if (i == 0) {
                rule.first_special = static_cast<std::uint32_t>(tokens);
            }
            rule.data[tokens++] = value; // The non-terminal.
            rule.data[tokens++] = token_cparen;
        }
    }
    return rule;
}

// _vsatisfy_rule: the rule with its first special matched by a word.
std::optional<ParseRule> satisfy_rule(const ParseRule& rule, const WordMeanings& input) {
    if (rule.num_specials == 0 || rule.first_special >= rule.data.size()) {
        return std::nullopt;
    }
    const std::uint32_t dep = rule.data[rule.first_special];
    int count = 0;
    std::uint32_t match = 0;
    std::vector<std::uint32_t> matches;
    for (const ResultWord& word : input) {
        const auto word_class = static_cast<std::uint32_t>(word.word_class);
        const auto group = static_cast<std::uint32_t>(word.group);
        if (((dep & token_terminal_class) != 0 && ((dep & 0xffffU) & word_class) != 0) ||
            ((dep & token_terminal_group) != 0 && ((dep & 0xffffU) & group) != 0)) {
            if (count == 0) {
                match = token_stuffing_word | group;
            } else {
                matches.push_back(token_stuffing_word | group);
            }
            ++count;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }
    ParseRule result = rule;
    result.data[rule.first_special] = match;
    if (count > 1) {
        result.data.insert(result.data.begin() + static_cast<std::ptrdiff_t>(rule.first_special) + 1, matches.begin(),
                           matches.end());
    }
    --result.num_specials;
    result.first_special = 0;
    if (result.num_specials != 0) { // The next special, if any.
        for (std::size_t i = rule.first_special; i < result.data.size(); ++i) {
            const std::uint32_t token = result.data[i];
            if ((token & token_non_nt) == 0 || (token & token_terminal) != 0) {
                result.first_special = static_cast<std::uint32_t>(i);
                break;
            }
        }
    }
    return result;
}

// _vocab_split_rule_list: the rules from the first terminal rule after the
// first rule on.
RuleList split_rule_list(RuleList& list) {
    std::size_t split = 1;
    while (split < list.size() && terminal_of(list[split]) == 0) {
        ++split;
    }
    if (split >= list.size()) {
        return {};
    }
    RuleList tail(std::make_move_iterator(list.begin() + static_cast<std::ptrdiff_t>(split)),
                  std::make_move_iterator(list.end()));
    list.resize(split);
    return tail;
}

// The tree writers (_vbpt_*), writing after node `pos`.
class TreeWriter {
public:
    TreeWriter(std::vector<ParseNode>& nodes, int pos) : nodes_(&nodes), pos_(pos) {}

    // False once a write went past the node table (ScummVM's buffer overrun).
    [[nodiscard]] bool ok() const { return ok_; }

    int pareno(int base) {
        node(base).left = pos_ + 1;
        node(++pos_) = ParseNode{.type = NodeType::Branch, .value = node(pos_).value, .left = -1, .right = -1};
        return pos_;
    }

    int parenc(int paren) {
        node(paren).right = ++pos_;
        node(pos_) = ParseNode{.type = NodeType::Branch, .value = node(pos_).value, .left = -1, .right = -1};
        return pos_;
    }

    int append(int base, int value) {
        node(base).left = ++pos_;
        ParseNode& leaf = node(pos_);
        leaf.type = NodeType::Leaf;
        leaf.value = value;
        leaf.right = -1;
        node(base).right = ++pos_;
        node(pos_) = ParseNode{.type = NodeType::Branch, .value = node(pos_).value, .left = -1, .right = -1};
        return pos_;
    }

    int terminate(int base, int value) {
        ParseNode& leaf = node(base);
        leaf.type = NodeType::Leaf;
        leaf.value = value;
        leaf.right = -1;
        return pos_;
    }

    int append_word(int base, int value) {
        ParseNode& word = node(base);
        word.type = NodeType::Word;
        word.value = value;
        word.right = ++pos_;
        node(pos_) = ParseNode{.type = NodeType::Branch, .value = node(pos_).value, .left = -1, .right = -1};
        return pos_;
    }

    int terminate_word(int base, int value) {
        ParseNode& word = node(base);
        word.type = NodeType::Word;
        word.value = value;
        word.right = -1;
        return pos_;
    }

    // _vbpt_write_subexpression.
    // NOLINTNEXTLINE(misc-no-recursion): nested parentheses, as in ScummVM.
    std::size_t write_subexpression(const ParseRule& rule, std::size_t rule_pos, int write_pos) {
        const auto token_at = [&](std::size_t at) { return at < rule.data.size() ? rule.data[at] : token_cparen; };
        std::uint32_t token = 0;
        while ((token = token_at(rule_pos++)) != token_cparen && ok_) {
            std::uint32_t next = token_at(rule_pos);
            if (token == token_oparen) {
                const int write_pos2 = pareno(write_pos);
                rule_pos = write_subexpression(rule, rule_pos, write_pos2);
                next = token_at(rule_pos);
                if (next != token_cparen) {
                    write_pos = parenc(write_pos);
                }
            } else if ((token & token_stuffing_leaf) != 0) {
                const auto value = static_cast<int>(token & 0xffffU);
                write_pos = next == token_cparen ? terminate(write_pos, value) : append(write_pos, value);
            } else if ((token & token_stuffing_word) != 0) {
                const auto value = static_cast<int>(token & 0xffffU);
                write_pos = next == token_cparen ? terminate_word(write_pos, value) : append_word(write_pos, value);
            } else {
                return rule_pos; // ScummVM warns: the rule data is broken.
            }
        }
        return rule_pos;
    }

private:
    ParseNode& node(int index) {
        static ParseNode overflow{};
        if (index < 0 || static_cast<std::size_t>(index) >= nodes_->size()) {
            ok_ = false;
            overflow = ParseNode{};
            return overflow;
        }
        return (*nodes_)[static_cast<std::size_t>(index)];
    }

    std::vector<ParseNode>* nodes_;
    int pos_; // The last node used.
    bool ok_ = true;
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to Vocabulary::buildGNF.
void Vocabulary::build_gnf() {
    RuleList nonterminals;
    for (std::size_t i = 1; i < branches_.size(); ++i) { // Branch 0 is special.
        std::optional<ParseRule> rule = build_rule(branches_[i]);
        if (!rule) {
            rules_.clear();
            return;
        }
        add_rule(nonterminals, std::move(rule));
    }
    RuleList new_terminals = split_rule_list(nonterminals);
    RuleList terminals;
    for (int iteration = 0; iteration < max_gnf_iterations; ++iteration) {
        RuleList newer;
        for (const ParseRule& nonterminal : nonterminals) {
            for (const ParseRule& terminal : new_terminals) {
                add_rule(newer, insert_rule(nonterminal, terminal));
            }
        }
        for (ParseRule& rule : new_terminals) { // _vocab_merge_rule_lists
            add_rule(terminals, std::move(rule));
        }
        new_terminals = std::move(newer);
        if (new_terminals.empty()) {
            break;
        }
        if (terminals.size() + new_terminals.size() > max_gnf_rules || iteration + 1 == max_gnf_iterations) {
            warnings_.emplace_back("Parser grammar does not converge, disabling the parser");
            rules_.clear();
            return;
        }
    }
    rules_ = std::move(terminals);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to Vocabulary::parseGNF.
bool Vocabulary::parse(const Sentence& sentence) {
    if (branches_.empty()) {
        return false;
    }
    RuleList work;
    for (const ParseRule& rule : rules_) { // _vocab_clone_rule_list_by_id
        if (rule.id == branches_[0].data[1]) {
            add_rule(work, rule);
        }
    }
    const std::size_t words = sentence.size();
    for (std::size_t word = 0; word < words; ++word) {
        RuleList reduced;
        for (const ParseRule& rule : work) {
            if (rule.num_specials <= words - word) {
                add_rule(reduced, satisfy_rule(rule, sentence[word]));
            }
        }
        if (reduced.empty()) {
            return false;
        }
        RuleList next;
        if (word + 1 < words) {
            for (const ParseRule& rule : reduced) {
                if (rule.num_specials == 0 || rule.first_special >= rule.data.size()) {
                    continue;
                }
                const std::uint32_t id = rule.data[rule.first_special];
                for (const ParseRule& candidate : rules_) {
                    const auto candidate_id = static_cast<std::uint32_t>(candidate.id);
                    if (candidate_id == id) {
                        add_rule(next, insert_rule(rule, candidate));
                    }
                }
            }
        } else {
            next = std::move(reduced);
        }
        work = std::move(next);
        if (work.empty()) {
            return false;
        }
    }
    if (work.empty()) {
        return false;
    }
    nodes_[0] = ParseNode{.type = NodeType::Branch, .value = nodes_[0].value, .left = 1, .right = 2};
    nodes_[1] = ParseNode{.type = NodeType::Leaf, .value = 0x141, .left = nodes_[1].left, .right = -1};
    nodes_[2] = ParseNode{.type = NodeType::Branch, .value = nodes_[2].value, .left = -1, .right = -1};
    TreeWriter writer(nodes_, 2);
    const int top = writer.append(2, branches_[0].id);
    writer.write_subexpression(work.front(), 0, top);
    return writer.ok();
}

// NOLINTNEXTLINE(misc-no-recursion): it looks below the 0x141 node, as in ScummVM.
int scan_for_major(const TreeView& tree, int node, int major) {
    if (tree.is_terminal(node)) {
        return tree.major(node) == major ? node : -1;
    }
    int ptr = tree.right(node);
    while (tree.right(ptr) >= 0) {
        ptr = tree.right(ptr);
        if (tree.major(tree.left(ptr)) == major) {
            return tree.left(ptr);
        }
    }
    if (major == 0x141) {
        return -1;
    }
    const int below = scan_for_major(tree, node, 0x141);
    return below < 0 ? -1 : scan_for_major(tree, below, major);
}

} // namespace sci::text
