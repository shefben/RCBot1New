#ifndef RCBOT_RL_AGENT_H
#define RCBOT_RL_AGENT_H

#include "rl_types.h" // For BotState, BotActionType, RLTransition
#include <vector>
#include <string>
#include <map> // May not be needed if using linear approximator directly

class RCBotRLAgent {
public:
    RCBotRLAgent(int num_state_features, int num_actions,
                 float learning_rate = 0.1f,
                 float discount_factor = 0.9f,
                 float epsilon = 0.1f);

    BotActionType chooseAction(const BotState& state, bool is_exploration_allowed = true);
    void learn(const RLTransition& transition); // Q-learning or SARSA update rule

    bool saveModel(const std::string& filepath) const;
    bool loadModel(const std::string& filepath);
    void resetWeights(); // Initialize weights to small random values or zero

    void setEpsilon(float epsilon) { m_epsilon = epsilon; }
    float getEpsilon() const { return m_epsilon; }
    void setLearningRate(float alpha) { m_learningRate = alpha; }
    float getLearningRate() const { return m_learningRate; }


private:
    int m_numStateFeatures;
    int m_numActions;
    float m_learningRate;    // Alpha
    float m_discountFactor;  // Gamma
    float m_epsilon;         // For epsilon-greedy exploration

    // For Linear Function Approximation: Q(s,a) = dot(weights_a, features_s)
    // One weight vector per action. Each vector has m_numStateFeatures weights.
    std::vector<std::vector<float>> m_weights;

    // Helper methods
    float getQValue(const BotState& state, BotActionType action) const;
    // int discretizeFeature(float value, int num_bins); // If we were to discretize for Q-table
    // long long calculateStateHash(const BotState& state); // If using Q-table
};

#endif // RCBOT_RL_AGENT_H
