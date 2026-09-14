#include "Core/Simulation.h"
#include "Core/SimulationWorker.h"
#include "TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    SimulationConfig TestConfig()
    {
        SimulationConfig config;
        config.floorCount = 20;
        config.elevatorCount = 6;
        config.capacity = 8;
        config.moveTimePerFloor = 0.5;
        config.personTime = 0.25;
        config.simulationDuration = 120.0;
        config.passengerRate = 2.0;
        return config;
    }

    void SameState(TestSuite& tests, const Simulation& sequential, const Simulation& parallel)
    {
        tests.Near(sequential.GetCurrentTime(), parallel.GetCurrentTime(), "same simulation clock");
        tests.Check(sequential.GetState() == parallel.GetState(), "same lifecycle state");
        const auto leftCars = sequential.GetElevatorSnapshots();
        const auto rightCars = parallel.GetElevatorSnapshots();
        tests.Check(leftCars.size() == rightCars.size(), "same elevator count");
        for (std::size_t index = 0; index < leftCars.size(); ++index)
            tests.Check(leftCars[index].id == rightCars[index].id &&
                leftCars[index].currentFloor == rightCars[index].currentFloor &&
                leftCars[index].direction == rightCars[index].direction &&
                leftCars[index].state == rightCars[index].state &&
                leftCars[index].passengerCount == rightCars[index].passengerCount &&
                leftCars[index].repositionTargetFloor == rightCars[index].repositionTargetFloor,
                "same elevator state");

        auto leftPassengers = sequential.GetPassengerSnapshots();
        auto rightPassengers = parallel.GetPassengerSnapshots();
        const auto byId = [](const auto& left, const auto& right) { return left.id < right.id; };
        std::sort(leftPassengers.begin(), leftPassengers.end(), byId);
        std::sort(rightPassengers.begin(), rightPassengers.end(), byId);
        tests.Check(leftPassengers.size() == rightPassengers.size(), "same active passengers");
        for (std::size_t index = 0; index < leftPassengers.size(); ++index)
        {
            const auto& left = leftPassengers[index];
            const auto& right = rightPassengers[index];
            tests.Check(left.id == right.id && left.startFloor == right.startFloor &&
                left.targetFloor == right.targetFloor && left.state == right.state &&
                left.elevatorId == right.elevatorId, "same passenger assignment");
            tests.Near(left.requestTime, right.requestTime, "same request time");
            tests.Near(left.boardTime, right.boardTime, "same board time");
            tests.Near(left.arrivalTime, right.arrivalTime, "same arrival time");
        }

        const auto leftCalls = sequential.GetHallCallSnapshots();
        const auto rightCalls = parallel.GetHallCallSnapshots();
        tests.Check(leftCalls.size() == rightCalls.size(), "same hall call count");
        for (std::size_t index = 0; index < leftCalls.size(); ++index)
            tests.Check(leftCalls[index].floorNumber == rightCalls[index].floorNumber &&
                leftCalls[index].direction == rightCalls[index].direction &&
                leftCalls[index].waitingCount == rightCalls[index].waitingCount &&
                leftCalls[index].assignedElevatorId == rightCalls[index].assignedElevatorId,
                "same hall call assignment");

        const auto leftStats = sequential.GetStatisticsSnapshot();
        const auto rightStats = parallel.GetStatisticsSnapshot();
        tests.Check(leftStats.totalPassengerCount == rightStats.totalPassengerCount &&
            leftStats.waitingCount == rightStats.waitingCount &&
            leftStats.ridingCount == rightStats.ridingCount &&
            leftStats.arrivedCount == rightStats.arrivedCount &&
            leftStats.boardedCount == rightStats.boardedCount, "same statistics counts");
        tests.Near(leftStats.averageWaitingTime, rightStats.averageWaitingTime, "same mean wait");
        tests.Near(leftStats.maxWaitingTime, rightStats.maxWaitingTime, "same max wait");
        tests.Near(leftStats.averageRideTime, rightStats.averageRideTime, "same mean ride");
    }

    template <typename Predicate>
    std::shared_ptr<const SimulationUISnapshot> WaitForSnapshot(
        const SimulationWorker& worker, Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto snapshot = worker.GetLatestSnapshot();
            if (snapshot && predicate(*snapshot)) return snapshot;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return nullptr;
    }

    template <typename Predicate>
    std::shared_ptr<const DispatchObservationSnapshot> WaitForObservation(
        const SimulationWorker& worker, Predicate predicate, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto observation = worker.GetLatestObservation();
            if (predicate(observation)) return observation;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return nullptr;
    }
}

