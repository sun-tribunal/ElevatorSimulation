#include "Core/Dispatcher.h"
#include "TestSupport.h"
#include <limits>

namespace
{
    ElevatorDispatchSnapshot Car(int id, int floor, Direction direction = Direction::Idle,
        std::vector<int> up = {}, std::vector<int> down = {}, int passengers = 0, int capacity = 10)
    {
        ElevatorDispatchSnapshot car;
        car.elevator = { id, floor, direction,
            direction == Direction::Idle ? ElevatorState::Idle : ElevatorState::Stopped, passengers, capacity };
        car.floorCount = 20;
        car.upTasks = std::move(up);
        car.downTasks = std::move(down);
        for (int stop : car.upTasks) car.stopServices.push_back({ stop, Direction::Up });
        for (int stop : car.downTasks) car.stopServices.push_back({ stop, Direction::Down });
        return car;
    }

    // 用两台略快/略慢的空闲梯夹住预期成本，检查 ETA 数值而非只看大致排序。
    void CheckCost(TestSuite& tests, const ElevatorDispatchSnapshot& route,
        int floor, Direction direction, double expectedCost)
    {
        const ElevatorDispatcher dispatcher;
        auto idle = Car(99, floor == 1 ? 2 : floor - 1);
        idle.moveTimePerFloor = expectedCost - 0.01;
        tests.Check(dispatcher.SelectFromSnapshots(floor, direction, {route, idle}) == 1,
            "idle just below expected cost wins");
        idle.moveTimePerFloor = expectedCost + 0.01;
        tests.Check(dispatcher.SelectFromSnapshots(floor, direction, {route, idle}) == 0,
            "route beats idle just above expected cost");
    }

    HallCallDispatchSnapshot Call(int floor, Direction direction,
        PassengerId id = 0, double time = 0.0)
    {
        HallCallDispatchSnapshot request;
        request.floor=floor; request.direction=direction;
        request.firstPassengerId=id; request.firstRequestTime=time;
        return request;
    }

    Elevator AlightingCar(int count)
    {
        SimulationConfig config;
        Elevator car(0, 1, config);
        car.AddHallCall(1, Direction::Up);
        for (int id = 0; id < count; ++id)
        {
            if (!car.BeginBoarding(id, 5)) throw std::runtime_error("boarding fixture failed");
            car.Advance(config.personTime);
        }
        car.FinishStop();
        while (!car.IsAtStop()) car.Advance(car.GetTimeToNextEvent());
        if (!car.BeginAlighting(0)) throw std::runtime_error("alighting fixture failed");
        car.Advance(2.0); // 当前一人还剩 1 秒，其他人各需完整 T=3 秒。
        return car;
    }

    double RealPickupTime(Elevator car, int floor, Direction direction)
    {
        car.AddHallCall(floor, direction);
        double elapsed = 0.0;
        for (int event = 0; event < 1000; ++event)
        {
            if (car.IsAtStop())
            {
                const auto alighting = car.GetNextAlightingPassenger();
                if (alighting != InvalidPassengerId) car.BeginAlighting(alighting);
                else if (car.GetSnapshot().currentFloor == floor && car.GetSnapshot().direction == direction)
                    return elapsed;
                else car.FinishStop();
            }
            else
            {
                if (car.GetSnapshot().state == ElevatorState::Idle)
                    throw std::runtime_error("reference elevator lost request");
                elapsed += car.Advance(car.GetTimeToNextEvent()).elapsedTime;
            }
        }
        throw std::runtime_error("reference route did not finish");
    }
}

