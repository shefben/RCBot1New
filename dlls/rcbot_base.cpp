// Test comment for no-op edit
#include "rcbot_base.h"
#include "enginecallback.h" // For g_engfuncs
#include "extdll.h"
#include "meta_api.h"
#include "dll.h"
#include "h_export_meta.h"
#include "meta_api.h"
#include "rcbot_utils.h"
#include "rcbot_task_utility.h"
#include "rcbot_profile.h"
#include "rcbot_visibles.h"
#include "rcbot_weapons.h"
#include "rcbot_navigator.h"
#include "enginecallback.h"
#include "util.h"
#include "rcbot_chat_manager.h"
#include "rcbot_dynamic_objectives.h"
#include "rcbot_short_term_memory.h" // For GameEvent, RCBotReplayBuffer
#include "rcbot_long_term_memory.h"
#include "rcbot_macro_action.h"
#include "rl_types.h"

#include <math.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <limits>
#include <cmath>
#include <cfloat> // For FLT_MAX, though std::numeric_limits is preferred if available

// Static helper function to get player unique ID
static int GetPlayerUniqueIdFromEdict_Static(edict_t* pEdict) {
    if (pEdict && pEdict->pvPrivateData != NULL && (pEdict->v.flags & FL_CLIENT) && !(pEdict->v.flags & FL_FAKECLIENT)) {
        if (g_engfuncs.pfnGetPlayerUserId) { // Check if engine function is available
            return (*g_engfuncs.pfnGetPlayerUserId)(pEdict);
        }
    }
    return -1; // Invalid ID for non-players or if function unavailable
}

// Constants for Dynamic Objective Selection
static const float MIN_CONFIDENCE_FOR_DYNAMIC_OBJECTIVE = 0.3f;
static const float DISTANCE_PENALTY_FACTOR_DYN_OBJ = 0.0005f;
static const uint8_t DYNAMIC_OBJECTIVE_MOVE_PRIORITY = 3;
// Note: <cfloat> or <limits> should be included for FLT_MAX or std::numeric_limits

// Simulated game state flags for testing objective interactions
static bool g_debug_simulate_bomb_is_planted = false;
// (If multiple sites, might need g_debug_simulate_bomb_planted_at_A, g_debug_simulate_bomb_planted_at_B)
static bool g_debug_simulate_flag_is_loose_team1 = false;
static bool g_debug_simulate_flag_is_loose_team2 = false;

// Interaction Durations (defined in RLConsts in RCBotRLHelper.h, no need to redefine here)
// static const float INTERACTION_DEFUSE_TIME_CONST = 7.0f; // Example
// static const float INTERACTION_PLANT_TIME_CONST = 3.0f;  // Example


RCBotBase::RCBotBase() :
    m_rlAgent(RLStateProps::NUM_STATE_FEATURES,
              static_cast<int>(BotActionType::MAX_ACTIONS),
              0.1f,  // learning_rate (alpha)
              0.95f, // discount_factor (gamma)
              0.2f)  // initial epsilon (exploration rate)
    // NOTE: m_pVisibles and m_Utils are initialized with 'new' in the body, which is acceptable.
    // m_pSchedule is initialized to nullptr in the body.
{
	m_pVisibles = new RCBotVisibles(this);
	m_Utils = new RCBotUtilities();
	m_pSchedule = nullptr;
	setFOV(RCBOT_DEFAULT_FOV);

    // Initialize members from all previous subtasks that should be here
    m_pLongTermMemory = nullptr;
    m_lastLearnTime = 0.0f; // Initialize last learn time
    m_lastThreatUpdateTime = 0.0f; // Initialize last threat update time
    m_curiosityScore = 0.0f;
    m_currentFocusObjective = "";
    m_currentObjectiveFocusID = "";
    m_timeObjectiveFocused = 0.0f;
    m_currentMacroAction = nullptr;
    m_persona = PERSONA_NEUTRAL;
    m_lastTauntTime = 0.0f;
    m_damageTakenPostTaunt = 0;
    m_engagementScore = 0.0f;
    m_aggressivenessScore = 0.0f;
    m_timeSinceLastPersonaEvaluation = 0.0f;
    m_previousDistanceToFocusObjective = -1.0f;
    m_hasFocusObjectiveLocation = false;
    m_shapingRewardAccumulator = 0.0f;
    m_perceivedPlayerAggression = PERCEPTION_BASELINE;
    m_perceivedPlayerCooperation = PERCEPTION_BASELINE;
    m_lastInteractingPlayerEdict.Set(nullptr);

    // RL specific initializations from this subtask (7.7)
    // m_accumulatedRewardSinceLastTransition is now managed by m_rlHelper
    m_firstThinkCycle = true;
    m_lastAction = BotActionType::IDLE;
    m_chosenAIActionThisFrame = BotActionType::IDLE;
    m_timeSinceLastDamageTaken = 0.0f;
    m_timeSpentIdleOrStuck = 0.0f;
    // m_replayBuffer, m_chatContextMemory, m_seenEntityFeaturesLog are default constructed
    // m_currentState, m_previousState are default constructed

    m_currentObjectiveInteractionType = ObjectiveInteractionType::NONE;
    m_objectiveInteractionDuration = 0.0f;
    m_debug_sim_has_bomb = false;
    m_debug_sim_has_enemy_flag = false;
    m_debug_sim_enemy_flag_team_id = 0;

	Init(); // Calls spawnInit
    loadMacroActions(); // From macro subtask
}

RCBotBase :: ~RCBotBase()
{
	delete m_pVisibles;
}

void RCBotBase::Init()
{
	m_fLastRunPlayerMove = gpGlobals->time;
	m_bPreviousAliveState = false;
	m_pWeapons = new RCBotWeapons(this);

	spawnInit();
}

void RCBotBase::spawnInit()
{
	m_vMoveTo.reset();
	m_vLookAt.reset();
	m_bInterrupted = false;
	m_fRespawnTime = gpGlobals->time;
	m_fLastRunPlayerMove = gpGlobals->time;
	m_pEnemy.Set(nullptr);

    // Clear/Reset all relevant states
    m_replayBuffer.clear();
    m_seenEntityFeaturesLog.clear();
    m_chatContextMemory.clear();
    m_opponent_models.clear(); // Clear opponent models on spawn/new round

    m_curiosityScore = 0.0f;
    m_itemCuriosity.clear();
    m_encounteredEntityClasses.clear();
    m_visitedWaypoints.clear();

    m_objectiveInterests.clear();
    m_currentFocusObjective = "";
    m_currentObjectiveFocusID = "";
    m_timeObjectiveFocused = 0.0f;
    m_previousDistanceToFocusObjective = -1.0f;
    m_hasFocusObjectiveLocation = false;
    m_shapingRewardAccumulator = 0.0f;

    m_perceivedPlayerAggression = PERCEPTION_BASELINE;
    m_perceivedPlayerCooperation = PERCEPTION_BASELINE;
    m_lastInteractingPlayerEdict.Set(nullptr);

    m_timeSinceLastDamageTaken = 0.0f;
    m_timeSpentIdleOrStuck = 0.0f;
    // m_accumulatedRewardSinceLastTransition is now managed by m_rlHelper
    m_rlHelper.reset(); // Reset RL helper for the new spawn
    if (m_pEdict) { // Ensure edict is valid before accessing health
        m_lastThinkHealth = m_pEdict->v.health;
    } else {
        m_lastThinkHealth = 100.0f; // Default if edict not ready (should not happen ideally)
    }
    m_firstThinkCycle = true;
    m_currentState.clear();
    m_previousState.clear();
    m_lastAction = BotActionType::IDLE;
    m_chosenAIActionThisFrame = BotActionType::IDLE;

    m_pLastEnemy.Set(nullptr);
    m_lastEnemyHealth = 0.0f;
    m_previousDynamicObjectiveFocusID_debug = "";

    m_currentObjectiveInteractionType = ObjectiveInteractionType::NONE;
    m_objectiveInteractionDuration = 0.0f;
    m_debug_sim_has_bomb = false;
    m_debug_sim_has_enemy_flag = false;
    m_debug_sim_enemy_flag_team_id = 0;

    m_lastLearnTime = gpGlobals ? gpGlobals->time : 0.0f;
    m_lastThreatUpdateTime = gpGlobals ? gpGlobals->time : 0.0f;
    m_rlAgent.loadModel(getRLModelFilename()); // Attempt to load model
}

// Helper function to generate filename for RL model
std::string RCBotBase::getRLModelFilename() const {
    if (m_pEdict && STRING(gpGlobals->mapname)[0] != '\0') {
        // Check if rcbot/rl_models directory exists, create if not
        // This is a simplified check; a robust solution might use platform-specific directory creation.
        // For now, we assume the directory might need to be created manually or by an external script.
        // Ensure "rcbot" directory exists at the game root.
        // Ensure "rcbot/rl_models" directory exists.
        // Example: CreateDirectory("rcbot", NULL); CreateDirectory("rcbot/rl_models", NULL);
        // However, direct file system manipulation like CreateDirectory is often outside the scope
        // of typical game DLLs for portability and permissions reasons.
        // It's better to ensure these directories are part of the mod/bot's installation structure.

        char filename[256];
        // Use a safe way to get bot's name, STRING(m_pEdict->v.netname) might be empty initially on some engine versions
        const char* botName = STRING(m_pEdict->v.netname);
        if (!botName || botName[0] == '\0') {
            // Fallback if netname is not yet available (e.g. during early spawn)
            // Using entindex might be an option for a unique ID, but less readable.
            // For now, a generic name if netname is unavailable.
            snprintf(filename, sizeof(filename), "rcbot/rl_models/%s_bot_rl_model_fallback.txt", STRING(gpGlobals->mapname));
        } else {
             // Sanitize botName if it can contain invalid path characters, though typically netnames are simple.
            snprintf(filename, sizeof(filename), "rcbot/rl_models/%s_bot_%s_rl_model.txt", STRING(gpGlobals->mapname), botName);
        }
        return std::string(filename);
    }
    return "rcbot/rl_models/default_bot_rl_model.txt"; // Fallback if mapname or edict is not available
}


