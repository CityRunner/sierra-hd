// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/parser/said.cpp (the Said spec parser and the tree
//       matching)
// and translated into this project's style, types and error handling. The
// file-level state of ScummVM's parser is a SaidParser object here, and
// tree pointers are node indices.
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

#include <vector>

namespace sci::text {

namespace {

constexpr std::size_t max_said_tokens = 128;
constexpr int said_tree_start = 4; // The four top nodes.

constexpr int said_first = 0xf0; // SAID_FIRST
constexpr int said_term = 0xff;  // SAID_TERM

constexpr int token_comma = 0xF000;
constexpr int token_slash = 0xF200;
constexpr int token_pareno = 0xF300;
constexpr int token_parenc = 0xF400;
constexpr int token_bracketo = 0xF500;
constexpr int token_bracketc = 0xF600;
constexpr int token_lt = 0xF800;
constexpr int token_gt = 0xF900;
constexpr int token_term = 0xFF00;

constexpr int word_none = 0x0ffe;
constexpr int word_any = 0x0fff;

constexpr int null_node = -1;

// The Said spec parser: the tokens, the tree being built and the cursor,
// which ScummVM keeps in file-level variables.
class SaidParser {
public:
    explicit SaidParser(std::vector<int> tokens) : tokens_(std::move(tokens)), tree_(tree_nodes) {}

    // buildSaidTree.
    bool build() {
        branch_node(0, 1, 2);
        leaf_node(1, 0x141); // Magic number 1.
        branch_node(2, 3, null_node);
        leaf_node(3, 0x13f); // Magic number 2.
        tree_pos_ = said_tree_start;
        if (!parse_spec(2)) {
            return false;
        }
        if (token() != token_term) {
            tree_[2].right = null_node;
            token_ = 0;
            tree_pos_ = said_tree_start;
            return false;
        }
        return true;
    }

    [[nodiscard]] std::span<const ParseNode> tree() const { return tree_; }

private:
    // The cursor and the tree position, restored when a rule fails.
    struct Mark {
        std::size_t token;
        int tree_pos;
        int right;
    };

    [[nodiscard]] int token() const { return token_ < tokens_.size() ? tokens_[token_] : token_term; }

    Mark mark(int parent) { return Mark{.token = token_, .tree_pos = tree_pos_, .right = node(parent).right}; }

    bool restore(const Mark& saved, int parent) {
        token_ = saved.token;
        tree_pos_ = saved.tree_pos;
        node(parent).right = saved.right;
        return false;
    }

    ParseNode& node(int index) {
        if (static_cast<std::size_t>(index) >= tree_.size()) {
            tree_.resize(static_cast<std::size_t>(index) + 1);
        }
        return tree_[static_cast<std::size_t>(index)];
    }

    // said_next_node; ScummVM asserts below VOCAB_TREE_NODES, the table
    // grows here instead.
    int next_node() {
        node(tree_pos_);
        return tree_pos_++;
    }

    int leaf_node(int pos, int value) {
        ParseNode& n = node(pos);
        n.type = NodeType::Leaf;
        n.value = value;
        n.right = null_node;
        return pos;
    }

    int word_node(int pos, int value) {
        ParseNode& n = node(pos);
        n.type = NodeType::Word;
        n.value = value;
        n.right = null_node;
        return pos;
    }

    int branch_node(int pos, int left, int right) {
        ParseNode& n = node(pos);
        n.type = NodeType::Branch;
        n.left = left;
        n.right = right;
        return pos;
    }

    int attach_left(int pos, int left) {
        node(pos).type = NodeType::Branch;
        node(pos).left = left;
        return pos;
    }

    int attach_right(int pos, int right) {
        node(pos).type = NodeType::Branch;
        node(pos).right = right;
        return pos;
    }

    // said_attach_subtree: pos->right = ((major (minor subtree...)) nil),
    // with the nodes allocated in ScummVM's order of evaluation.
    void attach_subtree(int pos, int major, int minor, int subtree) {
        const int outer = next_node();
        const int inner = next_node();
        const int major_leaf = leaf_node(next_node(), major);
        const int minor_leaf = leaf_node(next_node(), minor);
        attach_right(pos,
                     branch_node(outer, branch_node(inner, major_leaf, attach_left(subtree, minor_leaf)), null_node));
    }

    bool parse_word(int parent) {
        const int value = token();
        if ((static_cast<unsigned>(value) & 0x8000U) != 0) {
            return false;
        }
        ++token_;
        node(parent).right = word_node(next_node(), value);
        return true;
    }

