#pragma once

#include "BehaviourTree.hpp"
#include "CompositeNodes.hpp"
#include "EnemyContext.hpp"
#include "LeafNodes.hpp"

#include <cmath>
#include <memory>

namespace ai
{
    inline constexpr float archerRetreatRange = 180.f;
    inline constexpr float archerPreferredAttackRange = 450.f;

    inline float archerDistance(const EnemyContext& context)
    {
        const sf::Vector2f difference = context.playerPosition - context.enemyPosition;
        return std::sqrt(difference.x * difference.x + difference.y * difference.y);
    }

    inline void updateArcherVisibility(EnemyContext& context)
    {
        context.playerVisible = archerDistance(context) <= context.detectionRange;
    }

    inline void buildArcherBehaviourTree(BehaviourTree& tree)
    {
        auto root = std::make_unique<Selector>();

        auto retreatSequence = std::make_unique<Sequence>();
        retreatSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                return context.playerVisible &&
                    archerDistance(context) < archerRetreatRange;
            }));
        retreatSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Retreat;
                return NodeStatus::Success;
            }));
        root->addChild(std::move(retreatSequence));

        auto rangedAttackSequence = std::make_unique<Sequence>();
        rangedAttackSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                const float distance = archerDistance(context);
                return context.playerVisible && context.hasLineOfSight &&
                    distance >= archerRetreatRange &&
                    distance <= archerPreferredAttackRange;
            }));
        rangedAttackSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::RangedAttack;
                return NodeStatus::Success;
            }));
        root->addChild(std::move(rangedAttackSequence));

        auto repositionSequence = std::make_unique<Sequence>();
        repositionSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                const float distance = archerDistance(context);
                return context.playerVisible && !context.hasLineOfSight &&
                    distance >= archerRetreatRange &&
                    distance <= archerPreferredAttackRange;
            }));
        repositionSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Reposition;
                return NodeStatus::Success;
            }));
        root->addChild(std::move(repositionSequence));

        auto approachSequence = std::make_unique<Sequence>();
        approachSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                return context.playerVisible &&
                    archerDistance(context) > archerPreferredAttackRange;
            }));
        approachSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Approach;
                return NodeStatus::Success;
            }));
        root->addChild(std::move(approachSequence));

        root->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Idle;
                return NodeStatus::Success;
            }));

        tree.setRoot(std::move(root));
    }
}
