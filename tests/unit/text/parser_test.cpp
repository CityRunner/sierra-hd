// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/text/parser.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using sci::text::ParseNode;
using sci::text::ResultWord;
using sci::text::Sentence;
using sci::text::Vocabulary;

namespace {

constexpr int group_look = 0x10;
constexpr int group_take = 0x11;
constexpr int group_lock = 0x12;
constexpr int group_door = 0x20;
constexpr int group_key = 0x21;
constexpr int group_it = 0x30;

constexpr int verb = sci::text::class_imperative_verb << 4;
constexpr int noun = sci::text::class_noun << 4;
constexpr int pronoun = sci::text::class_pronoun << 4;

// One vocab.000 entry: the characters kept from the previous word, the
// rest of the word (the last character has bit 7 set), then the class and
// group in 12 bits each.
void add_word(std::vector<std::uint8_t>& vocab, std::uint8_t kept, std::string_view rest, int word_class, int group) {
    vocab.push_back(kept);
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const auto c = static_cast<std::uint8_t>(rest[i]);
        vocab.push_back(i + 1 == rest.size() ? static_cast<std::uint8_t>(c | 0x80U) : c);
    }
    vocab.push_back(static_cast<std::uint8_t>(word_class >> 4));
    vocab.push_back(static_cast<std::uint8_t>(((word_class & 0xF) << 4) | (group >> 8)));
    vocab.push_back(static_cast<std::uint8_t>(group & 0xFF));
}

std::vector<std::uint8_t> sample_words() {
    std::vector<std::uint8_t> vocab(26 * 2, 0);
    vocab[0] = 1; // Any non-zero letter pointer: not the SCI01 format.
    add_word(vocab, 0, "door", noun, group_door);
    add_word(vocab, 0, "it", pronoun, group_it);
    add_word(vocab, 0, "key", noun, group_key);
    add_word(vocab, 0, "look", verb, group_look);
    add_word(vocab, 2, "ck", verb, group_lock); // "lo" + "ck".
    add_word(vocab, 0, "take", verb, group_take);
    return vocab;
}

// vocab.901: '*' alt NUL class '*' word NUL mask, repeated.
void add_suffix(std::vector<std::uint8_t>& suffixes, std::string_view alt, int result_class, std::string_view word,
                int mask) {
    suffixes.insert(suffixes.end(), alt.begin(), alt.end());
    suffixes.push_back(0);
    suffixes.push_back(static_cast<std::uint8_t>(result_class >> 8));
    suffixes.push_back(static_cast<std::uint8_t>(result_class & 0xFF));
    suffixes.push_back('*');
    suffixes.insert(suffixes.end(), word.begin(), word.end());
    suffixes.push_back(0);
    suffixes.push_back(static_cast<std::uint8_t>(mask >> 8));
    suffixes.push_back(static_cast<std::uint8_t>(mask & 0xFF));
    suffixes.push_back('*');
}

std::vector<std::uint8_t> sample_suffixes() {
    std::vector<std::uint8_t> suffixes{'*'};
    add_suffix(suffixes, "s", noun, "", noun);
    add_suffix(suffixes, "ing", verb, "", verb);
    add_suffix(suffixes, "ies", noun, "y", noun); // Matches nothing here.
    return suffixes;
}

// vocab.900: 20-byte branches, an id and up to 9 little-endian words
// (pairs of type and value).
void add_branch(std::vector<std::uint8_t>& branches, int id, std::initializer_list<int> data) {
    const auto put = [&](int value) {
        branches.push_back(static_cast<std::uint8_t>(value & 0xFF));
        branches.push_back(static_cast<std::uint8_t>(value >> 8));
    };
    put(id);
    std::size_t count = 0;
    for (const int value : data) {
        put(value);
        ++count;
    }
    for (; count < 9; ++count) {
        put(0);
    }
}

