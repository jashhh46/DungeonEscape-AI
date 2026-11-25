#pragma once

#include "BehaviourNode.hpp"
#include "EnemyContext.hpp"

#include <functional>
#include <utility>

namespace ai
{
    // Checks if a condition is true or false
    class ConditionNode : public BehaviourNode
    {
    public:
        using ConditionFunction = std::function<bool(EnemyContext&)>;

        explicit ConditionNode(ConditionFunction function)
            : condition_(std::move(function))
        {
        }

        NodeStatus tick(EnemyContext& context, float deltaTime) override
        {
            // Conditions do not need delta time
            (void)deltaTime;

            if (condition_(context))
                return NodeStatus::Success;

            return NodeStatus::Failure;
        }

    private:
        ConditionFunction condition_;
    };

    // Runs an action selected by the Behaviour Tree
    class ActionNode : public BehaviourNode
    {
    public:
        using ActionFunction =
            std::function<NodeStatus(EnemyContext&, float)>;

        explicit ActionNode(ActionFunction function)
            : action_(std::move(function))
        {
        }

        NodeStatus tick(EnemyContext& context, float deltaTime) override
        {
            // Run the action and return its result
            return action_(context, deltaTime);
        }

    private:
        ActionFunction action_;
    };
}
