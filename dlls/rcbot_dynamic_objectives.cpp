#include "rcbot_dynamic_objectives.h"
#include "enginecallback.h" // For gpGlobals, to get mapname if not set
#include "util.h"           // For STRING, FNullEnt, UTIL_ServerPrintf etc.
#include <stdio.h>          // For snprintf
#include <functional>       // For std::hash (if used, though not in current ID gen)
#include <set>              // For std::set (for s_interestingObjectiveClassnames)
#include <algorithm>        // For std::max, std::min

// Define the global instance
DynamicObjectiveManager g_ObjectiveManager;

// Static list of classnames considered interesting for automatic discovery
static const std::set<std::string> s_interestingObjectiveClassnames = {
    "func_bomb_target", "info_bomb_target",         // CS Bomb Target
    "item_flag_team1", "item_flag_team2",           // CTF Flags (example, actual names vary)
    "info_tfgoal",                                  // TFC Flag return point
    "info_capture_point", "info_control_point",     // DoD, TF2-like Capture Points
    "trigger_multiple", "trigger_once",             // Generic Triggers (might need more context)
    "func_button", "momentary_rot_button",          // Buttons
    "func_door", "func_door_rotating",             // Doors
    "hostage_entity", "monster_hostage",            // CS Hostages
    "weaponbox", "item_weapon", "armoury_entity",   // Weapons or weapon sources
    "item_healthkit", "item_battery",               // Health/Armor
    "item_ammobox"                                  // Ammo
    // Note: Some objectives might be defined by triggers (e.g. "trigger_teleport", "trigger_push")
    // or game-specific entities not listed here. This list is a starting point.
};

DynamicObjectiveManager::DynamicObjectiveManager() {
    if (gpGlobals) { // gpGlobals might be null if manager is constructed very early
        m_currentMapName = STRING(gpGlobals->mapname);
    }
    // Example: Load interesting classnames (can be from a config file later)
    // m_interesting_objective_classnames.insert("func_button");
    // m_interesting_objective_classnames.insert("hostage_entity");
    // m_interesting_objective_classnames.insert("item_bomb");
    // UTIL_ServerPrintf("DynamicObjectiveManager: Initialized. Current map (on construct): %s\n", m_currentMapName.c_str());
}

DynamicObjectiveManager::~DynamicObjectiveManager() {
    // Cleanup if needed
}

void DynamicObjectiveManager::setCurrentMapName(const std::string& mapName) {
    if (m_currentMapName != mapName) {
        UTIL_ServerPrintf("DynamicObjectiveManager: Map changed from %s to %s. Clearing all objectives.\n",
            m_currentMapName.c_str(), mapName.c_str());
        clearAllObjectives(); // Clear objectives if map name changes
        m_currentMapName = mapName;
    } else if (m_currentMapName.empty() && !mapName.empty()){
         m_currentMapName = mapName; // Set if it was empty
    }
}


std::string DynamicObjectiveManager::generateUniqueIDForLocation(const std::string& base_name, const Vector& location) {
    if (m_currentMapName.empty() && gpGlobals) {
         m_currentMapName = STRING(gpGlobals->mapname); // Fallback if not set by setCurrentMapName
    }
    char id_buf[256];
    // Using int casting for vector components to make IDs cleaner and less prone to float precision issues in strings.
    // This implies a grid-like discretization for location-based IDs.
    snprintf(id_buf, sizeof(id_buf), "%s_%s_%.0f_%.0f_%.0f",
             m_currentMapName.c_str(),
             base_name.c_str(),
             location.x, location.y, location.z);
    return std::string(id_buf);
}

