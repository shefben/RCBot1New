#ifndef __RCBOT_MANAGER_H__
#define __RCBOT_MANAGER_H__

#include <vector>
#include <stdint.h>
#include "rcbot_base.h"
#include "rcbot_long_term_memory.h"

#define BOT_MANAGER_DEFAULT_ADD_REMOVE_BOT_PERIOD 5.0f

class RCBotManager
{
public:
	~RCBotManager();
	RCBotManager();
	void Think();

	void KickBot();
	void LevelInit();
	void OnLevelChange();
	bool SetQuota(uint8_t iQuota);
	void IncreaseQuota()
	{
		m_iQuota++;
	}

	RCBotBase* getBotByEdict(edict_t* pEdict);
	const std::vector<RCBotBase*>& getActiveBots() const; // Added to get bot list

	void OnRoundEnd_Simulated(int winning_team_id); // Simulated round end handler
	void ProcessPlayerDeathEvent(edict_t* pVictimEdict, edict_t* pAttackerEdict); // Handles player death events
	void SimulateBombPlantedEvent(edict_t* pBombSiteObjectiveEdict, edict_t* pPlanterEdict); // Simulates a bomb plant

    // Public debug flags for global game state simulation
    bool m_debug_g_simulate_bomb_is_planted; // Renamed to match usage in commands
    bool m_debug_g_simulate_flag_is_loose_team1; // Renamed
    bool m_debug_g_simulate_flag_is_loose_team2; // Renamed

    // Real game state tracking
    bool m_real_game_state_bomb_planted;
    Vector m_real_bomb_planted_location;
    edict_t* m_real_planted_bomb_entity;

private:
    bool m_was_bomb_planted_last_frame;
	RCBotBase* AddBot();

	std::vector<RCBotBase*> m_Bots;
	uint8_t m_iQuota;
	float m_fAddRemoveBotTime;
	float m_fNodeDrawTime;
	RCBotLongTermMemory m_longTermMemory;

    float m_timeSinceLastObjectiveDecay; // Timer for periodic decay of dynamic objectives
    static const float OBJECTIVE_DECAY_INTERVAL = 15.0f; // Seconds
};

extern RCBotManager gRCBotManager;


#endif 