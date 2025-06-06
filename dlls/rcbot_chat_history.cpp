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
