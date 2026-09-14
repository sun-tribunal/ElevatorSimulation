#pragma once

#include "CommonTypes.h"

#include <deque>
#include <vector>

class Floor
{
public:
    explicit Floor(int floorNumber);

    int GetFloorNumber() const noexcept { return m_floorNumber; }
    std::size_t GetUpWaitingCount() const noexcept { return m_upWaitingPassengers.size(); }
    std::size_t GetDownWaitingCount() const noexcept { return m_downWaitingPassengers.size(); }
    FloorSnapshot GetSnapshot() const;

    bool Enqueue(PassengerId id, Direction direction);
    bool RemoveFront(PassengerId expectedId, Direction direction);
    PassengerId Peek(Direction direction) const;
    const std::deque<PassengerId>& GetWaitingIds(Direction direction) const;

    // 批量交通输入：一次性把同一方向的整组乘客按 FIFO 顺序入队，供批量/分组到达场景使用。
    // 任一项 id 非法、批内重复或已在本层等待时整批失败，不做部分入队。
    bool EnqueueBatch(const std::vector<PassengerId>& ids, Direction direction);
    // 查询指定乘客是否正等待在本层任一方向队列中，用于去重与一致性校验。
    bool Contains(PassengerId id) const noexcept;

private:
    int m_floorNumber;
    std::deque<PassengerId> m_upWaitingPassengers;
    std::deque<PassengerId> m_downWaitingPassengers;
};
