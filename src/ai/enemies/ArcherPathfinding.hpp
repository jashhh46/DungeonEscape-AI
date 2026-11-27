#pragma once

#include "../navigation/AStarPathfinder.hpp"
#include "../behaviour_tree/EnemyContext.hpp"
#include "SkeletonPathfinding.hpp"
#include "../navigation/GridCollision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ai
{
    inline constexpr float archerDesiredDistance = 340.f;
    inline constexpr float archerFootWidth = 24.f;
    inline constexpr float archerFootHeight = 16.f;
    inline constexpr float archerNavigationGroundOffset = 104.f;

    struct ArcherPathState
    {
        std::vector<GridCell> path;
        std::size_t nextWaypoint = 0;
        GridCell destination{ -1, -1 };
        GridCell playerCell{ -1, -1 };
        EnemyContext::Action action = EnemyContext::Action::Idle;
        bool hasAttempted = false;
        bool pathFailed = false;
        float stuckTime = 0.f;
    };

    template <typename Grid, typename IsCellWalkable, typename CanOccupy,
              typename HasLineOfSight, typename OnAStarSearch = IgnoreAStarObserver>
    sf::Vector2f moveArcher(EnemyContext& context, ArcherPathState& state,
        float deltaTime, const Grid& grid, float tileSize,
        IsCellWalkable isCellWalkable, CanOccupy canOccupy,
        HasLineOfSight hasLineOfSight, OnAStarSearch onAStarSearch = {})
    {
        const auto action = context.currentAction;
        if (action != EnemyContext::Action::Retreat &&
            action != EnemyContext::Action::Approach &&
            action != EnemyContext::Action::Reposition)
        {
            state = {};
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }
        if (deltaTime <= 0.f || grid.empty() || grid[0].empty())
            return { 0.f, 0.f };

        const int rows = static_cast<int>(grid.size());
        const int columns = static_cast<int>(grid[0].size());
        GridCell startCell;
        GridCell playerCell;
        if (!worldToGridCell(context.enemyPosition, tileSize, columns, rows, startCell) ||
            !worldToGridCell(context.playerPosition, tileSize, columns, rows, playerCell))
        {
            state.path.clear();
            state.pathFailed = true;
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        const auto pathStillValid = [&]
        {
            if (!isPathValid(state.path, isCellWalkable))
                return false;
            for (std::size_t i = state.nextWaypoint; i < state.path.size(); ++i)
            {
                if (!isCellWalkable(state.path[i]))
                    return false;
            }
            return true;
        };

        const bool actionChanged = !state.hasAttempted || state.action != action;
        const bool targetChanged = !state.hasAttempted || state.playerCell != playerCell;
        const bool invalidPath = state.hasAttempted && !state.pathFailed && !pathStillValid();
        if (actionChanged || targetChanged || invalidPath || state.stuckTime >= 0.5f)
        {
            state.path.clear();
            state.nextWaypoint = 0;
            state.pathFailed = false;

            if (action == EnemyContext::Action::Approach)
            {
                state.path = findAStarPathMeasured(startCell, playerCell, grid,
                    isCellWalkable, onAStarSearch);
                state.destination = playerCell;
            }
            else
            {
                // Pick a reachable cell away from the Player or with a clear shot.
                const sf::Vector2f away = context.enemyPosition - context.playerPosition;
                const float awayLength = std::sqrt(away.x * away.x + away.y * away.y);
                const sf::Vector2f awayDirection = awayLength > 0.001f
                    ? away / awayLength : sf::Vector2f{ 1.f, 0.f };
                float bestScore = std::numeric_limits<float>::max();
                for (int row = 0; row < rows; ++row)
                {
                    for (int column = 0; column < columns; ++column)
                    {
                        const GridCell candidate{ column, row };
                        if (!isCellWalkable(candidate))
                            continue;
                        const sf::Vector2f goal = gridCellWaypoint(
                            candidate, tileSize, archerNavigationGroundOffset);
                        const sf::Vector2f fromPlayer = goal - context.playerPosition;
                        const float distance = std::sqrt(fromPlayer.x * fromPlayer.x +
                            fromPlayer.y * fromPlayer.y);

                        float score = 0.f;
                        if (action == EnemyContext::Action::Retreat)
                        {
                            if (distance < 210.f || distance > 430.f)
                                continue;
                            const sf::Vector2f fromEnemy = goal - context.enemyPosition;
                            const float length = std::sqrt(fromEnemy.x * fromEnemy.x +
                                fromEnemy.y * fromEnemy.y);
                            const float awayAlignment = length > 0.001f
                                ? (fromEnemy.x * awayDirection.x +
                                   fromEnemy.y * awayDirection.y) / length : -1.f;
                            if (awayAlignment < 0.15f)
                                continue;
                            score = std::abs(distance - 300.f) / tileSize - awayAlignment;
                        }
                        else
                        {
                            if (distance < archerRetreatRange ||
                                distance > archerPreferredAttackRange ||
                                !hasLineOfSight(goal, context.playerPosition))
                                continue;
                            score = std::abs(distance - archerDesiredDistance) / tileSize;
                        }

                        const auto candidatePath = findAStarPathMeasured(
                            startCell, candidate, grid, isCellWalkable, onAStarSearch);
                        if (candidatePath.empty())
                            continue;
                        score += static_cast<float>(candidatePath.size()) * 0.01f;
                        if (score < bestScore)
                        {
                            bestScore = score;
                            state.path = candidatePath;
                            state.destination = candidate;
                        }
                    }
                }
            }

            state.action = action;
            state.playerCell = playerCell;
            state.hasAttempted = true;
            state.pathFailed = state.path.empty();
            state.nextWaypoint = state.path.empty() ? 0 : (state.path.size() > 1 ? 1 : 0);
            state.stuckTime = 0.f;
        }

        if (state.pathFailed)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        while (state.nextWaypoint < state.path.size())
        {
            const sf::Vector2f waypoint = gridCellWaypoint(state.path[state.nextWaypoint],
                tileSize, archerNavigationGroundOffset);
            const sf::Vector2f difference = waypoint - context.enemyPosition;
            if (difference.x * difference.x + difference.y * difference.y > 9.f)
                break;
            ++state.nextWaypoint;
        }

        sf::Vector2f target{};
        float stopDistance = 0.f;
        if (state.nextWaypoint < state.path.size())
            target = gridCellWaypoint(state.path[state.nextWaypoint],
                tileSize, archerNavigationGroundOffset);
        else if (action == EnemyContext::Action::Approach)
        {
            target = context.playerPosition;
            stopDistance = archerDesiredDistance;
        }
        else
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        const sf::Vector2f toTarget = target - context.enemyPosition;
        const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
        if (distance <= stopDistance || distance <= 0.001f)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        sf::Vector2f direction = toTarget / distance;
        float step = std::min(context.moveSpeed * deltaTime, distance - stopDistance);
        if (state.nextWaypoint < state.path.size() &&
            std::abs(toTarget.x) > 0.001f && std::abs(toTarget.y) > 0.001f)
        {
            if (std::abs(toTarget.x) >= std::abs(toTarget.y))
                direction = { toTarget.x > 0.f ? 1.f : -1.f, 0.f };
            else
                direction = { 0.f, toTarget.y > 0.f ? 1.f : -1.f };
        }

        const sf::Vector2f start = context.enemyPosition;
        sf::Vector2f intended = direction * step;
        if (action == EnemyContext::Action::Approach)
        {
            // Clip this step at the preferred distance so we do not overshoot.
            const sf::Vector2f fromPlayer = start - context.playerPosition;
            const float projection = fromPlayer.x * direction.x + fromPlayer.y * direction.y;
            const float c = fromPlayer.x * fromPlayer.x + fromPlayer.y * fromPlayer.y -
                archerDesiredDistance * archerDesiredDistance;
            const float discriminant = projection * projection - c;
            if (projection < 0.f && discriminant >= 0.f)
                step = std::min(step, -projection - std::sqrt(discriminant));
            intended = direction * std::max(0.f, step);
        }

        sf::Vector2f moved{};
        if (canOccupy(start + intended))
            moved = intended;
        else
        {
            if (std::abs(intended.x) > 0.f && canOccupy({ start.x + intended.x, start.y }))
                moved.x = intended.x;
            if (std::abs(intended.y) > 0.f && canOccupy({ start.x + moved.x, start.y + intended.y }))
                moved.y = intended.y;
        }

        context.enemyPosition += moved;
        const float movedLength = std::sqrt(moved.x * moved.x + moved.y * moved.y);
        if (movedLength > 0.01f)
        {
            context.moveDirection = moved / movedLength;
            state.stuckTime = 0.f;
        }
        else
        {
            context.moveDirection = { 0.f, 0.f };
            state.stuckTime += deltaTime;
        }
        return moved;
    }
}
