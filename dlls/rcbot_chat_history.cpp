#include "rcbot_chat_history.h"
#include <algorithm> // For std::sort if needed, though not used in current addItem

RCBotChatHistory::RCBotChatHistory() {
    // The deque m_context_window is automatically initialized (empty).
    // MAX_CONTEXT_ITEMS is a const, defined in the header.
}

void RCBotChatHistory::addItem(const ContextualItem& item) {
    // Assuming items are generally added in chronological order.
    // If timestamps could be significantly out of order, a sort after push_back
    // or a more complex insertion would be needed. For now, simple append.
    m_context_window.push_back(item);

    // If the window exceeds the maximum size, remove the oldest item (from the front).
    if (m_context_window.size() > MAX_CONTEXT_ITEMS) {
        m_context_window.pop_front();
    }
}

const std::deque<ContextualItem>& RCBotChatHistory::getContextWindow() const {
    return m_context_window;
}

std::vector<ContextualItem> RCBotChatHistory::getContextWindowCopy() const {
    // Create a vector from the deque for callers who need a mutable copy or a different container type.
    return std::vector<ContextualItem>(m_context_window.begin(), m_context_window.end());
}

void RCBotChatHistory::clear() {
    m_context_window.clear();
}

float RCBotChatHistory::getLastPlayerInteractionTime(int player_entity_index) const {
    // Iterate in reverse (most recent first)
    for (auto it = m_context_window.rbegin(); it != m_context_window.rend(); ++it) {
        const ContextualItem& item = *it;
        if (item.type == ContextItemType::CHAT_MESSAGE) {
            // Check if the sender_entity_index in the chat_message matches the player_entity_index
            // This assumes TaggedChatMessage has a field like sender_entity_index.
            // If ContextualItem itself stores sender info directly for CHAT_MESSAGE type, adjust accordingly.
            if (item.chat_message.sender_entity_index == player_entity_index) {
                return item.timestamp; // Return the timestamp of the most recent message from this player
            }
        }
    }
    return 0.0f; // Return 0.0 if no message from this player is found
}