void RCBotBase::setAmmo(uint8_t index, uint8_t amount)
{
	m_pWeapons->setAmmo(index, amount);
}

#define RCBOT_RESPAWN_WAIT_TIME 2.0f

void RCBotBase::Think()
{
	// Example of recording a game event: Bot firing weapon
	if (m_pEdict && (m_pEdict->v.button & IN_ATTACK) && m_pCurrentWeapon && gpGlobals) {
		// Construct event specific to weapon firing
        GameEvent fire_event(EVENT_WEAPON_FIRE, gpGlobals->time);
        fire_event.edict_source = m_pEdict;
        // Store weapon ID or some identifier in int_data1 or float_data1 if needed
        // fire_event.int_data1 = m_pCurrentWeapon->m_iId;
		recordGameEvent(fire_event);
	}
    // Example: Bot sighting an enemy (actual call would be in newVisible or similar logic)
    // if (m_pEnemy.Get() && /* logic for new sight */ gpGlobals) {
    //    GameEvent sight_event = GameEvent::EnemySighted(gpGlobals->time, m_pEnemy.Get());
    //    recordGameEvent(sight_event);
    // }

	// Increment timers
    m_timeSinceLastDamageTaken += gpGlobals->frametime; // Assuming gpGlobals->frametime is available and accurate

	m_pEdict->v.button = 0;
	m_pEdict->v.impulse = 0;
	m_fSpeedPercent = 1.0f; // full speed
	m_vMoveTo.reset();
	m_vLookAt.reset();

	if (!isAlive())
	{
		if (m_bPreviousAliveState == true) // Bot was alive in the previous frame, now it's not
		{
			// This is where the bot just died. Apply penalty.
			m_rlHelper.addReward(RCBotRLHelper::RLConsts::PENALTY_BOT_DEATH, true); // true for terminal state

			spawnInit(); // Reset states for next life
			m_bPreviousAliveState = false;
		}

		if (m_fRespawnTime < gpGlobals->time)
		{
			m_fRespawnTime = gpGlobals->time + RCBOT_RESPAWN_WAIT_TIME;

			respawn();
			return;
		}
	}
	else
	{
		// Bot is alive
		if (m_pEdict && m_pEdict->v.health < m_lastThinkHealth)
		{
			float damageTaken = m_lastThinkHealth - m_pEdict->v.health;
			if (damageTaken > 0) // Ensure we don't reward for health gain if logic changes
			{
				m_rlHelper.addReward(damageTaken * RCBotRLHelper::RLConsts::PENALTY_DAMAGE_TAKEN, false);
                // Update timeSinceLastDamageTaken, could be useful for state or other logic
                m_timeSinceLastDamageTaken = 0.0f;
			}
		}

		if (m_bPreviousAliveState == false)
		{
			m_bPreviousAliveState = true;
			if (m_pEdict) { // Initialize m_lastThinkHealth here too if bot just spawned
				m_lastThinkHealth = m_pEdict->v.health;
                m_pEdict->v.v_angle = m_pEdict->v.angles;
			}
		}
	}
	
    // Stuck/Idle detection - only if alive
    if (isAlive() && m_pEdict) {
        bool isPerformingSignificantAction = (m_pEdict->v.button & (IN_ATTACK | IN_USE | IN_JUMP)) != 0;
        if (m_currentMacroAction && !m_currentMacroAction->isFinished()) {
            isPerformingSignificantAction = true;
        }

        if (m_pEdict->v.velocity.Length2D() < RCBotRLHelper::RLConsts::MIN_MOVEMENT_SPEED_THRESHOLD && !isPerformingSignificantAction) {
            m_timeSpentIdleOrStuck += gpGlobals->frametime; // Assumes gpGlobals->frametime is available
        } else {
            m_timeSpentIdleOrStuck = 0.0f; // Reset if moving or acting
        }

        if (m_timeSpentIdleOrStuck > RCBotRLHelper::RLConsts::MAX_IDLE_TIME_SECONDS) {
            m_rlHelper.addReward(RCBotRLHelper::RLConsts::PENALTY_STUCK_OR_IDLE_LONG, false);
            m_timeSpentIdleOrStuck = 0.0f; // Reset after applying penalty to avoid continuous penalization each frame
        }
    } else {
        // If not alive or no edict, ensure time is reset
        m_timeSpentIdleOrStuck = 0.0f;
    }

	// ---- Main AI Logic ----

    // RL Transition Recording
    BotState s_prime; // S_t+1
    float reward_for_last_transition = 0.0f;
    bool is_terminal_transition = false;

    // These parameters need to be gathered for getCurrentBotState
    std::map<int, int> currentWeaponAmmoMap; // Placeholder
    std::map<int, int> currentWeaponMaxClipMap; // Placeholder
    int currentWeaponIdVal = 0; // Placeholder
    if (m_pCurrentWeapon) {
        currentWeaponIdVal = m_pCurrentWeapon->m_iId;
        // TODO: Populate ammo maps by iterating m_pWeapons->m_Weapons for currentWeaponAmmoMap and currentWeaponMaxClipMap
    }
    float taskCompletionRatio = 0.0f; // Placeholder for path following, etc.

    // --- Gather Current Enemy Data for BotState ---
    bool bot_has_current_enemy = false;
    float bot_current_enemy_threat = 0.5f; // Neutral baseline, using literal as OPPONENT_THREAT_NEUTRAL_BASELINE is not static in RCBotBase
    Vector bot_current_enemy_location(0,0,0);
    float bot_distance_to_current_enemy = -1.0f; // Using -1 to indicate no valid distance
    Vector bot_direction_to_current_enemy(0,0,0);

    edict_t* current_enemy_edict = m_pEnemy.Get();
    if (current_enemy_edict && m_pEdict && isAlive() && current_enemy_edict->v.deadflag == DEAD_NO) {
        int enemy_id = GetPlayerUniqueIdFromEdict_Static(current_enemy_edict);
        if (enemy_id != -1) { // It's a human player
            auto it = m_opponent_models.find(enemy_id);
            if (it != m_opponent_models.end()) {
                const OpponentStats& enemy_stats = it->second;
                bot_current_enemy_threat = enemy_stats.perceived_threat_level;
                bot_current_enemy_location = current_enemy_edict->v.origin;
                bot_has_current_enemy = true;
            } else {
                bot_current_enemy_location = current_enemy_edict->v.origin;
                bot_has_current_enemy = true;
                // bot_current_enemy_threat remains neutral baseline (0.5f)
            }
        } else { // Is an enemy, but not a human player (e.g. monster)
            bot_current_enemy_location = current_enemy_edict->v.origin;
            bot_has_current_enemy = true;
            // bot_current_enemy_threat remains neutral baseline (0.5f) for non-modeled entities
        }

        if (bot_has_current_enemy) {
            bot_distance_to_current_enemy = (bot_current_enemy_location - m_pEdict->v.origin).Length();
            if (bot_distance_to_current_enemy > 0.01f) {
                bot_direction_to_current_enemy = (bot_current_enemy_location - m_pEdict->v.origin).NormalizeSafe();
            } else {
                bot_distance_to_current_enemy = 0.0f; // Set to 0 if very close
            }
        }
    }
    // --- End Gather Current Enemy Data ---

    if (!m_firstThinkCycle && m_pEdict) {
        s_prime = m_rlHelper.getCurrentBotState(
            m_pEdict,
            m_currentObjectiveFocusID,
            m_focusObjectiveLocation,
            m_hasFocusObjectiveLocation,
            m_perceivedPlayerAggression,
            m_perceivedPlayerCooperation,
            m_timeSinceLastDamageTaken,
            (m_pEdict->v.flags & FL_ONGROUND) != 0,
            isUnderWater(),
            (m_pEdict->v.flags & FL_ONLADDER) != 0,
            currentWeaponAmmoMap,
            currentWeaponMaxClipMap,
            currentWeaponIdVal,
            taskCompletionRatio,
            // New args:
            bot_has_current_enemy,
            bot_current_enemy_threat,
            bot_current_enemy_location,
            bot_distance_to_current_enemy,
            bot_direction_to_current_enemy
        );

        reward_for_last_transition = m_rlHelper.getAccumulatedRewardAndReset();
        if (!isAlive()) { // If bot died this cycle
            is_terminal_transition = true;
        }

        RLTransition transition = {m_previousState, m_lastAction, reward_for_last_transition, s_prime, is_terminal_transition};
        m_replayBuffer.addTransition(transition); // Assuming m_replayBuffer is the member for RCBotReplayBuffer
    }

	// (Existing schedule, utility, enemy handling code determines m_chosenAIActionThisFrame)
	// ... (lots of code here) ...
	if (m_pEnemy.Get() != nullptr)
	{
		edict_t* pEnemy = m_pEnemy.Get();

		RCBotWeapon* pBestWeapon = m_pWeapons->getBestWeapon(pEnemy);

		if (pBestWeapon != nullptr && m_pCurrentWeapon != pBestWeapon)
		{
			selectWeapon(pBestWeapon);
		}
	}

	m_pVisibles->tasks(m_pProfile->getVisRevs()); // This might change m_pEnemy (e.g. newVisible, lostVisible)
    // m_chosenAIActionThisFrame should be set by the above AI logic based on current state.

    // --- Dynamic Objective Selection Logic (Part 1: Selection) ---
    bool pursued_dynamic_objective_this_frame = false;
    std::string selected_objective_id = ""; // Keep this name for clarity in this combined block
    // Vector selected_objective_location; // Not needed here anymore, obj_meta->location used directly

    if (isAlive() && m_pEdict) {
        const auto& candidates = g_ObjectiveManager.getObjectiveCandidates();
        if (!candidates.empty()) {
            float best_score = -std::numeric_limits<float>::max();
            // selected_objective_id is initialized above
            // selected_objective_location is not needed here anymore

            for (const auto& pair : candidates) {
                const ObjectiveCandidateMetadata& obj = pair.second;
                if (!obj.is_active || obj.confidence < MIN_CONFIDENCE_FOR_DYNAMIC_OBJECTIVE) {
                    continue;
                }
                float distance_to_objective = (obj.location - m_pEdict->v.origin).Length();
                float score = obj.confidence - (distance_to_objective * DISTANCE_PENALTY_FACTOR_DYN_OBJ);
                if (score > best_score) {
                    best_score = score;
                    selected_objective_id = obj.unique_id;
                }
            }

            if (!selected_objective_id.empty()) {
                ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(selected_objective_id);
                if (obj_meta) {
                    // Set general bot state related to this dynamic objective focus
                    m_currentObjectiveFocusID = selected_objective_id;
                    m_focusObjectiveLocation = obj_meta->location;
                    m_hasFocusObjectiveLocation = true;
                    if (m_currentObjectiveFocusID != m_previousDynamicObjectiveFocusID_debug) {
                         m_previousDistanceToFocusObjective = (m_pEdict->v.origin - m_focusObjectiveLocation).Length();
                         m_previousDynamicObjectiveFocusID_debug = m_currentObjectiveFocusID;
                    }
                    // Note: setMoveTo and setLookAt are removed from here. Schedule will handle.
                    pursued_dynamic_objective_this_frame = true;
                } else {
                    // Objective disappeared or became invalid between selection and this check
                    selected_objective_id = ""; // Clear it so we don't proceed
                    m_currentObjectiveFocusID = ""; // Clear focus
                    m_hasFocusObjectiveLocation = false;
                    pursued_dynamic_objective_this_frame = false;
                }
            }
        }
    }
    // --- End Dynamic Objective Selection (Part 1) ---

    // --- RL Agent Action Selection ---
    // Ensure m_previousState is valid (it's S_t for the current transition)
    // m_chosenAIActionThisFrame will be A_t, which is executed in this frame,
    // leading to S_t+1 (s_prime) and R_t.
    if (!m_firstThinkCycle && !m_previousState.isEmpty()) { // Ensure previous state is valid
        m_chosenAIActionThisFrame = m_rlAgent.chooseAction(m_previousState, true);
    } else if (m_firstThinkCycle && !m_currentState.isEmpty()){ // For the very first action after state is initialized
         m_chosenAIActionThisFrame = m_rlAgent.chooseAction(m_currentState, true);
    } else {
        // Fallback if state isn't properly initialized yet, though this should be rare.
        // Or if it's the first cycle and m_currentState (which is previousState) isn't ready.
        // The very first state for m_previousState is set at the end of Think().
        // For the absolute first Think() call, m_previousState might be empty.
        // Let's ensure m_previousState (S_t) is initialized before choosing action.
        // This is handled by m_firstThinkCycle logic at the end of Think().
        // For now, if m_previousState is empty here (should only be on the very first call to Think ever),
        // we might need a default action or ensure state is initialized before this.
        // The current structure initializes m_previousState at the END of Think().
        // So, for the first actual decision, m_previousState will be based on the initial dummy state.
        // This is generally acceptable.
        // If m_previousState is still empty (e.g. if state creation failed), default to IDLE.
        if (m_previousState.isEmpty()) {
             m_chosenAIActionThisFrame = BotActionType::IDLE;
        } else {
            m_chosenAIActionThisFrame = m_rlAgent.chooseAction(m_previousState, true);
        }
    }
    // --- End RL Agent Action Selection ---

    // --- Conceptual Opponent Model Influence on Tactics ---
    // if (bot_has_current_enemy && bot_current_enemy_threat > 0.8f) {
    //     // If current RL action is aggressive (e.g., TACTIC_ENGAGE_ENEMY),
    //     // consider overriding to a more cautious action if not already.
    //     // This is a placeholder for how direct modeling could override or bias RL.
    //     // For example:
    //     // if (m_chosenAIActionThisFrame == BotActionType::TACTIC_ENGAGE_ENEMY && m_pEdict->v.health < 50) {
    //     //     m_chosenAIActionThisFrame = BotActionType::TACTIC_RETREAT_OR_FALLBACK; // Override
    //     //     UTIL_ServerPrintf("Bot %s overriding to FALLBACK due to high threat enemy (%.2f) and low health.\n", STRING(m_pEdict->v.netname), bot_current_enemy_threat);
    //     // }
    // }
    // Target selection (m_pEnemy assignment) could also be influenced by these stats
    // in the newVisible() or other enemy acquisition logic. (TODO)
    // --- End Conceptual Opponent Model Influence ---


    // --- Determine Interaction Type and Dispatch Schedule (Part 2: Dispatch) ---
    if (pursued_dynamic_objective_this_frame && !m_currentObjectiveFocusID.empty()) { // m_currentObjectiveFocusID is set if obj_meta was valid
        ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(m_currentObjectiveFocusID); // Re-fetch is safe
        ObjectiveInteractionType determined_interaction_type = ObjectiveInteractionType::NONE;
        float determined_interaction_duration = 0.0f;

        if (obj_meta) { // Should generally be true if pursued_dynamic_objective_this_frame is true
            m_chosenAIActionThisFrame = BotActionType::TACTIC_PURSUE_DYNAMIC_OBJECTIVE;

            // Ensure m_pEdict is valid before using it for team checks etc.
            if (m_pEdict) {
                switch (obj_meta->category_tag) {
                    case ObjectiveCategoryType::BUTTON_ENTITY:
                    case ObjectiveCategoryType::DOOR_ENTITY:
                        determined_interaction_type = ObjectiveInteractionType::PRIMARY_INTERACT_USE;
                        break;
                    case ObjectiveCategoryType::BOMB_SITE: {
                        // Use real or simulated bomb state
                        bool use_real_bomb_state = true; // Could be a CVAR later
                        bool bomb_is_actually_planted = use_real_bomb_state ?
                                                        gRCBotManager.m_real_game_state_bomb_planted :
                                                        gRCBotManager.m_debug_g_simulate_bomb_is_planted;

                        if (bomb_is_actually_planted && m_pEdict->v.team == 2 /*CT*/) {
                           determined_interaction_type = ObjectiveInteractionType::USE_FOR_DURATION;
                           determined_interaction_duration = RLConsts::INTERACTION_DEFUSE_TIME;
                        } else if (!bomb_is_actually_planted && m_pEdict->v.team == 1 /*T*/ && m_debug_sim_has_bomb) { // Bot-specific flag for having bomb
                           determined_interaction_type = ObjectiveInteractionType::USE_FOR_DURATION;
                           determined_interaction_duration = RLConsts::INTERACTION_PLANT_TIME;
                        } else {
                           // If neither planting nor defusing, but it's a bomb site, could be TOUCH_TO_ACTIVATE (e.g. trigger a voice line or check)
                           // Or NONE if no specific action without bomb/planted state. Let's assume TOUCH for now.
                           determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE;
                        }
                        break;
                    }
                    case ObjectiveCategoryType::FLAG_STAND: {
                        // Logic:
                        // 1. If it's enemy flag stand AND their flag is loose: TOUCH_TO_ACTIVATE (pick up enemy flag)
                        // 2. If it's our flag stand AND we are carrying the enemy flag: TOUCH_TO_ACTIVATE (capture/score point)
                        // 3. If it's our flag stand AND our flag is loose (not at stand) AND we are NOT carrying enemy flag: TOUCH_TO_ACTIVATE (return our flag - simplified)
                        //    (More complex: returning might be automatic on touch or require holding USE)
                        bool is_my_flag_stand = (obj_meta->team_ownership == m_pEdict->v.team);
                        bool is_enemy_flag_stand = (obj_meta->team_ownership != 0 && obj_meta->team_ownership != m_pEdict->v.team);

                        if (is_enemy_flag_stand) {
                            bool enemy_flag_is_loose = (obj_meta->team_ownership == 1 && gRCBotManager.m_debug_g_simulate_flag_is_loose_team1) ||
                                                       (obj_meta->team_ownership == 2 && gRCBotManager.m_debug_g_simulate_flag_is_loose_team2);
                            if (enemy_flag_is_loose && !m_debug_sim_has_enemy_flag) { // If enemy flag is loose AND bot is NOT already carrying a flag
                                determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE; // Pick up enemy flag
                            } else {
                                determined_interaction_type = ObjectiveInteractionType::NONE; // Can't pick up if not loose or if bot already has a flag
                            }
                        } else if (is_my_flag_stand) {
                            if (m_debug_sim_has_enemy_flag && m_debug_sim_enemy_flag_team_id != m_pEdict->v.team) { // Bot is carrying the enemy flag
                                determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE; // Capture enemy flag at our stand
                            } else {
                                // Potentially for returning our flag if it was dropped and now at our stand (game specific)
                                // For now, if it's our stand and we don't have enemy flag, assume no primary interaction.
                                // Game logic might automatically return it on touch if it's the actual flag entity at the stand.
                                determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE; // Or NONE, depending on mod.
                            }
                        } else { // Neutral flag stand (if any)
                            determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE;
                        }
                        break;
                    }
                    case ObjectiveCategoryType::FLAG_CAPTURE_POINT: { // Usually for CTF flags, not base stands
                        // This is where you take a flag you are CARRYING to score.
                        // Assumes this capture point is for the bot's team or a neutral one.
                        if (m_debug_sim_has_enemy_flag && m_debug_sim_enemy_flag_team_id != 0 && m_debug_sim_enemy_flag_team_id != m_pEdict->v.team) {
                             // If bot has an ENEMY flag (enemy_flag_team_id is the team of the flag it's carrying)
                             // And this capture point is for the bot's team (obj_meta->team_ownership == m_pEdict->v.team) or neutral (obj_meta->team_ownership == 0)
                            if (obj_meta->team_ownership == m_pEdict->v.team || obj_meta->team_ownership == 0) {
                                determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE; // Capture the flag
                            } else {
                                determined_interaction_type = ObjectiveInteractionType::NONE; // Can't capture at enemy's capture point
                            }
                        } else {
                            determined_interaction_type = ObjectiveInteractionType::NONE; // Cannot capture if not carrying an enemy flag
                        }
                        break;
                    }
                    case ObjectiveCategoryType::HOSTAGE_ENTITY:
                        determined_interaction_type = ObjectiveInteractionType::PRIMARY_INTERACT_USE;
                        determined_interaction_duration = RLConsts::INTERACTION_RESCUE_TIME; // Example duration
                        break;
                    case ObjectiveCategoryType::CONTROL_POINT: {
                        determined_interaction_type = ObjectiveInteractionType::BE_IN_PROXIMITY_FOR_DURATION;
                        determined_interaction_duration = RLConsts::INTERACTION_CONTROL_POINT_CAPTURE_TIME;
                        break;
                    }
                    case ObjectiveCategoryType::WEAPON_ITEM:
                    case ObjectiveCategoryType::AMMO_ITEM:
                    case ObjectiveCategoryType::HEALTH_ITEM:
                    case ObjectiveCategoryType::ARMOR_ITEM:
                    case ObjectiveCategoryType::KEY_ITEM:
                    case ObjectiveCategoryType::GENERIC_TRIGGER:
                        determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE;
                        break;
                    default:
                        determined_interaction_type = ObjectiveInteractionType::TOUCH_TO_ACTIVATE;
                        break;
                }
            } else { // m_pEdict is NULL, should not happen if bot is alive and thinking
                determined_interaction_type = ObjectiveInteractionType::NONE;
            }

            m_currentObjectiveInteractionType = determined_interaction_type;
            m_objectiveInteractionDuration = determined_interaction_duration;

            UTIL_ServerPrintf("Bot %s: Focused Obj: %s, Category: %s -> Determined Interaction: %d, Duration: %.1f\n", (m_pEdict ? STRING(m_pEdict->v.netname) : "NO_EDICT"), m_currentObjectiveFocusID.c_str(), objectiveCategoryToString(obj_meta->category_tag).c_str(), static_cast<int>(m_currentObjectiveInteractionType), m_objectiveInteractionDuration);

            // UTIL_ServerPrintf("Bot %s: Obj %s, Cat %s. Interaction Type: %d, Duration: %.1f\n",
            //    (m_pEdict ? STRING(m_pEdict->v.netname) : "UNKNOWN_BOT"),
            //    m_currentObjectiveFocusID.c_str(),
            //    objectiveCategoryToString(obj_meta->category_tag).c_str(),
            //    static_cast<int>(m_currentObjectiveInteractionType),
            //    m_objectiveInteractionDuration);

            if (m_pSchedule) {
                delete m_pSchedule;
                m_pSchedule = nullptr;
            }

            // Only create a new schedule if a valid interaction type was determined
            if (m_currentObjectiveInteractionType != ObjectiveInteractionType::NONE) {
                m_pSchedule = new ScheduleExecuteObjectiveInteraction(this, m_currentObjectiveFocusID, m_currentObjectiveInteractionType, m_objectiveInteractionDuration);
                if (m_Utils) m_Utils->setUtility(nullptr);
                m_bInterrupted = false;
            } else {
                // If no specific interaction, don't create the interaction schedule.
                // Bot might still move towards the objective due to general utility/pathing if it's on the way.
                // Or clear focus if no interaction means it's not currently pursuable.
                // For now, we'll let it potentially path, but schedule won't do anything.
                // Consider clearing m_currentObjectiveFocusID here if NONE means it's truly un-actionable.
            }

        } else {
            m_currentObjectiveFocusID = "";
            m_currentObjectiveInteractionType = ObjectiveInteractionType::NONE;
            m_hasFocusObjectiveLocation = false; // Added to ensure focus is fully cleared
        }
    }
    // --- End Determine Interaction Type and Dispatch Schedule ---

    // --- Distance-Based Shaping Reward for Focused Objective (Dynamic or Intrinsic if location is set) ---
    // Ensure there's some focus ID (dynamic or intrinsic string) if hasFocusObjectiveLocation is true
    if (m_hasFocusObjectiveLocation && !(m_currentObjectiveFocusID.empty() && m_currentFocusObjective.empty()) && m_pEdict && isAlive()) {
        float currentDistance = (m_pEdict->v.origin - m_focusObjectiveLocation).Length();
        if (m_previousDistanceToFocusObjective > 0) { // Ensure previous distance was valid
            float distanceDelta = m_previousDistanceToFocusObjective - currentDistance;
            // Only reward positive progress or penalize significant negative progress for focused objectives.
            if (distanceDelta > RLConsts::SIGNIFICANT_PROGRESS_THRESHOLD_FOR_SHAPING ||
                distanceDelta < -RLConsts::SIGNIFICANT_PROGRESS_THRESHOLD_FOR_SHAPING) {
                float reward = distanceDelta * RLConsts::REWARD_OBJECTIVE_PROGRESS;
                m_rlHelper.addReward(reward);
                // std::string focus_id_for_print = m_currentObjectiveFocusID.empty() ? m_currentFocusObjective : m_currentObjectiveFocusID;
                // UTIL_ServerPrintf("Bot %s: Shaping reward for distance to obj %s: %.4f (Delta: %.2f)\n",
                //    STRING(m_pEdict->v.netname), focus_id_for_print.c_str(), reward, distanceDelta);
            }
        }
        m_previousDistanceToFocusObjective = currentDistance;
    } else {
        // If no focused objective with location, reset previous distance
        m_previousDistanceToFocusObjective = -1.0f;
    }
    // --- End Distance-Based Shaping Reward ---

	// Handle Damage Dealt Reward - AFTER m_pEnemy is determined for this frame.
	edict_t* currentEnemyEdict = m_pEnemy.Get();
	if (currentEnemyEdict && currentEnemyEdict->v.health > 0 && isEnemy(currentEnemyEdict) )
	{
		if (m_pLastEnemy.Get() != currentEnemyEdict)
		{
			m_pLastEnemy.Set(currentEnemyEdict);
			m_lastEnemyHealth = currentEnemyEdict->v.health;
		}
		else
		{
			if (currentEnemyEdict->v.health < m_lastEnemyHealth)
			{
				float damageDealt = m_lastEnemyHealth - currentEnemyEdict->v.health;
				bool isFacingEnemy = inViewCone(currentEnemyEdict->v.origin); // Check if bot is looking towards enemy

				if (damageDealt > 0 && isFacingEnemy)
				{
					m_rlHelper.addReward(damageDealt * RCBotRLHelper::RLConsts::REWARD_DAMAGE_DEALT, false);
				}
			}
			m_lastEnemyHealth = currentEnemyEdict->v.health;
		}
	}
	else // No current valid enemy, or current enemy is dead. Check if we damaged a non-enemy.
	{
		// This part needs to be careful. If m_pEnemy is null, we might have shot something else.
		// A more robust friendly fire detection would occur if the game provides an event hook for "player A damaged player B".
		// Here, we only check if our *previous* target (m_pLastEnemy) took damage AND was not an enemy.
		edict_t* lastTrackedEntity = m_pLastEnemy.Get();
		if (lastTrackedEntity && lastTrackedEntity != currentEnemyEdict && lastTrackedEntity->v.health < m_lastEnemyHealth)
		{
			// Previous target (m_pLastEnemy) took damage, and it's NOT our current target (which is null or different)
			if (!isEnemy(lastTrackedEntity) && lastTrackedEntity != m_pEdict) // Check if it was a non-enemy and not self
			{
				float damageToNonEnemy = m_lastEnemyHealth - lastTrackedEntity->v.health;
				if (damageToNonEnemy > 0)
				{
					m_rlHelper.addReward(damageToNonEnemy * RCBotRLHelper::RLConsts::PENALTY_FRIENDLY_FIRE, false);
				}
			}
		}

		if (m_pLastEnemy.Get() && m_pLastEnemy.Get()->v.health <= 0 && m_lastEnemyHealth > 0)
		{
			// Enemy was alive in the previous frame (tracked by m_pLastEnemy) and now is dead.
			// Award for the final damage dealt if it was indeed an enemy.
            if(isEnemy(m_pLastEnemy.Get())) { // Ensure this kill reward is for an actual enemy
			    float damageDealtOnKill = m_lastEnemyHealth; // The health it had before this killing blow frame.
			    m_rlHelper.addReward(damageDealtOnKill * RCBotRLHelper::RLConsts::REWARD_DAMAGE_DEALT, false);
			    m_rlHelper.addReward(RCBotRLHelper::RLConsts::REWARD_KILL_CONFIRMED, false);
            }
		}
		m_pLastEnemy.Set(nullptr);
		m_lastEnemyHealth = 0.0f;
	}

	if (m_pSchedule != nullptr)
	{
		switch (m_pSchedule->Execute(this))
		{
		case RCBotTaskState::RCBotTaskState_Complete:
		case RCBotTaskState::RCBotTaskState_Fail:
			m_pSchedule = nullptr;
			m_Utils->setUtility(nullptr);
			break;
		}
	}
	
	if ( m_pSchedule == nullptr || m_bInterrupted )
	{
        // TODO: getBestUtility and subsequent schedule execution should ideally be aware of
        // m_chosenAIActionThisFrame == BotActionType::TACTIC_PURSUE_DYNAMIC_OBJECTIVE
        // and use m_currentObjectiveFocusID to select/parameterize tasks that directly support
        // pursuing the dynamic objective (e.g., a specific 'PathToDynamicObjective' schedule).
        // For now, interrupting ensures that if no utility aligns, direct movement commands take precedence.
		RCBotUtility *pUtil = m_Utils->getBestUtility(this);

		m_bInterrupted = false;

		if (pUtil != nullptr)
		{
			// We don't have anything to do or we have found something better to do
			if (m_pSchedule == nullptr || !m_Utils->isCurrentUtility(pUtil) )
			{
				if (m_pSchedule != nullptr)
					delete m_pSchedule;

				m_pSchedule = pUtil->execute(this);

				if (m_pSchedule != nullptr)
					m_Utils->setUtility(pUtil);
			}
		}
	}

    // Update last known health at the end of think cycle, before movement
    if (m_pEdict) {
        m_lastThinkHealth = m_pEdict->v.health;
    }
    // m_timeSinceLastDamageTaken should be incremented elsewhere, e.g. at start of Think if no damage taken this frame.
    // For now, it's reset on damage, and would need another place to increment.

    // --- Interaction-Specific Shaping Rewards ---
    if (m_chosenAIActionThisFrame == BotActionType::TACTIC_PURSUE_DYNAMIC_OBJECTIVE &&
        !m_currentObjectiveFocusID.empty() && m_pEdict && (m_pEdict->v.button & IN_USE) && isAlive()) {

        ObjectiveCandidateMetadata* focused_obj = g_ObjectiveManager.getObjectiveCandidateById(m_currentObjectiveFocusID);
        if (focused_obj && focused_obj->is_active) {
            float distance_to_obj = (focused_obj->location - m_pEdict->v.origin).Length();

            if (distance_to_obj < 64.0f) { // Bot is very close to the objective
                if (focused_obj->category_tag == ObjectiveCategoryType::BUTTON_ENTITY ||
                    focused_obj->category_tag == ObjectiveCategoryType::DOOR_ENTITY) {

                    Vector to_obj = (focused_obj->location - getViewOrigin()).Normalize();
                    MAKE_VECTORS(m_pEdict->v.v_angle);
                    float dot_product = DotProduct(to_obj, gpGlobals->v_forward);

                    if (dot_product > 0.707) { // Bot is facing within ~45 degrees of objective
                        m_rlHelper.addReward(RLConsts::REWARD_SHAPING_INTERACT_BUTTON_DOOR);
                        // UTIL_ServerPrintf("Bot %s got shaping reward for using button/door obj %s\n", STRING(m_pEdict->v.netname), m_currentObjectiveFocusID.c_str());
                        // TODO: Consider adding a short cooldown for this specific objective interaction reward
                    }
                }
                // Placeholder for item pickup shaping rewards:
                // else if (focused_obj->category_tag == ObjectiveCategoryType::WEAPON_ITEM && /* some_flag_or_event_indicating_pickup_of_this_item */) {
                //    m_rlHelper.addReward(RLConsts::REWARD_SHAPING_PICKUP_ITEM_OBJECTIVE);
                // }
            }
        }
    }
    // --- End Interaction-Specific Shaping Rewards ---

    // Sentiment-based influence on chosen action (simple version)
    if (isAlive() && m_pEdict) {
        if (m_pEnemy.Get() != nullptr) { // Only consider if has an enemy
            if (m_perceivedPlayerAggression > 0.75f && m_perceivedPlayerCooperation < 0.25f) {
                // If perceiving high aggression and low cooperation
                if (m_pEnemy.Get() == m_lastInteractingPlayerEdict.Get()) {
                    // Bias is stronger if current enemy is the source of recent negative sentiment
                    m_chosenAIActionThisFrame = BotActionType::TACTIC_ENGAGE_ENEMY;
                } else if (m_chosenAIActionThisFrame != BotActionType::TACTIC_RETREAT_OR_FALLBACK &&
                           m_chosenAIActionThisFrame != BotActionType::TACTIC_PURSUE_OBJECTIVE) { // Don't override critical objective or retreat
                    // If general aggression is high due to other interactions, still slightly bias to engage current target
                    // unless already retreating or focusing on an objective.
                    // This avoids making the bot overly aggressive if it's just generally "angry" but current situation is neutral.
                    // For a more direct general aggression: check m_perceivedPlayerAggression without m_lastInteractingPlayerEdict context.
                    // However, the current m_perceivedPlayerAggression is updated by last interaction, so it carries some context.
                    // A more nuanced model would have general mood vs specific player sentiment.
                    // For now, this simplified check is okay.
                     if (RANDOM_FLOAT(0.0f, 1.0f) < 0.3f) { // Add some randomness to not always override
                         m_chosenAIActionThisFrame = BotActionType::TACTIC_ENGAGE_ENEMY;
                     }
                }
            }
            // No direct override for cooperative perception in this simplified version,
            // as it's harder to define a universally "less aggressive" action without more context.
            // It would better influence utility scores for choosing less risky actions.
        }
        // Decay general perception scores slightly over time if no new interactions
        // This should ideally happen if m_lastInteractingPlayerEdict is not recent, or no interaction for a while.
        // For simplicity, a slow decay each think cycle if not actively interacting with that specific player.
        if (m_lastInteractingPlayerEdict.Get() == nullptr || gpGlobals->time - m_chatContextMemory.getLastPlayerInteractionTime(m_lastInteractingPlayerEdict.Get()) > 15.0f) {
             m_perceivedPlayerAggression = std::max(PERCEPTION_BASELINE, m_perceivedPlayerAggression * PERCEPTION_DECAY_RATE);
             m_perceivedPlayerCooperation = std::max(PERCEPTION_BASELINE, m_perceivedPlayerCooperation * PERCEPTION_DECAY_RATE);
        }

    }

    // After AI logic has set m_chosenAIActionThisFrame for the current state (m_previousState if !m_firstThinkCycle, or freshly computed state if m_firstThinkCycle)

    // --- Interaction-Specific Shaping Rewards ---
    // Check after m_pEdict->v.button has been set by AI logic (schedule/utility or direct action choice for TACTIC_PURSUE_DYNAMIC_OBJECTIVE)
    if (m_chosenAIActionThisFrame == BotActionType::TACTIC_PURSUE_DYNAMIC_OBJECTIVE &&
        !m_currentObjectiveFocusID.empty() && m_pEdict && (m_pEdict->v.button & IN_USE) && isAlive()) {

        ObjectiveCandidateMetadata* focused_obj = g_ObjectiveManager.getObjectiveCandidateById(m_currentObjectiveFocusID);
        if (focused_obj && focused_obj->is_active) {
            float distance_to_obj = (focused_obj->location - m_pEdict->v.origin).Length();

            if (distance_to_obj < 64.0f) { // Bot is very close to the objective
                if (focused_obj->category_tag == ObjectiveCategoryType::BUTTON_ENTITY ||
                    focused_obj->category_tag == ObjectiveCategoryType::DOOR_ENTITY) {

                    Vector to_obj = (focused_obj->location - getViewOrigin()).NormalizeSafe();
                    if(to_obj.IsZero()) { // if objective is at view origin, dot product is undefined. Assume facing.
                        m_rlHelper.addReward(RLConsts::REWARD_SHAPING_INTERACT_BUTTON_DOOR);
                    } else {
                        MAKE_VECTORS(m_pEdict->v.v_angle);
                        float dot_product = DotProduct(to_obj, gpGlobals->v_forward);
                        if (dot_product > 0.707) { // Bot is facing within ~45 degrees of objective
                            m_rlHelper.addReward(RLConsts::REWARD_SHAPING_INTERACT_BUTTON_DOOR);
                            // UTIL_ServerPrintf("Bot %s got shaping reward for using button/door obj %s\n", STRING(m_pEdict->v.netname), m_currentObjectiveFocusID.c_str());
                        }
                    }
                }
                // Placeholder for item pickup shaping rewards:
                // else if (focused_obj->category_tag == ObjectiveCategoryType::WEAPON_ITEM && /* some_flag_or_event_indicating_pickup_of_this_item */) {
                //    m_rlHelper.addReward(RLConsts::REWARD_SHAPING_PICKUP_ITEM_OBJECTIVE);
                // }
            }
        }
    }
    // --- End Interaction-Specific Shaping Rewards ---

    m_lastAction = m_chosenAIActionThisFrame;

    if (m_firstThinkCycle && m_pEdict) {
        m_previousState = m_rlHelper.getCurrentBotState( /* Same params as above */
            m_pEdict,
            m_currentObjectiveFocusID,
            m_focusObjectiveLocation,
            m_hasFocusObjectiveLocation,
            m_perceivedPlayerAggression,
            m_perceivedPlayerCooperation,
            m_timeSinceLastDamageTaken,
            (m_pEdict->v.flags & FL_ONGROUND) != 0,
            isUnderWater(),
            (m_pEdict->v.flags & FL_ONLADDER) != 0,
            currentWeaponAmmoMap,
            currentWeaponMaxClipMap,
            currentWeaponIdVal,
            taskCompletionRatio,
            // New args:
            bot_has_current_enemy,
            bot_current_enemy_threat,
            bot_current_enemy_location,
            bot_distance_to_current_enemy,
            bot_direction_to_current_enemy
        );
        m_firstThinkCycle = false;
    } else if (m_pEdict) { // ensure s_prime was computed if !m_firstThinkCycle
        m_previousState = s_prime;
    } else {
        // If no edict (e.g. bot not fully spawned), reset firstThinkCycle to try init again
        m_firstThinkCycle = true;
    }

    // --- RL Agent Learning Step ---
    if (m_pEdict && gpGlobals && m_replayBuffer.size() >= static_cast<size_t>(MIN_REPLAY_BUFFER_SIZE_FOR_LEARNING) &&
        m_replayBuffer.size() >= static_cast<size_t>(RL_LEARNING_BATCH_SIZE) &&
        (gpGlobals->time - m_lastLearnTime) > RL_LEARN_INTERVAL) {

       std::vector<RLTransition> batch = m_replayBuffer.sampleBatch(RL_LEARNING_BATCH_SIZE);
       if (!batch.empty()) {
           for (const RLTransition& transition : batch) {
               m_rlAgent.learn(transition);
           }
           m_lastLearnTime = gpGlobals->time;
           // UTIL_ServerPrintf("Bot %s learned from batch of %d experiences.\n", STRING(m_pEdict->v.netname), RL_LEARNING_BATCH_SIZE);
       }
    }
    // --- End RL Agent Learning Step ---

    // --- Opponent Threat Decay Logic ---
    if (gpGlobals && (gpGlobals->time - m_lastThreatUpdateTime > THREAT_UPDATE_INTERVAL)) {
        for (auto& pair : m_opponent_models) {
            OpponentStats& stats = pair.second;
            // Only decay if no very recent encounter
            if (gpGlobals->time - stats.last_encounter_time > THREAT_UPDATE_INTERVAL * 1.5f) {
                stats.perceived_threat_level *= THREAT_DECAY_RATE;
                // Decay towards baseline instead of zero - more complex, stick to simple decay for now
                // stats.perceived_threat_level = stats.perceived_threat_level * THREAT_DECAY_RATE +
                //                                RCBotBase::OPPONENT_THREAT_NEUTRAL_BASELINE * (1.0f - THREAT_DECAY_RATE);
                if (stats.perceived_threat_level < 0.01f) { // Using a small epsilon rather than OPPONENT_MIN_THREAT if it's 0.0
                    stats.perceived_threat_level = 0.0f;
                }
            }
        }
        m_lastThreatUpdateTime = gpGlobals->time;
    }
    // --- End Opponent Threat Decay Logic ---
}

