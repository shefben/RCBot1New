#include "rcbot_macro_action.h"
#include "rcbot_base.h" // For calling bot actions like pressButton, setLookAt, etc.
#include "rcbot_utils.h" // For RCBotUtils::GetGlobalTime or similar if needed, though gpGlobals is usually preferred.
#include "extdll.h"     // For gpGlobals
#include "util.h"       // For UTIL_LogPrintf (if used for debugging) or other engine utilities.

RCBotMacroAction::RCBotMacroAction(const std::string& name)
    : m_name(name), m_currentStepIndex(0), m_currentStepTimeElapsed(0.0f) {
    m_steps.clear();
}

void RCBotMacroAction::addStep(const MacroStep& step) {
    m_steps.push_back(step);
}

void RCBotMacroAction::start() {
    m_currentStepIndex = 0;
    m_currentStepTimeElapsed = 0.0f;
    // Potentially log: fprintf(stdout, "MacroAction '%s' started.\n", m_name.c_str());
}

bool RCBotMacroAction::isFinished() const {
    return m_currentStepIndex >= m_steps.size();
}

void RCBotMacroAction::reset() {
    start();
}

void RCBotMacroAction::update(RCBotBase* bot, float flTimeDelta) {
    if (isFinished() || !bot || !bot->isAlive()) {
        return;
    }

    if (m_steps.empty()) {
        m_currentStepIndex = 0; // Ensure it's marked as finished if steps are empty
        return;
    }

    MacroStep& currentStep = m_steps[m_currentStepIndex];
    bool stepCompleted = false;

    // Process the current step based on its type
    switch (currentStep.type) {
        case PRESS_KEY:
            bot->pressButton(currentStep.key); // Assumes RCBotBase::pressButton directly sets the button state for the current frame
            stepCompleted = true; // Key presses are usually instantaneous for one frame in this context
            break;

        case RELEASE_KEY:
            // Releasing a key usually means *not* pressing it in the current usercmd.
            // RCBotBase::Think() typically clears buttons at the start.
            // If a key was pressed in a previous step and needs to be explicitly released *within* a macro
            // that spans multiple frames, the bot's button handling logic needs to support this.
            // For now, we assume that not calling pressButton() achieves release for that frame.
            // A more robust system might have bot->releaseButton(currentStep.key);
            stepCompleted = true;
            break;

        case SET_AIM_DIRECTION:
            // Assuming bot has a method like setViewDirection(const Vector& direction, float priority)
            // For now, using setLookAt which takes a world point. Need to calculate a point.
            // Vector lookAtPoint = bot->getViewOrigin() + currentStep.aimDirection * 1000.0f; // Look far in that direction
            // bot->setLookAt(lookAtPoint, 2); // High priority for macro
            // This needs careful thought: setLookAt is based on m_vLookAt demand.
            // A direct override for view angles might be better for macros.
            // Let's assume a direct v_angle manipulation for now (conceptual)
            // bot->getEdict()->v.v_angle = RCBotUtils::VectorToAngles(currentStep.aimDirection);
            // This is too direct and might fight with RunPlayerMove's angle smoothing.
            // Using setLookAt with a point far away.
            bot->setLookAt(bot->getViewOrigin() + currentStep.aimDirection * 1000.0f, 2);
            stepCompleted = true; // Aiming is set for this frame
            break;

        case SET_AIM_AT_OFFSET:
            {
                Vector targetPoint = bot->getEdict()->v.origin + currentStep.aimOffset;
                bot->setLookAt(targetPoint, 2); // High priority
            }
            stepCompleted = true; // Aiming is set for this frame
            break;

        case MOVE_FORWARD_DURATION:
        case MOVE_RIGHT_DURATION:
        case MOVE_UP_DURATION:
            // These require RCBotBase to have a way to sustain movement commands
            // or for the macro to repeatedly issue them.
            // A simple way: set a desired velocity or movement intention.
            // For now, let's assume pressButton for movement and rely on duration.
            if (m_currentStepTimeElapsed == 0.0f) { // First frame of this step
                if(currentStep.type == MOVE_FORWARD_DURATION) bot->pressButton(IN_FORWARD);
                else if(currentStep.type == MOVE_RIGHT_DURATION) bot->pressButton(IN_MOVERIGHT);
                else if(currentStep.type == MOVE_UP_DURATION) bot->pressButton(IN_MOVEUP); // Hypothetical IN_MOVEUP
            }
            m_currentStepTimeElapsed += flTimeDelta;
            if (m_currentStepTimeElapsed >= currentStep.duration) {
                stepCompleted = true;
                // Implicitly, the IN_FORWARD etc. will be cleared by RCBotBase::Think on next frame if not repressed
            }
            break;

        case WAIT_DURATION:
            m_currentStepTimeElapsed += flTimeDelta;
            if (m_currentStepTimeElapsed >= currentStep.duration) {
                stepCompleted = true;
            }
            break;

        case SET_SPEED_PERCENT:
            // bot->setSpeedPercent(currentStep.speedPercent); // Assuming such a method exists
            // For now, this is conceptual as RCBotBase sets m_fSpeedPercent = 1.0f each Think.
            // This would need m_fSpeedPercent to be respected by RunPlayerMove's parameters.
            stepCompleted = true;
            break;

        case CUSTOM_LOGIC:
            // This would require RCBotBase to have a dispatch mechanism for custom logic strings
            // e.g., bot->executeCustomMacroLogic(currentStep.customLogicIdentifier);
            stepCompleted = true; // Assume custom logic handles its own duration or is instant
            break;

        default:
            stepCompleted = true; // Unknown types are skipped
            break;
    }

    if (stepCompleted) {
        m_currentStepIndex++;
        m_currentStepTimeElapsed = 0.0f; // Reset timer for the next step
        if (isFinished()) {
            // Potentially log: fprintf(stdout, "MacroAction '%s' finished.\n", m_name.c_str());
        }
    }
}
