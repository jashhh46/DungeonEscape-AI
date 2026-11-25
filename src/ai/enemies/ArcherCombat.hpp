#pragma once

#include "SkeletonCombat.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ai
{
    inline constexpr float archerArrowSpeed = 400.f;
    inline constexpr float archerAttackCooldown = 1.5f;
    inline constexpr float archerAttackFramesPerSecond = 8.f;
    inline constexpr int archerAttackFrameCount = 4;
    inline constexpr int archerArrowReleaseFrame = 2;
    inline constexpr int archerArrowDamage = 8;
    inline constexpr float archerArrowLifetime = 4.f;

    struct ArcherAttackState
    {
        bool animationActive = false;
        bool projectileReleased = false;
        float animationTime = 0.f;
        float cooldownRemaining = 0.f;
        int frame = 0;
    };

    struct ArcherAttackUpdate
    {
        bool animationActive = false;
        bool releaseProjectile = false;
        int frame = 0;
    };

    inline ArcherAttackUpdate updateArcherAttack(ArcherAttackState& state,
        bool attackSelected, float deltaTime)
    {
        if (deltaTime > 0.f)
            state.cooldownRemaining = std::max(0.f, state.cooldownRemaining - deltaTime);

        if (!attackSelected)
        {
            state.animationActive = false;
            state.animationTime = 0.f;
            state.projectileReleased = false;
            state.frame = 0;
            return {};
        }

        if (!state.animationActive && state.cooldownRemaining <= 0.f)
        {
            state.animationActive = true;
            state.animationTime = 0.f;
            state.projectileReleased = false;
            state.cooldownRemaining = archerAttackCooldown;
        }
        if (!state.animationActive)
            return {};

        state.animationTime += std::max(0.f, deltaTime);
        state.frame = std::min(static_cast<int>(state.animationTime *
            archerAttackFramesPerSecond), archerAttackFrameCount - 1);
        bool releaseProjectile = false;
        if (state.frame >= archerArrowReleaseFrame && !state.projectileReleased)
        {
            state.projectileReleased = true;
            releaseProjectile = true;
        }

        const bool stillActive = state.animationTime <
            archerAttackFrameCount / archerAttackFramesPerSecond;
        if (!stillActive)
        {
            state.animationActive = false;
            state.animationTime = 0.f;
            state.frame = 0;
        }
        return { stillActive, releaseProjectile, state.frame };
    }

    struct ArcherProjectile
    {
        sf::Vector2f position{};
        sf::Vector2f velocity{};
        float lifetimeRemaining = archerArrowLifetime;
        bool active = false;
    };

    inline ArcherProjectile makeArcherProjectile(sf::Vector2f origin,
        sf::Vector2f target, float speed = archerArrowSpeed)
    {
        const sf::Vector2f difference = target - origin;
        const float distance = std::sqrt(difference.x * difference.x +
            difference.y * difference.y);
        if (distance <= 0.001f || speed <= 0.f)
            return {};
        return { origin, (difference / distance) * speed, archerArrowLifetime, true };
    }

    inline bool arrowOverlapsPlayer(sf::Vector2f arrowPosition,
        sf::Vector2f playerGround, float playerWidth, float playerHeight,
        float arrowRadius = 5.f)
    {
        const float left = playerGround.x - playerWidth / 2.f;
        const float top = playerGround.y - playerHeight;
        const float right = left + playerWidth;
        const float bottom = playerGround.y;
        const float closestX = std::clamp(arrowPosition.x, left, right);
        const float closestY = std::clamp(arrowPosition.y, top, bottom);
        const sf::Vector2f difference = arrowPosition - sf::Vector2f{ closestX, closestY };
        return difference.x * difference.x + difference.y * difference.y <=
            arrowRadius * arrowRadius;
    }

    // Sweep projectiles in short steps to avoid passing through thin obstacles.
    template <typename CanOccupy>
    int updateArcherProjectiles(std::vector<ArcherProjectile>& projectiles,
        float deltaTime, sf::Vector2f playerGround, float playerWidth,
        float playerHeight, PlayerHealth& player, CanOccupy canOccupy)
    {
        if (deltaTime <= 0.f)
            return 0;

        int hits = 0;
        for (ArcherProjectile& projectile : projectiles)
        {
            if (!projectile.active)
                continue;

            const float activeTime = std::min(deltaTime, projectile.lifetimeRemaining);
            const float travelDistance = std::sqrt(projectile.velocity.x * projectile.velocity.x +
                projectile.velocity.y * projectile.velocity.y) * activeTime;
            const int steps = std::max(1, static_cast<int>(std::ceil(travelDistance / 8.f)));
            const float stepTime = activeTime / steps;
            for (int step = 0; step < steps && projectile.active; ++step)
            {
                const sf::Vector2f next = projectile.position + projectile.velocity * stepTime;
                if (!canOccupy(next))
                {
                    projectile.active = false;
                    break;
                }
                projectile.position = next;
                if (arrowOverlapsPlayer(projectile.position, playerGround,
                    playerWidth, playerHeight))
                {
                    if (player.takeDamage(archerArrowDamage))
                        ++hits;
                    projectile.active = false;
                }
            }
            projectile.lifetimeRemaining -= deltaTime;
            if (projectile.lifetimeRemaining <= 0.f)
                projectile.active = false;
        }
        return hits;
    }
}
