#include "rcbot_manager.h"
#include "rcbot_profile.h"
#include "rcbot_mod.h"
#include "extdll.h"
#include "h_export_meta.h"
#include "meta_api.h"
#include "rcbot_engine_funcs.h"
#include "rcbot_utils.h"
#include "rcbot_navigator.h"
#include "rcbot_chat_manager.h" // For g_ChatManager
#include "rcbot_dynamic_objectives.h" // For g_ObjectiveManager

// Constants for ProcessPlayerDeathEvent
static const float OBJECTIVE_CONF_PENALTY_ON_DEATH_WHILE_PURSUING = -0.1f; // Retained for potential direct use, though TD is primary
static const float OBJECTIVE_CONF_BONUS_ON_KILL_NEAR_OBJECTIVE = 0.05f;  // Retained for potential direct use
static const float OBJECTIVE_PROXIMITY_FOR_RELEVANCE = 300.0f; // Units for "near objective"

// Constants for round end rewards related to TD learning for objectives
static const float REWARD_VALUE_ROUND_WIN = 1.0f;
static const float PENALTY_VALUE_ROUND_LOSS = -1.0f;
static const float REWARD_VALUE_ROUND_DRAW = 0.0f;

/// <summary>
/// 
/// </summary>
RCBotManager gRCBotManager;
/// <summary>
/// 
/// </summary>
extern globalvars_t* gpGlobals;
/// <summary>
/// 
/// </summary>
RCBotManager::~RCBotManager()
{
	for (auto* pBot : m_Bots)
	{
		delete pBot;
	}

	m_Bots.clear();
}
/// <summary>
/// 
/// </summary>
RCBotManager::RCBotManager()
{
	m_iQuota = 0;
	m_fAddRemoveBotTime = 0.0f;
	m_fNodeDrawTime = 0.0f;
    m_timeSinceLastObjectiveDecay = 0.0f;
	// m_longTermMemory is implicitly default-constructed
}
/// <summary>
/// 
/// </summary>
void RCBotManager::Think()
{
	for ( auto pBot : m_Bots )
	{
		pBot->Think();
		pBot->RunPlayerMove();
	}

	if (m_fAddRemoveBotTime < gpGlobals->time)
	{
		m_fAddRemoveBotTime = gpGlobals->time + BOT_MANAGER_DEFAULT_ADD_REMOVE_BOT_PERIOD;

		if (m_Bots.size() > m_iQuota)
		{
			KickBot();
		}
		else if (m_Bots.size() < m_iQuota)
		{
			RCBotBase * pBot = AddBot();

			if (pBot != nullptr)
			{
				m_Bots.push_back(pBot);
			}
			else 
			{
				if ( m_iQuota > 0 )
					m_iQuota--; // decrease Quota 
			}
		}
	}

	gRCBotNavigatorNodes->gameFrame();

    // Simulate Round End for testing objective outcome recording
    // static float s_timeSinceLastRoundEndSim = 0.0f; // Needs to be member or proper static if used long-term
    // if (gpGlobals) { // Ensure gpGlobals is valid
    //     s_timeSinceLastRoundEndSim += gpGlobals->frametime;
    //     if (s_timeSinceLastRoundEndSim > 60.0f) { // Simulate round end every 60 seconds
    //         int winning_team = (RANDOM_LONG(0,100) < 45) ? 1 : ((RANDOM_LONG(0,100) < 50) ? 2 : 0); // Random winner (more chance for team 1/2, then draw)
    //         OnRoundEnd_Simulated(winning_team);
    //         s_timeSinceLastRoundEndSim = 0.0f;
    //     }
    // }

    // Periodically decay dynamic objectives
    if (gpGlobals) { // Ensure gpGlobals is valid
        m_timeSinceLastObjectiveDecay += gpGlobals->frametime;
        if (m_timeSinceLastObjectiveDecay >= OBJECTIVE_DECAY_INTERVAL) {
            g_ObjectiveManager.decayAndUpdateObjectives(gpGlobals->time);
            m_timeSinceLastObjectiveDecay = 0.0f;
        }
    }

    // Simulate Player Death Event for testing
    static float s_timeSinceLastDeathSim = 0.0f;
    if (gpGlobals) { // Ensure gpGlobals is valid
        s_timeSinceLastDeathSim += gpGlobals->frametime;
        if (s_timeSinceLastDeathSim > 20.0f && m_Bots.size() >= 1) { // Simulate a death every 20s if at least one bot
            edict_t* pVictim = nullptr;
            edict_t* pAttacker = nullptr;

            if (m_Bots.size() >= 2) { // Prefer bot vs bot if possible
                 pVictim = m_Bots[RAND_LONG(0, m_Bots.size()-1)]->getEdict();
                 pAttacker = m_Bots[RAND_LONG(0, m_Bots.size()-1)]->getEdict();
                 if (pVictim == pAttacker && m_Bots.size() > 1) { // Ensure attacker is different from victim
                     int attacker_idx = RAND_LONG(0, m_Bots.size()-1);
                     int victim_idx = (attacker_idx + 1) % m_Bots.size(); // Simple way to get a different index
                     pAttacker = m_Bots[attacker_idx]->getEdict();
                     pVictim = m_Bots[victim_idx]->getEdict();
                 } else if (pVictim == pAttacker && m_Bots.size() == 1) {
                     // If only one bot, simulate attacker as world (e.g. suicide, environment)
                     pAttacker = gpGlobals->pEdictWorld;
                 }
            } else if (m_Bots.size() == 1) { // Only one bot
                pVictim = m_Bots[0]->getEdict();
                pAttacker = gpGlobals->pEdictWorld; // Simulate world as attacker
            }


            if (pVictim && pAttacker) {
                 ProcessPlayerDeathEvent(pVictim, pAttacker);
            }
            s_timeSinceLastDeathSim = 0.0f;
        }
    }
}

