#ifndef SENTIMENT_ANALYZER_H
#define SENTIMENT_ANALYZER_H

#include <string>
#include <vector>
#include <map>
#include <set>

class SentimentAnalyzer {
public:
    SentimentAnalyzer();

    // Analyzes the sentiment of a given text.
    // Returns a float score (e.g., positive, negative, or neutral 0.0).
    float analyzeSentiment(const std::string& text);

    // Loads a default lexicon of words and their sentiment scores, and negation words.
    void loadDefaultLexicon();

private:
    std::map<std::string, float> m_lexicon;
    std::set<std::string> m_negationWords;

    // Helper to split text into words.
    std::vector<std::string> tokenize(const std::string& text) const;

    // Helper to normalize a single word (e.g., lowercase, remove punctuation).
    std::string normalizeWord(const std::string& word) const;
};

#endif // SENTIMENT_ANALYZER_H
