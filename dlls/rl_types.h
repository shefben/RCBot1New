#ifndef RL_TYPES_H
#define RL_TYPES_H

#include <vector>
#include <string> // For state description if needed
#include <deque>  // For potential use in state or action history

// Simplified State Representation (Example)
// This will need to be expanded significantly with actual game variables.
struct BotState {
    std::vector<float> features;

    BotState() = default; // Default constructor

    // Optional: Add a method to serialize/deserialize or describe the state
    std::string toString() const {
        std::string s = "State: [";
        for (size_t i = 0; i < features.size(); ++i) {
            s += std::to_string(features[i]);
            if (i < features.size() - 1) s += ", ";
        }
        s += "]";
        return s;
    }

    void addFeature(float f) { features.push_back(f); }
    void clear() { features.clear(); }
    bool isEmpty() const { return features.empty(); }
};

// Simplified Action Representation (Example)
// This will map to bot's decisions, e.g., move direction, attack, use macro
enum class BotActionType : int {
    IDLE = 0,
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,     // Might be less used if strafing is preferred
    MOVE_RIGHT,    // Might be less used if strafing is preferred
    STRAFE_LEFT,
    STRAFE_RIGHT,
    JUMP,
    DUCK_BEGIN,    // Action to start ducking
    DUCK_MAINTAIN, // State of being ducked (might be more state than action)
    DUCK_END,      // Action to stop ducking (stand up)
    PRIMARY_ATTACK_PRESS,
    PRIMARY_ATTACK_RELEASE, // If attacks are not just single frame events
    SECONDARY_ATTACK_PRESS,
    SECONDARY_ATTACK_RELEASE,
    RELOAD_PRESS,
    USE_ITEM_PRESS, // For 'use' key on doors, objectives etc.
    // Tactical/High-Level Intentions
    TACTIC_ENGAGE_ENEMY,
    TACTIC_PURSUE_OBJECTIVE,
    TACTIC_RETREAT_OR_FALLBACK,
    TACTIC_HOLD_POSITION,
    TACTIC_USE_MACRO, // A generic action if a macro is active
    // Potentially more specific macros if they are distinct choices
    // TACTIC_USE_MACRO_STRAFE_JUMP,
    // TACTIC_USE_MACRO_PEEK_COVER,
    SWITCH_WEAPON_PRIMARY, // Example, could be more granular
    SWITCH_WEAPON_SECONDARY,
    SWITCH_WEAPON_MELEE,
    TACTIC_PURSUE_DYNAMIC_OBJECTIVE, // New action type
    MAX_ACTIONS // For sizing arrays or loops
};

struct RLTransition {
    BotState state;
    BotActionType action;
    float reward;
    BotState next_state;
    bool is_terminal; // True if next_state is a terminal state (e.g., bot died, round ended)

    RLTransition() : action(BotActionType::IDLE), reward(0.0f), is_terminal(false) {}
};

#endif // RL_TYPES_H
