#pragma once

#include "../behaviour_tree/BehaviourTree.hpp"
#include "../behaviour_tree/CompositeNodes.hpp"
#include "../behaviour_tree/EnemyContext.hpp"
#include "../behaviour_tree/LeafNodes.hpp"

#include <cmath>
#include <memory>

namespace ai
{
    inline float ogreDistance(const EnemyContext& context)
    {
        const sf::Vector2f difference = context.playerPosition - context.enemyPosition;
        return std::sqrt(difference.x * difference.x + difference.y * difference.y);
    }

    inline void updateOgreVisibility(EnemyContext& context)
    {
        context.playerVisible = ogreDistance(context) <= context.detectionRange;
    }

    inline void buildOgreBehaviourTree(BehaviourTree& tree)
    {
        auto root = std::make_unique<Selector>();

        // Keep the current attack or recovery phase ahead of Chase.
        auto attackCycleSequence = std::make_unique<Sequence>();
        attackCycleSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context) { return context.attackCycleActive; }));

        auto attackPhase = std::make_unique<Selector>();
        auto recoverySequence = std::make_unique<Sequence>();
        recoverySequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                return context.currentAction == EnemyContext::Action::Recovery;
            }));
        recoverySequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Recovery;
                return NodeStatus::Running;
            }));
        attackPhase->addChild(std::move(recoverySequence));
        attackPhase->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                // The future attack animation will move this phase into Recovery.
                context.currentAction = EnemyContext::Action::HeavyAttack;
                return NodeStatus::Running;
            }));
        attackCycleSequence->addChild(std::move(attackPhase));
        root->addChild(std::move(attackCycleSequence));

        auto heavyAttackSequence = std::make_unique<Sequence>();
        heavyAttackSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context)
            {
                return !context.attackCycleActive && context.playerVisible &&
                    context.hasLineOfSight && ogreDistance(context) <= context.attackRange;
            }));
        heavyAttackSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::HeavyAttack;
                // The future combat code will start and lock the attack cycle.
                return NodeStatus::Success;
            }));
        root->addChild(std::move(heavyAttackSequence));

        auto chaseSequence = std::make_unique<Sequence>();
        chaseSequence->addChild(std::make_unique<ConditionNode>(
            [](EnemyContext& context) { return context.playerVisible; }));
        chaseSequence->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Chase;
                return NodeStatus::Success;
            }));
        root->addChild(std::move(chaseSequence));

        root->addChild(std::make_unique<ActionNode>(
            [](EnemyContext& context, float)
            {
                context.currentAction = EnemyContext::Action::Idle;
                return NodeStatus::Success;
            }));

        tree.setRoot(std::move(root));
    }
}