#define BOT_MOVE_TO_MIN_DISTANCE 16.0f
#define BOT_MOVE_TO_MAX_SPEED 320.0f

void RCBotBase::setCurrentWeapon(uint8_t iState, uint8_t iId, uint8_t iClip)
{
	RCBotWeapon* weapon = m_pWeapons->findById(iId);

	if (weapon != nullptr)
	{
		//weapon->setState();
		weapon->setClip(iClip);
		m_pCurrentWeapon = weapon;
	}
}

bool RCBotBase::isCurrentWeapon(RCBotWeapon* weapon)
{
	return m_pCurrentWeapon == weapon;
}

void RCBotBase::weaponPickup(uint8_t iID)
{
	m_pWeapons->weaponPickup(iID);
}

void RCBotBase::selectWeapon(RCBotWeapon* weapon)
{
	if ( !isCurrentWeapon(weapon) )
		FakeClientCommand(m_pEdict, weapon->getClassname());
}

void RCBotBase::pressButton(int button)
{
	m_pEdict->v.button |= button;
}

void RCBotBase::RunPlayerMove()
{
	float msec = (gpGlobals->time - m_fLastRunPlayerMove) * 1000;

	Vector vOrigin = getViewOrigin();

	float fForwardSpeed = 0;
	float fSideSpeed = 0;
	float fUpSpeed = 0;

	if (msec > 255)
		msec = 255;

	if (m_vLookAt.isValid() )
	{
		float fTurnSpeed = 10.0f;
		entvars_t* pev = &m_pEdict->v;
		Vector vLook = m_vLookAt.getValue() - vOrigin;
		Vector vAngles = RCBotUtils::VectorToAngles(vLook);
		RCBotUtils::FixAngles(&vAngles);

		pev->idealpitch = -vAngles.x;
		pev->ideal_yaw = vAngles.y;

		vAngles.z = 0;
		pev->v_angle.z = pev->v_angle.z = 0;

		pev->ideal_yaw = vAngles.y;

		// change angles smoothly

		float temp;

		//temp = 1/1+exp(-fabs((pev->ideal_yaw+180.0f)-(pev->v_angle.y+180.0f))/180);
		temp = fabs(pev->ideal_yaw + 180.0f - (pev->v_angle.y + 180.0f));

		fTurnSpeed = temp / fTurnSpeed;//fabs((pev->ideal_yaw+180.0f)-(pev->v_angle.y+180.0f))/20;//m_fTurnSpeed;
		// change yaw
		RCBotUtils::ChangeAngle(&fTurnSpeed, &pev->ideal_yaw, &pev->v_angle.y, &pev->angles.y); // 5 degrees

		//temp = 1/1+exp(-fabs((pev->idealpitch+180.0f)-(pev->v_angle.x+180.0f))/180);
		temp = fabs(pev->idealpitch + 180.0f - (pev->v_angle.x + 180.0f));

		// set by ChangeAngles... remove this functionality soon...
		fTurnSpeed = temp / fTurnSpeed;
		//fTurnSpeed = fabs((pev->idealpitch+180.0f)-(pev->v_angle.x+180.0f))/20;//m_fTurnSpeed;

		// change pitch
		RCBotUtils::ChangeAngle(&fTurnSpeed, &pev->idealpitch, &pev->v_angle.x, &pev->angles.x);

		//pev->v_angle.x = -pev->v_angle.x;
		pev->angles.x = -pev->v_angle.x / 3;

		pev->angles.y = pev->v_angle.y;//*/
	}

	if (m_vMoveTo.isValid())
	{
		Vector vMoveTo = m_vMoveTo.getValue();

		if (distanceFrom2D(vMoveTo) > BOT_MOVE_TO_MIN_DISTANCE)
		{
			Vector vComponent = m_vMoveTo.getValue() - vOrigin;
			Vector vMove = RCBotUtils::VectorToAngles(vComponent);
			vMove = vMove.Normalize();

			float fAngle = RCBotUtils::yawAngle(m_pEdict, vMoveTo);

			float radians = fAngle * 3.141592f / 180.f; // degrees to radians
			fForwardSpeed = cos(radians) * BOT_MOVE_TO_MAX_SPEED;
			fSideSpeed = sin(radians) * BOT_MOVE_TO_MAX_SPEED;

			fUpSpeed = 0;
		}

		if (getActualSpeed() < 1)
		{
			if (RANDOM_LONG(0, 100) > 50)
				jump();
		}

		if (m_pEdict->v.movetype == MOVETYPE_FLY)
			m_pEdict->v.button |= IN_FORWARD;
	}

	m_fLastRunPlayerMove = gpGlobals->time;

	(*g_engfuncs.pfnRunPlayerMove)(m_pEdict, m_pEdict->v.angles, fForwardSpeed, fSideSpeed, fUpSpeed, m_pEdict->v.button,
		m_pEdict->v.impulse, (uint8_t)msec);

}

