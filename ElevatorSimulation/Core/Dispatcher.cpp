#include "Dispatcher.h"
#include "FixedThreadPool.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <thread>

namespace
{
    // 集中定义 Aging 斜率与上限：成本折扣最多 8 秒，避免等待时间压倒路线质量。
    constexpr double kAgingRate = ElevatorDispatcher::AgingBonusRate;
    constexpr double kAgingCap = ElevatorDispatcher::MaxAgingBonus;

    bool IsTravelDirection(Direction direction)
    {
        return direction == Direction::Up || direction == Direction::Down;
    }

    struct RouteEstimate
    {
        double eta = std::numeric_limits<double>::infinity();
        int projectedOccupancy = 0;
        bool reachedRequest = false;
    };

    // 在局部副本上预演 LOOK：内呼估计下 1 人，外呼估计上 1 人，不猜测目的层。
    RouteEstimate EstimatePickupTime(int requestFloor, Direction requestDirection,
        const ElevatorDispatchSnapshot& snapshot)
    {
        const auto& car = snapshot.elevator;
        RouteEstimate result;
        result.projectedOccupancy = car.passengerCount + snapshot.reservedBoardingCount;
        if (result.projectedOccupancy < 0 || result.projectedOccupancy > car.capacity)
            return result;
        std::set<int> up(snapshot.upTasks.begin(), snapshot.upTasks.end());
        std::set<int> down(snapshot.downTasks.begin(), snapshot.downTasks.end());
        std::set<int> carTargets;
        std::set<std::pair<int, Direction>> hallCalls;
        for (const auto& stop : snapshot.stopServices)
        {
            if (stop.floor < 1 || (snapshot.floorCount > 0 && stop.floor > snapshot.floorCount) ||
                (stop.direction != Direction::Idle && !IsTravelDirection(stop.direction)))
                return result;
            if (stop.direction == Direction::Idle)
                carTargets.insert(stop.floor);
            else
            {
                hallCalls.emplace(stop.floor, stop.direction);
                (stop.direction == Direction::Up ? up : down).insert(stop.floor);
            }
        }
        // 内呼在任一方向服务，双向外呼仅在各自方向服务。
        for (int target : carTargets)
        {
            if (hallCalls.count({ target, Direction::Up }) == 0) up.erase(target);
            if (hallCalls.count({ target, Direction::Down }) == 0) down.erase(target);
        }
        (requestDirection == Direction::Up ? up : down).insert(requestFloor);
        int floor = car.currentFloor;
        Direction direction = car.direction;
        double time = snapshot.remainingActionTime;
        if (snapshot.betweenFloors)
            floor += direction == Direction::Up ? 1 : -1;
        if (car.state == ElevatorState::Alighting)
        {
            // 当前下客者只计动作剩余时间；同层内呼不再额外释放第二个席位。
            if (result.projectedOccupancy <= 0) return result;
            --result.projectedOccupancy;
            carTargets.erase(floor);
        }
        if (car.state == ElevatorState::Boarding)
        {
            // 当前外呼的一人估计已由预留席位和动作剩余时间覆盖。
            (direction == Direction::Up ? up : down).erase(floor);
        }
        if (car.state == ElevatorState::Idle)
            direction = floor == requestFloor ? requestDirection : GetDirection(floor, requestFloor);

        // 每个不同内呼只消费一次；未知外呼不会创建未来内呼或延长扫描终点。
        bool currentStop = !snapshot.betweenFloors &&
            (car.state == ElevatorState::Stopped || car.state == ElevatorState::Boarding ||
                car.state == ElevatorState::Alighting);
        while (currentStop || !up.empty() || !down.empty() || !carTargets.empty())
        {
            auto& active = direction == Direction::Up ? up : down;
            const bool carStop = carTargets.erase(floor) != 0;
            if (currentStop || carStop || active.count(floor) != 0)
            {
                currentStop = false;
                if (carStop && result.projectedOccupancy > 0)
                {
                    --result.projectedOccupancy;
                    time += snapshot.personTime;
                }
                if (floor == requestFloor && direction == requestDirection)
                {
                    // ETA 到请求层估计下客完成为止，尚未计本外呼的上客。
                    result.eta = time;
                    result.reachedRequest = result.projectedOccupancy < car.capacity;
                    return result;
                }
                if (active.erase(floor) != 0)
                {
                    // 未知停站固定计一个 T；有剩余容量时估计上 1 人。
                    time += snapshot.personTime;
                    if (result.projectedOccupancy < car.capacity)
                        ++result.projectedOccupancy;
                }
            }
            int nextFloor = floor;
            bool found = false;
            // 和 HasTasksAhead 一致：任意方向外呼及内呼都决定当前扫描的延伸。
            // 经过反向外呼时不停车服务，但不能在抵达该折返点前提前反向。
            for (const auto* tasks : { &up, &down, &carTargets })
                for (int target : *tasks)
                {
                    const bool ahead = direction == Direction::Up ? target > floor : target < floor;
                    const bool nearer = direction == Direction::Up ? target < nextFloor : target > nextFloor;
                    if (ahead && (!found || nearer))
                    {
                        found = true;
                        nextFloor = target;
                    }
                }
            if (found)
            {
                time += std::abs(static_cast<double>(nextFloor) - floor) * snapshot.moveTimePerFloor;
                floor = nextFloor;
            }
            else
                direction = direction == Direction::Up ? Direction::Down : Direction::Up;
        }
        return result;
    }

