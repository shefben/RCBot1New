#ifndef RCBOT_CHAT_TYPES_H
#define RCBOT_CHAT_TYPES_H

#include <string>
#include <vector> // Needed for std::vector
#include <map>    // Needed for std::map
#include "extdll.h" // For gpGlobals if used for timestamp, or just time_t

// Defines the personality of the bot, influencing chat style and behavior
enum BotPersona {
    PERSONA_NEUTRAL,
    PERSONA_AGGRESSIVE,
    PERSONA_PLAYFUL,
    PERSONA_SUPPORTIVE,
    PERSONA_TRASH_TALKER, // More specific than aggressive
    PERSONA_TACTICAL,     // Focuses on game state and commands
    PERSONA_MAX_PERSONAS  // For iteration or random selection
};

// Defines the sentiment of a specific chat message
enum ChatSentiment {
    SENTIMENT_NEUTRAL,
    SENTIMENT_POSITIVE,   // e.g., "Good job team!"
    SENTIMENT_NEGATIVE,   // e.g., "That was unlucky."
    SENTIMENT_COMPLIMENT, // e.g., "Nice shot!"
    SENTIMENT_TAUNT,      // e.g., "Too easy!"
    SENTIMENT_QUESTION,   // e.g., "What's the plan?"
    SENTIMENT_ANSWER,
    SENTIMENT_COMMAND,    // e.g., "Push B site!"
    SENTIMENT_INFO,       // e.g., "Enemy spotted at A."
    SENTIMENT_GREETING,
    SENTIMENT_FAREWELL,
    SENTIMENT_MAX_SENTIMENTS
};

// Structure for a chat message with associated metadata
struct TaggedChatMessage {
    std::string message;
    ChatSentiment sentiment;
    BotPersona persona_at_time_of_sending; // The persona of the bot when this message was generated/sent
    float timestamp;                       // Time the message was generated/sent (e.g., gpGlobals->time)
    float sentiment_score;                 // Numerical sentiment score, e.g., from -1.0 (v. neg) to 1.0 (v. pos)

    // Default constructor
    TaggedChatMessage(const std::string& msg = "",
                      ChatSentiment s = SENTIMENT_NEUTRAL,
                      BotPersona p = PERSONA_NEUTRAL,
                      float ts = 0.0f,
                      float num_score = 0.0f)
        : message(msg), sentiment(s), persona_at_time_of_sending(p), timestamp(ts), sentiment_score(num_score) {}
};


// Map from BotPersona to a simple style vector
// These are manually defined placeholders for actual learned embeddings
// Dimensions could represent [aggression, support, playfulness] for example.
inline std::map<BotPersona, std::vector<float>>& getPersonaStyleEmbeddings() {
    static std::map<BotPersona, std::vector<float>> personaStyleEmbeddings = {
        {BotPersona::PERSONA_NEUTRAL,        {0.5f, 0.5f, 0.3f}},
        {BotPersona::PERSONA_AGGRESSIVE,     {0.9f, 0.1f, 0.2f}},
        {BotPersona::PERSONA_PLAYFUL,        {0.3f, 0.4f, 0.9f}},
        {BotPersona::PERSONA_SUPPORTIVE,     {0.1f, 0.9f, 0.4f}},
        {BotPersona::PERSONA_TRASH_TALKER,   {0.8f, 0.1f, 0.8f}}, // High aggression, high playfulness
        {BotPersona::PERSONA_TACTICAL,       {0.4f, 0.7f, 0.1f}}  // Neutral aggression, moderate support, low playfulness
    };
    // Ensure all personas defined in the enum have an entry
    // This static map is initialized once.
    return personaStyleEmbeddings;
}


#endif // RCBOT_CHAT_TYPES_H