void RCBotBase::setProfile(RCBotProfile* profile)
{
	m_pProfile = profile;
}

void RCBotBase::setEdict(edict_t* pEdict)
{
	m_pEdict = pEdict;
	m_pEdict->v.flags |= FL_FAKECLIENT;
}

void RCBotBase::setFOV(float fFOV)
{
	m_fFovCos = cos(RCBotUtils::DegreesToRadians(fFOV));
}

Vector RCBotBase::getViewOrigin()
{
	return m_pEdict->v.origin + m_pEdict->v.view_ofs;
}

bool RCBotBase::inViewCone(Vector &vOrigin)
{
	static Vector vecLOS;
	static float flDot;

	// in fov? Check angle to edict
	MAKE_VECTORS(m_pEdict->v.v_angle);

	vecLOS = vOrigin - getViewOrigin();
	vecLOS = vecLOS.Normalize();

	flDot = DotProduct(vecLOS, gpGlobals->v_forward);

	return flDot > m_fFovCos;
}

void RCBotBase::setUpClientInfo()
{
	char* sInfoBuffer;

	int index = ENTINDEX(m_pEdict);

	m_pEdict->v.frags = 0;

	CALL_GAME_ENTITY(PLID, "player", VARS(m_pEdict)); // Olo

	sInfoBuffer = GET_INFOKEYBUFFER(m_pEdict);

	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "rate", "3500");
	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "cl_updaterate", "20");
	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "cl_dlmax", "128");

	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "_vgui_menus", "0");

	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "_ah", "0");
	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "dm", "0");
	(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "tracker", "0");

	if (m_pProfile->getModel() != nullptr)
	{
		(*g_engfuncs.pfnSetClientKeyValue)(index, sInfoBuffer, "model", (char*) m_pProfile->getModel());
	}

}

