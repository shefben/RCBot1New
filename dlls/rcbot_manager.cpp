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
static const float OBJECTIVE_CONF_PENALTY_ON_DEATH_WHILE_PURSUING = -0.1f;
static const float OBJECTIVE_CONF_BONUS_ON_KILL_NEAR_OBJECTIVE = 0.05f;
static const float OBJECTIVE_PROXIMITY_FOR_RELEVANCE = 300.0f;

// Constants for round end rewards related to TD learning for objectives
static const float REWARD_VALUE_ROUND_WIN = 1.0f;
static const float PENALTY_VALUE_ROUND_LOSS = -1.0f;
static const float REWARD_VALUE_ROUND_DRAW = 0.0f;

// Constants for Bomb Plant Event
static const float REWARD_BOMB_PLANTED_AT_SITE_TERRORIST = 0.7f;
static const float PENALTY_BOMB_PLANTED_AT_SITE_CT = -0.7f;

RCBotManager gRCBotManager;
extern globalvars_t* gpGlobals;

RCBotManager::~RCBotManager()
{
	for (auto* pBot : m_Bots)
	{
		delete pBot;
	}
	m_Bots.clear();
}

RCBotManager::RCBotManager()
{
	m_iQuota = 0;
	m_fAddRemoveBotTime = 0.0f;
	m_fNodeDrawTime = 0.0f;
    m_timeSinceLastObjectiveDecay = 0.0f;

    m_debug_g_simulate_bomb_is_planted = false;
    m_debug_g_simulate_flag_is_loose_team1 = false;
    m_debug_g_simulate_flag_is_loose_team2 = false;

    m_real_game_state_bomb_planted = false;
    m_real_bomb_planted_location = Vector(0,0,0);
    m_real_planted_bomb_entity = nullptr;
    m_was_bomb_planted_last_frame = false;
}

