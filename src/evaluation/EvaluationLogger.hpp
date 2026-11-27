#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <system_error>

namespace evaluation
{
    struct EnemyMetrics
    {
        std::uint64_t behaviourTreeTicks = 0;
        std::uint64_t behaviourTreeTotalMicroseconds = 0;
        std::uint64_t behaviourTreeMaxMicroseconds = 0;
        std::uint64_t aStarCalls = 0;
        std::uint64_t aStarSuccesses = 0;
        std::uint64_t aStarFailures = 0;
        std::uint64_t attacks = 0;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t retreats = 0;
        std::uint64_t recoveries = 0;
        std::uint64_t detectionsAcquired = 0;
        std::uint64_t detectionsLost = 0;
    };

    class EvaluationLogger
    {
    public:
        explicit EvaluationLogger(
            const std::filesystem::path& directory = "evaluation/logs")
        {
            openSession(directory);
            resetMetrics();
        }

        ~EvaluationLogger()
        {
            if (enabled_)
            {
                writeSummaries(lastSimulationTime_);
                output_.flush();
            }
        }

        EvaluationLogger(const EvaluationLogger&) = delete;
        EvaluationLogger& operator=(const EvaluationLogger&) = delete;

        bool enabled() const { return enabled_; }
        const std::filesystem::path& outputPath() const { return outputPath_; }
        const std::string& sessionId() const { return sessionId_; }
        std::uint64_t trial() const { return trial_; }

        const EnemyMetrics& metrics(const std::string& enemy) const
        {
            static const EnemyMetrics empty{};
            const auto found = metrics_.find(enemy);
            return found == metrics_.end() ? empty : found->second;
        }

        void recordBehaviourTreeTick(double timeSeconds, const std::string& enemy,
            std::uint64_t durationMicroseconds)
        {
            lastSimulationTime_ = timeSeconds;
            EnemyMetrics& values = metrics_[enemy];
            ++values.behaviourTreeTicks;
            values.behaviourTreeTotalMicroseconds += durationMicroseconds;
            values.behaviourTreeMaxMicroseconds = std::max(
                values.behaviourTreeMaxMicroseconds, durationMicroseconds);
        }

        bool recordAction(double timeSeconds, const std::string& enemy,
            const std::string& action, float playerDistance, bool lineOfSight)
        {
            lastSimulationTime_ = timeSeconds;
            const auto found = lastActions_.find(enemy);
            if (found != lastActions_.end() && found->second == action)
                return false;

            lastActions_[enemy] = action;
            EnemyMetrics& values = metrics_[enemy];
            if (action == "Retreat")
                ++values.retreats;
            writeEvent(timeSeconds, enemy, "action_change", action,
                playerDistance, lineOfSight);
            return true;
        }

        bool recordDetection(double timeSeconds, const std::string& enemy,
            bool detected, float playerDistance, bool lineOfSight)
        {
            lastSimulationTime_ = timeSeconds;
            const auto found = detectionStates_.find(enemy);
            const bool previous = found != detectionStates_.end() && found->second;
            detectionStates_[enemy] = detected;
            if (previous == detected)
                return false;

            EnemyMetrics& values = metrics_[enemy];
            const std::string event = detected ? "detection_acquired" : "detection_lost";
            if (detected)
                ++values.detectionsAcquired;
            else
                ++values.detectionsLost;
            writeEvent(timeSeconds, enemy, event, {}, playerDistance, lineOfSight);
            return true;
        }

        void recordAStar(double timeSeconds, const std::string& enemy,
            bool success, std::uint64_t durationMicroseconds)
        {
            lastSimulationTime_ = timeSeconds;
            EnemyMetrics& values = metrics_[enemy];
            ++values.aStarCalls;
            if (success)
                ++values.aStarSuccesses;
            else
                ++values.aStarFailures;
            writeEvent(timeSeconds, enemy, success ? "astar_success" : "astar_failure",
                {}, 0.f, false, durationMicroseconds);
        }

        void recordCombat(double timeSeconds, const std::string& enemy,
            const std::string& event, float playerDistance = 0.f,
            bool lineOfSight = false)
        {
            lastSimulationTime_ = timeSeconds;
            EnemyMetrics& values = metrics_[enemy];
            if (event == "attack_attempt") ++values.attacks;
            if (event == "hit") ++values.hits;
            if (event == "miss") ++values.misses;
            if (event == "recovery_started") ++values.recoveries;
            writeEvent(timeSeconds, enemy, event, {}, playerDistance, lineOfSight);
        }

