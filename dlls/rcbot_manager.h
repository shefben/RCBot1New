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
    bool m_debug_g_bomb_planted;
    bool m_debug_g_flag_loose_t1;
    bool m_debug_g_flag_loose_t2;

private:
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