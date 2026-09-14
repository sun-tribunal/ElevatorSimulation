#include "Core/Simulation.h"
#include "Core/EventScheduler.h"
#include "TestSupport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

namespace
{
    SimulationConfig Config()
    {
        SimulationConfig config;
        config.floorCount = 6;
        config.elevatorCount = 3;
        config.capacity = 2;
        config.passengerRate = 0.0;
        config.simulationDuration = 100.0;
        return config;
    }

    constexpr std::array<TrafficPattern, 4> TrafficPatterns = {
        TrafficPattern::Uniform, TrafficPattern::UpPeak,
        TrafficPattern::DownPeak, TrafficPattern::InterFloor
    };

    SimulationConfig TrafficConfig(TrafficPattern pattern, double passengerRate = 300.0)
    {
        SimulationConfig config;
        config.floorCount = 20;
        config.elevatorCount = 6;
        config.capacity = 15;
        config.moveTimePerFloor = 100.0;
        config.personTime = 100.0;
        config.simulationDuration = 1.0;
        config.passengerRate = passengerRate;
        config.trafficPattern = pattern;
        return config;
    }

    std::vector<PassengerSnapshot> RunTraffic(
        TrafficPattern pattern, std::uint32_t seed, double passengerRate = 300.0)
    {
        Simulation simulation;
        if (!simulation.Initialize(TrafficConfig(pattern, passengerRate), seed))
            throw std::runtime_error("traffic fixture initialization failed");
        simulation.Start();
        simulation.Update(1.0);
        const auto passengers = simulation.GetPassengerSnapshots();
        if (passengers.size() != simulation.GetStatisticsSnapshot().totalPassengerCount)
            throw std::runtime_error("traffic fixture unexpectedly completed a passenger");
        return passengers;
    }

    void SamePassengerSequence(TestSuite& tests,
        const std::vector<PassengerSnapshot>& left, const std::vector<PassengerSnapshot>& right)
    {
        tests.Check(left.size()==right.size(),"same generated passenger count");
        for(std::size_t i=0;i<left.size();++i)
        {
            tests.Check(left[i].id==right[i].id && left[i].startFloor==right[i].startFloor &&
                left[i].targetFloor==right[i].targetFloor && left[i].state==right[i].state &&
                left[i].elevatorId==right[i].elevatorId,"same generated passenger fields");
            tests.Near(left[i].requestTime,right[i].requestTime,"same generated request time");
            tests.Near(left[i].boardTime,right[i].boardTime,"same generated board time");
            tests.Near(left[i].arrivalTime,right[i].arrivalTime,"same generated arrival time");
        }
    }

    Simulation FullUpFleet()
    {
        auto config=Config(); config.floorCount=20; config.capacity=1; config.simulationDuration=300;
        Simulation simulation; simulation.Initialize(config,42);
        simulation.AddPassenger(20,1); simulation.Start(); simulation.Update(44);
        simulation.AddPassenger(1,20); simulation.AddPassenger(1,20); simulation.AddPassenger(10,20);
        simulation.Update(6.125); // 三台满载上行，唯一已知下客点均为 20F。
        return simulation;
    }

    HallCallSnapshot HallAt(const Simulation& simulation, int floor, Direction direction)
    {
        for(const auto& call:simulation.GetHallCallSnapshots())
            if(call.floorNumber==floor && call.direction==direction) return call;
        throw std::runtime_error("expected hall call missing");
    }

    // 只重建 FullUpFleet 的已知单乘客直达路线，供独立的三请求规划作结果对照。
    // 不访问 Simulation 私有成员，也不重写可行性/容量算法。
    std::vector<ElevatorDispatchSnapshot> FullFleetSnapshots(const Simulation& simulation)
    {
        std::vector<ElevatorDispatchSnapshot> snapshots;
        const auto people=simulation.GetPassengerSnapshots();
        for(const auto& car:simulation.GetElevatorSnapshots())
        {
            const auto passenger=std::find_if(people.begin(),people.end(),[&](const auto& p) { return p.elevatorId==car.id; });
            if(passenger==people.end() || car.state!=ElevatorState::MovingUp || car.passengerCount!=1)
                throw std::runtime_error("full upward fixture changed");
            ElevatorDispatchSnapshot snapshot; snapshot.elevator=car; snapshot.floorCount=20;
            snapshot.betweenFloors=true; snapshot.upTasks={20}; snapshot.stopServices={{20,Direction::Idle}};
            snapshot.remainingActionTime=2-(simulation.GetCurrentTime()-passenger->boardTime-
                (car.currentFloor-passenger->startFloor)*2);
            snapshots.push_back(std::move(snapshot));
        }
        return snapshots;
    }

    void SameState(TestSuite& tests, const Simulation& a, const Simulation& b)
    {
        tests.Check(a.ValidateState() && b.ValidateState(), "both states valid");
        tests.Near(a.GetCurrentTime(), b.GetCurrentTime(), "same clock");
        const auto sa=a.GetStatisticsSnapshot(), sb=b.GetStatisticsSnapshot();
        tests.Check(sa.totalPassengerCount==sb.totalPassengerCount && sa.waitingCount==sb.waitingCount &&
            sa.ridingCount==sb.ridingCount && sa.arrivedCount==sb.arrivedCount,"same population");
        tests.Near(sa.averageWaitingTime,sb.averageWaitingTime,"same mean wait",1e-7);
        tests.Near(sa.averageRideTime,sb.averageRideTime,"same mean ride",1e-7);
        const auto ea=a.GetElevatorSnapshots(), eb=b.GetElevatorSnapshots();
        tests.Check(ea.size()==eb.size(),"same car count");
        for(std::size_t i=0;i<ea.size();++i)
            tests.Check(ea[i].currentFloor==eb[i].currentFloor && ea[i].direction==eb[i].direction &&
                ea[i].state==eb[i].state && ea[i].passengerCount==eb[i].passengerCount &&
                ea[i].repositionTargetFloor==eb[i].repositionTargetFloor,"same car state");
        const auto pa=a.GetPassengerSnapshots(), pb=b.GetPassengerSnapshots();
        tests.Check(pa.size()==pb.size(),"same active count");
        for(std::size_t i=0;i<pa.size();++i)
        {
            tests.Check(pa[i].id==pb[i].id && pa[i].startFloor==pb[i].startFloor &&
                pa[i].targetFloor==pb[i].targetFloor && pa[i].state==pb[i].state &&
                pa[i].elevatorId==pb[i].elevatorId,"same passenger state");
            tests.Near(pa[i].requestTime,pb[i].requestTime,"same request timestamp",1e-7);
            tests.Near(pa[i].boardTime,pb[i].boardTime,"same boarding timestamp",1e-7);
        }
        const auto ha=a.GetHallCallSnapshots(), hb=b.GetHallCallSnapshots();
        tests.Check(ha.size()==hb.size(),"same hall calls");
        for(std::size_t i=0;i<ha.size();++i)
            tests.Check(ha[i].floorNumber==hb[i].floorNumber && ha[i].direction==hb[i].direction &&
                ha[i].assignedElevatorId==hb[i].assignedElevatorId && ha[i].waitingCount==hb[i].waitingCount,
                "same request ownership");
    }

    void SameObservation(TestSuite& tests, const DispatchObservationSnapshot& left,
        const DispatchObservationSnapshot& right)
    {
        tests.Check(left.valid==right.valid && left.floor==right.floor && left.direction==right.direction &&
            left.waitingCount==right.waitingCount && left.assignedElevatorId==right.assignedElevatorId,
            "same observation request");
        tests.Near(left.firstRequestTime,right.firstRequestTime,"same observation request time");
        tests.Near(left.currentTime,right.currentTime,"same observation current time");
        tests.Check(left.candidates.size()==right.candidates.size(),"same observation candidate count");
        for(std::size_t i=0;i<left.candidates.size();++i)
        {
            const auto& a=left.candidates[i]; const auto& b=right.candidates[i];
            tests.Check(a.elevatorId==b.elevatorId && a.feasible==b.feasible &&
                a.projectedOccupancy==b.projectedOccupancy,"same observation candidate");
            if(a.feasible)
            {
                tests.Near(a.cost,b.cost,"same observation cost");
                tests.Near(a.eta,b.eta,"same observation ETA");
            }
        }
    }

    double FirstArrivalTime(double passengerRate, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        const double uniform = (static_cast<double>(random()) + 0.5) / 4294967296.0;
        return -std::log(uniform) / passengerRate;
    }

    double NextUnitRandomForTest(std::mt19937& random)
    {
        return (static_cast<double>(random()) + 0.5) / 4294967296.0;
    }

    void SameCompleteState(TestSuite& tests, const Simulation& left, const Simulation& right)
    {
        SameState(tests, left, right);
        const auto leftUI = left.GetUISnapshot();
        const auto rightUI = right.GetUISnapshot();
        tests.Check(leftUI.trafficScenario == rightUI.trafficScenario &&
            leftUI.activeTrafficPattern == rightUI.activeTrafficPattern &&
            leftUI.trafficPhaseIndex == rightUI.trafficPhaseIndex,
            "same complete traffic scenario state");
        const auto leftFloors = left.GetFloorSnapshots();
        const auto rightFloors = right.GetFloorSnapshots();
        tests.Check(leftFloors.size() == rightFloors.size(), "same complete floor count");
        for (std::size_t index = 0; index < leftFloors.size(); ++index)
            tests.Check(leftFloors[index].floorNumber == rightFloors[index].floorNumber &&
                leftFloors[index].upWaitingCount == rightFloors[index].upWaitingCount &&
                leftFloors[index].downWaitingCount == rightFloors[index].downWaitingCount,
                "same complete floor queues");

        const auto leftStats = left.GetStatisticsSnapshot();
        const auto rightStats = right.GetStatisticsSnapshot();
        tests.Check(leftStats.boardedCount == rightStats.boardedCount,
            "same complete boarded count");
        tests.Near(leftStats.maxWaitingTime, rightStats.maxWaitingTime,
            "same complete max wait", 1e-6);
        tests.Check(leftStats.floorTraffic.size() == rightStats.floorTraffic.size(),
            "same complete floor statistics count");
        for (std::size_t index = 0; index < leftStats.floorTraffic.size(); ++index)
        {
            const auto& a = leftStats.floorTraffic[index];
            const auto& b = rightStats.floorTraffic[index];
            tests.Check(a.floor == b.floor && a.generatedCount == b.generatedCount &&
                a.upRequestCount == b.upRequestCount &&
                a.downRequestCount == b.downRequestCount &&
                a.boardedCount == b.boardedCount &&
                a.totalWaitingTime == b.totalWaitingTime &&
                a.maxWaitingTime == b.maxWaitingTime,
                "same complete per-floor traffic statistics");
        }
        tests.Check(leftStats.elevators.size() == rightStats.elevators.size(),
            "same complete elevator statistics count");
        for (std::size_t index = 0; index < leftStats.elevators.size(); ++index)
        {
            const auto& a = leftStats.elevators[index];
            const auto& b = rightStats.elevators[index];
            tests.Check(a.id == b.id && a.transportedCount == b.transportedCount &&
                a.traveledFloors == b.traveledFloors &&
                a.emptyTravelFloors == b.emptyTravelFloors,
                "same complete elevator counters");
            tests.Near(a.idleTime, b.idleTime, "same complete idle time", 1e-6);
            tests.Near(a.fullTime, b.fullTime, "same complete full time", 1e-6);
        }

        const auto leftCalls = left.GetHallCallSnapshots();
        const auto rightCalls = right.GetHallCallSnapshots();
        for (std::size_t index = 0; index < leftCalls.size(); ++index)
            tests.Near(leftCalls[index].firstRequestTime, rightCalls[index].firstRequestTime,
                "same complete hall call time", 1e-6);
    }
}

