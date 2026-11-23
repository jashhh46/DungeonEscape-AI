#pragma once

#include "BehaviourNode.hpp"

#include <memory>
#include <utility>

namespace ai
{
    // Manages and runs the Behaviour Tree
    class BehaviourTree
    {
    public:
        // Set the main node of the tree
        void setRoot(std::unique_ptr<BehaviourNode> root)
        {
            root_ = std::move(root);
        }

        // Run the Behaviour Tree
        NodeStatus tick(EnemyContext& context, float deltaTime)
        {
            // Check if the root node exists
            if (!root_)
                return NodeStatus::Failure;

            // Start checking the tree from the root
            return root_->tick(context, deltaTime);
        }

    private:
        // Stores the root node
        std::unique_ptr<BehaviourNode> root_;
    };
}