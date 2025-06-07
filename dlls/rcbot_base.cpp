#include "rcbot_base.h"
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
#include "rcbot_navigator.h" // For gRCBotNavigatorNodes
#include "enginecallback.h" // For GetEngineFunctions, server_print
#include "util.h" // For UTIL_SayTextAll (needed for chat)
#include "rcbot_chat_manager.h" // For g_ChatManager
#include <math.h>
#include <string> // Required for std::string manipulation in Think
#include <random> // For random chat chance

RCBotBase ::RCBotBase()
{
	m_pVisibles = new RCBotVisibles(this);
	m_Utils = new RCBotUtilities();
	m_pSchedule = nullptr;
	setFOV(RCBOT_DEFAULT_FOV);
	// m_shortTermMemory is implicitly default-constructed
	// Explicitly: m_shortTermMemory = RCBotShortTermMemory();
	m_pLongTermMemory = nullptr; // Initialize LTM pointer
	m_curiosityScore = 0.0f;
	// m_encounteredEntityClasses, m_visitedWaypoints, m_itemCuriosity are default-initialized
	// m_objectiveInterests is default-initialized (empty map)
	m_currentFocusObjective = ""; // No initial focus
	m_currentMacroAction = nullptr; // No active macro action initially
	m_persona = PERSONA_NEUTRAL; // Default persona
	m_lastTauntTime = 0.0f;
	m_damageTakenPostTaunt = 0;
	m_engagementScore = 0.0f;
	m_aggressivenessScore = 0.0f;
	m_timeSinceLastPersonaEvaluation = 0.0f;
	// m_sentChatMessages is default-initialized (empty deque)
    // m_chatContextMemory is default-initialized
	m_previousDistanceToFocusObjective = -1.0f;
	m_hasFocusObjectiveLocation = false;
	m_shapingRewardAccumulator = 0.0f;
	m_perceivedPlayerAggression = 0.5f; // Initialize perception
	m_perceivedPlayerCooperation = 0.5f; // Initialize perception
	m_lastInteractingPlayerEdict.Set(nullptr); // Initialize EHandle

	Init();
	loadMacroActions(); // Load predefined macro actions
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
	m_chatContextMemory.clear(); // Clear chat context on spawn/respawn

	// Reset perception on spawn/respawn
	m_perceivedPlayerAggression = 0.5f;
	m_perceivedPlayerCooperation = 0.5f;
	m_lastInteractingPlayerEdict.Set(nullptr);
	m_previousDistanceToFocusObjective = -1.0f; // Also reset objective focus distance
	m_hasFocusObjectiveLocation = false;
    m_shapingRewardAccumulator = 0.0f;
}

void RCBotBase::setAmmo(uint8_t index, uint8_t amount)
{
	m_pWeapons->setAmmo(index, amount);
}

#define RCBOT_RESPAWN_WAIT_TIME 2.0f