void RCBotManager::Think()
{
    // --- Real Bomb State Detection ---
    bool bomb_found_this_frame = false;
    edict_t* found_bomb_entity_this_frame = nullptr;

    if (gpGlobals) {
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
                    if (pCurrentEntity->v.effects & EF_BRIGHTLIGHT) {
                        actually_planted = true;
                    }
                }

                if (actually_planted) {
                    bomb_found_this_frame = true;
                    m_real_bomb_planted_location = pCurrentEntity->v.origin;
                    found_bomb_entity_this_frame = pCurrentEntity;
                    break;
                }
            }
        }
    }
    m_real_game_state_bomb_planted = bomb_found_this_frame;
    m_real_planted_bomb_entity = found_bomb_entity_this_frame;
    // --- End of original Real Bomb State Detection part ---

    // --- Bomb Plant Event Detection & TD Update ---
    if (m_real_game_state_bomb_planted && !m_was_bomb_planted_last_frame) {
        if (m_real_planted_bomb_entity && gpGlobals) {
            std::string bomb_site_obj_id_found = "";
            const auto& objectives = g_ObjectiveManager.getObjectiveCandidates();
            for (const auto& pair : objectives) {
                const ObjectiveCandidateMetadata& obj_meta = pair.second;
                if (obj_meta.category_tag == ObjectiveCategoryType::BOMB_SITE && obj_meta.is_active) {
                    if ((obj_meta.location - m_real_bomb_planted_location).LengthSquared() < (50.0f * 50.0f)) {
                        bomb_site_obj_id_found = obj_meta.unique_id;
                        break;
                    }
                }
            }

            if (!bomb_site_obj_id_found.empty()) {
                int planter_team_id = 1;
                edict_t* planter_edict = m_real_planted_bomb_entity->v.owner;
                if (planter_edict && ENTINDEX(planter_edict) > 0 && ENTINDEX(planter_edict) <= gpGlobals->maxClients && planter_edict->v.team != 0 ) {
                     planter_team_id = planter_edict->v.team;
                } else if (m_real_planted_bomb_entity->v.team !=0) {
                     planter_team_id = m_real_planted_bomb_entity->v.team;
                }

                float site_update_reward = 0.0f;
                if (planter_team_id == 1) site_update_reward = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;

                if (site_update_reward != 0.0f) {
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
            }
        }
    }
    m_was_bomb_planted_last_frame = m_real_game_state_bomb_planted;
    // --- End Bomb Plant Event Detection & TD Update ---

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
					m_iQuota--;
			}
		}
	}

	gRCBotNavigatorNodes->gameFrame();

    if (gpGlobals) {
        m_timeSinceLastObjectiveDecay += gpGlobals->frametime;
        if (m_timeSinceLastObjectiveDecay >= OBJECTIVE_DECAY_INTERVAL) {
            g_ObjectiveManager.decayAndUpdateObjectives(gpGlobals->time);
            m_timeSinceLastObjectiveDecay = 0.0f;
        }
    }

    static float s_timeSinceLastDeathSim = 0.0f;
    if (gpGlobals) {
        s_timeSinceLastDeathSim += gpGlobals->frametime;
        if (s_timeSinceLastDeathSim > 20.0f && m_Bots.size() >= 1) {
            edict_t* pVictim = nullptr;
            edict_t* pAttacker = nullptr;

            if (m_Bots.size() >= 2) {
                 pVictim = m_Bots[RAND_LONG(0, m_Bots.size()-1)]->getEdict();
                 pAttacker = m_Bots[RAND_LONG(0, m_Bots.size()-1)]->getEdict();
                 if (pVictim == pAttacker && m_Bots.size() > 1) {
                     int attacker_idx = RAND_LONG(0, m_Bots.size()-1);
                     int victim_idx = (attacker_idx + 1) % m_Bots.size();
                     pAttacker = m_Bots[attacker_idx]->getEdict();
                     pVictim = m_Bots[victim_idx]->getEdict();
                 } else if (pVictim == pAttacker && m_Bots.size() == 1) {
                     pAttacker = gpGlobals->pEdictWorld;
                 }
            } else if (m_Bots.size() == 1) {
                pVictim = m_Bots[0]->getEdict();
                pAttacker = gpGlobals->pEdictWorld;
            }

            if (pVictim && pAttacker) {
                 ProcessPlayerDeathEvent(pVictim, pAttacker);
            }
            s_timeSinceLastDeathSim = 0.0f;
        }
    }

    static float s_timeSinceLastBombPlantSim = 0.0f;
    if (gpGlobals) {
        s_timeSinceLastBombPlantSim += gpGlobals->frametime;
        if (s_timeSinceLastBombPlantSim > 75.0f && !m_Bots.empty()) {
            RCBotBase* planter_bot = m_Bots[RAND_LONG(0, m_Bots.size()-1)];
            if (planter_bot && planter_bot->getEdict()) {
                const auto& objectives = g_ObjectiveManager.getObjectiveCandidates();
                edict_t* target_site_edict = nullptr;

                for(const auto& pair : objectives) {
                    if(pair.second.category_tag == ObjectiveCategoryType::BOMB_SITE && pair.second.is_active) {
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
                    int old_team = planter_bot->getEdict()->v.team;
                    planter_bot->getEdict()->v.team = 1;
                    SimulateBombPlantedEvent(target_site_edict, planter_bot->getEdict());
                    planter_bot->getEdict()->v.team = old_team;
                }
            }
            s_timeSinceLastBombPlantSim = 0.0f;
        }
    }
}

void RCBotManager::OnRoundEnd_Simulated(int winning_team_id) {
    const auto& active_bots = getActiveBots();
    for (RCBotBase* bot : active_bots) {
        if (!bot || !bot->getEdict() || bot->m_currentObjectiveFocusID.empty()) {
            continue;
        }
        ObjectiveCandidateMetadata* objective_data = g_ObjectiveManager.getObjectiveCandidateById(bot->m_currentObjectiveFocusID);
        if (objective_data && objective_data->is_active) {
            float round_outcome_reward = REWARD_VALUE_ROUND_DRAW;
            bool bot_team_won_this_round = false;
            if (winning_team_id != 0) {
                bot_team_won_this_round = (bot->getEdict()->v.team == winning_team_id);
                round_outcome_reward = bot_team_won_this_round ? REWARD_VALUE_ROUND_WIN : PENALTY_VALUE_ROUND_LOSS;
            }
            g_ObjectiveManager.applyTDUpdate(
                bot->m_currentObjectiveFocusID,
                round_outcome_reward,
                "",
                0.0f,
                true
            );
            if (winning_team_id != 0) {
                 g_ObjectiveManager.recordObjectiveInteractionOutcome(bot->m_currentObjectiveFocusID, bot_team_won_this_round);
            }
        }
    }
    if (winning_team_id != 0) {
        for (auto& pair : g_ObjectiveManager.getMutableObjectiveCandidates()) {
            ObjectiveCandidateMetadata& objective = pair.second;
            if (objective.is_active) {
                if (winning_team_id == 1) {
                    objective.rounds_active_in_win++;
                } else if (winning_team_id == 2) {
                    objective.rounds_active_in_loss++;
                }
            }
        }
    }
    g_ObjectiveManager.clearObjectivesOnNewRound();
}

