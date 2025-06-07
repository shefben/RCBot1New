#include "rcbot_short_term_memory.h" // Contains RCBotReplayBuffer declaration and RLTransition (via rl_types.h)
#include <algorithm> // For std::min, std::shuffle (for sampleBatch if not using C++17 std::sample)
#include <random>    // For std::mt19937, std::random_device, std::uniform_int_distribution

// Constructor for RCBotReplayBuffer
RCBotReplayBuffer::RCBotReplayBuffer() {
    // m_replay_buffer is a deque, default constructor is fine.
    // m_rng can be initialized here if needed, or on first use.
    // std::random_device rd; // Using this directly in sample methods for now
    // m_rng = std::mt19937(rd());
}

// Adds a new transition to the replay buffer
void RCBotReplayBuffer::addTransition(const RLTransition& transition) {
    if (m_replay_buffer.size() >= MAX_REPLAY_BUFFER_SIZE) {
        m_replay_buffer.pop_front(); // Remove the oldest transition
    }
    m_replay_buffer.push_back(transition);
}

// Retrieves a specified number of recent transitions
std::vector<RLTransition> RCBotReplayBuffer::getRecentTransitions(int count) const {
    std::vector<RLTransition> recent_transitions;
    int numToRetrieve = std::min(static_cast<int>(m_replay_buffer.size()), count);

    if (numToRetrieve <= 0) {
        return recent_transitions;
    }

    recent_transitions.reserve(numToRetrieve);
    for (auto it = m_replay_buffer.rbegin(); it != m_replay_buffer.rend() && numToRetrieve > 0; ++it, --numToRetrieve) {
        recent_transitions.push_back(*it);
    }
    std::reverse(recent_transitions.begin(), recent_transitions.end()); // To return in chronological order
    return recent_transitions;
}

// Retrieves all transitions currently in the buffer
std::vector<RLTransition> RCBotReplayBuffer::getAllTransitions() const {
    return std::vector<RLTransition>(m_replay_buffer.begin(), m_replay_buffer.end());
}

// Samples a single random transition from the buffer
RLTransition RCBotReplayBuffer::sampleTransition() {
    if (m_replay_buffer.empty()) {
        return RLTransition(); // Return a default/empty transition
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, m_replay_buffer.size() - 1);
    return m_replay_buffer[distrib(gen)];
}

// Samples a batch of random transitions from the buffer
std::vector<RLTransition> RCBotReplayBuffer::sampleBatch(int batch_size) {
    std::vector<RLTransition> batch;
    if (m_replay_buffer.empty() || batch_size <= 0) {
        return batch;
    }

    size_t num_to_sample = std::min(static_cast<size_t>(batch_size), m_replay_buffer.size());
    batch.reserve(num_to_sample);

    // For true random sampling without replacement for a small batch from a large buffer,
    // we can pick random indices and ensure uniqueness, or shuffle a copy of pointers/indices.
    // For simplicity here, if batch_size is small relative to buffer, N calls to sampleTransition is often okay,
    // but proper batch sampling without replacement is better.
    // C++17 std::sample is ideal. Without it, we can do this:

    std::vector<int> indices(m_replay_buffer.size());
    std::iota(indices.begin(), indices.end(), 0); // Fill with 0, 1, ..., n-1

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(indices.begin(), indices.end(), gen);

    for (size_t i = 0; i < num_to_sample; ++i) {
        batch.push_back(m_replay_buffer[indices[i]]);
    }

    return batch;
}
