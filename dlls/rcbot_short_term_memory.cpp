#include "rcbot_short_term_memory.h"

RCBotShortTermMemory::RCBotShortTermMemory() : currentIndex(0) {
    // Pre-allocate memory for events to avoid frequent reallocations
    events.reserve(MAX_MEMORY_EVENTS);
}

void RCBotShortTermMemory::addEvent(const GameEvent& event) {
    if (events.size() < MAX_MEMORY_EVENTS) {
        events.push_back(event);
    } else {
        events[currentIndex] = event;
        currentIndex = (currentIndex + 1) % MAX_MEMORY_EVENTS;
    }
}

std::vector<GameEvent> RCBotShortTermMemory::getRecentEvents(int count) const {
    std::vector<GameEvent> recentEvents;
    int numEventsToRetrieve = std::min(count, static_cast<int>(events.size()));

    if (events.size() < MAX_MEMORY_EVENTS) {
        // If buffer is not full, events are in order from oldest to newest
        for (int i = events.size() - numEventsToRetrieve; i < events.size(); ++i) {
            recentEvents.push_back(events[i]);
        }
    } else {
        // If buffer is full and wrapped around
        for (int i = 0; i < numEventsToRetrieve; ++i) {
            int index = (currentIndex - 1 - i + MAX_MEMORY_EVENTS) % MAX_MEMORY_EVENTS;
            recentEvents.push_back(events[index]);
        }
        // The events will be from newest to oldest, so reverse them
        std::reverse(recentEvents.begin(), recentEvents.end());
    }
    return recentEvents;
}

std::vector<GameEvent> RCBotShortTermMemory::getAllEvents() const {
    if (events.size() < MAX_MEMORY_EVENTS) {
        return events;
    } else {
        // If the buffer has wrapped, the order is mixed.
        // We need to return them in chronological order.
        std::vector<GameEvent> orderedEvents;
        orderedEvents.reserve(MAX_MEMORY_EVENTS);
        for (int i = 0; i < MAX_MEMORY_EVENTS; ++i) {
            orderedEvents.push_back(events[(currentIndex + i) % MAX_MEMORY_EVENTS]);
        }
        return orderedEvents;
    }
}
