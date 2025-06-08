#ifndef RCBOT_OPPONENT_MODEL_H
#define RCBOT_OPPONENT_MODEL_H

#include <string>
#include <vector> // For Vector if used directly, or include extdll.h
#include "extdll.h" // For Vector, edict_t related info if needed for IDs

struct OpponentStats {
    int         player_unique_id;       // Engine's unique ID for a player (e.g., userid from GETPLAYERUSERID)
    std::string last_known_name;

    float       damage_dealt_to_bot;    // Total damage this opponent dealt to our bot
    float       damage_taken_from_bot;  // Total damage our bot dealt to this opponent

    int         kills_by_opponent_on_bot;
    int         kills_by_bot_on_opponent;

    float       perceived_threat_level; // Calculated, e.g., [0,1] or some other scale
    float       perceived_friendliness; // Calculated, e.g., based on chat sentiment, lack of FF

    float       last_encounter_time;
    Vector      last_known_location;    // Location of last encounter or death

    // Optional future fields:
    // std::string primary_weapon_preference;
    // float       accuracy_against_bot;
    // float       bot_accuracy_against;

    OpponentStats() :
        player_unique_id(0),
        damage_dealt_to_bot(0.0f), damage_taken_from_bot(0.0f),
        kills_by_opponent_on_bot(0), kills_by_bot_on_opponent(0),
        perceived_threat_level(0.5f), // Start neutral
        perceived_friendliness(0.5f), // Start neutral
        last_encounter_time(0.0f),
        last_known_location(0,0,0) {}
};

#endif // RCBOT_OPPONENT_MODEL_H
