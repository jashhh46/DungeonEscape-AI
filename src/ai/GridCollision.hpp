#pragma once

#include <SFML/System/Vector2.hpp>

#include <cstddef>

namespace ai
{
    // Checks a bottom-centred foot box against blocked grid tiles.
    template <typename Grid, typename IsWalkable>
    bool isFootprintClear(const sf::Vector2f& groundPosition,
                          float width,
                          float height,
                          const Grid& grid,
                          float tileSize,
                          IsWalkable isWalkable)
    {
        const float left = groundPosition.x - width / 2.f;
        const float top = groundPosition.y - height;
        const float right = left + width;
        const float bottom = groundPosition.y;
        const float worldWidth = static_cast<float>(grid[0].size()) * tileSize;
        const float worldHeight = static_cast<float>(grid.size()) * tileSize;

        if (left < 0.f || top < 0.f || right > worldWidth || bottom > worldHeight)
            return false;

        for (std::size_t row = 0; row < grid.size(); ++row)
        {
            for (std::size_t column = 0; column < grid[row].size(); ++column)
            {
                if (isWalkable(grid[row][column]))
                    continue;

                const float tileLeft = static_cast<float>(column) * tileSize;
                const float tileTop = static_cast<float>(row) * tileSize;
                const float tileRight = tileLeft + tileSize;
                const float tileBottom = tileTop + tileSize;
                const bool overlaps = left < tileRight && right > tileLeft &&
                    top < tileBottom && bottom > tileTop;
                if (overlaps)
                    return false;
            }
        }

        return true;
    }
}
