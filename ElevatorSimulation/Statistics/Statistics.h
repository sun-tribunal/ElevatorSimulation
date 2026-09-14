#pragma once

#include "../Core/CommonTypes.h"

#include <string>
#include <vector>

// 被 Simulation 组合，仅依赖 Common，不反向依赖 Simulation/UI。
class Statistics
{
public:
    void Reset(int elevatorCount, int floorCount);
    StatisticsSnapshot GetSnapshot() const;

    void PassengerCreated(int floor, Direction direction);
    void PassengerBoarded(int floor, double waitingTime);
    void PassengerArrived(int elevatorId, double rideTime);
    void ElevatorMoved(int elevatorId, bool empty);
    void ElevatorTimeElapsed(int elevatorId, double seconds, ElevatorState state, bool full);

    // 只读文本汇总，供测试与后续 UI 展示；不改变统计状态。
    std::string FormatSummary() const;
    // 单梯满载次数（连续满载时段计数）与工作时间（非空闲累计秒）；越界抛 out_of_range。
    std::size_t GetFullLoadCount(int elevatorId) const;
    double GetWorkingTime(int elevatorId) const;

private:
    StatisticsSnapshot m_snapshot;
    double m_waitingTimeSum = 0.0;
    double m_rideTimeSum = 0.0;
    std::vector<std::size_t> m_fullLoadCounts;
    std::vector<bool> m_wasFull;
    std::vector<double> m_workingTimes;
};
