#pragma once

#include "CommonTypes.h"

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class Simulation;

class SimulationWorker
{
public:
    explicit SimulationWorker(const SimulationConfig& config,
        DispatcherExecutionMode mode = DispatcherExecutionMode::Parallel,
        std::size_t dispatcherWorkerCount = 0);
    SimulationWorker(const SimulationConfig& config, std::uint32_t seed,
        DispatcherExecutionMode mode = DispatcherExecutionMode::Parallel,
        std::size_t dispatcherWorkerCount = 0);
    ~SimulationWorker();

    SimulationWorker(const SimulationWorker&) = delete;
    SimulationWorker& operator=(const SimulationWorker&) = delete;

    void Start();
    void Pause();
    void Resume();
    void Reset();
    void AddPassengers(int startFloor, int upCount, int downCount);
    void SetSimulationSpeed(double speed);
    void ObserveHallCall(int floor, Direction direction);
    void ClearObservedHallCall();
    void Stop();

    std::shared_ptr<const SimulationUISnapshot> GetLatestSnapshot() const;
    std::shared_ptr<const DispatchObservationSnapshot> GetLatestObservation() const;

private:
    enum class CommandType
    {
        Start, Pause, Resume, Reset, AddPassengers,
        ObserveHallCall, ClearObservedHallCall, Stop, SetSimulationSpeed
    };
    struct Command
    {
        CommandType type = CommandType::Start;
        int floor = 1;
        Direction direction = Direction::Idle;
        int upCount = 0;
        int downCount = 0;
        double speed = 1.0;
    };

    SimulationConfig m_config;
    std::uint32_t m_seed = 0;
    bool m_hasFixedSeed = false;
    DispatcherExecutionMode m_dispatcherMode = DispatcherExecutionMode::Parallel;
    std::size_t m_dispatcherWorkerCount = 0;
    mutable std::shared_ptr<const SimulationUISnapshot> m_latestSnapshot;
    mutable std::shared_ptr<const DispatchObservationSnapshot> m_latestObservation;
    std::mutex m_commandMutex;
    std::condition_variable m_commandCondition;
    std::deque<Command> m_commands;
    bool m_stopQueued = false;
    std::thread m_thread;
    std::vector<FloorCoverageSnapshot> m_cachedFloorCoverage;
    std::chrono::steady_clock::time_point m_nextCoverageRefresh;
    double m_lastCoverageSimulationTime = UnsetTime;
    bool m_hasCoverage = false;

    void Enqueue(Command command);
    void ThreadMain();
    void PublishSnapshot(const Simulation& simulation, bool workerActive);
    bool PublishObservation(const Simulation& simulation, int floor, Direction direction);
};
