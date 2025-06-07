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
}

void RCBotManager::OnRoundEnd_Simulated(int winning_team_id) {
    UTIL_ServerPrintf("RCBotManager: Simulated Round End. Winning Team ID: %d\n", winning_team_id);
    const auto& active_bots = getActiveBots();

    for (RCBotBase* bot : active_bots) {
        if (!bot || !bot->getEdict() || bot->m_currentObjectiveFocusID.empty()) { // Accessing public member for now as per subtask plan
            continue;
        }

        ObjectiveCandidateMetadata* objective_data = g_ObjectiveManager.getObjectiveCandidateById(bot->m_currentObjectiveFocusID);
        if (objective_data) {
            bool bot_team_won_round = (bot->getEdict()->v.team == winning_team_id && winning_team_id != 0);
            bool positive_outcome_for_objective = false;

            // Determine if the objective itself was "achieved" based on team win.
            // This is a simplification. A real system would check objective-specific completion.
            if (objective_data->team_ownership == 0) { // Neutral objective (e.g., press a button)
                // If bot was focused on it, and its team won, consider it a positive interaction for now.
                // Or, if it's a general objective, any win might be positive.
                // This needs more game-specific logic. For now, let's assume neutral objectives
                // are positive if the bot's team wins the round while it was focused.
                positive_outcome_for_objective = bot_team_won_round;
            } else if (objective_data->team_ownership == bot->getEdict()->v.team) {
                // Bot was focused on an objective belonging to its own team.
                // If bot's team won, it's a positive outcome for pursuing/defending this objective.
                positive_outcome_for_objective = bot_team_won_round;
            } else {
                // Bot was focused on an objective belonging to the enemy team (e.g., attacking it).
                // If bot's team won, it's a positive outcome (they successfully overcame/captured it).
                positive_outcome_for_objective = bot_team_won_round;
            }

            UTIL_ServerPrintf("Bot %s (Team %d) was focused on objective %s (Team %d). Outcome recorded: %s\n",
                STRING(bot->getEdict()->v.netname), bot->getEdict()->v.team,
                bot->m_currentObjectiveFocusID.c_str(), objective_data->team_ownership,
                positive_outcome_for_objective ? "POSITIVE" : "NEGATIVE");

            g_ObjectiveManager.recordObjectiveInteractionOutcome(bot->m_currentObjectiveFocusID, positive_outcome_for_objective);
        }
        // Bot should clear its specific focus ID after round end.
        // The general m_currentFocusObjective (string description) might persist or be re-evaluated.
        // bot->m_currentObjectiveFocusID = ""; // This should be done in RCBotBase when it processes round end for itself
    }

    g_ObjectiveManager.clearObjectivesOnNewRound(); // Reset round-specific stats for all objectives
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
}