std::string DynamicObjectiveManager::generateUniqueIDForEntity(edict_t* pEntity) {
    if (FNullEnt(pEntity)) return "";

    // Option 1: Classname + Location (good for static or respawning items at fixed spots)
    return generateUniqueIDForLocation(STRING(pEntity->v.classname), pEntity->v.origin);

    // Option 2: Classname + Entity Index (good if index is somewhat persistent for *that specific instance*)
    // char id_buf[256];
    // snprintf(id_buf, sizeof(id_buf), "%s_%s_%d",
    //          m_currentMapName.c_str(),
    //          STRING(pEntity->v.classname),
    //          ENTINDEX(pEntity));
    // return std::string(id_buf);

    // Option 3: Using pEntity->serialnumber if the mod guarantees its uniqueness and persistence
    // if (pEntity->serialnumber > 0) { ... }
}


void DynamicObjectiveManager::discoverObjectiveCandidate(edict_t* pEntity, const Vector& loc, const std::string& class_or_type_name_param,
                                                       const std::string& discovery_event_type, int team_for_trigger /*= 0*/) {
    std::string obj_classname = class_or_type_name_param;
    Vector obj_location = loc;
    // int ent_index = -1; // For debug or alternative ID

    if (pEntity && !FNullEnt(pEntity)) { // If pEntity is provided and valid, use its details preferentially
        obj_classname = STRING(pEntity->v.classname);
        obj_location = pEntity->v.origin;
        // ent_index = ENTINDEX(pEntity);
    } else if (obj_classname.empty()) { // If no pEntity and no classname, invalid
        return;
    }

    // Filter by interesting classnames if event_type is entity-based and from general iteration
    if ((discovery_event_type == "entity_iteration" || discovery_event_type == "entity_spawn")) {
        if (s_interestingObjectiveClassnames.find(obj_classname) == s_interestingObjectiveClassnames.end()) {
            // If it's a trigger, we might still be interested even if not in the primary list,
            // but for now, strict filtering for entity_iteration/spawn.
            if (obj_classname.rfind("trigger_", 0) != 0) { // A simple check if it starts with trigger_
                 //return; // Not in our list of objectives to automatically discover from iteration/spawn
            }
            // For now, let's allow all triggers to pass this initial filter if not explicitly denied,
            // their confidence will remain low unless something else boosts them.
            // A more refined system would have categories of interesting classnames.
            // For this phase, the main filter is being in s_interestingObjectiveClassnames.
            // So, if not found, we return.
            if (s_interestingObjectiveClassnames.find(obj_classname) == s_interestingObjectiveClassnames.end()) {
                 return;
            }
        }
    }
    // For other discovery_event_types like "trigger_touch_player" triggered by specific game logic,
    // we might always log it, or that logic should pre-filter.

    std::string id = generateUniqueIDForLocation(obj_classname, obj_location);
    if (id.empty()) return; // Could not generate a valid ID

    auto it = m_objective_candidates.find(id);
    float current_time = gpGlobals ? gpGlobals->time : 0.0f;

    if (it == m_objective_candidates.end()) {
        // New candidate
        ObjectiveCandidateMetadata data;
        data.unique_id = id;
        data.entity_classname = obj_classname;
        data.location = obj_location;
        data.first_seen_timestamp = current_time;
        data.last_seen_timestamp = current_time;
        data.is_active = true; // Assumed active when first discovered
        data.confidence = 0.15f; // Slightly higher initial confidence for being on the "interesting" list
        data.times_seen_or_touched = 1;

        if (pEntity && pEntity->v.team > 0 && pEntity->v.team < 3) { // Assuming 1 and 2 are main teams
            data.team_ownership = pEntity->v.team;
        } else if (team_for_trigger > 0 && team_for_trigger < 3) {
            data.team_ownership = team_for_trigger;
        } else {
            // Basic team ownership from classname (very simplified)
            if (obj_classname.find("team1") != std::string::npos || obj_classname.find("_t") != std::string::npos)
                data.team_ownership = 1;
            else if (obj_classname.find("team2") != std::string::npos || obj_classname.find("_ct") != std::string::npos)
                data.team_ownership = 2;
            else
                data.team_ownership = 0; // Neutral
        }

        m_objective_candidates[id] = data;
        UTIL_ServerPrintf("DOM: Discovered new objective candidate: %s (Class: %s, Loc: %.0f,%.0f,%.0f, Team: %d)\n",
                          id.c_str(), obj_classname.c_str(), obj_location.x, obj_location.y, obj_location.z, data.team_ownership);
    } else {
        // Existing candidate
        it->second.last_seen_timestamp = current_time;
        it->second.is_active = true; // Mark active if seen/interacted with again
        it->second.times_seen_or_touched++;
        // Confidence might be slightly boosted on re-discovery too, or handled by interaction outcomes.
        // it->second.confidence = std::min(1.0f, it->second.confidence + 0.01f);
    }
}

