#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <queue>
#include <vector>

namespace ai
{
    struct IgnoreAStarObserver
    {
        void operator()(bool, std::uint64_t) const {}
    };

    struct GridCell
    {
        int column = 0;
        int row = 0;
    };

    inline bool operator==(GridCell first, GridCell second)
    {
        return first.column == second.column && first.row == second.row;
    }

    inline bool operator!=(GridCell first, GridCell second)
    {
        return !(first == second);
    }

    inline int manhattanDistance(GridCell first, GridCell second)
    {
        return std::abs(first.column - second.column) + std::abs(first.row - second.row);
    }

    template <typename Grid, typename IsWalkable>
    std::vector<GridCell> findAStarPath(GridCell start, GridCell goal,
        const Grid& grid, IsWalkable isWalkable)
    {
        const int rows = static_cast<int>(grid.size());
        if (rows == 0)
            return {};
        const int columns = static_cast<int>(grid[0].size());
        const auto insideGrid = [=](GridCell cell)
        {
            return cell.column >= 0 && cell.column < columns &&
                cell.row >= 0 && cell.row < rows;
        };
        if (!insideGrid(start) || !insideGrid(goal) ||
            !isWalkable(start) || !isWalkable(goal))
            return {};

        const int cellCount = rows * columns;
        const int startIndex = start.row * columns + start.column;
        const int goalIndex = goal.row * columns + goal.column;
        const int noParent = -1;
        std::vector<int> cost(cellCount, std::numeric_limits<int>::max());
        std::vector<int> parent(cellCount, noParent);

        struct OpenCell
        {
            int index;
            int estimatedTotal;
        };
        struct HigherCostFirst
        {
            bool operator()(const OpenCell& first, const OpenCell& second) const
            {
                return first.estimatedTotal > second.estimatedTotal;
            }
        };
        std::priority_queue<OpenCell, std::vector<OpenCell>, HigherCostFirst> open;
        cost[startIndex] = 0;
        open.push({ startIndex, manhattanDistance(start, goal) });

        constexpr std::array<GridCell, 4> directions = {{
            { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }
        }};
        while (!open.empty())
        {
            const int currentIndex = open.top().index;
            open.pop();
            if (currentIndex == goalIndex)
                break;

            const GridCell current{ currentIndex % columns, currentIndex / columns };
            for (const GridCell direction : directions)
            {
                const GridCell next{ current.column + direction.column,
                    current.row + direction.row };
                if (!insideGrid(next) || !isWalkable(next))
                    continue;

                const int nextIndex = next.row * columns + next.column;
                const int nextCost = cost[currentIndex] + 1;
                if (nextCost >= cost[nextIndex])
                    continue;

                cost[nextIndex] = nextCost;
                parent[nextIndex] = currentIndex;
                open.push({ nextIndex, nextCost + manhattanDistance(next, goal) });
            }
        }

        if (startIndex != goalIndex && parent[goalIndex] == noParent)
            return {};

        std::vector<GridCell> path;
        for (int index = goalIndex; index != noParent; index = parent[index])
        {
            path.push_back({ index % columns, index / columns });
            if (index == startIndex)
                break;
        }
        if (path.empty() || path.back() != start)
            return {};
        std::reverse(path.begin(), path.end());
        return path;
    }

    template <typename Grid, typename IsWalkable, typename Observer>
    std::vector<GridCell> findAStarPathMeasured(GridCell start, GridCell goal,
        const Grid& grid, IsWalkable isWalkable, Observer observer)
    {
        const auto startTime = std::chrono::steady_clock::now();
        std::vector<GridCell> path = findAStarPath(start, goal, grid, isWalkable);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        observer(!path.empty(), static_cast<std::uint64_t>(elapsed));
        return path;
    }

    template <typename IsWalkable>
    bool isPathValid(const std::vector<GridCell>& path, IsWalkable isWalkable)
    {
        if (path.empty())
            return false;
        for (std::size_t i = 0; i < path.size(); ++i)
        {
            if (!isWalkable(path[i]))
                return false;
            if (i > 0 && manhattanDistance(path[i - 1], path[i]) != 1)
                return false;
        }
        return true;
    }
}
