#include "rcbot_schedule_obj_interaction.h"
#include "rcbot_base.h"
#include "rcbot_dynamic_objectives.h"
#include "enginecallback.h" // For gpGlobals
#include "util.h"           // For UTIL_ServerPrintf (debugging), MAKE_VECTORS
#include "util_shared.h"    // For DotProduct, NormalizeSafe (if available, else use Vector::Normalize())
#include "in_buttons.h"     // For IN_USE
#include "RCBotRLHelper.h"  // For RLConsts namespace

// Constants for the schedule
static const float NAVIGATION_STOP_DISTANCE_DEFAULT = 50.0f;
static const float NAVIGATION_STOP_DISTANCE_TOUCH = 32.0f;
static const float ALIGNMENT_DOT_PRODUCT_THRESHOLD = 0.95f;
static const int MAX_ALIGNMENT_ATTEMPTS = 15;
static const uint8_t OBJECTIVE_INTERACTION_PRIORITY = 4;
static const float OBJECTIVE_VALIDITY_CHECK_INTERVAL = 0.5f; // Seconds

ScheduleExecuteObjectiveInteraction::ScheduleExecuteObjectiveInteraction(
    RCBotBase* pBot,
    const std::string& objective_id,
    ObjectiveInteractionType interaction_type,
    float interaction_duration)
    : RCBotSchedule(pBot),
      m_targetObjectiveID(objective_id),
      m_interactionType(interaction_type),
      m_interactionDurationNeeded(interaction_duration),
      m_currentState(InteractionState::NAVIGATING), // Start with NAVIGATING
      m_interactionTimer(0.0f),
      m_alignAttempts(0),
      m_maxAlignAttempts(MAX_ALIGNMENT_ATTEMPTS),
      m_fLastScheduleUpdateTimestamp(0.0f) { // Initialize new member

    if (!initializeObjectiveData(pBot)) {
        m_currentState = InteractionState::INIT_FAIL;
        // UTIL_ServerPrintf("DEBUG ScheduleExecuteObjectiveInteraction: FAILED to initialize objective data for %s\n", m_targetObjectiveID.c_str());
    } else {
        if (m_interactionType == ObjectiveInteractionType::TOUCH_TO_ACTIVATE) {
            m_navStopDistance = NAVIGATION_STOP_DISTANCE_TOUCH;
        } else {
            m_navStopDistance = NAVIGATION_STOP_DISTANCE_DEFAULT;
        }
        // UTIL_ServerPrintf("DEBUG ScheduleExecuteObjectiveInteraction: Created for obj %s, type %d, stop_dist %.1f\n",
        //    m_targetObjectiveID.c_str(), static_cast<int>(m_interactionType), m_navStopDistance);
    }
    if (gpGlobals) m_fLastScheduleUpdateTimestamp = gpGlobals->time; // Initialize with current time
}

ScheduleExecuteObjectiveInteraction::~ScheduleExecuteObjectiveInteraction() {}

bool ScheduleExecuteObjectiveInteraction::initializeObjectiveData(RCBotBase* pBot) {
    if (!pBot || !pBot->getEdict()) return false;
    ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(m_targetObjectiveID);
    if (obj_meta && obj_meta->is_active) {
        m_targetObjectiveLocation = obj_meta->location;
        return true;
    }
    // UTIL_ServerPrintf("DEBUG ScheduleExecuteObjectiveInteraction: Failed to init/re-init objective data for %s. Meta: %p, Active: %d\n",
    //    m_targetObjectiveID.c_str(), obj_meta, obj_meta ? obj_meta->is_active : -1 );
    return false;
}