// A sentence is a verb, optionally followed by a noun:
//   0x12f -> (141 149 <0x149>) (142 14a <0x14a>) | (141 149 <0x149>)
//   0x149 -> (141 14f <0x14f>);  0x14a -> (141 14f <0x14f>)
//   0x14f -> a verb | a noun
// which gives the trees Said builds: (141 13f (141 149 (141 14f look))
// (142 14a (141 14f door))).
std::vector<std::uint8_t> sample_branches() {
    std::vector<std::uint8_t> branches;
    add_branch(branches, 0x13f, {0, 0x12f});
    add_branch(branches, 0x12f, {0x141, 0x149, 0x142, 0x14a});
    add_branch(branches, 0x12f, {0x141, 0x149});
    add_branch(branches, 0x149, {0x141, 0x14f});
    add_branch(branches, 0x14a, {0x141, 0x14f});
    add_branch(branches, 0x14f, {0x146, verb});
    add_branch(branches, 0x14f, {0x146, noun});
    add_branch(branches, 0, {}); // The empty rule some vocabularies end with.
    return branches;
}

Vocabulary sample_vocabulary(sci::text::ParserOptions options = {}) {
    const std::vector<std::uint8_t> words = sample_words();
    const std::vector<std::uint8_t> suffixes = sample_suffixes();
    const std::vector<std::uint8_t> branches = sample_branches();
    return Vocabulary(words, suffixes, branches, options);
}

std::optional<Sentence> tokenize(const Vocabulary& vocabulary, std::string_view text) {
    std::string error;
    return vocabulary.tokenize(text, error);
}

// Tokenizes and parses; the parse tree, or nothing.
std::optional<std::vector<ParseNode>> parse(Vocabulary& vocabulary, std::string_view text) {
    std::optional<Sentence> sentence = tokenize(vocabulary, text);
    if (!sentence) {
        return std::nullopt;
    }
    vocabulary.replace_pronouns(*sentence);
    if (!vocabulary.parse(*sentence)) {
        return std::nullopt;
    }
    vocabulary.store_pronoun_reference();
    const auto nodes = vocabulary.nodes();
    return std::vector<ParseNode>(nodes.begin(), nodes.end());
}

int said(std::initializer_list<std::uint8_t> spec, std::span<const ParseNode> tree) {
    const std::vector<std::uint8_t> bytes(spec);
    return sci::text::said(bytes, tree);
}

// Said spec bytes: a word group, and the operators.
constexpr std::uint8_t comma = 0xF0;
constexpr std::uint8_t slash = 0xF2;
constexpr std::uint8_t bracket_open = 0xF5;
constexpr std::uint8_t bracket_close = 0xF6;
constexpr std::uint8_t greater = 0xF9;
constexpr std::uint8_t end = 0xFF;

} // namespace

SCI_TEST(parser_words_are_found_with_shared_prefixes) {
    const Vocabulary vocabulary = sample_vocabulary();
    SCI_CHECK(vocabulary.warnings().empty());
    SCI_CHECK(vocabulary.lookup_word("look") == sci::text::WordMeanings{ResultWord{verb, group_look}});
    SCI_CHECK(vocabulary.lookup_word("lock") == sci::text::WordMeanings{ResultWord{verb, group_lock}});
    SCI_CHECK(vocabulary.lookup_word("door") == sci::text::WordMeanings{ResultWord{noun, group_door}});
    SCI_CHECK(vocabulary.lookup_word("lo-ok") == sci::text::WordMeanings{ResultWord{verb, group_look}});
    SCI_CHECK(vocabulary.lookup_word("xyzzy").empty());
}

SCI_TEST(parser_suffixes_map_to_the_stem_with_the_class_mask) {
    const Vocabulary vocabulary = sample_vocabulary();
    SCI_CHECK(vocabulary.suffixes().size() == 3);
    SCI_CHECK(vocabulary.lookup_word("doors") == sci::text::WordMeanings{ResultWord{noun, group_door}});
    SCI_CHECK(vocabulary.lookup_word("looking") == sci::text::WordMeanings{ResultWord{verb, group_look}});
    // "-ing" applies to verbs only, "-s" to nouns only.
    SCI_CHECK(vocabulary.lookup_word("dooring").empty());
    SCI_CHECK(vocabulary.lookup_word("looks").empty());
}