int main()
{
    TestSuite tests("Dispatcher");
    const ElevatorDispatcher dispatcher;
    const auto select = [&](int floor, Direction direction, const std::vector<ElevatorDispatchSnapshot>& cars,
        double requestTime = UnsetTime, double currentTime = UnsetTime)
    { return dispatcher.SelectFromSnapshots(floor, direction, cars, requestTime, currentTime); };
    tests.Run("nearest idle", [&] { tests.Check(select(7, Direction::Up,
        { Car(0,1), Car(1,6), Car(2,20) }) == 1, "nearest idle"); });
    tests.Run("idle at landing", [&] { tests.Check(select(4, Direction::Down,
        { Car(0,8), Car(1,4) }) == 1, "same floor"); });
    tests.Run("up on-way beats closer opposite", [&] { tests.Check(select(10, Direction::Up,
        { Car(0,5,Direction::Up,{15}), Car(1,11,Direction::Down,{}, {2}) }) == 0, "opposite route costs more"); });
    tests.Run("down on-way beats closer opposite", [&] { tests.Check(select(10, Direction::Down,
        { Car(0,15,Direction::Down,{}, {2}), Car(1,9,Direction::Up,{19}) }) == 0, "opposite route costs more"); });
    tests.Run("idle at request beats distant on-way", [&] { tests.Check(select(10, Direction::Up,
        { Car(0,1,Direction::Up,{15}), Car(1,10) }) == 1, "immediate idle response"); });
    tests.Run("near idle beats distant on-way", [&] { tests.Check(select(10, Direction::Up,
        { Car(0,1,Direction::Up,{15}), Car(1,9) }) == 1, "compare ETA across idle and on-way"); });
    tests.Run("fast on-way beats distant idle", [&] { tests.Check(select(10, Direction::Up,
        { Car(0,9,Direction::Up,{15}), Car(1,1) }) == 0, "idle has no absolute priority either"); });
    tests.Run("downward idle and on-way use same cost", [&] {
        tests.Check(select(10,Direction::Down,{Car(0,20,Direction::Down,{}, {1}),Car(1,10)})==1,"idle at down call");
        tests.Check(select(10,Direction::Down,{Car(0,20,Direction::Down,{}, {1}),Car(1,11)})==1,"near idle down call");
        tests.Check(select(10,Direction::Down,{Car(0,11,Direction::Down,{}, {1}),Car(1,20)})==0,"fast down on-way");
    });
    tests.Run("reasonable idle beats opposite busy", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,11,Direction::Down,{}, {2}),Car(1,9)})==1,"opposite must finish accepted route"); });
    tests.Run("opposite surcharge is significant but finite", [&] {
        // 反向梯预计 5 秒接客：移动 2 秒 + 已有反向停站 3 秒；S+T 附加后成本 10。
        const auto opposite=Car(0,11,Direction::Down,{}, {10});
        tests.Check(select(10,Direction::Up,{opposite,Car(1,6)})==1,"idle ETA 8 beats opposite cost 10");
        tests.Check(select(10,Direction::Up,{opposite,Car(1,1)})==0,"cost 10 can still beat idle ETA 18");
    });
    tests.Run("same-direction behind penalty is not absolute", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,11,Direction::Up,{12}),Car(1,1)})==0,"short remaining sweep can beat distant idle"); });
    tests.Run("load cost can outweigh faster on-way ETA", [&] {
        auto loaded=Car(0,9,Direction::Up,{15},{},9); loaded.personTime=10;
        tests.Check(select(10,Direction::Up,{loaded,Car(1,8)})==1,"ETA 2 plus load 9 loses to idle 4");
    });
    tests.Run("intermediate stops compared against idle", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,6,Direction::Up,{7,8,9,15}),Car(1,4)})==1,"nearer moving car has more pickup delay"); });
    tests.Run("remaining boarding compared against idle", [&] {
        auto boarding=Car(0,9,Direction::Up,{15}); boarding.elevator.state=ElevatorState::Boarding;
        boarding.personTime=10; boarding.remainingActionTime=8; boarding.reservedBoardingCount=1;
        tests.Check(select(10,Direction::Up,{boarding,Car(1,7)})==1,"remaining action belongs in pickup ETA");
    });
    tests.Run("observable intermediate transfers have fixed costs", [&] {
        auto route=Car(0,5,Direction::Up,{7,8,15},{},2);
        route.stopServices={{7,Direction::Idle},{8,Direction::Up},{15,Direction::Up}};
        // 移动 10，下客 3，上客 3，估计载荷 2/10。
        CheckCost(tests,route,10,Direction::Up,16.6);
    });
    tests.Run("full car needs a known car call before pickup", [&] {
        auto full=Car(0,1,Direction::Up,{5},{},2,2);
        tests.Check(!dispatcher.ScoreSnapshot(10,Direction::Up,full).feasible,"hall call cannot release seats");
        full.stopServices={{5,Direction::Idle}};
        auto score=dispatcher.ScoreSnapshot(10,Direction::Up,full);
        tests.Check(score.feasible && score.projectedOccupancy==1,"one distinct car call releases one seat");
        tests.Near(score.eta,21,"18 travel plus one T");
        tests.Check(!dispatcher.ScoreSnapshot(4,Direction::Up,full).feasible,"car call after pickup cannot release early");
        score=dispatcher.ScoreSnapshot(5,Direction::Up,full);
        tests.Check(score.feasible && score.projectedOccupancy==1,"same floor alights before pickup");
        tests.Near(score.eta,11,"same floor transfer included");
    });
    tests.Run("each distinct car call releases at most one seat", [&] {
        auto route=Car(0,3,Direction::Up,{5,6},{},3,3);
        route.stopServices={{5,Direction::Idle},{5,Direction::Idle},{6,Direction::Idle}};
        auto score=dispatcher.ScoreSnapshot(8,Direction::Up,route);
        tests.Check(score.feasible && score.projectedOccupancy==1,"duplicate button cannot release another seat");
        tests.Near(score.eta,16,"two distinct transfers plus ten travel");
    });
    tests.Run("earlier unknown hall consumes released seat", [&] {
        auto route=Car(0,1,Direction::Up,{4,6},{},1,1);
        route.stopServices={{4,Direction::Idle},{6,Direction::Up}};
        tests.Check(!dispatcher.ScoreSnapshot(8,Direction::Up,route).feasible,"no invented destination after unknown pickup");
        route.stopServices.pop_back(); route.upTasks={4};
        tests.Check(dispatcher.ScoreSnapshot(8,Direction::Up,route).feasible,"cancellation recovers capacity");
    });
    tests.Run("downward estimates use the same observable rules", [&] {
        auto route=Car(0,15,Direction::Down,{}, {14,12},2,2);
        route.stopServices={{14,Direction::Idle},{12,Direction::Down}};
        tests.Check(!dispatcher.ScoreSnapshot(10,Direction::Down,route).feasible,"one release then one pickup fills car");
        route.elevator.capacity=3;
        CheckCost(tests,route,10,Direction::Down,18);
    });
    tests.Run("alighting consumed once across both hall directions", [&] {
        auto route=Car(0,3,Direction::Up,{5},{5,8},1,2);
        route.stopServices={{5,Direction::Idle},{5,Direction::Down},{8,Direction::Down}};
        tests.Check(!dispatcher.ScoreSnapshot(4,Direction::Down,route).feasible,
            "5F cannot release again on return after two hall pickups");
    });
    tests.Run("unknown hall does not invent a turnaround destination", [&] {
        auto route=Car(0,5,Direction::Up,{6},{},0,2);
        // 5->6->3，8 秒移动、一次外呼 3 秒、载荷 1.5、方向成本 5。
        CheckCost(tests,route,3,Direction::Up,17.5);
    });
    tests.Run("Boarding target is hidden until Boarded", [&] {
        SimulationConfig config; config.capacity=1;
        Elevator near(0,1,config), far(0,1,config);
        near.AddHallCall(1,Direction::Up); far.AddHallCall(1,Direction::Up);
        near.BeginBoarding(7,5); far.BeginBoarding(7,15);
        near.Advance(1); far.Advance(1);
        const auto a=near.GetDispatchSnapshot(), b=far.GetDispatchSnapshot();
        tests.Check(a.upTasks==b.upTasks && a.downTasks==b.downTasks &&
            a.stopServices.size()==1 && b.stopServices.size()==1,"pending targets absent from route and buttons");
        tests.Check(!dispatcher.ScoreSnapshot(10,Direction::Up,a).feasible &&
            !dispatcher.ScoreSnapshot(10,Direction::Up,b).feasible,"reserved last seat has no known release");
        tests.Check(near.Advance(2).type==ElevatorEventType::Boarded &&
            far.Advance(2).type==ElevatorEventType::Boarded,"complete real boarding");
        tests.Check(dispatcher.ScoreSnapshot(10,Direction::Up,near.GetDispatchSnapshot()).feasible &&
            !dispatcher.ScoreSnapshot(10,Direction::Up,far.GetDispatchSnapshot()).feasible,
            "known car call may now affect capacity and ETA");
    });
    tests.Run("boarding reservation is counted once", [&] {
        SimulationConfig config; config.capacity=2;
        Elevator real(0,5,config); real.AddHallCall(5,Direction::Up);
        real.BeginBoarding(1,9); real.Advance(2);
        tests.Near(RealPickupTime(real,6,Direction::Up),3,"actual one remaining second plus travel");
        CheckCost(tests,real.GetDispatchSnapshot(),6,Direction::Up,4.5);
    });
    tests.Run("in-progress alighting uses remaining time only", [&] {
        const auto real=AlightingCar(1);
        tests.Near(RealPickupTime(real,6,Direction::Up),3,"actual one remaining second plus travel");
        CheckCost(tests,real.GetDispatchSnapshot(),6,Direction::Up,3);
    });
    tests.Run("unknown same-floor alighting count is not predicted", [&] {
        const auto real=AlightingCar(3);
        tests.Near(RealPickupTime(real,6,Direction::Up),9,"actual still serves all three people");
        CheckCost(tests,real.GetDispatchSnapshot(),6,Direction::Up,3.4);
        CheckCost(tests,real.GetDispatchSnapshot(),5,Direction::Up,1.4);
    });
    tests.Run("car call distribution does not expose exact alighting counts", [&] {
        SimulationConfig config;
        Elevator a(0,1,config), b(0,1,config);
        for(auto* car:{&a,&b}) car->AddHallCall(1,Direction::Up);
        for(int id=0;id<3;++id) {
            a.BeginBoarding(id,id<2 ? 5 : 15); a.Advance(config.personTime);
            b.BeginBoarding(id,id<1 ? 5 : 15); b.Advance(config.personTime);
        }
        a.FinishStop(); b.FinishStop();
        const auto left=dispatcher.ScoreSnapshot(10,Direction::Up,a.GetDispatchSnapshot());
        const auto right=dispatcher.ScoreSnapshot(10,Direction::Up,b.GetDispatchSnapshot());
        tests.Check(left.feasible && right.feasible && left.eta==right.eta && left.cost==right.cost &&
            left.projectedOccupancy==2 && right.projectedOccupancy==2,"same buttons and load give exactly same score");
    });
    tests.Run("observable preview remains read-only and deterministic", [&] {
        const auto route=Car(0,3,Direction::Up,{4},{},0,2);
        for(int repeat=0;repeat<10;++repeat) CheckCost(tests,route,8,Direction::Up,14.5);
        tests.Check(route.upTasks==std::vector<int>({4}) && route.elevator.passengerCount==0 &&
            route.stopServices.size()==1,"preview leaves source unchanged");
    });
    tests.Run("invalid stop metadata rejected", [&] {
        auto route=Car(0,3); route.stopServices={{21,Direction::Up}};
        tests.Check(!dispatcher.ScoreSnapshot(8,Direction::Up,route).feasible,"above building");
        route.stopServices={{0,Direction::Idle}};
        tests.Check(!dispatcher.ScoreSnapshot(8,Direction::Up,route).feasible,"below building");
    });
    tests.Run("estimated LOOK preserves real mixed route travel", [&] {
        // 108 组混合路线：未知外呼增加服务估计，但不会少走真实 LOOK 的扫描路程。
        for(int start:{2,5,10}) for(int target:{3,8,12})
            for(Direction initial:{Direction::Up,Direction::Down})
                for(int request:{2,5,11}) for(Direction direction:{Direction::Up,Direction::Down}) {
                    SimulationConfig config; Elevator real(0,start,config);
                    real.AddHallCall(target,initial); real.AddInternalTarget(7);
                    real.AddHallCall(9,Direction::Down); real.AddHallCall(4,Direction::Up);
                    real.Advance(0.5);
                    const auto score=dispatcher.ScoreSnapshot(request,direction,real.GetDispatchSnapshot());
                    const double travel=RealPickupTime(real,request,direction);
                    tests.Check(score.feasible && score.eta>=travel && score.eta<=travel+5*config.personTime,
                        "same travel with bounded estimated button service");
                }
    });
    tests.Run("aging bonus grows and is capped", [&] {
        tests.Near(dispatcher.GetAgingBonus(10,10),0,"no waiting bonus");
        tests.Near(dispatcher.GetAgingBonus(10,20),0.5,"continuous aging rate");
        tests.Near(dispatcher.GetAgingBonus(10,1000),ElevatorDispatcher::MaxAgingBonus,"aging cap");
        tests.Near(dispatcher.GetAgingBonus(20,10),0,"invalid time order");
    });
    tests.Run("aging admits a reasonable reverse route", [&] {
        const auto reverse=Car(0,11,Direction::Down,{}, {10});
        const auto idle=Car(1,6);
        tests.Check(select(10,Direction::Up,{reverse,idle},0,0)==1,"fresh request prefers idle");
        tests.Check(select(10,Direction::Up,{reverse,idle},0,200)==0,"old request receives bounded relief");
    });
    tests.Run("aging cannot force an obviously poor route", [&] {
        const auto reverse=Car(0,20,Direction::Down,{}, {1});
        const auto idle=Car(1,9);
        tests.Check(select(10,Direction::Up,{reverse,idle},0,10000)==1,"cap does not erase huge ETA");
    });
    tests.Run("equal cost prefers lower ETA before distance and ID", [&] {
        auto faster=Car(1,9,Direction::Up,{15},{},5); faster.personTime=4;
        tests.Check(select(10,Direction::Up,{Car(0,8,Direction::Up,{15}),faster})==1,"both cost 4, ETA 2 beats 4");
    });
    tests.Run("equal cost and ETA prefer distance before ID", [&] {
        auto farther=Car(0,8,Direction::Up,{15}); farther.moveTimePerFloor=1;
        tests.Check(select(10,Direction::Up,{farther,Car(1,9,Direction::Up,{15})})==1,"both ETA 2, distance 1 beats 2");
    });
    tests.Run("equal cost ETA and distance prefer fewer tasks", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,9,Direction::Up,{15,16}),Car(1,9,Direction::Up,{15})})==1,"post-pickup task count before ID"); });
    tests.Run("real snapshot selection leaves elevator state untouched", [&] {
        SimulationConfig config;
        std::vector<Elevator> cars{Elevator(0,1,config),Elevator(1,10,config)};
        cars[0].AddInternalTarget(15); cars[0].Advance(0.5);
        const auto movingBefore=cars[0].GetDispatchSnapshot();
        const auto idleBefore=cars[1].GetDispatchSnapshot();
        for(int repeat=0;repeat<10;++repeat)
            tests.Check(dispatcher.SelectElevator(10,Direction::Up,cars)==1,"repeatable real idle choice");
        const auto movingAfter=cars[0].GetDispatchSnapshot();
        const auto idleAfter=cars[1].GetDispatchSnapshot();
        tests.Check(movingBefore.elevator.currentFloor==movingAfter.elevator.currentFloor &&
            movingBefore.elevator.direction==movingAfter.elevator.direction &&
            movingBefore.elevator.state==movingAfter.elevator.state &&
            movingBefore.remainingActionTime==movingAfter.remainingActionTime &&
            movingBefore.upTasks==movingAfter.upTasks && movingBefore.downTasks==movingAfter.downTasks,
            "dispatch cannot change route, state or remaining timer");
        tests.Check(idleBefore.elevator.currentFloor==idleAfter.elevator.currentFloor &&
            idleAfter.elevator.state==ElevatorState::Idle && idleAfter.elevator.direction==Direction::Idle &&
            idleAfter.remainingActionTime==0 && idleAfter.upTasks.empty() && idleAfter.downTasks.empty() &&
            cars[0].GetPassengerIds().empty() && cars[1].GetPassengerIds().empty(),"dispatch does not assign or move cars");
    });
    tests.Run("full on-way rejected", [&] { tests.Check(select(10, Direction::Up,
        { Car(0,5,Direction::Up,{15},{},10), Car(1,1) }) == 1, "full bypass"); });
    tests.Run("boarding reserves last seat", [&] { auto car=Car(0,5,Direction::Up,{15},{},9);
        car.reservedBoardingCount=1; tests.Check(select(10,Direction::Up,{car,Car(1,1)})==1,"reserved capacity"); });
    tests.Run("all full", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,1,Direction::Up,{20},{},10),Car(1,20,Direction::Down,{}, {1},10)})==-1,"no capacity"); });
    tests.Run("no elevators", [&] { tests.Check(select(5,Direction::Up,{})==-1,"empty group"); });
    tests.Run("idle request invalid", [&] { tests.Check(select(5,Direction::Idle,{Car(0,1)})==-1,"direction invalid"); });
    tests.Run("invalid floor", [&] { tests.Check(select(0,Direction::Up,{Car(0,1)})==-1,"lower bound"); });
    tests.Run("above building", [&] { tests.Check(select(21,Direction::Down,{Car(0,1)})==-1,"upper bound"); });
    tests.Run("top down", [&] { tests.Check(select(20,Direction::Down,{Car(0,1),Car(1,19)})==1,"top call"); });
    tests.Run("bottom up", [&] { tests.Check(select(1,Direction::Up,{Car(0,2),Car(1,19)})==0,"bottom call"); });
    tests.Run("impossible boundary directions", [&] {
        tests.Check(select(20,Direction::Up,{Car(0,1)})==-1,"top up");
        tests.Check(select(1,Direction::Down,{Car(0,1)})==-1,"bottom down"); });
    tests.Run("up request behind uses idle", [&] { tests.Check(select(3,Direction::Up,
        {Car(0,5,Direction::Up,{15}),Car(1,20)})==1,"behind is not on-way"); });
    tests.Run("down request behind uses idle", [&] { tests.Check(select(17,Direction::Down,
        {Car(0,15,Direction::Down,{}, {2}),Car(1,1)})==1,"behind is not on-way"); });
    tests.Run("all busy deferred route", [&] { tests.Check(select(3,Direction::Up,
        {Car(0,5,Direction::Up,{19}),Car(1,8,Direction::Up,{10})})==1,"shorter remaining sweep"); });
    tests.Run("stable id not vector order", [&] { tests.Check(select(5,Direction::Up,
        {Car(9,1),Car(2,1)})==1,"lower ID wins, return index"); });
    tests.Run("load breaks equal route", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,5,Direction::Up,{15},{},8),Car(1,5,Direction::Up,{15},{},1)})==1,"load cost"); });
    tests.Run("intermediate stops cost time", [&] { tests.Check(select(10,Direction::Up,
        {Car(0,6,Direction::Up,{7,8,9,15}),Car(1,5,Direction::Up,{15})})==1,"ETA beats distance"); });
    tests.Run("remaining movement time", [&] { auto a=Car(0,5,Direction::Up,{15}); auto b=a;
        a.betweenFloors=b.betweenFloors=true; a.remainingActionTime=1.8; b.remainingActionTime=0.2;
        b.elevator.id=1; tests.Check(select(10,Direction::Up,{a,b})==1,"fractional movement"); });
    tests.Run("remaining service time", [&] { auto a=Car(0,5,Direction::Up,{15}); auto b=a;
        a.remainingActionTime=2.5; b.remainingActionTime=0.5; b.elevator.id=1;
        tests.Check(select(10,Direction::Up,{a,b})==1,"service delay"); });
    tests.Run("departed landing is behind", [&] { auto a=Car(0,5,Direction::Up,{15});
        a.betweenFloors=true; a.remainingActionTime=1.0;
        tests.Check(select(5,Direction::Up,{a,Car(1,1)})==1,"cannot stop after departure"); });
    tests.Run("no side effects and repeatable", [&] { std::vector<ElevatorDispatchSnapshot> cars{
        Car(0,5,Direction::Up,{8,12,15}),Car(1,20,Direction::Down,{}, {1})};
        for(int repeat=0;repeat<10;++repeat) tests.Check(select(10,Direction::Up,cars)==0,"deterministic");
        tests.Check(cars[0].upTasks==std::vector<int>({8,12,15}) && cars[0].elevator.currentFloor==5,"immutable"); });
    tests.Run("nonfinite estimate rejected", [&] { auto a=Car(0,5); a.moveTimePerFloor=std::numeric_limits<double>::infinity();
        tests.Check(select(10,Direction::Up,{a})==-1,"finite cost"); });
    tests.Run("invalid route snapshot rejected", [&] {
        auto a=Car(0,20,Direction::Up,{21}); a.betweenFloors=true; a.remainingActionTime=1;
        tests.Check(select(10,Direction::Up,{a})==-1,"cannot travel above roof");
        a=Car(0,5,Direction::Up,{0}); tests.Check(select(10,Direction::Up,{a})==-1,"bad task floor");
        a=Car(0,(std::numeric_limits<int>::max)(),Direction::Up); a.floorCount=0; a.betweenFloors=true;
        tests.Check(select(10,Direction::Up,{a})==-1,"no integer overflow at unknown upper bound");
    });
    tests.Run("reassign only for a material ETA improvement", [&] {
        const auto request=Call(10,Direction::Up);
        const auto owner=Car(0,1,Direction::Up,{10});
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,6)},20)==1,"18s to 8s qualifies");
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,3)},20)==0,"4s gain below 5s threshold");
        auto boundary=Car(1,6); boundary.moveTimePerFloor=3.25;
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,boundary},20)==1,"exactly 5s gain qualifies");
    });
    tests.Run("near and serving owners are protected", [&] {
        const auto request=Call(10,Direction::Up);
        auto owner=Car(0,9,Direction::Up,{10}); owner.remainingActionTime=50;
        owner.elevator.state=ElevatorState::MovingUp; owner.betweenFloors=true; owner.moveTimePerFloor=60;
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20)==0,"one floor proximity lock");
        owner.elevator.currentFloor=10; owner.elevator.state=ElevatorState::Boarding;
        owner.betweenFloors=false;
        owner.reservedBoardingCount=1;
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20)==0,"boarding at request locked");
        owner.elevator.state=ElevatorState::Alighting;
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20)==0,"alighting at request locked");
    });
    tests.Run("reassignment cooldown prevents oscillation", [&] {
        const auto request=Call(10,Direction::Up);
        const std::vector<ElevatorDispatchSnapshot> cars{Car(0,1),Car(1,6)};
        int owner=dispatcher.SelectReassignment(request,0,cars,20);
        tests.Check(owner==1,"initial reassignment");
        for(int repeat=0;repeat<20;++repeat)
            tests.Check(dispatcher.SelectReassignment(request,owner,cars,20,20)==owner,"stable same timestamp");
        const std::vector<ElevatorDispatchSnapshot> changed{Car(0,10),Car(1,6)};
        tests.Check(dispatcher.SelectReassignment(request,1,changed,29.9,20)==1,"10 second cooldown");
        tests.Check(dispatcher.SelectReassignment(request,1,changed,30,20)==0,"cooldown expires at physical event");
    });
    tests.Run("departed request floor does not lock reassignment", [&] {
        const auto request=Call(10,Direction::Up);
        auto owner=Car(0,10,Direction::Up,{10,18});
        owner.elevator.state=ElevatorState::MovingUp; owner.betweenFloors=true; owner.remainingActionTime=1;
        tests.Check(dispatcher.ScoreSnapshot(10,Direction::Up,owner).feasible,"departed owner still feasible after reversal");
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20)==1,"same integer floor already departed");
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20,19)==0,"feasible departed owner still has cooldown");
    });
    tests.Run("one floor away while departing is not proximity protected", [&] {
        const auto request=Call(10,Direction::Up);
        auto below=Car(0,9,Direction::Down,{10},{2});
        below.elevator.state=ElevatorState::MovingDown; below.betweenFloors=true; below.remainingActionTime=1;
        auto above=Car(0,11,Direction::Up,{10,18});
        above.elevator.state=ElevatorState::MovingUp; above.betweenFloors=true; above.remainingActionTime=1;
        tests.Check(dispatcher.SelectReassignment(request,0,{below,Car(1,10)},20)==1,"moving away below request");
        tests.Check(dispatcher.SelectReassignment(request,0,{above,Car(1,10)},20)==1,"moving away above request");
        auto idle=Car(0,9); idle.moveTimePerFloor=10;
        tests.Check(dispatcher.SelectReassignment(request,0,{idle,Car(1,10)},20)==1,"idle is not approaching");
        auto approaching=Car(0,11,Direction::Down,{}, {10});
        approaching.elevator.state=ElevatorState::MovingDown; approaching.betweenFloors=true;
        approaching.moveTimePerFloor=10; approaching.remainingActionTime=8;
        tests.Check(dispatcher.SelectReassignment(Call(10,Direction::Down),0,{approaching,Car(1,10)},20)==0,
            "downward approach also protected despite eight second gain");
    });
    tests.Run("infeasible owner bypasses cooldown and approaching protection", [&] {
        const auto request=Call(10,Direction::Up);
        auto owner=Car(0,9,Direction::Up,{10,20},{},1,1);
        owner.elevator.state=ElevatorState::MovingUp; owner.betweenFloors=true; owner.remainingActionTime=1;
        tests.Check(!dispatcher.ScoreSnapshot(10,Direction::Up,owner).feasible,"full through pickup floor");
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20,19)==1,
            "unserviceable owner bypasses both protections immediately");
        tests.Check(dispatcher.SelectReassignment(request,0,{owner,owner},20,19)==0,"no feasible alternative keeps owner");
    });
    tests.Run("actual landing service remains locked even if infeasible", [&] {
        const auto request=Call(10,Direction::Up);
        for(auto state:{ElevatorState::Stopped,ElevatorState::Boarding,ElevatorState::Alighting})
        {
            auto owner=Car(0,10,Direction::Up,{10},{},1,1);
            owner.elevator.state=state;
            if(state==ElevatorState::Boarding)
            {
                owner.elevator.passengerCount=0; owner.reservedBoardingCount=1;
                owner.stopServices.push_back({15,Direction::Idle});
            }
            if(state==ElevatorState::Alighting) owner.stopServices.push_back({10,Direction::Idle});
            owner.personTime=10;
            owner.remainingActionTime=state==ElevatorState::Stopped ? 0 : 8;
            const auto score=dispatcher.ScoreSnapshot(10,Direction::Up,owner);
            tests.Check(state==ElevatorState::Alighting ? score.feasible && score.eta>=5 : !score.feasible,
                "fixture would reassign without actual service lock");
            tests.Check(dispatcher.SelectReassignment(request,0,{owner,Car(1,10)},20)==0,
                "actual service cannot be stolen");
        }
    });
    tests.Run("joint assignment beats a fixed greedy counterexample", [&] {
        const std::vector<ElevatorDispatchSnapshot> cars{Car(0,5),Car(1,1)};
        const std::vector<HallCallDispatchSnapshot> requests{
            Call(4,Direction::Up,0),Call(6,Direction::Up,1)};
        const auto first=dispatcher.SelectFromSnapshots(4,Direction::Up,cars);
        SimulationConfig config; config.capacity=10;
        Elevator greedyFirst(0,5,config); greedyFirst.AddHallCall(4,Direction::Up);
        auto accepted=greedyFirst.GetDispatchSnapshot();
        const auto second=dispatcher.SelectFromSnapshots(6,Direction::Up,{accepted,cars[1]});
        tests.Check(first==0 && second==1,"greedy E1 then E2");
        const double greedy=dispatcher.ScoreSnapshot(4,Direction::Up,cars[0]).cost+
            dispatcher.ScoreSnapshot(6,Direction::Up,cars[1]).cost;
        const auto plan=dispatcher.PlanAssignments(requests,cars,0);
        tests.Check(plan.elevatorIndices==std::vector<int>({1,0}),"joint E2 then E1");
        tests.Near(greedy,12,"greedy total cost"); tests.Near(plan.totalCost,8,"joint total cost");
        std::cout << "Two-call cost: greedy=" << greedy << ", joint=" << plan.totalCost << '\n';
    });
    tests.Run("three request search is bounded and deterministic", [&] {
        const std::vector<ElevatorDispatchSnapshot> cars{Car(0,2),Car(1,6),Car(2,10)};
        const std::vector<HallCallDispatchSnapshot> calls{
            Call(2,Direction::Up,0),Call(6,Direction::Up,1),Call(10,Direction::Down,2)};
        const auto plan=dispatcher.PlanAssignments(calls,cars,0);
        tests.Check(plan.elevatorIndices==std::vector<int>({0,1,2}) && plan.assignedCount==3,"three immediate pickups");
        tests.Near(plan.totalCost,0,"no travel or earlier service");
        tests.Check(plan.evaluatedCombinations<=ElevatorDispatcher::MaxJointCombinations &&
            plan.scoreEvaluations<=21*cars.size()+192,"bounded branching and scoring");
        for(int repeat=0;repeat<5;++repeat)
            tests.Check(dispatcher.PlanAssignments(calls,cars,0).elevatorIndices==plan.elevatorIndices,"stable tie breaks");
        tests.Check(cars[0].upTasks.empty() && cars[0].elevator.state==ElevatorState::Idle &&
            calls[0].floor==2 && calls[0].direction==Direction::Up,"search only changes local copies");
    });
    tests.Run("joint search selects oldest three even if input unsorted", [&] {
        const auto plan=dispatcher.PlanAssignments({Call(2,Direction::Up,0,30),Call(4,Direction::Up),
            Call(6,Direction::Up,2,10),Call(8,Direction::Up,3,20)},
            {Car(0,2),Car(1,4),Car(2,6),Car(3,8)},40);
        tests.Check(plan.assignedCount==3 && plan.elevatorIndices[0]==InvalidElevatorId,"fourth newest waits");
        tests.Check(plan.evaluatedCombinations<=64,"large input does not grow batch");
    });
    tests.Run("joint requests share current spare capacity without invented releases", [&] {
        const auto plan=dispatcher.PlanAssignments({Call(2,Direction::Up,0),Call(4,Direction::Up,1),
            Call(6,Direction::Up,2)}, {Car(0,1,Direction::Idle,{}, {},0,3)},0);
        tests.Check(plan.elevatorIndices==std::vector<int>({0,0,0}),"all three fit along one route");
        tests.Near(plan.totalEta,27,"ETA 2 + 9 + 16 includes one T per earlier hall");
        tests.Near(plan.maxEta,16,"latest pickup");
    });
    tests.Run("final joint route reevaluates earlier request costs", [&] {
        const auto plan=dispatcher.PlanAssignments({Call(6,Direction::Up,0),Call(2,Direction::Up,1)},
            {Car(0,1,Direction::Idle,{}, {},0,2)},0);
        tests.Check(plan.assignedCount==2,"same car route feasible");
        tests.Near(plan.totalCost,16.5,"older 6F ETA becomes 13 plus load 1.5; 2F costs 2");
    });
    tests.Run("insufficient capacity leaves partial plan", [&] {
        const auto plan=dispatcher.PlanAssignments({Call(4,Direction::Up,0),Call(5,Direction::Up,1)},
            {Car(0,1,Direction::Idle,{}, {},0,1),Car(1,2,Direction::Up,{20},{},1,1)},0);
        tests.Check(plan.assignedCount==1 && plan.elevatorIndices==std::vector<int>({0,-1}),"cannot fill same seat twice");
        const auto empty=dispatcher.PlanAssignments({Call(4,Direction::Up)},{Car(0,1,Direction::Up,{20},{},1,1)},0);
        tests.Check(empty.assignedCount==0 && empty.elevatorIndices[0]==-1,"all infeasible stays pending");
    });
    tests.Run("joint assignment reuses observable capacity feasibility", [&] {
        auto full=Car(0,1,Direction::Up,{5},{},1,1);
        const auto blocked=dispatcher.PlanAssignments({Call(10,Direction::Up)},{full},0);
        tests.Check(blocked.assignedCount==0 && blocked.elevatorIndices[0]==InvalidElevatorId,
            "full car without an earlier car call stays infeasible");
        full.stopServices={{5,Direction::Idle}};
        const auto released=dispatcher.PlanAssignments({Call(10,Direction::Up)},{full},0);
        tests.Check(released.assignedCount==1 && released.elevatorIndices[0]==0,
            "same ScoreSnapshot release estimate makes the joint candidate feasible");
    });
    tests.Run("aging remains active in joint cost", [&] {
        const std::vector<ElevatorDispatchSnapshot> cars{Car(0,11,Direction::Down,{}, {10}),Car(1,6)};
        const std::vector<HallCallDispatchSnapshot> calls{Call(10,Direction::Up)};
        tests.Check(dispatcher.PlanAssignments(calls,cars,0).elevatorIndices[0]==1,"fresh prefers idle");
        tests.Check(dispatcher.PlanAssignments(calls,cars,200).elevatorIndices[0]==0,"aging admits reverse route");
    });
    tests.Run("large fleet still respects joint search bound", [&] {
        std::vector<ElevatorDispatchSnapshot> cars;
        for(int id=0;id<60;++id) cars.push_back(Car(id,id%18+1));
        const auto plan=dispatcher.PlanAssignments({Call(4,Direction::Up,0),Call(9,Direction::Down,1),
            Call(15,Direction::Up,2)},cars,0);
        tests.Check(plan.assignedCount==3 && plan.evaluatedCombinations<=64,"60 cars do not cause 60 cubed search");
        tests.Check(plan.scoreEvaluations<=21*cars.size()+192,"candidate scan is linear in fleet size");
    });
    tests.Run("joint ties use IDs rather than input order", [&] {
        const std::vector<HallCallDispatchSnapshot> calls{Call(5,Direction::Up)};
        tests.Check(dispatcher.PlanAssignments(calls,{Car(9,1),Car(2,1)},0).elevatorIndices[0]==1,"lower ID selected");
        tests.Check(dispatcher.PlanAssignments(calls,{Car(2,1),Car(9,1)},0).elevatorIndices[0]==0,"stable after permutation");
    });
    tests.Run("hall cancellation restores deferred feasibility on fresh snapshot", [&] {
        SimulationConfig config; config.capacity=1;
        Elevator owner(0,1,config); owner.AddInternalTarget(20); owner.AddHallCall(5,Direction::Up);
        auto before=owner.GetDispatchSnapshot();
        const auto request=Call(10,Direction::Up,42,7);
        tests.Check(!dispatcher.ScoreSnapshot(10,Direction::Up,before,7,100).feasible,"known pickup consumes only seat");
        tests.Check(owner.RemoveHallCall(5,Direction::Up),"route cancellation accepted");
        const auto after=owner.GetDispatchSnapshot();
        tests.Check(dispatcher.ScoreSnapshot(10,Direction::Up,after,7,100).feasible,"same feasibility restores eligibility");
        const auto plan=dispatcher.PlanAssignments({request},{after},100);
        tests.Check(plan.assignedCount==1 && plan.elevatorIndices[0]==0,"restored request participates normally");
        tests.Check(request.firstPassengerId==42 && request.firstRequestTime==7,"request identity and aging input unchanged");
    });
    return tests.Finish();
}
