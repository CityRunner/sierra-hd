// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 text parser: the vocabulary (words, suffixes, grammar branches),
// tokenizing typed commands, building the parse tree, and matching it
// against Said specs. Ported from ScummVM's engines/sci/parser (see
// src/vocabulary.cpp, grammar.cpp and said.cpp for the licence and
// revision); the VM side (kParse, kSaid) is in sci-vm.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::text {

// Word classes (VOCAB_CLASS_*), as stored shifted left by 4 in ResultWord.
inline constexpr int class_preposition = 0x01;
inline constexpr int class_article = 0x02;
inline constexpr int class_adjective = 0x04;
inline constexpr int class_pronoun = 0x08;
inline constexpr int class_noun = 0x10;
inline constexpr int class_indicative_verb = 0x20;
inline constexpr int class_adverb = 0x40;
inline constexpr int class_imperative_verb = 0x80;

inline constexpr int magic_number_group = 0xffd; // VOCAB_MAGIC_NUMBER_GROUP
inline constexpr int no_pronoun_reference = 0x1000;

// Said results (SAID_*).
inline constexpr int said_full_match = 0xffff;
inline constexpr int said_no_match = 0xfffe;
inline constexpr int said_partial_match = 0xfffd;

struct ResultWord {
    int word_class = 0; // _class
    int group = 0;      // _group
    friend bool operator==(const ResultWord&, const ResultWord&) = default;
};

// The meanings of one word (ResultWordList), and a tokenized sentence
// (ResultWordListList).
using WordMeanings = std::vector<ResultWord>;
using Sentence = std::vector<WordMeanings>;

struct Suffix {
    int class_mask = 0;
    int result_class = 0;
    std::string alt_suffix;  // As typed.
    std::string word_suffix; // As in the vocabulary.
};

struct Synonym {
    std::uint16_t replaceant = 0;
    std::uint16_t replacement = 0;
};

struct ParseTreeBranch {
    int id = 0;
    std::array<int, 10> data{};
};

enum class NodeType : std::uint8_t { Word = 4, Leaf = 5, Branch = 6 };

// A node of a parse or Said tree; children are indices into the same
// tree, -1 for none.
struct ParseNode {
    NodeType type = NodeType::Leaf;
    int value = 0;
    int left = -1;
    int right = -1;
};

inline constexpr std::size_t tree_nodes = 500; // VOCAB_TREE_NODES

struct ParseRule;

// The interpreter versions whose parser behaviour differs.
struct ParserOptions {
    bool sci0 = true;        // < SCI_VERSION_01: one meaning per word, first suffix match only.
    bool sci0_early = false; // SCI_VERSION_0_EARLY: no numbers.
};

class Vocabulary {
public:
    // Vocabulary::Vocabulary for SCI0: vocab.000 (words), 901 (suffixes) and
    // 900 (branches), any of which may be missing (then there is no
    // parser). Problems ScummVM warns about are in warnings().
    Vocabulary(std::optional<std::span<const std::uint8_t>> words,
               std::optional<std::span<const std::uint8_t>> suffixes,
               std::optional<std::span<const std::uint8_t>> branches, ParserOptions options = {});
    ~Vocabulary();
    Vocabulary(const Vocabulary&) = delete;
    Vocabulary& operator=(const Vocabulary&) = delete;
    Vocabulary(Vocabulary&&) noexcept;
    Vocabulary& operator=(Vocabulary&&) noexcept;

    [[nodiscard]] bool has_rules() const { return !rules_.empty(); }

    [[nodiscard]] const std::vector<std::string>& warnings() const { return warnings_; }

    // The words of vocab.000 and their meanings, by spelling.
    [[nodiscard]] const std::map<std::string, WordMeanings, std::less<>>& words() const { return words_; }

    [[nodiscard]] const std::vector<Suffix>& suffixes() const { return suffixes_; }

    [[nodiscard]] const std::vector<ParseTreeBranch>& branches() const { return branches_; }

    // Vocabulary::lookupWord.
    [[nodiscard]] WordMeanings lookup_word(std::string_view word) const;
    // Vocabulary::tokenizeString: the words, or nothing with the unknown
    // word in `error` (empty if the sentence had no words at all, which is
    // not an error: an empty sentence is returned then).
    [[nodiscard]] std::optional<Sentence> tokenize(std::string_view sentence, std::string& error) const;
    static void synonymize(Sentence& sentence, std::span<const Synonym> synonyms);
    void replace_pronouns(Sentence& sentence) const;
    // Vocabulary::parseGNF: true if the sentence parses; the tree is then in
    // nodes().
    bool parse(const Sentence& sentence);
    // Vocabulary::storePronounReference, after a successful parse.
    bool store_pronoun_reference();

    [[nodiscard]] std::span<const ParseNode> nodes() const { return nodes_; }

private:
    bool load_words(std::span<const std::uint8_t> data);
    void load_suffixes(std::span<const std::uint8_t> data);
    bool load_branches(std::span<const std::uint8_t> data);
    void build_gnf();

    ParserOptions options_;
    std::map<std::string, WordMeanings, std::less<>> words_;
    std::vector<Suffix> suffixes_;
    std::vector<ParseTreeBranch> branches_;
    std::vector<ParseRule> rules_; // The GNF rules.
    std::vector<ParseNode> nodes_;
    int pronoun_reference_ = no_pronoun_reference;
    std::vector<std::string> warnings_;
};

// said(): matches a Said spec (bytes up to 0xff) against the parse tree of
// the last valid parse. Returns said_full_match, said_partial_match or
// said_no_match. Bytes past the end of `spec` read as the terminator.
[[nodiscard]] int said(std::span<const std::uint8_t> spec, std::span<const ParseNode> parse_tree,
                       std::vector<std::string>* warnings = nullptr);

} // namespace sci::text
