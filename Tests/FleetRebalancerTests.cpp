#include "Core/Dispatcher.h"
#include "Core/Elevator.h"
#include "Core/FleetRebalancer.h"
#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <set>
#include <vector>

namespace
{
    SimulationConfig Config(int floors = 20)
    {
        SimulationConfig config;
        config.floorCount = floors;
        config.elevatorCount = 6;
        config.capacity = 10;
        config.moveTimePerFloor = 2.0;
        config.personTime = 1.0;
        return config;
    }

    ElevatorDispatchSnapshot IdleCar(int id, int floor, const SimulationConfig& config)
    {
        Elevator car(id, floor, config);
        return car.GetDispatchSnapshot();
    }

    double WeightAt(const std::vector<HallDemandWeight>& weights,
        int floor, Direction direction)
    {
        const auto found = std::find_if(weights.begin(), weights.end(),
            [floor, direction](const HallDemandWeight& weight)
            { return weight.floor == floor && weight.direction == direction; });
        if (found == weights.end()) throw std::runtime_error("demand weight missing");
        return found->probability;
    }

    const FloorCoverageSnapshot& CoverageAt(
        const std::vector<FloorCoverageSnapshot>& coverage, int floor)
    {
        const auto found = std::find_if(coverage.begin(), coverage.end(),
            [floor](const FloorCoverageSnapshot& item) { return item.floor == floor; });
        if (found == coverage.end()) throw std::runtime_error("floor coverage missing");
        return *found;
    }

    std::vector<int> FinalFloors(const std::vector<ElevatorDispatchSnapshot>& cars,
        const RebalancePlan& plan)
    {
        std::vector<int> floors;
        for (const auto& car : cars) floors.push_back(car.elevator.currentFloor);
        for (const auto& assignment : plan.assignments)
            floors[static_cast<std::size_t>(assignment.elevatorId)] = assignment.targetFloor;
        return floors;
    }

    double Mean(const std::vector<int>& values)
    {
        return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    }

    std::vector<ElevatorDispatchSnapshot> IdleFleet(
        int count, int floor, const SimulationConfig& config)
    {
        std::vector<ElevatorDispatchSnapshot> cars;
        for (int id = 0; id < count; ++id) cars.push_back(IdleCar(id, floor, config));
        return cars;
    }

    std::vector<int> AllIndices(int count)
    {
        std::vector<int> result(static_cast<std::size_t>(count));
        std::iota(result.begin(), result.end(), 0);
        return result;
    }
}

