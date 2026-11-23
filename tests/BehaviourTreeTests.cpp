#include "ai/BehaviourTree.hpp"
#include "ai/CompositeNodes.hpp"
#include "ai/EnemyContext.hpp"
#include "ai/LeafNodes.hpp"
#include "ai/SkeletonDecision.hpp"

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
}

int main()
{
    testSelector();
    testSequence();
    testLeafNodesAndTree();
    testSkeletonDecision();

    std::cout << "\n" << (failures == 0 ? "All tests passed." : "Test failures: ")
              << (failures == 0 ? 0 : failures) << '\n';
    return failures == 0 ? 0 : 1;
}
