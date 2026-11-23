#include "ai/BehaviourTree.hpp"
#include "ai/CompositeNodes.hpp"
#include "ai/EnemyContext.hpp"
#include "ai/GridCollision.hpp"
#include "ai/LeafNodes.hpp"
#include "ai/SkeletonDecision.hpp"

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

        context.playerPosition = { 400.f, 100.f };
        ai::updateSkeletonVisibility(context);
        tree.tick(context, 0.016f);
        expect(context.currentAction == ai::EnemyContext::Action::Idle,
               "Skeleton selects Idle outside detection range");
        expect(!context.playerVisible, "Skeleton does not see player outside detection range");
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
    }
}

int main()
{
    testSelector();
    testSequence();
    testLeafNodesAndTree();
    testSkeletonDecision();
    testSkeletonMovement();

    std::cout << "\n" << (failures == 0 ? "All tests passed." : "Test failures: ")
              << (failures == 0 ? 0 : failures) << '\n';
    return failures == 0 ? 0 : 1;
}
