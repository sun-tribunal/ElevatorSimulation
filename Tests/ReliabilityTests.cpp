#include "Core/Simulation.h"
#include "TestSupport.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// 可靠性多维检验：功能 / 性能 / 边界 / 一致性 / 稳定性 / 可扩展性。
namespace
{
    struct PerfRow
    {
        std::string dimension;
        std::string label;
        double wallMs = 0.0;
        std::size_t delivered = 0;
    };
    std::vector<PerfRow> g_perf;

    SimulationConfig Config(int floorCount = 20, int elevatorCount = 6, int capacity = 4,
        double rate = 0.0, double duration = 600.0)
    {
        SimulationConfig config;
        config.floorCount = floorCount;
        config.elevatorCount = elevatorCount;
        config.capacity = capacity;
        config.moveTimePerFloor = 1.0;
        config.personTime = 1.0;
        config.simulationDuration = duration;
        config.passengerRate = rate;
        return config;
    }

    void InjectBatch(Simulation& simulation, std::size_t count)
    {
        const int floors = simulation.GetConfig().floorCount;
        for (std::size_t i = 0; i < count; ++i)
        {
            const int start = static_cast<int>(i % static_cast<std::size_t>(floors)) + 1;
            const int target = (start - 1 + 1 +
                static_cast<int>(i % static_cast<std::size_t>(floors - 1))) % floors + 1;
            if (simulation.AddPassenger(start, target) == InvalidPassengerId)
                throw std::runtime_error("injection failed");
        }
    }

    double RunWallMs(Simulation& simulation, double seconds)
    {
        const auto begin = std::chrono::steady_clock::now();
        simulation.Update(seconds);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }

    void PrintPerfTable()
    {
        std::cout << "[reliability perf] dimension,label,wall_ms,delivered\n";
        for (const auto& row : g_perf)
            std::cout << std::fixed << std::setprecision(2) << row.dimension << ',' << row.label
                << ',' << row.wallMs << ',' << row.delivered << '\n';
    }
}