void RCBotManager::SimulateBombPlantedEvent(edict_t* pBombSiteObjectiveEdict, edict_t* pPlanterEdict) {
    if (!pBombSiteObjectiveEdict || !pPlanterEdict || !gpGlobals) {
        return;
    }
    std::string bomb_site_obj_id = g_ObjectiveManager.generateUniqueIDForEntity(pBombSiteObjectiveEdict);
    ObjectiveCandidateMetadata* bomb_site_meta = g_ObjectiveManager.getObjectiveCandidateById(bomb_site_obj_id);
    if (!bomb_site_meta || !bomb_site_meta->is_active || bomb_site_meta->category_tag != ObjectiveCategoryType::BOMB_SITE) {
        return;
    }
    float reward_for_site_objective = 0.0f;
    int planter_team = pPlanterEdict->v.team;
    if (planter_team == 1) {
        reward_for_site_objective = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
    } else {
        reward_for_site_objective = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
    }
    g_ObjectiveManager.applyTDUpdate(bomb_site_obj_id, reward_for_site_objective, "", 0.0f, false);
    const auto& active_bots = getActiveBots();
    for (RCBotBase* bot : active_bots) {
        if (bot && bot->getEdict() && !bot->m_currentObjectiveFocusID.empty() && bot->m_currentObjectiveFocusID == bomb_site_obj_id) {
            float bot_specific_reward = 0.0f;
            if (bot->getEdict()->v.team == planter_team) {
                bot_specific_reward = REWARD_BOMB_PLANTED_AT_SITE_TERRORIST;
            } else {
                bot_specific_reward = PENALTY_BOMB_PLANTED_AT_SITE_CT;
            }
            g_ObjectiveManager.applyTDUpdate(bot->m_currentObjectiveFocusID, bot_specific_reward, "", 0.0f, false);
        }
    }
}

bool RCBotManager::SetQuota(uint8_t iQuota)
{
	if (m_iQuota > gpGlobals->maxClients)
		return false;
	m_iQuota = iQuota;
	return true;
}

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
				char ptr[128];
				pBot->setEdict(pBotEdict);
				pBot->setProfile(profile);
				pBot->setLongTermMemory(&m_longTermMemory);
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

RCBotBase* RCBotManager::getBotByEdict(edict_t* pEdict)
{
	for (auto pBot : m_Bots)
	{
		if (pBot->isEdict(pEdict))
			return pBot;
	}
	return nullptr;
}

void RCBotManager::OnLevelChange()
{
	for (auto* pBot : m_Bots)
	{
		delete pBot;
	}
	m_Bots.clear();
}

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

void RCBotManager::LevelInit()
{
	m_fAddRemoveBotTime = 0.0f;
    m_real_game_state_bomb_planted = false;
    m_real_bomb_planted_location = Vector(0,0,0);
    m_real_planted_bomb_entity = nullptr;
    m_was_bomb_planted_last_frame = false;
	OnLevelChange();
	gRCBotNavigatorNodes->mapInit();
	if (true) {
		Episode dummyEpisode;
		dummyEpisode.metadata.mapName = STRING(gpGlobals->mapname);
		dummyEpisode.metadata.timestamp = static_cast<long>(gpGlobals->time);
		dummyEpisode.metadata.outcome = "pending";
		dummyEpisode.metadata.gameCvars["sv_cheats"] = CVAR_GET_STRING("sv_cheats");
		dummyEpisode.metadata.gameCvars["mp_timelimit"] = CVAR_GET_STRING("mp_timelimit");
		if (!m_Bots.empty()) {
		}
        GameEvent event1(DAMAGE_EVENT, gpGlobals->time - 10.0f, 25.0f);
        dummyEpisode.events.push_back(event1);
        GameEvent event2(HEAR_SOUND_EVENT, gpGlobals->time - 5.0f, 0.0f);
        dummyEpisode.events.push_back(event2);
		m_longTermMemory.archiveEpisode(dummyEpisode);
	}
	g_ChatManager.initializeChatModels("rcbot/chat_training_log.txt"); // Updated call
    g_ObjectiveManager.clearAllObjectives();
    if (gpGlobals) {
        g_ObjectiveManager.setCurrentMapName(STRING(gpGlobals->mapname));
    }
    if (gpGlobals) {
        edict_t* pCurrentEntity = nullptr;
        for (int i = 1; i < gpGlobals->maxEntities; i++) {
            pCurrentEntity = INDEXENT(i);
            if (!pCurrentEntity || pCurrentEntity->free || (pCurrentEntity->v.flags & FL_KILLME)) {
                continue;
            }
            if (pCurrentEntity->v.flags & (FL_CLIENT | FL_FAKECLIENT)) {
                continue;
            }
            g_ObjectiveManager.discoverObjectiveCandidate(
                pCurrentEntity,
                pCurrentEntity->v.origin,
                STRING(pCurrentEntity->v.classname),
                "entity_iteration",
                pCurrentEntity->v.team
            );
        }
        UTIL_ServerPrintf("RCBotManager: Initial entity scan for dynamic objectives complete. Found %d candidates.\n",
            g_ObjectiveManager.getObjectiveCandidates().size());
    }
    if (g_ObjectiveManager.getObjectiveCandidates().size() > 0) {
        g_ObjectiveManager.clusterObjectives(5);
    }
    g_ObjectiveManager.inferObjectiveCategories();
}

