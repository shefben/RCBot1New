#include "rcbot_dynamic_objectives.h"
#include "enginecallback.h" // For gpGlobals, to get mapname if not set
#include "util.h"           // For STRING, FNullEnt, UTIL_ServerPrintf etc.
#include <stdio.h>          // For snprintf
#include <functional>       // For std::hash (if used, though not in current ID gen)
#include <set>              // For std::set (for s_interestingObjectiveClassnames)
#include <algorithm>        // For std::max, std::min
#include <limits>           // For std::numeric_limits

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

// Decay and Pruning Parameters
static const float OBJECTIVE_CONFIDENCE_DECAY_RATE = 0.995f; // Per call to decay, if conditions met
static const float OBJECTIVE_TIME_UNSEEN_FOR_DECAY = 60.0f; // Seconds: Time unseen before confidence starts decaying
static const float OBJECTIVE_MIN_CONFIDENCE_FOR_ACTIVE = 0.1f; // Confidence below which an objective becomes inactive

static const float OBJECTIVE_PRUNE_CONFIDENCE_THRESHOLD = 0.02f; // Confidence below which an inactive objective is a candidate for pruning
static const float OBJECTIVE_TIME_UNSEEN_FOR_PRUNING = 300.0f;  // Seconds: Markedly longer than time for just deactivation

// TD Learning Parameters for Dynamic Objectives
static const float OBJECTIVE_TD_LEARNING_RATE_ALPHA = 0.1f;
static const float OBJECTIVE_TD_DISCOUNT_FACTOR_GAMMA = 0.9f;


