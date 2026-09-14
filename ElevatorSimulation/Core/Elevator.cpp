#include "Elevator.h"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <limits>

Elevator::Elevator(int id, int initialFloor, int capacity)
    : m_id(id), m_currentFloor(initialFloor), m_capacity(capacity)
{
    if (id < 0 || initialFloor < 1 || capacity <= 0)
        throw std::invalid_argument("电梯数据无效");
}

Elevator::Elevator(int id, int initialFloor, const SimulationConfig& config)
    : Elevator(id, initialFloor, config.capacity)
{
    if (config.floorCount < 2 || initialFloor > config.floorCount ||
        !std::isfinite(config.moveTimePerFloor) || config.moveTimePerFloor <= 0.0 ||
        !std::isfinite(config.personTime) || config.personTime <= 0.0)
        throw std::invalid_argument("电梯配置无效");
    m_floorCount = config.floorCount;
    m_moveTimePerFloor = config.moveTimePerFloor;
    m_personTime = config.personTime;
}

ElevatorSnapshot Elevator::GetSnapshot() const
{
    return { m_id, m_currentFloor, m_direction, m_state,
        static_cast<int>(m_passengerIds.size()), m_capacity, m_repositionTargetFloor };
}

ElevatorDispatchSnapshot Elevator::GetDispatchSnapshot() const
{
    ElevatorDispatchSnapshot snapshot;
    snapshot.elevator = GetSnapshot();
    snapshot.floorCount = m_floorCount;
    snapshot.moveTimePerFloor = m_moveTimePerFloor;
    snapshot.personTime = m_personTime;
    snapshot.remainingActionTime = m_actionRemaining;
    snapshot.betweenFloors = m_state == ElevatorState::MovingUp || m_state == ElevatorState::MovingDown;
    snapshot.reservedBoardingCount = m_state == ElevatorState::Boarding ? 1 : 0;
    snapshot.upTasks.assign(m_upTasks.begin(), m_upTasks.end());
    snapshot.downTasks.assign(m_downTasks.begin(), m_downTasks.end());
    // 仅导出已经 Boarded 后可见的内呼集合；同层多人不暴露人数。
    std::set<int> carFloors(m_carCalls.begin(), m_carCalls.end());
    for (const auto& destination : m_destinations)
        carFloors.insert(destination.second);
    for (int floor : carFloors)
        snapshot.stopServices.push_back({ floor, Direction::Idle });
    for (int floor : m_upHallCalls)
        snapshot.stopServices.push_back({ floor, Direction::Up });
    for (int floor : m_downHallCalls)
        snapshot.stopServices.push_back({ floor, Direction::Down });
    return snapshot;
}

bool Elevator::IsValidFloor(int floor) const noexcept
{
    return floor >= 1 && (m_floorCount == 0 || floor <= m_floorCount);
}

bool Elevator::HasHallCall(int floor, Direction direction) const
{
    if (direction == Direction::Up) return m_upHallCalls.count(floor) != 0;
    if (direction == Direction::Down) return m_downHallCalls.count(floor) != 0;
    return false;
}

bool Elevator::AddHallCall(int floor, Direction direction)
{
    if (!IsValidFloor(floor) || (direction != Direction::Up && direction != Direction::Down) ||
        (floor == 1 && direction == Direction::Down) ||
        (m_floorCount > 0 && floor == m_floorCount && direction == Direction::Up))
        return false;
    ClearRepositionTarget();
    (direction == Direction::Up ? m_upHallCalls : m_downHallCalls).insert(floor);
    RebuildTasks();
    if (m_state == ElevatorState::Idle)
    {
        m_direction = floor == m_currentFloor ? direction : GetDirection(m_currentFloor, floor);
        ChooseActionAtFloor();
    }
    return true;
}

bool Elevator::RemoveHallCall(int floor, Direction direction)
{
    const bool serving = floor == m_currentFloor && (m_state == ElevatorState::Stopped ||
        m_state == ElevatorState::Boarding || m_state == ElevatorState::Alighting);
    if (!HasHallCall(floor, direction) || serving) return false;
    (direction == Direction::Up ? m_upHallCalls : m_downHallCalls).erase(floor);
    RebuildTasks();
    return true;
}

bool Elevator::AddInternalTarget(int floor)
{
    if (!IsValidFloor(floor) || floor == m_currentFloor) return false;
    ClearRepositionTarget();
    m_carCalls.insert(floor);
    RebuildTasks();
    if (m_state == ElevatorState::Idle)
    {
        m_direction = GetDirection(m_currentFloor, floor);
        ChooseActionAtFloor();
    }
    return true;
}

bool Elevator::SetRepositionTarget(int floor)
{
    if (!IsValidFloor(floor) || floor == m_currentFloor || m_state != ElevatorState::Idle ||
        !m_passengerIds.empty() || !m_carCalls.empty() ||
        !m_upHallCalls.empty() || !m_downHallCalls.empty())
        return false;
    m_repositionTargetFloor = floor;
    m_direction = GetDirection(m_currentFloor, floor);
    ChooseActionAtFloor();
    return true;
}

bool Elevator::CanBoard() const noexcept
{
    const auto occupied = m_passengerIds.size() + (m_state == ElevatorState::Boarding ? 1u : 0u);
    return occupied < static_cast<std::size_t>(m_capacity);
}

PassengerId Elevator::GetNextAlightingPassenger() const
{
    for (PassengerId id : m_passengerIds)
        if (m_destinations.at(id) == m_currentFloor) return id;
    return InvalidPassengerId;
}

