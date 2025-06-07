#ifndef RCBOT_DYNAMIC_OBJECTIVES_H
#define RCBOT_DYNAMIC_OBJECTIVES_H

#include <string>
#include <vector>
#include <map>
#include <set>      // Potentially for m_interesting_objective_classnames
#include <functional> // For std::hash if used in ID generation
#include "extdll.h" // For Vector, edict_t, gpGlobals
#include "util.h"   // For STRING() and potentially other utilities

// Forward declaration
class DynamicObjectiveManager;

enum class ObjectiveCategoryType {
    UNKNOWN = 0,
    BOMB_SITE,          // CS: Plant bomb area
    RESCUE_ZONE,        // CS: Hostage rescue area
    HOSTAGE_ENTITY,     // CS: Individual hostage entity
    FLAG_STAND,         // CTF: Enemy flag's home location / our flag if returned
    FLAG_CAPTURE_POINT, // CTF: Location to bring enemy flag to score
    CONTROL_POINT,      // DoD/TFC: Area to capture/control
    WEAPON_ITEM,        // Specific weapon pickup
    AMMO_ITEM,          // Specific ammo pickup
    HEALTH_ITEM,        // Specific health pickup
    ARMOR_ITEM,         // Specific armor pickup
    KEY_ITEM,           // Generic important item/pickup for a map
    NAV_EXPLORE_POINT,  // Bot-generated exploration goal (less of a world objective)
    GENERIC_TRIGGER,    // Game-specific trigger with unknown function initially
    BUTTON_ENTITY,      // A pressable button
    DOOR_ENTITY,        // A door that might be an objective to open/pass
    OBJECTIVE_ITEM_HELD // e.g. bot is carrying the flag/bomb
    // Add more as common patterns are identified
};

inline std::string objectiveCategoryToString(ObjectiveCategoryType category) {
    switch (category) {
        case ObjectiveCategoryType::UNKNOWN: return "Unknown";
        case ObjectiveCategoryType::BOMB_SITE: return "BombSite";
        case ObjectiveCategoryType::RESCUE_ZONE: return "RescueZone";
        case ObjectiveCategoryType::HOSTAGE_ENTITY: return "HostageEntity";
        case ObjectiveCategoryType::FLAG_STAND: return "FlagStand";
        case ObjectiveCategoryType::FLAG_CAPTURE_POINT: return "FlagCapture";
        case ObjectiveCategoryType::CONTROL_POINT: return "ControlPoint";
        case ObjectiveCategoryType::WEAPON_ITEM: return "WeaponItem";
        case ObjectiveCategoryType::AMMO_ITEM: return "AmmoItem";
        case ObjectiveCategoryType::HEALTH_ITEM: return "HealthItem";
        case ObjectiveCategoryType::ARMOR_ITEM: return "ArmorItem";
        case ObjectiveCategoryType::KEY_ITEM: return "KeyItem";
        case ObjectiveCategoryType::NAV_EXPLORE_POINT: return "NavExplore";
        case ObjectiveCategoryType::GENERIC_TRIGGER: return "GenericTrigger";
        case ObjectiveCategoryType::BUTTON_ENTITY: return "Button";
        case ObjectiveCategoryType::DOOR_ENTITY: return "Door";
        case ObjectiveCategoryType::OBJECTIVE_ITEM_HELD: return "HeldObjectiveItem";
        default: return "CategoryUndefined";
    }
}

struct ObjectiveCandidateMetadata {
    std::string entity_classname; // Classname of the entity, if applicable
    Vector location;              // World location (e.g., pEntity->v.origin, or a specific point)
    int team_ownership;           // 0 for neutral, 1 for T, 2 for CT (game-specific)

    float confidence;             // Belief this is a valuable objective (0.0 to 1.0)

    int times_interacted_positive_outcome;
    int times_interacted_negative_outcome;
    int times_seen_or_touched;    // General interaction/discovery count

    std::string unique_id;        // Generated ID, e.g., mapname_type_X_Y_Z
    float first_seen_timestamp;
    float last_seen_timestamp;
    bool  is_active;              // Still present in the map / relevant for current game phase
    int   cluster_id;             // ID of the cluster this objective belongs to
    ObjectiveCategoryType category_tag; // Category of the objective

