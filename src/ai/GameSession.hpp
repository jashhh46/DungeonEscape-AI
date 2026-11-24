#pragma once

#include "EnemyHealth.hpp"
#include "SkeletonCombat.hpp"

#include <array>
#include <algorithm>

namespace ai
{
    struct GameSessionState
    {
        bool gameOver = false;
        bool victory = false;

        template <std::size_t EnemyCount>
        void update(const PlayerHealth& player,
            const std::array<EnemyHealth, EnemyCount>& enemies)
        {
            if (gameOver || victory)
                return;

            if (player.current <= 0)
            {
                gameOver = true;
                return;
            }

            victory = std::all_of(enemies.begin(), enemies.end(),
                [](const EnemyHealth& enemy) { return enemy.isDefeated(); });
        }

        bool isPaused() const
        {
            return gameOver || victory;
        }

        bool canRestart(bool restartPressed) const
        {
            return isPaused() && restartPressed;
        }

        void restart(PlayerHealth& player,
            std::array<EnemyHealth, 3>& enemies)
        {
            player.current = PlayerHealth::maximum;
            enemies = { EnemyHealth{ 45 }, EnemyHealth{ 35 }, EnemyHealth{ 100 } };
            gameOver = false;
            victory = false;
        }
    };
}