    auto CandidateKey(const DispatchScore& score, const ElevatorDispatchSnapshot& snapshot, int floor)
    {
        return std::make_tuple(score.cost, score.eta,
            std::abs(static_cast<double>(floor) - snapshot.elevator.currentFloor),
            snapshot.upTasks.size() + snapshot.downTasks.size(), snapshot.elevator.id);
    }

    void AddPlannedHallCall(ElevatorDispatchSnapshot& snapshot, const HallCallDispatchSnapshot& request)
    {
        auto& tasks = request.direction == Direction::Up ? snapshot.upTasks : snapshot.downTasks;
        tasks.push_back(request.floor);
        std::sort(tasks.begin(), tasks.end());
        tasks.erase(std::unique(tasks.begin(), tasks.end()), tasks.end());
        snapshot.stopServices.push_back({ request.floor, request.direction });
        if (snapshot.elevator.state == ElevatorState::Idle)
        {
            // 与真实 AddHallCall 一致：第一项任务锁定起步方向；后续组合不能重选方向。
            const bool atFloor = snapshot.elevator.currentFloor == request.floor;
            snapshot.elevator.direction = atFloor ? request.direction :
                GetDirection(snapshot.elevator.currentFloor, request.floor);
            snapshot.elevator.state = atFloor ? ElevatorState::Stopped :
                (snapshot.elevator.direction == Direction::Up ? ElevatorState::MovingUp : ElevatorState::MovingDown);
            snapshot.betweenFloors = !atFloor;
            snapshot.remainingActionTime = atFloor ? 0.0 : snapshot.moveTimePerFloor;
        }
    }
}

ElevatorDispatcher::ElevatorDispatcher() = default;

ElevatorDispatcher::ElevatorDispatcher(DispatcherExecutionMode mode, std::size_t workerCount)
{
    SetExecutionMode(mode, workerCount);
}

ElevatorDispatcher::~ElevatorDispatcher() = default;
ElevatorDispatcher::ElevatorDispatcher(ElevatorDispatcher&&) noexcept = default;
ElevatorDispatcher& ElevatorDispatcher::operator=(ElevatorDispatcher&&) noexcept = default;

void ElevatorDispatcher::SetExecutionMode(DispatcherExecutionMode mode, std::size_t workerCount)
{
    if (mode == DispatcherExecutionMode::Sequential)
    {
        m_threadPool.reset();
        m_executionMode = mode;
        return;
    }
    if (workerCount == 0)
    {
        const auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
        workerCount = (std::min)(hardwareThreads > 1 ? hardwareThreads - 1 : 1, std::size_t{ 8 });
    }
    m_threadPool = std::make_unique<FixedThreadPool>(workerCount);
    m_executionMode = mode;
}

std::size_t ElevatorDispatcher::GetWorkerCount() const noexcept
{
    return m_threadPool ? m_threadPool->GetThreadCount() : 0;
}

double ElevatorDispatcher::GetAgingBonus(double requestTime, double currentTime) const noexcept
{
    if (!std::isfinite(requestTime) || !std::isfinite(currentTime) || currentTime <= requestTime)
        return 0.0;
    return (std::min)(kAgingCap, (currentTime - requestTime) * kAgingRate);
}

