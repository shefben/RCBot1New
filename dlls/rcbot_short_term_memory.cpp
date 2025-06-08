#include "rcbot_short_term_memory.h"
#include <algorithm> // For std::min

// Define edict_t and other engine types if not available
// This is a common way to include engine types in external files
#ifndef METAMOD_BUILD
#include "extdll.h" // For edict_t, gpGlobals, etc.
#else
#include <extdll.h>
#endif

RCBotShortTermMemory::RCBotShortTermMemory() {
    // Constructor can be used for any initial setup if needed
    // m_events deque is automatically initialized
}

void RCBotShortTermMemory::addEvent(const GameEvent& event) {
    // Add new event to the front (or back, depending on desired retrieval order)
    // Adding to front makes getRecentEvents naturally get the latest ones if iterating from begin()
    m_events.push_front(event);

    // If the buffer exceeds the maximum size, remove the oldest event
    if (m_events.size() > MAX_GAME_EVENTS_IN_STM) {
        m_events.pop_back(); // Remove from the end, which is the oldest
    }
}

std::vector<GameEvent> RCBotShortTermMemory::getRecentEvents(int count) const {
    std::vector<GameEvent> recent_events;
    int num_to_retrieve = std::min(count, static_cast<int>(m_events.size()));

    // Iterate from the beginning of the deque (newest events)
    for (int i = 0; i < num_to_retrieve; ++i) {
        recent_events.push_back(m_events[i]);
    }
    return recent_events;
}

std::vector<GameEvent> RCBotShortTermMemory::getAllEvents() const {
    std::vector<GameEvent> all_events;
    // Iterate through the deque and add all events to the vector
    // The order will be from newest to oldest if push_front was used in addEvent
    for (const auto& event : m_events) {
        all_events.push_back(event);
    }
    return all_events;
}

void RCBotShortTermMemory::clearEvents() {
    m_events.clear();
}

size_t RCBotShortTermMemory::getEventCount() const {
    return m_events.size();
}