int main()
{
    TestSuite tests("Simulation");
    tests.Run("floor traffic statistics accumulate and reset", [&] {
        Statistics statistics;
        statistics.Reset(3,4);
        statistics.PassengerCreated(1,Direction::Up);
        statistics.PassengerCreated(1,Direction::Up);
        statistics.PassengerCreated(3,Direction::Down);
        statistics.PassengerBoarded(1,2.0);
        statistics.PassengerBoarded(1,6.0);
        auto snapshot=statistics.GetSnapshot();
        tests.Check(snapshot.floorTraffic.size()==4,"one traffic slot per floor");
        const auto& first=snapshot.floorTraffic[0];
        const auto& third=snapshot.floorTraffic[2];
        tests.Check(first.floor==1 && first.generatedCount==2 &&
            first.upRequestCount==2 && first.downRequestCount==0,
            "first-floor generated and up counts");
        tests.Check(third.floor==3 && third.generatedCount==1 &&
            third.upRequestCount==0 && third.downRequestCount==1,
            "third-floor generated and down counts");
        tests.Check(first.boardedCount==2,"per-floor boarded count");
        tests.Near(first.totalWaitingTime/first.boardedCount,4.0,
            "per-floor average wait");
        tests.Near(first.maxWaitingTime,6.0,"per-floor maximum wait");
        statistics.Reset(3,4);
        snapshot=statistics.GetSnapshot();
        for(const auto& floor:snapshot.floorTraffic)
            tests.Check(floor.generatedCount==0 && floor.upRequestCount==0 &&
                floor.downRequestCount==0 && floor.boardedCount==0 &&
                floor.totalWaitingTime==0.0 && floor.maxWaitingTime==0.0,
                "reset clears floor traffic");
    });
    tests.Run("event calendar has deterministic total ordering", [&] {
        EventScheduler scheduler;
        scheduler.Push(5.0,SimulationEventType::PassengerArrival);
        scheduler.Push(5.0,SimulationEventType::TrafficPhaseChange);
        scheduler.Push(5.0,SimulationEventType::ElevatorAction,2);
        scheduler.Push(5.0,SimulationEventType::SimulationEnd);
        scheduler.Push(5.0,SimulationEventType::ElevatorAction,0);
        scheduler.Push(5.0,SimulationEventType::ElevatorAction,1);
        scheduler.Push(4.0,SimulationEventType::SimulationEnd);
        tests.Near(scheduler.Top().time,4.0,"earlier time wins"); scheduler.Pop();
        for(int id=0;id<3;++id)
        {
            tests.Check(scheduler.Top().type==SimulationEventType::ElevatorAction &&
                scheduler.Top().elevatorId==id,"same-time elevators ordered by ID");
            scheduler.Pop();
        }
        tests.Check(scheduler.Top().type==SimulationEventType::TrafficPhaseChange,
            "phase change follows elevator batch"); scheduler.Pop();
        tests.Check(scheduler.Top().type==SimulationEventType::PassengerArrival,
            "passenger follows elevator batch"); scheduler.Pop();
        tests.Check(scheduler.Top().type==SimulationEventType::SimulationEnd,
            "simulation end is last"); scheduler.Pop();
        scheduler.Push(6.0,SimulationEventType::PassengerArrival);
        scheduler.Push(6.0,SimulationEventType::PassengerArrival);
        const auto firstSequence=scheduler.Top().sequence; scheduler.Pop();
        tests.Check(firstSequence<scheduler.Top().sequence,"sequence stabilizes exact ties");
        scheduler.Clear(); tests.Check(scheduler.Empty(),"clear removes all events");
    });
    tests.Run("simultaneous elevator completions form one deterministic batch", [&] {
        auto config=Config(); config.capacity=1; config.simulationDuration=20;
        Simulation simulation; tests.Check(simulation.Initialize(config,42),"initialize simultaneous actions");
        simulation.AddPassenger(1,6); simulation.AddPassenger(6,1); simulation.AddPassenger(3,5);
        simulation.Start(); simulation.Update(config.personTime);
        const auto elevators=simulation.GetElevatorSnapshots();
        tests.Check(simulation.GetStatisticsSnapshot().ridingCount==3,"all simultaneous boards complete");
        for(int id=0;id<3;++id)
            tests.Check(elevators[static_cast<std::size_t>(id)].id==id &&
                (elevators[static_cast<std::size_t>(id)].state==ElevatorState::MovingUp ||
                 elevators[static_cast<std::size_t>(id)].state==ElevatorState::MovingDown),
                "batch stabilizes after every elevator action");
        tests.Check(simulation.ValidateState(),"simultaneous batch remains valid");
    });
    tests.Run("elevator action precedes coincident passenger arrival", [&] {
        constexpr std::uint32_t seed=123456u;
        auto config=Config(); config.floorCount=20; config.passengerRate=1.0;
        config.personTime=FirstArrivalTime(config.passengerRate,seed);
        config.simulationDuration=20;
        Simulation simulation; tests.Check(simulation.Initialize(config,seed),"initialize coincident events");
        simulation.AddPassenger(1,20); simulation.Start(); simulation.Update(config.personTime);
        const auto passengers=simulation.GetPassengerSnapshots();
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==2,
            "coincident random passenger generated");
        const auto manual=std::find_if(passengers.begin(),passengers.end(),
            [](const auto& passenger) { return passenger.id==0; });
        const auto generated=std::find_if(passengers.begin(),passengers.end(),
            [](const auto& passenger) { return passenger.id==1; });
        tests.Check(manual!=passengers.end() && manual->state==PassengerState::Riding,
            "elevator completion applied at coincident time");
        tests.Check(generated!=passengers.end(),"passenger arrival applied after elevator batch");
        tests.Near(manual->boardTime,config.personTime,"coincident board timestamp");
        tests.Near(generated->requestTime,config.personTime,"coincident request timestamp");
        tests.Check(simulation.ValidateState(),"coincident events stabilize once to valid state");
    });
    tests.Run("single passenger exact timeline", [&] {
        Simulation simulation; tests.Check(simulation.Initialize(Config(),42),"initialize");
        tests.Check(simulation.AddPassenger(1,3)==0,"first ID"); simulation.Start(); simulation.Update(2.0);
        auto people=simulation.GetPassengerSnapshots(); tests.Check(people.size()==1 && people[0].state==PassengerState::Waiting,"boarding pending");
        tests.Check(simulation.GetFloorSnapshots()[0].upWaitingCount==1,"remain queued until transfer completes");
        simulation.Update(1.0); people=simulation.GetPassengerSnapshots();
        tests.Check(people[0].state==PassengerState::Riding,"boarded at T"); tests.Near(people[0].boardTime,3,"board time");
        simulation.Update(4.0); tests.Check(simulation.GetElevatorSnapshots()[0].state==ElevatorState::Alighting,"alighting at destination");
        simulation.Update(2.0); tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==0,"not arrived early");
        simulation.Update(1.0); const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(stats.arrivedCount==1 && simulation.GetPassengerSnapshots().empty(),"arrival deletes active object");
        tests.Near(stats.averageWaitingTime,3,"waiting includes boarding"); tests.Near(stats.averageRideTime,7,"ride includes alighting");
        tests.Check(stats.elevators[0].transportedCount==1 && stats.elevators[0].traveledFloors==2,"transport statistics");
        tests.Check(simulation.ValidateState(),"ownership and conservation");
    });
    tests.Run("single downward passenger", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(6,1); simulation.Start(); simulation.Update(16);
        const auto stats=simulation.GetStatisticsSnapshot(); tests.Check(stats.arrivedCount==1,"down arrival");
        tests.Near(stats.averageWaitingTime,3,"down wait"); tests.Near(stats.averageRideTime,13,"down ride");
        tests.Check(stats.elevators[1].transportedCount==1 && simulation.ValidateState(),"top elevator assignment");
    });
    tests.Run("pause freezes transfer", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(1,3); simulation.Start(); simulation.Update(2);
        simulation.Pause(); simulation.Update(100); tests.Near(simulation.GetCurrentTime(),2,"paused clock");
        tests.Check(simulation.GetPassengerSnapshots()[0].state==PassengerState::Waiting,"paused boarding");
        simulation.Resume(); simulation.Update(1); tests.Near(simulation.GetPassengerSnapshots()[0].boardTime,3,"resume remaining T");
    });
    tests.Run("speed applied once to all events", [&] {
        auto config=Config(); config.simulationSpeed=2;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,3); simulation.Start(); simulation.Update(1.5);
        tests.Near(simulation.GetPassengerSnapshots()[0].boardTime,3,"not double multiplied"); simulation.Update(3.5);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==1,"same simulated timeline");
    });
    tests.Run("deadline during transfer", [&] {
        auto config=Config(); config.simulationDuration=2;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,3); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.IsFinished() && simulation.GetStatisticsSnapshot().waitingCount==1,"cutoff freezes pending board");
        tests.Check(simulation.GetPassengerSnapshots()[0].boardTime==UnsetTime && simulation.ValidateState(),"no event after deadline");
    });
    tests.Run("FIFO boarding order", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(1,6); simulation.AddPassenger(1,5);
        simulation.Start(); simulation.Update(3); auto people=simulation.GetPassengerSnapshots();
        tests.Check(people[0].state==PassengerState::Riding && people[1].state==PassengerState::Waiting,"oldest first");
        simulation.Update(3); people=simulation.GetPassengerSnapshots(); tests.Near(people[1].boardTime,6,"second sequential T");
        tests.Check(simulation.ValidateState(),"FIFO conservation");
    });
    tests.Run("multiple elevators run concurrently", [&] {
        auto config=Config(); config.capacity=1;
        Simulation simulation; simulation.Initialize(config,42);
        simulation.AddPassenger(1,6); simulation.AddPassenger(6,1);
        simulation.Start(); simulation.Update(0.01);
        // 首两台已经预留最后座位，第三个请求才能按容量规则分给中间梯。
        // 固定分配时机以验证预留容量；同一批请求的选择由各梯成本决定，不保证均分。
        simulation.AddPassenger(3,5); simulation.Update(3.1);
        tests.Check(simulation.GetStatisticsSnapshot().ridingCount==3,"three transfers overlap in simulated time");
        simulation.Update(30); const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(stats.arrivedCount==3,"all delivered");
        for(const auto& car:stats.elevators) tests.Check(car.transportedCount==1,"group distribution");
    });
    tests.Run("partial boarding retains and reassigns call", [&] {
        auto config=Config(); config.simulationDuration=200;
        Simulation simulation; simulation.Initialize(config,42); for(int i=0;i<5;++i) simulation.AddPassenger(5,6);
        simulation.Start(); simulation.Update(8); const auto calls=simulation.GetHallCallSnapshots();
        tests.Check(simulation.GetFloorSnapshots()[4].upWaitingCount==3,"three remain at landing");
        tests.Check(calls.size()==1 && calls[0].waitingCount==3 && calls[0].assignedElevatorId!=1,"not cleared or assigned to full car");
        for(int second=0;second<100;++second) { simulation.Update(1); tests.Check(simulation.ValidateState(),"partial-load conservation"); }
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==5 && simulation.GetHallCallSnapshots().empty(),"all remainder served");
    });
    tests.Run("one owner per direction call", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); for(int i=0;i<8;++i) simulation.AddPassenger(3,6);
        simulation.Start(); simulation.Update(0.5); auto calls=simulation.GetHallCallSnapshots();
        tests.Check(calls.size()==1 && calls[0].assignedElevatorId==2 && calls[0].waitingCount==8,"one group call");
        tests.Check(simulation.ValidateState(),"no duplicate car reservations");
    });
    tests.Run("opposite hall calls are independent", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(3,6); simulation.AddPassenger(3,1);
        simulation.Start(); simulation.Update(0.5); tests.Check(simulation.GetHallCallSnapshots().size()==2,"two directions");
        simulation.Update(50); tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==2 && simulation.ValidateState(),"both served");
    });
    tests.Run("one versus many waiting passengers leaves route estimates identical", [&] {
        auto config=Config(); config.floorCount=20;
        Simulation one,many; one.Initialize(config,42); many.Initialize(config,42);
        one.AddPassenger(4,6); many.AddPassenger(4,18);
        for(int i=0;i<5;++i) many.AddPassenger(4,19);
        one.AddPassenger(8,12); many.AddPassenger(8,12);
        one.Start(); many.Start(); one.Update(0.125); many.Update(0.125);
        tests.Check(one.GetStatisticsSnapshot().ridingCount==0 && many.GetStatisticsSnapshot().ridingCount==0,
            "no passenger has boarded");
        for(int floor:{4,8}) {
            const auto left=one.GetDispatchObservation(floor,Direction::Up);
            auto right=many.GetDispatchObservation(floor,Direction::Up);
            if(floor==4) tests.Check(left.waitingCount==1 && right.waitingCount==6,"real one and six waiting");
            right.waitingCount=left.waitingCount;
            SameObservation(tests,left,right);
            for(std::size_t i=0;i<left.candidates.size();++i)
                tests.Check(left.candidates[i].eta==right.candidates[i].eta &&
                    left.candidates[i].cost==right.candidates[i].cost,"exact score equality including other hall routes");
        }
    });
    tests.Run("waiting targets and queue length are invisible to dispatch", [&] {
        auto config=Config(); config.floorCount=20; config.moveTimePerFloor=1;
        config.personTime=1; config.simulationDuration=200;
        for(bool many:{false,true}) {
            Simulation a,b; a.Initialize(config,42); b.Initialize(config,42);
            for(auto* simulation:{&a,&b}) {
                simulation->AddPassenger(1,12);
                simulation->AddPassenger(20,1);
                simulation->AddPassenger(10,20);
            }
            a.AddPassenger(1,6); b.AddPassenger(1,18);
            if(many) for(int count=0;count<5;++count) b.AddPassenger(1,19);
            a.Start(); b.Start(); a.Update(0.1); b.Update(0.1);
            tests.Check(a.GetElevatorSnapshots()[0].state==ElevatorState::Boarding &&
                b.GetElevatorSnapshots()[0].state==ElevatorState::Boarding,"head still boarding, all others waiting");
            // 第二个外呼让队列信息有机会污染联合分配、改派及中途停站的 ETA。
            a.AddPassenger(8,9); b.AddPassenger(8,15); a.Update(0.01); b.Update(0.01);
            for(int floor:{1,8,10,20}) {
                const auto direction=floor==20 ? Direction::Down : Direction::Up;
                const auto left=a.GetDispatchObservation(floor,direction);
                auto right=b.GetDispatchObservation(floor,direction);
                tests.Check(left.valid && right.valid,"real hall call remains active");
                if(floor==1) tests.Check(right.waitingCount==left.waitingCount+(many ? 5 : 0),
                    "UI retains real queue count");
                right.waitingCount=left.waitingCount; // 仅 UI 人数允许不同。
                SameObservation(tests,left,right);
                for(std::size_t i=0;i<left.candidates.size();++i)
                    tests.Check(left.candidates[i].eta==right.candidates[i].eta &&
                        left.candidates[i].cost==right.candidates[i].cost,"scores and ETA exactly equal before Boarded");
            }
            a.Update(190); b.Update(190);
            tests.Check(a.GetStatisticsSnapshot().arrivedCount==5 &&
                b.GetStatisticsSnapshot().arrivedCount==(many ? 10u : 5u) &&
                a.ValidateState() && b.ValidateState(),"both true queues still fully served");
        }
    });
    tests.Run("new request cannot reverse occupied car", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(1,6); simulation.Start(); simulation.Update(4);
        simulation.AddPassenger(2,1); simulation.AddPassenger(5,6); simulation.Update(0.5);
        tests.Check(simulation.GetElevatorSnapshots()[0].direction==Direction::Up,"old internal target retained");
        simulation.Update(80); tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==3,"all calls eventually served");
    });
    tests.Run("zero random rate", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==0,"no accidental random passenger");
        for(const auto& car:simulation.GetStatisticsSnapshot().elevators) tests.Near(car.idleTime,100,"idle seconds");
    });
    tests.Run("arrival exactly at cutoff included", [&] {
        auto config=Config(); config.simulationDuration=8;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,2); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.IsFinished() && simulation.GetStatisticsSnapshot().arrivedCount==1,"completion at deadline");
        tests.Check(simulation.ValidateState(),"cutoff conservation");
    });
    tests.Run("boarding exactly at cutoff included", [&] {
        auto config=Config(); config.simulationDuration=3;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,2); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.GetStatisticsSnapshot().ridingCount==1,"completed boarding retained");
        tests.Check(simulation.GetElevatorSnapshots()[0].currentFloor==1 &&
            simulation.GetElevatorSnapshots()[0].state==ElevatorState::Stopped && simulation.ValidateState(),
            "deadline completion does not start follow-up movement");
    });
    tests.Run("random arrival exactly at cutoff is excluded", [&] {
        constexpr std::uint32_t seed=73u;
        auto config=Config(); config.passengerRate=1.0;
        config.simulationDuration=FirstArrivalTime(config.passengerRate,seed);
        Simulation simulation; tests.Check(simulation.Initialize(config,seed),"initialize exact random cutoff");
        simulation.Start(); simulation.Update(100);
        tests.Check(simulation.IsFinished() && simulation.GetStatisticsSnapshot().totalPassengerCount==0,
            "arrival at duration is not generated");
        tests.Check(simulation.GetPassengerSnapshots().empty() && simulation.ValidateState(),
            "exact cutoff leaves no random passenger");
    });
    tests.Run("cutoff during movement", [&] {
        auto config=Config(); config.simulationDuration=4;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,6); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.GetElevatorSnapshots()[0].currentFloor==1,"unfinished segment does not increment floor");
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==0,"still active at cutoff");
    });
    tests.Run("fractional travel and transfer duration", [&] {
        auto config=Config(); config.moveTimePerFloor=0.2; config.personTime=0.3; config.simulationDuration=0.8;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,2); simulation.Start(); simulation.Update(10);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==1,"fractional completion deadline");
        tests.Near(simulation.GetStatisticsSnapshot().averageRideTime,0.5,"fractional ride");
    });
    tests.Run("invalid injection preserves state", [&] {
        Simulation simulation; tests.Check(simulation.AddPassenger(1,2)==-1,"uninitialized"); simulation.Initialize(Config(),42);
        tests.Check(simulation.AddPassenger(1,1)==-1 && simulation.AddPassenger(0,2)==-1 && simulation.AddPassenger(1,7)==-1,"bounds");
        tests.Check(simulation.AddPassenger(1,2)==0,"no ID consumed by invalid request"); simulation.Start(); simulation.Update(100);
        tests.Check(simulation.AddPassenger(1,2)==-1,"finished");
    });
    tests.Run("directional batch injection", [&] {
        Simulation simulation; simulation.Initialize(Config(),42);
        tests.Check(simulation.AddPassengersAtFloor(3,Direction::Up,4),"add upward batch");
        tests.Check(simulation.AddPassengersAtFloor(3,Direction::Down,3),"add downward batch");
        const auto passengers=simulation.GetPassengerSnapshots();
        tests.Check(passengers.size()==7,"all batch passengers created");
        for(const auto& passenger:passengers) {
            tests.Check(passenger.startFloor==3,"batch start floor retained");
            tests.Check((passenger.direction==Direction::Up && passenger.targetFloor>3) ||
                (passenger.direction==Direction::Down && passenger.targetFloor<3),
                "batch target follows requested direction");
        }
        const auto floors=simulation.GetFloorSnapshots();
        tests.Check(floors[2].upWaitingCount==4 && floors[2].downWaitingCount==3,
            "directional queues expose batch counts");
        const std::size_t before=simulation.GetStatisticsSnapshot().totalPassengerCount;
        tests.Check(!simulation.AddPassengersAtFloor(6,Direction::Up,1) &&
            !simulation.AddPassengersAtFloor(1,Direction::Down,1) &&
            !simulation.AddPassengersAtFloor(3,Direction::Idle,1) &&
            !simulation.AddPassengersAtFloor(3,Direction::Up,0),"invalid batch rejected");
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==before &&
            simulation.ValidateState(),"invalid batch preserves state");
    });
    tests.Run("IDs are not reused after arrival", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(1,2); simulation.Start(); simulation.Update(10);
        tests.Check(simulation.AddPassenger(1,2)==1,"monotonic ID"); tests.Check(simulation.ValidateState(),"registry");
    });
    tests.Run("active snapshots cannot change core", [&] {
        Simulation simulation; simulation.Initialize(Config(),42); simulation.AddPassenger(1,3);
        auto people=simulation.GetPassengerSnapshots(); auto calls=simulation.GetHallCallSnapshots();
        people[0].targetFloor=999; calls[0].assignedElevatorId=999;
        tests.Check(simulation.GetPassengerSnapshots()[0].targetFloor==3 &&
            simulation.GetHallCallSnapshots()[0].assignedElevatorId==-1,"copy isolation");
    });
    tests.Run("uniform preserves legacy fixed-seed route sequence", [&] {
        tests.Check(SimulationConfig{}.trafficScenario==TrafficScenario::Fixed,
            "default traffic scenario is fixed");
        tests.Check(SimulationConfig{}.trafficPattern==TrafficPattern::Uniform,"default traffic is uniform");
        const auto passengers=RunTraffic(TrafficPattern::Uniform,123456u,50.0);
        const std::array<std::pair<int,int>,12> expected = {{
            {11,20},{6,15},{16,8},{7,13},{7,17},{3,4},
            {2,9},{9,20},{19,17},{17,4},{4,6},{16,3}
        }};
        tests.Check(passengers.size()==48,"legacy uniform generated count");
        for(std::size_t i=0;i<expected.size();++i)
            tests.Check(passengers[i].startFloor==expected[i].first &&
                passengers[i].targetFloor==expected[i].second,"legacy uniform route");
    });
    tests.Run("office day exposes each phase and generates its traffic pattern", [&] {
        auto config=TrafficConfig(TrafficPattern::Uniform,400.0);
        config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation simulation; tests.Check(simulation.Initialize(config,20260904u),
            "initialize office day phases");
        simulation.Start(); simulation.Update(0.249999);
        auto snapshot=simulation.GetUISnapshot();
        tests.Check(snapshot.trafficScenario==TrafficScenario::OfficeDay &&
            snapshot.trafficPhaseIndex==0 && snapshot.activeTrafficPattern==TrafficPattern::UpPeak,
            "first quarter is morning up peak");
        simulation.Update(0.000001); snapshot=simulation.GetUISnapshot();
        tests.Check(snapshot.trafficPhaseIndex==1 &&
            snapshot.activeTrafficPattern==TrafficPattern::InterFloor,
            "quarter boundary enters daytime inter-floor phase");
        simulation.Update(0.45); snapshot=simulation.GetUISnapshot();
        tests.Check(snapshot.trafficPhaseIndex==2 &&
            snapshot.activeTrafficPattern==TrafficPattern::DownPeak,
            "seventy-percent boundary enters evening down peak");
        simulation.Update(0.299999);

        const auto passengers=simulation.GetPassengerSnapshots();
        std::size_t morningCount=0, morningLobbyStarts=0;
        std::size_t daytimeCount=0, daytimeAvoidsLobby=0;
        std::size_t eveningCount=0, eveningLobbyTargets=0;
        for(const auto& passenger:passengers)
        {
            if(passenger.requestTime<0.25)
            {
                ++morningCount;
                if(passenger.startFloor==1) ++morningLobbyStarts;
            }
            else if(passenger.requestTime<0.70)
            {
                ++daytimeCount;
                if(passenger.startFloor!=1 && passenger.targetFloor!=1) ++daytimeAvoidsLobby;
            }
            else
            {
                ++eveningCount;
                if(passenger.targetFloor==1) ++eveningLobbyTargets;
            }
        }
        tests.Check(morningCount>100 && daytimeCount>100 && eveningCount>100,
            "every office phase has enough deterministic samples");
        tests.Check(static_cast<double>(morningLobbyStarts)/morningCount>0.65,
            "morning phase uses up-peak routes");
        tests.Check(static_cast<double>(daytimeAvoidsLobby)/daytimeCount>0.80,
            "daytime phase uses inter-floor routes");
        tests.Check(static_cast<double>(eveningLobbyTargets)/eveningCount>0.65,
            "evening phase uses down-peak routes");
        tests.Check(simulation.ValidateState(),"office phase traffic remains valid");
    });
    tests.Run("phase boundary discards out-of-phase arrival candidate", [&] {
        constexpr std::uint32_t seed=42u;
        std::mt19937 random(seed);
        const double firstLog=-std::log(NextUnitRandomForTest(random));
        const double baseRate=firstLog/(26.0*1.5);
        const double nextPhaseArrival=25.0-
            std::log(NextUnitRandomForTest(random))/(baseRate*0.75);
        tests.Check(nextPhaseArrival>26.0 && nextPhaseArrival<70.0,
            "boundary fixture resample lies in daytime phase");

        auto config=Config(); config.floorCount=20; config.passengerRate=baseRate;
        config.simulationDuration=100; config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation simulation; tests.Check(simulation.Initialize(config,seed),
            "initialize phase boundary fixture");
        simulation.Start(); simulation.Update(26.0);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==0,
            "old-rate candidate at 26 seconds was not retained");
        simulation.Update(nextPhaseArrival-26.0);
        const auto passengers=simulation.GetPassengerSnapshots();
        tests.Check(passengers.size()==1,"new phase produces one resampled arrival");
        tests.Near(passengers[0].requestTime,nextPhaseArrival,
            "arrival is sampled from phase boundary");
    });
    tests.Run("traffic patterns follow route distributions", [&] {
        for(const auto pattern:TrafficPatterns)
        {
            const auto passengers=RunTraffic(pattern,20260903u);
            tests.Check(passengers.size()>200,"enough fixed-seed traffic samples");
            for(const auto& passenger:passengers)
                tests.Check(passenger.startFloor!=passenger.targetFloor,"traffic route floors differ");

            const auto count=static_cast<double>(passengers.size());
            if(pattern==TrafficPattern::UpPeak)
            {
                const auto lobbyStarts=std::count_if(passengers.begin(),passengers.end(),
                    [](const auto& passenger) { return passenger.startFloor==1; });
                tests.Check(lobbyStarts/count>0.65,"up peak has a clear majority from 1F");
            }
            else if(pattern==TrafficPattern::DownPeak)
            {
                const auto lobbyTargets=std::count_if(passengers.begin(),passengers.end(),
                    [](const auto& passenger) { return passenger.targetFloor==1; });
                tests.Check(lobbyTargets/count>0.65,"down peak has a clear majority to 1F");
            }
            else if(pattern==TrafficPattern::InterFloor)
            {
                const auto avoidsLobby=std::count_if(passengers.begin(),passengers.end(),
                    [](const auto& passenger)
                    { return passenger.startFloor!=1 && passenger.targetFloor!=1; });
                tests.Check(avoidsLobby/count>0.80,"inter-floor traffic clearly avoids 1F");
            }
        }
    });
    tests.Run("all traffic patterns replay seed and reset", [&] {
        for(const auto pattern:TrafficPatterns)
        {
            const auto config=TrafficConfig(pattern,80.0);
            Simulation first,second;
            tests.Check(first.Initialize(config,4567u) && second.Initialize(config,4567u),
                "initialize matching traffic simulations");
            first.Start(); second.Start(); first.Update(1.0); second.Update(1.0);
            SameState(tests,first,second);
            const auto expectedPassengers=second.GetPassengerSnapshots();
            SamePassengerSequence(tests,first.GetPassengerSnapshots(),expectedPassengers);
            first.Reset();
            tests.Check(first.GetConfig().trafficPattern==pattern,"reset preserves traffic pattern");
            first.Start(); first.Update(1.0);
            SameState(tests,first,second);
            SamePassengerSequence(tests,first.GetPassengerSnapshots(),expectedPassengers);
        }
    });
    tests.Run("fixed seed replay", [&] {
        auto config=Config(); config.passengerRate=0.4;
        Simulation a,b; a.Initialize(config,123); b.Initialize(config,123); a.Start(); b.Start(); a.Update(80); b.Update(80);
        tests.Check(a.GetStatisticsSnapshot().totalPassengerCount>0,"random events generated"); SameState(tests,a,b);
    });
    tests.Run("update partition independent random timeline", [&] {
        auto config=Config(); config.passengerRate=0.4; config.simulationDuration=200;
        Simulation a,b; a.Initialize(config,123); b.Initialize(config,123); a.Start(); b.Start(); a.Update(100);
        for(int frame=0;frame<800;++frame) b.Update(0.125);
        SameState(tests,a,b);
    });
    tests.Run("one large update matches small updates completely", [&] {
        auto config=Config(); config.floorCount=12; config.elevatorCount=6; config.capacity=4;
        config.moveTimePerFloor=1.25; config.personTime=0.75;
        config.passengerRate=0.8; config.simulationDuration=150;
        Simulation large,small;
        tests.Check(large.Initialize(config,918273u) && small.Initialize(config,918273u),
            "initialize partition comparison");
        large.Start(); small.Start(); large.Update(100.0);
        for(int frame=0;frame<2000;++frame) small.Update(0.05);
        SameCompleteState(tests,large,small);
    });
    tests.Run("office day is independent of update partition", [&] {
        auto config=Config(); config.floorCount=12; config.elevatorCount=6; config.capacity=4;
        config.moveTimePerFloor=1.25; config.personTime=0.75;
        config.passengerRate=0.8; config.simulationDuration=100;
        config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation large,small;
        tests.Check(large.Initialize(config,918273u) && small.Initialize(config,918273u),
            "initialize office partition comparison");
        large.Start(); small.Start(); large.Update(100.0);
        for(int frame=0;frame<2000;++frame) small.Update(0.05);
        SameCompleteState(tests,large,small);
        const auto snapshot=large.GetUISnapshot();
        tests.Check(snapshot.trafficPhaseIndex==2 &&
            snapshot.activeTrafficPattern==TrafficPattern::DownPeak,
            "partition comparison crosses every office phase");
    });
    tests.Run("office day reset replays phases and passengers", [&] {
        auto config=TrafficConfig(TrafficPattern::Uniform,120.0);
        config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation simulation; tests.Check(simulation.Initialize(config,4567u),
            "initialize office reset replay");
        simulation.Start(); simulation.Update(0.999);
        const auto expectedPassengers=simulation.GetPassengerSnapshots();
        const auto expectedUI=simulation.GetUISnapshot();
        tests.Check(expectedUI.trafficPhaseIndex==2 && expectedPassengers.size()>80,
            "office replay fixture crosses all phases");
        simulation.Reset(); simulation.Start(); simulation.Update(0.999);
        SamePassengerSequence(tests,simulation.GetPassengerSnapshots(),expectedPassengers);
        const auto replayUI=simulation.GetUISnapshot();
        tests.Check(replayUI.trafficScenario==TrafficScenario::OfficeDay &&
            replayUI.trafficPhaseIndex==expectedUI.trafficPhaseIndex &&
            replayUI.activeTrafficPattern==expectedUI.activeTrafficPattern,
            "reset replays office phase state");
        tests.Check(simulation.ValidateState(),"office reset replay remains valid");
    });
    tests.Run("office day sequential and parallel dispatch agree", [&] {
        auto config=Config(); config.floorCount=12; config.elevatorCount=6;
        config.passengerRate=1.2; config.simulationDuration=100;
        config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation sequential,parallel;
        parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel,4);
        tests.Check(sequential.Initialize(config,20260904u) &&
            parallel.Initialize(config,20260904u),"initialize office dispatcher modes");
        sequential.Start(); parallel.Start();
        for(int step=0;step<100;++step)
        {
            sequential.Update(1.0); parallel.Update(1.0);
            SameCompleteState(tests,sequential,parallel);
        }
    });
    tests.Run("simulation speed crosses office phases once", [&] {
        auto config=Config(); config.passengerRate=0.0; config.simulationDuration=100;
        config.simulationSpeed=5.0; config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation simulation; tests.Check(simulation.Initialize(config,42),
            "initialize office speed fixture");
        simulation.Start(); simulation.Update(4.9998);
        tests.Check(simulation.GetUISnapshot().trafficPhaseIndex==0,
            "real delta before scaled quarter stays in morning phase");
        simulation.Update(0.0002);
        tests.Near(simulation.GetCurrentTime(),25.0,"speed applied once at quarter boundary");
        tests.Check(simulation.GetUISnapshot().trafficPhaseIndex==1,
            "scaled quarter enters daytime phase");
        simulation.Update(9.0);
        tests.Near(simulation.GetCurrentTime(),70.0,"speed reaches seventy-percent boundary once");
        tests.Check(simulation.GetUISnapshot().trafficPhaseIndex==2,
            "scaled seventy percent enters evening phase");
    });
    tests.Run("arrival-time dispatch uses absolute scheduled remainder", [&] {
        constexpr std::uint32_t seed=123456u;
        auto config=Config(); config.floorCount=20; config.capacity=4;
        config.moveTimePerFloor=2.0; config.personTime=0.25;
        config.passengerRate=1.0; config.simulationDuration=30;
        const double arrivalTime=FirstArrivalTime(config.passengerRate,seed);
        tests.Check(arrivalTime>config.personTime &&
            arrivalTime<config.personTime+config.moveTimePerFloor,
            "arrival lies inside first movement action");
        Simulation simulation; tests.Check(simulation.Initialize(config,seed),"initialize scheduled remainder fixture");
        simulation.AddPassenger(1,20); simulation.Start(); simulation.Update(arrivalTime);
        const auto passengers=simulation.GetPassengerSnapshots();
        const auto request=std::find_if(passengers.begin(),passengers.end(),
            [](const auto& passenger) { return passenger.id==1; });
        tests.Check(request!=passengers.end() && request->startFloor==11 && request->targetFloor==20,
            "fixed-seed request is available at movement midpoint");
        const auto observation=simulation.GetDispatchObservation(11,Direction::Up);
        tests.Check(observation.valid && observation.assignedElevatorId!=0,
            "midpoint request leaves comparison elevator route unchanged");
        const auto candidate=std::find_if(observation.candidates.begin(),observation.candidates.end(),
            [](const auto& item) { return item.elevatorId==0; });
        tests.Check(candidate!=observation.candidates.end() && candidate->feasible,
            "moving comparison elevator is a feasible candidate");

        Elevator elevator(0,1,config);
        tests.Check(elevator.AddHallCall(1,Direction::Up) && elevator.BeginBoarding(0,20),
            "build equivalent elevator action");
        tests.Check(elevator.Advance(config.personTime).type==ElevatorEventType::Boarded &&
            elevator.FinishStop(),"advance equivalent elevator into movement");
        auto corrected=elevator.GetDispatchSnapshot();
        const auto stale=corrected;
        corrected.remainingActionTime=config.personTime+config.moveTimePerFloor-arrivalTime;
        ElevatorDispatcher dispatcher;
        const auto expected=dispatcher.ScoreSnapshot(11,Direction::Up,corrected,
            request->requestTime,arrivalTime);
        const auto staleScore=dispatcher.ScoreSnapshot(11,Direction::Up,stale,
            request->requestTime,arrivalTime);
        tests.Near(candidate->eta,expected.eta,"dispatch ETA uses scheduled completion remainder");
        tests.Near(candidate->cost,expected.cost,"dispatch cost uses scheduled completion remainder");
        tests.Check(std::abs(candidate->eta-staleScore.eta)>0.1,
            "dispatch does not use frozen Elevator action duration");
        tests.Check(simulation.ValidateState(),"mid-action arrival remains valid");
    });
    tests.Run("speed equivalence with random events", [&] {
        auto config=Config(); config.passengerRate=0.4; config.simulationDuration=200;
        Simulation a,b; a.Initialize(config,123); config.simulationSpeed=5; b.Initialize(config,123);
        a.Start(); b.Start(); a.Update(100); b.Update(20); SameState(tests,a,b);
    });
    tests.Run("reset clears population and replays seed", [&] {
        auto config=Config(); config.passengerRate=0.4;
        Simulation a,b; a.Initialize(config,123); b.Initialize(config,123); a.Start(); a.Update(80); a.Reset();
        tests.Check(a.GetRandomSeed()==123 && a.GetPassengerSnapshots().empty() && a.GetHallCallSnapshots().empty(),"reset clears live state");
        a.Start(); b.Start(); a.Update(80); b.Update(80); SameState(tests,a,b);
    });
    tests.Run("reset rebuilds rather than retaining event calendar", [&] {
        constexpr std::uint32_t seed=222u;
        auto config=Config(); config.passengerRate=1.0; config.simulationDuration=20;
        const double firstArrival=FirstArrivalTime(config.passengerRate,seed);
        Simulation simulation; tests.Check(simulation.Initialize(config,seed),"initialize reset calendar fixture");
        simulation.Start(); simulation.Update(firstArrival+0.01);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==1,"first calendar generated one passenger");
        simulation.Reset(); simulation.Start(); simulation.Update(firstArrival/2.0);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==0,"old calendar event was cleared");
        simulation.Update(firstArrival/2.0);
        tests.Check(simulation.GetStatisticsSnapshot().totalPassengerCount==1,
            "rebuilt calendar replays exactly one first arrival");
        tests.Check(simulation.ValidateState(),"reset calendar state remains valid");
    });
    tests.Run("failed initialize preserves future random stream", [&] {
        auto config=Config(); config.passengerRate=0.4;
        Simulation a,b; a.Initialize(config,123); b.Initialize(config,123); a.Start(); b.Start(); a.Update(10); b.Update(10);
        config.capacity=0; tests.Check(!a.Initialize(config,999),"invalid config rejected");
        tests.Check(a.GetRandomSeed()==123,"seed preserved"); a.Update(50); b.Update(50); SameState(tests,a,b);
    });
    tests.Run("different seeds give different requests", [&] {
        auto config=Config(); config.passengerRate=2;
        Simulation a,b; a.Initialize(config,1); b.Initialize(config,2); a.Start(); b.Start(); a.Update(1); b.Update(1);
        const auto pa=a.GetPassengerSnapshots(),pb=b.GetPassengerSnapshots();
        tests.Check(!pa.empty() && !pb.empty() && pa[0].requestTime!=pb[0].requestTime,"different random timeline");
    });
    tests.Run("all full cars leave pending requests alive", [&] {
        auto config=Config(); config.capacity=1; config.simulationDuration=300;
        Simulation simulation; simulation.Initialize(config,42);
        simulation.AddPassenger(1,6); simulation.AddPassenger(6,1); simulation.Start(); simulation.Update(0.01);
        simulation.AddPassenger(3,6); simulation.Update(3.1);
        tests.Check(simulation.GetStatisticsSnapshot().ridingCount==3,"fixture has three full cars");
        // 当前满载不等于无法分配；预测折返前可下客的梯可提前接受，请求仍保留到上梯完成。
        simulation.AddPassenger(2,1); simulation.Update(0.1); const auto calls=simulation.GetHallCallSnapshots();
        tests.Check(calls.size()==1 && calls[0].waitingCount==1,"full cars preserve pending request");
        simulation.Update(200); tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==4 && simulation.ValidateState(),"retry after capacity released");
    });
    tests.Run("finite batch drains without passenger loss", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6; config.capacity=3;
        config.moveTimePerFloor=0.5; config.personTime=0.25; config.simulationDuration=20000;
        Simulation simulation; simulation.Initialize(config,42);
        for(int i=0;i<2000;++i) { const int start=i%20+1; const int target=(start-1+1+i%19)%20+1; simulation.AddPassenger(start,target); }
        simulation.Start(); simulation.Update(20000); const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount==2000 && stats.arrivedCount==2000,"all 2000 delivered");
        tests.Check(simulation.GetPassengerSnapshots().empty() && simulation.GetHallCallSnapshots().empty() && simulation.ValidateState(),"no leaked active IDs");
    });
    tests.Run("high Poisson traffic remains consistent", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6; config.capacity=4;
        config.moveTimePerFloor=0.3; config.personTime=0.2; config.passengerRate=8; config.simulationDuration=600;
        Simulation simulation; simulation.Initialize(config,321); simulation.Start();
        for(int sample=0;sample<1200;++sample) { simulation.Update(0.5); tests.Check(simulation.ValidateState(),"high-flow invariants"); }
        const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(stats.totalPassengerCount>4300 && stats.totalPassengerCount<5300,"plausible Poisson count, not forced exact count");
        tests.Check(stats.arrivedCount>0 && stats.waitingCount>0,"loaded service with backlog");
        double fullTime=0; for(const auto& car:stats.elevators) fullTime+=car.fullTime;
        tests.Check(fullTime>0,"full operation recorded");
        std::cout << "High flow: generated=" << stats.totalPassengerCount << ", arrived=" << stats.arrivedCount
            << ", waiting=" << stats.waitingCount << ", riding=" << stats.ridingCount << '\n';
    });
    tests.Run("high-flow office day remains consistent", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6; config.capacity=4;
        config.moveTimePerFloor=0.3; config.personTime=0.2;
        config.passengerRate=6; config.simulationDuration=120;
        config.trafficScenario=TrafficScenario::OfficeDay;
        Simulation simulation; tests.Check(simulation.Initialize(config,20260904u),
            "initialize high-flow office day");
        simulation.Start();
        for(int sample=0;sample<240;++sample)
        {
            simulation.Update(0.5);
            tests.Check(simulation.ValidateState(),"high-flow office invariants");
        }
        const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(simulation.IsFinished() && stats.totalPassengerCount>600,
            "high-flow office day reaches deadline with traffic");
        tests.Check(simulation.GetUISnapshot().trafficPhaseIndex==2,
            "high-flow office day completes evening phase");
    });
    tests.Run("one-hour stability", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6; config.capacity=15;
        config.moveTimePerFloor=1.5; config.personTime=0.5; config.passengerRate=0.6; config.simulationDuration=3600;
        Simulation simulation; simulation.Initialize(config,987); simulation.Start();
        for(int minute=0;minute<60;++minute) { simulation.Update(60); tests.Check(simulation.ValidateState(),"long-run invariant"); }
        const auto stats=simulation.GetStatisticsSnapshot();
        tests.Check(simulation.IsFinished() && stats.arrivedCount>1000,"long run completes");
        tests.Check(std::isfinite(stats.averageWaitingTime) && std::isfinite(stats.averageRideTime),"finite aggregates");
        std::cout << "One hour: generated=" << stats.totalPassengerCount << ", arrived=" << stats.arrivedCount << '\n';
    });
    tests.Run("very large real delta clamped", [&] {
        auto config=Config(); config.simulationSpeed=10;
        Simulation simulation; simulation.Initialize(config,42); simulation.AddPassenger(1,6); simulation.Start();
        simulation.Update((std::numeric_limits<double>::max)());
        tests.Near(simulation.GetCurrentTime(),100,"clamped time");
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==1 && simulation.ValidateState(),"events handled before cutoff");
    });
    tests.Run("non-binary update partitions across seeds", [&] {
        auto config=Config(); config.passengerRate=1; config.moveTimePerFloor=0.3; config.personTime=0.2;
        for(std::uint32_t seed=1;seed<=10;++seed)
        {
            Simulation a,b; a.Initialize(config,seed); b.Initialize(config,seed); a.Start(); b.Start(); a.Update(100);
            for(int frame=0;frame<3333;++frame) b.Update(0.03);
            b.Update(100-b.GetCurrentTime()); SameState(tests,a,b);
        }
    });
    tests.Run("passenger transition guards", [&] {
        Passenger passenger(0,1,3,2);
        tests.Check(!passenger.MarkArrived(4) && !passenger.MarkBoarded(0,1),"cannot skip state or go backward in time");
        tests.Check(passenger.MarkBoarded(0,5) && !passenger.MarkBoarded(1,6),"one boarding");
        tests.Check(!passenger.MarkArrived(4) && passenger.MarkArrived(10) && !passenger.MarkArrived(11),"one arrival");
    });
    tests.Run("floor FIFO and ID guards", [&] {
        Floor floor(3); tests.Check(floor.Enqueue(1,Direction::Up) && floor.Enqueue(2,Direction::Up),"enqueue");
        tests.Check(!floor.Enqueue(1,Direction::Down) && !floor.Enqueue(-1,Direction::Up),"duplicate or invalid ID");
        tests.Check(!floor.RemoveFront(2,Direction::Up) && floor.Peek(Direction::Up)==1,"cannot bypass front");
        tests.Check(floor.RemoveFront(1,Direction::Up) && floor.Peek(Direction::Up)==2,"remove front only");
    });
    tests.Run("event-driven reassignment updates unique ownership", [&] {
        auto config=Config(); config.floorCount=20; config.simulationDuration=200;
        Simulation simulation; simulation.Initialize(config,42);
        simulation.AddPassenger(15,20); simulation.Start(); simulation.Update(2);
        tests.Check(simulation.GetHallCallSnapshots()[0].assignedElevatorId==1,"E2 initially wins equal distance");
        simulation.AddPassenger(17,1); simulation.Update(2);
        tests.Check(HallAt(simulation,15,Direction::Up).assignedElevatorId==1,
            "unboarded down destination cannot yet trigger reassignment");
        simulation.Update(5); // 17F 下行乘客在 9 秒完成 Boarded，1F 内呼现在才可见。
        int owner=-1;
        for(const auto& call:simulation.GetHallCallSnapshots()) if(call.floorNumber==15) owner=call.assignedElevatorId;
        tests.Check(owner==2,"Boarded down passenger makes E3 much faster for 15F up");
        tests.Check(simulation.ValidateState(),"old elevator no longer owns reassigned call");
        for(int frame=0;frame<50;++frame)
        {
            simulation.Update(0.1);
            for(const auto& call:simulation.GetHallCallSnapshots()) if(call.floorNumber==15)
                tests.Check(call.assignedElevatorId==2,"no ping-pong across frame boundaries");
            tests.Check(simulation.ValidateState(),"one owner during reassignment cooldown");
        }
        simulation.Update(180);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==2 && simulation.ValidateState(),"both requests complete");
    });
    tests.Run("dynamic and joint decisions are independent of frame boundaries", [&] {
        auto config=Config(); config.floorCount=20; config.simulationDuration=200;
        Simulation a,b; a.Initialize(config,42); b.Initialize(config,42);
        a.AddPassenger(15,20); b.AddPassenger(15,20); a.Start(); b.Start(); a.Update(2);
        for(int frame=0;frame<8;++frame) b.Update(0.25);
        a.AddPassenger(17,1); b.AddPassenger(17,1); a.Update(10);
        for(int frame=0;frame<80;++frame) b.Update(0.125);
        SameState(tests,a,b);
    });
    tests.Run("one dispatch event processes six and nine pending calls", [&] {
        for(int count:{6,9})
        {
            auto config=Config(); config.floorCount=30; config.capacity=20;
            Simulation simulation; simulation.Initialize(config,42);
            for(int floor=2;floor<2+count;++floor) simulation.AddPassenger(floor,30);
            simulation.Start(); simulation.Update(0.01); // 无到层或传送完成事件，也无当前层停站。
            const auto calls=simulation.GetHallCallSnapshots();
            tests.Check(calls.size()==static_cast<std::size_t>(count),"all distinct calls remain waiting");
            for(const auto& call:calls) tests.Check(call.assignedElevatorId!=InvalidElevatorId,"assigned without another model event");
            tests.Check(simulation.GetStatisticsSnapshot().ridingCount==0 && simulation.ValidateState(),"ownership valid before any pickup");
        }
    });
    tests.Run("entirely infeasible batch stops without zero time loop", [&] {
        auto config=Config(); config.floorCount=20; config.capacity=1; config.simulationDuration=300;
        Simulation simulation; simulation.Initialize(config,42);
        simulation.AddPassenger(20,1); simulation.Start(); simulation.Update(44);
        // 两台梯在 1F、一台在 10F；全部装载到 20F，12~14F 上行前无法释放容量。
        simulation.AddPassenger(1,20); simulation.AddPassenger(1,20); simulation.AddPassenger(10,20);
        simulation.Update(6.1);
        tests.Check(simulation.GetStatisticsSnapshot().ridingCount==3,"three full upward cars");
        for(int floor=12;floor<=14;++floor) simulation.AddPassenger(floor,20);
        simulation.Update(0.01);
        const auto calls=simulation.GetHallCallSnapshots();
        tests.Check(calls.size()==3,"infeasible batch stays pending");
        for(const auto& call:calls) tests.Check(call.assignedElevatorId==InvalidElevatorId,"no impossible assignment");
        tests.Near(simulation.GetCurrentTime(),50.11,"clock advances after zero-assignment batch");
        tests.Check(simulation.ValidateState(),"pending queues stay intact");
        simulation.Update(249.89);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==7 && simulation.ValidateState(),"later capacity release drains requests");
    });
    tests.Run("three deferred calls do not consume the active joint window", [&] {
        auto simulation=FullUpFleet(); const double originalTime=simulation.GetCurrentTime();
        for(int floor=12;floor<=14;++floor) simulation.AddPassenger(floor,20);
        simulation.Update(0.125);
        const auto snapshots=FullFleetSnapshots(simulation);
        const ElevatorDispatcher dispatcher;
        for(int floor=12;floor<=14;++floor)
            for(const auto& car:snapshots)
                tests.Check(!dispatcher.ScoreSnapshot(floor,Direction::Up,car).feasible,"oldest three have no candidate");
        std::vector<HallCallDispatchSnapshot> active;
        for(int floor=15;floor<=17;++floor)
        {
            const auto id=simulation.AddPassenger(floor,1);
            active.push_back({floor,Direction::Down,simulation.GetCurrentTime(),id});
        }
        const auto expected=dispatcher.PlanAssignments(active,snapshots,simulation.GetCurrentTime());
        tests.Check(active.size()==3 && expected.assignedCount==3,"full three-active-request joint plan exists");
        simulation.Update(0.125); // 下次物理到层在 51 秒；本次只执行新增乘客触发的调度。
        for(std::size_t index=0;index<active.size();++index)
            tests.Check(HallAt(simulation,active[index].floor,Direction::Down).assignedElevatorId==expected.elevatorIndices[index],
                "actual assignments match joint planning of all three active requests");
        for(int floor=12;floor<=14;++floor)
        {
            const auto call=HallAt(simulation,floor,Direction::Up);
            tests.Check(call.assignedElevatorId==InvalidElevatorId && call.waitingCount==1,"deferred call remains queued");
            tests.Near(call.firstRequestTime,originalTime,"deferred timestamp retained");
        }
        tests.Check(simulation.GetHallCallSnapshots().size()==6 && simulation.ValidateState(),"six calls retain unique ownership");
    });
    tests.Run("deferred FIFO and aging survive route recovery and alighting", [&] {
        auto simulation=FullUpFleet(); const double originalTime=simulation.GetCurrentTime();
        const auto first=simulation.AddPassenger(12,20); simulation.Update(0.125);
        const auto second=simulation.AddPassenger(12,20); simulation.Update(0.125);
        const auto deferred=HallAt(simulation,12,Direction::Up);
        tests.Check(deferred.assignedElevatorId==InvalidElevatorId && deferred.waitingCount==2,"both IDs stay deferred in FIFO");
        tests.Near(deferred.firstRequestTime,originalTime,"later passenger does not reset head time");
        for(int refresh=0;refresh<20;++refresh) simulation.GetHallCallSnapshots();
        tests.Check(HallAt(simulation,12,Direction::Up).assignedElevatorId==InvalidElevatorId,"UI snapshot reads do not dispatch");
        const auto arrivedBefore=simulation.GetStatisticsSnapshot().arrivedCount;
        simulation.Update(0.625); // 51 秒已驶离 12F：20F 下客改为发生在折返接客之前。
        const auto active=HallAt(simulation,12,Direction::Up);
        tests.Check(active.assignedElevatorId==2 && simulation.GetStatisticsSnapshot().arrivedCount==arrivedBefore,
            "route-order change reactivates request before actual unloading");
        tests.Near(active.firstRequestTime,originalTime,"reactivation preserves first request time");
        const ElevatorDispatcher dispatcher;
        tests.Near(dispatcher.GetAgingBonus(active.firstRequestTime,simulation.GetCurrentTime()),
            (51-originalTime)*ElevatorDispatcher::AgingBonusRate,"aging continues from original request");
        // ETA 已预见下客，因此不能故意等到 Alighted 才允许分配；实际事件后归属仍须有效。
        simulation.Update(69.875-simulation.GetCurrentTime());
        tests.Check(simulation.GetElevatorSnapshots()[2].state==ElevatorState::Alighting,"immediately before alighting event");
        const auto arrivals=simulation.GetStatisticsSnapshot().arrivedCount; simulation.Update(0.125);
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==arrivals+1,"Alighted releases actual capacity");
        tests.Check(HallAt(simulation,12,Direction::Up).assignedElevatorId!=InvalidElevatorId && simulation.ValidateState(),
            "previously deferred request remains serviceable after Alighted");
        double firstBoard=UnsetTime, secondBoard=UnsetTime;
        while(simulation.GetCurrentTime()<300 && secondBoard==UnsetTime)
        {
            simulation.Update(0.25);
            for(const auto& passenger:simulation.GetPassengerSnapshots())
            {
                if(passenger.id==first && passenger.boardTime!=UnsetTime) firstBoard=passenger.boardTime;
                if(passenger.id==second && passenger.boardTime!=UnsetTime) secondBoard=passenger.boardTime;
            }
        }
        tests.Check(firstBoard>originalTime && secondBoard>firstBoard,"original head ID boards before newer passenger");
        simulation.Update(300-simulation.GetCurrentTime());
        tests.Check(simulation.GetStatisticsSnapshot().arrivedCount==6 && simulation.ValidateState(),"deferred passengers eventually delivered");
    });
    tests.Run("deferred active filtering is frame partition independent", [&] {
        auto a=FullUpFleet(), b=FullUpFleet();
        for(int floor=12;floor<=14;++floor) { a.AddPassenger(floor,20); b.AddPassenger(floor,20); }
        a.Update(0.125); b.Update(0.125);
        for(int floor=15;floor<=17;++floor) { a.AddPassenger(floor,1); b.AddPassenger(floor,1); }
        a.Update(100);
        for(int frame=0;frame<800;++frame) b.Update(0.125);
        SameState(tests,a,b);
    });
    tests.Run("dispatch observation is read-only and reuses dispatcher scoring", [&] {
        const auto config=Config(); Simulation simulation;
        tests.Check(simulation.Initialize(config,42),"initialize observation fixture");
        tests.Check(simulation.AddPassenger(2,6)!=InvalidPassengerId,"add observed request");
        const auto beforeUI=simulation.GetUISnapshot();
        const auto beforePeople=simulation.GetPassengerSnapshots();
        const auto observation=simulation.GetDispatchObservation(2,Direction::Up);
        tests.Check(observation.valid && observation.floor==2 && observation.direction==Direction::Up &&
            observation.waitingCount==1 && observation.assignedElevatorId==InvalidElevatorId,
            "observation identifies real unassigned hall call");
        tests.Check(observation.candidates.size()==3,"observation scores complete fleet");

        ElevatorDispatcher dispatcher;
        const int starts[]={1,6,3};
        for(int id=0;id<3;++id)
        {
            Elevator elevator(id,starts[id],config);
            const auto expected=dispatcher.ScoreSnapshot(2,Direction::Up,elevator.GetDispatchSnapshot(),0,0);
            const auto actual=std::find_if(observation.candidates.begin(),observation.candidates.end(),
                [id](const auto& candidate) { return candidate.elevatorId==id; });
            tests.Check(actual!=observation.candidates.end() && actual->feasible==expected.feasible &&
                actual->projectedOccupancy==expected.projectedOccupancy,"observation candidate matches score");
            tests.Near(actual->cost,expected.cost,"observation cost matches ScoreSnapshot");
            tests.Near(actual->eta,expected.eta,"observation ETA matches ScoreSnapshot");
        }

        const auto afterUI=simulation.GetUISnapshot();
        const auto afterPeople=simulation.GetPassengerSnapshots();
        tests.Check(beforeUI.state==afterUI.state && beforeUI.currentTime==afterUI.currentTime &&
            beforeUI.randomSeed==afterUI.randomSeed && beforeUI.elevators.size()==afterUI.elevators.size() &&
            beforeUI.floors.size()==afterUI.floors.size() && beforeUI.hallCalls.size()==afterUI.hallCalls.size() &&
            beforeUI.statistics.totalPassengerCount==afterUI.statistics.totalPassengerCount &&
            beforeUI.statistics.waitingCount==afterUI.statistics.waitingCount &&
            beforeUI.statistics.ridingCount==afterUI.statistics.ridingCount &&
            beforeUI.statistics.arrivedCount==afterUI.statistics.arrivedCount &&
            beforePeople.size()==afterPeople.size(),"observation preserves public simulation state");
        for(std::size_t i=0;i<beforeUI.elevators.size();++i)
            tests.Check(beforeUI.elevators[i].currentFloor==afterUI.elevators[i].currentFloor &&
                beforeUI.elevators[i].direction==afterUI.elevators[i].direction &&
                beforeUI.elevators[i].state==afterUI.elevators[i].state &&
                beforeUI.elevators[i].passengerCount==afterUI.elevators[i].passengerCount,
                "observation preserves elevator state");
        tests.Check(beforePeople[0].id==afterPeople[0].id && beforePeople[0].state==afterPeople[0].state &&
            beforePeople[0].elevatorId==afterPeople[0].elevatorId &&
            beforePeople[0].requestTime==afterPeople[0].requestTime,
            "observation preserves passenger state");
        tests.Check(simulation.ValidateState(),"state valid after observation");
    });
    tests.Run("dispatch observation is mode-independent and invalid when absent", [&] {
        const auto config=Config(); Simulation sequential,parallel;
        parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel,2);
        tests.Check(sequential.Initialize(config,73) && parallel.Initialize(config,73),
            "initialize observation modes");
        sequential.AddPassenger(5,1); parallel.AddPassenger(5,1);
        const auto left=sequential.GetDispatchObservation(5,Direction::Down);
        const auto right=parallel.GetDispatchObservation(5,Direction::Down);
        SameObservation(tests,left,right);
        tests.Check(!sequential.GetDispatchObservation(4,Direction::Up).valid,
            "missing hall call observation is invalid");
        tests.Check(!sequential.GetDispatchObservation(5,Direction::Idle).valid,
            "invalid direction observation is invalid");
    });
    tests.Run("predictive switch preserves floor statistics accounting", [&] {
        auto config=Config(); config.floorCount=12; config.elevatorCount=6; config.capacity=4;
        config.moveTimePerFloor=1.0; config.personTime=0.5;
        config.passengerRate=0.8; config.simulationDuration=80;
        Simulation disabled,enabled;
        tests.Check(disabled.Initialize(config,24680),"initialize disabled accounting run");
        config.predictiveRebalancing=true;
        tests.Check(enabled.Initialize(config,24680),"initialize enabled accounting run");
        disabled.Start(); enabled.Start(); disabled.Update(60); enabled.Update(60);
        const auto left=disabled.GetStatisticsSnapshot();
        const auto right=enabled.GetStatisticsSnapshot();
        tests.Check(left.floorTraffic.size()==12 && right.floorTraffic.size()==12,
            "both modes expose all floor statistics");
        const auto verify=[&](const StatisticsSnapshot& snapshot)
        {
            std::uint64_t generated=0,boarded=0;
            double waiting=0.0;
            for(const auto& floor:snapshot.floorTraffic)
            {
                tests.Check(floor.generatedCount==floor.upRequestCount+floor.downRequestCount,
                    "floor request directions sum to generated count");
                tests.Check(floor.boardedCount<=floor.generatedCount,
                    "floor boarded count cannot exceed generated count");
                generated+=floor.generatedCount;
                boarded+=floor.boardedCount;
                waiting+=floor.totalWaitingTime;
            }
            tests.Check(generated==snapshot.totalPassengerCount,
                "floor generated totals match global statistics");
            tests.Check(boarded==snapshot.boardedCount,
                "floor boarded totals match global statistics");
            if(boarded!=0)
                tests.Near(waiting/static_cast<double>(boarded),snapshot.averageWaitingTime,
                    "floor wait sums match global average",1e-7);
        };
        verify(left); verify(right);
        for(std::size_t index=0;index<left.floorTraffic.size();++index)
            tests.Check(left.floorTraffic[index].generatedCount==right.floorTraffic[index].generatedCount &&
                left.floorTraffic[index].upRequestCount==right.floorTraffic[index].upRequestCount &&
                left.floorTraffic[index].downRequestCount==right.floorTraffic[index].downRequestCount,
                "predictive switch does not alter fixed-seed request history");
        tests.Check(disabled.ValidateState() && enabled.ValidateState(),
            "both predictive modes preserve statistics invariants");
    });
    tests.Run("predictive rebalancing defaults off", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6;
        config.passengerRate=0.5; config.simulationDuration=60;
        tests.Check(!config.predictiveRebalancing,"configuration default remains disabled");
        Simulation simulation; simulation.Initialize(config,42); simulation.Start(); simulation.Update(0.01);
        for(const auto& elevator:simulation.GetElevatorSnapshots())
            tests.Check(elevator.repositionTargetFloor==InvalidFloor,
                "disabled mode leaves every idle car in place");
        tests.Check(simulation.ValidateState(),"disabled state remains valid");
    });
    tests.Run("enabled rebalancing is frame partition and reset deterministic", [&] {
        auto config=Config(); config.floorCount=20; config.elevatorCount=6; config.capacity=8;
        config.moveTimePerFloor=1.0; config.personTime=0.5; config.passengerRate=0.8;
        config.simulationDuration=80; config.predictiveRebalancing=true;
        Simulation started; started.Initialize(config,31415); started.Start(); started.Update(0.01);
        const auto startedCars=started.GetElevatorSnapshots();
        tests.Check(std::any_of(startedCars.begin(),startedCars.end(),[](const auto& elevator)
            { return elevator.repositionTargetFloor!=InvalidFloor; }),
            "first Start performs an event-level predictive rebalance");
        Simulation large,small; large.Initialize(config,31415); small.Initialize(config,31415);
        large.Start(); small.Start(); large.Update(40.0);
        for(int frame=0;frame<400;++frame) small.Update(0.1);
        SameCompleteState(tests,large,small);
        const auto expected=large.GetElevatorSnapshots();
        large.Reset();
        for(const auto& elevator:large.GetElevatorSnapshots())
            tests.Check(elevator.repositionTargetFloor==InvalidFloor,"reset clears soft targets");
        large.Start(); large.Update(40.0);
        const auto replay=large.GetElevatorSnapshots();
        tests.Check(expected.size()==replay.size(),"replay elevator count");
        for(std::size_t index=0;index<expected.size();++index)
            tests.Check(expected[index].currentFloor==replay[index].currentFloor &&
                expected[index].direction==replay[index].direction &&
                expected[index].state==replay[index].state &&
                expected[index].passengerCount==replay[index].passengerCount &&
                expected[index].repositionTargetFloor==replay[index].repositionTargetFloor,
                "fixed seed replays reposition state");
    });
    tests.Run("enabled rebalancing is dispatcher-mode independent under load", [&] {
        auto config=Config(); config.floorCount=30; config.elevatorCount=9; config.capacity=6;
        config.moveTimePerFloor=0.5; config.personTime=0.25; config.passengerRate=3.0;
        config.simulationDuration=120; config.predictiveRebalancing=true;
        Simulation sequential,parallel;
        parallel.SetDispatcherExecutionMode(DispatcherExecutionMode::Parallel,3);
        sequential.Initialize(config,2718); parallel.Initialize(config,2718);
        sequential.Start(); parallel.Start(); sequential.Update(120); parallel.Update(120);
        SameCompleteState(tests,sequential,parallel);
        tests.Check(sequential.ValidateState() && parallel.ValidateState(),
            "high-traffic predictive runs preserve invariants");
    });
    tests.Run("simulation dispatch can preempt a repositioning elevator", [&] {
        SimulationConfig config;
        config.floorCount=20; config.elevatorCount=6; config.capacity=8;
        config.moveTimePerFloor=2.0; config.personTime=1.0;
        config.passengerRate=0.1; config.simulationDuration=100.0;
        config.predictiveRebalancing=true;
        Simulation simulation;
        tests.Check(simulation.Initialize(config,42),"initialize reposition preemption fixture");
        simulation.Start(); simulation.Update(0.01);
        const auto before=simulation.GetElevatorSnapshots();
        const auto repositioning=std::find_if(before.begin(),before.end(),[](const auto& elevator)
        {
            return elevator.state==ElevatorState::MovingUp &&
                elevator.repositionTargetFloor==elevator.currentFloor+1 &&
                elevator.repositionTargetFloor<4;
        });
        tests.Check(repositioning!=before.end(),"an elevator is moving toward a one-floor soft target");
        const int elevatorId=repositioning->id;
        const int departedFloor=repositioning->currentFloor;
        const int oldRepositionTarget=repositioning->repositionTargetFloor;

        tests.Check(simulation.AddPassenger(4,20)!=InvalidPassengerId,
            "add nearby real hall call while repositioning");
        simulation.Update(0.001);
        const auto calls=simulation.GetHallCallSnapshots();
        const auto call=std::find_if(calls.begin(),calls.end(),[](const auto& item)
            { return item.floorNumber==4 && item.direction==Direction::Up; });
        tests.Check(call!=calls.end() && call->assignedElevatorId==elevatorId,
            "repositioning elevator wins real hall call through Simulation dispatch");
        auto cars=simulation.GetElevatorSnapshots();
        const auto assigned=std::find_if(cars.begin(),cars.end(),[elevatorId](const auto& elevator)
            { return elevator.id==elevatorId; });
        tests.Check(assigned!=cars.end() && assigned->repositionTargetFloor==InvalidFloor,
            "real assignment clears soft target");
        tests.Check(assigned->currentFloor==departedFloor && assigned->state==ElevatorState::MovingUp,
            "already-started floor segment remains in progress");

        simulation.Update(config.moveTimePerFloor);
        cars=simulation.GetElevatorSnapshots();
        const auto atOldTarget=std::find_if(cars.begin(),cars.end(),[elevatorId](const auto& elevator)
            { return elevator.id==elevatorId; });
        tests.Check(atOldTarget!=cars.end() && atOldTarget->currentFloor==oldRepositionTarget &&
            atOldTarget->state==ElevatorState::MovingUp && atOldTarget->direction==Direction::Up &&
            atOldTarget->repositionTargetFloor==InvalidFloor,
            "at old soft target the elevator continues on the real LOOK route");
        simulation.Update(config.moveTimePerFloor);
        cars=simulation.GetElevatorSnapshots();
        const auto beyondOldTarget=std::find_if(cars.begin(),cars.end(),[elevatorId](const auto& elevator)
            { return elevator.id==elevatorId; });
        tests.Check(beyondOldTarget!=cars.end() && beyondOldTarget->currentFloor>oldRepositionTarget &&
            beyondOldTarget->state==ElevatorState::MovingUp,
            "elevator passes the former reposition destination toward the real request");
        tests.Check(simulation.ValidateState(),"preempted reposition state remains valid");
    });
    tests.Run("office day rebalancing follows active traffic phase", [&] {
        SimulationConfig config;
        config.floorCount=20; config.elevatorCount=6; config.capacity=8;
        config.moveTimePerFloor=0.5; config.personTime=0.25;
        config.passengerRate=0.5; config.simulationDuration=80.0;
        config.trafficScenario=TrafficScenario::OfficeDay;
        config.predictiveRebalancing=true;
        Simulation simulation;
        tests.Check(simulation.Initialize(config,673),"initialize OfficeDay rebalance fixture");
        simulation.Start(); simulation.Update(0.01);
        const auto upPeak=simulation.GetUISnapshot();
        simulation.Update(20.0);
        const auto interFloor=simulation.GetUISnapshot();
        simulation.Update(36.0);
        const auto downPeak=simulation.GetUISnapshot();

        tests.Check(upPeak.trafficPhaseIndex==0 &&
            upPeak.activeTrafficPattern==TrafficPattern::UpPeak,"first phase is UpPeak");
        tests.Check(interFloor.trafficPhaseIndex==1 &&
            interFloor.activeTrafficPattern==TrafficPattern::InterFloor,"middle phase is InterFloor");
        tests.Check(downPeak.trafficPhaseIndex==2 &&
            downPeak.activeTrafficPattern==TrafficPattern::DownPeak,"last phase is DownPeak");

        const auto targets=[](const SimulationUISnapshot& snapshot)
        {
            std::vector<int> result;
            for(const auto& elevator:snapshot.elevators)
                if(elevator.repositionTargetFloor!=InvalidFloor)
                    result.push_back(elevator.repositionTargetFloor);
            return result;
        };
        const auto upTargets=targets(upPeak);
        const auto interTargets=targets(interFloor);
        const auto downTargets=targets(downPeak);
        tests.Check(!upTargets.empty() && !interTargets.empty() && !downTargets.empty(),
            "every OfficeDay phase exposes a reposition target distribution");
        for(const auto* snapshot:{&upPeak,&interFloor,&downPeak}) {
            const auto weights=FleetRebalancer::BuildDemandWeights(config.floorCount,snapshot->activeTrafficPattern);
            for(const auto& coverage:snapshot->floorCoverage) {
                double expected=0;
                for(const auto& weight:weights) if(weight.floor==coverage.floor) expected+=weight.probability;
                tests.Near(coverage.demandWeight,expected,"coverage uses the current phase demand weights");
            }
        }
        tests.Check(simulation.ValidateState(),"OfficeDay phase rebalancing state remains valid");
    });
    return tests.Finish();
}