SCI_TEST(parser_numbers_are_words_except_in_early_sci0) {
    const Vocabulary vocabulary = sample_vocabulary();
    SCI_CHECK(vocabulary.lookup_word("42") == sci::text::WordMeanings{ResultWord{noun, sci::text::magic_number_group}});
    SCI_CHECK(vocabulary.lookup_word("4x").empty());
    const Vocabulary early = sample_vocabulary({.sci0 = true, .sci0_early = true});
    SCI_CHECK(early.lookup_word("42").empty());
}

SCI_TEST(parser_tokenize_lowers_and_skips_punctuation) {
    const Vocabulary vocabulary = sample_vocabulary();
    std::string error;
    const auto sentence = vocabulary.tokenize("LOOK,  d'oor!", error);
    SCI_CHECK(sentence && error.empty());
    if (sentence) {
        SCI_CHECK(sentence->size() == 2);
        SCI_CHECK(sentence->size() == 2 && (*sentence)[1] == sci::text::WordMeanings{ResultWord{noun, group_door}});
    }
    const auto unknown = vocabulary.tokenize("look Xyzzy", error);
    SCI_CHECK(!unknown && error == "xyzzy");
    const auto empty = vocabulary.tokenize(" ?! ", error);
    SCI_CHECK(empty && empty->empty() && error.empty());
}

SCI_TEST(parser_synonyms_replace_groups) {
    const Vocabulary vocabulary = sample_vocabulary();
    auto sentence = tokenize(vocabulary, "take key");
    SCI_CHECK(sentence.has_value());
    if (sentence) {
        const std::vector<sci::text::Synonym> synonyms{{.replaceant = group_key, .replacement = group_door}};
        Vocabulary::synonymize(*sentence, synonyms);
        SCI_CHECK((*sentence)[0][0].group == group_take && (*sentence)[1][0].group == group_door);
    }
}

SCI_TEST(parser_builds_the_tree_said_expects) {
    Vocabulary vocabulary = sample_vocabulary();
    SCI_CHECK(vocabulary.has_rules() && vocabulary.branches().size() == 7);
    const auto tree = parse(vocabulary, "look door");
    SCI_CHECK(tree.has_value());
    if (tree) {
        const std::vector<ParseNode>& t = *tree;
        // (141 13f ...)
        SCI_CHECK(t[0].type == sci::text::NodeType::Branch && t[1].value == 0x141 && t[3].value == 0x13f);
        SCI_CHECK(said({0, group_look, slash, 0, group_door, end}, t) == sci::text::said_full_match);
    }
    SCI_CHECK(parse(vocabulary, "look").has_value());
    SCI_CHECK(!parse(vocabulary, "look door door").has_value());
    // As in ScummVM, no words leave the start rules as the result; kParse
    // never parses an empty sentence.
    SCI_CHECK(vocabulary.parse(Sentence{}));
}

SCI_TEST(parser_pronouns_refer_to_the_last_object) {
    Vocabulary vocabulary = sample_vocabulary();
    SCI_CHECK(parse(vocabulary, "look door").has_value());
    auto sentence = tokenize(vocabulary, "take it");
    SCI_CHECK(sentence.has_value());
    if (sentence) {
        vocabulary.replace_pronouns(*sentence);
        SCI_CHECK((*sentence)[1] == sci::text::WordMeanings{ResultWord{noun, group_door}});
    }
    const auto tree = parse(vocabulary, "take it");
    SCI_CHECK(tree && said({0, group_take, slash, 0, group_door, end}, *tree) == sci::text::said_full_match);
}