int main()
{
    TestSuite tests("FleetRebalancer");

    tests.Run("Uniform demand weights", [&]
    {
        constexpr int floors = 10;
        const auto weights = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::Uniform);
        double sum = 0.0;
        for (const auto& weight : weights) sum += weight.probability;
        tests.Near(sum, 1.0, "uniform weights sum to one");
        for (int floor = 1; floor <= floors; ++floor)
        {
            tests.Near(WeightAt(weights, floor, Direction::Up),
                static_cast<double>(floors - floor) / floors / (floors - 1),
                "uniform up formula");
            tests.Near(WeightAt(weights, floor, Direction::Down),
                static_cast<double>(floor - 1) / floors / (floors - 1),
                "uniform down formula");
        }
    });

    tests.Run("UpPeak demand weights", [&]
    {
        constexpr int floors = 20;
        const auto uniform = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::Uniform);
        const auto peak = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::UpPeak);
        tests.Near(WeightAt(peak, 1, Direction::Up),
            0.75 + 0.25 * WeightAt(uniform, 1, Direction::Up), "UpPeak lobby mixture");
        tests.Near(WeightAt(peak, 8, Direction::Up),
            0.25 * WeightAt(uniform, 8, Direction::Up), "UpPeak non-lobby up mixture");
        tests.Near(WeightAt(peak, 8, Direction::Down),
            0.25 * WeightAt(uniform, 8, Direction::Down), "UpPeak down mixture");
        tests.Check(WeightAt(peak, 1, Direction::Up) >
            WeightAt(peak, 2, Direction::Up), "lobby up is dominant");
    });

    tests.Run("DownPeak demand weights", [&]
    {
        constexpr int floors = 20;
        const auto uniform = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::Uniform);
        const auto peak = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::DownPeak);
        for (int floor = 2; floor <= floors; ++floor)
            tests.Near(WeightAt(peak, floor, Direction::Down),
                0.75 / (floors - 1) + 0.25 * WeightAt(uniform, floor, Direction::Down),
                "DownPeak source-floor mixture");
        tests.Near(WeightAt(peak, 1, Direction::Up),
            0.25 * WeightAt(uniform, 1, Direction::Up), "DownPeak residual uniform");
    });

    tests.Run("InterFloor demand weights", [&]
    {
        constexpr int floors = 20;
        const auto uniform = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::Uniform);
        const auto inter = FleetRebalancer::BuildDemandWeights(floors, TrafficPattern::InterFloor);
        tests.Near(WeightAt(inter, 1, Direction::Up),
            0.10 * WeightAt(uniform, 1, Direction::Up), "first floor only residual uniform");
        tests.Near(WeightAt(inter, 1, Direction::Down), 0.0, "invalid first-floor down is zero");
        for (int floor = 2; floor <= floors; ++floor)
        {
            const double denominator = static_cast<double>(floors - 1) * (floors - 2);
            tests.Near(WeightAt(inter, floor, Direction::Up),
                0.90 * (floors - floor) / denominator +
                    0.10 * WeightAt(uniform, floor, Direction::Up),
                "InterFloor up formula");
            tests.Near(WeightAt(inter, floor, Direction::Down),
                0.90 * (floor - 2) / denominator +
                    0.10 * WeightAt(uniform, floor, Direction::Down),
                "InterFloor down formula");
        }
        const auto fallback = FleetRebalancer::BuildDemandWeights(2, TrafficPattern::InterFloor);
        const auto uniformTwo = FleetRebalancer::BuildDemandWeights(2, TrafficPattern::Uniform);
        for (std::size_t index = 0; index < fallback.size(); ++index)
            tests.Near(fallback[index].probability, uniformTwo[index].probability,
                "L=2 uses generator fallback");
    });

    tests.Run("Traffic pattern changes demand", [&]
    {
        const auto up = FleetRebalancer::BuildDemandWeights(20, TrafficPattern::UpPeak);
        const auto down = FleetRebalancer::BuildDemandWeights(20, TrafficPattern::DownPeak);
        const auto inter = FleetRebalancer::BuildDemandWeights(20, TrafficPattern::InterFloor);
        tests.Check(WeightAt(up, 1, Direction::Up) > WeightAt(down, 1, Direction::Up),
            "UpPeak emphasizes lobby");
        tests.Check(WeightAt(down, 20, Direction::Down) > WeightAt(up, 20, Direction::Down),
            "DownPeak emphasizes upper origins");
        tests.Check(WeightAt(inter, 10, Direction::Up) > WeightAt(up, 10, Direction::Up),
            "InterFloor emphasizes interior origins");
    });

    tests.Run("Coverage uses Dispatcher ScoreSnapshot for every floor", [&]
    {
        const auto config = Config(10);
        const std::vector<ElevatorDispatchSnapshot> cars{
            IdleCar(0, 2, config), IdleCar(1, 9, config) };
        ElevatorDispatcher dispatcher;
        const auto coverage = FleetRebalancer::BuildCoverageSnapshots(cars, 10,
            TrafficPattern::UpPeak, 7.0, dispatcher);
        tests.Check(coverage.size() == 10, "coverage contains every floor");
        double expected = std::numeric_limits<double>::infinity();
        for (const auto& car : cars)
        {
            const auto score = dispatcher.ScoreSnapshot(1, Direction::Up, car, 7.0, 7.0);
            if (score.feasible) expected = (std::min)(expected, score.eta);
        }
        const auto& lobby = CoverageAt(coverage, 1);
        tests.Near(lobby.coverageEta, expected,
            "floor coverage is minimum feasible Dispatcher ETA");
        double interiorDemand = 0.0;
        double interiorWeightedEta = 0.0;
        for (const auto& weight : FleetRebalancer::BuildDemandWeights(10,
            TrafficPattern::UpPeak))
        {
            if (weight.floor != 5 || weight.probability == 0.0) continue;
            double bestEta = std::numeric_limits<double>::infinity();
            for (const auto& car : cars)
            {
                const auto score = dispatcher.ScoreSnapshot(5, weight.direction,
                    car, 7.0, 7.0);
                if (score.feasible) bestEta = (std::min)(bestEta, score.eta);
            }
            interiorDemand += weight.probability;
            interiorWeightedEta += weight.probability * bestEta;
        }
        const auto& interior = CoverageAt(coverage, 5);
        tests.Near(interior.demandWeight, interiorDemand,
            "interior coverage sums Up and Down demand weights");
        tests.Near(interior.coverageEta, interiorWeightedEta / interiorDemand,
            "interior coverage weights both Dispatcher ETAs");
        for (int floor = 1; floor <= 10; ++floor)
            tests.Check(coverage[static_cast<std::size_t>(floor - 1)].floor == floor,
                "coverage preserves one-based floor order");
    });

    tests.Run("Coverage shares observable capacity and fixed hall service estimates", [&] {
        auto config=Config(); config.capacity=1;
        auto car=IdleCar(0,10,config);
        car.elevator.state=ElevatorState::MovingDown; car.elevator.direction=Direction::Down;
        car.elevator.passengerCount=1;
        car.downTasks={5}; car.stopServices={{5,Direction::Down}};
        ElevatorDispatcher dispatcher;
        auto coverage=FleetRebalancer::BuildCoverageSnapshots({car},20,TrafficPattern::UpPeak,0,dispatcher);
        tests.Check(!std::isfinite(CoverageAt(coverage,1).coverageEta),"full with no car call remains uncovered");
        car.stopServices={{5,Direction::Idle}};
        const auto score=dispatcher.ScoreSnapshot(1,Direction::Up,car,0,0);
        coverage=FleetRebalancer::BuildCoverageSnapshots({car},20,TrafficPattern::UpPeak,0,dispatcher);
        tests.Check(score.feasible && score.projectedOccupancy==0,"known car call releases one seat");
        tests.Near(CoverageAt(coverage,1).coverageEta,score.eta,"coverage uses exact same new ScoreSnapshot result");
        car.downTasks.push_back(3); car.stopServices.push_back({3,Direction::Down});
        coverage=FleetRebalancer::BuildCoverageSnapshots({car},20,TrafficPattern::UpPeak,0,dispatcher);
        tests.Check(!dispatcher.ScoreSnapshot(1,Direction::Up,car).feasible &&
            !std::isfinite(CoverageAt(coverage,1).coverageEta),"estimated hall pickup consumes the released seat in both paths");
    });

    tests.Run("Busy and repositioning elevators participate in Coverage", [&]
    {
        const auto config = Config();
        ElevatorDispatcher dispatcher;
        const auto emptyCoverage = FleetRebalancer::BuildCoverageSnapshots({}, 20,
            TrafficPattern::UpPeak, 0.0, dispatcher);
        tests.Check(!std::isfinite(CoverageAt(emptyCoverage, 1).coverageEta),
            "no fleet has no feasible coverage");

        Elevator busy(0, 5, config);
        tests.Check(busy.AddInternalTarget(18), "create committed busy LOOK route");
        busy.Advance(0.5);
        const auto busySnapshot = busy.GetDispatchSnapshot();
        const auto busyScore = dispatcher.ScoreSnapshot(1, Direction::Up,
            busySnapshot, 0.0, 0.0);
        tests.Check(busyScore.feasible, "busy car is feasible for coverage fixture");
        const auto busyCoverage = FleetRebalancer::BuildCoverageSnapshots({ busySnapshot },
            20, TrafficPattern::UpPeak, 0.0, dispatcher);
        tests.Near(CoverageAt(busyCoverage, 1).coverageEta, busyScore.eta,
            "busy elevator contributes its Dispatcher ETA");

        Elevator repositioning(1, 5, config);
        tests.Check(repositioning.SetRepositionTarget(10), "create soft reposition route");
        repositioning.Advance(0.5);
        const auto repositionSnapshot = repositioning.GetDispatchSnapshot();
        const auto repositionScore = dispatcher.ScoreSnapshot(1, Direction::Up,
            repositionSnapshot, 0.0, 0.0);
        tests.Check(repositionScore.feasible, "repositioning car is feasible for coverage fixture");
        const auto repositionCoverage = FleetRebalancer::BuildCoverageSnapshots(
            { repositionSnapshot }, 20, TrafficPattern::UpPeak, 0.0, dispatcher);
        tests.Near(CoverageAt(repositionCoverage, 1).coverageEta, repositionScore.eta,
            "repositioning elevator contributes its Dispatcher ETA");
        tests.Check(CoverageAt(repositionCoverage, 10).hasRepositionTarget,
            "soft target floor is marked in coverage");
    });

    tests.Run("Coverage demand reflects peak traffic patterns", [&]
    {
        const auto config = Config();
        const auto cars = IdleFleet(1, 10, config);
        ElevatorDispatcher dispatcher;
        const auto up = FleetRebalancer::BuildCoverageSnapshots(cars, 20,
            TrafficPattern::UpPeak, 0.0, dispatcher);
        const auto down = FleetRebalancer::BuildCoverageSnapshots(cars, 20,
            TrafficPattern::DownPeak, 0.0, dispatcher);
        tests.Check(CoverageAt(up, 1).demandWeight >
            10.0 * CoverageAt(up, 2).demandWeight,
            "UpPeak makes first-floor demand clearly dominant");
        tests.Check(CoverageAt(down, 20).demandWeight >
            3.0 * CoverageAt(up, 20).demandWeight,
            "DownPeak clearly increases high-floor down demand");
    });

    tests.Run("Greedy marginal coverage disperses idle cars", [&]
    {
        const auto config = Config();
        const auto cars = IdleFleet(4, 10, config);
        ElevatorDispatcher dispatcher;
        const auto plan = FleetRebalancer{}.BuildPlan(cars, AllIndices(4), 20,
            TrafficPattern::Uniform, 1.0, 0.0, config, dispatcher);
        const auto floors = FinalFloors(cars, plan);
        tests.Check(std::set<int>(floors.begin(), floors.end()).size() >= 3,
            "idle fleet covers distinct regions");
    });

    tests.Run("Committed LOOK routes leave a middle coverage gap", [&]
    {
        const auto config = Config();
        std::vector<Elevator> elevators;
        elevators.emplace_back(0, 1, config);
        elevators.emplace_back(1, 20, config);
        elevators.emplace_back(2, 8, config);
        elevators.emplace_back(3, 12, config);
        elevators.emplace_back(4, 18, config);
        elevators.emplace_back(5, 20, config);
        elevators[2].AddInternalTarget(18);
        elevators[3].AddInternalTarget(20);
        elevators[4].AddInternalTarget(2);
        elevators[5].AddInternalTarget(1);
        std::vector<ElevatorDispatchSnapshot> cars;
        for (const auto& elevator : elevators) cars.push_back(elevator.GetDispatchSnapshot());
        ElevatorDispatcher dispatcher;
        const auto plan = FleetRebalancer{}.BuildPlan(cars, { 0, 1 }, 20,
            TrafficPattern::Uniform, 5.0, 0.0, config, dispatcher);
        tests.Check(plan.assignments.size() == 2, "both idle cars fill uncovered regions");
        std::vector<int> targets;
        for (const auto& assignment : plan.assignments)
            targets.push_back(assignment.targetFloor);
        tests.Check(*std::min_element(targets.begin(), targets.end()) > 1 &&
            *std::max_element(targets.begin(), targets.end()) < 20 &&
            Mean(targets) >= 8.0 && Mean(targets) <= 13.0,
            "Dispatcher-scored high and low routes leave aggregate middle coverage weakest");
    });

    tests.Run("Traffic patterns shift fleet layout", [&]
    {
        const auto config = Config();
        const auto cars = IdleFleet(6, 10, config);
        const auto indices = AllIndices(6);
        ElevatorDispatcher dispatcher;
        const auto up = FinalFloors(cars, FleetRebalancer{}.BuildPlan(cars, indices, 20,
            TrafficPattern::UpPeak, 1.0, 0.0, config, dispatcher));
        const auto down = FinalFloors(cars, FleetRebalancer{}.BuildPlan(cars, indices, 20,
            TrafficPattern::DownPeak, 1.0, 0.0, config, dispatcher));
        const auto inter = FinalFloors(cars, FleetRebalancer{}.BuildPlan(cars, indices, 20,
            TrafficPattern::InterFloor, 1.0, 0.0, config, dispatcher));
        tests.Check(Mean(up) < Mean(down), "UpPeak layout is lower than DownPeak");
        tests.Check(std::count(up.begin(), up.end(), 1) > std::count(inter.begin(), inter.end(), 1),
            "InterFloor stacks fewer cars at the lobby");
    });

    tests.Run("Zero and low traffic avoid wasteful empty travel", [&]
    {
        const auto config = Config();
        const auto cars = IdleFleet(1, 10, config);
        ElevatorDispatcher dispatcher;
        tests.Check(FleetRebalancer{}.BuildPlan(cars, { 0 }, 20, TrafficPattern::Uniform,
            0.0, 0.0, config, dispatcher).assignments.empty(), "zero rate has no reposition");
        tests.Check(FleetRebalancer{}.BuildPlan(cars, { 0 }, 20, TrafficPattern::Uniform,
            0.001, 0.0, config, dispatcher).assignments.empty(),
            "small coverage gain loses to empty-travel penalty");
    });

    tests.Run("Reposition target stays outside committed route", [&]
    {
        auto config = Config();
        Elevator car(0, 5, config);
        tests.Check(car.SetRepositionTarget(10), "soft target accepted");
        const auto snapshot = car.GetDispatchSnapshot();
        tests.Check(snapshot.elevator.repositionTargetFloor == 10, "target exposed only for display");
        tests.Check(snapshot.upTasks.empty() && snapshot.downTasks.empty() &&
            snapshot.stopServices.empty(), "soft target absent from committed route");
    });

    tests.Run("Repositioning elevator can win a real hall call", [&]
    {
        auto config = Config();
        Elevator repositioning(0, 5, config);
        Elevator distant(1, 20, config);
        tests.Check(repositioning.SetRepositionTarget(10), "start soft reposition");
        repositioning.Advance(1.0);
        ElevatorDispatcher dispatcher;
        const std::vector<ElevatorDispatchSnapshot> candidates{
            repositioning.GetDispatchSnapshot(), distant.GetDispatchSnapshot() };
        tests.Check(dispatcher.SelectFromSnapshots(4, Direction::Down, candidates, 0.0, 0.0) == 0,
            "moving reposition car remains the best real-call candidate");
        tests.Check(repositioning.AddHallCall(4, Direction::Down), "winning car accepts real call");
        tests.Check(!repositioning.IsRepositioning(), "real call clears soft target immediately");
        tests.Near(repositioning.GetTimeToNextEvent(), 1.0,
            "current floor segment is not cancelled");
        repositioning.Advance(1.0);
        const auto afterFloor = repositioning.GetSnapshot();
        tests.Check(afterFloor.currentFloor == 6 && afterFloor.state == ElevatorState::MovingDown &&
            afterFloor.direction == Direction::Down,
            "next floor resumes real LOOK route instead of old reposition target");
    });

    tests.Run("Large-fleet rebalance benchmark", [&]
    {
        auto config = Config(100);
        config.elevatorCount = 99;
        std::vector<ElevatorDispatchSnapshot> cars;
        cars.reserve(99);
        for (int id = 0; id < 99; ++id)
        {
            const int floor = id < 33 ? 1 : (id < 66 ? 100 : 50);
            cars.push_back(IdleCar(id, floor, config));
        }
        ElevatorDispatcher dispatcher;
        const auto start = std::chrono::steady_clock::now();
        const auto plan = FleetRebalancer{}.BuildPlan(cars, AllIndices(99), 100,
            TrafficPattern::Uniform, 1.0, 0.0, config, dispatcher);
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        tests.Check(!plan.assignments.empty(), "large fleet produces a plan");
        std::cout << "Fleet rebalance benchmark (100 floors, 99 elevators): "
            << elapsed << " ms\n";
        const auto coverageStart = std::chrono::steady_clock::now();
        const auto coverage = FleetRebalancer::BuildCoverageSnapshots(cars, 100,
            TrafficPattern::Uniform, 0.0, dispatcher);
        const auto coverageElapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - coverageStart).count();
        tests.Check(coverage.size() == 100, "large fleet coverage contains every floor");
        std::cout << "Floor coverage benchmark (100 floors, 99 elevators): "
            << coverageElapsed << " ms\n";
    });

    return tests.Finish();
}