float RCBotBase::getEnemyFactor(edict_t* pEntity)
{
	return distanceFrom(pEntity);
}

void RCBotBase::newVisible(edict_t* pEntity)
{
	if ( isEnemy(pEntity) )
	{
		edict_t* pCurrentEnemy = m_pEnemy.Get();

		if (pCurrentEnemy == nullptr || (getEnemyFactor(pEntity) < getEnemyFactor(pCurrentEnemy)))
		{
			m_pEnemy.Set(pEntity);
			m_bInterrupted = true; // interrupt the bots utility
		}
	}
}

void RCBotBase::lostVisible(edict_t* pEntity)
{
	if (pEntity == nullptr)
		return;

	if (m_pEnemy.Get() == pEntity)
	{
		m_pEnemy.Set(nullptr);
		m_bInterrupted = true; // interrupt the bots utility
	}
}

bool RCBotBase::isAlive()
{
	return m_pEdict->v.deadflag == DEAD_NO;
}

void RCBotBase::respawn()
{
	primaryAttack();
}


float RCBotBase::distanceFrom(const edict_t* pEntity)
{
	return distanceFrom(RCBotUtils::entityOrigin(pEntity));
}


float RCBotBase::distanceFrom(const Vector& vOrigin)
{
	return (vOrigin - getViewOrigin()).Length();
}


