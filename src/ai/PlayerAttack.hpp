#pragma once

#include <algorithm>

namespace ai
{
    inline constexpr int playerAttackFrameCount = 4;
    inline constexpr float playerAttackFramesPerSecond = 8.f;
    inline constexpr int playerAttackImpactFrame = 2;
    inline constexpr float playerAttackDuration =
        playerAttackFrameCount / playerAttackFramesPerSecond;
    inline constexpr float playerAttackRange = 90.f;
    inline constexpr float playerAttackHalfWidth = 36.f;
    inline constexpr int playerAttackDamage = 15;

    struct PlayerAttackState
    {
        bool animationActive = false;
        bool spaceHeld = false;
        float animationTime = 0.f;
        int frame = 0;
        int facingRow = 0;
        bool hitApplied = false;
    };

    struct PlayerAttackUpdate
    {
        bool animationActive = false;
        int frame = 0;
        int facingRow = 0;
        bool damageFrame = false;
    };

    // Start one attack for a fresh Space press.
    inline bool pressPlayerAttack(PlayerAttackState& state, int facingRow)
    {
        if (state.spaceHeld)
            return false;

        state.spaceHeld = true;
        if (state.animationActive)
            return false;

        state.animationActive = true;
        state.animationTime = 0.f;
        state.frame = 0;
        state.facingRow = facingRow;
        state.hitApplied = false;
        return true;
    }

    inline void releasePlayerAttack(PlayerAttackState& state)
    {
        state.spaceHeld = false;
    }

    inline PlayerAttackUpdate updatePlayerAttack(PlayerAttackState& state,
        float deltaTime)
    {
        if (!state.animationActive)
            return {};

        state.animationTime += std::max(0.f, deltaTime);
        const int currentFrame = std::min(static_cast<int>(state.animationTime *
            playerAttackFramesPerSecond), playerAttackFrameCount - 1);
        bool damageFrame = false;
        if (currentFrame >= playerAttackImpactFrame && !state.hitApplied)
        {
            state.hitApplied = true;
            damageFrame = true;
        }
        if (state.animationTime >= playerAttackDuration)
        {
            state.animationActive = false;
            state.animationTime = 0.f;
            state.frame = 0;
            return { false, 0, state.facingRow, damageFrame };
        }

        state.frame = currentFrame;
        return { true, state.frame, state.facingRow, damageFrame };
    }
}
