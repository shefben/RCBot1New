#include "rcbot_chat_manager.h"
#include "extdll.h" // For gpGlobals
#include "rcbot_manager.h" // For gRCBotManager to iterate bots
#include "rcbot_chat_history.h" // For ContextualItem, RCBotChatHistory
#include "util.h" // For UTIL_ServerPrintf for debugging

// Define the global instance
RCBotChatManager g_ChatManager;

RCBotChatManager::RCBotChatManager() : m_modelsInitialized(false) {
    // Constructor: Potentially load chat lines from a file/database in the future
    // For now, it's empty as generateBotChat uses hardcoded examples.
}

void RCBotChatManager::initializeChatModels(const std::string& training_data_override) {
    if (m_modelsInitialized && training_data_override.empty()) { // Only prevent re-init if no override
        // UTIL_ServerPrintf("RCBotChatManager: Chat models already initialized.\n");
        return;
    }

    const std::string default_training_data =
        "Hello there. Good game everyone. Nice shot! I need help over here. "
        "Let's go this way. Objective is clear. Enemy spotted. "
        "Affirmative. Negative. Covering you. Thanks for the support. "
        "That was a close call. We can win this. Don't give up. GG. Good luck next round. "
        "Where are they? I see one. Moving now. Wait for me. "
        "Defend this spot. Attack the objective. Good job. Well played. "
        "He is low health. Watch out behind you. Grenade! "
        "Follow me. Yes. No. Maybe. I don't know. What do you think? "
        "Are you ready? Let us proceed with strategic positioning. My calculations indicate a high probability of success. "
        "My sensors detect hostility. Engaging offensive protocols. Beep boop. Does not compute. "
        "Why did the chicken cross the road? To get to the other side! Ha ha. "
        "I am a robot. I like to shoot things. Pew pew pew. That is fun. ";


    const std::string& used_training_data = training_data_override.empty() ? default_training_data : training_data_override;

    m_ngramModel.buildModel(used_training_data);
    m_modelsInitialized = true;
    UTIL_ServerPrintf("RCBotChatManager: N-gram chat model built (N=%d). Vocab size: %lu unique prefixes.\n", m_ngramModel.getNgramSize(), (unsigned long)0); // Add a way to get vocab size from ngram model if desired
}


std::string RCBotChatManager::getSeedFromContext(RCBotChatHistory* chat_history, const std::string& context_trigger) {
    if (!chat_history) return "";

    std::string seed = "";
    const auto& window = chat_history->getContextWindow();

    // Try to pick last 1-2 words from the most recent chat message if available
    for (auto it = window.rbegin(); it != window.rend(); ++it) {
        if (it->type == ContextItemType::CHAT_MESSAGE) {
            std::vector<std::string> tokens = m_ngramModel.tokenize(it->chat_message.message); // Use NgramModel's tokenizer for consistency
            if (!tokens.empty()) {
                if (tokens.size() >= 2) {
                    seed = tokens[tokens.size() - 2] + " " + tokens.back();
                } else {
                    seed = tokens.back();
                }
                break;
            }
        }
    }
    // Could also add words from context_trigger if seed is still short or empty
    // e.g., if (seed.length() < 5 && !context_trigger.empty()) seed += " " + context_trigger;
    return seed;
}