// --- RL Related Helper Implementations ---
// BotState RCBotBase::getCurrentBotState() const is now handled by m_rlHelper.getCurrentBotState()
// void RCBotBase::calculateReward() is now handled by m_rlHelper.addReward()

BotActionType RCBotBase::determineBotAction() const {
    return m_chosenAIActionThisFrame;
}

// --- Game Event Recording ---
void RCBotBase::recordGameEvent(const GameEvent& event)
{
    m_shortTermMemory.addEvent(event);

    // Also add to contextual chat/event history
    if (m_chatContextMemory.getMaxItems() > 0) {
        ContextualItem context_item;
        context_item.type = GAME_EVENT_ITEM; // Ensure this is GAME_EVENT_ITEM or similar
        context_item.timestamp = event.timestamp;
        context_item.game_event_data = event;
        m_chatContextMemory.addItem(context_item);
    }

    // --- Opponent Modeling based on Damage Event ---
    // Corrected to use 'event' parameter and GameEvent structure
    if ((event.type == EVENT_DAMAGE_TAKEN || event.type == EVENT_DAMAGE_DEALT) && gpGlobals && g_engfuncs.pfnGetPlayerUserId) {
        edict_t* pAttacker = event.edict_source;
        edict_t* pVictimOrTarget = event.edict_target; // Victim if damage taken, Target if damage dealt
        float damageAmount = event.float_data1;

        // Case 1: Bot took damage from a human player
        if (event.type == EVENT_DAMAGE_TAKEN && pVictimOrTarget == m_pEdict && pAttacker && pAttacker != m_pEdict) {
            int attacker_player_id = GetPlayerUniqueIdFromEdict_Static(pAttacker);

            if (attacker_player_id != -1) { // Attacker is a human player
                OpponentStats& stats = m_opponent_models[attacker_player_id];

                if (stats.player_unique_id == 0) { // New entry
                    stats.player_unique_id = attacker_player_id;
                }

                stats.last_known_name = STRING(pAttacker->v.netname);
                stats.damage_dealt_to_bot += damageAmount;
                stats.last_encounter_time = gpGlobals->time;
                stats.last_known_location = pAttacker->v.origin;
                stats.perceived_threat_level += damageAmount * RCBotBase::THREAT_FROM_DAMAGE_FACTOR;
                stats.perceived_threat_level = std::min(stats.perceived_threat_level, 1.0f); // Clamp to MAX_THREAT (assumed 1.0f)

                // UTIL_ServerPrintf("Bot %s took %.1f dmg from player %s (ID %d). Threat: %.2f\n",
                //    STRING(m_pEdict->v.netname), damageAmount, stats.last_known_name.c_str(), attacker_player_id, stats.perceived_threat_level);
            }
        }
        // Case 2: Bot dealt damage to a human player
        else if (event.type == EVENT_DAMAGE_DEALT && pAttacker == m_pEdict && pVictimOrTarget && pVictimOrTarget != m_pEdict) {
            int target_player_id = GetPlayerUniqueIdFromEdict_Static(pVictimOrTarget);

            if (target_player_id != -1) { // Target is a human player
                OpponentStats& stats = m_opponent_models[target_player_id];

                if (stats.player_unique_id == 0) { // New entry
                    stats.player_unique_id = target_player_id;
                }

                stats.last_known_name = STRING(pVictimOrTarget->v.netname);
                stats.damage_taken_from_bot += damageAmount; // This is damage bot dealt TO target
                stats.last_encounter_time = gpGlobals->time;
                stats.last_known_location = pVictimOrTarget->v.origin;
                // Dealing damage doesn't directly increase threat OF the target, but confirms interaction and updates stats.
                // Threat of target is how much they threaten the bot, not how much bot threatens them.
            }
        }
    }
}

