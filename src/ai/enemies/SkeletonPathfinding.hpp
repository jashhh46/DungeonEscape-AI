#pragma once

#include "../navigation/AStarPathfinder.hpp"
#include "../behaviour_tree/EnemyContext.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ai
{
    inline constexpr float skeletonMoveSpeed = 100.f * 4.0f / 3.0f;

    struct SkeletonPathState
    {
        std::vector<GridCell> path;
        std::size_t nextWaypoint = 0;
        GridCell destination{ -1, -1 };
        GridCell lastStart{ -1, -1 };
        bool hasAttempted = false;
        bool pathFailed = false;
        float stuckTime = 0.f;
    };

    inline bool worldToGridCell(sf::Vector2f position, float tileSize,
        int columns, int rows, GridCell& cell)
    {
        if (tileSize <= 0.f || position.x < 0.f || position.y < 0.f ||
            position.x >= columns * tileSize || position.y >= rows * tileSize)
            return false;
        cell = { static_cast<int>(position.x / tileSize),
            static_cast<int>(position.y / tileSize) };
        return true;
    }

    inline sf::Vector2f gridCellCenter(GridCell cell, float tileSize)
    {
        return { (static_cast<float>(cell.column) + 0.5f) * tileSize,
            (static_cast<float>(cell.row) + 0.5f) * tileSize };
    }

    inline sf::Vector2f gridCellWaypoint(GridCell cell, float tileSize,
        float groundOffset = -1.f)
    {
        const float y = groundOffset < 0.f
            ? (static_cast<float>(cell.row) + 0.5f) * tileSize
            : static_cast<float>(cell.row) * tileSize + groundOffset;
        return { (static_cast<float>(cell.column) + 0.5f) * tileSize, y };
    }

    template <typename Grid, typename IsCellWalkable, typename CanOccupy>
    sf::Vector2f followSkeletonPath(EnemyContext& context, SkeletonPathState& state,
        float deltaTime, const Grid& grid, float tileSize,
        IsCellWalkable isCellWalkable, CanOccupy canOccupy,
        float waypointGroundOffset = -1.f)
    {
        if (context.currentAction != EnemyContext::Action::Chase)
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
        GridCell goalCell;
        if (!worldToGridCell(context.enemyPosition, tileSize, columns, rows, startCell) ||
            !worldToGridCell(context.playerPosition, tileSize, columns, rows, goalCell))
        {
            state.path.clear();
            state.pathFailed = true;
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        const auto remainingPathWalkable = [&]
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
        const bool invalidPath = !state.pathFailed && state.hasAttempted &&
            !remainingPathWalkable();
        const bool retryAfterFailure = state.pathFailed && state.lastStart != startCell;
        const bool stuck = state.stuckTime >= 0.5f;
        if (destinationChanged || invalidPath || retryAfterFailure || stuck)
        {
            const bool firstAttempt = !state.hasAttempted;
            state.path = findAStarPath(startCell, goalCell, grid, isCellWalkable);
            state.destination = goalCell;
            state.lastStart = startCell;
            state.hasAttempted = true;
            state.pathFailed = state.path.empty();
            // Replans continue toward the next cell instead of backing up to this cell's centre.
            state.nextWaypoint = state.path.empty() ? 0 : (firstAttempt ? 0 : 1);
            state.stuckTime = 0.f;
        }

        if (state.pathFailed)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        // Skip waypoints already reached, including the start cell.
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
        float minimumTargetDistance = 0.f;
        if (state.nextWaypoint < state.path.size())
            target = gridCellWaypoint(state.path[state.nextWaypoint], tileSize,
                waypointGroundOffset);
        else
        {
            target = context.playerPosition;
            minimumTargetDistance = context.attackRange;
        }

        const sf::Vector2f startPosition = context.enemyPosition;
        const sf::Vector2f toTarget = target - startPosition;
        const float targetDistance = std::sqrt(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
        if (targetDistance <= minimumTargetDistance || targetDistance <= 0.001f)
        {
            context.moveDirection = { 0.f, 0.f };
            return { 0.f, 0.f };
        }

        sf::Vector2f direction = toTarget / targetDistance;
        float step = std::min(context.moveSpeed * deltaTime,
            targetDistance - minimumTargetDistance);
        if (state.nextWaypoint >= state.path.size() ||
            (std::abs(toTarget.x) > 0.001f && std::abs(toTarget.y) > 0.001f))
        {
            // Handle changed targets without introducing a diagonal step.
            if (std::abs(toTarget.x) >= std::abs(toTarget.y))
            {
                direction = { toTarget.x >= 0.f ? 1.f : -1.f, 0.f };
                step = std::min(step, std::abs(toTarget.x));
            }
            else
            {
                direction = { 0.f, toTarget.y >= 0.f ? 1.f : -1.f };
                step = std::min(step, std::abs(toTarget.y));
            }
        }

        // Never let a waypoint step carry the Skeleton inside attack range.
        const sf::Vector2f toPlayer = context.playerPosition - startPosition;
        const float playerDistance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
        const float projection = toPlayer.x * direction.x + toPlayer.y * direction.y;
        const float rangeGap = playerDistance * playerDistance -
            context.attackRange * context.attackRange;
        if (projection > 0.f && rangeGap > 0.f)
        {
            const float discriminant = projection * projection - rangeGap;
            if (discriminant >= 0.f)
                step = std::min(step, projection - std::sqrt(discriminant));
        }

        const sf::Vector2f intended = direction * std::max(0.f, step);
        sf::Vector2f moved{};
        const sf::Vector2f fullCandidate = startPosition + intended;
        if (canOccupy(fullCandidate))
            moved = intended;
        else
        {
            sf::Vector2f candidate{ startPosition.x + intended.x, startPosition.y };
            if (canOccupy(candidate))
                moved.x = intended.x;
            candidate = { startPosition.x + moved.x, startPosition.y + intended.y };
            if (canOccupy(candidate))
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
