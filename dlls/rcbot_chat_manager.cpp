#include "rcbot_chat_manager.h"
#include "extdll.h" // For gpGlobals
#include "rcbot_manager.h" // For gRCBotManager to iterate bots
#include "rcbot_chat_history.h" // For ContextualItem, RCBotChatHistory
#include "util.h" // For UTIL_ServerPrintf for debugging
#include <fstream>   // For file operations
#include <sstream>   // For stringstream

// Define the global instance
RCBotChatManager g_ChatManager;

RCBotChatManager::RCBotChatManager() : m_modelsInitialized(false) {
    // Constructor
    // Populate m_category_seed_keywords
    m_category_seed_keywords[ChatContextCategory::GENERAL_NEUTRAL] = {"the", "is", "it", "a", "yes", "no", "okay"};
    m_category_seed_keywords[ChatContextCategory::COMBAT_EVENT_SELF_POSITIVE] = {"yes", "got", "nice", "boom", "owned", "easy", "kill", "frag"};
    m_category_seed_keywords[ChatContextCategory::COMBAT_EVENT_SELF_NEGATIVE] = {"ouch", "no", "dang", "cover", "need", "help", "medic", "critical"};
    m_category_seed_keywords[ChatContextCategory::COMBAT_EVENT_TEAM_POSITIVE] = {"nice", "great", "good", "job", "team", "awesome"};
    m_category_seed_keywords[ChatContextCategory::COMBAT_EVENT_TEAM_NEGATIVE] = {"oh", "no", "watch", "out", "behind", "team", "careful"};
    m_category_seed_keywords[ChatContextCategory::OBJECTIVE_PROGRESS_POSITIVE] = {"objective", "going", "push", "yes", "site", "bomb", "flag", "capture"};
    m_category_seed_keywords[ChatContextCategory::OBJECTIVE_PROGRESS_NEGATIVE] = {"stop", "them", "defend", "no", "objective", "lost", "fail"};
    m_category_seed_keywords[ChatContextCategory::OBJECTIVE_QUERY] = {"where", "what", "objective", "bomb", "flag", "status", "plan"};
    m_category_seed_keywords[ChatContextCategory::SOCIAL_GREETING] = {"hello", "hi", "hey", "yo", "greetings", "sup"};
    m_category_seed_keywords[ChatContextCategory::SOCIAL_THANKS_RESPONSE] = {"welcome", "no problem", "np", "anytime", "sure"};
    m_category_seed_keywords[ChatContextCategory::SOCIAL_TAUNT_ENEMY] = {"you", "are", "too", "slow", "easy", "noob", "loser", "owned"};
    m_category_seed_keywords[ChatContextCategory::SOCIAL_ENCOURAGE_TEAM] = {"we", "can", "do", "it", "team", "go", "push", "don't give up", "focus"};
    m_category_seed_keywords[ChatContextCategory::DEBUG_INFO] = {"debug", "status", "report", "info", "current", "objective"};
}

void RCBotChatManager::initializeChatModels(const std::string& training_data_filepath) {
    // Allow re-initialization if called again, useful if training file changes or for testing

    std::string training_content;
    bool loaded_from_file = false;

    if (!training_data_filepath.empty()) {
        std::ifstream training_file(training_data_filepath);
        if (training_file.is_open()) {
            std::stringstream buffer;
            buffer << training_file.rdbuf();
            training_content = buffer.str();
            training_file.close();

            if (!training_content.empty()) {
                loaded_from_file = true;
                // UTIL_ServerPrintf("RCBotChatManager: Loaded training data from '%s' (%zu bytes).\n",
                //                   training_data_filepath.c_str(), training_content.length());
            } else {
                // UTIL_ServerPrintf("RCBotChatManager_WARNING: Training data file '%s' was empty. Using default.\n",
                //                   training_data_filepath.c_str());
            }
        } else {
            // UTIL_ServerPrintf("RCBotChatManager_WARNING: Could not open training file '%s'. Using default.\n",
            //                   training_data_filepath.c_str());
        }
    }

    if (!loaded_from_file || training_content.length() < 20) { // Ensure default if file too small
        if (loaded_from_file && training_content.length() < 20) {
            // UTIL_ServerPrintf("RCBotChatManager_INFO: Training file content too short. Switching to internal default.\n");
        }
        // UTIL_ServerPrintf("RCBotChatManager: Using minimal internal default training data.\n");
        training_content = "Hello there. Good game everyone. Nice shot! I need help over here. "
                           "Let's go this way. Objective is clear. Enemy spotted. "
                           "Affirmative. Negative. Covering you. Thanks for the support. "
                           "That was a close call. We can win this. Don't give up. GG. Good luck next round.";
        loaded_from_file = false; // Mark that we are using default
    }

    m_ngramModel.buildModel(training_content); // Assumes m_ngramModel is RCBotNgramBase
    m_modelsInitialized = true;

    // int ngram_size = m_ngramModel.getNgramSize(); // Requires getNgramSize() in RCBotNgramBase
    // UTIL_ServerPrintf("RCBotChatManager: N-gram model (N=%d) built. Loaded from file: %s.\n",
    //                   ngram_size, loaded_from_file ? "Yes" : "No");
}


