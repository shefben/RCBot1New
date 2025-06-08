#include "RCBotRLHelper.h"
#include "rcbot_dynamic_objectives.h" // Not directly used here, but if ObjectiveCandidateMetadata were passed
#include "util_shared.h" // For DotProduct, etc. if needed, or Vector operations
#include "extdll.h"      // For gpGlobals, Vector, edict_t
#include <cmath>         // For std::sqrt, std::fabs, std::fmod
#include <algorithm>     // For std::min, std::max
#include <limits>        // For std::numeric_limits

// Reward/Penalty constants
// These are now part of the helper, not RCBotBase directly for RL purposes
const float RL_REWARD_DAMAGE_DEALT = 0.02f;
const float RL_PENALTY_DAMAGE_TAKEN = -0.03f;
const float RL_REWARD_KILL_CONFIRMED = 0.5f;
const float RL_PENALTY_BOT_DEATH = -1.0f;
const float RL_REWARD_OBJECTIVE_PROGRESS_FACTOR = 0.1f; // Multiplied by shaping reward from RCBotBase
const float RL_REWARD_OBJECTIVE_COMPLETED = 1.0f;
const float RL_PENALTY_STUCK_OR_IDLE_LONG = -0.01f;
const float RL_PENALTY_FRIENDLY_FIRE = -0.5f;

// Define static const members
const float RCBotRLHelper::MAX_MAP_COORDINATE_DIM = 4096.0f;
const float RCBotRLHelper::MAX_PLAYER_SPEED = 1000.0f; // General max speed
const float RCBotRLHelper::MAX_TIME_SINCE_EVENT = 60.0f; // For normalizing timers

RCBotRLHelper::RCBotRLHelper() : m_accumulatedReward(0.0f) {}

void RCBotRLHelper::reset() {
    m_accumulatedReward = 0.0f;
}

void RCBotRLHelper::addReward(float amount) {
    m_accumulatedReward += amount;
}

float RCBotRLHelper::getAccumulatedRewardAndReset() {
    float reward = m_accumulatedReward;
    m_accumulatedReward = 0.0f;
    return reward;
}

// Note: normalizeValue and normalizeDirection helpers removed as per plan, normalization is inline or part of data prep before calling.

