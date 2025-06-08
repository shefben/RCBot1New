#ifndef RL_TYPES_H
#define RL_TYPES_H

#include <vector>
#include <string> // For state description if needed
#include <deque>  // For potential use in state or action history

namespace RLStateProps {
    // THIS VALUE MUST BE MANUALLY KEPT IN SYNC with features added in RCBotRLHelper::getCurrentBotState()
    // Health, Armor, Pos(3), Vel(4), Flags(5), Ammo(5), CurrWpnID, Cooldowns(2), ObjInfo(4), Percept(2), Timers(1), TaskCompl(1), OpponentChars(7)
    //  1   +   1  +   3   +  4   +   5    +   5    +    1    +     2      +    4    +    2     +    1     +    1      +      7       = 37 Features
    static const int NUM_STATE_FEATURES = 37;
}

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
    // Removed TACTIC_USE_MACRO, replaced by specific macro actions below

    // Specific Macro Actions
    ACTION_MACRO_STRAFE_JUMP_LEFT,
    ACTION_MACRO_PEEK_COVER_RIGHT_QUICK,
    ACTION_MACRO_SHORT_FORWARD_BURST,
    ACTION_MACRO_AIM_UP_BRIEFLY,

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

// Defines how a bot might interact with a dynamic objective
enum class ObjectiveInteractionType : int {
    NONE = 0,
    PRIMARY_INTERACT_USE,     // Press 'use' once (e.g., button, door, initial hostage interaction)
    USE_FOR_DURATION,         // Press and hold 'use' (e.g., defuse, plant, some control points)
    TOUCH_TO_ACTIVATE,        // Physical contact needed (e.g., flag pickup, some triggers)
    BE_IN_PROXIMITY_FOR_DURATION // New type for control points, area triggers
    // More complex types for future consideration:
    // ESCORT_TARGET,
};

#endif // RL_TYPES_H
