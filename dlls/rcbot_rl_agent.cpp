#include "rcbot_rl_agent.h"
#include <random>    // For std::mt19937, std::uniform_real_distribution, std::uniform_int_distribution
#include <algorithm> // For std::max_element
#include <vector>
#include <fstream>   // For save/load
#include <sstream>   // For save/load
#include <limits>    // For std::numeric_limits
#include <time.h>    // For time() in srand

// Constructor
RCBotRLAgent::RCBotRLAgent(int num_state_features, int num_actions,
                           float learning_rate, float discount_factor, float epsilon)
    : m_numStateFeatures(num_state_features),
      m_numActions(num_actions),
      m_learningRate(learning_rate),
      m_discountFactor(discount_factor),
      m_epsilon(epsilon) {
    resetWeights(); // Initialize weights
    // Seed random number generator (once, or pass engine if available)
    // For now, simple seed. A proper game bot might use a game-time based seed or engine's RNG.
    srand(static_cast<unsigned int>(time(0)));
}

void RCBotRLAgent::resetWeights() {
    m_weights.assign(m_numActions, std::vector<float>(m_numStateFeatures, 0.0f));
    // Optionally, initialize with small random values:
    // std::mt19937 gen(std::random_device{}()); // Needs <random> for std::random_device
    // std::uniform_real_distribution<float> dist(-0.01f, 0.01f);
    // for (int i = 0; i < m_numActions; ++i) {
    //     for (int j = 0; j < m_numStateFeatures; ++j) {
    //         m_weights[i][j] = dist(gen);
    //     }
    // }
}

// Calculate Q(s,a) using dot product of state features and action weights
float RCBotRLAgent::getQValue(const BotState& state, BotActionType action) const {
    if (state.features.empty() || static_cast<int>(action) < 0 || static_cast<int>(action) >= m_numActions) {
        return 0.0f; // Or some default for invalid action/state
    }
    // Ensure state feature vector matches weight vector size, or handle gracefully
    if (state.features.size() != static_cast<size_t>(m_numStateFeatures)) {
        // This is an error condition, indicates mismatch in state representation
        // For now, return 0, but should be logged/handled.
        // One common case: initial state might be empty before first real state is formed.
        return 0.0f;
    }

    float q_value = 0.0f;
    const std::vector<float>& action_weights = m_weights[static_cast<int>(action)];
    for (size_t i = 0; i < static_cast<size_t>(m_numStateFeatures); ++i) { // Cast m_numStateFeatures for comparison if state.features.size() is size_t
        q_value += action_weights[i] * state.features[i];
    }
    return q_value;
}

// Epsilon-greedy action selection
BotActionType RCBotRLAgent::chooseAction(const BotState& state, bool is_exploration_allowed) {
    if (state.features.empty()) { // Handle empty/invalid state
         return static_cast<BotActionType>(rand() % m_numActions); // Random action if no state
    }
    if (state.features.size() != static_cast<size_t>(m_numStateFeatures)) { // Handle mismatched features
        return static_cast<BotActionType>(rand() % m_numActions); // Random action
    }


    // Exploration vs. Exploitation
    if (is_exploration_allowed && (static_cast<float>(rand()) / RAND_MAX) < m_epsilon) {
        // Explore: choose a random action
        return static_cast<BotActionType>(rand() % m_numActions);
    } else {
        // Exploit: choose the action with the highest Q-value
        float max_q = -std::numeric_limits<float>::infinity();
        // BotActionType best_action = BotActionType::IDLE; // Default // Not used directly

        std::vector<BotActionType> best_actions; // For ties

        for (int i = 0; i < m_numActions; ++i) {
            BotActionType current_action = static_cast<BotActionType>(i);
            float q = getQValue(state, current_action);
            if (q > max_q) {
                max_q = q;
                best_actions.clear();
                best_actions.push_back(current_action);
            } else if (q == max_q) { // Use a small epsilon for float comparison if necessary, but direct equality is often fine here.
                 best_actions.push_back(current_action);
            }
        }
        if (!best_actions.empty()) {
            return best_actions[rand() % best_actions.size()]; // Randomly pick among ties
        }
        // Fallback if all Q-values were -infinity or no actions possible (should not happen with proper init)
        return static_cast<BotActionType>(rand() % m_numActions);
    }
}

