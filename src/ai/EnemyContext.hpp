#pragma once

#include <SFML/System/Vector2.hpp>

namespace ai
{
    // Information used by an enemy's Behaviour Tree
    struct EnemyContext
    {
        // Current enemy position
        sf::Vector2f enemyPosition{0.f, 0.f};

        // Current player position
        sf::Vector2f playerPosition{0.f, 0.f};

        // Enemy movement settings
        float moveSpeed = 100.f;
        float detectionRange = 250.f;
        float attackRange = 60.f;
        float attackRangeBuffer = 10.f;

        // Can the enemy currently see the player?
        bool playerVisible = false;
        bool hasLineOfSight = false;
        bool attackCycleActive = false;

        // Movement requested by the Behaviour Tree
        sf::Vector2f moveDirection{0.f, 0.f};

        // Current action selected by the AI
        enum class Action
        {
            Idle,
            Patrol,
            Chase,
            Attack,
            Retreat,
            Search,
            Approach,
            RangedAttack,
            Reposition
        };

        Action currentAction = Action::Idle;
    };
}