RCBotTaskState ScheduleExecuteObjectiveInteraction::Execute(RCBotBase* pBot) {
    if (!pBot || !pBot->getEdict() || !pBot->isAlive()) {
         m_currentState = InteractionState::FAILED;
    }
    if (m_currentState == InteractionState::INIT_FAIL) return RCBotTaskState::RCBotTaskState_Fail;
    if (m_currentState == InteractionState::FAILED) return RCBotTaskState::RCBotTaskState_Fail;
    if (m_currentState == InteractionState::COMPLETED) return RCBotTaskState::RCBotTaskState_Complete;

    if (!gpGlobals) return RCBotTaskState::RCBotTaskState_Fail; // Cannot proceed without gpGlobals

    // Periodically re-check objective validity and update location
    if (gpGlobals->time - m_fLastScheduleUpdateTimestamp > OBJECTIVE_VALIDITY_CHECK_INTERVAL) {
         ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(m_targetObjectiveID);
         if (!obj_meta || !obj_meta->is_active) {
             m_currentState = InteractionState::FAILED;
             // UTIL_ServerPrintf("DEBUG ScheduleExecuteObjectiveInteraction: Target objective %s no longer valid/active during Execute.\n", m_targetObjectiveID.c_str());
         } else {
             m_targetObjectiveLocation = obj_meta->location;
         }
         m_fLastScheduleUpdateTimestamp = gpGlobals->time;
    }
    if (m_currentState == InteractionState::FAILED) return RCBotTaskState::RCBotTaskState_Fail;


    switch (m_currentState) {
        case InteractionState::NAVIGATING: {
            pBot->setMoveTo(m_targetObjectiveLocation, OBJECTIVE_INTERACTION_PRIORITY);
            pBot->setLookAt(m_targetObjectiveLocation, OBJECTIVE_INTERACTION_PRIORITY);

            float distance_to_target_sq = (pBot->getViewOrigin() - m_targetObjectiveLocation).LengthSquared();

            if (distance_to_target_sq < (m_navStopDistance * m_navStopDistance)) {
                pBot->setMoveTo(Vector(0,0,0), 0);
                pBot->getEdict()->v.velocity = g_vecZero; // Attempt to halt more directly

                if (m_interactionType == ObjectiveInteractionType::TOUCH_TO_ACTIVATE) {
                    m_currentState = InteractionState::COMPLETED;
                } else {
                    m_currentState = InteractionState::ALIGNING;
                    m_alignAttempts = 0;
                }
            } else {
                // Basic stuck detection (relies on RCBotBase updating m_fTimeStuck)
                // Note: m_fTimeStuck needs to be a public member or have a getter in RCBotBase for this to work.
                // Assuming it exists for now as per subtask description.
                // if (pBot->m_fTimeStuck > 2.0f) {
                //    UTIL_ServerPrintf("DEBUG: Schedule %s NAVIGATING stuck for %s (time_stuck > 2s)\n", GetName().c_str(), m_targetObjectiveID.c_str());
                //    m_currentState = InteractionState::FAILED;
                // }
            }
            break;
        }
        case InteractionState::ALIGNING: {
            pBot->setLookAt(m_targetObjectiveLocation, OBJECTIVE_INTERACTION_PRIORITY);
            pBot->setMoveTo(Vector(0,0,0),0);

            Vector to_obj = (m_targetObjectiveLocation - pBot->getViewOrigin()).NormalizeSafe();
            if (to_obj.IsZero()) {
                 m_currentState = InteractionState::INTERACTING;
                 m_interactionTimer = 0.0f;
                 break;
            }
            MAKE_VECTORS(pBot->getEdict()->v.v_angle);
            float dot_product = DotProduct(to_obj, gpGlobals->v_forward);

            if (dot_product > ALIGNMENT_DOT_PRODUCT_THRESHOLD) {
                m_currentState = InteractionState::INTERACTING;
                m_interactionTimer = 0.0f;
            } else {
                m_alignAttempts++;
                if (m_alignAttempts > m_maxAlignAttempts) {
                    // UTIL_ServerPrintf("DEBUG: Schedule %s ALIGNING failed for %s after %d attempts\n", GetName().c_str(), m_targetObjectiveID.c_str(), m_maxAlignAttempts);
                    m_currentState = InteractionState::FAILED;
                }
            }
            break;
        }
        case InteractionState::INTERACTING: {
            pBot->setLookAt(m_targetObjectiveLocation, OBJECTIVE_INTERACTION_PRIORITY);
            pBot->setMoveTo(Vector(0,0,0),0);

            float dt = gpGlobals->frametime;
            if (dt <= 0.0f) { // Fallback if frametime is 0 (e.g. paused)
                float host_framerate_val = CVAR_GET_FLOAT("host_framerate");
                dt = (host_framerate_val > 0.001f) ? (1.0f / host_framerate_val) : (1.0f / 60.0f);
            }


            switch (m_interactionType) {
                case ObjectiveInteractionType::PRIMARY_INTERACT_USE:
                    pBot->pressButton(IN_USE);
                    m_currentState = InteractionState::COMPLETED;
                    break;
                case ObjectiveInteractionType::USE_FOR_DURATION:
                    pBot->pressButton(IN_USE);
                    m_interactionTimer += dt;
                    if (m_interactionTimer >= m_interactionDurationNeeded) {
                        m_currentState = InteractionState::COMPLETED;
                    } else {
                        // Check if bot moved away too far while interacting
                        float dist_sq_to_obj = (pBot->getViewOrigin() - m_targetObjectiveLocation).LengthSquared();
                        if (dist_sq_to_obj > (m_navStopDistance + 20.0f) * (m_navStopDistance + 20.0f) ) {
                             // UTIL_ServerPrintf("DEBUG: Schedule %s INTERACTING (duration) failed for %s, bot moved away.\n", GetName().c_str(), m_targetObjectiveID.c_str());
                             m_currentState = InteractionState::FAILED;
                        }
                    }
                    break;
                case ObjectiveInteractionType::TOUCH_TO_ACTIVATE:
                    m_currentState = InteractionState::COMPLETED;
                    break;
                case ObjectiveInteractionType::BE_IN_PROXIMITY_FOR_DURATION: {
                    pBot->setMoveTo(Vector(0,0,0),0); // Ensure bot stays put

                    float distance_sq_to_obj = (pBot->getViewOrigin() - m_targetObjectiveLocation).LengthSquared();

                    // Using CONTROL_POINT_CAPTURE_RADIUS from RLConsts
                    if (distance_sq_to_obj > RLConsts::CONTROL_POINT_CAPTURE_RADIUS * RLConsts::CONTROL_POINT_CAPTURE_RADIUS) {
                        // UTIL_ServerPrintf("DEBUG: Schedule %s INTERACTING (proximity) failed for %s, bot moved away from capture radius.\n", GetName().c_str(), m_targetObjectiveID.c_str());
                        m_currentState = InteractionState::FAILED; // Or NAVIGATING to return to point
                    } else {
                        m_interactionTimer += dt;
                        if (m_interactionTimer >= m_interactionDurationNeeded) {
                            m_currentState = InteractionState::COMPLETED;
                        }
                    }
                    break;
                }
                default:
                    // UTIL_ServerPrintf("DEBUG: Schedule %s INTERACTING failed for %s, unknown interaction type %d\n", GetName().c_str(), m_targetObjectiveID.c_str(), static_cast<int>(m_interactionType));
                    m_currentState = InteractionState::FAILED;
                    break;
            }
            break;
        }
        default:
            m_currentState = InteractionState::FAILED;
            break;
    }

    if (m_currentState == InteractionState::FAILED) return RCBotTaskState::RCBotTaskState_Fail;
    if (m_currentState == InteractionState::COMPLETED) return RCBotTaskState::RCBotTaskState_Complete;
    return RCBotTaskState::RCBotTaskState_Active;
}

void ScheduleExecuteObjectiveInteraction::OnScheduleInterrupt(RCBotBase* pBot) {
    // UTIL_ServerPrintf("DEBUG: Schedule %s for %s INTERRUPTED.\n", GetName().c_str(), m_targetObjectiveID.c_str());
    m_currentState = InteractionState::FAILED;
}

void ScheduleExecuteObjectiveInteraction::OnScheduleResume(RCBotBase* pBot) {
    // UTIL_ServerPrintf("DEBUG: Schedule %s for %s RESUMED.\n", GetName().c_str(), m_targetObjectiveID.c_str());
    if (m_currentState != InteractionState::COMPLETED && m_currentState != InteractionState::FAILED) {
         if(!initializeObjectiveData(pBot)) {
             m_currentState = InteractionState::FAILED;
         } else {
             m_currentState = InteractionState::NAVIGATING;
         }
    }
}
