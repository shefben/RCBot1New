#ifndef RCBOT_NGRAM_MODEL_H
#define RCBOT_NGRAM_MODEL_H

#include <string>
#include <vector>
#include <deque>
#include <map>

// Define special tokens for padding sentences if needed
const std::string NGRAM_START_TOKEN = "<S>";
const std::string NGRAM_END_TOKEN = "</S>"; // Not explicitly used in this basic buildModel yet

class RCBotNgramBase {
public:
    RCBotNgramBase(int ngramSize = 3);

    // Builds the N-gram model from a given corpus of text.
    void buildModel(const std::string& training_text);

    // Generates a sentence using the built N-gram model.
    // seed_prefix_str: An optional string to seed the generation.
    // max_words: Maximum number of words in the generated sentence.
    std::string generateSentence(const std::string& seed_prefix_str = "", int max_words = 20);

    // Clears the existing N-gram model data.
    void clearModel();

    int getNgramSize() const { return m_ngramSize; }

private:
    int m_ngramSize;
    // m_ngramTable: Key is a prefix (deque of N-1 words), Value is a map of (next word -> count)
    std::map<std::deque<std::string>, std::map<std::string, int>> m_ngramTable;

    // Helper to split text into words and normalize them.
    std::vector<std::string> tokenize(const std::string& text) const;

    // Helper to normalize a single word (lowercase, remove basic punctuation).
    std::string normalizeWord(const std::string& word) const;
};

#endif // RCBOT_NGRAM_MODEL_H