void RCBotManager::OnRoundEnd_Simulated(int winning_team_id) {
    // UTIL_ServerPrintf("RCBotManager: Simulated Round End. Winning Team: %d\n", winning_team_id);
    const auto& active_bots = getActiveBots();

    for (RCBotBase* bot : active_bots) {
        if (!bot || !bot->getEdict() || bot->m_currentObjectiveFocusID.empty()) {
            continue;
        }

        ObjectiveCandidateMetadata* objective_data = g_ObjectiveManager.getObjectiveCandidateById(bot->m_currentObjectiveFocusID);
        // Ensure objective is still valid and active before applying update
        if (objective_data && objective_data->is_active) {
            float round_outcome_reward = REWARD_VALUE_ROUND_DRAW;
            bool bot_team_won_this_round = false; // For recordObjectiveInteractionOutcome

            if (winning_team_id != 0) { // If not a draw (0 often means draw or no winner)
                bot_team_won_this_round = (bot->getEdict()->v.team == winning_team_id);
                round_outcome_reward = bot_team_won_this_round ? REWARD_VALUE_ROUND_WIN : PENALTY_VALUE_ROUND_LOSS;
            }

            // This is a terminal transition for the objective pursuit within this round.
            // The value of the terminal state V(s') is 0 because the outcome is fully captured in 'round_outcome_reward'.
            g_ObjectiveManager.applyTDUpdate(
                bot->m_currentObjectiveFocusID,
                round_outcome_reward,
                "",    // No next_objective_id for a terminal round event
                0.0f,  // explicit_next_objective_value for terminal state is 0
                true   // is_terminal_transition = true
            );

            // Still call recordObjectiveInteractionOutcome to update interaction counters,
            // but it no longer directly modifies confidence.
            if (winning_team_id != 0) { // Only record win/loss if there was a winner
                 g_ObjectiveManager.recordObjectiveInteractionOutcome(bot->m_currentObjectiveFocusID, bot_team_won_this_round);
            }

            // UTIL_ServerPrintf("RCBotManager: Bot %s (Team %d) focused on %s. Round outcome reward: %.2f. TD update applied.\n",
            //     STRING(bot->getEdict()->v.netname), bot->getEdict()->v.team,
            //     bot->m_currentObjectiveFocusID.c_str(), round_outcome_reward);
        }

        // It's debatable whether to clear m_currentObjectiveFocusID here.
        // If objectives persist across rounds, the bot might continue.
        // If focus should reset, then: bot->m_currentObjectiveFocusID = "";
        // This is better handled in RCBotBase itself if it needs to react to round end.
    }

    // This call resets per-round stats for objectives, like interaction counts,
    // and marks them as active for re-evaluation in the new round.
    g_ObjectiveManager.clearObjectivesOnNewRound();
}


