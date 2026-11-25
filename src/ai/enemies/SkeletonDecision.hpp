#pragma once

#include "../behaviour_tree/BehaviourTree.hpp"
#include "../behaviour_tree/CompositeNodes.hpp"
#include "../behaviour_tree/EnemyContext.hpp"
#include "../behaviour_tree/LeafNodes.hpp"

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
                const float range = context.attackRange +
                    (context.currentAction == EnemyContext::Action::Attack &&
                        context.attackCycleActive
                        ? context.attackRangeBuffer : 0.f);
                return context.hasLineOfSight &&
                    distanceBetween(context.enemyPosition, context.playerPosition) <= range;
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
                const sf::Vector2f difference = context.playerPosition - context.enemyPosition;
                const float distance = std::sqrt(
                    difference.x * difference.x + difference.y * difference.y);
                context.moveDirection = distance > 0.f
                    ? difference / distance
                    : sf::Vector2f{ 0.f, 0.f };
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

    template <typename CanOccupy>
    inline sf::Vector2f moveSkeleton(EnemyContext& context,
                                    float deltaTime,
                                    CanOccupy canOccupy)
    {
        if (context.currentAction != EnemyContext::Action::Chase || deltaTime <= 0.f)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        const sf::Vector2f start = context.enemyPosition;
        const sf::Vector2f movement = context.moveDirection * context.moveSpeed * deltaTime;
        sf::Vector2f candidate = start;

        candidate.x += movement.x;
        if (canOccupy(candidate))
            context.enemyPosition.x = candidate.x;

        candidate = context.enemyPosition;
        candidate.y += movement.y;
        if (canOccupy(candidate))
            context.enemyPosition.y = candidate.y;

        return context.enemyPosition - start;
    }
}
