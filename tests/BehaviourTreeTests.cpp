#include "ai/BehaviourTree.hpp"
#include "ai/ArcherDecision.hpp"
#include "ai/ArcherCombat.hpp"
#include "ai/ArcherPathfinding.hpp"
#include "ai/AStarPathfinder.hpp"
#include "ai/CompositeNodes.hpp"
#include "ai/EnemyContext.hpp"
#include "ai/GridCollision.hpp"
#include "ai/LeafNodes.hpp"
#include "ai/OgreDecision.hpp"
#include "ai/OgrePathfinding.hpp"
#include "ai/OgreCombat.hpp"
#include "ai/PlayerAttack.hpp"
#include "ai/PlayerCombat.hpp"
#include "ai/EnemyHealth.hpp"
#include "ai/GameSession.hpp"
#include "ai/SkeletonCombat.hpp"
#include "ai/SkeletonDecision.hpp"
#include "ai/SkeletonPathfinding.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class PresetNode final : public ai::BehaviourNode
    {
    public:
        PresetNode(ai::NodeStatus result, int id, std::vector<int>& calls)
            : result_(result), id_(id), calls_(calls)
        {
        }

        ai::NodeStatus tick(ai::EnemyContext&, float) override
        {
            calls_.push_back(id_);
            return result_;
        }

    private:
        ai::NodeStatus result_;
        int id_;
        std::vector<int>& calls_;
    };

    int failures = 0;

    void expect(bool passed, const std::string& description)
    {
        std::cout << (passed ? "PASS: " : "FAIL: ") << description << '\n';
        if (!passed)
            ++failures;
    }

    bool callsAre(const std::vector<int>& actual,
                  std::initializer_list<int> expected)
    {
        return actual == std::vector<int>(expected);
    }

    void testSelector()
    {
        ai::EnemyContext context{};
        std::vector<int> calls;
        ai::Selector selector;
        selector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 1, calls));
        selector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 2, calls));
        selector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Running, 3, calls));
        expect(selector.tick(context, 0.016f) == ai::NodeStatus::Success,
               "Selector returns Success when a child succeeds");
        expect(callsAre(calls, {1, 2}), "Selector stops after its first Success");

        calls.clear();
        ai::Selector runningSelector;
        runningSelector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 1, calls));
        runningSelector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Running, 2, calls));
        runningSelector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 3, calls));
        expect(runningSelector.tick(context, 0.016f) == ai::NodeStatus::Running,
               "Selector propagates Running");
        expect(callsAre(calls, {1, 2}), "Selector stops after its first Running");

        calls.clear();
        ai::Selector failureSelector;
        failureSelector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 1, calls));
        failureSelector.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 2, calls));
        expect(failureSelector.tick(context, 0.016f) == ai::NodeStatus::Failure,
               "Selector returns Failure when every child fails");
        expect(callsAre(calls, {1, 2}), "Selector checks failed children in order");
    }

    void testSequence()
    {
        ai::EnemyContext context{};
        std::vector<int> calls;
        ai::Sequence sequence;
        sequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 1, calls));
        sequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 2, calls));
        sequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Running, 3, calls));
        expect(sequence.tick(context, 0.016f) == ai::NodeStatus::Failure,
               "Sequence propagates Failure");
        expect(callsAre(calls, {1, 2}), "Sequence stops after its first Failure");

        calls.clear();
        ai::Sequence runningSequence;
        runningSequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 1, calls));
        runningSequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Running, 2, calls));
        runningSequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Failure, 3, calls));
        expect(runningSequence.tick(context, 0.016f) == ai::NodeStatus::Running,
               "Sequence propagates Running");
        expect(callsAre(calls, {1, 2}), "Sequence stops after its first Running");

        calls.clear();
        ai::Sequence successSequence;
        successSequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 1, calls));
        successSequence.addChild(std::make_unique<PresetNode>(ai::NodeStatus::Success, 2, calls));
        expect(successSequence.tick(context, 0.016f) == ai::NodeStatus::Success,
               "Sequence returns Success when every child succeeds");
        expect(callsAre(calls, {1, 2}), "Sequence checks successful children in order");
    }

    void testLeafNodesAndTree()
    {
        ai::EnemyContext context{};
        ai::ConditionNode trueCondition([](ai::EnemyContext&) { return true; });
        ai::ConditionNode falseCondition([](ai::EnemyContext&) { return false; });
        expect(trueCondition.tick(context, 0.016f) == ai::NodeStatus::Success,
               "ConditionNode maps true to Success");
        expect(falseCondition.tick(context, 0.016f) == ai::NodeStatus::Failure,
               "ConditionNode maps false to Failure");

        float receivedDelta = 0.0f;
        ai::ActionNode action([&receivedDelta](ai::EnemyContext&, float deltaTime)
        {
            receivedDelta = deltaTime;
            return ai::NodeStatus::Running;
        });
        expect(action.tick(context, 0.025f) == ai::NodeStatus::Running,
               "ActionNode returns the callback status");
        expect(std::abs(receivedDelta - 0.025f) < 0.0001f,
               "ActionNode passes delta time to its callback");

        std::vector<int> calls;
        ai::BehaviourTree tree;
        tree.setRoot(std::make_unique<PresetNode>(ai::NodeStatus::Success, 7, calls));
        expect(tree.tick(context, 0.016f) == ai::NodeStatus::Success,
               "BehaviourTree returns its root status");
        expect(callsAre(calls, {7}), "BehaviourTree ticks its root once");

        ai::BehaviourTree emptyTree;
        expect(emptyTree.tick(context, 0.016f) == ai::NodeStatus::Failure,
               "BehaviourTree with no root returns Failure");
    }

    void testSkeletonDecision()
    {
        ai::BehaviourTree tree;
        ai::buildSkeletonBehaviourTree(tree);
        ai::EnemyContext context{};
        context.detectionRange = 375.f;
        context.hasLineOfSight = true;
        context.enemyPosition = { 100.f, 100.f };
        context.moveDirection = { 2.f, 3.f };

        context.playerPosition = { 140.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Attack,
               "Skeleton selects Attack inside attack range");
        expect(context.playerVisible, "Skeleton sees player inside detection range");
        expect(context.moveDirection == sf::Vector2f{ 0.f, 0.f },
               "Skeleton decision keeps movement direction at zero");

        context.playerPosition = { 200.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Skeleton selects Chase inside detection range");
        expect(context.playerVisible, "Skeleton sees player within detection range");

        context.playerPosition = { 450.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase && context.playerVisible,
               "Skeleton detects the Player at 350 world units with the 375-unit range");

        context.playerPosition = { 475.f, 100.f };
        ai::updateSkeletonVisibility(context);
        expect(context.playerVisible,
               "Detection includes the 375-unit world-space boundary");
        context.playerPosition = { 476.f, 100.f };
        ai::updateSkeletonVisibility(context);
        expect(!context.playerVisible,
               "Detection ends just beyond 375 world units");

        context.playerPosition = { 500.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Idle,
               "Skeleton selects Idle outside detection range");
        expect(!context.playerVisible, "Skeleton does not see player outside detection range");

        context.playerPosition = { 161.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Attack hysteresis does not enter Attack above the 60-unit start range");
        context.playerPosition = { 165.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Skeleton stays in Chase when still outside the Attack entry range");
        context.playerPosition = { 159.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Attack,
               "Skeleton enters Attack inside the configured range");
        context.attackCycleActive = true;
        context.playerPosition = { 165.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Attack,
               "Skeleton finishes an active attack inside its hysteresis buffer");
        context.attackCycleActive = false;
        context.playerPosition = { 171.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Skeleton returns to Chase after leaving the Attack buffer");
    }

    void testArcherDecision()
    {
        ai::EnemyContext context{};
        context.detectionRange = 600.f;
        context.enemyPosition = { 300.f, 500.f };
        ai::BehaviourTree tree;
        ai::buildArcherBehaviourTree(tree);

        const auto selectAtDistance = [&](float distance, bool lineOfSight)
        {
            context.playerPosition = { context.enemyPosition.x + distance,
                context.enemyPosition.y };
            context.hasLineOfSight = lineOfSight;
            ai::updateArcherVisibility(context);
            tree.tick(context, 0.016f);
            return context.currentAction;
        };

        expect(selectAtDistance(601.f, true) == ai::EnemyContext::Action::Idle,
               "Archer selects Idle beyond its 600-unit detection range");
        expect(selectAtDistance(100.f, false) == ai::EnemyContext::Action::Retreat,
               "Archer prioritizes Retreat when the Player is closer than 180 units");
        expect(selectAtDistance(180.f, true) == ai::EnemyContext::Action::RangedAttack,
               "Archer enters Ranged Attack at the preferred range boundary");
        expect(selectAtDistance(300.f, true) == ai::EnemyContext::Action::RangedAttack,
               "Archer selects Ranged Attack inside its preferred distance");
        expect(selectAtDistance(450.f, true) == ai::EnemyContext::Action::RangedAttack,
               "Archer remains in Ranged Attack at the far preferred range boundary");
        expect(selectAtDistance(451.f, true) == ai::EnemyContext::Action::Approach,
               "Archer selects Approach beyond its preferred attack distance");
        expect(selectAtDistance(600.f, true) == ai::EnemyContext::Action::Approach,
               "Archer still selects Approach at the detection limit");
        expect(selectAtDistance(300.f, false) == ai::EnemyContext::Action::Reposition,
               "Archer tree selects repositioning when a shot is blocked");
        expect(context.moveDirection == sf::Vector2f{ 0.f, 0.f },
               "Archer decision tests do not request movement");
    }

    void testArcherMovement()
    {
        constexpr int size = 8;
        constexpr float tileSize = 128.f;
        using TestGrid = std::array<std::array<int, size>, size>;
        const auto makeGrid = [=]
        {
            TestGrid grid{};
            for (int i = 0; i < size; ++i)
            {
                grid[0][i] = grid[size - 1][i] = 1;
                grid[i][0] = grid[i][size - 1] = 1;
            }
            return grid;
        };
        auto grid = makeGrid();
        const auto canOccupy = [&grid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::archerFootWidth,
                ai::archerFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto cellWalkable = [&grid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            const sf::Vector2f point = ai::gridCellWaypoint(
                cell, tileSize, ai::archerNavigationGroundOffset);
            return ai::isFootprintClear(point, ai::archerFootWidth,
                ai::archerFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto hasLineOfSight = [&canOccupy](const sf::Vector2f& from,
            const sf::Vector2f& to)
        {
            return ai::hasClearLineOfSight(from, to, canOccupy, 12.f);
        };

        ai::EnemyContext retreat{};
        retreat.currentAction = ai::EnemyContext::Action::Retreat;
        retreat.enemyPosition = ai::gridCellWaypoint({ 4, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        retreat.playerPosition = ai::gridCellWaypoint({ 3, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        retreat.moveSpeed = 120.f;
        ai::ArcherPathState retreatState;
        const sf::Vector2f retreatStart = retreat.enemyPosition;
        const sf::Vector2f retreatMove = ai::moveArcher(retreat, retreatState,
            0.25f, grid, tileSize, cellWalkable, canOccupy, hasLineOfSight);
        expect(retreatMove.x > 0.f && retreat.enemyPosition.x > retreatStart.x,
               "Archer retreats away from the Player toward a reachable position");

        auto obstacleGrid = makeGrid();
        obstacleGrid[3][3] = 1;
        const auto obstacleCanOccupy = [&obstacleGrid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::archerFootWidth,
                ai::archerFootHeight, obstacleGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto obstacleWalkable = [&obstacleGrid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::archerNavigationGroundOffset), ai::archerFootWidth,
                ai::archerFootHeight, obstacleGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        ai::EnemyContext aroundObstacle{};
        aroundObstacle.currentAction = ai::EnemyContext::Action::Approach;
        aroundObstacle.enemyPosition = ai::gridCellWaypoint({ 2, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        aroundObstacle.playerPosition = ai::gridCellWaypoint({ 5, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        aroundObstacle.moveSpeed = 120.f;
        ai::ArcherPathState obstacleState;
        const sf::Vector2f obstacleMove = ai::moveArcher(aroundObstacle,
            obstacleState, 0.25f, obstacleGrid, tileSize,
            obstacleWalkable, obstacleCanOccupy,
            [&obstacleCanOccupy](const sf::Vector2f& from, const sf::Vector2f& to)
            {
                return ai::hasClearLineOfSight(from, to, obstacleCanOccupy, 12.f);
            });
        expect(!obstacleState.pathFailed && ai::isPathValid(obstacleState.path, obstacleWalkable),
               "Archer plans an A* route around a blocking obstacle");
        expect(obstacleMove != sf::Vector2f{ 0.f, 0.f } &&
               obstacleCanOccupy(aroundObstacle.enemyPosition),
               "Archer moves without entering the obstacle footprint");

        ai::EnemyContext approach{};
        approach.currentAction = ai::EnemyContext::Action::Approach;
        approach.enemyPosition = ai::gridCellWaypoint({ 1, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        approach.playerPosition = ai::gridCellWaypoint({ 5, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        approach.moveSpeed = 120.f;
        ai::ArcherPathState approachState;
        for (int i = 0; i < 30; ++i)
            ai::moveArcher(approach, approachState, 0.1f, grid, tileSize,
                cellWalkable, canOccupy, hasLineOfSight);
        const float approachDistance = ai::archerDistance(approach);
        expect(approachDistance >= ai::archerDesiredDistance - 1.f &&
               approachDistance <= ai::archerDesiredDistance + 12.f,
               "Archer approach stops at its preferred shooting distance (" +
                   std::to_string(approachDistance) + " units)");

        auto dividerGrid = makeGrid();
        for (int row = 1; row < size - 1; ++row)
            if (row != 6)
                dividerGrid[row][4] = 1;
        const auto dividerCanOccupy = [&dividerGrid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::archerFootWidth,
                ai::archerFootHeight, dividerGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto dividerWalkable = [&dividerGrid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::archerNavigationGroundOffset), ai::archerFootWidth,
                ai::archerFootHeight, dividerGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        ai::EnemyContext reposition{};
        reposition.currentAction = ai::EnemyContext::Action::Reposition;
        reposition.enemyPosition = ai::gridCellWaypoint({ 2, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        reposition.playerPosition = ai::gridCellWaypoint({ 6, 3 }, tileSize,
            ai::archerNavigationGroundOffset);
        reposition.moveSpeed = 120.f;
        ai::ArcherPathState repositionState;
        const auto dividerLos = [&dividerCanOccupy](const sf::Vector2f& from,
            const sf::Vector2f& to)
        {
            return ai::hasClearLineOfSight(from, to, dividerCanOccupy, 12.f);
        };
        ai::moveArcher(reposition, repositionState, 0.1f, dividerGrid,
            tileSize, dividerWalkable, dividerCanOccupy, dividerLos);
        const sf::Vector2f firingPoint = ai::gridCellWaypoint(
            repositionState.destination, tileSize, ai::archerNavigationGroundOffset);
        expect(!repositionState.pathFailed && repositionState.destination.column > 4 &&
               dividerLos(firingPoint, reposition.playerPosition),
               "Blocked line of sight repositions the Archer to a reachable clear shot");

        ai::EnemyContext boundary{};
        boundary.currentAction = ai::EnemyContext::Action::Retreat;
        boundary.enemyPosition = ai::gridCellWaypoint({ 1, 1 }, tileSize,
            ai::archerNavigationGroundOffset);
        boundary.playerPosition = ai::gridCellWaypoint({ 2, 1 }, tileSize,
            ai::archerNavigationGroundOffset);
        boundary.moveSpeed = 120.f;
        ai::ArcherPathState boundaryState;
        for (int i = 0; i < 100; ++i)
            ai::moveArcher(boundary, boundaryState, 0.1f, grid, tileSize,
                cellWalkable, canOccupy, hasLineOfSight);
        expect(canOccupy(boundary.enemyPosition),
               "Archer foot collision keeps movement inside the dungeon boundary");
    }

    void testArcherCombat()
    {
        ai::ArcherAttackState attack;
        ai::ArcherAttackUpdate start = ai::updateArcherAttack(attack, true, 0.125f);
        const ai::ArcherAttackUpdate release = ai::updateArcherAttack(attack, true, 0.125f);
        const ai::ArcherAttackUpdate afterRelease = ai::updateArcherAttack(attack, true, 0.125f);
        expect(start.animationActive && release.releaseProjectile &&
               release.frame == ai::archerArrowReleaseFrame &&
               !afterRelease.releaseProjectile,
               "Archer releases exactly one arrow at the attack sheet release frame");

        ai::ArcherAttackState cooldown;
        ai::updateArcherAttack(cooldown, true, 0.05f);
        ai::updateArcherAttack(cooldown, true, 0.45f);
        const ai::ArcherAttackUpdate waiting = ai::updateArcherAttack(cooldown, true, 1.0f);
        const ai::ArcherAttackUpdate ready = ai::updateArcherAttack(cooldown, true, 0.05f);
        expect(!waiting.animationActive && ready.animationActive,
               "Archer waits for the 1.5-second attack cooldown before firing again");

        ai::ArcherProjectile arrow = ai::makeArcherProjectile({ 10.f, 20.f }, { 210.f, 20.f });
        std::vector<ai::ArcherProjectile> traveling{ arrow };
        ai::PlayerHealth distantPlayer;
        ai::updateArcherProjectiles(traveling, 0.25f, { 1000.f, 1000.f },
            48.f, 28.f, distantPlayer, [](const sf::Vector2f&) { return true; });
        expect(std::abs(traveling[0].position.x - 110.f) < 0.01f &&
               std::abs(traveling[0].velocity.x - ai::archerArrowSpeed) < 0.01f,
               "Arrow travels at 400 units per second along its firing-time direction");

        ai::PlayerHealth hitPlayer;
        std::vector<ai::ArcherProjectile> hitting{
            ai::makeArcherProjectile({ 0.f, 50.f }, { 100.f, 50.f }) };
        const int firstHit = ai::updateArcherProjectiles(hitting, 0.2f,
            { 50.f, 60.f }, 40.f, 20.f, hitPlayer,
            [](const sf::Vector2f&) { return true; });
        const int secondHit = ai::updateArcherProjectiles(hitting, 0.2f,
            { 50.f, 60.f }, 40.f, 20.f, hitPlayer,
            [](const sf::Vector2f&) { return true; });
        expect(firstHit == 1 && secondHit == 0 && hitPlayer.current == 92,
               "One arrow damages the Player once for 8 HP, then is removed");

        ai::PlayerHealth wallPlayer;
        std::vector<ai::ArcherProjectile> blocked{
            ai::makeArcherProjectile({ 0.f, 50.f }, { 100.f, 50.f }) };
        ai::updateArcherProjectiles(blocked, 0.2f, { 80.f, 60.f },
            40.f, 20.f, wallPlayer,
            [](const sf::Vector2f& point) { return point.x < 40.f; });
        expect(!blocked[0].active && wallPlayer.current == 100,
               "Arrow is removed by a blocking wall or solid obstacle without damage");

        std::vector<ai::ArcherProjectile> leaving{
            ai::makeArcherProjectile({ 40.f, 20.f }, { 100.f, 20.f }) };
        ai::updateArcherProjectiles(leaving, 0.1f, { 300.f, 300.f },
            40.f, 20.f, wallPlayer,
            [](const sf::Vector2f& point) { return point.x < 50.f; });
        expect(!leaving[0].active,
               "Arrow is removed when it reaches the dungeon boundary");

        ai::PlayerHealth lifetimePlayer;
        std::vector<ai::ArcherProjectile> expiring{
            ai::makeArcherProjectile({ 0.f, 0.f }, { 100.f, 0.f }) };
        expiring[0].lifetimeRemaining = 0.05f;
        ai::updateArcherProjectiles(expiring, 0.1f, { 500.f, 500.f },
            40.f, 20.f, lifetimePlayer,
            [](const sf::Vector2f&) { return true; });
        expect(!expiring[0].active,
               "Arrow expires after its maximum lifetime");
    }

    void testArcherRepositionIntegration()
    {
        constexpr int size = 8;
        constexpr float tileSize = 128.f;
        using TestGrid = std::array<std::array<int, size>, size>;
        TestGrid grid{};
        for (int i = 0; i < size; ++i)
        {
            grid[0][i] = grid[size - 1][i] = 1;
            grid[i][0] = grid[i][size - 1] = 1;
        }
        // A single wall blocks the direct shot but leaves a route around it.
        grid[5][4] = 1;

        const auto canOccupy = [&grid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::archerFootWidth,
                ai::archerFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto cellWalkable = [&grid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::archerNavigationGroundOffset), ai::archerFootWidth,
                ai::archerFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto hasLineOfSight = [&canOccupy](const sf::Vector2f& from,
            const sf::Vector2f& to)
        {
            return ai::hasClearLineOfSight(from, to, canOccupy, 12.f);
        };

        ai::EnemyContext context{};
        context.detectionRange = 600.f;
        context.enemyPosition = ai::gridCellWaypoint({ 3, 5 }, tileSize,
            ai::archerNavigationGroundOffset);
        context.playerPosition = ai::gridCellWaypoint({ 5, 5 }, tileSize,
            ai::archerNavigationGroundOffset);
        context.moveSpeed = 120.f;
        context.hasLineOfSight = hasLineOfSight(context.enemyPosition, context.playerPosition);
        ai::updateArcherVisibility(context);
        ai::BehaviourTree tree;
        ai::buildArcherBehaviourTree(tree);
        tree.tick(context, 0.05f);
        expect(context.playerVisible && !context.hasLineOfSight &&
               ai::archerDistance(context) >= ai::archerRetreatRange &&
               ai::archerDistance(context) <= ai::archerPreferredAttackRange &&
               context.currentAction == ai::EnemyContext::Action::Reposition,
               "Detected Player in shooting range behind a wall selects Reposition");

        ai::ArcherPathState state;
        ai::moveArcher(context, state, 0.05f, grid, tileSize,
            cellWalkable, canOccupy, hasLineOfSight);
        const ai::GridCell firstGoal = state.destination;
        const bool foundShot = !state.pathFailed &&
            ai::isPathValid(state.path, cellWalkable) &&
            hasLineOfSight(ai::gridCellWaypoint(firstGoal, tileSize,
                ai::archerNavigationGroundOffset), context.playerPosition);
        expect(foundShot,
               "A* finds a reachable reposition waypoint with a clear shot");

        bool movedWhileBlocked = context.moveDirection != sf::Vector2f{ 0.f, 0.f };
        bool stableGoal = true;
        bool recoveredToShot = false;
        for (int frame = 0; frame < 100; ++frame)
        {
            context.hasLineOfSight = hasLineOfSight(context.enemyPosition,
                context.playerPosition);
            ai::updateArcherVisibility(context);
            tree.tick(context, 0.05f);
            if (context.currentAction == ai::EnemyContext::Action::RangedAttack)
            {
                recoveredToShot = context.hasLineOfSight;
                break;
            }
            if (context.currentAction != ai::EnemyContext::Action::Reposition)
                break;

            ai::moveArcher(context, state, 0.05f, grid, tileSize,
                cellWalkable, canOccupy, hasLineOfSight);
            movedWhileBlocked = movedWhileBlocked ||
                context.moveDirection != sf::Vector2f{ 0.f, 0.f };
            stableGoal = stableGoal && state.destination == firstGoal && !state.pathFailed;
        }
        expect(movedWhileBlocked && stableGoal,
               "Archer follows one valid reposition path without replanning or getting stuck");
        expect(recoveredToShot,
               "Archer returns to Ranged Attack when movement restores line of sight");
    }

    void testOgreDecision()
    {
        ai::BehaviourTree tree;
        ai::buildOgreBehaviourTree(tree);
        ai::EnemyContext context{};
        context.detectionRange = 450.f;
        context.attackRange = 85.f;
        context.enemyPosition = { 500.f, 500.f };

        const auto selectAt = [&](float distance, bool lineOfSight)
        {
            context.enemyPosition = { 500.f, 500.f };
            context.playerPosition = { 500.f + distance, 500.f };
            context.hasLineOfSight = lineOfSight;
            context.attackCycleActive = false;
            ai::updateOgreVisibility(context);
            tree.tick(context, 0.016f);
            return context.currentAction;
        };

        expect(selectAt(451.f, true) == ai::EnemyContext::Action::Idle,
               "Ogre selects Idle outside its 450-unit detection range");
        expect(selectAt(250.f, false) == ai::EnemyContext::Action::Chase,
               "Ogre selects Chase inside detection range even without line of sight");
        expect(selectAt(85.f, true) == ai::EnemyContext::Action::HeavyAttack &&
               !context.attackCycleActive,
               "Ogre selects HeavyAttack at 85 units with clear line of sight");
        context.playerPosition = { context.enemyPosition.x + 250.f, context.enemyPosition.y };
        context.hasLineOfSight = false;
        ai::updateOgreVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Ogre can return to Chase before an attack cycle is started");
        expect(selectAt(50.f, false) == ai::EnemyContext::Action::Chase,
               "Ogre does not select HeavyAttack when line of sight is blocked");

        context.currentAction = ai::EnemyContext::Action::Recovery;
        context.attackCycleActive = true;
        context.playerPosition = { context.enemyPosition.x + 250.f, context.enemyPosition.y };
        context.hasLineOfSight = false;
        ai::updateOgreVisibility(context);
        const ai::NodeStatus recoveryStatus = tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Recovery &&
               recoveryStatus == ai::NodeStatus::Running,
               "Ogre Recovery remains active ahead of a valid Chase decision");

        context.currentAction = ai::EnemyContext::Action::HeavyAttack;
        const ai::NodeStatus attackStatus = tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::HeavyAttack &&
               attackStatus == ai::NodeStatus::Running,
               "An active Ogre HeavyAttack cannot be preempted by Chase");
    }

    void testOgrePathfinding()
    {
        constexpr int size = 8;
        constexpr float tileSize = 128.f;
        using TestGrid = std::array<std::array<int, size>, size>;
        const auto makeGrid = [=]
        {
            TestGrid grid{};
            for (int i = 0; i < size; ++i)
            {
                grid[0][i] = grid[size - 1][i] = 1;
                grid[i][0] = grid[i][size - 1] = 1;
            }
            return grid;
        };

        auto grid = makeGrid();
        const auto canOccupy = [&grid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::ogreFootWidth,
                ai::ogreFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto cellWalkable = [&grid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::ogreNavigationGroundOffset), ai::ogreFootWidth,
                ai::ogreFootHeight, grid, tileSize,
                [](int tile) { return tile == 0; });
        };

        ai::EnemyContext mover{};
        mover.currentAction = ai::EnemyContext::Action::Chase;
        mover.enemyPosition = ai::gridCellWaypoint({ 1, 3 }, tileSize,
            ai::ogreNavigationGroundOffset);
        mover.playerPosition = ai::gridCellWaypoint({ 5, 3 }, tileSize,
            ai::ogreNavigationGroundOffset);
        mover.attackRange = 32.f;
        mover.moveSpeed = 65.f;
        ai::OgrePathState moveState;
        const sf::Vector2f firstStep = ai::followOgrePath(mover, moveState,
            0.5f, grid, tileSize, cellWalkable, canOccupy);
        const std::vector<ai::GridCell> originalPath = moveState.path;
        ai::followOgrePath(mover, moveState, 0.1f, grid, tileSize, cellWalkable, canOccupy);
        expect(std::abs(std::sqrt(firstStep.x * firstStep.x + firstStep.y * firstStep.y) - 32.5f) < 0.01f,
               "Ogre Chase movement uses 65 units per second and delta time");
        expect(moveState.path == originalPath && moveState.destination == ai::GridCell{ 5, 3 },
               "Ogre keeps its path while the Player remains in the same destination cell");

        const sf::Vector2f beforeAttackStop = mover.enemyPosition;
        const std::size_t waypointBeforeStop = moveState.nextWaypoint;
        mover.currentAction = ai::EnemyContext::Action::HeavyAttack;
        const sf::Vector2f attackStop = ai::followOgrePath(mover, moveState,
            0.1f, grid, tileSize, cellWalkable, canOccupy);
        expect(attackStop == sf::Vector2f{ 0.f, 0.f } &&
               mover.enemyPosition == beforeAttackStop &&
               moveState.path == originalPath && moveState.nextWaypoint == waypointBeforeStop,
               "Ogre stops during HeavyAttack without discarding its current route");
        mover.currentAction = ai::EnemyContext::Action::Chase;
        const sf::Vector2f resumedMovement = ai::followOgrePath(mover, moveState,
            0.1f, grid, tileSize, cellWalkable, canOccupy);
        expect(resumedMovement.x > 0.f && mover.enemyPosition.x > beforeAttackStop.x &&
               moveState.path == originalPath,
               "Ogre resumes forward on the cached route instead of backtracking to its start cell");

        auto obstacleGrid = makeGrid();
        obstacleGrid[3][3] = 1;
        const auto obstacleCanOccupy = [&obstacleGrid, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::ogreFootWidth,
                ai::ogreFootHeight, obstacleGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto obstacleWalkable = [&obstacleGrid, tileSize, size](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= size || cell.row < 0 || cell.row >= size)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::ogreNavigationGroundOffset), ai::ogreFootWidth,
                ai::ogreFootHeight, obstacleGrid, tileSize,
                [](int tile) { return tile == 0; });
        };
        ai::EnemyContext obstacleChase{};
        obstacleChase.currentAction = ai::EnemyContext::Action::Chase;
        obstacleChase.enemyPosition = ai::gridCellWaypoint({ 2, 3 }, tileSize,
            ai::ogreNavigationGroundOffset);
        obstacleChase.playerPosition = ai::gridCellWaypoint({ 5, 3 }, tileSize,
            ai::ogreNavigationGroundOffset);
        obstacleChase.attackRange = 32.f;
        obstacleChase.moveSpeed = 65.f;
        ai::OgrePathState obstacleState;
        for (int frame = 0; frame < 180; ++frame)
        {
            ai::followOgrePath(obstacleChase, obstacleState, 0.1f,
                obstacleGrid, tileSize, obstacleWalkable, obstacleCanOccupy);
            if (!obstacleCanOccupy(obstacleChase.enemyPosition))
                break;
        }
        const bool routeAvoidsBlock = std::any_of(obstacleState.path.begin(),
            obstacleState.path.end(), [](ai::GridCell cell)
            {
                return cell.column == 3 && cell.row != 3;
            });
        expect(!obstacleState.pathFailed && ai::isPathValid(obstacleState.path, obstacleWalkable) &&
               routeAvoidsBlock,
               "Ogre A* routes around a blocking obstacle with its foot-sized clearance");
        expect(obstacleCanOccupy(obstacleChase.enemyPosition) &&
               std::abs(ai::ogreDistance(obstacleChase) - obstacleChase.attackRange) < 1.f,
               "Ogre reaches attack distance without crossing the obstacle or the Player");

        auto dungeon = std::array<std::array<int, 28>, 16>{};
        constexpr int columns = 28;
        constexpr int rows = 16;
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                if (row == 0 || row == rows - 1 || column == 0 || column == columns - 1)
                    dungeon[row][column] = 1;
        for (int row = 1; row < rows - 1; ++row)
        {
            if (row >= 6 && row <= 9)
                continue;
            dungeon[row][13] = 1;
            dungeon[row][14] = 1;
        }
        dungeon[4][5] = 1;
        dungeon[11][16] = 1;
        const auto dungeonCanOccupy = [&dungeon, tileSize](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, ai::ogreFootWidth,
                ai::ogreFootHeight, dungeon, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto dungeonWalkable = [&dungeon, tileSize, columns, rows](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= columns || cell.row < 0 || cell.row >= rows)
                return false;
            return ai::isFootprintClear(ai::gridCellWaypoint(cell, tileSize,
                ai::ogreNavigationGroundOffset), ai::ogreFootWidth,
                ai::ogreFootHeight, dungeon, tileSize,
                [](int tile) { return tile == 0; });
        };
        ai::EnemyContext passageChase{};
        passageChase.currentAction = ai::EnemyContext::Action::Chase;
        passageChase.enemyPosition = ai::gridCellWaypoint({ 10, 7 }, tileSize,
            ai::ogreNavigationGroundOffset);
        passageChase.playerPosition = ai::gridCellWaypoint({ 17, 7 }, tileSize,
            ai::ogreNavigationGroundOffset);
        passageChase.attackRange = 85.f;
        passageChase.moveSpeed = 65.f;
        ai::OgrePathState passageState;
        bool crossedPassage = false;
        for (int frame = 0; frame < 400; ++frame)
        {
            ai::followOgrePath(passageChase, passageState, 0.1f,
                dungeon, tileSize, dungeonWalkable, dungeonCanOccupy);
            if (passageChase.enemyPosition.x >= 15.f * tileSize)
                crossedPassage = true;
            if (!dungeonCanOccupy(passageChase.enemyPosition))
                break;
        }
        const bool pathUsesOpening = std::any_of(passageState.path.begin(),
            passageState.path.end(), [](ai::GridCell cell)
            {
                return (cell.column == 13 || cell.column == 14) &&
                    cell.row >= 6 && cell.row <= 9;
            });
        expect(!passageState.pathFailed && ai::isPathValid(passageState.path, dungeonWalkable) &&
               pathUsesOpening,
               "Ogre pathfinding uses the central passage between dungeon rooms");
        expect(crossedPassage && dungeonCanOccupy(passageChase.enemyPosition),
               "Ogre moves through the central passage without entering blocked tiles");

        const sf::Vector2f beforeStop = passageChase.enemyPosition;
        const std::vector<ai::GridCell> routeBeforeStop = passageState.path;
        passageChase.currentAction = ai::EnemyContext::Action::HeavyAttack;
        passageChase.moveDirection = { 1.f, 0.f };
        const sf::Vector2f stopped = ai::followOgrePath(passageChase,
            passageState, 0.5f, dungeon, tileSize, dungeonWalkable, dungeonCanOccupy);
        expect(stopped == sf::Vector2f{ 0.f, 0.f } &&
               passageChase.enemyPosition == beforeStop &&
               passageChase.moveDirection == sf::Vector2f{ 0.f, 0.f } &&
               passageState.path == routeBeforeStop,
               "Ogre stops and preserves its route when HeavyAttack is selected");
    }

    void testOgreCombat()
    {
        ai::PlayerHealth player;
        ai::EnemyContext context{};
        context.currentAction = ai::EnemyContext::Action::HeavyAttack;
        context.enemyPosition = { 100.f, 100.f };
        context.playerPosition = { 185.f, 100.f };
        context.attackRange = 85.f;
        context.hasLineOfSight = true;
        ai::OgreCombatState state;

        const ai::OgreCombatUpdate started = ai::updateOgreCombat(state,
            context, player, 0.016f);
        expect(started.attackStarted && started.animationActive &&
               context.attackCycleActive && player.current == 100,
               "Ogre starts a locked HeavyAttack animation without immediate damage");
        ai::updateOgreCombat(state, context, player, 0.25f);
        const ai::OgreCombatUpdate beforeImpact = ai::updateOgreCombat(
            state, context, player, ai::ogreHeavyAttackWindup - 0.25f - 0.001f);
        expect(beforeImpact.animationActive && !beforeImpact.damageApplied &&
               player.current == 100,
               "Ogre wind-up does not damage the Player before 0.5 seconds");
        const ai::OgreCombatUpdate impact = ai::updateOgreCombat(
            state, context, player, 0.001f);
        expect(impact.damageApplied && impact.frame == ai::ogreAttackImpactFrame &&
               player.current == 80,
               "Ogre applies 20 damage once at the 0.5-second impact frame");
        const ai::OgreCombatUpdate afterImpact = ai::updateOgreCombat(
            state, context, player, 0.1f);
        expect(!afterImpact.damageApplied && player.current == 80,
               "One Ogre attack cycle cannot damage the Player twice");
        ai::updateOgreCombat(state, context, player,
            ai::ogreAttackAnimationDuration - state.elapsed);
        expect(context.currentAction == ai::EnemyContext::Action::Recovery &&
               state.phase == ai::OgreCombatState::Phase::Recovery &&
               context.attackCycleActive,
               "Ogre enters a locked Recovery after the attack animation");

        ai::BehaviourTree tree;
        ai::buildOgreBehaviourTree(tree);
        const ai::NodeStatus lockedStatus = tree.tick(context, 0.016f);
        expect(lockedStatus == ai::NodeStatus::Running &&
               context.currentAction == ai::EnemyContext::Action::Recovery,
               "Ogre Behaviour Tree keeps Recovery ahead of a new HeavyAttack");

        const ai::OgreCombatUpdate recovery = ai::updateOgreCombat(
            state, context, player, ai::ogreRecoveryDuration - 0.01f);
        expect(!recovery.animationActive && context.attackCycleActive &&
               context.currentAction == ai::EnemyContext::Action::Recovery,
               "Ogre remains stationary in Recovery for the full 1.5 seconds");
        ai::updateOgreCombat(state, context, player, 0.02f);
        expect(!context.attackCycleActive && state.phase == ai::OgreCombatState::Phase::None,
               "Ogre releases its attack lock after the Recovery timer ends");

        context.playerPosition = { 300.f, 100.f };
        context.hasLineOfSight = true;
        ai::updateOgreVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Chase,
               "Ogre Behaviour Tree resumes Chase after Recovery completes");

        const auto runMiss = [](float playerDistance, bool clearShot)
        {
            ai::PlayerHealth target;
            ai::EnemyContext attackContext{};
            attackContext.currentAction = ai::EnemyContext::Action::HeavyAttack;
            attackContext.enemyPosition = { 100.f, 100.f };
            attackContext.playerPosition = { 100.f + playerDistance, 100.f };
            attackContext.attackRange = 85.f;
            attackContext.hasLineOfSight = clearShot;
            ai::OgreCombatState attackState;
            ai::updateOgreCombat(attackState, attackContext, target, 0.016f);
            const ai::OgreCombatUpdate hitFrame = ai::updateOgreCombat(
                attackState, attackContext, target, ai::ogreHeavyAttackWindup);
            return std::pair<int, bool>{ target.current, hitFrame.damageApplied };
        };
        const auto escaped = runMiss(86.f, true);
        expect(escaped.first == 100 && !escaped.second,
               "Ogre misses when the Player leaves the 85-unit range before impact");
        const auto blocked = runMiss(80.f, false);
        expect(blocked.first == 100 && !blocked.second,
               "Ogre heavy attack cannot hit the Player through a blocking wall");

        expect(ai::ogreFacingRow({ 100.f, 100.f }, { 100.f, 200.f }) == 0 &&
               ai::ogreFacingRow({ 100.f, 200.f }, { 100.f, 100.f }) == 1 &&
               ai::ogreFacingRow({ 100.f, 100.f }, { 50.f, 100.f }) == 2 &&
               ai::ogreFacingRow({ 100.f, 100.f }, { 150.f, 100.f }) == 3,
               "Ogre attack facing maps to the four Orc spritesheet direction rows");
    }

    void testPlayerAttack()
    {
        ai::PlayerAttackState state;
        expect(ai::pressPlayerAttack(state, 2) && state.animationActive &&
               state.frame == 0 && state.facingRow == 2,
               "A fresh Space press starts one attack facing the current direction");
        expect(!ai::pressPlayerAttack(state, 3) && state.facingRow == 2,
               "Holding Space does not restart or turn the active attack");

        const ai::PlayerAttackUpdate secondFrame = ai::updatePlayerAttack(state, 0.125f);
        expect(secondFrame.animationActive && secondFrame.frame == 1 &&
               secondFrame.facingRow == 2,
               "Player attack advances to its second directional frame");
        const ai::PlayerAttackUpdate strikeFrame = ai::updatePlayerAttack(state, 0.125f);
        expect(strikeFrame.animationActive && strikeFrame.frame == 2,
               "Player attack reaches the visible strike frame");
        const ai::PlayerAttackUpdate finalFrame = ai::updatePlayerAttack(state, 0.125f);
        expect(finalFrame.animationActive && finalFrame.frame == 3,
               "Player attack plays its final recovery frame");
        const ai::PlayerAttackUpdate finished = ai::updatePlayerAttack(state, 0.125f);
        expect(!finished.animationActive && !state.animationActive && state.frame == 0,
               "Player attack ends after its four frames and returns to locomotion rendering");
        expect(!ai::pressPlayerAttack(state, 1),
               "Holding Space after an attack ends does not automatically start another");

        ai::releasePlayerAttack(state);
        expect(ai::pressPlayerAttack(state, 1) && state.animationActive &&
               state.facingRow == 1,
               "Releasing and pressing Space again starts a new attack");
    }

    void testPlayerCombat()
    {
        constexpr float playerWidth = 48.f;
        constexpr float playerHeight = 28.f;
        constexpr float skeletonWidth = 28.f;
        constexpr float skeletonHeight = 18.f;
        const sf::Vector2f playerGround{ 100.f, 100.f };
        ai::PlayerAttackState attackState;
        expect(ai::pressPlayerAttack(attackState, 3),
               "Player combat test starts an attack facing right");
        const ai::PlayerAttackUpdate secondFrame = ai::updatePlayerAttack(attackState, 0.125f);
        expect(secondFrame.animationActive && secondFrame.frame == 1 &&
               !secondFrame.damageFrame,
               "Player attack does not damage before the third animation frame");
        const ai::PlayerAttackUpdate impact = ai::updatePlayerAttack(attackState, 0.125f);
        expect(impact.animationActive && impact.frame == ai::playerAttackImpactFrame &&
               impact.damageFrame,
               "Player attack reports one hit event on its third animation frame");

        ai::EnemyHealth target{ 45 };
        expect(ai::applyPlayerAttackDamage(impact, target, playerGround,
                   playerWidth, playerHeight, { 180.f, 100.f },
                   skeletonWidth, skeletonHeight, true) && target.current == 30,
               "Player deals 15 damage to an enemy inside the 90-unit attack range");
        const ai::PlayerAttackUpdate laterFrame = ai::updatePlayerAttack(attackState, 0.1f);
        expect(!laterFrame.damageFrame &&
               !ai::applyPlayerAttackDamage(laterFrame, target, playerGround,
                   playerWidth, playerHeight, { 180.f, 100.f },
                   skeletonWidth, skeletonHeight, true) && target.current == 30,
               "One Player attack cycle cannot damage the same enemy more than once");

        expect(!ai::playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
                   { 230.f, 100.f }, skeletonWidth, skeletonHeight, 3) &&
               !ai::playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
                   { 60.f, 100.f }, skeletonWidth, skeletonHeight, 3),
               "Player attack misses targets out of range or behind the facing direction");
        expect(ai::playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
                   { 60.f, 100.f }, skeletonWidth, skeletonHeight, 2) &&
               ai::playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
                   { 100.f, 160.f }, skeletonWidth, skeletonHeight, 0) &&
               !ai::playerAttackOverlapsEnemy(playerGround, playerWidth, playerHeight,
                   { 100.f, 160.f }, skeletonWidth, skeletonHeight, 1),
               "Attack area follows left, down and up-facing directions correctly");

        ai::EnemyHealth skeleton{ 45 };
        ai::EnemyHealth archer{ 35 };
        ai::EnemyHealth ogre{ 100 };
        ai::EnemyHealth outside{ 100 };
        expect(ai::applyPlayerAttackDamage(impact, skeleton, playerGround,
                   playerWidth, playerHeight, { 150.f, 100.f }, 28.f, 18.f, true) &&
               ai::applyPlayerAttackDamage(impact, archer, playerGround,
                   playerWidth, playerHeight, { 170.f, 102.f }, 24.f, 16.f, true) &&
               ai::applyPlayerAttackDamage(impact, ogre, playerGround,
                   playerWidth, playerHeight, { 185.f, 100.f }, 32.f, 20.f, true) &&
               !ai::applyPlayerAttackDamage(impact, outside, playerGround,
                   playerWidth, playerHeight, { 240.f, 100.f }, 32.f, 20.f, true) &&
               skeleton.current == 30 && archer.current == 20 && ogre.current == 85 &&
               outside.current == 100,
               "One swing can hit several enemies only when each foot box is in its attack area");

        std::array<std::array<int, 6>, 4> wallGrid{};
        wallGrid[1][2] = 1;
        const auto rayCanOccupy = [&wallGrid](const sf::Vector2f& point)
        {
            return ai::isFootprintClear(point, 1.f, 1.f, wallGrid, 32.f,
                [](int tile) { return tile == 0; });
        };
        const bool clearShot = ai::hasClearLineOfSight(
            { 24.f, 48.f }, { 104.f, 48.f }, rayCanOccupy, 4.f);
        ai::EnemyHealth behindWall{ 45 };
        expect(!clearShot && !ai::applyPlayerAttackDamage(impact, behindWall,
                   { 24.f, 48.f }, playerWidth, playerHeight, { 104.f, 48.f },
                   skeletonWidth, skeletonHeight, clearShot) && behindWall.current == 45,
               "Player attack cannot pass through a blocking wall");

        ai::EnemyHealth defeated{ 15 };
        expect(ai::applyPlayerAttackDamage(impact, defeated, playerGround,
                   playerWidth, playerHeight, { 150.f, 100.f },
                   skeletonWidth, skeletonHeight, true) && defeated.current == 0 &&
               defeated.isDefeated() && !defeated.takeDamage(15),
               "Enemy health reaches zero and defeated enemies cannot take more damage");

        expect(ai::EnemyHealth{ 45 }.maximum == 45 &&
               ai::EnemyHealth{ 35 }.current == 35 &&
               ai::EnemyHealth{ 100 }.current == 100,
               "Enemy health starts at the requested values for all three roles");
    }

    void testDefeatedEnemyRenderingAndInteraction()
    {
        ai::EnemyHealth enemy{ 15 };
        expect(ai::shouldRenderEnemy(enemy),
               "Living enemy is eligible for sprite and health-bar rendering");
        enemy.takeDamage(15);
        expect(enemy.isDefeated() && !ai::shouldRenderEnemy(enemy),
               "Enemy at zero HP is excluded from sprite and health-bar rendering");

        const ai::PlayerAttackUpdate attack{ true, ai::playerAttackImpactFrame, 3, true };
        expect(!ai::applyPlayerAttackDamage(attack, enemy,
                   { 100.f, 100.f }, 48.f, 28.f, { 150.f, 100.f },
                   28.f, 18.f, true) && enemy.current == 0,
               "Defeated enemy cannot receive more Player attack damage");
    }

    void testGameOverAndRestart()
    {
        ai::GameSessionState session;
        ai::PlayerHealth player;
        std::array<ai::EnemyHealth, 3> enemies{
            ai::EnemyHealth{ 45 }, ai::EnemyHealth{ 35 }, ai::EnemyHealth{ 100 }
        };

        session.update(player, enemies);
        expect(!session.gameOver,
               "Game Over stays inactive while the Player has health");
        expect(!session.victory,
               "Victory stays inactive while any enemy remains undefeated");
        player.takeDamage(ai::PlayerHealth::maximum);
        session.update(player, enemies);
        expect(session.gameOver && !session.canRestart(false) && session.canRestart(true),
               "Game Over activates at zero Player HP and accepts an R restart request");

        enemies[0].takeDamage(45);
        enemies[1].takeDamage(35);
        enemies[2].takeDamage(100);
        session.restart(player, enemies);
        expect(!session.gameOver && !session.canRestart(true),
               "Restart clears the Game Over state");
        expect(player.current == 100 && enemies[0].current == 45 &&
               enemies[1].current == 35 && enemies[2].current == 100,
               "Restart restores Player and all enemy health values");
        expect(ai::shouldRenderEnemy(enemies[0]) && ai::shouldRenderEnemy(enemies[1]) &&
               ai::shouldRenderEnemy(enemies[2]),
               "Restart reactivates all enemies for gameplay and rendering");

        enemies[0].takeDamage(45);
        enemies[1].takeDamage(35);
        session.update(player, enemies);
        expect(!session.victory,
               "Defeating only some enemies does not trigger Victory");
        enemies[2].takeDamage(100);
        session.update(player, enemies);
        expect(session.victory && !session.gameOver && session.isPaused() &&
               !session.canRestart(false) && session.canRestart(true),
               "Defeating all enemies triggers Victory and pauses gameplay");

        // A terminal state does not change if update is called again.
        const int frozenPlayerHealth = player.current;
        session.update(player, enemies);
        expect(session.victory && player.current == frozenPlayerHealth,
               "Victory remains active while gameplay state is frozen");

        session.restart(player, enemies);
        expect(!session.victory && !session.gameOver && player.current == 100 &&
               enemies[0].current == 45 && enemies[1].current == 35 &&
               enemies[2].current == 100,
               "Restart from Victory resets Player and enemy health");

        for (ai::EnemyHealth& enemy : enemies)
            enemy.takeDamage(enemy.maximum);
        player.takeDamage(ai::PlayerHealth::maximum);
        session.update(player, enemies);
        expect(session.gameOver && !session.victory,
               "Game Over takes priority if Player and final enemy are defeated together");
    }

    void testSkeletonMovement()
    {
        ai::BehaviourTree tree;
        ai::buildSkeletonBehaviourTree(tree);

        ai::EnemyContext directionContext{};
        directionContext.enemyPosition = { 100.f, 100.f };
        directionContext.playerPosition = { 160.f, 180.f };
        ai::updateSkeletonVisibility(directionContext);
        tree.tick(directionContext, 0.016f);
        expect(directionContext.currentAction == ai::EnemyContext::Action::Chase,
               "Movement direction test selects Chase");
        expect(std::abs(directionContext.moveDirection.x - 0.6f) < 0.001f &&
               std::abs(directionContext.moveDirection.y - 0.8f) < 0.001f,
               "Chase direction is normalized from enemy ground point to player ground point");

        ai::EnemyContext stopContext{};
        stopContext.enemyPosition = { 40.f, 40.f };
        stopContext.playerPosition = { 80.f, 40.f };
        stopContext.moveDirection = { 1.f, 0.f };
        stopContext.hasLineOfSight = true;
        ai::updateSkeletonVisibility(stopContext);
        tree.tick(stopContext, 0.1f);
        const sf::Vector2f stoppedMovement = ai::moveSkeleton(
            stopContext, 0.1f, [](const sf::Vector2f&) { return true; });
        expect(stopContext.currentAction == ai::EnemyContext::Action::Attack,
               "Attack range selects Attack");
        expect(stoppedMovement == sf::Vector2f{ 0.f, 0.f } &&
               stopContext.enemyPosition == sf::Vector2f{ 40.f, 40.f },
               "Skeleton does not move while Attack is selected");
        expect(stopContext.moveDirection == sf::Vector2f{ 0.f, 0.f },
               "Attack clears the Skeleton movement direction");

        // A non-zero tile represents a wall, pillar, or brazier.
        std::array<std::array<int, 4>, 4> tiles{};
        tiles[1][2] = 1;
        const auto canOccupy = [&tiles](const sf::Vector2f& groundPosition)
        {
            return ai::isFootprintClear(groundPosition, 16.f, 12.f, tiles, 32.f,
                [](int tile) { return tile == 0; });
        };

        ai::EnemyContext collisionContext{};
        collisionContext.enemyPosition = { 40.f, 52.f };
        collisionContext.playerPosition = { 120.f, 52.f };
        collisionContext.attackRange = 20.f;
        collisionContext.detectionRange = 200.f;
        collisionContext.hasLineOfSight = true;
        collisionContext.moveSpeed = 100.f;
        ai::updateSkeletonVisibility(collisionContext);
        tree.tick(collisionContext, 0.1f);
        ai::moveSkeleton(collisionContext, 0.1f, canOccupy);
        expect(collisionContext.enemyPosition.x == 50.f,
               "Skeleton advances across a clear tile toward an obstacle");
        const sf::Vector2f blockedStep = ai::moveSkeleton(collisionContext, 0.1f, canOccupy);
        expect(collisionContext.enemyPosition.x == 50.f && blockedStep == sf::Vector2f{ 0.f, 0.f },
               "Foot collision prevents the Skeleton entering a blocked tile");
        expect(!canOccupy({ 7.f, 20.f }),
               "Foot collision rejects positions beyond the dungeon boundary");

        expect(std::abs(ai::skeletonMoveSpeed - 100.f * 4.f / 3.f) < 0.0001f,
               "Skeleton speed is increased by exactly one-third from 100 units per second");

        std::array<std::array<int, 8>, 3> speedGrid{};
        const auto speedWalkable = [](ai::GridCell cell)
        {
            return cell.column >= 0 && cell.column < 8 && cell.row >= 0 && cell.row < 3;
        };
        ai::EnemyContext fasterSkeleton{};
        fasterSkeleton.currentAction = ai::EnemyContext::Action::Chase;
        fasterSkeleton.enemyPosition = ai::gridCellWaypoint({ 1, 1 }, 128.f);
        fasterSkeleton.playerPosition = ai::gridCellWaypoint({ 7, 1 }, 128.f);
        fasterSkeleton.attackRange = 0.f;
        fasterSkeleton.moveSpeed = ai::skeletonMoveSpeed;
        ai::SkeletonPathState fasterSkeletonPath;
        const float skeletonStartX = fasterSkeleton.enemyPosition.x;
        const sf::Vector2f speedStep = ai::followSkeletonPath(
            fasterSkeleton, fasterSkeletonPath, 0.1f, speedGrid, 128.f,
            speedWalkable, [](const sf::Vector2f&) { return true; });
        expect(std::abs(speedStep.x - ai::skeletonMoveSpeed * 0.1f) < 0.01f,
               "Faster Skeleton movement uses the configured speed and delta time");
        for (int frame = 1; frame < 30; ++frame)
            ai::followSkeletonPath(fasterSkeleton, fasterSkeletonPath, 0.1f,
                speedGrid, 128.f, speedWalkable,
                [](const sf::Vector2f&) { return true; });
        expect(fasterSkeleton.enemyPosition.x < fasterSkeleton.playerPosition.x &&
               fasterSkeleton.enemyPosition.x > skeletonStartX &&
               !fasterSkeletonPath.pathFailed &&
               ai::isPathValid(fasterSkeletonPath.path, speedWalkable),
               "Faster Skeleton follows A* waypoints without overshooting or losing its path");
    }

    void testAStarPathfinding()
    {
        std::array<std::array<int, 5>, 5> grid{};
        grid[1][2] = 1;
        grid[2][2] = 1;
        grid[3][2] = 1;
        const auto walkable = [&grid](ai::GridCell cell)
        {
            return cell.column >= 0 && cell.column < 5 && cell.row >= 0 && cell.row < 5 &&
                grid[cell.row][cell.column] == 0;
        };

        const std::vector<ai::GridCell> path = ai::findAStarPath(
            { 0, 2 }, { 4, 2 }, grid, walkable);
        expect(!path.empty() && path.front() == ai::GridCell{ 0, 2 } &&
               path.back() == ai::GridCell{ 4, 2 },
               "A* finds a route around a wall to the requested destination");
        expect(ai::isPathValid(path, walkable),
               "A* path uses clear cells connected in four directions");
        expect(path.size() == 9,
               "Manhattan A* chooses a shortest route around the wall");

        grid[0][2] = 1;
        grid[4][2] = 1;
        expect(!ai::isPathValid(path, walkable),
               "Path validation rejects a route after a cell becomes blocked");

        std::array<std::array<int, 5>, 5> sealedGrid{};
        for (int row = 0; row < 5; ++row)
            sealedGrid[row][2] = 1;
        const auto sealedWalkable = [&sealedGrid](ai::GridCell cell)
        {
            return cell.column >= 0 && cell.column < 5 && cell.row >= 0 && cell.row < 5 &&
                sealedGrid[cell.row][cell.column] == 0;
        };
        expect(ai::findAStarPath({ 0, 2 }, { 4, 2 }, sealedGrid, sealedWalkable).empty(),
               "A* returns no path when a wall fully separates the destination");
    }

    void testCentralPassageNavigation()
    {
        constexpr int columns = 28;
        constexpr int rows = 16;
        constexpr float tileSize = 128.f;
        std::array<std::array<int, columns>, rows> dungeon{};
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                if (row == 0 || row == rows - 1 || column == 0 || column == columns - 1)
                    dungeon[row][column] = 1;
            }
        }
        for (int row = 1; row < rows - 1; ++row)
        {
            if (row < 6 || row > 9)
            {
                dungeon[row][13] = 1;
                dungeon[row][14] = 1;
            }
        }
        dungeon[6][11] = 1;
        dungeon[8][16] = 1;

        const auto walkable = [&dungeon, columns, rows, tileSize](ai::GridCell cell)
        {
            if (cell.column < 0 || cell.column >= columns || cell.row < 0 || cell.row >= rows)
                return false;
            const sf::Vector2f ground = ai::gridCellWaypoint(cell, tileSize, 104.f);
            return ai::isFootprintClear(ground, 28.f, 18.f, dungeon, tileSize,
                [](int tile) { return tile == 0; });
        };
        const auto path = ai::findAStarPath({ 10, 7 }, { 17, 7 }, dungeon, walkable);
        expect(!path.empty() && ai::isPathValid(path, walkable),
               "A* finds a valid route between rooms with obstacles nearby");
        const bool usesOpening = std::any_of(path.begin(), path.end(), [](ai::GridCell cell)
        {
            return (cell.column == 13 || cell.column == 14) &&
                cell.row >= 6 && cell.row <= 9;
        });
        expect(usesOpening,
               "A* routes through the four-row central passage, not through the divider");

        const auto canOccupy = [&dungeon, tileSize](const sf::Vector2f& ground)
        {
            return ai::isFootprintClear(ground, 28.f, 18.f, dungeon, tileSize,
                [](int tile) { return tile == 0; });
        };
        expect(ai::hasClearLineOfSight({ 10.5f * tileSize, 7.f * tileSize + 104.f },
                   { 17.5f * tileSize, 7.f * tileSize + 104.f }, canOccupy),
               "Ground-position line of sight stays clear through the passage");
        expect(!ai::hasClearLineOfSight({ 10.5f * tileSize, 5.f * tileSize + 104.f },
                    { 17.5f * tileSize, 5.f * tileSize + 104.f }, canOccupy),
               "The central divider blocks attacks outside its passage opening");
    }

    void testSkeletonCombat()
    {
        ai::PlayerHealth player;
        ai::SkeletonAttackState attack;
        auto update = [&](float delta, float distance = 50.f, bool lineOfSight = true)
        {
            return ai::updateSkeletonAttack(attack, player, true, distance,
                60.f, lineOfSight, delta);
        };

        expect(player.current == 100, "Player starts with 100 HP");
        expect(update(0.125f).frame == 1 && player.current == 100,
               "Skeleton attack begins with its anticipation frame");
        const ai::SkeletonAttackUpdate hit = update(0.125f);
        expect(hit.frame == 2 && hit.damageApplied && player.current == 90,
               "Attack hit frame deals 10 damage once");
        update(0.125f);
        expect(player.current == 90,
               "One attack animation cannot damage the Player more than once");

        for (int i = 0; i < 7; ++i)
            update(0.125f);
        expect(player.current == 90,
               "One-second cooldown prevents another hit before it expires");

        ai::SkeletonAttackState missedAttack;
        ai::PlayerHealth unhurtPlayer;
        ai::updateSkeletonAttack(missedAttack, unhurtPlayer, true, 50.f, 60.f, true, 0.125f);
        const ai::SkeletonAttackUpdate missedHit = ai::updateSkeletonAttack(
            missedAttack, unhurtPlayer, true, 65.f, 60.f, false, 0.125f);
        expect(!missedHit.damageApplied && unhurtPlayer.current == 100,
               "Hit frame does no damage when the Player moved out of range or behind a wall");

        expect(unhurtPlayer.takeDamage(150) && unhurtPlayer.current == 0 &&
               !unhurtPlayer.takeDamage(10),
               "Player health clamps at zero and cannot take damage after defeat");
    }

    void testSkeletonPathFollowing()
    {
        std::array<std::array<int, 4>, 4> grid{};
        const auto walkable = [&grid](ai::GridCell cell)
        {
            return cell.column >= 0 && cell.column < 4 && cell.row >= 0 && cell.row < 4 &&
                grid[cell.row][cell.column] == 0;
        };
        const auto canOccupy = [&grid](const sf::Vector2f& ground)
        {
            return ai::isFootprintClear(ground, 10.f, 8.f, grid, 32.f,
                [](int tile) { return tile == 0; });
        };

        ai::EnemyContext context{};
        context.currentAction = ai::EnemyContext::Action::Chase;
        context.enemyPosition = ai::gridCellCenter({ 0, 0 }, 32.f);
        context.playerPosition = ai::gridCellCenter({ 3, 0 }, 32.f);
        context.attackRange = 8.f;
        context.moveSpeed = 64.f;
        ai::SkeletonPathState state;
        const sf::Vector2f firstMovement = ai::followSkeletonPath(
            context, state, 0.25f, grid, 32.f, walkable, canOccupy);
        expect(std::abs(firstMovement.x - 16.f) < 0.001f &&
               std::abs(firstMovement.y) < 0.001f &&
               context.enemyPosition == sf::Vector2f{ 32.f, 16.f },
               "Skeleton follows its next grid waypoint using speed and delta time");

        ai::EnemyContext repathContext{};
        repathContext.currentAction = ai::EnemyContext::Action::Chase;
        repathContext.enemyPosition = { 60.f, 48.f };
        repathContext.playerPosition = { 112.f, 48.f };
        repathContext.moveSpeed = 64.f;
        ai::SkeletonPathState repathState;
        repathState.path = { { 1, 1 }, { 0, 1 } };
        repathState.nextWaypoint = 1;
        repathState.destination = { 0, 1 };
        repathState.lastStart = { 1, 1 };
        repathState.hasAttempted = true;
        const sf::Vector2f repathMovement = ai::followSkeletonPath(
            repathContext, repathState, 0.1f, grid, 32.f, walkable, canOccupy);
        expect(repathState.destination == ai::GridCell{ 3, 1 } && repathMovement.x > 0.f,
               "Replanning for a new target cell continues forward instead of returning to the old cell centre");

        ai::EnemyContext rangeContext{};
        rangeContext.currentAction = ai::EnemyContext::Action::Chase;
        rangeContext.enemyPosition = ai::gridCellCenter({ 0, 0 }, 32.f);
        rangeContext.playerPosition = ai::gridCellCenter({ 2, 0 }, 32.f);
        rangeContext.attackRange = 12.f;
        rangeContext.moveSpeed = 100.f;
        ai::SkeletonPathState rangeState;
        ai::followSkeletonPath(rangeContext, rangeState, 0.5f,
            grid, 32.f, walkable, canOccupy);
        ai::followSkeletonPath(rangeContext, rangeState, 0.5f,
            grid, 32.f, walkable, canOccupy);
        const sf::Vector2f rangeDifference =
            rangeContext.playerPosition - rangeContext.enemyPosition;
        expect(std::abs(std::sqrt(rangeDifference.x * rangeDifference.x +
                       rangeDifference.y * rangeDifference.y) - rangeContext.attackRange) < 0.001f,
               "Skeleton stops at its attack range while following waypoints");

        context.enemyPosition = ai::gridCellCenter({ 0, 0 }, 32.f);
        context.moveSpeed = 64.f;
        ai::SkeletonPathState blockedState;
        const auto blockedCanOccupy = [](const sf::Vector2f&) { return false; };
        const sf::Vector2f blockedMovement = ai::followSkeletonPath(
            context, blockedState, 0.25f, grid, 32.f, walkable, blockedCanOccupy);
        expect(blockedMovement == sf::Vector2f{ 0.f, 0.f } &&
               context.enemyPosition == sf::Vector2f{ 16.f, 16.f },
               "Skeleton collision callback prevents movement through an obstacle");

        // A prop foot box can block a navigation point even when its tile is floor.
        const sf::Vector2f propGround = ai::gridCellWaypoint({ 1, 0 }, 32.f, 26.f);
        const auto propBlockedWalkable = [propGround](ai::GridCell cell)
        {
            const sf::Vector2f node = ai::gridCellWaypoint(cell, 32.f, 26.f);
            const bool overlapsProp = std::abs(node.x - propGround.x) < (28.f + 20.f) / 2.f &&
                node.y - 18.f < propGround.y && node.y > propGround.y - 8.f;
            return cell.column >= 0 && cell.column < 4 && cell.row >= 0 && cell.row < 4 &&
                !overlapsProp;
        };
        expect(!propBlockedWalkable({ 1, 0 }),
               "Navigation rejects a prop cell using the Skeleton's 28x18 foot box");

        std::array<std::array<int, 4>, 4> dividedGrid{};
        for (int row = 0; row < 4; ++row)
            dividedGrid[row][1] = 1;
        const auto dividedWalkable = [&dividedGrid](ai::GridCell cell)
        {
            return cell.column >= 0 && cell.column < 4 && cell.row >= 0 && cell.row < 4 &&
                dividedGrid[cell.row][cell.column] == 0;
        };
        ai::EnemyContext noPathContext{};
        noPathContext.currentAction = ai::EnemyContext::Action::Chase;
        noPathContext.enemyPosition = ai::gridCellCenter({ 0, 1 }, 32.f);
        noPathContext.playerPosition = ai::gridCellCenter({ 3, 1 }, 32.f);
        ai::SkeletonPathState noPathState;
        const sf::Vector2f noPathMovement = ai::followSkeletonPath(
            noPathContext, noPathState, 0.1f, dividedGrid, 32.f,
            dividedWalkable, [](const sf::Vector2f&) { return true; });
        expect(noPathState.pathFailed && noPathMovement == sf::Vector2f{ 0.f, 0.f } &&
               noPathContext.moveDirection == sf::Vector2f{ 0.f, 0.f },
               "Skeleton holds position safely when A* cannot reach the Player");

        context.currentAction = ai::EnemyContext::Action::Attack;
        const sf::Vector2f stoppedMovement = ai::followSkeletonPath(
            context, state, 0.25f, grid, 32.f, walkable, canOccupy);
        expect(stoppedMovement == sf::Vector2f{ 0.f, 0.f } && state.path.empty(),
               "Skeleton clears its path and stops when Chase is no longer selected");
    }
}

int main()
{
    testSelector();
    testSequence();
    testLeafNodesAndTree();
    testSkeletonDecision();
    testArcherDecision();
    testArcherMovement();
    testArcherRepositionIntegration();
    testArcherCombat();
    testOgreDecision();
    testOgrePathfinding();
    testOgreCombat();
    testPlayerAttack();
    testPlayerCombat();
    testDefeatedEnemyRenderingAndInteraction();
    testGameOverAndRestart();
    testSkeletonMovement();
    testAStarPathfinding();
    testCentralPassageNavigation();
    testSkeletonCombat();
    testSkeletonPathFollowing();

    std::cout << "\n" << (failures == 0 ? "All tests passed." : "Test failures: ")
              << (failures == 0 ? 0 : failures) << '\n';
    return failures == 0 ? 0 : 1;
}