int ElevatorDispatcher::SelectElevator(int requestFloor, Direction requestDirection,
    const std::vector<Elevator>& elevators, double requestTime, double currentTime) const
{
    std::vector<ElevatorDispatchSnapshot> snapshots;
    snapshots.reserve(elevators.size());
    for (const auto& elevator : elevators)
        snapshots.push_back(elevator.GetDispatchSnapshot());
    return SelectFromSnapshots(requestFloor, requestDirection, snapshots, requestTime, currentTime);
}

int ElevatorDispatcher::SelectFromSnapshots(int requestFloor, Direction requestDirection,
    const std::vector<ElevatorDispatchSnapshot>& elevators,
    double requestTime, double currentTime) const
{
    using Score = std::tuple<double, double, double, std::size_t, int>;
    Score bestScore;
    int selected = InvalidElevatorId;
    const auto scores = ScoreSnapshots(requestFloor, requestDirection, elevators, requestTime, currentTime);
    for (std::size_t index = 0; index < elevators.size(); ++index)
    {
        const auto& score = scores[index];
        if (!score.feasible) continue;
        const auto key = CandidateKey(score, elevators[index], requestFloor);
        if (selected == InvalidElevatorId || key < bestScore)
        {
            selected = static_cast<int>(index);
            bestScore = key;
        }
    }
    return selected;
}

std::vector<DispatchScore> ElevatorDispatcher::ScoreSnapshots(int requestFloor, Direction requestDirection,
    const std::vector<ElevatorDispatchSnapshot>& elevators, double requestTime, double currentTime) const
{
    std::vector<DispatchScore> scores(elevators.size());
    if (m_executionMode == DispatcherExecutionMode::Sequential || elevators.size() < 2)
    {
        for (std::size_t index = 0; index < elevators.size(); ++index)
            scores[index] = ScoreSnapshot(requestFloor, requestDirection, elevators[index], requestTime, currentTime);
        return scores;
    }

    std::vector<std::future<DispatchScore>> futures;
    futures.reserve(elevators.size());
    for (std::size_t index = 0; index < elevators.size(); ++index)
    {
        futures.push_back(m_threadPool->Submit([this, requestFloor, requestDirection, &elevators,
            requestTime, currentTime, index]
        {
            return ScoreSnapshot(requestFloor, requestDirection, elevators[index], requestTime, currentTime);
        }));
    }
    // 固定按快照下标取回结果；任务完成先后不参与选择或同分排序。
    for (std::size_t index = 0; index < futures.size(); ++index)
        scores[index] = futures[index].get();
    return scores;
}

DispatchScore ElevatorDispatcher::ScoreSnapshot(int requestFloor, Direction requestDirection,
    const ElevatorDispatchSnapshot& snapshot, double requestTime, double currentTime) const
{
    DispatchScore score;
    if (requestFloor < 1 || !IsTravelDirection(requestDirection)) return score;
    const auto& car = snapshot.elevator;
    if (car.id < 0 || car.currentFloor < 1 || car.capacity <= 0 || car.passengerCount < 0 ||
            snapshot.reservedBoardingCount < 0 ||
            snapshot.reservedBoardingCount > car.capacity ||
            car.passengerCount > car.capacity - snapshot.reservedBoardingCount ||
            (snapshot.floorCount > 0 && (requestFloor > snapshot.floorCount ||
                car.currentFloor > snapshot.floorCount ||
                (requestFloor == snapshot.floorCount && requestDirection == Direction::Up))) ||
            (requestFloor == 1 && requestDirection == Direction::Down) ||
            !std::isfinite(snapshot.moveTimePerFloor) || snapshot.moveTimePerFloor <= 0.0 ||
            !std::isfinite(snapshot.personTime) || snapshot.personTime <= 0.0 ||
            !std::isfinite(snapshot.remainingActionTime) || snapshot.remainingActionTime < 0.0)
        return score;
    if (snapshot.betweenFloors &&
            ((car.direction == Direction::Down && car.currentFloor == 1) ||
                (car.direction == Direction::Up && (car.currentFloor == (std::numeric_limits<int>::max)() ||
                    (snapshot.floorCount > 0 && car.currentFloor == snapshot.floorCount)))))
        return score;
    const auto validTask = [&](int floor)
    { return floor >= 1 && (snapshot.floorCount == 0 || floor <= snapshot.floorCount); };
    bool validTasks = true;
    for (int floor : snapshot.upTasks) validTasks = validTasks && validTask(floor);
    for (int floor : snapshot.downTasks) validTasks = validTasks && validTask(floor);
    if (!validTasks) return score;
    const bool idle = car.state == ElevatorState::Idle;
    if (!idle && !IsTravelDirection(car.direction)) return score;
    const bool ahead = car.direction == Direction::Up ? requestFloor > car.currentFloor :
        requestFloor < car.currentFloor;
    const bool onWay = !idle && car.direction == requestDirection &&
        (ahead || (!snapshot.betweenFloors && requestFloor == car.currentFloor));
    const RouteEstimate estimate = EstimatePickupTime(requestFloor, requestDirection, snapshot);
    if (!estimate.reachedRequest) return score;
    const double eta = estimate.eta;
    // 顺路与空闲统一比较成本。非顺路忙碌梯增加 S+T 的有限策略成本，
    // 实际返程/折返耗时已计入 ETA；该附加项不代表额外物理耗时。
    const double directionCost = idle || onWay ? 0.0 : snapshot.moveTimePerFloor + snapshot.personTime;
    // 以请求层完成下客、尚未接本外呼时的估计载荷评分，附加不超过一个 T。
    const double loadCost = snapshot.personTime * static_cast<double>(estimate.projectedOccupancy) / car.capacity;
    // Aging 抵消有限方向惩罚，保持已验证的 Cost 口径。
    score.directionPenalty = (std::max)(0.0, directionCost - GetAgingBonus(requestTime, currentTime));
    score.cost = eta + loadCost + score.directionPenalty;
    score.eta = eta;
    score.projectedOccupancy = estimate.projectedOccupancy;
    score.feasible = std::isfinite(score.cost);
    return score;
}

