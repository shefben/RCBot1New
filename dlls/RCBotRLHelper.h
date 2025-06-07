#ifndef RCBOT_RL_HELPER_H
#define RCBOT_RL_HELPER_H

#include "rl_types.h" // For BotState
#include "extdll.h"   // For edict_t, Vector
#include <string>
#include <vector>
#include <map>

// Forward declare if necessary
// class RCBotBase;

// Reward/Penalty constants - these are for reference by the caller (RCBotBase)
// They are not directly used by RCBotRLHelper's methods other than conceptually.
namespace RLConsts {
    static const float REWARD_DAMAGE_DEALT = 0.02f;
    static const float PENALTY_DAMAGE_TAKEN = -0.03f;
    static const float REWARD_KILL_CONFIRMED = 0.5f;
    static const float PENALTY_BOT_DEATH = -1.0f;
    static const float REWARD_OBJECTIVE_PROGRESS = 0.005f;
    static const float REWARD_OBJECTIVE_COMPLETED = 1.0f;
    static const float PENALTY_STUCK_OR_IDLE_LONG = -0.01f; // Penalty applied if idle for too long
    static const float PENALTY_FRIENDLY_FIRE = -0.5f;

    // Constants for Stuck/Idle detection
    static const float MAX_IDLE_TIME_SECONDS = 10.0f;          // Max duration bot can be idle before penalty
    static const float MIN_MOVEMENT_SPEED_THRESHOLD = 10.0f;   // Velocity magnitude below which bot is considered potentially stuck/idle
}

class RCBotRLHelper {
public:
    RCBotRLHelper();

    BotState getCurrentBotState(
        edict_t* pEdict,
        const std::string& currentObjectiveFocusID,      // Current dynamic objective ID
        // ObjectiveCandidateMetadata* focusedObjective, // Pass the actual objective data if available
        const Vector& focusedObjectiveLocation,          // Location of the focused objective (if any)
        bool hasFocusedObjectiveLocation,                // Flag if the location is valid
        float perceivedPlayerAggression,                // Bot's perception
        float perceivedPlayerCooperation,               // Bot's perception
        float timeSinceLastDamageTaken,                  // Timer
        bool isOnGround,                                 // From pEdict->v.flags
        bool isInWater,                                  // From pEdict->v.waterlevel
        bool isOnLadder,                                 // From p_edict->v.flags
        const std::map<int, int>& currentWeaponAmmo,     // Clip ammo per weaponID
        const std::map<int, int>& currentWeaponMaxClip,  // Max clip per weaponID
        int currentWeaponId,                             // ID of the currently equipped weapon
        float currentTaskCompletionRatio                // e.g. for path following, 0.0 to 1.0
    ) const;

    void addReward(float amount);
    float getAccumulatedRewardAndReset();
    void reset();

private:
    float m_accumulatedReward;

    // Normalization constants (can be private static const or local to .cpp)
    static const float MAX_MAP_COORDINATE_DIM; // Example: 4096.0f
    static const float MAX_PLAYER_SPEED;       // Example: 1000.0f
    static const float MAX_TIME_SINCE_EVENT;   // Example: 60.0f (for normalizing timers)
};

#endif // RCBOT_RL_HELPER_H