std::string RCBotChatManager::getSeedFromContext(RCBotChatHistory* chat_history,
                                               const std::string& context_trigger,
                                               float perceived_aggression,
                                               float perceived_cooperation) {
    if (!chat_history) return ""; // Should not happen if called correctly

    ChatContextCategory determined_category = ChatContextCategory::GENERAL_NEUTRAL;

    // 1. Determine category from context_trigger (most direct signal)
    if (context_trigger == "on_kill_self") determined_category = ChatContextCategory::COMBAT_EVENT_SELF_POSITIVE;
    else if (context_trigger == "on_death_self") determined_category = ChatContextCategory::COMBAT_EVENT_SELF_NEGATIVE;
    else if (context_trigger == "on_bomb_planted_friendly") determined_category = ChatContextCategory::OBJECTIVE_PROGRESS_POSITIVE;
    else if (context_trigger == "on_bomb_defused_enemy") determined_category = ChatContextCategory::OBJECTIVE_PROGRESS_NEGATIVE;
    else if (context_trigger.find("greet") != std::string::npos) determined_category = ChatContextCategory::SOCIAL_GREETING;
    // Add more specific trigger mappings here as needed. For example:
    else if (context_trigger == "enemy_spotted") determined_category = ChatContextCategory::GENERAL_NEUTRAL; // Could be more specific if desired
    else if (context_trigger == "request_help") determined_category = ChatContextCategory::COMBAT_EVENT_SELF_NEGATIVE;
    else if (context_trigger == "thank_player") determined_category = ChatContextCategory::SOCIAL_THANKS_RESPONSE;


    // 2. (Optional refinement) Analyze recent chat_history if no strong trigger
    // This part can be expanded later. For now, primary focus is on context_trigger and category keywords.
    // Example:
    // if (determined_category == ChatContextCategory::GENERAL_NEUTRAL && chat_history && !chat_history->getContextWindow().empty()) {
    //     const auto& last_item = chat_history->getContextWindow().back();
    //     if (last_item.type == ContextItemType::GAME_EVENT &&
    //         last_item.game_event_data.type == GameEventType::DAMAGE_EVENT &&
    //         last_item.game_event_data.target_edict == bot->getEdict()) { // Assuming bot is target
    //         determined_category = ChatContextCategory::COMBAT_EVENT_SELF_NEGATIVE;
    //     }
    // }

    std::string seed_phrase = "";

    // 3. Try to get seed from category keywords
    auto it_keywords = m_category_seed_keywords.find(determined_category);
    if (it_keywords != m_category_seed_keywords.end() && !it_keywords->second.empty()) {
        const auto& keywords = it_keywords->second;
        seed_phrase = keywords[rand() % keywords.size()]; // Pick a random keyword from the category
    } else {
        // Fallback to old logic: use last chat words or trigger words if no category match or empty keywords
        const auto& window = chat_history->getContextWindow();
        for (auto it = window.rbegin(); it != window.rend(); ++it) {
            if (it->type == ContextItemType::CHAT_MESSAGE) {
                std::vector<std::string> tokens = m_ngramModel.tokenize(it->chat_message.message);
                if (!tokens.empty()) {
                    if (tokens.size() >= 2) {
                        seed_phrase = tokens[tokens.size() - 2] + " " + tokens.back();
                    } else {
                        seed_phrase = tokens.back();
                    }
                    break;
                }
            }
        }
        if (seed_phrase.empty() && !context_trigger.empty() && context_trigger.rfind("generic", 0) != 0) {
             std::vector<std::string> tokens = m_ngramModel.tokenize(context_trigger);
             if(!tokens.empty()) seed_phrase = tokens.back(); else seed_phrase = context_trigger;
        }
    }

    // 4. Fallback seed based on aggression/cooperation (from previous implementation)
    if (seed_phrase.empty() || seed_phrase.length() < 3) {
        if (perceived_aggression > 0.7f && perceived_cooperation < 0.3f) {
            const auto& taunt_seeds_it = m_category_seed_keywords.find(ChatContextCategory::SOCIAL_TAUNT_ENEMY);
            if (taunt_seeds_it != m_category_seed_keywords.end() && !taunt_seeds_it->second.empty()) {
                 seed_phrase = taunt_seeds_it->second[rand() % taunt_seeds_it->second.size()];
            } else {
                seed_phrase = "enemy"; // Ultimate fallback if SOCIAL_TAUNT_ENEMY is not populated
            }
        } else if (perceived_cooperation > 0.7f && perceived_aggression < 0.3f) {
            const auto& encourage_seeds_it = m_category_seed_keywords.find(ChatContextCategory::SOCIAL_ENCOURAGE_TEAM);
            if (encourage_seeds_it != m_category_seed_keywords.end() && !encourage_seeds_it->second.empty()) {
                seed_phrase = encourage_seeds_it->second[rand() % encourage_seeds_it->second.size()];
            } else {
                seed_phrase = "team"; // Ultimate fallback
            }
        }
    }
    return seed_phrase;
}