int main()
{
    TestSuite tests("Concurrency");

    tests.Run("default dispatcher pool is capped", [&]
    {
        ElevatorDispatcher dispatcher(DispatcherExecutionMode::Parallel);
        const auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
        const auto expected = (std::min)(hardwareThreads > 1 ? hardwareThreads - 1 : 1, std::size_t{ 8 });
        tests.Check(dispatcher.GetWorkerCount() == expected, "default pool uses at most eight workers");

        ElevatorDispatcher explicitDispatcher(DispatcherExecutionMode::Parallel, 12);
        tests.Check(explicitDispatcher.GetWorkerCount() == 12, "explicit pool size remains unchanged");
    });

    tests.Run("UI snapshot omits passenger details", [&]
    {
        auto config = TestConfig();
        config.passengerRate = 0.0;
        Simulation simulation;
        tests.Check(simulation.Initialize(config, 42), "initialize snapshot fixture");
        tests.Check(simulation.AddPassenger(1, 2) != InvalidPassengerId, "add snapshot passenger");
        tests.Check(simulation.GetPassengerSnapshots().size() == 1, "passenger API retains details");
        tests.Check(simulation.GetUISnapshot(true).passengers.empty(), "UI snapshot skips passenger details");
    });

    tests.Run("sequential and parallel fixed-seed equivalence", [&]
    {
        const auto config = TestConfig();
        Simulation sequential;
        Simulation parallel;
        parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel, 4);
        tests.Check(sequential.Initialize(config, 20260902), "initialize sequential");
        tests.Check(parallel.Initialize(config, 20260902), "initialize parallel");
        tests.Check(parallel.GetDispatcherWorkerCount() == 4, "fixed dispatcher pool size");
        sequential.Start();
        parallel.Start();
        for (int step = 0; step < 120; ++step)
        {
            sequential.Update(1.0);
            parallel.Update(1.0);
            SameState(tests, sequential, parallel);
        }
        tests.Check(sequential.ValidateState() && parallel.ValidateState(), "both final states valid");
    });

    tests.Run("all traffic patterns keep sequential and parallel equivalent", [&]
    {
        constexpr std::array<TrafficPattern, 4> patterns = {
            TrafficPattern::Uniform, TrafficPattern::UpPeak,
            TrafficPattern::DownPeak, TrafficPattern::InterFloor
        };
        for (const auto pattern : patterns)
        {
            auto config = TestConfig();
            config.simulationDuration = 30.0;
            config.trafficPattern = pattern;
            Simulation sequential;
            Simulation parallel;
            parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel, 4);
            tests.Check(sequential.Initialize(config, 20260903), "initialize patterned sequential");
            tests.Check(parallel.Initialize(config, 20260903), "initialize patterned parallel");
            sequential.Start();
            parallel.Start();
            for (int step = 0; step < 30; ++step)
            {
                sequential.Update(1.0);
                parallel.Update(1.0);
                SameState(tests, sequential, parallel);
            }
            tests.Check(sequential.ValidateState() && parallel.ValidateState(),
                "patterned sequential and parallel states valid");
        }
    });

    tests.Run("observable dispatch is deterministic across execution and Update sizes", [&] {
        auto config=TestConfig(); config.simulationDuration=20; config.predictiveRebalancing=true;
        Simulation sequential,parallel;
        parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel,4);
        sequential.Initialize(config,20260909); parallel.Initialize(config,20260909);
        sequential.Start(); parallel.Start();
        for(int batch=0;batch<4;++batch) {
            sequential.Update(5);
            for(int frame=0;frame<40;++frame) parallel.Update(0.125);
            SameState(tests,sequential,parallel);
        }
        tests.Check(sequential.ValidateState() && parallel.ValidateState(),"conservation across execution modes and frames");
    });

    tests.Run("pause resume resets wall clock", [&]
    {
        auto config = TestConfig();
        config.passengerRate = 0.0;
        config.simulationDuration = 5.0;
        SimulationWorker worker(config, 42, DispatcherExecutionMode::Parallel, 2);
        auto snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Ready; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not initialize");
        worker.Start();
        snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Running && value.currentTime > 0.04; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not start");
        worker.Pause();
        snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Paused; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not pause");
        const double pausedTime = snapshot->currentTime;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        snapshot = worker.GetLatestSnapshot();
        tests.Near(snapshot->currentTime, pausedTime, "pause does not advance simulation");
        worker.Resume();
        snapshot = WaitForSnapshot(worker,
            [pausedTime](const auto& value)
            { return value.state == SimulationState::Running && value.currentTime > pausedTime + 0.04; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not resume");
        tests.Check(snapshot->currentTime < pausedTime + 0.5, "paused wall time not injected");
        worker.Stop();
        snapshot = worker.GetLatestSnapshot();
        tests.Check(snapshot && !snapshot->workerActive, "stop publishes inactive snapshot");
    });

    tests.Run("reset command preserves seed", [&]
    {
        auto config = TestConfig();
        config.simulationDuration = 5.0;
        SimulationWorker worker(config, 1234, DispatcherExecutionMode::Parallel, 2);
        auto snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Ready; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not initialize");
        worker.Start();
        snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.currentTime > 0.04; }, std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not run before reset");
        worker.Reset();
        snapshot = WaitForSnapshot(worker,
            [](const auto& value)
            { return value.state == SimulationState::Ready && value.currentTime == 0.0; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not reset");
        tests.Check(snapshot->randomSeed == 1234 && snapshot->statistics.totalPassengerCount == 0,
            "reset restores initial fixed-seed state");
    });

    tests.Run("worker queues manual directional passengers", [&]
    {
        auto config = TestConfig();
        config.passengerRate = 0.0;
        SimulationWorker worker(config, 88, DispatcherExecutionMode::Parallel, 2);
        auto snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Ready; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not initialize for manual passengers");
        worker.AddPassengers(7, 3, 2);
        snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.statistics.totalPassengerCount == 5; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not publish manual passengers");
        const auto floor = std::find_if(snapshot->floors.begin(), snapshot->floors.end(),
            [](const FloorSnapshot& item) { return item.floorNumber == 7; });
        tests.Check(floor != snapshot->floors.end() && floor->upWaitingCount == 3 &&
            floor->downWaitingCount == 2, "worker preserves manual direction counts");
    });

    tests.Run("worker publishes and clears read-only observation", [&]
    {
        auto config = TestConfig();
        config.passengerRate = 0.0;
        SimulationWorker worker(config, 55, DispatcherExecutionMode::Parallel, 2);
        auto snapshot = WaitForSnapshot(worker,
            [](const auto& value) { return value.state == SimulationState::Ready; },
            std::chrono::seconds(2));
        if (!snapshot) throw std::runtime_error("worker did not initialize for observation");
        worker.ObserveHallCall(7, Direction::Up);
        const auto observation = WaitForObservation(worker,
            [](const auto& value) { return value && !value->valid; }, std::chrono::seconds(2));
        if (!observation) throw std::runtime_error("worker did not publish invalid observation");
        tests.Check(observation->floor == 7 && observation->direction == Direction::Up,
            "worker preserves requested hall call identity");
        worker.ClearObservedHallCall();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (worker.GetLatestObservation() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        tests.Check(!worker.GetLatestObservation(), "worker clears observation snapshot");
    });

    tests.Run("running worker closes and joins", [&]
    {
        const auto begin = std::chrono::steady_clock::now();
        {
            auto config = TestConfig();
            config.simulationDuration = 30.0;
            SimulationWorker worker(config, 7, DispatcherExecutionMode::Parallel, 2);
            auto snapshot = WaitForSnapshot(worker,
                [](const auto& value) { return value.state == SimulationState::Ready; },
                std::chrono::seconds(2));
            if (!snapshot) throw std::runtime_error("worker did not initialize");
            worker.Start();
            snapshot = WaitForSnapshot(worker,
                [](const auto& value) { return value.state == SimulationState::Running; },
                std::chrono::seconds(2));
            if (!snapshot) throw std::runtime_error("worker did not enter running state");
        }
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        tests.Check(elapsed < std::chrono::seconds(3), "running worker joined without deadlock");
    });

    return tests.Finish();
}