bool Elevator::BeginBoarding(PassengerId id, int targetFloor)
{
    if (!IsAtStop() || !CanBoard() || id < 0 || m_destinations.count(id) != 0 ||
        !IsValidFloor(targetFloor) || targetFloor == m_currentFloor ||
        GetDirection(m_currentFloor, targetFloor) != m_direction ||
        GetNextAlightingPassenger() != InvalidPassengerId)
        return false;
    m_pendingPassengerId = id;
    m_pendingTarget = targetFloor;
    m_state = ElevatorState::Boarding;
    m_actionRemaining = m_personTime;
    return true;
}

bool Elevator::BeginAlighting(PassengerId id)
{
    const auto passenger = m_destinations.find(id);
    if (!IsAtStop() || passenger == m_destinations.end() || passenger->second != m_currentFloor)
        return false;
    m_pendingPassengerId = id;
    m_state = ElevatorState::Alighting;
    m_actionRemaining = m_personTime;
    return true;
}

bool Elevator::FinishStop()
{
    if (!IsAtStop() || GetNextAlightingPassenger() != InvalidPassengerId) return false;
    (m_direction == Direction::Up ? m_upHallCalls : m_downHallCalls).erase(m_currentFloor);
    m_carCalls.erase(m_currentFloor);
    RebuildTasks();
    ChooseActionAtFloor();
    return true;
}

void Elevator::RebuildTasks()
{
    m_upTasks = m_upHallCalls;
    m_downTasks = m_downHallCalls;
    for (int floor : m_carCalls)
    {
        if (floor > m_currentFloor) m_upTasks.insert(floor);
        else if (floor < m_currentFloor) m_downTasks.insert(floor);
    }
}

bool Elevator::HasTasksAhead(Direction direction) const
{
    for (const auto* tasks : { &m_upHallCalls, &m_downHallCalls, &m_carCalls })
        for (int floor : *tasks)
            if (direction == Direction::Up ? floor > m_currentFloor : floor < m_currentFloor)
                return true;
    return false;
}

void Elevator::ChooseActionAtFloor()
{
    // 仅在整层到达、空闲接受首任务或本次上下客完成时调用。
    // 中途新增请求只登记集合，不能在移动区间改变 m_direction。
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (m_carCalls.count(m_currentFloor) != 0 || HasHallCall(m_currentFloor, m_direction))
        {
            m_state = ElevatorState::Stopped;
            m_actionRemaining = 0.0;
            m_carCalls.erase(m_currentFloor);
            RebuildTasks();
            return;
        }
        if (HasTasksAhead(m_direction))
        {
            m_state = m_direction == Direction::Up ? ElevatorState::MovingUp : ElevatorState::MovingDown;
            m_actionRemaining = m_moveTimePerFloor;
            return;
        }
        m_direction = m_direction == Direction::Up ? Direction::Down : Direction::Up;
    }
    if (m_repositionTargetFloor != InvalidFloor)
    {
        if (m_currentFloor == m_repositionTargetFloor)
            m_repositionTargetFloor = InvalidFloor;
        else
        {
            m_direction = GetDirection(m_currentFloor, m_repositionTargetFloor);
            m_state = m_direction == Direction::Up ?
                ElevatorState::MovingUp : ElevatorState::MovingDown;
            m_actionRemaining = m_moveTimePerFloor;
            return;
        }
    }
    m_direction = Direction::Idle;
    m_state = ElevatorState::Idle;
    m_actionRemaining = 0.0;
}

double Elevator::GetTimeToNextEvent() const noexcept
{
    return m_state == ElevatorState::Idle || IsAtStop() ?
        std::numeric_limits<double>::infinity() : m_actionRemaining;
}

ElevatorEvent Elevator::Advance(double simulationSeconds)
{
    ElevatorEvent event;
    if (!std::isfinite(simulationSeconds) || simulationSeconds <= 0.0) return event;
    event.elapsedTime = simulationSeconds;
    if (m_state == ElevatorState::Idle || IsAtStop()) return event;
    if (simulationSeconds < m_actionRemaining)
    {
        m_actionRemaining -= simulationSeconds;
        return event;
    }
    event.elapsedTime = m_actionRemaining;
    m_actionRemaining = 0.0;
    if (m_state == ElevatorState::MovingUp || m_state == ElevatorState::MovingDown)
    {
        event.type = ElevatorEventType::FloorReached;
        event.emptyMovement = m_passengerIds.empty();
        m_currentFloor += m_direction == Direction::Up ? 1 : -1;
        RebuildTasks();
        ChooseActionAtFloor();
    }
    else
    {
        event.passengerId = m_pendingPassengerId;
        if (m_state == ElevatorState::Boarding)
        {
            event.type = ElevatorEventType::Boarded;
            m_passengerIds.push_back(m_pendingPassengerId);
            m_destinations.emplace(m_pendingPassengerId, m_pendingTarget);
            m_carCalls.insert(m_pendingTarget);
        }
        else
        {
            event.type = ElevatorEventType::Alighted;
            m_destinations.erase(m_pendingPassengerId);
            m_passengerIds.erase(std::find(m_passengerIds.begin(), m_passengerIds.end(), m_pendingPassengerId));
        }
        m_pendingPassengerId = InvalidPassengerId;
        m_pendingTarget = 0;
        m_state = ElevatorState::Stopped;
        RebuildTasks();
    }
    return event;
}
