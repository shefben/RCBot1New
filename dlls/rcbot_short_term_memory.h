#ifndef RCBOT_SHORT_TERM_MEMORY_H
#define RCBOT_SHORT_TERM_MEMORY_H

#include <deque>  // For std::deque as replay buffer
#include <vector>
#include <string>
#include <algorithm> // Required for std::min, std::sample (C++17) or custom sample
#include <random>    // For sampling
#include "rl_types.h" // For RLTransition

// Define a maximum size for the replay buffer
const size_t MAX_REPLAY_BUFFER_SIZE = 10000; // Increased size for RL replay buffer

// GameEvent struct and GameEventType enum can remain if they are still used for other purposes
// or by other systems (e.g. logging to LTM).
// If they are *only* for the old STM, they could be removed or deprecated.
// For now, assuming they might still be used by LTM or other parts, so keeping them.
enum GameEventType {
    DAMAGE_EVENT,
    HEAR_SOUND_EVENT,
    // Add other event types as needed
};
struct GameEvent {
    GameEventType type;
    float timestamp;
    float damageAmount;
    std::string attacker_info_str;
    std::string target_info_str;

    GameEvent(GameEventType t, float ts, float dmg,
              const std::string& attacker = "", const std::string& target = "")
        : type(t), timestamp(ts), damageAmount(dmg),
          attacker_info_str(attacker), target_info_str(target) {}
    GameEvent() : type(DAMAGE_EVENT), timestamp(0.0f), damageAmount(0.0f) {}
};


// Class to manage a replay buffer for Reinforcement Learning transitions
class RCBotReplayBuffer { // Renamed class for clarity
public:
    RCBotReplayBuffer();

    // Adds a new transition to the replay buffer
    void addTransition(const RLTransition& transition);

    // Retrieves a specified number of recent transitions
    std::vector<RLTransition> getRecentTransitions(int count) const;

    // Retrieves all transitions currently in the buffer
    std::vector<RLTransition> getAllTransitions() const;

    // Samples a single random transition from the buffer
    // Returns an empty/default transition if buffer is empty (caller should check)
    RLTransition sampleTransition();

    // Samples a batch of random transitions from the buffer
    // Returns fewer than batch_size if buffer is smaller.
    std::vector<RLTransition> sampleBatch(int batch_size);

    size_t size() const { return m_replay_buffer.size(); }
    bool isEmpty() const { return m_replay_buffer.empty(); }
    void clear() { m_replay_buffer.clear(); }

private:
    std::deque<RLTransition> m_replay_buffer;
    // currentIndex is not needed for deque-based replay buffer if adding to back and popping from front
    // std::mt19937 m_rng; // For random sampling, initialized with a random_device
};

#endif // RCBOT_SHORT_TERM_MEMORY_H
