#ifndef RCBOT_CHAT_MANAGER_H
#define RCBOT_CHAT_MANAGER_H

#include "rcbot_chat_types.h"
#include "rcbot_base.h" // For RCBotBase* to get persona
#include "extdll.h"     // For gpGlobals
#include "rcbot_chat_history.h" // For RCBotChatHistory
#include "rcbot_ngram_model.h"  // For RCBotNgramBase
#include "SentimentAnalyzer.h"  // For SentimentAnalyzer

class RCBotChatManager {
public:
    RCBotChatManager();

    // Initializes the N-gram model with training data.
    // training_data_filepath is the path to the file containing training text.
    void initializeChatModels(const std::string& training_data_filepath);

    // Generates a chat message for a bot based on context and persona
    // Context_trigger is a string indicating why the chat is being generated (e.g., "on_kill", "enemy_spotted")
    TaggedChatMessage generateBotChat(RCBotBase* bot,
                                      const std::string& context_trigger,
                                      RCBotChatHistory* chat_history,
                                      float perceived_aggression,
                                      float perceived_cooperation);

    // Potentially add methods here to load chat lines from files based on persona/sentiment/context
    // void loadChatDatabase();

    // Records a chat message sent by a human player, distributing it to all bots' context histories.
    void recordPlayerChat(edict_t* pPlayerEdict, const std::string& messageText);

private:
    RCBotNgramBase m_ngramModel; // N-gram model for chat generation
    bool m_modelsInitialized;    // Flag to prevent re-initialization

    // Helper to extract a seed phrase from context for N-gram generation
    std::string getSeedFromContext(RCBotChatHistory* chat_history,
                                   const std::string& context_trigger,
                                   float perceived_aggression,
                                   float perceived_cooperation);

    SentimentAnalyzer m_sentimentAnalyzer; // Sentiment analyzer instance
    std::map<ChatContextCategory, std::vector<std::string>> m_category_seed_keywords; // For categorized seed words
    // std::map<BotPersona, std::map<ChatSentiment, std::vector<std::string>>> m_chatLines; // Future use for more structured chat
};

// Global instance of the chat manager
extern RCBotChatManager g_ChatManager;

#endif // RCBOT_CHAT_MANAGER_H