void RCBotBase::ProcessDeathInvolvingBot(edict_t* pOtherPlayer, bool bBotWasKilled, const Vector& deathLocation) {
    if (!pOtherPlayer || !m_pEdict || !gpGlobals || !g_engfuncs.pfnGetPlayerUserId) { // Check m_pEdict
        return;
    }

    // Ensure the "other player" is a human player
    if ((pOtherPlayer->v.flags & FL_CLIENT) && !(pOtherPlayer->v.flags & FL_FAKECLIENT) && pOtherPlayer != m_pEdict) { // Ensure not self
        int other_player_id = (*g_engfuncs.pfnGetPlayerUserId)(pOtherPlayer);
        if (other_player_id == -1) {
            return; // Invalid ID
        }

        OpponentStats& stats = m_opponent_models[other_player_id]; // Creates if not exists
        stats.player_unique_id = other_player_id; // Ensure it's set if new entry
        stats.last_known_name = STRING(pOtherPlayer->v.netname);
        stats.last_encounter_time = gpGlobals->time;
        stats.last_known_location = deathLocation;

        GameEvent death_event;
        if (bBotWasKilled) { // Bot was killed by pOtherPlayer
            stats.kills_by_opponent_on_bot++;
            stats.perceived_threat_level += THREAT_FROM_KILL_FACTOR;
            stats.perceived_threat_level = std::min(1.0f, stats.perceived_threat_level); // Clamp

            death_event = GameEvent(EVENT_PLAYER_DIED, gpGlobals->time); // Bot is a player
            death_event.edict_target = m_pEdict; // Victim
            death_event.edict_source = pOtherPlayer; // Killer
        } else { // Bot killed pOtherPlayer
            stats.kills_by_bot_on_opponent++;
            stats.perceived_threat_level -= THREAT_FROM_KILL_FACTOR * 0.5f;
            stats.perceived_threat_level = std::max(0.0f, stats.perceived_threat_level); // Clamp

            death_event = GameEvent(EVENT_PLAYER_DIED, gpGlobals->time);
            death_event.edict_target = pOtherPlayer; // Victim
            death_event.edict_source = m_pEdict;    // Killer
        }
        recordGameEvent(death_event); // Record the death event
    }
}

// Game-specific event handling entry points
void RCBotBase::BotTakeDamage(edict_t* pInflictor, edict_t* pAttacker, float flDamage, int bitsDamageType) {
    if (!m_pEdict || !gpGlobals) return;

    // Create a game event for damage taken
    // Note: GameEvent::DamageTaken is a static helper from the .h file.
    // We need to ensure it's correctly defined or construct manually.
    // Assuming manual construction for now to match the .h:
    GameEvent damage_event(EVENT_DAMAGE_TAKEN, gpGlobals->time);
    damage_event.float_data1 = flDamage;
    damage_event.edict_source = pAttacker; // The one who dealt damage
    damage_event.edict_target = m_pEdict;  // The one who took damage (the bot)
    // damage_event.int_data1 = bitsDamageType; // Optional: store damage type if needed

    recordGameEvent(damage_event);

    // The original takeDamage logic (like RL penalties) is already in Think() based on health changes.
    // This function specifically records the event for STM and potentially other systems.
}

void RCBotBase::BotKilled(edict_t* pKiller, int realVictimIsKiller) {
    if (!m_pEdict || !gpGlobals) return;

    // Create a game event for bot death
    GameEvent killed_event(EVENT_PLAYER_DIED, gpGlobals->time); // Bot is a player
    killed_event.edict_target = m_pEdict;  // The bot is the victim
    killed_event.edict_source = pKiller;   // The entity that killed the bot
    // killed_event.int_data1 = realVictimIsKiller; // Optional: store gib status or similar

    recordGameEvent(killed_event);

    // Call ProcessDeathInvolvingBot to update opponent model if killer is a human player
    // Note: The original Killed() might have also called ProcessDeathInvolvingBot.
    // Ensure no double processing if this BotKilled is called AND the manager's ProcessPlayerDeathEvent calls it too.
    // For now, assuming this is the primary path for bot's own death event that should update models.
    if (pKiller && pKiller != m_pEdict) { // If killed by someone else
         ProcessDeathInvolvingBot(pKiller, true /*bot was killed*/, m_pEdict->v.origin);
    }


    // Original death logic (RL penalty, spawnInit) is in Think() when it detects !isAlive().
    // This function is for recording the explicit death event.
}