    // parsePart2 and parsePart3, which differ only in the major.
    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_part(int parent, bool& nonempty, int major) {
        const Mark saved = mark(parent);
        const int new_node = branch_node(next_node(), null_node, null_node);
        nonempty = true;
        if (parse_slash(new_node)) {
            attach_subtree(parent, major, 0x14a, new_node);
            return true;
        }
        if (token() == token_bracketo) {
            ++token_;
            if (parse_part(new_node, nonempty, major) && token() == token_bracketc) {
                ++token_;
                attach_subtree(parent, 0x152, major, new_node);
                return true;
            }
        }
        if (token() == token_slash) {
            ++token_;
            nonempty = false;
            return true;
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_slash(int parent) {
        const Mark saved = mark(parent);
        if (token() == token_slash) {
            ++token_;
            if (parse_expr(parent)) {
                return true;
            }
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_ref(int parent) {
        const Mark saved = mark(parent);
        int new_node = branch_node(next_node(), null_node, null_node);
        int new_parent = parent;
        if (token() == token_lt) {
            ++token_;
            if (parse_list(new_node)) {
                attach_subtree(new_parent, 0x144, 0x14f, new_node);
                new_parent = node(new_parent).right;
                new_node = branch_node(next_node(), null_node, null_node);
                if (parse_ref(new_node)) {
                    attach_subtree(new_parent, 0x141, 0x144, new_node);
                }
                return true;
            }
        }
        if (token() == token_bracketo) {
            ++token_;
            if (parse_ref(new_node) && token() == token_bracketc) {
                ++token_;
                attach_subtree(parent, 0x152, 0x144, new_node);
                return true;
            }
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_comma(int parent) {
        const Mark saved = mark(parent);
        if (token() == token_comma) {
            ++token_;
            if (parse_list(parent)) {
                return true;
            }
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_list_entry(int parent) {
        const Mark saved = mark(parent);
        const int new_node = branch_node(next_node(), null_node, null_node);
        if (token() == token_bracketo) {
            ++token_;
            if (parse_expr(new_node) && token() == token_bracketc) {
                ++token_;
                attach_subtree(parent, 0x152, 0x14c, new_node);
                return true;
            }
        } else if (token() == token_pareno) {
            ++token_;
            if (parse_expr(new_node) && token() == token_parenc) {
                ++token_;
                attach_subtree(parent, 0x141, 0x14c, new_node);
                return true;
            }
        } else if (parse_word(new_node)) {
            attach_subtree(parent, 0x141, 0x153, new_node);
            return true;
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_list(int parent) {
        const Mark saved = mark(parent);
        if (parse_list_entry(parent)) {
            const int new_parent = node(parent).right;
            parse_comma(new_parent);
            return true;
        }
        return restore(saved, parent);
    }

    // NOLINTNEXTLINE(misc-no-recursion): the spec grammar nests, as in ScummVM.
    bool parse_expr(int parent) {
        const Mark saved = mark(parent);
        const int new_node = branch_node(next_node(), null_node, null_node);
        bool found_list = false;
        int new_parent = parent;
        if (parse_list(new_node)) {
            found_list = true;
            attach_subtree(new_parent, 0x141, 0x14F, new_node);
            new_parent = node(new_parent).right;
        }
        if (parse_ref(new_parent) || found_list) {
            return true;
        }
        return restore(saved, parent);
    }

    bool parse_spec(int parent) {
        const Mark saved = mark(parent);
        int new_node = branch_node(next_node(), null_node, null_node);
        bool found_any = false;
        int new_parent = parent;
        if (parse_expr(new_node)) {
            attach_subtree(new_parent, 0x141, 0x149, new_node);
            new_parent = node(new_parent).right;
            found_any = true;
        }
        bool nonempty = false;
        if (parse_part(new_parent, nonempty, 0x142)) {
            found_any = true;
            if (nonempty) {
                new_parent = node(new_parent).right;
            }
            if (parse_part(new_parent, nonempty, 0x143) && nonempty) {
                new_parent = node(new_parent).right;
            }
        }
        if (token() == token_gt) {
            ++token_;
            const int outer = next_node();
            new_node = branch_node(outer, null_node, leaf_node(next_node(), token_gt));
            attach_subtree(new_parent, 0x14B, token_gt, new_node);
        }
        if (found_any) {
            return true;
        }
        return restore(saved, parent);
    }

    std::vector<int> tokens_;
    std::size_t token_ = 0;
    std::vector<ParseNode> tree_;
    int tree_pos_ = said_tree_start;
};

// The matching of a parse tree against a Said tree (augment_parse_nodes).
class Matcher {
public:
    Matcher(std::span<const ParseNode> parse, std::span<const ParseNode> said) : parse_{parse}, said_{said} {}

    // augment_parse_nodes.
    int augment() {
        dont_claim_ = false;
        if (match_trees(0, 0) != 1) {
            return 0;
        }
        return dont_claim_ ? said_partial_match : 1;
    }

private:
    // matchTrees.
    // NOLINTNEXTLINE(misc-no-recursion,readability-function-cognitive-complexity): kept close to matchTrees.
    int match_trees(int parse_t, int said_t) {
        const bool in_paren = said_.minor(said_t) == 0x14F || said_.minor(said_t) == 0x150;
        const bool in_bracket = said_.major(said_t) == 0x152;
        int ret = 0;
        if (parse_.major(parse_t) != 0x141 && said_.major(said_t) != 0x141 && said_.major(said_t) != 0x152 &&
            said_.major(said_t) != parse_.major(parse_t)) {
            ret = -1;
        } else if (said_.is_terminal(said_t) && parse_.is_terminal(parse_t)) {
            const int said_value = said_.terminal_value(said_t);
            if (said_value == word_none) {
                ret = -1;
            } else if (said_value == word_any) {
                ret = 1;
            } else {
                ret = -1;
                int word = parse_.right(parse_.right(parse_t));
                do {
                    const int parse_value = parse_.at(word).value;
                    if (parse_value == word_any || parse_value == said_value) {
                        ret = 1;
                        break;
                    }
                    word = parse_.right(word);
                } while (word >= 0);
            }
        } else if (said_.is_terminal(said_t) && !parse_.is_terminal(parse_t)) {
            if (parse_.major(parse_t) == 0x141 || parse_.major(parse_t) == said_.major(said_t)) {
                ret = scan_parse_children(parse_.right(parse_.right(parse_t)), said_t);
            } else {
                ret = 0;
            }
        } else if (parse_.is_terminal(parse_t)) {
            if (said_.major(said_t) == 0x141 || said_.major(said_t) == 0x152 ||
                said_.major(said_t) == parse_.major(parse_t)) {
                ret = scan_said_children(parse_t, said_.right(said_.right(said_t)), in_paren);
            } else {
                ret = 0;
            }
        } else if (said_.major(said_t) != 0x141 && said_.major(said_t) != 0x152 &&
                   said_.major(said_t) != parse_.major(parse_t)) {
            ret = scan_parse_children(parse_.right(parse_.right(parse_t)), said_t);
        } else {
            ret = scan_said_children(parse_.right(parse_.right(parse_t)), said_.right(said_.right(said_t)), in_paren);
        }
        if (in_bracket && ret == 0) {
            ret = 1;
        }
        return ret;
    }

    // scanSaidChildren: AND, or OR inside parentheses.
    // NOLINTNEXTLINE(misc-no-recursion): the trees nest.
    int scan_said_children(int parse_t, int said_t, bool any) {
        int ret = 1;
        while (said_t >= 0) {
            const int said_child = said_.left(said_t);
            if (said_.major(said_child) != 0x145) {
                ret = scan_parse_children(parse_t, said_child);
                if (!any && ret != 1) {
                    break;
                }
                if (any && ret == 1) {
                    break;
                }
            }
            said_t = said_.right(said_t);
        }
        return ret;
    }

    // scanParseChildren.
    // NOLINTNEXTLINE(misc-no-recursion): the trees nest.
    int scan_parse_children(int parse_t, int said_t) {
        if (said_.major(said_t) == 0x14B) {
            dont_claim_ = true;
            return 1;
        }
        const bool in_paren = said_.minor(said_t) == 0x14F || said_.minor(said_t) == 0x150;
        const bool in_bracket = said_.major(said_t) == 0x152;
        int ret = 0;
        if ((said_.major(said_t) == 0x141 || said_.major(said_t) == 0x152) && !said_.is_terminal(said_t)) {
            ret = scan_said_children(parse_t, said_.right(said_.right(said_t)), in_paren);
        } else if (parse_t >= 0 && parse_.at(parse_.left(parse_t)).type == NodeType::Branch) {
            ret = 0;
            int subresult = 0;
            while (parse_t >= 0) {
                const int parse_child = parse_.left(parse_t);
                if (parse_.major(parse_child) == said_.major(said_t) || parse_.major(parse_child) == 0x141) {
                    subresult = match_trees(parse_child, said_t);
                }
                if (subresult != 0) {
                    ret = subresult;
                }
                if (ret == 1) {
                    break;
                }
                parse_t = parse_.right(parse_t);
            }
        } else {
            ret = match_trees(parse_t, said_t);
        }
        if (in_bracket && ret == 0) {
            ret = 1;
        }
        return ret;
    }

    TreeView parse_;
    TreeView said_;
    bool dont_claim_ = false;
};

} // namespace

int said(std::span<const std::uint8_t> spec, std::span<const ParseNode> parse_tree,
         std::vector<std::string>* warnings) {
    // said_parse_spec: words take two bytes, operators one; bytes past the
    // end read as the terminator.
    std::vector<int> tokens;
    std::size_t at = 0;
    const auto next_byte = [&]() -> int { return at < spec.size() ? spec[at++] : said_term; };
    int item = 0;
    do {
        item = next_byte();
        if (item < said_first) {
            tokens.push_back((item << 8) | next_byte());
        } else {
            tokens.push_back(item << 8);
        }
    } while (item != said_term && tokens.size() < max_said_tokens);
    if (item != said_term) {
        if (warnings != nullptr) {
            warnings->emplace_back("SAID spec is too long");
        }
        return said_no_match;
    }
    SaidParser parser(std::move(tokens));
    if (!parser.build()) {
        if (warnings != nullptr) {
            warnings->emplace_back("Error while parsing SAID spec");
        }
        return said_no_match;
    }
    Matcher matcher(parse_tree, parser.tree());
    const int result = matcher.augment();
    if (result == 0) {
        return said_no_match;
    }
    return result == said_partial_match ? said_partial_match : said_full_match;
}

} // namespace sci::text
