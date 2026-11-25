#pragma once

#include <algorithm>

namespace ai
{
    struct PlayerHealth
    {
        static constexpr int maximum = 100;
        int current = maximum;

        bool takeDamage(int amount)
        {
            if (amount <= 0 || current <= 0)
                return false;
            current = std::max(0, current - amount);
            return true;
        }
    };

    struct SkeletonAttackState
    {
        bool animationActive = false;
        bool hitApplied = false;
        float animationTime = 0.f;
        float cooldownRemaining = 0.f;
        int frame = 0;
    };

    struct SkeletonAttackUpdate
    {
        bool animationActive = false;
        bool damageApplied = false;
        int frame = 0;
    };

    inline SkeletonAttackUpdate updateSkeletonAttack(SkeletonAttackState& state,
        PlayerHealth& player, bool attackSelected, float distanceToPlayer,
        float attackRange, bool hasLineOfSight, float deltaTime)
    {
        if (deltaTime > 0.f)
            state.cooldownRemaining = std::max(0.f, state.cooldownRemaining - deltaTime);

        if (!attackSelected || player.current <= 0)
        {
            state.animationActive = false;
            state.animationTime = 0.f;
            state.hitApplied = false;
            state.frame = 0;
            return {};
        }

        constexpr int frameCount = 4;
        constexpr int hitFrame = 2;
        constexpr float framesPerSecond = 8.f;
        constexpr float cooldownSeconds = 1.f;
        constexpr int damage = 10;

        if (!state.animationActive && state.cooldownRemaining <= 0.f &&
            distanceToPlayer <= attackRange && hasLineOfSight)
        {
            state.animationActive = true;
            state.animationTime = 0.f;
            state.hitApplied = false;
        }

        if (!state.animationActive)
            return {};

        state.animationTime += std::max(0.f, deltaTime);
        state.frame = std::min(static_cast<int>(state.animationTime * framesPerSecond),
            frameCount - 1);
        bool damageApplied = false;
        if (state.frame >= hitFrame && !state.hitApplied)
        {
            if (distanceToPlayer <= attackRange && hasLineOfSight)
                damageApplied = player.takeDamage(damage);
            state.hitApplied = true;
            state.cooldownRemaining = cooldownSeconds;
        }

        const bool stillActive = state.animationTime < frameCount / framesPerSecond;
        if (!stillActive)
        {
            state.animationActive = false;
            state.animationTime = 0.f;
            state.frame = 0;
        }
        return { stillActive, damageApplied, state.frame };
    }
}
