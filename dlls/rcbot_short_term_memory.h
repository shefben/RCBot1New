#ifndef RCBOT_SHORT_TERM_MEMORY_H
#define RCBOT_SHORT_TERM_MEMORY_H

#include <vector>
#include <deque> // Using deque for efficient addition and removal from front/back
#include <string> // Required for std::string if used in GameEvent, e.g. for entity names

// Forward declaration for edict_t to avoid including heavy engine headers here
struct edict_s; // Standard Half-Life SDK practice
typedef struct edict_s edict_t;

// Define types of game events
enum GameEventType {
    EVENT_UNKNOWN = 0,
    EVENT_DAMAGE_TAKEN,    // Bot took damage
    EVENT_DAMAGE_DEALT,    // Bot dealt damage
    EVENT_HEARD_ENEMY_SOUND, // Bot heard an enemy sound (footsteps, gunfire)
    EVENT_HEARD_GENERAL_SOUND, // Bot heard a general sound (e.g., door, item pickup)
    EVENT_PLAYER_DIED,       // A player (could be bot or human) died
    EVENT_ENEMY_SIGHTED,     // Bot visually confirmed an enemy
    EVENT_TEAMMATE_SIGHTED,  // Bot visually confirmed a teammate
    EVENT_ITEM_PICKUP,     // Bot picked up an item
    EVENT_WEAPON_FIRE,     // Bot fired its weapon
    // Add more event types as needed
};

struct GameEvent {
    GameEventType type;
    float timestamp; // Using engine time (e.g., gpGlobals->time)

    // Data specific to event types
    // Using a union or struct members directly. Consider which is more appropriate.
    // For simplicity, using direct members and ignoring irrelevant ones per event type.
    float float_data1;   // e.g., damage amount, sound volume
    float float_data2;   // Additional float data if needed
    int int_data1;       // e.g., entity index of source/target
    int int_data2;       // Additional int data if needed

    // Store pointers to edicts carefully. Edicts can become invalid.
    // Consider storing entity indices or unique IDs instead if events persist long.
    // For short-term memory, direct edict pointers might be okay if handled with care.
    edict_t* edict_source;    // e.g., attacker, sound origin entity
    edict_t* edict_target;    // e.g., victim of damage

    // Potentially add a string for names or other descriptive info, but be mindful of performance
    // std::string string_data;

    // Constructor
    GameEvent(GameEventType t = EVENT_UNKNOWN, float time = 0.0f)
        : type(t), timestamp(time), float_data1(0.0f), float_data2(0.0f),
          int_data1(0), int_data2(0), edict_source(nullptr), edict_target(nullptr) {}

    // Example constructors for specific events:
    static GameEvent DamageTaken(float time, float amount, edict_t* attacker, edict_t* victim) {
        GameEvent ev(EVENT_DAMAGE_TAKEN, time);
        ev.float_data1 = amount;
        ev.edict_source = attacker;
        ev.edict_target = victim;
        return ev;
    }

    static GameEvent SoundHeard(float time, edict_t* sound_origin, float volume, bool is_enemy_sound) {
        GameEvent ev(is_enemy_sound ? EVENT_HEARD_ENEMY_SOUND : EVENT_HEARD_GENERAL_SOUND, time);
        ev.edict_source = sound_origin;
        ev.float_data1 = volume;
        return ev;
    }

    static GameEvent EnemySighted(float time, edict_t* enemy_sighted) {
        GameEvent ev(EVENT_ENEMY_SIGHTED, time);
        ev.edict_source = enemy_sighted;
        return ev;
    }
};

const int MAX_GAME_EVENTS_IN_STM = 50; // Define a maximum size for the buffer

class RCBotShortTermMemory {
public:
    RCBotShortTermMemory();

    // Adds an event to the memory. Handles circular buffer logic.
    void addEvent(const GameEvent& event);

    // Retrieves the last 'count' events. Events are ordered newest first.
    std::vector<GameEvent> getRecentEvents(int count) const;

    // Retrieves all events currently in memory. Events are ordered newest first.
    std::vector<GameEvent> getAllEvents() const;

    // Clears all events from memory
    void clearEvents();

    // Gets the current number of events stored
    size_t getEventCount() const;

private:
    std::deque<GameEvent> m_events; // Using deque for efficient addition to front and removal from back
                                   // to maintain newest-first order if desired, or add to back / remove from front.
                                   // For newest-first in getRecentEvents, adding to front is convenient.
};

#endif // RCBOT_SHORT_TERM_MEMORY_H