// --- Chat Related Methods ---
void RCBotBase::sayChat(const std::string& context_trigger) {
    if (!m_pEdict || !isAlive()) return; // Don't chat if dead or no edict

    // Pass the bot's general perception scores.
    // If chat were directed at a specific player AND we had per-player sentiment,
    // we'd retrieve that specific player's perceived aggression/cooperation.
    // For now, m_perceivedPlayerAggression/Cooperation are general sentiment from last interaction.
    float target_player_aggression = m_perceivedPlayerAggression;
    float target_player_cooperation = m_perceivedPlayerCooperation;

    TaggedChatMessage chatMessage = g_ChatManager.generateBotChat(
        this,
        context_trigger,
        &m_chatContextMemory,
        target_player_aggression,
        target_player_cooperation
    );

    if (!chatMessage.message.empty()) {
        char say_text[256];
        // Using bot's name in chat:
        snprintf(say_text, sizeof(say_text), "%s: %s", STRING(m_pEdict->v.netname), chatMessage.message.c_str());

        // Send to all players
        MESSAGE_BEGIN(MSG_BROADCAST, SVC_SAYTEXT);
            WRITE_BYTE(ENTINDEX(m_pEdict)); // Entity that is "speaking"
            WRITE_STRING(say_text);
        MESSAGE_END();

        // Add bot's own message to its context memory and sent messages deque
        // Ensure sender_entity_index is set for the bot's own message.
        TaggedChatMessage bot_sent_message = chatMessage; // Copy
        if (m_pEdict) {
            bot_sent_message.sender_entity_index = ENTINDEX(m_pEdict);
        } else {
            bot_sent_message.sender_entity_index = 0; // Should not happen if bot is saying something
        }

        ContextualItem bot_said_item(bot_sent_message);
        m_chatContextMemory.addItem(bot_said_item);

        m_sentChatMessages.push_back(bot_sent_message);
        if (m_sentChatMessages.size() > MAX_SENT_CHAT_HISTORY) {
            m_sentChatMessages.pop_front();
        }

        // Placeholder: If the chat was a taunt, update m_lastTauntTime
        if (chatMessage.sentiment == SENTIMENT_TAUNT) {
            m_lastTauntTime = gpGlobals->time;
        }
    }
}

void RCBotBase::setPersona(BotPersona persona) {
    m_persona = persona;
}

BotPersona RCBotBase::getPersona() const {
    return m_persona;
}

void RCBotBase::evaluateAndAdjustPersona() {
    // Placeholder for persona adjustment logic
    // This could be based on m_engagementScore, m_aggressivenessScore, game outcomes, etc.
    // For now, let's cycle persona for demonstration if a command is added later.
    m_timeSinceLastPersonaEvaluation = 0.0f; // Reset timer
}

void RCBotBase::updatePerceptionFromPlayerChat(edict_t* pPlayerEdict, float chat_sentiment_score) {
    if (!pPlayerEdict || pPlayerEdict == m_pEdict) return; // Ignore self or null

    m_lastInteractingPlayerEdict.Set(pPlayerEdict);

    // Simple update: directly influence based on chat sentiment.
    // More complex: decay old values, consider player history, etc.
    // Current sentiment_score: positive means cooperative, negative means aggressive (based on SentimentAnalyzer)

    if (chat_sentiment_score > 0) { // Positive sentiment
        m_perceivedPlayerCooperation += chat_sentiment_score * PERCEPTION_SENTIMENT_TO_COOPERATION_FACTOR; // Use const
        m_perceivedPlayerAggression -= chat_sentiment_score * PERCEPTION_SENTIMENT_TO_AGGRESSION_FACTOR * 0.5f; // Positive chat reduces perceived aggression
    } else if (chat_sentiment_score < 0) { // Negative sentiment
        m_perceivedPlayerAggression += std::abs(chat_sentiment_score) * PERCEPTION_SENTIMENT_TO_AGGRESSION_FACTOR; // Use const
        m_perceivedPlayerCooperation -= std::abs(chat_sentiment_score) * PERCEPTION_SENTIMENT_TO_COOPERATION_FACTOR * 0.5f; // Negative chat reduces perceived cooperation
    }

    // Clamp values to [0, 1]
    m_perceivedPlayerAggression = std::max(0.0f, std::min(1.0f, m_perceivedPlayerAggression));
    m_perceivedPlayerCooperation = std::max(0.0f, std::min(1.0f, m_perceivedPlayerCooperation));

    // UTIL_ServerPrintf("Bot %s perception of %s: Aggro=%.2f, Coop=%.2f (after chat score %.2f)\n",
    //    STRING(m_pEdict->v.netname), STRING(pPlayerEdict->v.netname), m_perceivedPlayerAggression, m_perceivedPlayerCooperation, chat_sentiment_score);
}


// --- Non-Visual Entity Interaction Novelty ---
// This method is called when a non-visual interaction occurs (e.g. bot bumps into an entity, uses an entity)
// It's a placeholder for where more specific game event handling would go.
void RCBotBase::processEntityInteractionNovelty(edict_t* pEntity, const std::string& interaction_type) {
    if (!pEntity || FNullEnt(pEntity) || pEntity == m_pEdict) {
        return;
    }

    std::vector<float> features = extractEntityFeatures(pEntity, interaction_type);
    if (features.empty()) {
        return;
    }

    float min_distance_sq = std::numeric_limits<float>::max();
    if (m_seenEntityFeaturesLog.empty()) {
        // First interaction of this type, always max novelty.
        // No need to set min_distance_sq, it remains max.
    } else {
        for (const auto& seen_features : m_seenEntityFeaturesLog) {
            if (seen_features.size() != features.size()) continue; // Should not happen if features are consistent
            float dist_sq = 0.0f;
            for (size_t i = 0; i < features.size(); ++i) {
                dist_sq += std::pow(features[i] - seen_features[i], 2);
            }
            if (dist_sq < min_distance_sq) {
                min_distance_sq = dist_sq;
            }
        }
    }

    float novelty_score = 0.0f;
    if (min_distance_sq > ENTITY_FEATURE_NOVELTY_THRESHOLD * ENTITY_FEATURE_NOVELTY_THRESHOLD) { // Compare squared distances
        novelty_score = sqrt(min_distance_sq) * ENTITY_NOVELTY_REWARD_MULTIPLIER; // Example scoring
        m_curiosityScore += novelty_score; // Boost general curiosity

        // Add to item-specific curiosity as well
        std::string entity_id_str = std::string(STRING(pEntity->v.classname)) + "_" + interaction_type;
        m_itemCuriosity[entity_id_str] = std::min(50.0f, m_itemCuriosity[entity_id_str] + novelty_score * 2.0f); // Higher boost for specific item

        // Log this feature set as seen
        m_seenEntityFeaturesLog.push_back(features);
        if (m_seenEntityFeaturesLog.size() > MAX_SEEN_FEATURES_LOG_SIZE) {
            m_seenEntityFeaturesLog.pop_front();
        }

        // UTIL_ServerPrintf("Bot %s: Novel interaction with %s (type: %s). Novelty: %.2f. Curiosity: %.2f\n",
        //    STRING(m_pEdict->v.netname), STRING(pEntity->v.classname), interaction_type.c_str(), novelty_score, m_curiosityScore);

        // --- START of ADDED LOGIC for Subtask 7.11 ---
        if (pEntity) { // Ensure pEntity is valid (already checked above, but good practice)
            std::string actual_classname = STRING(pEntity->v.classname);
            if (!g_ObjectiveManager.isClassnameGloballyInteresting(actual_classname)) {
                // It's a novel interaction with an entity type not on our main "interesting" list.
                // Log it as a dynamic objective candidate based on this interaction.
                g_ObjectiveManager.discoverObjectiveCandidate(
                    pEntity,
                    pEntity->v.origin,
                    actual_classname,
                    "interaction_novelty", // New discovery_event_type
                    pEntity->v.team
                );
                // UTIL_ServerPrintf("Bot %s: Novel interaction with unknown type %s (ID: %s), logged as objective candidate.\n",
                //    STRING(m_pEdict->v.netname), actual_classname.c_str(), g_ObjectiveManager.generateUniqueIDForEntity(pEntity).c_str());
            }
        }
        // --- END of ADDED LOGIC for Subtask 7.11 ---

    } else {
        // Not novel enough, or seen before.
        // Could potentially decay m_itemCuriosity for this specific entity_id_str if not interacted with recently.
    }
}

// Helper to extract features from an entity for novelty detection
// This is highly game/mod dependent.
std::vector<float> RCBotBase::extractEntityFeatures(edict_t* pEntity, const std::string& interaction_type) const {
    std::vector<float> features;
    if (!pEntity || FNullEnt(pEntity)) return features;

    // Feature 1: Entity classname (converted to a numerical ID or hash if possible, or one-hot encoded if few types)
    // For simplicity, let's assume g_ObjectiveManager can provide an ID or we use a local map.
    // This part is complex to make generic. For now, a placeholder.
    // features.push_back(static_cast<float>(std::hash<std::string>{}(STRING(pEntity->v.classname)) % 1000) / 1000.0f); // Example hash

    // Feature 2: Interaction type (hash or enum to float)
    // features.push_back(static_cast<float>(std::hash<std::string>{}(interaction_type) % 100) / 100.0f);

    // Feature 3: Entity health (if applicable, normalized)
    if (pEntity->v.health > 0) features.push_back(pEntity->v.health / 100.0f); else features.push_back(0.0f);

    // Feature 4: Distance to entity (normalized by some typical interaction range)
    // float dist = (pEntity->v.origin - m_pEdict->v.origin).Length();
    // features.push_back(dist / 1000.0f); // Normalize by e.g. 1000 units

    // Feature 5: Relative angle / Facing (dot product)
    // Vector dirToEntity = (pEntity->v.origin - m_pEdict->v.origin).Normalize();
    // MAKE_VECTORS(m_pEdict->v.v_angle);
    // features.push_back((DotProduct(gpGlobals->v_forward, dirToEntity) + 1.0f) / 2.0f); // Normalized to [0,1]

    // For this subtask, the presence of features is more important than their specific content for testing the mechanism.
    // A very simple feature set:
    features.push_back(static_cast<float>(pEntity->v.playerclass)); // example, might be 0 for non-players
    features.push_back(static_cast<float>(pEntity->v.modelindex % 100) / 100.0f); // example based on model

    // The key is that these features, however derived, allow differentiation between interactions.
    // If all interactions produce the same feature vector, novelty won't be detected.
    if (features.empty()) { // Ensure some feature is always present if entity is valid
        features.push_back(1.0f); // Generic feature if others are not applicable
    }

    return features;
}