#pragma once

#include "BehaviourNode.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ai
{
    // Selector tries children until one succeeds or is running
    class Selector : public BehaviourNode
    {
    public:
        // Add a child node
        void addChild(std::unique_ptr<BehaviourNode> child)
        {
            children_.push_back(std::move(child));
        }

        NodeStatus tick(EnemyContext& context, float deltaTime) override
        {
            // Check each child in priority order
            for (const auto& child : children_)
            {
                NodeStatus result = child->tick(context, deltaTime);

                if (result == NodeStatus::Success)
                    return NodeStatus::Success;

                if (result == NodeStatus::Running)
                    return NodeStatus::Running;

                // If Failure, try the next child
            }

            // All children failed
            return NodeStatus::Failure;
        }

    private:
        std::vector<std::unique_ptr<BehaviourNode>> children_;
    };


    // Sequence runs children until one fails or is running
    class Sequence : public BehaviourNode
    {
    public:
        // Add a child node
        void addChild(std::unique_ptr<BehaviourNode> child)
        {
            children_.push_back(std::move(child));
        }

        NodeStatus tick(EnemyContext& context, float deltaTime) override
        {
            // Check each child in order
            for (const auto& child : children_)
            {
                NodeStatus result = child->tick(context, deltaTime);

                if (result == NodeStatus::Failure)
                    return NodeStatus::Failure;

                if (result == NodeStatus::Running)
                    return NodeStatus::Running;

                // If Success, continue to the next child
            }

            // All children succeeded
            return NodeStatus::Success;
        }

    private:
        std::vector<std::unique_ptr<BehaviourNode>> children_;
    };
}