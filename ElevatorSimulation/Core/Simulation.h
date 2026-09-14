#pragma once

#include "CommonTypes.h"
#include "Dispatcher.h"
#include "Elevator.h"
#include "EventScheduler.h"
#include "FleetRebalancer.h"
#include "Floor.h"
#include "Passenger.h"
#include "../Statistics/Statistics.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <random>

class Simulation
{
public:
    // 成功：替换为全新的 Ready 状态。失败：保留原参数、容器和运行状态。
    bool Initialize(const SimulationConfig& config);
    bool Initialize(const SimulationConfig& config, std::uint32_t seed);
    std::uint32_t GetRandomSeed() const noexcept { return m_seed; }
    const std::string& GetLastError() const noexcept { return m_lastError; }

    void Start();  // 仅 Ready -> Running，不隐式重置已结束的仿真。
    void Pause(); // 仅 Running -> Paused。
    void Resume(); // 仅 Paused -> Running。
    void Reset(); // 以最近一次有效参数重新初始化；未初始化时无操作。

    // 运行时修改仿真倍速：只影响后续 Update 的时间缩放，不重置时钟或重建状态。
    // 合法值必须为有限正数；非法值返回 false 且保持原倍速不变。
    bool SetSimulationSpeed(double speed) noexcept;

    // deltaTime 是真实经过的秒数，内部只乘一次 simulationSpeed。
    // 以事件边界推进整个群组，所有核心事件仅使用仿真秒。
    void Update(double deltaTime);

    bool IsRunning() const noexcept { return m_state == SimulationState::Running; }
    bool IsFinished() const noexcept { return m_state == SimulationState::Finished; }
    SimulationState GetState() const noexcept { return m_state; }
    double GetCurrentTime() const noexcept { return m_currentTime; }
    SimulationConfig GetConfig() const { return m_config; }
    void SetDispatcherExecutionMode(DispatcherExecutionMode mode, std::size_t workerCount = 0)
    { m_dispatcher.SetExecutionMode(mode, workerCount); }
    DispatcherExecutionMode GetDispatcherExecutionMode() const noexcept
    { return m_dispatcher.GetExecutionMode(); }
    std::size_t GetDispatcherWorkerCount() const noexcept { return m_dispatcher.GetWorkerCount(); }

    std::vector<ElevatorSnapshot> GetElevatorSnapshots() const;
    std::vector<FloorSnapshot> GetFloorSnapshots() const;
    std::vector<FloorCoverageSnapshot> GetFloorCoverageSnapshots() const;
    StatisticsSnapshot GetStatisticsSnapshot() const;
    std::vector<PassengerSnapshot> GetPassengerSnapshots() const;
    std::vector<HallCallSnapshot> GetHallCallSnapshots() const;
    DispatchObservationSnapshot GetDispatchObservation(int floor, Direction direction) const;
    SimulationUISnapshot GetUISnapshot(bool workerActive = false,
        bool includeFloorCoverage = true) const;
    // 手工注入便于测试/演示，生成时间为当前仿真时间；失败返回 -1。
    PassengerId AddPassenger(int startFloor, int targetFloor);
    // 在指定楼层按方向批量注入乘客，目的层在该方向的有效楼层中均匀生成。
    // 整批参数无效时不修改仿真；成功时所有乘客使用同一当前仿真时刻。
    bool AddPassengersAtFloor(int startFloor, Direction direction, int count);
    // 只读一致性诊断：所有权、人数守恒、楼层/方向、外呼唯一归属。
    bool ValidateState() const;

private:
    SimulationConfig m_config;
    SimulationState m_state = SimulationState::Uninitialized;
    double m_currentTime = 0.0;
    std::string m_lastError;
    // m_floors 的存储下标不是楼层编号；Floor 自身和所有对外接口使用 1~L。
    std::vector<Floor> m_floors;
    std::vector<Elevator> m_elevators;
    std::unordered_map<PassengerId, Passenger> m_passengers;
    ElevatorDispatcher m_dispatcher;
    FleetRebalancer m_fleetRebalancer;
    Statistics m_statistics;
    struct HallCall
    {
        int assignedElevatorId = InvalidElevatorId;
        double firstRequestTime = 0.0;
        PassengerId firstPassengerId = InvalidPassengerId;
        double lastReassignmentTime = UnsetTime;
    };
    using HallCallKey = std::pair<int, Direction>;
    std::map<HallCallKey, HallCall> m_hallCalls;
    std::uint64_t m_nextPassengerId = 0;
    std::uint32_t m_seed = 0;
    std::mt19937 m_random;
    double m_nextArrivalTime = 0.0;
    EventScheduler m_eventScheduler;
    // 绝对仿真完成时刻；无计时动作时为 infinity。
    std::vector<double> m_elevatorScheduledTimes;
    std::size_t m_trafficPhaseIndex = 0;
    TrafficPattern m_activeTrafficPattern = TrafficPattern::Uniform;
    double m_activePassengerRate = 0.0;
    double m_currentPhaseEnd = 0.0;
    bool m_dispatchDirty = true; // 仅模型事件置脏，帧边界不触发重评估。
    double m_lastReassessmentTime = UnsetTime;
    bool m_rebalanceDirty = true;
    double m_lastFleetRebalanceTime = UnsetTime;

    void GeneratePassengerArrival();
    void HandleTrafficPhaseChange();
    void ScheduleNextPassengerArrival();
    void AdvanceClockTo(double newTime);
    void ScheduleMissingElevatorEvents();
    std::vector<ElevatorDispatchSnapshot> BuildDispatchSnapshots() const;
    HallCallDispatchSnapshot BuildHallCallSnapshot(const HallCallKey& key, const HallCall& call) const;
    bool DispatchCalls();
    void RebalanceIdleFleet();
    void StabilizeCurrentTime();
    void ReleaseHallCall(int floor, Direction direction, int elevatorId);
    void HandleElevatorEvent(int elevatorId, const ElevatorEvent& event);
};