// Q-learning update rule for linear function approximation:
// w_a = w_a + alpha * (reward + gamma * max_a'(Q(s',a')) - Q(s,a)) * features_s
void RCBotRLAgent::learn(const RLTransition& transition) {
    if (transition.state.features.empty() || transition.next_state.features.empty() ||
        static_cast<int>(transition.action) < 0 || static_cast<int>(transition.action) >= m_numActions) {
        return; // Invalid transition
    }
     if (transition.state.features.size() != static_cast<size_t>(m_numStateFeatures) ||
         transition.next_state.features.size() != static_cast<size_t>(m_numStateFeatures)) {
        return; // Mismatched feature vector size
    }


    float current_q = getQValue(transition.state, transition.action);
    float max_next_q = -std::numeric_limits<float>::infinity(); // Initialize to very small number

    if (!transition.is_terminal) {
        bool first_q_val_for_next_state = true;
        for (int i = 0; i < m_numActions; ++i) {
            float q_val = getQValue(transition.next_state, static_cast<BotActionType>(i));
            if (first_q_val_for_next_state || q_val > max_next_q) {
                max_next_q = q_val;
                first_q_val_for_next_state = false;
            }
        }
    } else {
      max_next_q = 0.0f; // Value of terminal state is 0
    }


    float td_error = transition.reward + m_discountFactor * max_next_q - current_q;

    // Update weights for the taken action
    std::vector<float>& weights_for_action = m_weights[static_cast<int>(transition.action)];
    for (size_t i = 0; i < static_cast<size_t>(m_numStateFeatures); ++i) {
        weights_for_action[i] += m_learningRate * td_error * transition.state.features[i];
    }
}

// Basic model saving (weights to a text file)
bool RCBotRLAgent::saveModel(const std::string& filepath) const {
    std::ofstream outfile(filepath);
    if (!outfile.is_open()) {
        return false;
    }
    outfile << m_numActions << " " << m_numStateFeatures << std::endl;
    outfile << m_learningRate << " " << m_discountFactor << " " << m_epsilon << std::endl;
    for (int i = 0; i < m_numActions; ++i) {
        for (int j = 0; j < m_numStateFeatures; ++j) {
            outfile << m_weights[i][j] << (j == m_numStateFeatures - 1 ? "" : " ");
        }
        outfile << std::endl;
    }
    outfile.close();
    return true;
}

// Basic model loading
bool RCBotRLAgent::loadModel(const std::string& filepath) {
    std::ifstream infile(filepath);
    if (!infile.is_open()) {
        return false;
    }
    int num_a, num_f;
    infile >> num_a >> num_f;

    // It's safer to try to read parameters first, then check if they match.
    // If they don't match, we can't use the weights anyway.
    float loaded_lr, loaded_df, loaded_ep;
    if (!(infile >> loaded_lr >> loaded_df >> loaded_ep)) {
        infile.close();
        return false; // Failed to read parameters
    }

    if (num_a != m_numActions || num_f != m_numStateFeatures) {
        // Model dimensions mismatch. Don't load, keep current (default or previously loaded) settings.
        // Or, one could decide to re-initialize the agent if dimensions mismatch,
        // but for now, failing the load is safer.
        infile.close();
        return false;
    }

    // If dimensions match, apply the loaded parameters
    m_learningRate = loaded_lr;
    m_discountFactor = loaded_df;
    m_epsilon = loaded_ep;

    m_weights.assign(m_numActions, std::vector<float>(m_numStateFeatures, 0.0f));
    for (int i = 0; i < m_numActions; ++i) {
        for (int j = 0; j < m_numStateFeatures; ++j) {
            if (!(infile >> m_weights[i][j])) {
                // Error reading weights
                infile.close();
                resetWeights(); // Reset to default if load fails midway
                return false;
            }
        }
    }
    infile.close();
    return true;
}
