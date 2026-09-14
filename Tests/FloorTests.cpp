#include "Core/Floor.h"
#include "TestSupport.h"

#include <vector>

// A 部分（Floor）批量交通输入接口回归：EnqueueBatch 原子整组入队与 Contains 去重/一致性校验。
int main()
{
    TestSuite tests("Floor");
    tests.Run("initial state empty", [&] {
        Floor floor(1);
        tests.Check(floor.GetUpWaitingCount() == 0 && floor.GetDownWaitingCount() == 0, "empty queues");
        tests.Check(!floor.Contains(0) && !floor.Contains(-1), "contains empty");
    });
    tests.Run("batch enqueue FIFO order", [&] {
        Floor floor(3);
        tests.Check(floor.EnqueueBatch({ 0, 1, 2 }, Direction::Up), "batch accepted");
        tests.Check(floor.GetUpWaitingCount() == 3 && floor.GetDownWaitingCount() == 0, "up count");
        const auto& up = floor.GetWaitingIds(Direction::Up);
        tests.Check(up.size() == 3 && up[0] == 0 && up[1] == 1 && up[2] == 2, "order preserved");
        tests.Check(floor.Peek(Direction::Up) == 0, "peek head");
        tests.Check(floor.Peek(Direction::Down) == InvalidPassengerId, "empty down peek");
    });
    tests.Run("directions are independent queues", [&] {
        Floor floor(3);
        tests.Check(floor.EnqueueBatch({ 0, 1 }, Direction::Up), "up batch");
        tests.Check(floor.EnqueueBatch({ 2, 3 }, Direction::Down), "down batch");
        tests.Check(floor.GetUpWaitingCount() == 2 && floor.GetDownWaitingCount() == 2, "split counts");
        tests.Check(floor.GetWaitingIds(Direction::Up)[0] == 0, "up queue head");
        tests.Check(floor.GetWaitingIds(Direction::Down)[0] == 2, "down queue head");
    });
    tests.Run("batch duplicate rejected atomically", [&] {
        Floor floor(1);
        tests.Check(!floor.EnqueueBatch({ 4, 4 }, Direction::Up), "in-batch duplicate");
        tests.Check(floor.GetUpWaitingCount() == 0, "no partial enqueue");
    });
    tests.Run("already waiting id rejects batch", [&] {
        Floor floor(1);
        tests.Check(floor.Enqueue(5, Direction::Up), "seed single");
        tests.Check(!floor.EnqueueBatch({ 6, 5 }, Direction::Up), "waiting id rejects");
        tests.Check(floor.GetUpWaitingCount() == 1, "no partial enqueue");
        // 跨方向去重口径与单条 Enqueue 一致：同一 id 不能同时出现在上下行。
        tests.Check(!floor.EnqueueBatch({ 5 }, Direction::Down), "cross-direction duplicate");
        tests.Check(floor.GetDownWaitingCount() == 0, "down still empty");
    });
    tests.Run("invalid parameters rejected", [&] {
        Floor floor(1);
        tests.Check(!floor.EnqueueBatch({ 7, -1 }, Direction::Up), "negative id");
        tests.Check(!floor.EnqueueBatch({ 7 }, Direction::Idle), "idle direction");
        tests.Check(floor.GetUpWaitingCount() == 0, "no partial enqueue");
    });
    tests.Run("empty batch is harmless no-op", [&] {
        Floor floor(1);
        tests.Check(floor.EnqueueBatch({}, Direction::Up), "empty accepted");
        tests.Check(floor.GetUpWaitingCount() == 0, "unchanged");
    });
    tests.Run("append preserves existing FIFO", [&] {
        Floor floor(1);
        tests.Check(floor.EnqueueBatch({ 0, 1 }, Direction::Up), "first batch");
        tests.Check(floor.EnqueueBatch({ 2, 3 }, Direction::Up), "second batch");
        const auto& up = floor.GetWaitingIds(Direction::Up);
        tests.Check(up.size() == 4 && up[0] == 0 && up[1] == 1 && up[2] == 2 && up[3] == 3, "global order");
    });
    tests.Run("contains tracks removal", [&] {
        Floor floor(1);
        tests.Check(floor.EnqueueBatch({ 0, 1, 2 }, Direction::Up), "seed batch");
        tests.Check(floor.Contains(0) && floor.Contains(1) && floor.Contains(2), "all contained");
        tests.Check(!floor.Contains(3) && !floor.Contains(-1), "absent ids");
        tests.Check(floor.RemoveFront(0, Direction::Up), "remove head");
        tests.Check(!floor.Contains(0), "removed id gone");
        tests.Check(floor.Contains(1), "remaining kept");
    });
    tests.Run("snapshot reflects batch counts", [&] {
        Floor floor(3);
        tests.Check(floor.EnqueueBatch({ 0, 1 }, Direction::Up) && floor.EnqueueBatch({ 2 }, Direction::Down), "seed batches");
        const auto snapshot = floor.GetSnapshot();
        tests.Check(snapshot.floorNumber == 3 && snapshot.upWaitingCount == 2 && snapshot.downWaitingCount == 1,
            "snapshot counts");
    });
    return tests.Finish();
}