    int rounds_active_in_win;     // How many rounds this objective was active when "our" team won
    int rounds_active_in_loss;    // How many rounds this objective was active when "our" team lost

    ObjectiveCandidateMetadata() :
        team_ownership(0), confidence(0.1f),
        times_interacted_positive_outcome(0), times_interacted_negative_outcome(0),
        times_seen_or_touched(0),
        first_seen_timestamp(0.0f), last_seen_timestamp(0.0f), is_active(true),
        cluster_id(-1), category_tag(ObjectiveCategoryType::UNKNOWN),
        rounds_active_in_win(0), rounds_active_in_loss(0) {
        // location will be zero-initialized by Vector's default constructor
    }
};

class DynamicObjectiveManager {
public:
    DynamicObjectiveManager();
    ~DynamicObjectiveManager();

    // Core methods for Phase 1
    // discovery_event_type: "entity_spawn", "trigger_touch_player", "heard_about_from_cvar", "seen_item", "player_report"
    // team_for_trigger: Relevant for objectives like button that only one team should press.
    void discoverObjectiveCandidate(edict_t* pEntity, const Vector& loc, const std::string& class_or_type_name,
                                    const std::string& discovery_event_type, int team_for_trigger = 0);

    void recordObjectiveInteractionOutcome(const std::string& objective_id, bool positive_outcome);

    // Confidence update - simple for now, placeholder for TD later
    void updateObjectiveConfidence(const std::string& objective_id, float change);

    const std::map<std::string, ObjectiveCandidateMetadata>& getObjectiveCandidates() const;
    std::map<std::string, ObjectiveCandidateMetadata>& getMutableObjectiveCandidates(); // Added for updating stats
    ObjectiveCandidateMetadata* getObjectiveCandidateById(const std::string& objective_id); // Non-const version

    void clearObjectivesOnNewRound(); // Resets some stats, not full clear (e.g. interaction counts)
    void clearAllObjectives();      // Full clear, e.g. on map change / server shutdown

    // For later phases
    void decayAndUpdateObjectives(float current_time); // Handles confidence decay, activity status
    void clusterObjectives(int k_num_clusters = 5); // K-Means clustering
    // void shareObjectives();

    // Helper
    // Generates ID based on a base name (e.g. classname, trigger_name) and location.
    std::string generateUniqueIDForLocation(const std::string& base_name, const Vector& location);
    // Generates ID based on an entity. May use its classname and location, or a persistent entity index if available.
    std::string generateUniqueIDForEntity(edict_t* pEntity);

    // Call this on map load to set the current map name context
    void setCurrentMapName(const std::string& mapName);

    // Checks if a classname is part of the globally interesting set
    bool isClassnameGloballyInteresting(const std::string& classname) const;

    // Applies a Temporal Difference update to an objective's confidence (value)
    void applyTDUpdate(const std::string& objective_id,
                       float immediate_reward,
                       const std::string& next_objective_id, // If empty, implies terminal or next state value is explicit_next_objective_value
                       float explicit_next_objective_value = 0.0f,
                       bool is_terminal_transition = false);

    // Infers and sets the category_tag for all current objective candidates
    void inferObjectiveCategories();

private:
    std::map<std::string, ObjectiveCandidateMetadata> m_objective_candidates;
    std::string m_currentMapName;

    // For feature extraction and clustering
    Vector m_mapMinBounds;
    Vector m_mapMaxBounds;
    bool m_mapBoundsDetermined;
    std::map<std::string, int> m_objectiveClassnameToId;
    int m_nextObjectiveClassnameId;

    std::vector<float> getObjectiveFeatureVector(const ObjectiveCandidateMetadata& objective) const; // Removed bounds params for now, calculate on demand or store normalized
    void determineMapBounds();


    // Example: Set of classnames considered inherently interesting for objective discovery
    // std::set<std::string> m_interesting_objective_classnames;
};

// Global instance
extern DynamicObjectiveManager g_ObjectiveManager;

#endif // RCBOT_DYNAMIC_OBJECTIVES_H