BotState RCBotRLHelper::getCurrentBotState(
    edict_t* pEdict,
    const std::string& currentObjectiveFocusID, // Unused in this direct feature extraction but kept for API
    const Vector& focusedObjectiveLocation,
    bool hasFocusedObjectiveLocation,
    float perceivedPlayerAggression,
    float perceivedPlayerCooperation,
    float timeSinceLastDamageTaken,
    bool isOnGround,
    bool isInWater,
    bool isOnLadder,
    const std::map<int, int>& currentWeaponAmmo,  // Clip
    const std::map<int, int>& currentWeaponMaxClip, // Max Clip
    int currentWeaponId,
    float currentTaskCompletionRatio,
    // New parameters for current enemy from opponent model:
    bool hasCurrentEnemy,
    float currentEnemyThreat,
    const Vector& currentEnemyLocation,
    float distanceToCurrentEnemy,
    const Vector& directionToCurrentEnemy
) const {
    BotState current_s;
    // RLStateProps::NUM_STATE_FEATURES is 36 based on rl_types.h

    if (!pEdict || pEdict->free || (pEdict->v.deadflag != DEAD_NO && pEdict->v.deadflag != DEAD_RESPAWNABLE)) {
        current_s.features.resize(RLStateProps::NUM_STATE_FEATURES, 0.0f); // Initialize with zeros
        return current_s; // Return zeroed state if bot not valid or dead
    }

    current_s.features.clear(); // Clear before reserving if we are not resizing
    current_s.features.reserve(RLStateProps::NUM_STATE_FEATURES);

    // 1. Basic Stats (Normalized [0,1])
    current_s.features.push_back(std::max(0.0f, pEdict->v.health / 100.0f));
    current_s.features.push_back(std::max(0.0f, pEdict->v.armorvalue / 100.0f));

    // 2. Location (Normalized [-1,1] assuming 0,0,0 is map center)
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.origin.x / MAX_MAP_COORDINATE_DIM_H)));
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.origin.y / MAX_MAP_COORDINATE_DIM_H)));
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.origin.z / MAX_MAP_COORDINATE_DIM_H)));

    // 3. Velocity (Normalized [-1,1]) & Speed (Normalized [0,1])
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.velocity.x / MAX_PLAYER_SPEED_H)));
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.velocity.y / MAX_PLAYER_SPEED_H)));
    current_s.features.push_back(std::max(-1.0f, std::min(1.0f, pEdict->v.velocity.z / MAX_PLAYER_SPEED_H)));
    current_s.features.push_back(std::max(0.0f, std::min(1.0f, pEdict->v.velocity.Length() / MAX_PLAYER_SPEED_H)));

    // 4. Status Flags (Binary 0 or 1)
    current_s.features.push_back(isOnGround ? 1.0f : 0.0f);
    current_s.features.push_back(isInWater ? 1.0f : 0.0f);
    current_s.features.push_back(isOnLadder ? 1.0f : 0.0f);
    current_s.features.push_back((pEdict->v.flags & FL_DUCKING) ? 1.0f : 0.0f);
    current_s.features.push_back(pEdict->v.movetype == MOVETYPE_FLY ? 1.0f : 0.0f); // Example for fly/swim

    // 5. Ammo (Normalized [0,1] for 5 weapon slots/IDs 1 through 5 as an example)
    for (int weapon_slot_idx = 0; weapon_slot_idx < 5; ++weapon_slot_idx) {
        int game_weapon_id_to_check = weapon_slot_idx + 1; // Example: slot 0 maps to weapon_id 1
        float norm_ammo = 0.0f;
        auto it_ammo = currentWeaponAmmo.find(game_weapon_id_to_check);
        auto it_max_clip = currentWeaponMaxClip.find(game_weapon_id_to_check);
        if (it_ammo != currentWeaponAmmo.end() && it_max_clip != currentWeaponMaxClip.end() && it_max_clip->second > 0) {
            norm_ammo = std::max(0.0f, static_cast<float>(it_ammo->second) / static_cast<float>(it_max_clip->second));
        } else if (it_ammo != currentWeaponAmmo.end() && it_max_clip != currentWeaponMaxClip.end() && it_max_clip->second == 0) {
            // Weapon exists but has no clip (e.g. knife, grenade, some special weapons)
            // norm_ammo = 1.0f; // Or some other indicator, or rely on currentWeaponId for this.
        }
        current_s.features.push_back(norm_ammo);
    }
    // Current Equipped Weapon ID (normalized)
    current_s.features.push_back(currentWeaponId > 0 ? static_cast<float>(currentWeaponId) / MAX_WEAPON_ID_NORMALIZATION_H : 0.0f);

    // 6. Cooldowns (Placeholders - Normalized [0,1], 1.0 if ready)
    current_s.features.push_back(1.0f); // Placeholder for primary weapon attack_finished time
    current_s.features.push_back(1.0f); // Placeholder for secondary weapon attack_finished time

    // 7. Objective Info (Normalized)
    float norm_dist_to_obj = 0.0f;
    Vector norm_dir_to_obj(0,0,0);
    if (hasFocusedObjectiveLocation) {
        float dist_to_obj_val = (focusedObjectiveLocation - pEdict->v.origin).Length();
        if (dist_to_obj_val > 0.01f) {
            norm_dist_to_obj = std::exp(-dist_to_obj_val / MAX_OBJECTIVE_DISTANCE_NORMALIZATION_H); // Exponential decay
            Vector dir = (focusedObjectiveLocation - pEdict->v.origin).Normalize();
            norm_dir_to_obj.x = dir.x;
            norm_dir_to_obj.y = dir.y;
            norm_dir_to_obj.z = dir.z;
        } else if (dist_to_obj_val <= 0.01f){
            norm_dist_to_obj = 1.0f;
        }
    }
    current_s.features.push_back(norm_dist_to_obj);
    current_s.features.push_back(norm_dir_to_obj.x);
    current_s.features.push_back(norm_dir_to_obj.y);
    current_s.features.push_back(norm_dir_to_obj.z);

    // 8. Bot's Perception Scores (Already [0,1])
    current_s.features.push_back(perceivedPlayerAggression);
    current_s.features.push_back(perceivedPlayerCooperation);

    // 9. Timers (Normalized [0,1], 0 if event just happened, 1 if long ago / maxed out)
    current_s.features.push_back(std::min(1.0f, timeSinceLastDamageTaken / MAX_TIME_SINCE_EVENT_H));

    // 10. Current Task Completion Ratio (Already [0,1])
    current_s.features.push_back(currentTaskCompletionRatio);

    // 11. Opponent Characteristics (Now using passed-in data for current enemy)
    current_s.features.push_back(hasCurrentEnemy ? currentEnemyThreat : 0.5f); // Use 0.5f as neutral baseline if no specific constant from RCBotBase is available here

    // Weapon category and speed category are still placeholders as we don't track these in OpponentStats yet
    current_s.features.push_back(0.0f); // opponent_last_weapon_category
    current_s.features.push_back(0.0f); // opponent_speed_category

    if (hasCurrentEnemy && distanceToCurrentEnemy > 0.001f) { // Check distance to avoid normalization issues if zero
        // Normalize distance: Using exponential decay, similar to objective distance
        // Ensure MAX_MAP_COORDINATE_DIM is appropriate, or use a specific constant for enemy distance normalization if needed.
        current_s.features.push_back(std::exp(-distanceToCurrentEnemy / MAX_MAP_COORDINATE_DIM_H));
        current_s.features.push_back(directionToCurrentEnemy.x);
        current_s.features.push_back(directionToCurrentEnemy.y);
        current_s.features.push_back(directionToCurrentEnemy.z);
    } else { // No enemy or too close to calculate meaningful direction / or invalid distance
        current_s.features.push_back(0.0f); // norm_dist_to_current_enemy
        current_s.features.push_back(0.0f); // norm_dir_to_current_enemy_x
        current_s.features.push_back(0.0f); // norm_dir_to_current_enemy_y
        current_s.features.push_back(0.0f); // norm_dir_to_current_enemy_z
    }
    // The original 6 placeholders are now replaced by:
    // 1. currentEnemyThreat (or baseline)
    // 2. placeholder weapon category
    // 3. placeholder speed category
    // 4. normalized distance to current enemy (or 0)
    // 5. directionToCurrentEnemy.x (or 0)
    // 6. directionToCurrentEnemy.y (or 0)
    // This means one feature (directionToCurrentEnemy.z) is effectively added if we were strictly replacing.
    // The original comment had 6 placeholders. The new logic provides 7 if we count all components of direction.
    // Let's adjust to ensure it's still 6 features for this section to match NUM_STATE_FEATURES = 36.
    // We'll keep Threat, placeholder weapon, placeholder speed, distance, dir.x, dir.y. (Removing dir.z for now to maintain size 36)
    // OR, if the original 6 placeholders were meant to be more generic, we can adjust.
    // The original comment was: OpponentChars(6)
    // New features:
    // 1. currentEnemyThreat
    // 2. (Placeholder) opponent_last_weapon_category
    // 3. (Placeholder) opponent_speed_category
    // 4. norm_dist_to_current_enemy
    // 5. directionToCurrentEnemy.x
    // 6. directionToCurrentEnemy.y
    // This matches the 6 features. The .z component of direction will be omitted for now.

    // Final check: if features.size() is not NUM_STATE_FEATURES, it's an error.
    // For now, assume it matches due to careful push_backs.

    return current_s;
}
