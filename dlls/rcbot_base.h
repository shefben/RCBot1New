#ifndef __RCBOT_BASE_H__
#define __RCBOT_BASE_H__

#include "extdll.h"
#include "rcbot_ehandle.h"
#include <stdint.h>
#include "rcbot_short_term_memory.h"
#include "rcbot_long_term_memory.h" // Added for LTM
#include <set> // For std::set
#include <map> // For std::map
#include <string> // For std::string
#include "rcbot_macro_action.h" // For Macro Actions
#include "rcbot_chat_types.h"   // For BotPersona
#include "rcbot_chat_manager.h" // For g_ChatManager
#include "rcbot_chat_history.h" // For RCBotChatHistory
#include "rl_types.h"           // For BotState, BotActionType, RLTransition
#include "RCBotRLHelper.h"      // For RCBotRLHelper
#include <deque>                // For std::deque

class RCBotProfile;
class RCBotVisibles;
class RCBotSchedule;
class RCBotUtilities;
class RCBotWeapons;

#define RCBOT_DEFAULT_FOV 100

template <class T>
class RCBotDemand
{
public:
	RCBotDemand()
	{
		m_iPriority = 0;
	}

	void setValue(T& value, uint8_t iPriority )
	{
		if (iPriority > m_iPriority)
		{
			m_Value = value;
			m_iPriority = iPriority;
		}
	}

	T getValue(void) const
	{
		return m_Value;
	}

	void reset()
	{
		m_iPriority = 0;
	}

	bool isValid()
	{
		return m_iPriority > 0;
	}
private:
	T m_Value;
	uint8_t m_iPriority;
};

class RCBotWeapon;

class RCBotBase
{
public:

	RCBotBase();

	~RCBotBase();

	void selectWeapon(RCBotWeapon* weapon);
	bool isCurrentWeapon(RCBotWeapon* weapon);

	virtual void Think();

	void Interrupt()
	{
		m_bInterrupted = true;
	}

	void Init();
	virtual void spawnInit();

	void setAmmo(uint8_t index, uint8_t amount);

	void setProfile(RCBotProfile *profile);
	void setEdict(edict_t *pEdict);
	void setLongTermMemory(RCBotLongTermMemory* ltm); // Added for LTM
	
	virtual void setUpClientInfo();

	bool inViewCone(Vector &vOrigin);

	void setFOV(float fFov);
	Vector getViewOrigin();

	void newVisible(edict_t* pEntity);

	void lostVisible(edict_t* pEntity);

	// do stuff
	virtual void setCurrentWeapon(uint8_t iState, uint8_t iId, uint8_t iClip);
	virtual void weaponPickup(uint8_t iId);

	virtual bool isEnemy(edict_t* pEntity)
	{
		return false;
	}

	virtual float getEnemyFactor(edict_t* pEntity);

	edict_t* getEdict()
	{
		return m_pEdict;
	}

	bool isEdict(const edict_t* pEdict)
	{
		return m_pEdict == pEdict;
	}


	void walk()
	{
		m_fSpeedPercent = 0.5; // half speed
	}

	void setMoveTo(Vector vMoveTo, uint8_t iPriority = 1 )
	{
		m_vMoveTo.setValue(vMoveTo, iPriority);
	}

	void setLookAt(Vector vLookAt, uint8_t iPriority = 1)
	{
		m_vLookAt.setValue(vLookAt, iPriority);
	}

	void jump()
	{
		// buttons
		m_pEdict->v.button |= IN_JUMP;
	}


	void duck()
	{
		// buttons
		m_pEdict->v.button |= IN_DUCK;
	}

	void primaryAttack()
	{
		m_pEdict->v.button |= IN_ATTACK;
	}

	float getActualSpeed()
	{
		return m_pEdict->v.velocity.Length();
	}

	bool isAlive();

	virtual void respawn();

	float distanceFrom(const edict_t* pEntity);
	float distanceFrom(const Vector &vOrigin);
	float distanceFrom2D(const Vector& vOrigin)
	{
		return (vOrigin - getViewOrigin()).Length2D();
	}
	void RunPlayerMove();

	void pressButton(int button);

	inline bool isUnderWater() { return m_pEdict->v.waterlevel > 2; }

	uint32_t getEnemyWeaponFlags( const edict_t *pEnemy ) { return 0; }
protected:
	edict_t* m_pEdict;
	EHandle m_pEnemy;
	RCBotShortTermMemory m_shortTermMemory; // Short-term memory for game events
private:
	RCBotProfile* m_pProfile;
	RCBotVisibles* m_pVisibles;
	RCBotSchedule* m_pSchedule;
	RCBotUtilities *m_Utils;
	RCBotWeapons* m_pWeapons;
	RCBotWeapon* m_pCurrentWeapon;
	bool m_bInterrupted; // bot was interrupted and a new utility may be chosen

	float m_fFovCos;

	RCBotDemand<Vector> m_vMoveTo;
	RCBotDemand<Vector> m_vLookAt;

	float m_fRespawnTime;

	float m_fLastRunPlayerMove;

	bool m_bPreviousAliveState;

	float m_fSpeedPercent;

	RCBotReplayBuffer m_replayBuffer;      // Changed from RCBotShortTermMemory
	RCBotLongTermMemory* m_pLongTermMemory; // Pointer to the LTM system