int main()
{
    TestSuite tests("Reliability");

    // ---------- 1. 功能验证 ----------
    tests.Run("functional end-to-end matrix", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        const int trips[][2] = { { 1, 20 }, { 20, 1 }, { 5, 15 }, { 15, 5 }, { 3, 10 },
            { 10, 3 }, { 8, 12 }, { 12, 8 }, { 2, 19 }, { 19, 2 }, { 7, 14 }, { 14, 7 } };
        for (const auto& trip : trips) simulation.AddPassenger(trip[0], trip[1]);
        simulation.Start(); simulation.Update(600);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 12 && stats.arrivedCount == 12, "all twelve delivered");
        std::size_t transported = 0;
        for (const auto& e : stats.elevators) transported += e.transportedCount;
        tests.Check(transported == 12, "transport sums to total");
        tests.Check(simulation.GetHallCallSnapshots().empty() && simulation.GetPassengerSnapshots().empty(),
            "no residual calls or passengers");
        tests.Check(simulation.ValidateState(), "consistent state");
    });

    // ---------- 2. 性能评估 ----------
    tests.Run("performance light load", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 1.0, 300.0), 1);
        simulation.Start();
        const double ms = RunWallMs(simulation, 300.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        g_perf.push_back({ "perf", "light_lambda1_300s", ms, stats.arrivedCount });
        tests.Check(stats.arrivedCount > 0, "light load delivered");
    });
    tests.Run("performance medium load", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 4.0, 300.0), 2);
        simulation.Start();
        const double ms = RunWallMs(simulation, 300.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        g_perf.push_back({ "perf", "medium_lambda4_300s", ms, stats.arrivedCount });
        tests.Check(stats.arrivedCount > 0, "medium load delivered");
    });
    tests.Run("performance high load", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 8.0, 300.0), 3);
        simulation.Start();
        const double ms = RunWallMs(simulation, 300.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        g_perf.push_back({ "perf", "high_lambda8_300s", ms, stats.arrivedCount });
        tests.Check(stats.arrivedCount > 0, "high load delivered");
    });
    tests.Run("performance bounded batch", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 0.0, 2000.0), 4);
        InjectBatch(simulation, 600);
        simulation.Start();
        const double ms = RunWallMs(simulation, 2000.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        g_perf.push_back({ "perf", "batch600", ms, stats.arrivedCount });
        tests.Check(stats.arrivedCount == 600, "batch fully delivered");
    });

    // ---------- 3. 边界测试 ----------
    tests.Run("boundary minimum building", [&] {
        Simulation simulation; simulation.Initialize(Config(2, 3, 2, 0.0, 60.0), 5);
        simulation.AddPassenger(1, 2); simulation.AddPassenger(2, 1);
        simulation.Start(); simulation.Update(120);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount == 2, "two-floor delivered");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("boundary large building", [&] {
        Simulation simulation; simulation.Initialize(Config(200, 12, 4, 0.0, 300.0), 6);
        simulation.AddPassenger(1, 200); simulation.AddPassenger(200, 1); simulation.AddPassenger(100, 50);
        simulation.Start(); simulation.Update(600);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount == 3, "200-floor delivered");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("boundary extreme inputs rejected", [&] {
        Simulation simulation; simulation.Initialize(Config(), 7);
        tests.Check(simulation.AddPassenger(0, 5) == InvalidPassengerId, "floor zero rejected");
        tests.Check(simulation.AddPassenger(21, 5) == InvalidPassengerId, "above top rejected");
        tests.Check(simulation.AddPassenger(5, 5) == InvalidPassengerId, "same floor rejected");
        for (double value : { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity() })
            simulation.Update(value);
        tests.Check(simulation.GetCurrentTime() == 0.0, "invalid deltas ignored");
        bool rejected = false;
        auto bad = Config(); bad.floorCount = 1;
        if (!simulation.Initialize(bad)) rejected = true;
        tests.Check(rejected, "invalid config rejected");
        tests.Check(simulation.GetState() == SimulationState::Ready && simulation.GetCurrentTime() == 0.0,
            "failed init preserves state");
    });
    tests.Run("boundary capacity one", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 1, 0.0, 300.0), 8);
        simulation.AddPassenger(1, 3); simulation.AddPassenger(1, 4); simulation.AddPassenger(1, 5);
        simulation.Start(); simulation.Update(300);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.arrivedCount == 3, "capacity one delivered");
        double fullTime = 0.0;
        for (const auto& e : stats.elevators) fullTime += e.fullTime;
        tests.Check(fullTime > 0.0, "full load recorded at capacity one");
    });
    tests.Run("boundary zero rate", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 0.0, 600.0), 9);
        simulation.Start(); simulation.Update(700);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount == 0, "no random passengers");
    });

    // ---------- 4. 一致性检查 ----------
    tests.Run("consistency seed determinism", [&] {
        Simulation a, b;
        a.Initialize(Config(20, 6, 4, 2.0, 300.0), 12345);
        b.Initialize(Config(20, 6, 4, 2.0, 300.0), 12345);
        a.Start(); b.Start();
        a.Update(300.0); b.Update(300.0);
        const auto sa = a.GetStatisticsSnapshot(), sb = b.GetStatisticsSnapshot();
        tests.Check(sa.totalPassengerCount == sb.totalPassengerCount &&
            sa.arrivedCount == sb.arrivedCount && sa.waitingCount == sb.waitingCount &&
            sa.ridingCount == sb.ridingCount, "deterministic counts");
        tests.Check(sa.averageWaitingTime == sb.averageWaitingTime &&
            sa.maxWaitingTime == sb.maxWaitingTime && sa.averageRideTime == sb.averageRideTime,
            "deterministic means");
        tests.Check(a.ValidateState() && b.ValidateState(), "both consistent");
    });
    tests.Run("consistency frame partition invariance", [&] {
        Simulation a, b;
        a.Initialize(Config(20, 6, 4, 2.0, 60.0), 42);
        b.Initialize(Config(20, 6, 4, 2.0, 60.0), 42);
        a.Start(); b.Start();
        for (int i = 0; i < 60; ++i) a.Update(1.0);
        b.Update(60.0);
        const auto sa = a.GetStatisticsSnapshot(), sb = b.GetStatisticsSnapshot();
        tests.Check(sa.totalPassengerCount == sb.totalPassengerCount && sa.arrivedCount == sb.arrivedCount &&
            sa.averageWaitingTime == sb.averageWaitingTime && sa.averageRideTime == sb.averageRideTime,
            "frame partition identical");
    });
    tests.Run("consistency conservation identities", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 1.5, 120.0), 7);
        simulation.Start(); simulation.Update(120.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == stats.waitingCount + stats.ridingCount + stats.arrivedCount,
            "population conservation");
        tests.Check(stats.boardedCount == stats.ridingCount + stats.arrivedCount, "boarded identity");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("consistency theoretical waiting equals T", [&] {
        auto config = Config(20, 3, 4, 0.0, 60.0);
        config.moveTimePerFloor = 1.0; config.personTime = 1.0;
        Simulation simulation; simulation.Initialize(config, 3);
        simulation.AddPassenger(1, 2);
        simulation.Start(); simulation.Update(60.0);
        tests.Near(simulation.GetStatisticsSnapshot().averageWaitingTime, 1.0, "wait equals boarding T");
    });
    tests.Run("consistency theoretical ride equals travel plus alight T", [&] {
        auto config = Config(20, 3, 4, 0.0, 60.0);
        config.moveTimePerFloor = 1.0; config.personTime = 1.0;
        Simulation simulation; simulation.Initialize(config, 4);
        simulation.AddPassenger(1, 3);
        simulation.Start(); simulation.Update(60.0);
        tests.Near(simulation.GetStatisticsSnapshot().averageRideTime, 3.0, "ride equals two floors plus alight T");
    });

    // ---------- 5. 稳定性测试 ----------
    tests.Run("stability one hour run", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 0.5, 3600.0), 987);
        simulation.Start(); simulation.Update(3600.0);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(simulation.IsFinished() && stats.totalPassengerCount > 0 && stats.arrivedCount > 0,
            "hour completed with delivery");
        tests.Check(stats.totalPassengerCount == stats.waitingCount + stats.ridingCount + stats.arrivedCount,
            "conservation after hour");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("stability reset repeat", [&] {
        Simulation simulation; simulation.Initialize(Config(20, 6, 4, 2.0, 120.0), 55);
        simulation.Start(); simulation.Update(120.0);
        const auto first = simulation.GetStatisticsSnapshot();
        simulation.Reset();
        tests.Check(simulation.GetRandomSeed() == 55, "seed preserved across reset");
        simulation.Start(); simulation.Update(120.0);
        const auto second = simulation.GetStatisticsSnapshot();
        tests.Check(first.totalPassengerCount == second.totalPassengerCount &&
            first.arrivedCount == second.arrivedCount &&
            first.averageWaitingTime == second.averageWaitingTime, "repeat identical after reset");
    });

    // ---------- 6. 可扩展性评估 ----------
    tests.Run("scalability elevator count", [&] {
        for (int n : { 3, 6, 12, 24 })
        {
            Simulation simulation; simulation.Initialize(Config(20, n, 4, 0.0, 600.0), 11);
            InjectBatch(simulation, 80);
            simulation.Start();
            const double ms = RunWallMs(simulation, 600.0);
            const auto stats = simulation.GetStatisticsSnapshot();
            g_perf.push_back({ "scale", "elevators=" + std::to_string(n), ms, stats.arrivedCount });
            tests.Check(stats.arrivedCount == 80, "all delivered");
            tests.Check(simulation.GetElevatorSnapshots().size() == static_cast<std::size_t>(n),
                "elevator count matches");
            tests.Check(simulation.ValidateState(), "consistent state");
        }
    });
    tests.Run("scalability floor count", [&] {
        for (int floors : { 10, 20, 40, 80 })
        {
            Simulation simulation; simulation.Initialize(Config(floors, 6, 4, 0.0, 600.0), 12);
            InjectBatch(simulation, 80);
            simulation.Start();
            const double ms = RunWallMs(simulation, 600.0);
            const auto stats = simulation.GetStatisticsSnapshot();
            g_perf.push_back({ "scale", "floors=" + std::to_string(floors), ms, stats.arrivedCount });
            tests.Check(stats.arrivedCount == 80, "all delivered");
            tests.Check(simulation.ValidateState(), "consistent state");
        }
    });
    tests.Run("scalability initial layout at scale", [&] {
        Simulation simulation; simulation.Initialize(Config(40, 24, 4, 0.0, 60.0), 13);
        const auto elevators = simulation.GetElevatorSnapshots();
        tests.Check(elevators.size() == 24, "twenty four elevators");
        for (int id = 0; id < 24; ++id)
        {
            const int expected = id < 8 ? 1 : (id < 16 ? 40 : 20);
            tests.Check(elevators[static_cast<std::size_t>(id)].currentFloor == expected, "group placement");
        }
    });

    const int result = tests.Finish();
    PrintPerfTable();
    return result;
}