DynamicObjectiveManager::DynamicObjectiveManager() {
    if (gpGlobals) { // gpGlobals might be null if manager is constructed very early
        m_currentMapName = STRING(gpGlobals->mapname);
    }
    m_mapBoundsDetermined = false;
    m_nextObjectiveClassnameId = 0;
    m_mapMinBounds = Vector(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    m_mapMaxBounds = Vector(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
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

    bool is_globally_interesting = s_interestingObjectiveClassnames.count(obj_classname) > 0;

    // If discovered via general iteration/spawn, it MUST be globally interesting to be added.
    if ((discovery_event_type == "entity_iteration" || discovery_event_type == "entity_spawn") && !is_globally_interesting) {
        return;
    }
    // If discovered via direct interaction (damage, bump, novelty, pickup), log it even if not globally interesting.
    // The confidence mechanism will then determine its value.

    std::string id = generateUniqueIDForLocation(obj_classname, obj_location);
    if (id.empty()) return; // Could not generate a valid ID

    auto it = m_objective_candidates.find(id);
    float current_time = gpGlobals ? gpGlobals->time : 0.0f;

    if (it == m_objective_candidates.end()) {
        // New candidate
        ObjectiveCandidateMetadata data;
        data.unique_id = id;
        data.entity_classname = obj_classname; // Store the actual classname
        data.location = obj_location;
        data.first_seen_timestamp = current_time;
        data.last_seen_timestamp = current_time;
        data.is_active = true; // Assumed active when first discovered
        data.times_seen_or_touched = 1;

        if (!is_globally_interesting) {
            // For unknown types discovered via interaction, start with lower confidence
            data.confidence = 0.05f;
            // data.is_exploratory_objective = true; // Could add such a flag
        } else {
            data.confidence = 0.1f; // Standard initial confidence for known types (was 0.15f, reduced for consistency)
        }

        if (pEntity && pEntity->v.team > 0 && pEntity->v.team < 3) { // Assuming 1 and 2 are main teams
            data.team_ownership = pEntity->v.team;
        } else if (team_for_trigger > 0 && team_for_trigger < 3) {
            data.team_ownership = team_for_trigger;
        } else {
            // Basic team ownership from classname (very simplified)
            if (obj_classname.find("team1") != std::string::npos || obj_classname.find("_t") != std::string::npos || obj_classname.find("terrorist") != std::string::npos)
                data.team_ownership = 1; // TEAM 1 (e.g. Terrorist)
            else if (obj_classname.find("team2") != std::string::npos || obj_classname.find("_ct") != std::string::npos || obj_classname.find("counter-terrorist") != std::string::npos)
                data.team_ownership = 2; // TEAM 2 (e.g. CT)
            else
                data.team_ownership = 0; // Neutral
        }

        // Add to classname ID map if new and interesting
        if (s_interestingObjectiveClassnames.count(obj_classname) || obj_classname.rfind("trigger_", 0) == 0) {
            if (m_objectiveClassnameToId.find(obj_classname) == m_objectiveClassnameToId.end()) {
                m_objectiveClassnameToId[obj_classname] = m_nextObjectiveClassnameId++;
            }
        }

        m_objective_candidates[id] = data;
        // UTIL_ServerPrintf("DOM: Discovered new objective candidate: %s (Class: %s, Loc: %.0f,%.0f,%.0f, Team: %d)\n",
        //                   id.c_str(), obj_classname.c_str(), obj_location.x, obj_location.y, obj_location.z, data.team_ownership);
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

    auto it = m_objective_candidates.find(objective_id);
    if (it == m_objective_candidates.end()) {
        // UTIL_ServerPrintf("DOM_ERROR: Outcome recorded for unknown objective ID: %s\n", objective_id.c_str());
        return;
    }

    ObjectiveCandidateMetadata& data = it->second;
    if (positive_outcome) {
        data.times_interacted_positive_outcome++;
    } else {
        data.times_interacted_negative_outcome++;
    }

    // UTIL_ServerPrintf("DOM: Objective %s interaction outcome: %s. (Pos: %d, Neg: %d)\n",
    //                   objective_id.c_str(), positive_outcome ? "POSITIVE" : "NEGATIVE",
    //                   data.times_interacted_positive_outcome, data.times_interacted_negative_outcome);

    // Confidence is NO LONGER updated here. It's handled by applyTDUpdate.
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
    if (m_objective_candidates.empty()) return;

    // --- Decay Confidence and Deactivate Objectives ---
    for (auto& pair : m_objective_candidates) {
        ObjectiveCandidateMetadata& data = pair.second;

        if (data.is_active) {
            // Decay confidence if not recently seen or interacted with
            // (Interaction logic would update last_seen_timestamp or a similar interaction_timestamp)
            if (current_time - data.last_seen_timestamp > OBJECTIVE_TIME_UNSEEN_FOR_DECAY) {
                data.confidence *= OBJECTIVE_CONFIDENCE_DECAY_RATE;
                // UTIL_ServerPrintf("DOM: Decaying confidence for active objective %s to %.2f (unseen for %.1fs)\n",
                //                   data.unique_id.c_str(), data.confidence, current_time - data.last_seen_timestamp);

                if (data.confidence < OBJECTIVE_MIN_CONFIDENCE_FOR_ACTIVE) {
                    data.is_active = false;
                    // UTIL_ServerPrintf("DOM: Deactivating objective %s due to low confidence (%.2f)\n",
                    //                   data.unique_id.c_str(), data.confidence);
                }
            }
        } else { // If already inactive, can also decay further, but perhaps slower
            if (current_time - data.last_seen_timestamp > OBJECTIVE_TIME_UNSEEN_FOR_DECAY * 2.0f) { // Slower decay for already inactive ones
                 data.confidence *= (OBJECTIVE_CONFIDENCE_DECAY_RATE + (1.0f - OBJECTIVE_CONFIDENCE_DECAY_RATE) / 2.0f); // Slower rate
            }
        }
    }

    // --- Pruning Stale Objectives ---
    int pruned_count = 0;
    for (auto it = m_objective_candidates.begin(); it != m_objective_candidates.end(); /* manual increment */) {
        ObjectiveCandidateMetadata& data = it->second;
        float time_since_last_seen = current_time - data.last_seen_timestamp;

        if (!data.is_active &&
            data.confidence < OBJECTIVE_PRUNE_CONFIDENCE_THRESHOLD &&
            time_since_last_seen > OBJECTIVE_TIME_UNSEEN_FOR_PRUNING) {

            // UTIL_ServerPrintf("DOM: Pruning stale objective %s (Conf: %.2f, Unseen: %.1fs)\n",
            //                   data.unique_id.c_str(), data.confidence, time_since_last_seen);
            it = m_objective_candidates.erase(it); // Erase returns iterator to the next element
            pruned_count++;
        } else {
            ++it;
        }
    }
    // if (pruned_count > 0) {
    //     UTIL_ServerPrintf("DOM: Pruning complete. Pruned %d objectives.\n", pruned_count);
    // }
}

void DynamicObjectiveManager::determineMapBounds() {
    if (m_objective_candidates.empty()) {
        // Default to some reasonable small area if no objectives yet, or use world size if accessible
        m_mapMinBounds = Vector(-1000, -1000, -1000);
        m_mapMaxBounds = Vector( 1000,  1000,  1000);
        m_mapBoundsDetermined = true; // Consider it "determined" for now
        return;
    }

    m_mapMinBounds = Vector(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    m_mapMaxBounds = Vector(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

    for (const auto& pair : m_objective_candidates) {
        const Vector& loc = pair.second.location;
        m_mapMinBounds.x = std::min(m_mapMinBounds.x, loc.x);
        m_mapMinBounds.y = std::min(m_mapMinBounds.y, loc.y);
        m_mapMinBounds.z = std::min(m_mapMinBounds.z, loc.z);
        m_mapMaxBounds.x = std::max(m_mapMaxBounds.x, loc.x);
        m_mapMaxBounds.y = std::max(m_mapMaxBounds.y, loc.y);
        m_mapMaxBounds.z = std::max(m_mapMaxBounds.z, loc.z);
    }
    m_mapBoundsDetermined = true;
    // UTIL_ServerPrintf("DOM: Map bounds determined: Min(%.0f,%.0f,%.0f) Max(%.0f,%.0f,%.0f)\n",
    //     m_mapMinBounds.x, m_mapMinBounds.y, m_mapMinBounds.z,
    //     m_mapMaxBounds.x, m_mapMaxBounds.y, m_mapMaxBounds.z);
}


std::vector<float> DynamicObjectiveManager::getObjectiveFeatureVector(const ObjectiveCandidateMetadata& objective) const {
    std::vector<float> features;

    if (!m_mapBoundsDetermined) {
        // This might happen if called before determineMapBounds or if no objectives.
        // Return empty or a default vector. For clustering, this is problematic.
        // Let's assume determineMapBounds has been called.
        // If not, the calling function (clusterObjectives) should call it.
        // For safety, if bounds are default, features might not be well-normalized.
        // UTIL_ServerPrintf("DOM_WARNING: getObjectiveFeatureVector called before map bounds determined.\n");
    }

    // 1. Normalized Location Features
    float range_x = m_mapMaxBounds.x - m_mapMinBounds.x;
    float range_y = m_mapMaxBounds.y - m_mapMinBounds.y;
    float range_z = m_mapMaxBounds.z - m_mapMinBounds.z;

    features.push_back(range_x > 1.0f ? std::max(0.0f, std::min(1.0f, (objective.location.x - m_mapMinBounds.x) / range_x)) : 0.5f);
    features.push_back(range_y > 1.0f ? std::max(0.0f, std::min(1.0f, (objective.location.y - m_mapMinBounds.y) / range_y)) : 0.5f);
    features.push_back(range_z > 1.0f ? std::max(0.0f, std::min(1.0f, (objective.location.z - m_mapMinBounds.z) / range_z)) : 0.5f);

    // 2. Classname Feature (normalized ID)
    float class_feature = 0.0f;
    auto it = m_objectiveClassnameToId.find(objective.entity_classname);
    if (it != m_objectiveClassnameToId.end() && m_nextObjectiveClassnameId > 0) {
        class_feature = static_cast<float>(it->second) / static_cast<float>(m_nextObjectiveClassnameId -1); // Normalize by max ID seen
                                                                                                           // (m_next... is one greater than max ID)
    }
    features.push_back(std::max(0.0f, std::min(1.0f, class_feature)));


    // 3. Team Ownership Feature (could be one-hot encoded if few teams, or simple normalized value)
    // Example: 0 for neutral, 0.5 for team1, 1.0 for team2 (assuming 2 main teams)
    if (objective.team_ownership == 0) features.push_back(0.0f);
    else if (objective.team_ownership == 1) features.push_back(0.5f);
    else if (objective.team_ownership == 2) features.push_back(1.0f);
    else features.push_back(0.0f); // Default for other teams

    // 4. Confidence (already 0-1)
    features.push_back(objective.confidence);

    return features;
}

void DynamicObjectiveManager::clusterObjectives(int k_num_clusters) {
    if (m_objective_candidates.empty() || k_num_clusters <= 0) {
        UTIL_ServerPrintf("DOM: Not enough candidates or k is non-positive for clustering. Candidates: %d, K: %d\n",
            m_objective_candidates.size(), k_num_clusters);
        return;
    }

    if (m_objective_candidates.size() < (size_t)k_num_clusters) {
         UTIL_ServerPrintf("DOM: Number of candidates (%d) is less than k_num_clusters (%d). Setting k to number of candidates.\n",
            m_objective_candidates.size(), k_num_clusters);
        k_num_clusters = m_objective_candidates.size();
    }

    if (!m_mapBoundsDetermined) {
        determineMapBounds();
    }

    // Prepare data points (feature vectors)
    std::vector<std::pair<std::string, std::vector<float>>> data_points;
    for (const auto& pair : m_objective_candidates) {
        if(pair.second.is_active) { // Only cluster active objectives
            data_points.push_back({pair.first, getObjectiveFeatureVector(pair.second)});
        }
    }

    if (data_points.size() < (size_t)k_num_clusters) {
        UTIL_ServerPrintf("DOM: Not enough active candidates (%d) for K-Means with K=%d.\n", data_points.size(), k_num_clusters);
        // Assign all to cluster 0 or handle differently
        for (auto& pair_data : data_points) {
            ObjectiveCandidateMetadata* objective_meta = getObjectiveCandidateById(pair_data.first);
            if(objective_meta) objective_meta->cluster_id = 0;
        }
        return;
    }


    // Initialize Centroids: K-Means++ like initialization (pick first randomly, then others based on distance)
    // For simplicity, a basic random sampling of initial distinct points is often sufficient as a start.
    std::vector<std::vector<float>> centroids(k_num_clusters);
    std::vector<int> chosen_indices;
    std::random_device rd;
    std::mt19937 gen(rd());

    // Ensure data_points has enough elements for k_num_clusters unique centroids
    if (data_points.size() == 0) {
        UTIL_ServerPrintf("DOM_ERROR: No active data points to select centroids from for K-Means.\n");
        return;
    }

    std::uniform_int_distribution<> distrib_idx(0, data_points.size() - 1);
    for (int i = 0; i < k_num_clusters; ++i) {
        int rand_idx = distrib_idx(gen);
        // Ensure distinct centroids if possible, simple retry here, better is to pick from remaining.
        // For now, this might pick same centroid multiple times if k is close to data_points.size()
        // A better way for small k is to shuffle data_points and pick first k.
        centroids[i] = data_points[rand_idx].second;
    }


    const int MAX_ITERATIONS = 100;
    bool changed = true;

    for (int iter = 0; iter < MAX_ITERATIONS && changed; ++iter) {
        changed = false;

        // Assignment Step
        for (auto& point_pair : data_points) {
            const std::string& objective_id = point_pair.first;
            const std::vector<float>& features = point_pair.second;

            if (features.empty()) continue;

            int nearest_centroid_idx = -1;
            float min_dist_sq = std::numeric_limits<float>::max();

            for (int j = 0; j < k_num_clusters; ++j) {
                if (centroids[j].size() != features.size()) { // Should not happen if initialized correctly
                     // UTIL_ServerPrintf("DOM_KMEANS_ERROR: Feature size mismatch with centroid %d!\n", j);
                     continue;
                }
                float current_dist_sq = 0.0f;
                for (size_t feat_idx = 0; feat_idx < features.size(); ++feat_idx) {
                    current_dist_sq += std::pow(features[feat_idx] - centroids[j][feat_idx], 2);
                }
                if (current_dist_sq < min_dist_sq) {
                    min_dist_sq = current_dist_sq;
                    nearest_centroid_idx = j;
                }
            }

            ObjectiveCandidateMetadata* objective_meta = getObjectiveCandidateById(objective_id);
            if (objective_meta && objective_meta->cluster_id != nearest_centroid_idx) {
                objective_meta->cluster_id = nearest_centroid_idx;
                changed = true;
            }
        }

        // Update Step
        if (changed) { // Only update centroids if assignments changed
            std::vector<std::vector<float>> new_centroids(k_num_clusters);
            std::vector<int> cluster_counts(k_num_clusters, 0);

            // Initialize new_centroids with zeros (assuming feature vectors are not empty)
            if(!data_points.empty() && !data_points[0].second.empty()){
                for(int i=0; i<k_num_clusters; ++i) new_centroids[i].resize(data_points[0].second.size(), 0.0f);
            } else { // No data points or features, cannot proceed
                UTIL_ServerPrintf("DOM_KMEANS_ERROR: No features to update centroids.\n");
                break;
            }


            for (const auto& point_pair : data_points) {
                 ObjectiveCandidateMetadata* objective_meta = getObjectiveCandidateById(point_pair.first);
                 if (objective_meta && objective_meta->cluster_id != -1) { // Check if assigned to a cluster
                    const std::vector<float>& features = point_pair.second;
                    for (size_t feat_idx = 0; feat_idx < features.size(); ++feat_idx) {
                        new_centroids[objective_meta->cluster_id][feat_idx] += features[feat_idx];
                    }
                    cluster_counts[objective_meta->cluster_id]++;
                 }
            }

            for (int j = 0; j < k_num_clusters; ++j) {
                if (cluster_counts[j] > 0) {
                    for (size_t feat_idx = 0; feat_idx < new_centroids[j].size(); ++feat_idx) {
                        new_centroids[j][feat_idx] /= static_cast<float>(cluster_counts[j]);
                    }
                } else {
                    // Handle empty cluster: re-initialize centroid
                    // Pick a random data point to be the new centroid for this empty cluster
                    if (!data_points.empty()) {
                        new_centroids[j] = data_points[distrib_idx(gen)].second;
                        // UTIL_ServerPrintf("DOM_KMEANS: Re-initializing empty cluster %d with random point.\n", j);
                    }
                }
            }
            centroids = new_centroids;
        }
    } // End iteration loop

    UTIL_ServerPrintf("DOM: Clustering complete. %d active objectives into %d clusters.\n",
        data_points.size(), k_num_clusters);

    // For debugging, print cluster assignments
    // for (const auto& pair : m_objective_candidates) {
    //     if (pair.second.is_active) {
    //         UTIL_ServerPrintf("  Objective: %s, Class: %s, Cluster: %d\n",
    //                           pair.first.c_str(), pair.second.entity_classname.c_str(), pair.second.cluster_id);
    //     }
    // }
}

bool DynamicObjectiveManager::isClassnameGloballyInteresting(const std::string& classname) const {
    return s_interestingObjectiveClassnames.count(classname) > 0;
}

void DynamicObjectiveManager::applyTDUpdate(const std::string& objective_id,
                                           float immediate_reward,
                                           const std::string& next_objective_id,
                                           float explicit_next_objective_value,
                                           bool is_terminal_transition) {
    ObjectiveCandidateMetadata* current_obj_meta = getObjectiveCandidateById(objective_id);
    if (!current_obj_meta) {
        // UTIL_ServerPrintf("DOM_TD_ERROR: Current objective '%s' not found for TD update.\n", objective_id.c_str());
        return;
    }

    float v_s = current_obj_meta->confidence; // Current value V(s)
    float v_s_prime = 0.0f;                   // Value of next state V(s')

    if (is_terminal_transition) {
        v_s_prime = explicit_next_objective_value;
        // For example, if round ends, explicit_next_objective_value might be 0 (if reward captured everything)
        // or a fixed value representing the game's end state if not incorporated in immediate_reward.
    } else if (!next_objective_id.empty()) {
        ObjectiveCandidateMetadata* next_obj_meta = getObjectiveCandidateById(next_objective_id);
        if (next_obj_meta) {
            v_s_prime = next_obj_meta->confidence; // V(s') from the next known objective
        } else {
            // UTIL_ServerPrintf("DOM_TD_WARNING: Next objective '%s' not found. Using explicit_next_objective_value (%.2f) for V(s').\n",
            //                   next_objective_id.c_str(), explicit_next_objective_value);
            v_s_prime = explicit_next_objective_value; // Fallback if next_objective_id is given but not found
        }
    } else {
        // No next_objective_id provided, and not explicitly terminal, so use explicit_next_objective_value.
        // This case might represent transitions where the next distinct 'objective state' isn't clear,
        // so we rely on the caller to provide an estimated value for what follows.
        v_s_prime = explicit_next_objective_value;
    }

    // TD Update: V(s) = V(s) + alpha * (reward + gamma * V(s') - V(s))
    float new_value = v_s + OBJECTIVE_TD_LEARNING_RATE_ALPHA *
                          (immediate_reward + OBJECTIVE_TD_DISCOUNT_FACTOR_GAMMA * v_s_prime - v_s);

    // Clamp new_value. Let's use a range like [-1.0, 1.0] as objectives can be good or bad.
    current_obj_meta->confidence = std::max(-1.0f, std::min(1.0f, new_value));

    // Optional: Log the update for debugging
    // char buffer[256];
    // snprintf(buffer, sizeof(buffer), "DOM_TD_UPDATE: Obj '%s', V(s_old)=%.3f, R=%.2f, V(s_prime)=%.3f -> V(s_new)=%.3f (is_term=%d, next_id=%s)\n",
    //                   objective_id.c_str(), v_s, immediate_reward, v_s_prime, current_obj_meta->confidence,
    //                   is_terminal_transition, next_objective_id.empty() ? "N/A" : next_objective_id.c_str());
    // UTIL_ServerPrintf(buffer); // Or your preferred logging mechanism
}
