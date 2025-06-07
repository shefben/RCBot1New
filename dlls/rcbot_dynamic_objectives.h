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

    ObjectiveCandidateMetadata() :
        team_ownership(0), confidence(0.1f),
        times_interacted_positive_outcome(0), times_interacted_negative_outcome(0),
        times_seen_or_touched(0),
        first_seen_timestamp(0.0f), last_seen_timestamp(0.0f), is_active(true) {
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
    ObjectiveCandidateMetadata* getObjectiveCandidateById(const std::string& objective_id); // Non-const version

    void clearObjectivesOnNewRound(); // Resets some stats, not full clear (e.g. interaction counts)
    void clearAllObjectives();      // Full clear, e.g. on map change / server shutdown

    // For later phases
    void decayAndUpdateObjectives(float current_time); // Handles confidence decay, activity status
    // void clusterObjectives();
    // void shareObjectives();

    // Helper
    // Generates ID based on a base name (e.g. classname, trigger_name) and location.
    std::string generateUniqueIDForLocation(const std::string& base_name, const Vector& location);
    // Generates ID based on an entity. May use its classname and location, or a persistent entity index if available.
    std::string generateUniqueIDForEntity(edict_t* pEntity);

    // Call this on map load to set the current map name context
    void setCurrentMapName(const std::string& mapName);


private:
    std::map<std::string, ObjectiveCandidateMetadata> m_objective_candidates;
    std::string m_currentMapName;

    // Example: Set of classnames considered inherently interesting for objective discovery
    // std::set<std::string> m_interesting_objective_classnames;
};

// Global instance
extern DynamicObjectiveManager g_ObjectiveManager;

#endif // RCBOT_DYNAMIC_OBJECTIVES_H