void RCBotManager::ProcessPlayerDeathEvent(edict_t* pVictimEdict, edict_t* pAttackerEdict) {
    if (!pVictimEdict || !pAttackerEdict || !gpGlobals) return;

    RCBotBase* victim_bot = getBotByEdict(pVictimEdict);
    RCBotBase* attacker_bot = getBotByEdict(pAttackerEdict);
    Vector death_location = pVictimEdict->v.origin;

    // --- Opponent Modeling Update ---
    if (victim_bot) {
        // The attacker (pAttackerEdict) could be a human player, another bot, or world.
        // RCBotBase::ProcessDeathInvolvingBot handles checking if pAttackerEdict is a human player.
        victim_bot->ProcessDeathInvolvingBot(pAttackerEdict, true, death_location);
    }
    if (attacker_bot && pVictimEdict != pAttackerEdict) { // If the attacker was one of our bots (and not suicide)
        // The victim (pVictimEdict) could be a human player, another bot, or world.
        // RCBotBase::ProcessDeathInvolvingBot handles checking if pVictimEdict is a human player.
        attacker_bot->ProcessDeathInvolvingBot(pVictimEdict, false, death_location);
    }
    // --- End Opponent Modeling Update ---


    // --- Objective Interaction Outcome (Original Logic) ---
    // Scenario 1: An RCBot was the victim
    if (victim_bot && victim_bot->getEdict() && !victim_bot->m_currentObjectiveFocusID.empty()) {
        ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(victim_bot->m_currentObjectiveFocusID);
        if (obj_meta && obj_meta->is_active) {
            g_ObjectiveManager.recordObjectiveInteractionOutcome(victim_bot->m_currentObjectiveFocusID, false);
            // UTIL_ServerPrintf("Bot %s died pursuing obj %s. Confidence potentially updated via interaction outcome.\n",
            //                   STRING(pVictimEdict->v.netname), victim_bot->m_currentObjectiveFocusID.c_str());
        }
    }

    // Scenario 2: An RCBot was the attacker AND the victim was an enemy
    if (attacker_bot && attacker_bot->getEdict() && !attacker_bot->m_currentObjectiveFocusID.empty() && pVictimEdict != pAttackerEdict) {
        bool victim_is_enemy = true;
        if (pVictimEdict->v.team != 0 && pAttackerEdict->v.team != 0 && pVictimEdict->v.team == pAttackerEdict->v.team) {
            victim_is_enemy = false;
        }
        // A more robust check would be: victim_is_enemy = attacker_bot->isEnemy(pVictimEdict);

        if (victim_is_enemy) {
            ObjectiveCandidateMetadata* obj_meta = g_ObjectiveManager.getObjectiveCandidateById(attacker_bot->m_currentObjectiveFocusID);
            if (obj_meta && obj_meta->is_active) {
                float distance_to_objective = (pVictimEdict->v.origin - obj_meta->location).Length();
                if (distance_to_objective < OBJECTIVE_PROXIMITY_FOR_RELEVANCE) {
                    g_ObjectiveManager.recordObjectiveInteractionOutcome(attacker_bot->m_currentObjectiveFocusID, true);
                    // UTIL_ServerPrintf("Bot %s killed enemy near obj %s. Confidence potentially updated via interaction outcome.\n",
                    //                   STRING(pAttackerEdict->v.netname), attacker_bot->m_currentObjectiveFocusID.c_str());
                }
            }
        }
    }
}