/// <summary>
/// 
/// </summary>
/// <param name="iQuota"></param>
/// <returns></returns>
bool RCBotManager::SetQuota(uint8_t iQuota)
{
	if (m_iQuota > gpGlobals->maxClients)
		return false;

	m_iQuota = iQuota;

	return true;
}
/// <summary>
/// RCBotManager :: AddBot
/// Add a bot to the server
/// </summary>
/// <returns>bot pointer</returns>
RCBotBase *RCBotManager::AddBot()
{
	RCBotProfile * profile = gRCBotProfiles->getRandomUnused();

	if (profile != nullptr)
	{
		edict_t *pBotEdict = (*g_engfuncs.pfnCreateFakeClient)(profile->getName());

		if ( !FNullEnt(pBotEdict) )
		{
			RCBotModification *pMod = gRCBotModifications.getCurrentMod();

			if (pMod == nullptr)
			{
				RCBotUtils::Message(nullptr, MessageErrorLevel::Information, "Unknown modification!");
				return nullptr;
			}

			RCBotBase *pBot = pMod->createBot();

			if (pBot != nullptr)
			{
				char ptr[128];  // allocate space for message from ClientConnect

				pBot->setEdict(pBotEdict);
				pBot->setProfile(profile);
				pBot->setLongTermMemory(&m_longTermMemory); // Set LTM for the bot
				pBot->setUpClientInfo();

				MDLL_ClientConnect(pBotEdict, nullptr, "127.0.0.1", ptr);
				ClientConnect(pBotEdict, nullptr, "127.0.0.1", ptr);

				MDLL_ClientPutInServer(pBotEdict);
				ClientPutInServer(pBotEdict);

				return pBot;
			}
		}
	}

	return nullptr;
}

const std::vector<RCBotBase*>& RCBotManager::getActiveBots() const {
    return m_Bots;
}

/// <summary>
/// 
/// </summary>
/// <param name="pEdict">bot edict</param>
/// <returns>bot pointer</returns>
RCBotBase* RCBotManager::getBotByEdict(edict_t* pEdict)
{
	for (auto pBot : m_Bots)
	{
		if (pBot->isEdict(pEdict))
			return pBot;
	}

	return nullptr;
}
/// <summary>
/// called on level change
/// </summary>
void RCBotManager::OnLevelChange()
{
	for (auto* pBot : m_Bots)
	{
		delete pBot;
	}

	m_Bots.clear();
}
/// <summary>
/// 
/// </summary>
void RCBotManager::KickBot()
{
	if (m_Bots.size() > 0)
	{
		RCBotBase* pBot = m_Bots[m_Bots.size() - 1];

		const char* szName = STRING(pBot->getEdict()->v.netname);

		char cmd[128];

		sprintf(cmd, "kick \"%s\"\n", szName);

		SERVER_COMMAND(cmd);

		m_Bots.erase(m_Bots.end());
	}
}
/// <summary>
/// 
/// </summary>
void RCBotManager::LevelInit()
{
	m_fAddRemoveBotTime = 0.0f;

	OnLevelChange();

	gRCBotNavigatorNodes->mapInit();

	// Placeholder: Archive a dummy episode at the start of a new level
	// In a real scenario, this would be at the *end* of an episode/round/match
	// and would collect actual data from the game and bots.
	if (true) { // Condition for when to archive (e.g., end of round)
		Episode dummyEpisode;
		dummyEpisode.metadata.mapName = STRING(gpGlobals->mapname);
		dummyEpisode.metadata.timestamp = static_cast<long>(gpGlobals->time);
		dummyEpisode.metadata.outcome = "pending"; // Or "map_start"
		dummyEpisode.metadata.gameCvars["sv_cheats"] = CVAR_GET_STRING("sv_cheats");
		dummyEpisode.metadata.gameCvars["mp_timelimit"] = CVAR_GET_STRING("mp_timelimit");

		// Add some dummy events
		// GameEvent(GameEventType type, float timestamp, float damageAmount)
		if (!m_Bots.empty()) { // Example: take some events from the first bot's STM if available
		    // This is just a conceptual placeholder.
		    // Actual event collection would be more sophisticated.
		    // dummyEpisode.events = m_Bots[0]->getShortTermMemory().getAllEvents(); // If such getter existed
		}
        GameEvent event1(DAMAGE_EVENT, gpGlobals->time - 10.0f, 25.0f);
        dummyEpisode.events.push_back(event1);
        GameEvent event2(HEAR_SOUND_EVENT, gpGlobals->time - 5.0f, 0.0f); // damageAmount not relevant for sound
        dummyEpisode.events.push_back(event2);


		m_longTermMemory.archiveEpisode(dummyEpisode);
	}

	// Initialize/Re-initialize chat models for the new level
	g_ChatManager.initializeChatModels();

    // Initialize Dynamic Objective System for the new map
    g_ObjectiveManager.clearAllObjectives(); // Clear objectives from previous map
    if (gpGlobals) { // Ensure gpGlobals is valid
        g_ObjectiveManager.setCurrentMapName(STRING(gpGlobals->mapname));
    }

    // Iterate through entities to discover initial set of objective candidates
    if (gpGlobals) {
        edict_t* pCurrentEntity = nullptr;
        for (int i = 1; i < gpGlobals->maxEntities; i++) { // Start from 1, 0 is worldspawn
            pCurrentEntity = INDEXENT(i);

            // Check if the edict is valid and not free/marked for deletion
            if (!pCurrentEntity || pCurrentEntity->free || (pCurrentEntity->v.flags & FL_KILLME)) {
                continue;
            }
            // Skip clients (players/bots) for this type of objective discovery for now,
            // unless specific player roles become objectives (e.g. VIP).
            if (pCurrentEntity->v.flags & (FL_CLIENT | FL_FAKECLIENT)) {
                continue;
            }

            // The discoverObjectiveCandidate method will use its internal list of interesting classnames.
            // It needs pEntity, location, classname, event_type, and optional team.
            // For entity iteration, location and classname are from pEntity.
            g_ObjectiveManager.discoverObjectiveCandidate(
                pCurrentEntity,
                pCurrentEntity->v.origin,
                STRING(pCurrentEntity->v.classname),
                "entity_iteration",
                pCurrentEntity->v.team // Pass entity's team if available
            );
        }
        UTIL_ServerPrintf("RCBotManager: Initial entity scan for dynamic objectives complete. Found %d candidates.\n",
            g_ObjectiveManager.getObjectiveCandidates().size());
    }

    // Perform initial clustering after discovering objectives
    if (g_ObjectiveManager.getObjectiveCandidates().size() > 0) { // Only cluster if there's something to cluster
        g_ObjectiveManager.clusterObjectives(5); // Example: 5 clusters
    }
}