void DynamicObjectiveManager::recordObjectiveInteractionOutcome(const std::string& objective_id, bool positive_outcome) {
    // Placeholder stub
    // UTIL_ServerPrintf("DOM: recordObjectiveInteractionOutcome for ID %s, outcome: %s\n",
    //    objective_id.c_str(), positive_outcome ? "positive" : "negative");
    // Logic for this will be in Phase 2 (6.2)
}

void DynamicObjectiveManager::updateObjectiveConfidence(const std::string& objective_id, float change) {
    // Placeholder stub
    auto it = m_objective_candidates.find(objective_id);
    if (it != m_objective_candidates.end()) {
        it->second.confidence += change;
        it->second.confidence = std::max(0.0f, std::min(1.0f, it->second.confidence)); // Clamp
        // UTIL_ServerPrintf("DOM: Updated confidence for objective ID %s by %.2f to %.2f\n",
        //    objective_id.c_str(), change, it->second.confidence);
    }
    // Logic for this will be in Phase 2 & 3 (6.2, 6.3)
}

const std::map<std::string, ObjectiveCandidateMetadata>& DynamicObjectiveManager::getObjectiveCandidates() const {
    return m_objective_candidates;
}

ObjectiveCandidateMetadata* DynamicObjectiveManager::getObjectiveCandidateById(const std::string& objective_id) {
    auto it = m_objective_candidates.find(objective_id);
    if (it != m_objective_candidates.end()) {
        return &(it->second);
    }
    return nullptr;
}

void DynamicObjectiveManager::clearAllObjectives() {
    m_objective_candidates.clear();
    UTIL_ServerPrintf("DynamicObjectiveManager: All objectives cleared.\n");
}

void DynamicObjectiveManager::clearObjectivesOnNewRound() {
    // This might not clear all objectives, but rather reset some of their dynamic properties.
    // For example, interaction counts for the round, but keep the objective itself and its confidence.
    // Or, if objectives are truly round-specific, then clear them.
    // For now, let's assume it resets interaction counts but keeps discovered objectives.
    for (auto& pair : m_objective_candidates) {
        pair.second.times_interacted_positive_outcome = 0;
        pair.second.times_interacted_negative_outcome = 0;
        // pair.second.times_seen_or_touched = 0; // Might want to keep this across rounds
        pair.second.is_active = true; // Re-evaluate activity at round start
    }
    UTIL_ServerPrintf("DynamicObjectiveManager: Objective interaction counts reset for new round.\n");
}

void DynamicObjectiveManager::decayAndUpdateObjectives(float current_time) {
    // Placeholder stub
    // Logic for this will be in Phase 4 (6.4)
    // Example decay:
    // for (auto& pair : m_objective_candidates) {
    //     if (pair.second.is_active) {
    //         // Decay confidence if not recently updated or interacted with
    //         if (current_time - pair.second.last_seen_timestamp > SOME_THRESHOLD) {
    //             pair.second.confidence *= CONFIDENCE_DECAY_RATE;
    //             if (pair.second.confidence < MIN_CONFIDENCE_TO_BE_ACTIVE) {
    //                 pair.second.is_active = false;
    //             }
    //         }
    //     }
    // }
}
