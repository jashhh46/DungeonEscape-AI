#pragma once

namespace ai
{
    // Possible results when an AI node is checked
    enum class NodeStatus
    {
        Success,
        Failure,
        Running
    };

    // We will define the enemy information later
    struct EnemyContext;

    // Parent class for all Behaviour Tree nodes
    class BehaviourNode
    {
    public:
        virtual ~BehaviourNode() = default;

        // Every child node will have its own tick logic
        virtual NodeStatus tick(EnemyContext& context,
                                float deltaTime) = 0;
    };
}