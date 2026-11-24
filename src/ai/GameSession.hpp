#pragma once

#include "EnemyHealth.hpp"
#include "SkeletonCombat.hpp"

#include <array>

namespace ai
{
    struct GameSessionState
    {
        bool gameOver = false;

        void update(const PlayerHealth& player)
        {
            if (player.current <= 0)
                gameOver = true;
        }

        bool canRestart(bool restartPressed) const
        {
            return gameOver && restartPressed;
        }

        void restart(PlayerHealth& player,
            std::array<EnemyHealth, 3>& enemies)
        {
            player.current = PlayerHealth::maximum;
            enemies = { EnemyHealth{ 45 }, EnemyHealth{ 35 }, EnemyHealth{ 100 } };
            gameOver = false;
        }
    };
}
