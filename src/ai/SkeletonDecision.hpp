#pragma once

#include "BehaviourTree.hpp"
#include "CompositeNodes.hpp"
#include "EnemyContext.hpp"
#include "LeafNodes.hpp"

#include <cmath>
#include <memory>

namespace ai
{
    inline float distanceBetween(const sf::Vector2f& first,
                                 const sf::Vector2f& second)
    {
        const sf::Vector2f difference = first - second;
        return std::sqrt(difference.x * difference.x + difference.y * difference.y);
    }

    inline void updateSkeletonVisibility(EnemyContext& context)
    {
        context.playerVisible = distanceBetween(context.enemyPosition,
                                                context.playerPosition)
            <= context.detectionRange;
    }

    inline void buildSkeletonBehaviourTree(BehaviourTree& tree)
    {
        auto root = std::make_unique<Selector>();

        auto attackSequence = std::make_unique<Sequence>();
        attackSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                return distanceBetween(context.enemyPosition, context.playerPosition)
                    <= context.attackRange;
            }));
        attackSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Attack;
                context.moveDirection = { 0.f, 0.f };
                return NodeStatus::Success;
            }));
        root->addChild(std::move(attackSequence));

        auto chaseSequence = std::make_unique<Sequence>();
        chaseSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context) { return context.playerVisible; }));
        chaseSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Chase;
                context.moveDirection = { 0.f, 0.f };
                return NodeStatus::Success;
            }));
        root->addChild(std::move(chaseSequence));

        root->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Idle;
                context.moveDirection = { 0.f, 0.f };
                return NodeStatus::Success;
            }));

        tree.setRoot(std::move(root));
    }
}