void RCBotManager::ProcessPlayerDeathEvent(edict_t* pVictimEdict, edict_t* pAttackerEdict) {
    if (!pVictimEdict || !pAttackerEdict || !gpGlobals) return;

    // Scenario 1: An RCBot was the victim
    RCBotBase* victim_bot = getBotByEdict(pVictimEdict);
    if (victim_bot && victim_bot->getEdict() && !victim_bot->m_currentObjectiveFocusID.empty()) { // Check edict validity
        ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(victim_bot->m_currentObjectiveFocusID);
        if (obj_meta && obj_meta->is_active) {
            // Bot died while pursuing this objective - likely negative for this objective's perceived value/safety
            g_ObjectiveManager.recordObjectiveInteractionOutcome(victim_bot->m_currentObjectiveFocusID, false);
            // Using recordObjectiveInteractionOutcome which internally calls updateObjectiveConfidence based on its own logic.
            // Direct confidence update as an alternative:
            // g_ObjectiveManager.updateObjectiveConfidence(victim_bot->m_currentObjectiveFocusID, OBJECTIVE_CONF_PENALTY_ON_DEATH_WHILE_PURSUING);

            // UTIL_ServerPrintf("Bot %s died pursuing obj %s. Confidence potentially updated via interaction outcome.\n",
            //                   STRING(pVictimEdict->v.netname), victim_bot->m_currentObjectiveFocusID.c_str());
        }
    }

    // Scenario 2: An RCBot was the attacker AND the victim was an enemy
    RCBotBase* attacker_bot = getBotByEdict(pAttackerEdict);
    if (attacker_bot && attacker_bot->getEdict() && !attacker_bot->m_currentObjectiveFocusID.empty() && pVictimEdict != pAttackerEdict) { // Check edict validity
        // Simplified isEnemy check:
        bool victim_is_enemy = true;
        if (pVictimEdict->v.team != 0 && pAttackerEdict->v.team != 0 && pVictimEdict->v.team == pAttackerEdict->v.team) {
            victim_is_enemy = false; // Simple team check for non-FFA (assumes team 0 is spectator or general)
        }
        // A more robust check would be: victim_is_enemy = attacker_bot->isEnemy(pVictimEdict);
        // However, isEnemy might not be fully implemented or might be specific to the mod.

        if (victim_is_enemy) {
            ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(attacker_bot->m_currentObjectiveFocusID);
            if (obj_meta && obj_meta->is_active) {
                // Bot killed an enemy while pursuing an objective.
                // Check if the kill happened near the objective location.
                float distance_to_objective = (pVictimEdict->v.origin - obj_meta->location).Length();
                if (distance_to_objective < OBJECTIVE_PROXIMITY_FOR_RELEVANCE) {
                    // Kill was relevant to the objective
                    g_ObjectiveManager.recordObjectiveInteractionOutcome(attacker_bot->m_currentObjectiveFocusID, true);
                    // Direct confidence update as an alternative:
                    // g_ObjectiveManager.updateObjectiveConfidence(attacker_bot->m_currentObjectiveFocusID, OBJECTIVE_CONF_BONUS_ON_KILL_NEAR_OBJECTIVE);

                    // UTIL_ServerPrintf("Bot %s killed enemy near obj %s. Confidence potentially updated via interaction outcome.\n",
                    //                   STRING(pAttackerEdict->v.netname), attacker_bot->m_currentObjectiveFocusID.c_str());
                }
            }
        }
    }
}
