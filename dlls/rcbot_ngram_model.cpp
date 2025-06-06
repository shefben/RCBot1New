#include "rcbot_ngram_model.h"
#include <sstream>      // For std::istringstream
#include <algorithm>    // For std::transform, std::remove_if
#include <random>       // For std::mt19937, std::uniform_int_distribution
#include <vector>       // Used by tokenize and generateSentence's weighted choice
#include <cctype>       // For std::tolower, std::ispunct

// Constructor
RCBotNgramBase::RCBotNgramBase(int ngramSize) : m_ngramSize(std::max(2, ngramSize)) {
    // Ensure N-gram size is at least 2 (bigrams)
}

// Clears the N-gram model data
void RCBotNgramBase::clearModel() {
    m_ngramTable.clear();
}

// Normalizes a single word (lowercase, remove basic punctuation from ends)
std::string RCBotNgramBase::normalizeWord(const std::string& word) const {
    if (word.empty()) {
        return "";
    }
    std::string normalized = word;

    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Remove punctuation from the beginning
    normalized.erase(normalized.begin(), std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch) {
        return !std::ispunct(ch);
    }));

    // Remove punctuation from the end
    normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch) {
        return !std::ispunct(ch);
    }).base(), normalized.end());

    return normalized;
}

// Splits text into words and normalizes them
std::vector<std::string> RCBotNgramBase::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::istringstream tokenStream(text);
    std::string token;

    while (tokenStream >> token) {
        std::string normalized = normalizeWord(token);
        if (!normalized.empty()) {
            tokens.push_back(normalized);
        }
    }
    return tokens;
}

// Builds the N-gram model from training text
void RCBotNgramBase::buildModel(const std::string& training_text) {
    clearModel();
    std::vector<std::string> words = tokenize(training_text);

    if (words.size() < m_ngramSize) {
        return; // Not enough words to build the model
    }

    std::deque<std::string> prefix;

    // Initialize the first prefix with start tokens
    for (int i = 0; i < m_ngramSize - 1; ++i) {
        prefix.push_back(NGRAM_START_TOKEN);
    }

    for (const std::string& currentWord : words) {
        m_ngramTable[prefix][currentWord]++;
        prefix.pop_front();
        prefix.push_back(currentWord);
    }

    // Optionally, add an end token to the last prefix if desired
    // m_ngramTable[prefix][NGRAM_END_TOKEN]++;
}

// Generates a sentence using the built N-gram model
std::string RCBotNgramBase::generateSentence(const std::string& seed_prefix_str, int max_words) {
    if (m_ngramTable.empty()) {
        return "Error: Model not built.";
    }

    std::deque<std::string> currentPrefix;
    std::vector<std::string> sentenceWords;

    // Initialize prefix
    if (!seed_prefix_str.empty()) {
        std::vector<std::string> seedTokens = tokenize(seed_prefix_str);
        for(const auto& token : seedTokens) {
            if (currentPrefix.size() >= (size_t)m_ngramSize - 1) {
                currentPrefix.pop_front();
            }
            currentPrefix.push_back(token);
        }
        // Ensure prefix is the correct size, padding with start tokens if necessary
        while (currentPrefix.size() < (size_t)m_ngramSize - 1 && m_ngramSize > 1) {
            currentPrefix.push_front(NGRAM_START_TOKEN);
        }
        // Add seed words to sentence if they are actual words
        for(const auto& token : seedTokens) sentenceWords.push_back(token);

    } else {
        // Start with a prefix that begins with NGRAM_START_TOKENs
        // Find a random prefix that starts with NGRAM_START_TOKEN or just any random if none.
        std::vector<std::deque<std::string>> possibleStartPrefixes;
        for(const auto& pair : m_ngramTable) {
            if (m_ngramSize > 1 && !pair.first.empty() && pair.first.front() == NGRAM_START_TOKEN) {
                possibleStartPrefixes.push_back(pair.first);
            }
        }
        if (!possibleStartPrefixes.empty()) {
             std::random_device rd;
             std::mt19937 gen(rd());
             std::uniform_int_distribution<> distrib(0, possibleStartPrefixes.size() - 1);
             currentPrefix = possibleStartPrefixes[distrib(gen)];
        } else if (!m_ngramTable.empty()) { // Fallback: pick any random prefix
             std::random_device rd;
             std::mt19937 gen(rd());
             std::uniform_int_distribution<> distrib(0, m_ngramTable.size() - 1);
             auto it = m_ngramTable.begin();
             std::advance(it, distrib(gen));
             currentPrefix = it->first;
        } else {
            return "Error: Model has no data to start sentence."; // Should not happen if m_ngramTable is not empty
        }

        // Add words from the chosen starting prefix to the sentence, if they are not start tokens
        for(const std::string& word : currentPrefix) {
            if (word != NGRAM_START_TOKEN) {
                sentenceWords.push_back(word);
            }
        }
    }
     // Ensure prefix is correct size for model lookup, if not seeded or seed was too short
    while (currentPrefix.size() < (size_t)m_ngramSize - 1 && m_ngramSize > 1) {
        currentPrefix.push_front(NGRAM_START_TOKEN);
    }


    // Generate words
    for (int i = 0; sentenceWords.size() < (size_t)max_words; ++i) {
        auto it = m_ngramTable.find(currentPrefix);
        if (it == m_ngramTable.end() || it->second.empty()) {
            break; // No known successors for this prefix
        }

        const std::map<std::string, int>& successors = it->second;
        std::vector<std::string> weightedSuccessors;
        for (const auto& pair : successors) {
            for (int k = 0; k < pair.second; ++k) {
                weightedSuccessors.push_back(pair.first);
            }
        }

        if (weightedSuccessors.empty()) {
            break; // Should not happen if successors map was not empty
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, weightedSuccessors.size() - 1);
        std::string nextWord = weightedSuccessors[distrib(gen)];

        if (nextWord == NGRAM_END_TOKEN) { // Optional: handle end token
            break;
        }

        sentenceWords.push_back(nextWord);

        if (m_ngramSize > 1) {
            currentPrefix.pop_front();
            currentPrefix.push_back(nextWord);
        } else { // For unigram model (m_ngramSize=1, though current code enforces min 2)
            // Prefix remains empty or is handled differently.
            // For now, this case is less relevant as we enforce m_ngramSize >= 2.
        }
    }

    // Join words into a sentence
    std::ostringstream sentenceStream;
    for (size_t i = 0; i < sentenceWords.size(); ++i) {
        sentenceStream << sentenceWords[i] << (i == sentenceWords.size() - 1 ? "" : " ");
    }
    return sentenceStream.str();
}
