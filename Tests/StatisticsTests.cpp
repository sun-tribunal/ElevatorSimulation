#include "Statistics/Statistics.h"
#include "TestSupport.h"

#include <limits>
#include <stdexcept>
#include <string>

// F 部分（Statistics）单元回归：事件计数、统计口径、非法事件与零样本边界。
// 合并 upstream 楼层客流统计后，Created/Boarded 事件携带楼层与方向，Reset 携带楼层数。
int main()
{
    TestSuite tests("Statistics");
    tests.Run("reset creates per-elevator slots", [&] {
        Statistics statistics; statistics.Reset(3, 10);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.elevators.size() == 3, "three slots");
        tests.Check(snapshot.elevators[0].id == 0 && snapshot.elevators[2].id == 2, "ids 0..N-1");
        tests.Check(snapshot.totalPassengerCount == 0 && snapshot.arrivedCount == 0 &&
            snapshot.boardedCount == 0, "empty counts");
    });
    tests.Run("reset zero elevators allowed", [&] {
        Statistics statistics; statistics.Reset(0, 0);
        tests.Check(statistics.GetSnapshot().elevators.empty(), "no slots");
    });
    tests.Run("negative elevator count rejected", [&] {
        Statistics statistics; bool rejected = false;
        try { statistics.Reset(-1, 0); }
        catch (const std::invalid_argument&) { rejected = true; }
        tests.Check(rejected, "invalid argument");
    });
    tests.Run("created increments total and waiting", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.PassengerCreated(1, Direction::Up);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.totalPassengerCount == 1 && snapshot.waitingCount == 1, "created counts");
    });
    tests.Run("boarded moves waiting to riding with T-inclusive mean", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.PassengerCreated(1, Direction::Up);
        statistics.PassengerBoarded(1, 5.0);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.waitingCount == 0 && snapshot.ridingCount == 1 && snapshot.boardedCount == 1,
            "transferred");
        tests.Near(snapshot.averageWaitingTime, 5.0, "mean includes T");
        tests.Near(snapshot.maxWaitingTime, 5.0, "max updated");
    });
    tests.Run("boarded updates running mean and max", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.PassengerCreated(1, Direction::Up); statistics.PassengerBoarded(1, 2.0);
        statistics.PassengerCreated(1, Direction::Up); statistics.PassengerBoarded(1, 8.0);
        const auto snapshot = statistics.GetSnapshot();
        tests.Near(snapshot.averageWaitingTime, 5.0, "running mean");
        tests.Near(snapshot.maxWaitingTime, 8.0, "running max");
    });
    tests.Run("invalid boarded events rejected", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        bool rejected = false;
        try { statistics.PassengerBoarded(1, 1.0); }
        catch (const std::logic_error&) { rejected = true; }
        tests.Check(rejected, "no waiting passenger");
        statistics.PassengerCreated(1, Direction::Up);
        for (double value : { -1.0, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity() })
        {
            bool invalid = false;
            try { statistics.PassengerBoarded(1, value); }
            catch (const std::logic_error&) { invalid = true; }
            tests.Check(invalid, "invalid time rejected");
        }
    });
    tests.Run("arrived moves riding to arrived with per-elevator count", [&] {
        Statistics statistics; statistics.Reset(2, 10);
        statistics.PassengerCreated(1, Direction::Up); statistics.PassengerBoarded(1, 3.0);
        statistics.PassengerArrived(1, 7.0);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.ridingCount == 0 && snapshot.arrivedCount == 1, "arrived");
        tests.Check(snapshot.elevators[1].transportedCount == 1 && snapshot.elevators[0].transportedCount == 0,
            "per elevator");
        tests.Near(snapshot.averageRideTime, 7.0, "mean ride includes T");
    });
    tests.Run("invalid arrived events rejected", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        bool rejected = false;
        try { statistics.PassengerArrived(0, 1.0); }
        catch (const std::logic_error&) { rejected = true; }
        tests.Check(rejected, "no riding passenger");
        statistics.PassengerCreated(1, Direction::Up); statistics.PassengerBoarded(1, 1.0);
        for (double value : { -1.0, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity() })
        {
            bool invalid = false;
            try { statistics.PassengerArrived(0, value); }
            catch (const std::logic_error&) { invalid = true; }
            tests.Check(invalid, "invalid ride time rejected");
        }
        bool missing = false;
        try { statistics.PassengerArrived(5, 1.0); }
        catch (const std::out_of_range&) { missing = true; }
        tests.Check(missing, "invalid elevator id rejected");
    });
    tests.Run("moved accumulates traveled and empty floors", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.ElevatorMoved(0, false);
        statistics.ElevatorMoved(0, true);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.elevators[0].traveledFloors == 2, "traveled");
        tests.Check(snapshot.elevators[0].emptyTravelFloors == 1, "empty");
    });
    tests.Run("elapsed accumulates idle and full time", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.ElevatorTimeElapsed(0, 3.0, ElevatorState::Idle, false);
        statistics.ElevatorTimeElapsed(0, 2.0, ElevatorState::MovingUp, true);
        const auto snapshot = statistics.GetSnapshot();
        tests.Near(snapshot.elevators[0].idleTime, 3.0, "idle");
        tests.Near(snapshot.elevators[0].fullTime, 2.0, "full");
        bool invalid = false;
        try { statistics.ElevatorTimeElapsed(0, -1.0, ElevatorState::Idle, false); }
        catch (const std::invalid_argument&) { invalid = true; }
        tests.Check(invalid, "negative seconds rejected");
    });
    tests.Run("invariants total equals waiting plus riding plus arrived", [&] {
        Statistics statistics; statistics.Reset(2, 10);
        for (int i = 0; i < 3; ++i) statistics.PassengerCreated(1, Direction::Up);
        statistics.PassengerBoarded(1, 1.0);
        statistics.PassengerBoarded(1, 2.0);
        statistics.PassengerArrived(0, 3.0);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.totalPassengerCount == 3 && snapshot.waitingCount == 1 &&
            snapshot.ridingCount == 1 && snapshot.arrivedCount == 1, "split counts");
        tests.Check(snapshot.totalPassengerCount == snapshot.waitingCount + snapshot.ridingCount + snapshot.arrivedCount,
            "conservation");
        tests.Check(snapshot.boardedCount == snapshot.ridingCount + snapshot.arrivedCount, "boarded identity");
    });
    tests.Run("zero-sample averages stay zero", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        const auto snapshot = statistics.GetSnapshot();
        tests.Check(snapshot.averageWaitingTime == 0.0 && snapshot.averageRideTime == 0.0 &&
            snapshot.maxWaitingTime == 0.0, "no NaN before samples");
    });
    tests.Run("snapshot copy isolation", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        auto snapshot = statistics.GetSnapshot();
        snapshot.totalPassengerCount = 999; snapshot.elevators[0].transportedCount = 999;
        const auto after = statistics.GetSnapshot();
        tests.Check(after.totalPassengerCount == 0 && after.elevators[0].transportedCount == 0, "copy isolated");
    });
    tests.Run("elapsed tracks working time and full episodes", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        statistics.ElevatorTimeElapsed(0, 2.0, ElevatorState::MovingUp, false);
        statistics.ElevatorTimeElapsed(0, 3.0, ElevatorState::MovingUp, true);
        statistics.ElevatorTimeElapsed(0, 1.0, ElevatorState::MovingUp, true);
        statistics.ElevatorTimeElapsed(0, 4.0, ElevatorState::Idle, false);
        tests.Near(statistics.GetWorkingTime(0), 6.0, "working time excludes idle");
        tests.Check(statistics.GetFullLoadCount(0) == 1, "one contiguous full episode");
        tests.Near(statistics.GetSnapshot().elevators[0].fullTime, 4.0, "full seconds");
        statistics.ElevatorTimeElapsed(0, 1.0, ElevatorState::MovingUp, false);
        statistics.ElevatorTimeElapsed(0, 1.0, ElevatorState::MovingUp, true);
        tests.Check(statistics.GetFullLoadCount(0) == 2, "second full episode counted");
        tests.Near(statistics.GetWorkingTime(0), 8.0, "working time keeps accumulating");
    });
    tests.Run("full load getters bounds check", [&] {
        Statistics statistics; statistics.Reset(1, 10);
        bool missing = false;
        try { statistics.GetFullLoadCount(5); }
        catch (const std::out_of_range&) { missing = true; }
        tests.Check(missing, "invalid elevator id rejected");
    });
    tests.Run("format summary contains key fields", [&] {
        Statistics statistics; statistics.Reset(2, 10);
        statistics.PassengerCreated(1, Direction::Up); statistics.PassengerBoarded(1, 4.0);
        const std::string summary = statistics.FormatSummary();
        tests.Check(summary.find("总乘客=1") != std::string::npos, "total");
        tests.Check(summary.find("平均等待=4.00s") != std::string::npos, "mean wait");
        tests.Check(summary.find("E1:") != std::string::npos && summary.find("E2:") != std::string::npos,
            "per elevator lines");
        tests.Check(summary.find("送达=") != std::string::npos, "transported field");
    });
    return tests.Finish();
}
