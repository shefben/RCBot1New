#ifndef RCBOT_SCHEDULE_OBJ_INTERACTION_H
#define RCBOT_SCHEDULE_OBJ_INTERACTION_H

#include "rcbot_schedule.h" // For RCBotSchedule base class
#include "rl_types.h"       // For ObjectiveInteractionType
#include <string>           // For std::string (objective ID)
// Forward declare RCBotBase if needed by Execute, or include rcbot_base.h
class RCBotBase;
// Forward declare ObjectiveCandidateMetadata or include rcbot_dynamic_objectives.h
struct ObjectiveCandidateMetadata;


class ScheduleExecuteObjectiveInteraction : public RCBotSchedule {
public:
    ScheduleExecuteObjectiveInteraction(RCBotBase* pBot,
                                        const std::string& objective_id,
                                        ObjectiveInteractionType interaction_type,
                                        float interaction_duration = 0.0f);
    ~ScheduleExecuteObjectiveInteraction() override;

    RCBotTaskState Execute(RCBotBase* pBot) override;
    void OnScheduleInterrupt(RCBotBase* pBot) override;
    void OnScheduleResume(RCBotBase* pBot) override; // Optional
    std::string GetName() const override { return "ScheduleExecuteObjectiveInteraction"; }


private:
    enum class InteractionState {
        INIT_FAIL = -1, // Added for immediate failure from constructor
        NAVIGATING,
        ALIGNING,
        INTERACTING,
        COMPLETED,
        FAILED
    };

    std::string m_targetObjectiveID;
    ObjectiveInteractionType m_interactionType;
    float m_interactionDurationNeeded; // For USE_FOR_DURATION

    InteractionState m_currentState;
    Vector m_targetObjectiveLocation; // Cached location
    float m_interactionTimer;       // For timed interactions
    int m_alignAttempts;
    int m_maxAlignAttempts;

    float m_navStopDistance; // How close to get before stopping navigation
    float m_fLastScheduleUpdateTimestamp; // For periodic objective re-validation

    // Helper to get objective data and location
    bool initializeObjectiveData(RCBotBase* pBot);
};

#endif // RCBOT_SCHEDULE_OBJ_INTERACTION_H