int ElevatorDispatcher::SelectReassignment(const HallCallDispatchSnapshot& request, int currentElevatorIndex,
    const std::vector<ElevatorDispatchSnapshot>& elevators, double currentTime, double lastReassignmentTime) const
{
    if (currentElevatorIndex < 0 || static_cast<std::size_t>(currentElevatorIndex) >= elevators.size())
        return InvalidElevatorId;
    const auto& owner = elevators[static_cast<std::size_t>(currentElevatorIndex)];
    if (!std::isfinite(currentTime) || !std::isfinite(lastReassignmentTime))
        return currentElevatorIndex;
    const auto current = ScoreSnapshot(request.floor, request.direction, owner, request.firstRequestTime, currentTime);
    const auto& car = owner.elevator;
    // currentFloor 在移动中是已离开的整数层，不能把它当成仍在站内服务。
    const bool serving = !owner.betweenFloors && car.currentFloor == request.floor &&
        (car.state == ElevatorState::Stopped || car.state == ElevatorState::Boarding ||
            car.state == ElevatorState::Alighting);
    if (serving) return currentElevatorIndex;
    if (current.feasible)
    {
        const double delta = static_cast<double>(request.floor) - car.currentFloor;
        const bool approaching = owner.betweenFloors &&
            ((car.state == ElevatorState::MovingUp && car.direction == Direction::Up && delta > 0) ||
                (car.state == ElevatorState::MovingDown && car.direction == Direction::Down && delta < 0));
        if ((lastReassignmentTime != UnsetTime && currentTime - lastReassignmentTime < ReassignCooldownSeconds) ||
            (approaching && std::abs(delta) <= ReassignLockDistanceFloors))
            return currentElevatorIndex;
    }
    using Key = std::tuple<double, double, double, std::size_t, int>;
    Key bestKey;
    double bestEta = std::numeric_limits<double>::infinity();
    int selected = currentElevatorIndex;
    const auto scores = ScoreSnapshots(request.floor, request.direction, elevators,
        request.firstRequestTime, currentTime);
    for (std::size_t index = 0; index < elevators.size(); ++index)
    {
        if (index == static_cast<std::size_t>(currentElevatorIndex)) continue;
        const auto& score = scores[index];
        if (!score.feasible) continue;
        const auto& snapshot = elevators[index];
        const Key key{score.eta, score.cost,
            std::abs(static_cast<double>(snapshot.elevator.currentFloor) - request.floor),
            snapshot.upTasks.size() + snapshot.downTasks.size(), snapshot.elevator.id};
        if (selected == currentElevatorIndex || key < bestKey)
        {
            selected = static_cast<int>(index);
            bestKey = key;
            bestEta = score.eta;
        }
    }
    // 已不可服务的归属立即寻找替代，不受普通滞回限制；正在站内服务的保护仍优先。
    return selected != currentElevatorIndex && (!current.feasible || current.eta - bestEta >= ReassignThresholdSeconds) ?
        selected : currentElevatorIndex;
}