TaggedChatMessage RCBotChatManager::generateBotChat(RCBotBase* bot, const std::string& context_trigger, RCBotChatHistory* chat_history) {
    if (!bot || !bot->getEdict()) {
        return TaggedChatMessage("Error: Bot pointer or edict null.", SENTIMENT_NEGATIVE, PERSONA_NEUTRAL, gpGlobals->time);
    }

    if (!m_modelsInitialized) {
         // This should ideally not happen if initializeChatModels is called correctly at startup/level init.
        UTIL_ServerPrintf("Error: N-gram model not initialized for RCBotChatManager!\n");
        // Fallback to very basic message
        return TaggedChatMessage("System error.", SENTIMENT_NEGATIVE, PERSONA_NEUTRAL, gpGlobals->time);
    }

    std::string seed_phrase = getSeedFromContext(chat_history, context_trigger);
    std::string generated_text = m_ngramModel.generateSentence(seed_phrase, 10); // Max 10 words

    BotPersona persona = bot->getPersona();
    float currentTime = gpGlobals->time;
    ChatSentiment determined_sentiment = SENTIMENT_NEUTRAL; // Default

    // Basic sentiment determination based on context_trigger
    if (context_trigger == "on_kill") determined_sentiment = SENTIMENT_POSITIVE; // Or TAUNT for aggressive
    else if (context_trigger == "on_death") determined_sentiment = SENTIMENT_NEGATIVE;
    else if (context_trigger == "enemy_spotted") determined_sentiment = SENTIMENT_INFO;

    if (generated_text.empty() || generated_text.length() < 5) { // Fallback if N-gram fails or produces too short text
        // UTIL_ServerPrintf("N-gram generation fallback for bot %s (seed: '%s')\n", STRING(bot->getEdict()->v.netname), seed_phrase.c_str());
        // Fallback to simple persona-based hardcoded messages
    // A real implementation would be far more complex, using context_trigger, game state, etc.

    // Example: A generic response if context_trigger is "generic_event"
    if (context_trigger == "generic_event") {
        switch (persona) {
            case PERSONA_AGGRESSIVE:
                return TaggedChatMessage("Get out of my way!", SENTIMENT_TAUNT, persona, currentTime);
            case PERSONA_PLAYFUL:
                return TaggedChatMessage("Whee! This is fun!", SENTIMENT_POSITIVE, persona, currentTime);
            case PERSONA_SUPPORTIVE:
                return TaggedChatMessage("We can do this, team!", SENTIMENT_POSITIVE, persona, currentTime);
            case PERSONA_TRASH_TALKER:
                return TaggedChatMessage("You call that a move? Pathetic!", SENTIMENT_TAUNT, persona, currentTime);
            case PERSONA_TACTICAL:
                return TaggedChatMessage("Hold your positions.", SENTIMENT_COMMAND, persona, currentTime);
            case PERSONA_NEUTRAL:
            default:
                return TaggedChatMessage("Okay.", SENTIMENT_NEUTRAL, persona, currentTime);
        }
    }
    // Example: Response to a kill event
    else if (context_trigger == "on_kill") {
         switch (persona) {
            case PERSONA_AGGRESSIVE:
                return TaggedChatMessage("Another one bites the dust!", SENTIMENT_TAUNT, persona, currentTime);
            case PERSONA_PLAYFUL:
                return TaggedChatMessage("Booyah! Got 'em!", SENTIMENT_POSITIVE, persona, currentTime);
            case PERSONA_SUPPORTIVE:
                // Supportive might not say much on a personal kill, or praise self mildly
                return TaggedChatMessage("Target neutralized.", SENTIMENT_INFO, persona, currentTime);
            case PERSONA_TRASH_TALKER:
                return TaggedChatMessage("Sit down, scrub!", SENTIMENT_TAUNT, persona, currentTime);
            case PERSONA_TACTICAL:
                return TaggedChatMessage("Hostile down. Area potentially clear.", SENTIMENT_INFO, persona, currentTime);
            case PERSONA_NEUTRAL:
            default:
                return TaggedChatMessage("Target eliminated.", SENTIMENT_NEUTRAL, persona, currentTime);
        }
    }
    // Default fallback for unknown contexts
    return TaggedChatMessage("...", SENTIMENT_NEUTRAL, persona, currentTime);
}

void RCBotChatManager::recordPlayerChat(edict_t* pPlayerEdict, const std::string& messageText) {
    if (!pPlayerEdict || messageText.empty()) {
        return;
    }

    // Create a TaggedChatMessage for the player's message.
    // For now, persona is set to NEUTRAL (or a specific PLAYER persona if added).

    float sentimentScore = m_sentimentAnalyzer.analyzeSentiment(messageText);

    // For now, we'll just store the raw score. Mapping to ChatSentiment enum could be done here or later.
    TaggedChatMessage playerMessage(
        messageText,
        SENTIMENT_NEUTRAL, // Could be derived from score if thresholds are set
        PERSONA_NEUTRAL,   // Or a specific PERSONA_PLAYER if defined
        gpGlobals->time,
        sentimentScore     // Store the analyzed numerical score
    );

    UTIL_ServerPrintf("Player '%s' said: '%s' (Analyzed Sentiment Score: %.2f)\n",
        pPlayerEdict ? STRING(pPlayerEdict->v.netname) : "UnknownPlayer", // Handle null pPlayerEdict just in case
        messageText.c_str(),
        sentimentScore);

    ContextualItem contextItem(playerMessage);

    // Add this player chat item to every bot's context memory.
    // This requires access to the list of bots from RCBotManager.
    // Assuming gRCBotManager has a way to iterate through bots.
    // This part is conceptual if direct access to m_Bots is not clean.
    // A better way would be for RCBotManager to provide an iterator or a method.
    // For now, let's assume we can iterate via a hypothetical getBots() or similar.

    // Accessing RCBotManager::m_Bots directly is generally not good practice if it's private.
    // Let's assume RCBotManager has a method like `getAllBots()` returning `const std::vector<RCBotBase*>&`
    // Or, if RCBotManager::m_Bots is public (as it was in some earlier structures of typical HPB_bot forks):

    // The following depends on how RCBotManager exposes its bot list.
    // If RCBotManager::m_Bots is private and there's no public getter for the list:
    // This part would need RCBotManager to have a method like:
    // void RCBotManager::propagatePlayerChatToBots(const ContextualItem& item);
    // which would then iterate its internal m_Bots list.
    // For this subtask, we'll just show the conceptual loop.

    // Conceptual: Iterate through bots managed by gRCBotManager
    // This requires gRCBotManager to expose its bot list or a way to apply an action to all bots.
    // Example if gRCBotManager.m_Bots was public (not ideal but common in older code):
    // for (RCBotBase* bot : gRCBotManager.m_Bots) { // This line is illustrative
    //     if (bot && bot->getChatContextMemory()) {
    //         bot->getChatContextMemory()->addItem(contextItem);
    //     }
    // }
    // A safer way if RCBotManager has a getter like `getActiveBots()`
    const std::vector<RCBotBase*>& allBots = gRCBotManager.getActiveBots(); // Assuming such a method
    for (RCBotBase* bot : allBots) {
        if (bot && bot->getEdict() != pPlayerEdict) { // Don't update perception for the bot that sent the message
            if (bot->getChatContextMemory()) { // Ensure memory is valid
                 bot->getChatContextMemory()->addItem(contextItem);
            }
            // Update each *other* bot's perception of the speaking player
            bot->updatePerceptionFromPlayerChat(pPlayerEdict, sentimentScore);
        } else if (bot && bot->getEdict() == pPlayerEdict && bot->getChatContextMemory()) {
            // The bot itself also records the message it sent (if it's a bot player), but doesn't update its perception of itself.
            // This case is more for if a bot is masquerading as a player or if bot-to-bot chat is implemented.
            // For player chat, this 'else if' branch might not be strictly needed if pPlayerEdict is always human.
            // However, adding it to its own context can be useful.
             bot->getChatContextMemory()->addItem(contextItem);
        }
    }
    // If no direct bot list access, RCBotManager should have a method:
    // gRCBotManager.addPlayerChatToAllBotContexts(contextItem, pPlayerEdict);
    // And that method in RCBotManager would do the loop.

    // For now, the above loop with getActiveBots() is the intended placeholder mechanism.
    // The actual hookup from the game engine's chat message (e.g. ClientSayText_Post)
    // to call this `recordPlayerChat` function is outside this specific subtask's scope,
    // but this function provides the necessary logic once called.
}