void RCBotBase::Think()
{
    // Handle Macro Action execution first if one is active
    if (m_currentMacroAction != nullptr) {
        if (!isAlive()) { // Stop macro if bot dies
             stopCurrentMacroAction();
        } else if (m_currentMacroAction->isFinished()) {
            // SERVER_PRINT("RCBot %s: Macro '%s' finished.\n", STRING(m_pEdict->v.netname), m_currentMacroAction->m_name.c_str());
            stopCurrentMacroAction();
        } else {
            // Clear buttons before macro update, so macro steps have direct control for this frame
            m_pEdict->v.button = 0;
            // Note: m_vMoveTo and m_vLookAt are reset at the start of the regular Think().
            // If a macro needs to set these, it will. If it doesn't, they remain reset.
            // The macro's update might also directly manipulate pev->v_angle or buttons.

            m_currentMacroAction->update(this, gpGlobals->frametime);

            // If macro is meant to fully control bot for its duration, we return here.
            // This means subsequent Think() logic (curiosity, objectives, schedule) is skipped.
            return;
        }
    }

	m_pEdict->v.button = 0;
	m_pEdict->v.impulse = 0;
	m_fSpeedPercent = 1.0f; // full speed
	m_vMoveTo.reset();
	m_vLookAt.reset();

	if (!isAlive())
	{
		if (m_bPreviousAliveState == true)
		{
			spawnInit();
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
		if (m_bPreviousAliveState == false)
		{
			m_bPreviousAliveState = true;

			m_pEdict->v.v_angle = m_pEdict->v.angles;
		}
	}
	
	if (m_pEnemy.Get() != nullptr)
	{
		edict_t* pEnemy = m_pEnemy.Get();

		RCBotWeapon* pBestWeapon = m_pWeapons->getBestWeapon(pEnemy);

		if (pBestWeapon != nullptr && m_pCurrentWeapon != pBestWeapon)
		{
			selectWeapon(pBestWeapon);
		}
	}

	m_pVisibles->tasks(m_pProfile->getVisRevs());

	// --- Curiosity System ---
	// 1. Decay existing scores
	m_curiosityScore *= CURIOSITY_DECAY_RATE;
	for (auto it = m_itemCuriosity.begin(); it != m_itemCuriosity.end(); /* no increment */) {
		it->second *= ITEM_CURIOSITY_DECAY_RATE;
		if (it->second < 0.01f) { // Threshold to remove very small scores
			it = m_itemCuriosity.erase(it);
		} else {
			++it;
		}
	}

	// 2. Detect new entities
	// Assuming m_pVisibles stores currently visible entities.
	// The actual structure of RCBotVisibles and how to iterate it would be needed.
	// This is a conceptual placeholder for iterating visible entities.
	if (m_pVisibles) {
		// Let's assume m_pVisibles has a way to get a list of edict_t*
		// This is hypothetical based on typical bot structure.
		// Replace with actual way to iterate visibles from RCBotVisibles class.
		// for (const auto& visible_entity_ptr : m_pVisibles->getVisibleEntities()) {
		//    if(visible_entity_ptr) {
		//        const char* classname = STRING(visible_entity_ptr->v.classname);
		//        std::string classNameStr(classname);
		//        if (!classNameStr.empty() && m_encounteredEntityClasses.find(classNameStr) == m_encounteredEntityClasses.end()) {
		//            m_encounteredEntityClasses.insert(classNameStr);
		//            m_curiosityScore += NEW_ENTITY_BONUS;
		//            m_itemCuriosity[classNameStr] = NEW_ENTITY_BONUS; // Or some other value
		//            // SERVER_PRINT( "RCBot %s: New entity encountered: %s\n", STRING(m_pEdict->v.netname), classname);
		//        }
		//    }
		// }
		// Given current structure, newVisible/lostVisible might be better integration points,
		// but for a general "scan for novelty", iterating all known nearby entities is an option.
		// For now, let's simulate seeing one new item for testing if m_pEnemy is new
		if (m_pEnemy.Get() != nullptr) {
			const char* classname = STRING(m_pEnemy.Get()->v.classname);
			std::string classNameStr(classname);
			if(!classNameStr.empty() && classNameStr != "player" && m_encounteredEntityClasses.find(classNameStr) == m_encounteredEntityClasses.end()){
				m_encounteredEntityClasses.insert(classNameStr);
				m_curiosityScore += NEW_ENTITY_BONUS;
				m_itemCuriosity[classNameStr] = NEW_ENTITY_BONUS;
				// SERVER_PRINT( "RCBot %s: New enemy entity type encountered: %s, curiosity: %f\n", STRING(m_pEdict->v.netname), classname, m_curiosityScore);
			}
		}
	}

	// 3. Detect new areas (waypoints)
	if (gRCBotNavigatorNodes && m_pEdict) {
		PathNode* nearestNode = gRCBotNavigatorNodes->Nearest(m_pEdict->v.origin, 500.0f, false); // Max distance 500 units
		if (nearestNode) {
			int waypointId = nearestNode->iId; // Assuming iId is the unique ID for the waypoint
			if (m_visitedWaypoints.find(waypointId) == m_visitedWaypoints.end()) {
				m_visitedWaypoints.insert(waypointId);
				m_curiosityScore += NEW_AREA_BONUS;
				m_itemCuriosity["waypoint_" + std::to_string(waypointId)] = NEW_AREA_BONUS;
				// SERVER_PRINT( "RCBot %s: New area (waypoint %d) discovered, curiosity: %f\n", STRING(m_pEdict->v.netname), waypointId, m_curiosityScore);
			}
		}
	}
	// --- End Curiosity System ---

	// --- Objective Interest System ---
	// 1. Decay existing objective interests
	for (auto it = m_objectiveInterests.begin(); it != m_objectiveInterests.end(); /* no increment */) {
		it->second *= INTEREST_DECAY_RATE;
		if (it->second < 0.01f) { // Threshold to remove very small interest scores
			it = m_objectiveInterests.erase(it);
		} else {
			++it;
		}
	}

	// 2. Link Curiosity to Interest (Generate/Boost Objectives from Novelty)
	for (const auto& pair : m_itemCuriosity) {
		if (pair.second > CURIOSITY_TO_INTEREST_THRESHOLD) {
			std::string objectiveKey = "investigate_" + pair.first; // e.g., "investigate_item_medkit", "investigate_waypoint_32"
			if (m_objectiveInterests.find(objectiveKey) == m_objectiveInterests.end()) {
				m_objectiveInterests[objectiveKey] = INITIAL_OBJECTIVE_INTEREST;
				// SERVER_PRINT("RCBot %s: New objective from curiosity: %s\n", STRING(m_pEdict->v.netname), objectiveKey.c_str());
			} else {
				// Boost existing interest if it was already there but perhaps decayed
				m_objectiveInterests[objectiveKey] += INITIAL_OBJECTIVE_INTEREST * 0.5f; // Smaller boost for existing
			}
		}
	}

	// 3. Basic Objective Selection (Placeholder)
	float maxInterest = 0.0f;
	std::string bestObjective = "";
	if (!m_objectiveInterests.empty()) {
		for (const auto& pair : m_objectiveInterests) {
			if (pair.second > maxInterest) {
				maxInterest = pair.second;
				bestObjective = pair.first;
			}
		}
		if (!bestObjective.empty() && bestObjective != m_currentFocusObjective) {
			m_currentFocusObjective = bestObjective;
			// UTIL_ServerPrintf("RCBot %s: New focus objective: %s (Interest: %f)\n", STRING(m_pEdict->v.netname), m_currentFocusObjective.c_str(), maxInterest);

			// New objective set, reset shaping variables and try to find location
			m_previousDistanceToFocusObjective = -1.0f;
			m_hasFocusObjectiveLocation = false;
			m_focusObjectiveLocation = Vector(0,0,0); // Reset location

			const std::string& objective_key = m_currentFocusObjective;
			static const std::string waypoint_prefix = "investigate_waypoint_";

			if (objective_key.rfind(waypoint_prefix, 0) == 0) { // Check prefix
				std::string waypoint_id_str = objective_key.substr(waypoint_prefix.length());
				try {
					int waypoint_id = std::stoi(waypoint_id_str);
					if (gRCBotNavigatorNodes) {
						// Iterate m_UsedNodes to find by ID (assuming ID is index)
                        // A more direct gRCBotNavigatorNodes->getNodeById(waypoint_id) would be better.
                        RCBotNavigatorNode* targetNode = nullptr;
                        for(RCBotNavigatorNode* pNode : gRCBotNavigatorNodes->getUsedNodes()) { // Use getter for m_UsedNodes
                            if(pNode && pNode->getIndex() == waypoint_id && pNode->isUsed()) {
                                targetNode = pNode;
                                break;
                            }
                        }

						if (targetNode) {
							m_focusObjectiveLocation = targetNode->getOrigin();
							m_hasFocusObjectiveLocation = true;
							if (m_pEdict) { // Ensure edict is valid before accessing origin
							    m_previousDistanceToFocusObjective = (m_pEdict->v.origin - m_focusObjectiveLocation).Length();
                                // UTIL_ServerPrintf("RCBot %s: Objective location SET for %s at (%.1f, %.1f, %.1f), initial dist: %.1f\n",
                                //     STRING(m_pEdict->v.netname), objective_key.c_str(),
                                //     m_focusObjectiveLocation.x, m_focusObjectiveLocation.y, m_focusObjectiveLocation.z,
                                //     m_previousDistanceToFocusObjective);
                            } else {  m_hasFocusObjectiveLocation = false; }
						} else {
                            // UTIL_ServerPrintf("RCBot %s: Waypoint ID %d not found or not used for objective %s.\n", STRING(m_pEdict->v.netname), waypoint_id, objective_key.c_str());
                            m_hasFocusObjectiveLocation = false;
                        }
					} else { m_hasFocusObjectiveLocation = false; }
				} catch (const std::invalid_argument& ia) {
					// UTIL_ServerPrintf("RCBot %s: Invalid argument for waypoint ID in %s.\n", STRING(m_pEdict->v.netname), objective_key.c_str());
					m_hasFocusObjectiveLocation = false;
				} catch (const std::out_of_range& oor) {
					// UTIL_ServerPrintf("RCBot %s: Out of range for waypoint ID in %s.\n", STRING(m_pEdict->v.netname), objective_key.c_str());
					m_hasFocusObjectiveLocation = false;
				}
			} else {
				m_hasFocusObjectiveLocation = false; // Not a waypoint objective we can parse for location
			}
		}
	} else { // No objectives currently
        if (!m_currentFocusObjective.empty()){
            // UTIL_ServerPrintf("RCBot %s: No objectives, clearing focus: %s\n", STRING(m_pEdict->v.netname), m_currentFocusObjective.c_str());
            m_currentFocusObjective = "";
			m_hasFocusObjectiveLocation = false;
			m_previousDistanceToFocusObjective = -1.0f;
        }
    }

	// --- Reward Shaping Logic ---
	if (m_hasFocusObjectiveLocation && !m_currentFocusObjective.empty() && m_pEdict) {
		float currentDistance = (m_pEdict->v.origin - m_focusObjectiveLocation).Length();
		if (m_previousDistanceToFocusObjective > 0) { // Was set properly
			float distanceDelta = m_previousDistanceToFocusObjective - currentDistance;
			if (distanceDelta > SIGNIFICANT_PROGRESS_THRESHOLD) {
				float reward = distanceDelta * SHAPING_REWARD_MULTIPLIER;
				m_curiosityScore += reward; // Add to general curiosity for now
				// m_shapingRewardAccumulator += reward; // Or use an accumulator

				if (m_objectiveInterests.count(m_currentFocusObjective)) {
                     m_objectiveInterests[m_currentFocusObjective] += reward * INTEREST_BOOST_FROM_SHAPING_FACTOR;
                }
				// UTIL_ServerPrintf("Bot %s made progress (%.2f units) towards %s, reward %.3f. Curiosity: %.2f. Objective Interest: %.2f\n",
                //     STRING(m_pEdict->v.netname), distanceDelta, m_currentFocusObjective.c_str(), reward, m_curiosityScore,
                //     m_objectiveInterests.count(m_currentFocusObjective) ? m_objectiveInterests[m_currentFocusObjective] : 0.0f);
			}
		}
		m_previousDistanceToFocusObjective = currentDistance;
	}
	// --- End Reward Shaping ---

	// 4. Conceptual: How m_currentFocusObjective would influence behavior
	// This would be integrated into the bot's decision-making for tasks, movement, etc.
	// For example:
	// if (!m_currentFocusObjective.empty()) {
	//     if (m_currentFocusObjective.rfind("investigate_waypoint_", 0) == 0) {
	//         // Extract waypoint ID and set as a movement goal
	//         // std::string wp_id_str = m_currentFocusObjective.substr(std::string("investigate_waypoint_").length());
	//         // int wp_id = std::stoi(wp_id_str);
	//         // PathNode* targetNode = gRCBotNavigatorNodes->GetNode(wp_id);
	//         // if (targetNode) setMoveTo(targetNode->v_origin, /* priority */);
	//     } else if (m_currentFocusObjective.rfind("investigate_", 0) == 0) {
	//         // Logic to find and move towards entities of a certain class, or a specific known new entity.
	//     }
	//		// else if (m_currentFocusObjective == "main_mission_objective") { ... }
	// } else {
	//     // Default behavior if no specific focus, e.g., patrol, standard combat.
	// }
	// --- End Objective Interest System ---

	// --- Player Perception Decay ---
	m_perceivedPlayerAggression = m_perceivedPlayerAggression * PERCEPTION_DECAY_RATE + PERCEPTION_BASELINE * (1.0f - PERCEPTION_DECAY_RATE);
	m_perceivedPlayerCooperation = m_perceivedPlayerCooperation * PERCEPTION_DECAY_RATE + PERCEPTION_BASELINE * (1.0f - PERCEPTION_DECAY_RATE);
	// Clamp to ensure they stay within [0,1] after decay if necessary, though decay to baseline should handle this.
    m_perceivedPlayerAggression = std::max(0.0f, std::min(1.0f, m_perceivedPlayerAggression));
    m_perceivedPlayerCooperation = std::max(0.0f, std::min(1.0f, m_perceivedPlayerCooperation));


	// Placeholder for opportunistic chat
	// This is a very simple trigger, e.g., a small chance per Think cycle.
	// A more sophisticated system would tie chats to specific game events (kills, deaths, objectives, etc.)
	if (RANDOM_LONG(0, 2000) < 2 && isAlive()) { // Reduced chance: 0.1% per Think frame
		// For now, use a generic context. This would be more specific in a real system.
		// sayChat("generic_event");
	}

    // Periodically evaluate persona
    m_timeSinceLastPersonaEvaluation += gpGlobals->frametime;
    if (m_timeSinceLastPersonaEvaluation > 5.0f) { // Evaluate every 5 seconds
        evaluateAndAdjustPersona();
        m_timeSinceLastPersonaEvaluation = 0.0f;
    }

	// --- Behavioral Adjustment Placeholders ---
	// if (m_lastInteractingPlayerEdict.Get() && m_perceivedPlayerAggression > 0.7f) {
	//     // TODO: Increase likelihood of targeting m_lastInteractingPlayerEdict.Get()
	//     // TODO: Maybe use more aggressive chat responses if talking about this player
	//		   UTIL_ServerPrintf("Bot %s is feeling aggressive towards %s!\n", STRING(m_pEdict->v.netname), STRING(m_lastInteractingPlayerEdict.Get()->v.netname));
	// }
	// if (m_perceivedPlayerCooperation > 0.7f) {
	//     // TODO: Increase likelihood of following/supporting players
	//     // TODO: Bias towards more positive/supportive chat
	//     if(m_lastInteractingPlayerEdict.Get())
	//		   UTIL_ServerPrintf("Bot %s is feeling cooperative towards %s!\n", STRING(m_pEdict->v.netname), STRING(m_lastInteractingPlayerEdict.Get()->v.netname));
	//     else
	//         UTIL_ServerPrintf("Bot %s is feeling generally cooperative!\n", STRING(m_pEdict->v.netname));
	// }


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

void RCBotBase::recordGameEvent(const GameEvent& event) {
    m_shortTermMemory.addEvent(event);

    // Also add to the unified chat context memory
    // Apply filtering here if only certain game events are relevant for chat context
    ContextualItem contextItem(event);
    m_chatContextMemory.addItem(contextItem);
}

// Example of where to call recordGameEvent:
// void RCBotBase::OnTakeDamage(float damageAmount, edict_t* attacker) {
//     // Assuming GameEvent has a constructor for damage events:
//     // GameEvent event(DAMAGE_EVENT, gpGlobals->time, damageAmount);
//     // recordGameEvent(event);
//     // ... other damage handling
// }

// void RCBotBase::OnHearSound(Vector soundOrigin, float volume) {
//     // Assuming GameEvent has a constructor for sound events:
//     // GameEvent event(HEAR_SOUND_EVENT, gpGlobals->time, soundOrigin, volume);
//     // recordGameEvent(event);
//     // ... other sound handling
// }

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

void RCBotBase::setLongTermMemory(RCBotLongTermMemory* ltm) {
    m_pLongTermMemory = ltm;
}

void RCBotBase::consultLongTermMemory() {
    if (m_pLongTermMemory) {
        // Example: Retrieve episodes for the current map and print some info
        // In a real scenario, this would be more targeted based on bot's needs.
        std::string currentMap = STRING(gpGlobals->mapname);
        // UTIL_LogPrintf("RCBot %s: Consulting LTM for map %s\n", STRING(m_pEdict->v.netname), currentMap.c_str());

        std::vector<Episode> episodes = m_pLongTermMemory->retrieveEpisodes(currentMap);

        // UTIL_LogPrintf("RCBot %s: Found %d episodes for map %s.\n", STRING(m_pEdict->v.netname), episodes.size(), currentMap.c_str());
        // For now, just a console print if possible, or internal state change.
        // Using fprintf for basic debug output here as UTIL_LogPrintf might not be set up or available at all levels.
        fprintf(stdout, "RCBot %s: Found %lu episodes for map %s.\n", STRING(m_pEdict->v.netname), episodes.size(), currentMap.c_str());
        for(const auto& ep : episodes) {
            fprintf(stdout, "  - Episode timestamp: %ld, outcome: %s, events: %lu\n", ep.metadata.timestamp, ep.metadata.outcome.c_str(), ep.events.size());
        }
    } else {
        // UTIL_LogPrintf("RCBot %s: LongTermMemory not available.\n", STRING(m_pEdict->v.netname));
        fprintf(stdout, "RCBot %s: LongTermMemory not available.\n", STRING(m_pEdict->v.netname));
    }
}

/*
Placeholder for event recording within Think():

void RCBotBase::Think()
{
    // ... existing Think() logic ...

    // Example: Check for damage (this is highly game-specific)
    // if (m_pEdict->v.health < m_previousHealth) {
    //     float damageTaken = m_previousHealth - m_pEdict->v.health;
    //     // Need to identify attacker if possible, or pass nullptr/world
    //     GameEvent dmgEvent(DAMAGE_EVENT, gpGlobals->time, damageTaken);
    //     recordGameEvent(dmgEvent);
    //     m_previousHealth = m_pEdict->v.health;
    // }

    // Example: Process heard sounds (requires sound detection system)
    // if (hasHeardSound()) {
    //     SoundInfo sound = getLatestSound(); // Hypothetical
    //     GameEvent soundEvent(HEAR_SOUND_EVENT, gpGlobals->time, sound.origin, sound.volume);
    //     recordGameEvent(soundEvent);
    // }

    // ... rest of Think() logic ...
}
*/

// --- Macro Action System Methods ---

void RCBotBase::loadMacroActions() {
    m_macroActions.clear(); // Clear any existing macros

    // Example: "strafe_jump_left"
    // This macro assumes that holding a movement key and jump for one frame is enough.
    // In reality, jump might need to be held until off the ground, or timed with physics.
    RCBotMacroAction strafeJumpLeft("strafe_jump_left");
    strafeJumpLeft.addStep(MacroStep(PRESS_KEY, IN_MOVELEFT)); // Press and hold moveleft
    strafeJumpLeft.addStep(MacroStep(PRESS_KEY, IN_JUMP));    // Press and hold jump
    strafeJumpLeft.addStep(MacroStep(WAIT_DURATION, 0.1f));   // Wait a short moment (e.g. for jump to initiate)
    // Note: RELEASE_KEY steps are conceptual. `RCBotBase::Think` clears buttons each frame.
    // If a key needs to be "held" across multiple `Think` frames by a macro, the macro's `update`
    // would need to re-assert `pressButton()` for that key in each of its `update` calls for the duration it's held.
    // The current `MacroStep` `PRESS_KEY` is treated as "press for this frame".
    // `RELEASE_KEY` is effectively a no-op unless `RCBotBase` button logic changes.
    // For simplicity now, assume buttons are cleared by main Think() unless macro re-presses.
    m_macroActions[strafeJumpLeft.m_name] = strafeJumpLeft;


    // Example: "peek_cover_right_quick"
    // This relies on MOVE_RIGHT_DURATION to continually press IN_MOVERIGHT.
    RCBotMacroAction peekRight("peek_cover_right_quick");
    peekRight.addStep(MacroStep(MOVE_RIGHT_DURATION, 0.3f)); // Move right for 0.3s
    // After 0.3s, the step completes. IN_MOVERIGHT will not be pressed by this macro anymore.
    m_macroActions[peekRight.m_name] = peekRight;


    // Example: "short_forward_burst"
    RCBotMacroAction forwardBurst("short_forward_burst");
    forwardBurst.addStep(MacroStep(MOVE_FORWARD_DURATION, 0.5f));
    m_macroActions[forwardBurst.m_name] = forwardBurst;


    // Example: "aim_up_briefly"
    // This will set the lookAt demand for one frame via SET_AIM_DIRECTION.
    // Then wait. Regular aiming will take over after the macro finishes.
    RCBotMacroAction aimUp("aim_up_briefly");
    aimUp.addStep(MacroStep(SET_AIM_DIRECTION, Vector(0,0,1))); // Vector for looking straight up (world Z axis)
    aimUp.addStep(MacroStep(WAIT_DURATION, 0.2f));
    m_macroActions[aimUp.m_name] = aimUp;

    // SERVER_PRINT("RCBot: Loaded %lu macro actions.\n", m_macroActions.size());
}

void RCBotBase::startMacroAction(const std::string& name) {
    auto it = m_macroActions.find(name);
    if (it != m_macroActions.end()) {
        if (m_currentMacroAction != nullptr && m_currentMacroAction->m_name != name) {
            // SERVER_PRINT("RCBot %s: Interrupting macro '%s' to start '%s'.\n", STRING(m_pEdict->v.netname),m_currentMacroAction->m_name.c_str(), name.c_str());
        } else if (m_currentMacroAction != nullptr && m_currentMacroAction->m_name == name && !m_currentMacroAction->isFinished()) {
            // SERVER_PRINT("RCBot %s: Macro '%s' already running, restarting.\n", STRING(m_pEdict->v.netname), name.c_str());
        }

        m_currentMacroAction = &(it->second);
        m_currentMacroAction->start();
        // SERVER_PRINT("RCBot %s: Starting macro action '%s'.\n", STRING(m_pEdict->v.netname), name.c_str());
    } else {
        // SERVER_PRINT("RCBot %s: Macro action '%s' not found.\n", STRING(m_pEdict->v.netname), name.c_str());
    }
}

void RCBotBase::stopCurrentMacroAction() {
    if (m_currentMacroAction) {
        // SERVER_PRINT("RCBot %s: Stopping macro action '%s'.\n", STRING(m_pEdict->v.netname), m_currentMacroAction->m_name.c_str());
        m_currentMacroAction = nullptr;
    }
}

// --- Persona Methods ---
void RCBotBase::setPersona(BotPersona persona) {
    m_persona = persona;
}

BotPersona RCBotBase::getPersona() const {
    return m_persona;
}

// --- Chat Methods ---
void RCBotBase::sayChat(const std::string& context_trigger) {
    if (!isAlive() || !m_pEdict) { // Don't chat if not alive or no edict
        return;
    }

    TaggedChatMessage chatMessage = g_ChatManager.generateBotChat(this, context_trigger, &m_chatContextMemory);

    if (!chatMessage.message.empty()) {
        // Format: "BotName: Message"
        // Note: UTIL_SayTextAll prepends [DEAD] if appropriate and handles team chat if the message starts with '(team) '
        // For general bot chat, we probably don't want team chat by default unless specified by persona/context.

        // Construct the full message string with bot name.
        // Max message length in GoldSrc is around 127 chars, but varies.
        // Bot names can be long. Keep chat messages themselves concise.
        char full_message[256]; // Buffer for full message
        snprintf(full_message, sizeof(full_message), "%s: %s", STRING(m_pEdict->v.netname), chatMessage.message.c_str());

        // Ensure null termination if message was truncated
        full_message[sizeof(full_message) - 1] = '\0';

        UTIL_SayTextAll(full_message, m_pEdict); // Pass player edict to attribute message correctly

        // Log the chat with its metadata (optional)
        // e.g., fprintf(stdout, "CHAT_LOG: [%s] %s (Persona: %d, Sentiment: %d, Time: %.2f)\n",
        //              STRING(m_pEdict->v.netname),
        //              chatMessage.message.c_str(),
        //              chatMessage.persona_at_time_of_sending,
        //              chatMessage.sentiment,
        //              chatMessage.timestamp);

        // Store the sent message in bot's own history
        m_sentChatMessages.push_back(chatMessage);
        if (m_sentChatMessages.size() > MAX_SENT_CHAT_HISTORY) {
            m_sentChatMessages.pop_front();
        }

        // Add bot's own chat to its context memory
        ContextualItem contextItem(chatMessage);
        m_chatContextMemory.addItem(contextItem);

        // If it was a taunt, record the time
        if (chatMessage.sentiment == SENTIMENT_TAUNT) {
            m_lastTauntTime = gpGlobals->time;
            m_damageTakenPostTaunt = 0; // Reset damage counter for this new taunt
        }
    }
}

void RCBotBase::evaluateAndAdjustPersona() {
    // This is a placeholder for more sophisticated logic.
    // For now, it just provides a hook and a very simple example.

    // Example conceptual logic (currently non-functional without actual metric updates):
    // if (m_persona == PERSONA_TRASH_TALKER && m_damageTakenPostTaunt > 50) {
    //    // If trash talking and then taking significant damage, maybe tone it down.
    //    setPersona(PERSONA_NEUTRAL);
    //    // SERVER_PRINT("RCBot %s: Persona changed to NEUTRAL due to post-taunt damage.\n", STRING(m_pEdict->v.netname));
    //    m_damageTakenPostTaunt = 0; // Reset metric
    //    return;
    // }

    // if (m_engagementScore < -5.0f) { // Example: if engagement is very low
    //    BotPersona newPersona = static_cast<BotPersona>(RANDOM_LONG(0, PERSONA_MAX_PERSONAS - 1));
    //    setPersona(newPersona);
    //    m_engagementScore = 0; // Reset score
    //    // SERVER_PRINT("RCBot %s: Low engagement, trying new persona: %d\n", STRING(m_pEdict->v.netname), newPersona);
    //    return;
    // }

    // Super simple placeholder: Cycle persona every few evaluations if no other logic changes it.
    // This is just to demonstrate the mechanism is called.
    if (RANDOM_LONG(0, 10) < 2) { // 20% chance to cycle persona during an evaluation
        BotPersona currentPersona = getPersona();
        BotPersona nextPersona = static_cast<BotPersona>((currentPersona + 1) % PERSONA_MAX_PERSONAS);
        if (nextPersona == currentPersona && PERSONA_MAX_PERSONAS > 1) { // Ensure it actually changes if possible
             nextPersona = static_cast<BotPersona>((currentPersona + 2) % PERSONA_MAX_PERSONAS);
        }
        if (nextPersona >= PERSONA_MAX_PERSONAS) nextPersona = PERSONA_NEUTRAL; // Safety for modulo with single persona

        setPersona(nextPersona);
        // SERVER_PRINT("RCBot %s: Periodically changed persona to %d.\n", STRING(m_pEdict->v.netname), nextPersona);
    }
}

void RCBotBase::updatePerceptionFromPlayerChat(edict_t* pPlayerEdict, float chat_sentiment_score) {
    if (!pPlayerEdict || !m_pEdict) return;

    m_lastInteractingPlayerEdict.Set(pPlayerEdict);

    if (chat_sentiment_score < -0.1f) { // Negative chat
        // chat_sentiment_score is negative, so subtracting it increases aggression
        m_perceivedPlayerAggression -= chat_sentiment_score * SENTIMENT_TO_AGGRESSION_FACTOR;
        // Adding a negative score decreases cooperation
        m_perceivedPlayerCooperation += chat_sentiment_score * SENTIMENT_TO_COOPERATION_FACTOR;
    } else if (chat_sentiment_score > 0.1f) { // Positive chat
        m_perceivedPlayerCooperation += chat_sentiment_score * SENTIMENT_TO_COOPERATION_FACTOR;
        // Positive score decreases perceived aggression
        m_perceivedPlayerAggression -= chat_sentiment_score * SENTIMENT_TO_AGGRESSION_FACTOR;
    }

    // Clamp values to [0.0, 1.0]
    m_perceivedPlayerAggression = std::max(0.0f, std::min(1.0f, m_perceivedPlayerAggression));
    m_perceivedPlayerCooperation = std::max(0.0f, std::min(1.0f, m_perceivedPlayerCooperation));

    UTIL_ServerPrintf("Bot %s perception of player %s (after chat score %.2f): Aggro=%.2f, Coop=%.2f\n",
        STRING(m_pEdict->v.netname),
        STRING(pPlayerEdict->v.netname),
        chat_sentiment_score,
        m_perceivedPlayerAggression,
        m_perceivedPlayerCooperation);
}