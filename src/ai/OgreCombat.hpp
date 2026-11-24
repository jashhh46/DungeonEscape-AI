#pragma once

#include "EnemyContext.hpp"
#include "SkeletonCombat.hpp"

#include <algorithm>
#include <cmath>

namespace ai
{
    inline constexpr float ogreHeavyAttackWindup = 0.5f;
    inline constexpr float ogreRecoveryDuration = 1.5f;
    inline constexpr float ogreAttackAnimationDuration = 1.f;
    inline constexpr float ogreAttackFramesPerSecond = 4.f;
    inline constexpr int ogreAttackFrameCount = 4;
    inline constexpr int ogreAttackImpactFrame = 2;
    inline constexpr int ogreHeavyAttackDamage = 20;

    struct OgreCombatState
    {
        enum class Phase
        {
            None,
            Windup,
            Recovery
        };

        Phase phase = Phase::None;
        float elapsed = 0.f;
        float recoveryRemaining = 0.f;
        int frame = 0;
        bool hitApplied = false;
    };

    struct OgreCombatUpdate
    {
        bool animationActive = false;
        bool attackStarted = false;
        bool damageApplied = false;
        int frame = 0;
    };

    inline int ogreFacingRow(sf::Vector2f enemyGround, sf::Vector2f playerGround)
    {
        const sf::Vector2f difference = playerGround - enemyGround;
        if (std::abs(difference.x) > std::abs(difference.y))
            return difference.x > 0.f ? 3 : 2;
        return difference.y > 0.f ? 0 : 1;
    }

    inline OgreCombatUpdate updateOgreCombat(OgreCombatState& state,
        EnemyContext& context, PlayerHealth& player, float deltaTime)
    {
        const float elapsedTime = std::max(0.f, deltaTime);

        if (state.phase == OgreCombatState::Phase::None &&
            context.currentAction == EnemyContext::Action::HeavyAttack)
        {
            state.phase = OgreCombatState::Phase::Windup;
            state.elapsed = 0.f;
            state.hitApplied = false;
            context.attackCycleActive = true;
            context.moveDirection = { 0.f, 0.f };
            // Start the wind-up on the decision frame; do not count that frame twice.
            return { true, true, false, 0 };
        }

        if (state.phase == OgreCombatState::Phase::Windup)
        {
            state.elapsed += elapsedTime;
            state.frame = std::min(static_cast<int>(state.elapsed *
                ogreAttackFramesPerSecond), ogreAttackFrameCount - 1);
            bool damageApplied = false;
            if (state.elapsed >= ogreHeavyAttackWindup && !state.hitApplied)
            {
                state.frame = ogreAttackImpactFrame;
                const sf::Vector2f difference = context.playerPosition - context.enemyPosition;
                const float distance = std::sqrt(difference.x * difference.x +
                    difference.y * difference.y);
                if (distance <= context.attackRange && context.hasLineOfSight)
                    damageApplied = player.takeDamage(ogreHeavyAttackDamage);
                state.hitApplied = true;
            }

            if (state.elapsed >= ogreAttackAnimationDuration)
            {
                state.phase = OgreCombatState::Phase::Recovery;
                state.recoveryRemaining = ogreRecoveryDuration;
                state.frame = 0;
                context.currentAction = EnemyContext::Action::Recovery;
                context.moveDirection = { 0.f, 0.f };
                return { false, false, damageApplied, 0 };
            }
            return { true, false, damageApplied, state.frame };
        }

        if (state.phase == OgreCombatState::Phase::Recovery)
        {
            context.currentAction = EnemyContext::Action::Recovery;
            context.moveDirection = { 0.f, 0.f };
            state.recoveryRemaining = std::max(0.f,
                state.recoveryRemaining - elapsedTime);
            if (state.recoveryRemaining <= 0.f)
            {
                state.phase = OgreCombatState::Phase::None;
                context.attackCycleActive = false;
                state.elapsed = 0.f;
                state.hitApplied = false;
            }
        }

        return {};
    }
}