DispatchPlan ElevatorDispatcher::PlanAssignments(const std::vector<HallCallDispatchSnapshot>& requests,
    const std::vector<ElevatorDispatchSnapshot>& elevators, double currentTime) const
{
    DispatchPlan result;
    result.elevatorIndices.assign(requests.size(), InvalidElevatorId);
    if (!std::isfinite(currentTime)) return result;
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right)
    {
        const auto& a=requests[left]; const auto& b=requests[right];
        const double ta=std::isfinite(a.firstRequestTime) ? a.firstRequestTime : std::numeric_limits<double>::infinity();
        const double tb=std::isfinite(b.firstRequestTime) ? b.firstRequestTime : std::numeric_limits<double>::infinity();
        return std::make_tuple(ta,a.firstPassengerId,a.floor,a.direction) <
            std::make_tuple(tb,b.firstPassengerId,b.floor,b.direction);
    });
    if (order.size() > MaxJointRequests) order.resize(MaxJointRequests);
    if (order.empty()) return result;
    auto local = elevators;
    std::vector<int> chosen(order.size(), InvalidElevatorId);
    std::vector<double> directionPenalties(order.size());
    using PlanKey = std::tuple<int, double, double, double, std::vector<int>>;
    PlanKey bestKey;
    bool foundPlan = false;
    std::function<void(std::size_t)> search = [&](std::size_t depth)
    {
        if (depth == order.size())
        {
            ++result.evaluatedCombinations;
            int assigned=0;
            double cost=0.0, maxEta=0.0, totalEta=0.0;
            std::vector<int> ids(order.size(), (std::numeric_limits<int>::max)());
            for (std::size_t i=0;i<order.size();++i)
            {
                if (chosen[i] == InvalidElevatorId) continue;
                const auto& request=requests[order[i]];
                const auto& car=local[static_cast<std::size_t>(chosen[i])];
                ++result.scoreEvaluations;
                const auto score=ScoreSnapshot(request.floor,request.direction,car,request.firstRequestTime,currentTime);
                if (!score.feasible) return;
                ++assigned;
                // 最终组合会改变先前请求的路线：重新计算所有 ETA/载荷。
                // 方向惩罚沿用插入该请求之前的上下文，避免空闲梯被事后重复惩罚。
                cost += score.eta + car.personTime * score.projectedOccupancy / car.elevator.capacity + directionPenalties[i];
                maxEta=(std::max)(maxEta,score.eta); totalEta+=score.eta;
                ids[i]=car.elevator.id;
            }
            if (!std::isfinite(cost)) return;
            // 先寻找可服务数量最多的方案，避免“全不分配=零成本”赢得搜索。
            const PlanKey key{-assigned,cost,maxEta,totalEta,ids};
            if (!foundPlan || key < bestKey)
            {
                foundPlan=true; bestKey=key;
                result.assignedCount=static_cast<std::size_t>(assigned);
                result.totalCost=cost; result.maxEta=maxEta; result.totalEta=totalEta;
                for (std::size_t i=0;i<order.size();++i) result.elevatorIndices[order[i]]=chosen[i];
            }
            return;
        }
        const auto& request=requests[order[depth]];
        const bool valid = std::isfinite(request.firstRequestTime);
        std::vector<std::pair<int,DispatchScore>> candidates;
        if (valid)
        {
            const auto scores = ScoreSnapshots(request.floor, request.direction, local,
                request.firstRequestTime, currentTime);
            for (std::size_t index=0;index<local.size();++index)
            {
                ++result.scoreEvaluations;
                const auto& score=scores[index];
                if (score.feasible) candidates.emplace_back(static_cast<int>(index),score);
            }
        }
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b)
        { return CandidateKey(a.second,local[static_cast<std::size_t>(a.first)],request.floor) <
            CandidateKey(b.second,local[static_cast<std::size_t>(b.first)],request.floor); });
        if (candidates.size() > MaxJointCandidates) candidates.resize(MaxJointCandidates);
        for (const auto& candidate:candidates)
        {
            auto& car=local[static_cast<std::size_t>(candidate.first)];
            auto before=car;
            chosen[depth]=candidate.first;
            directionPenalties[depth]=candidate.second.directionPenalty;
            AddPlannedHallCall(car,request);
            search(depth+1);
            car=std::move(before);
        }
        chosen[depth]=InvalidElevatorId;
        search(depth+1);
    };
    search(0);
    return result;
}
