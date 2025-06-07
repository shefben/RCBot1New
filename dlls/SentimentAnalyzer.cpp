#include "SentimentAnalyzer.h"
#include <sstream>      // For std::istringstream
#include <algorithm>    // For std::transform, std::remove_if
#include <cctype>       // For std::tolower, std::ispunct
// No need for <vector>, <set>, <map> here as they are included via SentimentAnalyzer.h

SentimentAnalyzer::SentimentAnalyzer() {
    loadDefaultLexicon();
}

void SentimentAnalyzer::loadDefaultLexicon() {
    m_lexicon.clear();
    m_negationWords.clear();

    // Positive words
    m_lexicon["good"] = 0.6f;
    m_lexicon["great"] = 0.8f;
    m_lexicon["awesome"] = 0.9f;
    m_lexicon["nice"] = 0.5f;
    m_lexicon["cool"] = 0.4f;
    m_lexicon["love"] = 0.7f;
    m_lexicon["happy"] = 0.7f;
    m_lexicon["glad"] = 0.6f;
    m_lexicon["thanks"] = 0.4f;
    m_lexicon["thank"] = 0.4f; // "thank you"
    m_lexicon["gg"] = 0.5f;     // good game
    m_lexicon["gj"] = 0.5f;     // good job
    m_lexicon["yes"] = 0.3f;
    m_lexicon["win"] = 0.8f;
    m_lexicon["winner"] = 0.8f;
    m_lexicon["lol"] = 0.3f; // Can be positive in some contexts
    m_lexicon["haha"] = 0.3f;
    m_lexicon["yay"] = 0.7f;
    m_lexicon["perfect"] = 1.0f;
    m_lexicon["excellent"] = 0.9f;

    // Negative words
    m_lexicon["bad"] = -0.6f;
    m_lexicon["terrible"] = -0.8f;
    m_lexicon["awful"] = -0.9f;
    m_lexicon["sucks"] = -0.7f;
    m_lexicon["sux"] = -0.7f;
    m_lexicon["hate"] = -0.7f;
    m_lexicon["noob"] = -0.5f;
    m_lexicon["n00b"] = -0.5f;
    m_lexicon["stupid"] = -0.6f;
    m_lexicon["idiot"] = -0.7f;
    m_lexicon["sad"] = -0.5f;
    m_lexicon["cry"] = -0.4f;
    m_lexicon["omg"] = -0.1f; // Can be negative (frustration)
    m_lexicon["wtf"] = -0.4f;
    m_lexicon["damn"] = -0.3f;
    m_lexicon["shit"] = -0.5f;
    m_lexicon["hell"] = -0.3f;
    m_lexicon["lose"] = -0.8f;
    m_lexicon["loser"] = -0.8f;
    m_lexicon["dead"] = -0.3f; // Contextual, but often in negative situations
    m_lexicon["kill"] = -0.2f; // Context dependent, slightly negative for general chat analysis

    // Neutral words that might affect context but not strongly scored themselves
    m_lexicon["help"] = 0.1f; // Slightly positive if it's a request being fulfilled
    m_lexicon["sorry"] = 0.0f; // Could be positive (apology) or negative (regret) contextually

    // Negation words
    m_negationWords.insert("not");
    m_negationWords.insert("no");
    m_negationWords.insert("never");
    m_negationWords.insert("don't"); // "do not" - simple tokenizer might split this
    m_negationWords.insert("dont");  // common misspelling
    m_negationWords.insert("can't"); // "can not"
    m_negationWords.insert("cant");
    m_negationWords.insert("cannot");
    m_negationWords.insert("isn't");
    m_negationWords.insert("isnt");
    m_negationWords.insert("aren't");
    m_negationWords.insert("arent");
    m_negationWords.insert("wasn't");
    m_negationWords.insert("wasnt");
    m_negationWords.insert("weren't");
    m_negationWords.insert("werent");
    m_negationWords.insert("hasn't");
    m_negationWords.insert("hasnt");
    m_negationWords.insert("haven't");
    m_negationWords.insert("havent");
    m_negationWords.insert("won't");
    m_negationWords.insert("wont");
    m_negationWords.insert("wouldn't");
    m_negationWords.insert("wouldnt");
    m_negationWords.insert("shouldn't");
    m_negationWords.insert("shouldnt");
}

std::string SentimentAnalyzer::normalizeWord(const std::string& word) const {
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

std::vector<std::string> SentimentAnalyzer::tokenize(const std::string& text) const {
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

float SentimentAnalyzer::analyzeSentiment(const std::string& text) {
    std::vector<std::string> tokens = tokenize(text);
    float totalScore = 0.0f;
    // int sentimentWordCount = 0; // Could be used for averaging, but sum is fine for now

    bool negationActive = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& currentWord = tokens[i];

        if (m_negationWords.count(currentWord)) {
            negationActive = true; // Negation applies to next sentiment word(s) in a small window
            continue; // Negation words themselves don't usually carry score
        }

        auto it = m_lexicon.find(currentWord);
        if (it != m_lexicon.end()) {
            float wordScore = it->second;
            if (negationActive) {
                wordScore *= -1.0f;
                negationActive = false; // Reset negation after applying it once
            }
            totalScore += wordScore;
            // sentimentWordCount++;
        } else {
            // If a non-sentiment word is encountered, it might reset the negation window for simple model
            negationActive = false;
        }
    }

    // Simple sum for now. Can be normalized or capped later.
    // Example capping:
    // if (totalScore > 1.0f) totalScore = 1.0f;
    // if (totalScore < -1.0f) totalScore = -1.0f;
    return totalScore;
}