	// RL State and Action Tracking
	BotState m_currentState;
	BotState m_previousState;
	BotActionType m_lastAction; // Action taken that led to m_currentState
    BotActionType m_chosenAIActionThisFrame; // High-level AI action chosen in current Think
	bool m_firstThinkCycle; // To handle initial state
    float m_timeSinceLastDamageTaken;
    float m_timeSpentIdleOrStuck;
    float m_lastThinkHealth; // Health at the end of the previous Think cycle
    EHandle m_pLastEnemy;    // Last enemy targeted, for damage dealt calculation
    float m_lastEnemyHealth; // Health of the last enemy, for damage dealt calculation
    RCBotRLHelper m_rlHelper; // RL Helper instance
    std::string m_previousDynamicObjectiveFocusID_debug; // For tracking changes in dynamic objective focus for shaping rewards

	// Curiosity and Novelty Detection
	float m_curiosityScore;
	std::set<std::string> m_encounteredEntityClasses;
	std::set<int> m_visitedWaypoints; // Assuming waypoint IDs are integers
	std::map<std::string, float> m_itemCuriosity; // Key: entity classname or waypoint_ID as string

public:
	// Constants for curiosity - can be moved to a config file later
	static const float NEW_ENTITY_BONUS = 10.0f;
	static const float NEW_AREA_BONUS = 15.0f; // Waypoint based
	static const float CURIOSITY_DECAY_RATE = 0.995f; // Per Think cycle
	static const float ITEM_CURIOSITY_DECAY_RATE = 0.99f; // Per Think cycle for specific items

	// Objective Interest System
	std::map<std::string, float> m_objectiveInterests; // Key: objective identifier, Value: interest score
	std::string m_currentFocusObjective; // String description from intrinsic motivation system
	std::string m_currentObjectiveFocusID; // unique_id from DynamicObjectiveManager for the current high-level objective
	float m_timeObjectiveFocused;      // Timestamp when m_currentObjectiveFocusID was set

public:
	// Constants for interest system
	static const float INTEREST_DECAY_RATE = 0.99f; // Per Think cycle
	static const float INITIAL_OBJECTIVE_INTEREST = 5.0f; // Default interest for new objectives from curiosity
	static const float CURIOSITY_TO_INTEREST_THRESHOLD = 10.0f; // Min item curiosity to generate an objective

	// Macro Action System
	std::map<std::string, RCBotMacroAction> m_macroActions;
	RCBotMacroAction* m_currentMacroAction;

public:
	void loadMacroActions();
	void startMacroAction(const std::string& name);
	void stopCurrentMacroAction();

	// Persona
	void setPersona(BotPersona persona);
	BotPersona getPersona() const;
	void evaluateAndAdjustPersona(); // Method to adjust persona based on feedback

	// Chat
	void sayChat(const std::string& context_trigger);
	// Store own recent messages for context (e.g., checking for replies)
	std::deque<TaggedChatMessage> m_sentChatMessages;
	static const size_t MAX_SENT_CHAT_HISTORY = 10; // Keep last 10 messages

	// Placeholder metrics for persona adjustment
	float m_lastTauntTime;
	int m_damageTakenPostTaunt; // Conceptual: damage taken shortly after a taunt
	float m_engagementScore;    // Conceptual: increases with positive interactions
	float m_aggressivenessScore; // Conceptual: tracks performance of aggressive actions/chats
    float m_timeSinceLastPersonaEvaluation;

	RCBotChatHistory m_chatContextMemory; // For storing chat and game event context

	// Reward Shaping for Objectives
	float m_previousDistanceToFocusObjective;
	Vector m_focusObjectiveLocation;
	bool m_hasFocusObjectiveLocation;
	float m_shapingRewardAccumulator; // Optional, not used in initial implementation

public:
	RCBotChatHistory* getChatContextMemory() { return &m_chatContextMemory; }

	// Constants for Reward Shaping
	static const float SHAPING_REWARD_MULTIPLIER = 0.05f;
	static const float SIGNIFICANT_PROGRESS_THRESHOLD = 1.0f; // Min distance change to get reward
	static const float INTEREST_BOOST_FROM_SHAPING_FACTOR = 0.01f;

public:
	// Player Perception Model (based on chat)
	float m_perceivedPlayerAggression;
	float m_perceivedPlayerCooperation;
	EHandle m_lastInteractingPlayerEdict; // Using EHandle for safety

	void updatePerceptionFromPlayerChat(edict_t* pPlayerEdict, float chat_sentiment_score);

	// Constants for Player Perception Model
	static const float SENTIMENT_TO_AGGRESSION_FACTOR = 0.1f;
	static const float SENTIMENT_TO_COOPERATION_FACTOR = 0.1f;
	static const float PERCEPTION_DECAY_RATE = 0.995f; // Per Think cycle, towards baseline
	static const float PERCEPTION_BASELINE = 0.5f;    // Neutral baseline for perception decay

public:
	// Non-Visual Entity Interaction Novelty
	void processEntityInteractionNovelty(edict_t* pEntity, const std::string& interaction_type);

private: // Helper methods for RL
    // getCurrentBotState() is now handled by m_rlHelper.getCurrentBotState(...)
    BotActionType determineBotAction() const; // Returns m_chosenAIActionThisFrame
    // calculateReward() is now handled by m_rlHelper.addReward(...)

    // Non-Visual Entity Interaction Novelty Helpers
    std::vector<float> extractEntityFeatures(edict_t* pEntity, const std::string& interaction_type) const;
    std::deque<std::vector<float>> m_seenEntityFeaturesLog;

public: // Constants for Entity Interaction Novelty
    static const size_t MAX_SEEN_FEATURES_LOG_SIZE = 200;
    static const float ENTITY_FEATURE_NOVELTY_THRESHOLD = 0.7f; // Min Euclidean distance to be "novel"
    static const float ENTITY_NOVELTY_REWARD_MULTIPLIER = 0.2f;

public: // Game Event Recording
    void recordGameEvent(const GameEvent& event);

private: // Make persona private and expose via getter/setter
	BotPersona m_persona;
};

#endif 