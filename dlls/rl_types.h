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
    MOVE_LEFT,
    MOVE_RIGHT,
    JUMP,
    DUCK, // Continuous press
    WALK, // Modifier for slower movement
    PRIMARY_ATTACK,
    SECONDARY_ATTACK,
    RELOAD,
    USE_ITEM, // Generic use
    CHANGE_WEAPON_TO_PRIMARY,
    CHANGE_WEAPON_TO_SECONDARY,
    CHANGE_WEAPON_TO_MELEE,
    // Example for specific macro actions, can be extended
    USE_MACRO_ACTION_STRAFE_JUMP_LEFT,
    USE_MACRO_ACTION_PEEK_COVER_RIGHT,
    // ... other specific actions
    MAX_ACTIONS // Keep this last for count if needed (e.g., for Q-table size)
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