SCI_TEST(parser_said_matches_words_and_operators) {
    Vocabulary vocabulary = sample_vocabulary();
    const auto look = parse(vocabulary, "look");
    const auto look_door = parse(vocabulary, "look door");
    const auto take_door = parse(vocabulary, "take door");
    SCI_CHECK(look && look_door && take_door);
    if (!look || !look_door || !take_door) {
        return;
    }
    using sci::text::said_full_match;
    using sci::text::said_no_match;
    using sci::text::said_partial_match;
    SCI_CHECK(said({0, group_look, end}, *look) == said_full_match);
    SCI_CHECK(said({0, group_take, end}, *look) == said_no_match);
    SCI_CHECK(said({0, group_look, comma, 0, group_take, end}, *look) == said_full_match);
    SCI_CHECK(said({0, group_look, slash, 0, group_door, end}, *look) == said_no_match);
    SCI_CHECK(said({0, group_look, slash, 0, group_key, end}, *look_door) == said_no_match);
    SCI_CHECK(said({0, group_look, slash, 0, group_door, end}, *take_door) == said_no_match);
    SCI_CHECK(said({slash, 0, group_door, end}, *take_door) == said_full_match);
    // An optional object: "look[/door]".
    SCI_CHECK(said({0, group_look, bracket_open, slash, 0, group_door, bracket_close, end}, *look) == said_full_match);
    SCI_CHECK(said({0, group_look, bracket_open, slash, 0, group_door, bracket_close, end}, *look_door) ==
              said_full_match);
    // Any word: 0x0fff.
    SCI_CHECK(said({0x0F, 0xFF, slash, 0, group_door, end}, *take_door) == said_full_match);
    // '>' leaves the event unclaimed.
    SCI_CHECK(said({0, group_look, greater, end}, *look_door) == said_partial_match);
}

SCI_TEST(parser_said_rejects_broken_specs) {
    Vocabulary vocabulary = sample_vocabulary();
    const auto look = parse(vocabulary, "look");
    SCI_CHECK(look.has_value());
    if (!look) {
        return;
    }
    std::vector<std::string> warnings;
    const std::vector<std::uint8_t> broken{bracket_close, 0, group_look, end};
    SCI_CHECK(sci::text::said(broken, *look, &warnings) == sci::text::said_no_match);
    SCI_CHECK(warnings.size() == 1);
    // A missing terminator reads as one.
    const std::vector<std::uint8_t> unterminated{0, group_look};
    SCI_CHECK(sci::text::said(unterminated, *look) == sci::text::said_full_match);
    const std::vector<std::uint8_t> too_long(300, comma);
    SCI_CHECK(sci::text::said(too_long, *look) == sci::text::said_no_match);
}

SCI_TEST(parser_broken_vocabularies_disable_the_parser) {
    const std::vector<std::uint8_t> tiny(10, 1);
    const Vocabulary small(tiny, std::nullopt, std::nullopt);
    SCI_CHECK(!small.has_rules() && small.warnings().size() == 1 && small.lookup_word("look").empty());

    std::vector<std::uint8_t> truncated = sample_words();
    truncated.resize(truncated.size() - 4); // Cuts into the last word.
    const std::vector<std::uint8_t> branches = sample_branches();
    const Vocabulary cut(truncated, std::nullopt, branches);
    SCI_CHECK(!cut.has_rules() && !cut.warnings().empty());

    const Vocabulary no_branches(sample_words(), sample_suffixes(), std::nullopt);
    SCI_CHECK(!no_branches.has_rules() && no_branches.lookup_word("doors").size() == 1);

    // 0x14f -> (141 14f <0x14f>) | a verb nests forever: ScummVM would
    // expand it until memory runs out.
    std::vector<std::uint8_t> recursive;
    add_branch(recursive, 0x13f, {0, 0x14f});
    add_branch(recursive, 0x14f, {0x141, 0x14f, 0x146, verb});
    add_branch(recursive, 0x14f, {0x146, verb});
    const Vocabulary endless(sample_words(), std::nullopt, recursive);
    SCI_CHECK(!endless.has_rules() && endless.warnings().size() == 1);

    const std::vector<std::uint8_t> zeros(26 * 2, 0);
    const Vocabulary disguised(zeros, std::nullopt, std::nullopt);
    SCI_CHECK(!disguised.warnings().empty());
}
