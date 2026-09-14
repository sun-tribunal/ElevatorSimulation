#include "SimulationWorker.h"

#include "Simulation.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    constexpr auto UpdateInterval = std::chrono::milliseconds(16);
    constexpr auto ObservationInterval = std::chrono::milliseconds(200);
    constexpr auto CoverageInterval = std::chrono::milliseconds(250);
}

SimulationWorker::SimulationWorker(const SimulationConfig& config,
    DispatcherExecutionMode mode, std::size_t dispatcherWorkerCount)
    : m_config(config), m_dispatcherMode(mode), m_dispatcherWorkerCount(dispatcherWorkerCount),
      m_thread(&SimulationWorker::ThreadMain, this)
{
}

SimulationWorker::SimulationWorker(const SimulationConfig& config, std::uint32_t seed,
    DispatcherExecutionMode mode, std::size_t dispatcherWorkerCount)
    : m_config(config), m_seed(seed), m_hasFixedSeed(true), m_dispatcherMode(mode),
      m_dispatcherWorkerCount(dispatcherWorkerCount), m_thread(&SimulationWorker::ThreadMain, this)
{
}

SimulationWorker::~SimulationWorker()
{
    Stop();
}

void SimulationWorker::Start()
{
    Enqueue({ CommandType::Start });
}

void SimulationWorker::Pause()
{
    Enqueue({ CommandType::Pause });
}

void SimulationWorker::Resume()
{
    Enqueue({ CommandType::Resume });
}

void SimulationWorker::Reset()
{
    Enqueue({ CommandType::Reset });
}

void SimulationWorker::AddPassengers(int startFloor, int upCount, int downCount)
{
    Command command;
    command.type = CommandType::AddPassengers;
    command.floor = startFloor;
    command.upCount = upCount;
    command.downCount = downCount;
    Enqueue(command);
}

void SimulationWorker::SetSimulationSpeed(double speed)
{
    Command command;
    command.type = CommandType::SetSimulationSpeed;
    command.speed = speed;
    Enqueue(command);
}

void SimulationWorker::ObserveHallCall(int floor, Direction direction)
{
    Enqueue({ CommandType::ObserveHallCall, floor, direction });
}

void SimulationWorker::ClearObservedHallCall()
{
    Enqueue({ CommandType::ClearObservedHallCall });
}

void SimulationWorker::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        if (!m_stopQueued)
        {
            m_commands.push_back({ CommandType::Stop });
            m_stopQueued = true;
        }
    }
    m_commandCondition.notify_one();
    if (m_thread.joinable()) m_thread.join();
}

std::shared_ptr<const SimulationUISnapshot> SimulationWorker::GetLatestSnapshot() const
{
    return std::atomic_load_explicit(&m_latestSnapshot, std::memory_order_acquire);
}

std::shared_ptr<const DispatchObservationSnapshot> SimulationWorker::GetLatestObservation() const
{
    return std::atomic_load_explicit(&m_latestObservation, std::memory_order_acquire);
}

void SimulationWorker::Enqueue(Command command)
{
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        if (m_stopQueued) return;
        m_commands.push_back(command);
    }
    m_commandCondition.notify_one();
}

