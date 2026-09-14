#include "Core/Simulation.h"
#include "Core/Elevator.h"
#include "Core/Dispatcher.h"
#include "TestSupport.h"

#include <cmath>
#include <vector>

// F 部分系统测试：典型情况（无乘客~长时间运行）、客流（低/正常/高）、
// 异常检查（超载、错误掉头、重复分配、目标层错误、等待乘客丢失）与统计字段。
namespace
{
    SimulationConfig Config(int capacity = 4)
    {
        SimulationConfig config;
        config.floorCount = 20;
        config.elevatorCount = 6;
        config.capacity = capacity;
        config.moveTimePerFloor = 1.0;
        config.personTime = 1.0;
        config.simulationDuration = 600.0;
        config.passengerRate = 0.0;
        return config;
    }

    bool IsFinite(double value)
    {
        return std::isfinite(value);
    }
}

int main()
{
    TestSuite tests("System");

    // ---------- 典型情况 ----------
    tests.Run("no passengers", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.Start(); simulation.Update(700); // 截断到 600 秒总时长。
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(simulation.GetCurrentTime() == 600.0 && simulation.IsFinished(), "finished at duration");
        tests.Check(stats.totalPassengerCount == 0 && stats.arrivedCount == 0 &&
            stats.waitingCount == 0 && stats.ridingCount == 0, "empty statistics");
        tests.Check(stats.averageWaitingTime == 0.0 && stats.averageRideTime == 0.0 &&
            stats.maxWaitingTime == 0.0, "zero means");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("single passenger", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.AddPassenger(1, 3); simulation.Start(); simulation.Update(300);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 1 && stats.arrivedCount == 1 &&
            stats.waitingCount == 0 && stats.ridingCount == 0, "single delivered");
        tests.Check(IsFinite(stats.averageWaitingTime) && stats.averageWaitingTime >= 1.0, "wait includes T");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("multiple passengers", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        const int trips[][2] = { { 1, 20 }, { 20, 1 }, { 5, 15 }, { 15, 5 }, { 3, 10 },
            { 10, 3 }, { 8, 12 }, { 12, 8 }, { 2, 19 }, { 19, 2 } };
        for (const auto& trip : trips) simulation.AddPassenger(trip[0], trip[1]);
        simulation.Start(); simulation.Update(400);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 10 && stats.arrivedCount == 10, "all ten delivered");
        std::size_t transported = 0;
        for (const auto& elevator : stats.elevators) transported += elevator.transportedCount;
        tests.Check(transported == 10, "per-elevator transport sums to total");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("full elevator transports", [&] {
        auto config = Config(1); // 满载：容量 1。
        config.moveTimePerFloor = 0.5; config.personTime = 0.5;
        Simulation simulation; simulation.Initialize(config, 1);
        simulation.AddPassenger(1, 3); simulation.AddPassenger(1, 4); simulation.AddPassenger(1, 5);
        simulation.Start(); simulation.Update(300);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 3 && stats.arrivedCount == 3, "all delivered at capacity one");
        double fullTime = 0.0;
        for (const auto& elevator : stats.elevators) fullTime += elevator.fullTime;
        tests.Check(fullTime > 0.0, "full-load seconds recorded");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("multiple idle elevators", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        const auto elevators = simulation.GetElevatorSnapshots();
        tests.Check(elevators.size() == 6, "six elevators");
        bool allIdle = true;
        for (const auto& elevator : elevators)
            allIdle = allIdle && elevator.state == ElevatorState::Idle && elevator.direction == Direction::Idle;
        tests.Check(allIdle, "all idle with no traffic");
    });
    tests.Run("opposite direction requests", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.AddPassenger(1, 20); simulation.AddPassenger(20, 1);
        simulation.Start(); simulation.Update(400);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 2 && stats.arrivedCount == 2, "both delivered");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("top and bottom floor requests", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.AddPassenger(20, 19); // 顶层下行
        simulation.AddPassenger(1, 2);   // 底层上行
        simulation.Start(); simulation.Update(400);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 2 && stats.arrivedCount == 2, "both delivered");
        // 非法方向请求：顶层向上、底层向下不可服务，调度器返回未分配。
        const ElevatorDispatcher dispatcher;
        const std::vector<Elevator> oneCar{ Elevator(0, 1, Config()) };
        tests.Check(dispatcher.SelectElevator(20, Direction::Up, oneCar) == InvalidElevatorId, "top up invalid");
        tests.Check(dispatcher.SelectElevator(1, Direction::Down, oneCar) == InvalidElevatorId, "bottom down invalid");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("long duration run", [&] {
        auto config = Config(); config.passengerRate = 0.3; config.simulationDuration = 3600.0;
        Simulation simulation; simulation.Initialize(config, 987);
        simulation.Start(); simulation.Update(4000);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(simulation.GetCurrentTime() == 3600.0 && simulation.IsFinished(), "finished at 3600s");
        tests.Check(stats.totalPassengerCount > 0 && stats.arrivedCount > 0, "long run generated and delivered");
        tests.Check(IsFinite(stats.averageWaitingTime) && IsFinite(stats.averageRideTime), "means finite");
        tests.Check(simulation.ValidateState(), "consistent state");
    });

    // ---------- 客流：低 / 正常 / 高 ----------
    tests.Run("low flow traffic", [&] {
        auto config = Config(); config.passengerRate = 0.05; config.simulationDuration = 3600.0;
        Simulation simulation; simulation.Initialize(config, 321);
        simulation.Start(); simulation.Update(4000);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount > 0 && stats.arrivedCount > 0, "low flow runs");
        tests.Check(stats.totalPassengerCount < 500, "low flow bounded");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("normal flow traffic", [&] {
        auto config = Config(); config.passengerRate = 0.6;
        Simulation simulation; simulation.Initialize(config, 987);
        simulation.Start(); simulation.Update(700);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount > 0 && stats.arrivedCount > 0, "normal flow runs");
        tests.Check(IsFinite(stats.averageWaitingTime), "finite mean wait");
        tests.Check(simulation.ValidateState(), "consistent state");
    });
    tests.Run("high flow traffic", [&] {
        auto config = Config(); config.passengerRate = 8.0;
        Simulation simulation; simulation.Initialize(config, 321);
        simulation.Start(); simulation.Update(700);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount > 1000, "high flow volume");
        tests.Check(stats.arrivedCount > 0 && IsFinite(stats.averageWaitingTime), "delivered with finite mean");
        tests.Check(simulation.ValidateState(), "consistent state");
    });

    // ---------- 异常情况检查 ----------
    tests.Run("overload prevented", [&] {
        Elevator car(0, 1, Config(1)); // 满载：容量 1。
        car.AddHallCall(1, Direction::Up);
        tests.Check(car.BeginBoarding(1, 3), "first seat");
        car.Advance(1.0); // 完成上梯传送后轿厢满载。
        tests.Check(!car.CanBoard() && !car.BeginBoarding(2, 4), "overload prevented at capacity");
    });
    tests.Run("wrong turn prevented", [&] {
        Elevator car(0, 5, Config());
        car.AddInternalTarget(8); car.AddInternalTarget(12);
        car.Advance(0.5); car.AddHallCall(3, Direction::Up);
        tests.Check(car.GetSnapshot().direction == Direction::Up, "direction locked while tasks ahead");
    });
    tests.Run("duplicate assignment deduplicated", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.AddPassenger(1, 5); simulation.AddPassenger(1, 6); simulation.AddPassenger(1, 7);
        simulation.Start(); simulation.Update(1.0);
        const auto calls = simulation.GetHallCallSnapshots();
        // 1 秒后首名乘客可能已完成上梯，队列人数为 2~3；外呼仍按 (楼层,方向) 唯一。
        tests.Check(calls.size() == 1 && calls[0].floorNumber == 1 &&
            calls[0].direction == Direction::Up && calls[0].waitingCount >= 2, "one hall call per key");
        tests.Check(calls[0].assignedElevatorId != InvalidElevatorId, "uniquely assigned");
        Elevator car(0, 1, Config());
        car.AddHallCall(3, Direction::Up); car.AddHallCall(3, Direction::Up);
        tests.Check(car.GetUpTasks().count(3) == 1, "single elevator task dedup");
    });
    tests.Run("wrong target floor rejected", [&] {
        Elevator car(0, 5, Config());
        car.AddHallCall(5, Direction::Up);
        tests.Check(!car.BeginBoarding(7, 3), "opposite direction destination rejected");
        tests.Check(!car.AddInternalTarget(21) && !car.AddInternalTarget(0), "target out of bounds rejected");
    });
    tests.Run("no waiting passenger lost", [&] {
        auto config = Config(); config.moveTimePerFloor = 1.0; config.personTime = 1.0;
        Simulation simulation; simulation.Initialize(config, 1);
        simulation.AddPassenger(1, 3);
        simulation.Start(); simulation.Update(0.5);
        // 上梯传送中，队头仍在队列（未丢失、未提前出队）。
        tests.Check(simulation.GetFloorSnapshots()[0].upWaitingCount == 1, "queued during transfer");
        simulation.Update(2.0);
        const auto people = simulation.GetPassengerSnapshots();
        tests.Check(people.size() == 1 && people[0].state == PassengerState::Riding, "riding after T");
        simulation.Update(300);
        tests.Check(simulation.GetPassengerSnapshots().empty() && simulation.GetHallCallSnapshots().empty(),
            "no leaked ids or calls after completion");
        tests.Check(simulation.ValidateState(), "consistent state");
    });

    // ---------- 统计结果 ----------
    tests.Run("statistics report final results", [&] {
        Simulation simulation; simulation.Initialize(Config(), 1);
        simulation.AddPassenger(1, 20); simulation.AddPassenger(20, 1); simulation.AddPassenger(5, 15);
        simulation.Start(); simulation.Update(400);
        const auto stats = simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount == 3 && stats.arrivedCount == 3 &&
            stats.boardedCount == 3, "totals");
        tests.Check(stats.averageWaitingTime >= 1.0 && stats.maxWaitingTime >= stats.averageWaitingTime,
            "wait statistics");
        tests.Check(stats.averageRideTime >= 1.0, "ride statistics");
        bool anyTravel = false, anyIdle = false;
        for (const auto& elevator : stats.elevators)
        {
            anyTravel = anyTravel || elevator.traveledFloors > 0 || elevator.emptyTravelFloors > 0;
            anyIdle = anyIdle || elevator.idleTime > 0.0;
        }
        tests.Check(anyTravel && anyIdle, "per-elevator movement and idle time");
    });
    return tests.Finish();
}