TaggedChatMessage RCBotChatManager::generateBotChat(RCBotBase* bot,
                                                  const std::string& context_trigger,
                                                  RCBotChatHistory* chat_history,
                                                  float perceived_aggression,
                                                  float perceived_cooperation) {
    if (!bot || !bot->getEdict()) {
        return TaggedChatMessage("Error: Bot pointer or edict null.", SENTIMENT_NEGATIVE, PERSONA_NEUTRAL, gpGlobals->time);
    }

    if (!m_modelsInitialized) {
        UTIL_ServerPrintf("Error: N-gram model not initialized for RCBotChatManager! Call initializeChatModels.\n");
        return TaggedChatMessage("System error: chat model offline.", SENTIMENT_NEGATIVE, PERSONA_NEUTRAL, gpGlobals->time);
    }

    BotPersona current_persona = bot->getPersona();
    const auto& embeddings_map = getPersonaStyleEmbeddings(); // from rcbot_chat_types.h
    std::vector<float> style_vector;
    auto it_style = embeddings_map.find(current_persona);
    if (it_style != embeddings_map.end()) {
        style_vector = it_style->second;
    } else {
        style_vector = embeddings_map.at(BotPersona::PERSONA_NEUTRAL); // Fallback
    }

    // Debug print for persona and style vector
    char style_vec_str[128] = {0};
    std::string temp_s;
    for(float val : style_vector) temp_s += std::to_string(val) + " ";
    snprintf(style_vec_str, sizeof(style_vec_str)-1, "%s", temp_s.c_str());

    // Reduced debug printing from previous step, focus on style vector
    // UTIL_ServerPrintf("RCBotChatManager: Bot %s (Persona %d, Style: [%s]) Trigger: %s\n",
    //     STRING(bot->getEdict()->v.netname), (int)current_persona, style_vec_str, context_trigger.c_str());

    // Pass perceived aggression/cooperation to getSeedFromContext
    std::string seed_phrase = getSeedFromContext(chat_history, context_trigger, perceived_aggression, perceived_cooperation);

    // Removed the style_vector based seed generation here as it's now handled in getSeedFromContext based on perception scores.

    // UTIL_ServerPrintf("RCBotChatManager: Bot %s using seed_phrase: '%s'\n", STRING(bot->getEdict()->v.netname), seed_phrase.c_str());


    std::string generated_text = m_ngramModel.generateSentence(seed_phrase, 10);

    float currentTime = gpGlobals->time;
    ChatSentiment determined_sentiment = SENTIMENT_NEUTRAL;

    // Basic sentiment determination based on context_trigger (can be refined)
    if (context_trigger == "on_kill") determined_sentiment = (current_persona == PERSONA_TRASH_TALKER || current_persona == PERSONA_AGGRESSIVE) ? SENTIMENT_TAUNT : SENTIMENT_POSITIVE;
    else if (context_trigger == "on_death") determined_sentiment = SENTIMENT_NEGATIVE;
    else if (context_trigger == "enemy_spotted") determined_sentiment = SENTIMENT_INFO;
    else if (context_trigger == "request_help") determined_sentiment = SENTIMENT_QUESTION; // or command if bot is asking for help

    if (generated_text.length() < 5 && generated_text.find_first_not_of(' ') != std::string::npos) { // If generated text is too short but not purely whitespace
         // Try to extend it slightly with a generic follow-up based on persona
        if (current_persona == PERSONA_PLAYFUL || current_persona == PERSONA_TRASH_TALKER) generated_text += " lol";
        else if (current_persona == PERSONA_SUPPORTIVE) generated_text += " right?";
    }


    if (generated_text.empty() || generated_text.length() < 5 || generated_text.find_first_not_of(' ') == std::string::npos ) {
        // UTIL_ServerPrintf("N-gram generation fallback for bot %s (Persona: %d, Seed: '%s', Trigger: '%s')\n",
        //    STRING(bot->getEdict()->v.netname), (int)current_persona, seed_phrase.c_str(), context_trigger.c_str());

        // Fallback to simple persona-based hardcoded messages
        if (context_trigger == "on_kill") {
             switch (current_persona) {
                case PERSONA_AGGRESSIVE:    generated_text = "Too easy."; determined_sentiment = SENTIMENT_TAUNT; break;
                case PERSONA_TRASH_TALKER:  generated_text = "Get owned!"; determined_sentiment = SENTIMENT_TAUNT; break;
                case PERSONA_PLAYFUL:       generated_text = "Woohoo!"; determined_sentiment = SENTIMENT_POSITIVE; break;
                default:                    generated_text = "Target down."; determined_sentiment = SENTIMENT_INFO; break;
            }
        } else if (context_trigger == "generic_event" || context_trigger.empty()){
             switch (current_persona) {
                case PERSONA_AGGRESSIVE:    generated_text = "Grrr."; determined_sentiment = SENTIMENT_NEUTRAL; break;
                case PERSONA_PLAYFUL:       generated_text = "Hi!"; determined_sentiment = SENTIMENT_GREETING; break;
                case PERSONA_SUPPORTIVE:    generated_text = "Team, let's focus."; determined_sentiment = SENTIMENT_COMMAND; break;
                case PERSONA_TRASH_TALKER:  generated_text = "What now?"; determined_sentiment = SENTIMENT_QUESTION; break;
                case PERSONA_TACTICAL:      generated_text = "Acknowledged."; determined_sentiment = SENTIMENT_INFO; break;
                default:                    generated_text = "Ok."; determined_sentiment = SENTIMENT_NEUTRAL; break;
            }
        } else { // Default fallback for other unknown/unhandled triggers
            generated_text = "...";
            determined_sentiment = SENTIMENT_NEUTRAL;
        }
        return TaggedChatMessage(generated_text, determined_sentiment, current_persona, currentTime, 0.0f); // sentiment_score 0 for hardcoded
    }

    // If N-gram produced something, use it.
    // The sentiment_score for N-gram generated text is not analyzed here; could be done as a post-step.
    // For now, it inherits determined_sentiment from context_trigger or remains neutral.
    return TaggedChatMessage(generated_text, determined_sentiment, current_persona, currentTime, 0.0f);
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
        sentimentScore,    // Store the analyzed numerical score
        pPlayerEdict ? ENTINDEX(pPlayerEdict) : 0 // Set sender_entity_index
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
