#pragma once

#include <algorithm>

namespace ai
{
    struct EnemyHealth
    {
        explicit EnemyHealth(int maxHealth = 100)
            : maximum(maxHealth), current(maxHealth)
        {
        }

        int maximum;
        int current;

        bool isDefeated() const
        {
            return current <= 0;
        }

        bool takeDamage(int amount)
        {
            if (amount <= 0 || isDefeated())
                return false;
            current = std::max(0, current - amount);
            return true;
        }
    };
}