void SimulationWorker::ThreadMain()
{
    Simulation simulation;
    try
    {
        simulation.SetDispatcherExecutionMode(m_dispatcherMode, m_dispatcherWorkerCount);
        if (m_hasFixedSeed)
            simulation.Initialize(m_config, m_seed);
        else
            simulation.Initialize(m_config);
        PublishSnapshot(simulation, true);

        using Clock = std::chrono::steady_clock;
        auto wallClockBase = Clock::now();
        auto nextObservation = wallClockBase;
        std::optional<std::pair<int, Direction>> observedHallCall;
        bool stopping = false;
        while (!stopping)
        {
            std::vector<Command> commands;
            {
                std::unique_lock<std::mutex> lock(m_commandMutex);
                if (simulation.IsRunning())
                    m_commandCondition.wait_for(lock, UpdateInterval, [this] { return !m_commands.empty(); });
                else
                    m_commandCondition.wait(lock, [this] { return !m_commands.empty(); });
                commands.assign(m_commands.begin(), m_commands.end());
                m_commands.clear();
            }

            const auto now = Clock::now();
            if (simulation.IsRunning())
            {
                const std::chrono::duration<double> elapsed = now - wallClockBase;
                simulation.Update(elapsed.count());
            }
            wallClockBase = now;

            bool observationRequested = false;
            for (const Command& command : commands)
            {
                switch (command.type)
                {
                case CommandType::Start: simulation.Start(); break;
                case CommandType::Pause: simulation.Pause(); break;
                case CommandType::Resume: simulation.Resume(); break;
                case CommandType::Reset:
                    simulation.Reset();
                    observationRequested = observedHallCall.has_value();
                    break;
                case CommandType::AddPassengers:
                    if (command.upCount > 0)
                        simulation.AddPassengersAtFloor(
                            command.floor, Direction::Up, command.upCount);
                    if (command.downCount > 0)
                        simulation.AddPassengersAtFloor(
                            command.floor, Direction::Down, command.downCount);
                    observationRequested = observedHallCall.has_value();
                    break;
                case CommandType::SetSimulationSpeed:
                    simulation.SetSimulationSpeed(command.speed);
                    break;
                case CommandType::ObserveHallCall:
                    observedHallCall = std::make_pair(command.floor, command.direction);
                    observationRequested = true;
                    break;
                case CommandType::ClearObservedHallCall:
                    observedHallCall.reset();
                    std::atomic_store_explicit(&m_latestObservation,
                        std::shared_ptr<const DispatchObservationSnapshot>(), std::memory_order_release);
                    break;
                case CommandType::Stop: stopping = true; break;
                }
                // 每个控制命令都建立新的墙钟基点，暂停时间不会进入下一次 Update。
                wallClockBase = Clock::now();
                if (stopping) break;
            }
            if (!stopping)
            {
                PublishSnapshot(simulation, true);
                const auto observationTime = Clock::now();
                if (observedHallCall && (observationRequested || observationTime >= nextObservation))
                {
                    if (!PublishObservation(simulation, observedHallCall->first, observedHallCall->second))
                        observedHallCall.reset();
                    nextObservation = observationTime + ObservationInterval;
                }
            }
        }
        PublishSnapshot(simulation, false);
    }
    catch (const std::exception& error)
    {
        auto failure = simulation.GetUISnapshot(false);
        failure.config = m_config;
        failure.lastError = error.what();
        std::shared_ptr<const SimulationUISnapshot> snapshot =
            std::make_shared<SimulationUISnapshot>(std::move(failure));
        std::atomic_store_explicit(&m_latestSnapshot, std::move(snapshot), std::memory_order_release);
        std::atomic_store_explicit(&m_latestObservation,
            std::shared_ptr<const DispatchObservationSnapshot>(), std::memory_order_release);
    }
}

void SimulationWorker::PublishSnapshot(const Simulation& simulation, bool workerActive)
{
    auto value = simulation.GetUISnapshot(workerActive, false);
    const auto now = std::chrono::steady_clock::now();
    const bool reset = m_lastCoverageSimulationTime != UnsetTime &&
        simulation.GetCurrentTime() < m_lastCoverageSimulationTime;
    if (!m_hasCoverage || reset || now >= m_nextCoverageRefresh)
    {
        m_cachedFloorCoverage = simulation.GetFloorCoverageSnapshots();
        m_lastCoverageSimulationTime = simulation.GetCurrentTime();
        m_nextCoverageRefresh = now + CoverageInterval;
        m_hasCoverage = true;
    }
    value.floorCoverage = m_cachedFloorCoverage;
    std::shared_ptr<const SimulationUISnapshot> snapshot =
        std::make_shared<SimulationUISnapshot>(std::move(value));
    std::atomic_store_explicit(&m_latestSnapshot, std::move(snapshot), std::memory_order_release);
}

bool SimulationWorker::PublishObservation(const Simulation& simulation, int floor, Direction direction)
{
    auto value = simulation.GetDispatchObservation(floor, direction);
    const bool valid = value.valid;
    std::shared_ptr<const DispatchObservationSnapshot> observation =
        std::make_shared<DispatchObservationSnapshot>(std::move(value));
    std::atomic_store_explicit(&m_latestObservation, std::move(observation), std::memory_order_release);
    return valid;
}
