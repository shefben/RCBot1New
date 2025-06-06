#ifndef RCBOT_SHORT_TERM_MEMORY_H
#define RCBOT_SHORT_TERM_MEMORY_H

#include <vector>
#include <algorithm> // Required for std::min

// Define a maximum size for the buffer
const int MAX_MEMORY_EVENTS = 100;

// Define different types of game events
enum GameEventType {
    DAMAGE_EVENT,
    HEAR_SOUND_EVENT,
    // Add other event types as needed
};

// Structure to represent a game event
struct GameEvent {
    GameEventType type;
    float timestamp;
    // Add other relevant data members as needed
    // For example, for a damage event:
    float damageAmount;
    // For a sound event:
    // Vector soundOrigin;

    // Constructor for damage event
    GameEvent(GameEventType type, float timestamp, float damageAmount)
        : type(type), timestamp(timestamp), damageAmount(damageAmount) {}

    // Add other constructors as needed for different event types
};

// Class to manage short-term memory for the bot
class RCBotShortTermMemory {
public:
    RCBotShortTermMemory();

    // Adds a new event to the memory
    void addEvent(const GameEvent& event);

    // Retrieves a specified number of recent events
    std::vector<GameEvent> getRecentEvents(int count) const;

    // Retrieves all events currently in the memory
    std::vector<GameEvent> getAllEvents() const;

private:
    std::vector<GameEvent> events;
    int currentIndex;
};

#endif // RCBOT_SHORT_TERM_MEMORY_H