        // Finish the current run and begin a separately counted restart trial.
        void startNewTrial(double previousTrialTime)
        {
            lastSimulationTime_ = previousTrialTime;
            writeSummaries(previousTrialTime);
            ++trial_;
            resetMetrics();
            lastActions_.clear();
            detectionStates_.clear();
            lastSimulationTime_ = 0.0;
        }

    private:
        static std::string csv(const std::string& value)
        {
            std::string escaped = "\"";
            for (char character : value)
            {
                if (character == '"')
                    escaped += '"';
                escaped += character;
            }
            escaped += '"';
            return escaped;
        }

        void openSession(const std::filesystem::path& directory)
        {
            try
            {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (error)
                    return;

                const auto now = std::chrono::system_clock::now().time_since_epoch();
                const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
                static std::atomic<std::uint64_t> sequence{ 0 };
                sessionId_ = std::to_string(stamp) + "_" +
                    std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
                outputPath_ = directory / ("session_" + sessionId_ + ".csv");
                unsigned int suffix = 1;
                while (std::filesystem::exists(outputPath_, error) && !error)
                    outputPath_ = directory / ("session_" + sessionId_ + "_" +
                        std::to_string(suffix++) + ".csv");
                if (error)
                    return;

                output_.open(outputPath_, std::ios::out | std::ios::trunc);
                if (!output_)
                    return;

                enabled_ = true;
                output_ << "timestamp_s,session_id,trial,event,enemy,action,player_distance_world,"
                    "line_of_sight,duration_us,bt_ticks,bt_average_us,bt_max_us,astar_calls,"
                    "astar_successes,astar_failures,attacks,hits,misses,retreats,recoveries,"
                    "detections_acquired,detections_lost\n";
            }
            catch (...)
            {
                enabled_ = false;
            }
        }

        void resetMetrics()
        {
            metrics_.clear();
            metrics_.emplace("Skeleton Guard", EnemyMetrics{});
            metrics_.emplace("Goblin Archer", EnemyMetrics{});
            metrics_.emplace("Dungeon Ogre", EnemyMetrics{});
        }

        void writeEvent(double timeSeconds, const std::string& enemy,
            const std::string& event, const std::string& action = {},
            float playerDistance = 0.f, bool lineOfSight = false,
            std::uint64_t durationMicroseconds = 0)
        {
            if (!enabled_)
                return;
            output_ << std::fixed << std::setprecision(3) << timeSeconds << ','
                << csv(sessionId_) << ',' << trial_ << ',' << csv(event) << ','
                << csv(enemy) << ',' << csv(action) << ',' << std::setprecision(1)
                << playerDistance << ',' << (lineOfSight ? 1 : 0) << ','
                << durationMicroseconds << ",,,,,,,,,,,,,\n";
        }

        void writeSummaries(double timeSeconds)
        {
            if (!enabled_)
                return;
            for (const auto& entry : metrics_)
            {
                const EnemyMetrics& values = entry.second;
                const double average = values.behaviourTreeTicks == 0 ? 0.0 :
                    static_cast<double>(values.behaviourTreeTotalMicroseconds) /
                    static_cast<double>(values.behaviourTreeTicks);
                output_ << std::fixed << std::setprecision(3) << timeSeconds << ','
                    << csv(sessionId_) << ',' << trial_ << ",\"summary\"," << csv(entry.first)
                    << ",,,," << 0 << ',' << values.behaviourTreeTicks << ','
                    << std::setprecision(2) << average << ',' << values.behaviourTreeMaxMicroseconds
                    << ',' << values.aStarCalls << ',' << values.aStarSuccesses << ','
                    << values.aStarFailures << ',' << values.attacks << ',' << values.hits << ','
                    << values.misses << ',' << values.retreats << ',' << values.recoveries << ','
                    << values.detectionsAcquired << ',' << values.detectionsLost << '\n';
            }
        }

        std::ofstream output_;
        std::filesystem::path outputPath_;
        std::string sessionId_;
        bool enabled_ = false;
        std::uint64_t trial_ = 1;
        double lastSimulationTime_ = 0.0;
        std::map<std::string, EnemyMetrics> metrics_;
        std::map<std::string, std::string> lastActions_;
        std::map<std::string, bool> detectionStates_;
    };
}
