#ifndef RCBOT_CHAT_HISTORY_H
#define RCBOT_CHAT_HISTORY_H

#include <deque>
#include <vector>
#include "rcbot_chat_types.h"       // For TaggedChatMessage
#include "rcbot_short_term_memory.h" // For GameEvent

// Maximum number of items to store in the context window
const size_t MAX_CONTEXT_ITEMS = 50; // Example value

// Enum to discriminate the type of item stored in ContextualItem
enum class ContextItemType {
    CHAT_MESSAGE,
    GAME_EVENT
};

// Structure to hold either a chat message or a game event, along with its type and timestamp
struct ContextualItem {
    ContextItemType type;
    float timestamp; // Unified timestamp for sorting and relevance

    // Data members for each type. Only one will be valid based on 'type'.
    TaggedChatMessage chat_message; // Valid if type == CHAT_MESSAGE
    GameEvent game_event;         // Valid if type == GAME_EVENT (needs default constructor or careful handling)

    // Constructors for convenience
    ContextualItem(const TaggedChatMessage& msg)
        : type(ContextItemType::CHAT_MESSAGE),
          timestamp(msg.timestamp),
          chat_message(msg),
          game_event(GameEventType{}, 0.0f, 0.0f) // Initialize game_event to a default state
    {}

    ContextualItem(const GameEvent& evt)
        : type(ContextItemType::GAME_EVENT),
          timestamp(evt.timestamp),
          // chat_message needs a default constructor if not explicitly initialized here
          game_event(evt)
    {}

    // Default constructor might be needed if stored directly in some containers
    // Ensure members have sensible defaults or are initialized in all constructors.
    ContextualItem()
        : type(ContextItemType::GAME_EVENT), // Arbitrary default
          timestamp(0.0f),
          game_event(GameEventType{}, 0.0f, 0.0f) // Default GameEvent
    {}
};

// Class to manage a chronological window of contextual items (chat and game events)
class RCBotChatHistory {
public:
    RCBotChatHistory();

    // Adds a new item (chat or game event) to the history window.
    // Ensures the window does not exceed MAX_CONTEXT_ITEMS.
    // Items should ideally be added in chronological order of their timestamps.
    void addItem(const ContextualItem& item);

    // Retrieves the current context window.
    // Returns a const reference to avoid copying if the caller only needs to read.
    const std::deque<ContextualItem>& getContextWindow() const;

    // Provides a mutable copy if needed (e.g. for filtering before use)
    std::vector<ContextualItem> getContextWindowCopy() const;


    // Clears all items from the history.
    void clear();

    // Gets the timestamp of the last chat message received from a specific player.
    // Returns 0.0f if no message from that player is found in the history.
    float getLastPlayerInteractionTime(int player_entity_index) const;

private:
    std::deque<ContextualItem> m_context_window;
};

#endif // RCBOT_CHAT_HISTORY_H
