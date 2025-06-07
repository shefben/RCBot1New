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

// Constants for Bomb Plant Event
static const float REWARD_BOMB_PLANTED_AT_SITE_TERRORIST = 0.7f;
static const float PENALTY_BOMB_PLANTED_AT_SITE_CT = -0.7f;

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

    // Initialize debug simulation flags
    m_debug_g_simulate_bomb_is_planted = false;
    m_debug_g_simulate_flag_is_loose_team1 = false;
    m_debug_g_simulate_flag_is_loose_team2 = false;

    // Initialize real game state tracking
    m_real_game_state_bomb_planted = false;
    m_real_bomb_planted_location = Vector(0,0,0);
    m_real_planted_bomb_entity = nullptr;
    m_was_bomb_planted_last_frame = false;
}
/// <summary>
/// 
/// </summary>
void RCBotManager::Think()
{
    // --- Real Bomb State Detection ---
    bool previous_bomb_state = m_real_game_state_bomb_planted;
    bool bomb_found_this_frame = false;
    edict_t* found_bomb_entity_this_frame = nullptr;
    // Don't reset m_real_bomb_planted_location here, keep last known if it disappears temporarily,
    // only update if a bomb is actively found. If not found, m_real_planted_bomb_entity will be null.

    if (gpGlobals) { // Ensure gpGlobals is valid
        edict_t* pCurrentEntity = nullptr;
        for (int i = 1; i < gpGlobals->maxEntities; i++) {
            pCurrentEntity = INDEXENT(i);
            if (!pCurrentEntity || pCurrentEntity->free || (pCurrentEntity->v.flags & FL_KILLME)) {
                continue;
            }

            const char* classname = STRING(pCurrentEntity->v.classname);
            const char* modelname = STRING(pCurrentEntity->v.model);
            bool is_potential_c4 = false;

            if (strcmp(classname, "grenade") == 0 && modelname && strstr(modelname, "c4.mdl") != nullptr) {
                is_potential_c4 = true;
            } else if (strcmp(classname, "planted_c4") == 0 || strcmp(classname, "armoury_entity_c4_bomb") == 0 ) {
                is_potential_c4 = true;
            }

            if (is_potential_c4) {
                bool actually_planted = false;
                if (strcmp(classname, "planted_c4") == 0 || strcmp(classname, "armoury_entity_c4_bomb") == 0) {
                    actually_planted = true;
                } else if (strcmp(classname, "grenade") == 0 && modelname && strstr(modelname, "c4.mdl") != nullptr) {
                    // Check for EF_BRIGHTLIGHT (blinking light on CS C4)
                    if (pCurrentEntity->v.effects & EF_BRIGHTLIGHT) {
                        actually_planted = true;
                    }
                }

                if (actually_planted) {
                    bomb_found_this_frame = true;
                    m_real_bomb_planted_location = pCurrentEntity->v.origin;
                    found_bomb_entity_this_frame = pCurrentEntity;
                    if (!previous_bomb_state) {
                    //    UTIL_ServerPrintf("RCBotManager: Real bomb detected as PLANTED at (%.0f, %.0f, %.0f)\n",
                    //                      m_real_bomb_planted_location.x, m_real_bomb_planted_location.y, m_real_bomb_planted_location.z);
                    }
                    break;
                }
            }
        }
    }
    m_real_game_state_bomb_planted = bomb_found_this_frame;
    m_real_planted_bomb_entity = found_bomb_entity_this_frame; // Update the member edict_t*

    // if (previous_bomb_state && !m_real_game_state_bomb_planted) {
    //    UTIL_ServerPrintf("RCBotManager: Real bomb is no longer detected (defused/exploded).\n");
    // }

    // --- Bomb Plant Event Detection & TD Update ---
    if (m_real_game_state_bomb_planted && !m_was_bomb_planted_last_frame) {
        // Bomb was just planted in this frame!
        // UTIL_ServerPrintf("RCBotManager: Detected REAL bomb plant event.\n");

        if (m_real_planted_bomb_entity) {
            std::string bomb_site_obj_id_found = "";
            const auto& objectives = g_ObjectiveManager.getObjectiveCandidates();
            for (const auto& pair : objectives) {
                const ObjectiveCandidateMetadata& obj_meta = pair.second;
                if (obj_meta.category_tag == ObjectiveCategoryType::BOMB_SITE && obj_meta.is_active) {
                    if ((obj_meta.location - m_real_bomb_planted_location).LengthSquared() < (50.0f * 50.0f)) { // 50 units proximity
                        bomb_site_obj_id_found = obj_meta.unique_id;
                        break;
                    }
                }
            }

            if (!bomb_site_obj_id_found.empty()) {
                // UTIL_ServerPrintf("RCBotManager: Real bomb plant corresponds to objective ID %s\n", bomb_site_obj_id_found.c_str());

                int planter_team_id = 1; // Default assumption: Terrorist (team 1) planted
                edict_t* planter_edict = m_real_planted_bomb_entity->v.owner;
                if (planter_edict && ENTINDEX(planter_edict) > 0 && ENTINDEX(planter_edict) <= gpGlobals->maxClients) {
                     planter_team_id = planter_edict->v.team;
                }
                // If v.owner is not reliable, one might need to infer planter from game events or C4 entity's own team field if set.

                float site_update_reward = 0.0f;
                if (planter_team_id == 1) site_update_reward = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;

                if (site_update_reward != 0.0f) { // Only apply if there's a relevant reward (e.g. T planted)
                     g_ObjectiveManager.applyTDUpdate(bomb_site_obj_id_found, site_update_reward, "", 0.0f, false);
                }

                const auto& active_bots = getActiveBots();
                for (RCBotBase* bot : active_bots) {
                    if (bot && bot->getEdict() && bot->m_currentObjectiveFocusID == bomb_site_obj_id_found) {
                        float bot_specific_reward = 0.0f;
                        if (bot->getEdict()->v.team == planter_team_id) {
                            bot_specific_reward = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
                        } else {
                            bot_specific_reward = PENALTY_BOMB_PLANTED_AT_SITE_CT;
                        }
                        g_ObjectiveManager.applyTDUpdate(bot->m_currentObjectiveFocusID, bot_specific_reward, "", 0.0f, false);
                    }
                }
            } else {
                // UTIL_ServerPrintf("RCBotManager: Real bomb plant at (%.0f, %.0f, %.0f) did not match any known BOMB_SITE objective.\n",
                //    m_real_bomb_planted_location.x, m_real_bomb_planted_location.y, m_real_bomb_planted_location.z);
                // Consider creating a new dynamic objective here if it's a valid plant location not yet known.
                // g_ObjectiveManager.discoverObjectiveCandidate(m_real_planted_bomb_entity, m_real_bomb_planted_location, "bombsite_discovered_by_plant", "real_event_discovery", 0); // Team 0 for neutral site
            }
        }
    }
    m_was_bomb_planted_last_frame = m_real_game_state_bomb_planted; // Update for next frame
    // --- End Bomb Plant Event Detection & TD Update ---

    // --- End Real Bomb State Detection --- // This comment seems misplaced, should be after the first block. The new block is self-contained.

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

    // Simulate Bomb Plant Event for testing
    static float s_timeSinceLastBombPlantSim = 0.0f;
    if (gpGlobals) { // Ensure gpGlobals is valid
        s_timeSinceLastBombPlantSim += gpGlobals->frametime;
        if (s_timeSinceLastBombPlantSim > 75.0f && !m_Bots.empty()) {
            RCBotBase* planter_bot = m_Bots[RAND_LONG(0, m_Bots.size()-1)];
            if (planter_bot && planter_bot->getEdict()) {
                const auto& objectives = g_ObjectiveManager.getObjectiveCandidates();
                edict_t* target_site_edict = nullptr;

                for(const auto& pair : objectives) {
                    if(pair.second.category_tag == ObjectiveCategoryType::BOMB_SITE && pair.second.is_active) {
                        // Try to find an actual entity matching this objective for the simulation
                        for (int i = 1; i < gpGlobals->maxEntities; i++) {
                            edict_t* pCurrentEntity = INDEXENT(i);
                            if (pCurrentEntity && !pCurrentEntity->free &&
                                STRING(pCurrentEntity->v.classname) == pair.second.entity_classname &&
                                (pCurrentEntity->v.origin - pair.second.location).Length() < 10.0f) {
                                target_site_edict = pCurrentEntity;
                                break;
                            }
                        }
                        if (target_site_edict) break;
                    }
                }

                if (target_site_edict) {
                    // Simulate planter is Terrorist (team 1) for this example
                    int old_team = planter_bot->getEdict()->v.team;
                    planter_bot->getEdict()->v.team = 1;
                    SimulateBombPlantedEvent(target_site_edict, planter_bot->getEdict());
                    planter_bot->getEdict()->v.team = old_team; // Restore team
                }
            }
            s_timeSinceLastBombPlantSim = 0.0f;
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

    // Update win/loss correlation counters for all objectives active this round
    if (winning_team_id != 0) { // If not a draw, proceed to update counters
        for (auto& pair : g_ObjectiveManager.getMutableObjectiveCandidates()) {
            ObjectiveCandidateMetadata& objective = pair.second;
            // Consider an objective "active in round" if its is_active is true.
            // is_active is reset by clearObjectivesOnNewRound after this block,
            // and updated by decay logic during the round.
            if (objective.is_active) {
                // Simplified assumption: winning_team_id 1 is a "win" context, 2 is a "loss" context
                // from a fixed reference (e.g., if we are tracking stats for "Team 1 objectives")
                if (winning_team_id == 1) {
                    objective.rounds_active_in_win++;
                } else if (winning_team_id == 2) {
                    objective.rounds_active_in_loss++;
                }
            }
        }
    }

    // This call resets per-round stats for objectives, like interaction counts,
    // and marks them as active for re-evaluation in the new round.
    g_ObjectiveManager.clearObjectivesOnNewRound();
}


void RCBotManager::SimulateBombPlantedEvent(edict_t* pBombSiteObjectiveEdict, edict_t* pPlanterEdict) {
    if (!pBombSiteObjectiveEdict || !pPlanterEdict || !gpGlobals) {
        // UTIL_ServerPrintf("SimulateBombPlantedEvent: Invalid edict(s).\n");
        return;
    }

    std::string bomb_site_obj_id = g_ObjectiveManager.generateUniqueIDForEntity(pBombSiteObjectiveEdict);
    ObjectiveCandidateMetadata* bomb_site_meta = g_ObjectiveManager.getObjectiveCandidateById(bomb_site_obj_id);

    if (!bomb_site_meta || !bomb_site_meta->is_active || bomb_site_meta->category_tag != ObjectiveCategoryType::BOMB_SITE) {
        // UTIL_ServerPrintf("SimulateBombPlantedEvent: Objective %s is not an active bomb site.\n", bomb_site_obj_id.c_str());
        return;
    }

    // UTIL_ServerPrintf("SimulateBombPlantedEvent: Bomb planted at %s by player/bot %s (Team %d).\n",
    //                   bomb_site_obj_id.c_str(), STRING(pPlanterEdict->v.netname), pPlanterEdict->v.team);

    float reward_for_site_objective = 0.0f;
    int planter_team = pPlanterEdict->v.team; // Assuming team 1 is T, team 2 is CT for CS example

    if (planter_team == 1) { // Terrorist planted
        reward_for_site_objective = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
    } else {
        // If a CT "plants", it might mean they are defusing or something else.
        // For this specific "BombPlanted" event, it's strongly implied T action.
        // If CTs were to secure a site *before* planting, that'd be a different event.
        // So, if planter_team is CT for a "BombPlanted" event, this is unusual.
        // We might give a slight negative or zero, or assume it's a T action regardless of current edict team for simulation.
        // For this simulation, we'll stick to the idea that this event means T planted.
        // If pPlanterEdict->v.team was not 1, this reward is effectively ignored or could be negative.
        // For a more robust system, the event source would clarify the true action.
        // Let's assume the simulation sets planter_team correctly for T.
        reward_for_site_objective = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
    }

    // Apply TD update to the bomb site objective itself.
    // This event (bomb planted) changes the state and value of the bomb site.
    // The "next state" for the bomb site objective is now "bomb ticking".
    // We don't have a V(s') for "bomb ticking" yet, so use 0 as explicit next state value for now,
    // meaning the reward_for_site_objective captures the full value change for this event.
    // A more advanced model might have V(bomb_ticking_site).
    g_ObjectiveManager.applyTDUpdate(bomb_site_obj_id, reward_for_site_objective, "", 0.0f, false);


    // Now, consider if any BOT was focused on this bomb site as an objective.
    const auto& active_bots = getActiveBots();
    for (RCBotBase* bot : active_bots) {
        if (bot && bot->getEdict() && !bot->m_currentObjectiveFocusID.empty() && bot->m_currentObjectiveFocusID == bomb_site_obj_id) {
            float bot_specific_reward = 0.0f;
            if (bot->getEdict()->v.team == planter_team) {
                bot_specific_reward = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
            } else {
                bot_specific_reward = PENALTY_BOMB_PLANTED_AT_SITE_CT;
            }
            // This is the reward for the bot's interaction (or failure to prevent interaction) with the objective.
            // The next state for the bot could be "defend planted bomb" or "retake site".
            // For simplicity, we use an explicit next state value of 0, assuming this reward captures the immediate outcome
            // of their focus on "plant/prevent plant at this site".
            g_ObjectiveManager.applyTDUpdate(bot->m_currentObjectiveFocusID, bot_specific_reward, "", 0.0f, false);
            // UTIL_ServerPrintf("Bot %s (Team %d) focused on %s. Bomb plant event. Bot reward: %.2f\n",
            //                   STRING(bot->getEdict()->v.netname), bot->getEdict()->v.team, bot->m_currentObjectiveFocusID.c_str(), bot_specific_reward);
        }
    }
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

    // Reset real game state tracking for new level
    m_real_game_state_bomb_planted = false;
    m_real_bomb_planted_location = Vector(0,0,0);
    m_real_planted_bomb_entity = nullptr;
    m_was_bomb_planted_last_frame = false;

    // Debug flags are typically set by commands, but can be reset here if desired
    // m_debug_g_simulate_bomb_is_planted = false;
    // m_debug_g_simulate_flag_is_loose_team1 = false;
    // m_debug_g_simulate_flag_is_loose_team2 = false;

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

    // After discovery and clustering, infer categories for all candidates
    g_ObjectiveManager.inferObjectiveCategories();
    // UTIL_ServerPrintf("RCBotManager: Objective category inference triggered after LevelInit discovery.\n");
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
