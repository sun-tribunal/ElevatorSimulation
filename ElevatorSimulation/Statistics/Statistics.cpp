#include "Statistics.h"

#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

void Statistics::Reset(int elevatorCount, int floorCount)
{
    if (elevatorCount < 0 || floorCount < 0)
        throw std::invalid_argument("统计维度不能为负数");

    StatisticsSnapshot snapshot;
    snapshot.elevators.reserve(static_cast<std::size_t>(elevatorCount));
    for (int id = 0; id < elevatorCount; ++id)
        snapshot.elevators.push_back({ id, 0, 0, 0 });
    snapshot.floorTraffic.reserve(static_cast<std::size_t>(floorCount));
    for (int floor = 1; floor <= floorCount; ++floor)
        snapshot.floorTraffic.push_back({ floor });
    m_snapshot = std::move(snapshot);
    m_waitingTimeSum = 0.0;
    m_rideTimeSum = 0.0;
    const std::size_t count = static_cast<std::size_t>(elevatorCount);
    m_fullLoadCounts.assign(count, std::size_t{ 0 });
    m_wasFull.assign(count, false);
    m_workingTimes.assign(count, 0.0);
}

StatisticsSnapshot Statistics::GetSnapshot() const
{
    return m_snapshot;
}

void Statistics::PassengerCreated(int floor, Direction direction)
{
    if (direction != Direction::Up && direction != Direction::Down)
        throw std::logic_error("请求方向统计事件无效");
    auto& traffic = m_snapshot.floorTraffic.at(static_cast<std::size_t>(floor - 1));
    if (traffic.floor != floor)
        throw std::logic_error("请求楼层统计事件无效");
    ++m_snapshot.totalPassengerCount;
    ++m_snapshot.waitingCount;
    ++traffic.generatedCount;
    if (direction == Direction::Up)
        ++traffic.upRequestCount;
    else
        ++traffic.downRequestCount;
}

void Statistics::PassengerBoarded(int floor, double waitingTime)
{
    if (m_snapshot.waitingCount == 0 || !std::isfinite(waitingTime) || waitingTime < 0.0)
        throw std::logic_error("登梯统计事件无效");
    auto& traffic = m_snapshot.floorTraffic.at(static_cast<std::size_t>(floor - 1));
    if (traffic.floor != floor || traffic.boardedCount >= traffic.generatedCount)
        throw std::logic_error("登梯楼层统计事件无效");
    --m_snapshot.waitingCount;
    ++m_snapshot.ridingCount;
    ++m_snapshot.boardedCount;
    m_waitingTimeSum += waitingTime;
    m_snapshot.averageWaitingTime = m_waitingTimeSum / m_snapshot.boardedCount;
    m_snapshot.maxWaitingTime = (std::max)(m_snapshot.maxWaitingTime, waitingTime);
    ++traffic.boardedCount;
    traffic.totalWaitingTime += waitingTime;
    traffic.maxWaitingTime = (std::max)(traffic.maxWaitingTime, waitingTime);
}

void Statistics::PassengerArrived(int elevatorId, double rideTime)
{
    if (m_snapshot.ridingCount == 0 || !std::isfinite(rideTime) || rideTime < 0.0)
        throw std::logic_error("到达统计事件无效");
    auto& elevator = m_snapshot.elevators.at(static_cast<std::size_t>(elevatorId));
    --m_snapshot.ridingCount;
    ++m_snapshot.arrivedCount;
    ++elevator.transportedCount;
    m_rideTimeSum += rideTime;
    m_snapshot.averageRideTime = m_rideTimeSum / m_snapshot.arrivedCount;
}

void Statistics::ElevatorMoved(int elevatorId, bool empty)
{
    auto& elevator = m_snapshot.elevators.at(static_cast<std::size_t>(elevatorId));
    ++elevator.traveledFloors;
    if (empty) ++elevator.emptyTravelFloors;
}

void Statistics::ElevatorTimeElapsed(int elevatorId, double seconds, ElevatorState state, bool full)
{
    if (!std::isfinite(seconds) || seconds < 0.0) throw std::invalid_argument("Invalid elapsed time");
    const std::size_t index = static_cast<std::size_t>(elevatorId);
    auto& elevator = m_snapshot.elevators.at(index);
    if (state == ElevatorState::Idle) elevator.idleTime += seconds;
    else m_workingTimes.at(index) += seconds; // 工作时间：非空闲状态累计。
    if (full)
    {
        elevator.fullTime += seconds;
        // 满载次数按连续满载时段计数，避免同一时段逐帧重复累计。
        if (!m_wasFull.at(index))
        {
            ++m_fullLoadCounts.at(index);
            m_wasFull.at(index) = true;
        }
    }
    else
        m_wasFull.at(index) = false;
}

std::size_t Statistics::GetFullLoadCount(int elevatorId) const
{
    return m_fullLoadCounts.at(static_cast<std::size_t>(elevatorId));
}

double Statistics::GetWorkingTime(int elevatorId) const
{
    return m_workingTimes.at(static_cast<std::size_t>(elevatorId));
}

std::string Statistics::FormatSummary() const
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(2);
    output << "总乘客=" << m_snapshot.totalPassengerCount
        << " 等待中=" << m_snapshot.waitingCount
        << " 乘梯中=" << m_snapshot.ridingCount
        << " 已到达=" << m_snapshot.arrivedCount
        << " 平均等待=" << m_snapshot.averageWaitingTime << "s"
        << " 最大等待=" << m_snapshot.maxWaitingTime << "s"
        << " 平均乘梯=" << m_snapshot.averageRideTime << "s\n";
    for (const auto& elevator : m_snapshot.elevators)
    {
        // UI 显示 E1~EN，即 id + 1。
        const std::size_t index = static_cast<std::size_t>(elevator.id);
        output << "E" << elevator.id + 1
            << ": 送达=" << elevator.transportedCount
            << " 移动=" << elevator.traveledFloors
            << " 空驶=" << elevator.emptyTravelFloors
            << " 满载=" << m_fullLoadCounts.at(index) << "次"
            << " 满载时长=" << elevator.fullTime << "s"
            << " 空闲=" << elevator.idleTime << "s"
            << " 工作=" << m_workingTimes.at(index) << "s\n";
    }
    return output.str();
}
