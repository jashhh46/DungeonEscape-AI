#pragma once

#include "EnemyHealth.hpp"
#include "PlayerAttack.hpp"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cmath>

namespace ai
{
    inline bool playerAttackOverlapsEnemy(sf::Vector2f playerGround,
        float playerWidth, float playerHeight, sf::Vector2f enemyGround,
        float enemyWidth, float enemyHeight, int facingRow,
        float attackRange = playerAttackRange)
    {
        if (facingRow < 0 || facingRow > 3)
            return false;

        // Use the centres of the gameplay foot boxes, not sprite frames.
        const sf::Vector2f playerCenter{ playerGround.x, playerGround.y - playerHeight / 2.f };
        const sf::Vector2f enemyCenter{ enemyGround.x, enemyGround.y - enemyHeight / 2.f };
        const sf::Vector2f difference = enemyCenter - playerCenter;

        const float left = enemyCenter.x - enemyWidth / 2.f;
        const float top = enemyCenter.y - enemyHeight / 2.f;
        const sf::Vector2f closest{
            std::clamp(playerCenter.x, left, left + enemyWidth),
            std::clamp(playerCenter.y, top, top + enemyHeight)
        };
        const sf::Vector2f toHitbox = closest - playerCenter;
        if (toHitbox.x * toHitbox.x + toHitbox.y * toHitbox.y > attackRange * attackRange)
            return false;

        sf::Vector2f forward{};
        switch (facingRow)
        {
        case 0: forward = { 0.f, 1.f }; break;  // Down
        case 1: forward = { 0.f, -1.f }; break; // Up
        case 2: forward = { -1.f, 0.f }; break; // Left
        case 3: forward = { 1.f, 0.f }; break;  // Right
        }

        const float centerDistance = std::sqrt(
            difference.x * difference.x + difference.y * difference.y);
        const float forwardDistance = difference.x * forward.x + difference.y * forward.y;
        const sf::Vector2f sideways{ -forward.y, forward.x };
        const float lateralDistance = std::abs(
            difference.x * sideways.x + difference.y * sideways.y);
        const float playerHalfWidth = (facingRow == 0 || facingRow == 1)
            ? playerWidth / 2.f : playerHeight / 2.f;
        const float enemyHalfWidth = (facingRow == 0 || facingRow == 1)
            ? enemyWidth / 2.f : enemyHeight / 2.f;

        // Keep a wide forward swing area while excluding enemies behind the Player.
        return (centerDistance <= 0.001f ||
                forwardDistance >= centerDistance * 0.5f) &&
            lateralDistance <= playerAttackHalfWidth + playerHalfWidth + enemyHalfWidth;
    }

    inline bool applyPlayerAttackDamage(const PlayerAttackUpdate& attack,
        EnemyHealth& enemy, sf::Vector2f playerGround, float playerWidth,
        float playerHeight, sf::Vector2f enemyGround, float enemyWidth,
        float enemyHeight, bool clearLineOfSight)
    {
        if (!attack.damageFrame || !clearLineOfSight || enemy.isDefeated())
            return false;
        if (!playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
            enemyGround, enemyWidth, enemyHeight, attack.facingRow))
            return false;
        return enemy.takeDamage(playerAttackDamage);
    }
}
