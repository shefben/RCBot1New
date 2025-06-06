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
}
