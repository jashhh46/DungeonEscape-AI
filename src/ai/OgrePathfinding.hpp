#pragma once

#include "AStarPathfinder.hpp"
#include "EnemyContext.hpp"
#include "GridCollision.hpp"
#include "SkeletonPathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ai
{
    inline constexpr float ogreFootWidth = 32.f;
    inline constexpr float ogreFootHeight = 20.f;
    inline constexpr float ogreNavigationGroundOffset = 104.f;

    struct OgrePathState
    {
        std::vector<GridCell> path;
        std::size_t nextWaypoint = 0;
        GridCell destination{ -1, -1 };
        GridCell lastStart{ -1, -1 };
        bool hasAttempted = false;
        bool pathFailed = false;
        float stuckTime = 0.f;
    };

    template <typename Grid, typename IsCellWalkable, typename CanOccupy>
    sf::Vector2f followOgrePath(EnemyContext& context, OgrePathState& state,
        float deltaTime, const Grid& grid, float tileSize,
        IsCellWalkable isCellWalkable, CanOccupy canOccupy,
        float waypointGroundOffset = ogreNavigationGroundOffset)
    {
        if (context.currentAction != EnemyContext::Action::Chase)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }
        if (deltaTime <= 0.f || grid.empty() || grid[0].empty())
            return { 0.f, 0.f };

        const int rows = static_cast<int>(grid.size());
        const int columns = static_cast<int>(grid[0].size());
        GridCell startCell;
        GridCell goalCell;
        if (!worldToGridCell(context.enemyPosition, tileSize, columns, rows, startCell) ||
            !worldToGridCell(context.playerPosition, tileSize, columns, rows, goalCell))
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

        const bool destinationChanged = !state.hasAttempted || state.destination != goalCell;
        const bool invalidPath = state.hasAttempted && !state.pathFailed && !pathStillValid();
        const bool retryAfterFailure = state.pathFailed && state.lastStart != startCell;
        if (destinationChanged || invalidPath || retryAfterFailure || state.stuckTime >= 0.5f)
        {
            const bool firstAttempt = !state.hasAttempted;
            state.path = findAStarPath(startCell, goalCell, grid, isCellWalkable);
            state.destination = goalCell;
            state.lastStart = startCell;
            state.hasAttempted = true;
            state.pathFailed = state.path.empty();
            state.nextWaypoint = state.path.empty() ? 0 : (firstAttempt ? 0 : 1);
            state.stuckTime = 0.f;
        }

        if (state.pathFailed)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        // Skip cell centres the Ogre has already reached.
        while (state.nextWaypoint < state.path.size())
        {
            const sf::Vector2f waypoint = gridCellWaypoint(
                state.path[state.nextWaypoint], tileSize, waypointGroundOffset);
            const sf::Vector2f difference = waypoint - context.enemyPosition;
            if (difference.x * difference.x + difference.y * difference.y > 9.f)
                break;
            ++state.nextWaypoint;
        }

        sf::Vector2f target{};
        if (state.nextWaypoint < state.path.size())
            target = gridCellWaypoint(state.path[state.nextWaypoint],
                tileSize, waypointGroundOffset);
        else
            target = context.playerPosition;

        const sf::Vector2f start = context.enemyPosition;
        const sf::Vector2f toTarget = target - start;
        const float targetDistance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
        if (targetDistance <= 0.001f)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        sf::Vector2f direction = toTarget / targetDistance;
        float step = context.moveSpeed * deltaTime;
        if (state.nextWaypoint < state.path.size() &&
            std::abs(toTarget.x) > 0.001f && std::abs(toTarget.y) > 0.001f)
        {
            // Keep movement aligned to the four-direction A* path.
            if (std::abs(toTarget.x) >= std::abs(toTarget.y))
                direction = { toTarget.x > 0.f ? 1.f : -1.f, 0.f };
            else
                direction = { 0.f, toTarget.y > 0.f ? 1.f : -1.f };
        }

        step = std::min(step, targetDistance);

        // Do not let the last step carry the Ogre through its HeavyAttack range.
        const sf::Vector2f toPlayer = context.playerPosition - start;
        const float playerDistance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
        if (playerDistance <= context.attackRange)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }
        else
        {
            const float projection = toPlayer.x * direction.x + toPlayer.y * direction.y;
            const float rangeGap = playerDistance * playerDistance -
                context.attackRange * context.attackRange;
            if (projection > 0.f && rangeGap > 0.f)
            {
                const float discriminant = projection * projection - rangeGap;
                if (discriminant >= 0.f)
                    step = std::min(step, projection - std::sqrt(discriminant));
            }
        }

        const sf::Vector2f intended = direction * std::max(0.f, step);
        sf::Vector2f moved{};
        if (canOccupy(start + intended))
            moved = intended;
        else
        {
            const sf::Vector2f horizontal{ start.x + intended.x, start.y };
            if (std::abs(intended.x) > 0.001f && canOccupy(horizontal))
                moved.x = intended.x;
            const sf::Vector2f vertical{ start.x + moved.x, start.y + intended.y };
            if (std::abs(intended.y) > 0.001f && canOccupy(vertical))
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
