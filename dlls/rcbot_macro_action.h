#ifndef RCBOT_MACRO_ACTION_H
#define RCBOT_MACRO_ACTION_H

#include <string>
#include <vector>
#include "extdll.h" // For Vector, and potentially IN_BUTTONS defines indirectly
#include "usercmd.h" // For IN_BUTTONS defines (like IN_JUMP, IN_MOVELEFT etc)

// Forward declaration
class RCBotBase;

enum MacroActionType {
    PRESS_KEY,          // Press a button (e.g., IN_JUMP)
    RELEASE_KEY,        // Release a button
    SET_AIM_DIRECTION,  // Aim at a specific world direction vector (normalized)
    SET_AIM_AT_OFFSET,  // Aim at a world-space offset from current bot position
    MOVE_FORWARD_DURATION, // Move forward for a specific duration
    MOVE_RIGHT_DURATION,   // Move right for a specific duration
    MOVE_UP_DURATION,      // Move up for a specific duration (e.g. for ladders/swimming if applicable)
    WAIT_DURATION,      // Do nothing for a specific duration
    SET_SPEED_PERCENT,  // Set bot's movement speed percentage
    CUSTOM_LOGIC        // Placeholder for a step that calls a specific bot function
};

struct MacroStep {
    MacroActionType type;

    // Parameters - only relevant ones are used based on type
    int key;              // For PRESS_KEY, RELEASE_KEY (e.g., IN_JUMP)
    Vector aimDirection;  // For SET_AIM_DIRECTION (world space direction)
    Vector aimOffset;     // For SET_AIM_AT_OFFSET (offset from bot origin)
    float duration;       // For _DURATION actions (in seconds)
    float speedPercent;   // For SET_SPEED_PERCENT
    std::string customLogicIdentifier; // For CUSTOM_LOGIC

    // Constructors for convenience
    MacroStep(MacroActionType t) : type(t), key(0), duration(0.0f), speedPercent(1.0f) {}

    MacroStep(MacroActionType t, int k) : type(t), key(k), duration(0.0f), speedPercent(1.0f) {} // For key presses

    MacroStep(MacroActionType t, float d) : type(t), key(0), duration(d), speedPercent(1.0f) {} // For durations

    MacroStep(MacroActionType t, const Vector& vec_param) : type(t), key(0), duration(0.0f), speedPercent(1.0f) {
        if (type == SET_AIM_DIRECTION) aimDirection = vec_param;
        else if (type == SET_AIM_AT_OFFSET) aimOffset = vec_param;
    }
     MacroStep(MacroActionType t, float val1, float val2) : type(t), key(0), duration(0.0f), speedPercent(1.0f) {
        if (type == SET_SPEED_PERCENT) speedPercent = val1;
        // Potentially other uses for two float params
    }
};

class RCBotMacroAction {
public:
    std::string m_name;
    std::vector<MacroStep> m_steps;
    int m_currentStepIndex;
    float m_currentStepTimeElapsed; // Time elapsed *within* the current step

    RCBotMacroAction(const std::string& name = "UnnamedMacro");

    void addStep(const MacroStep& step);

    void start();
    void update(RCBotBase* bot, float flTimeDelta); // flTimeDelta is frame time
    bool isFinished() const;
    void reset();
};

#endif // RCBOT_MACRO_ACTION_H
