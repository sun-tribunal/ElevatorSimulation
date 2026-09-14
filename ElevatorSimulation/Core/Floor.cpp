#include "Floor.h"

#include <stdexcept>
#include <algorithm>
#include <unordered_set>

Floor::Floor(int floorNumber) : m_floorNumber(floorNumber)
{
    if (floorNumber < 1)
        throw std::invalid_argument("楼层编号必须为正数");
}

FloorSnapshot Floor::GetSnapshot() const
{
    return { m_floorNumber, GetUpWaitingCount(), GetDownWaitingCount() };
}

bool Floor::Enqueue(PassengerId id, Direction direction)
{
    if (id < 0 || (direction != Direction::Up && direction != Direction::Down)) return false;
    for (const auto* queue : { &m_upWaitingPassengers, &m_downWaitingPassengers })
        if (std::find(queue->begin(), queue->end(), id) != queue->end()) return false;
    (direction == Direction::Up ? m_upWaitingPassengers : m_downWaitingPassengers).push_back(id);
    return true;
}

bool Floor::RemoveFront(PassengerId expectedId, Direction direction)
{
    if (direction != Direction::Up && direction != Direction::Down) return false;
    auto& queue = direction == Direction::Up ? m_upWaitingPassengers : m_downWaitingPassengers;
    if (queue.empty() || queue.front() != expectedId) return false;
    queue.pop_front();
    return true;
}

PassengerId Floor::Peek(Direction direction) const
{
    if (direction != Direction::Up && direction != Direction::Down) return InvalidPassengerId;
    const auto& queue = GetWaitingIds(direction);
    return queue.empty() ? InvalidPassengerId : queue.front();
}

const std::deque<PassengerId>& Floor::GetWaitingIds(Direction direction) const
{
    if (direction == Direction::Up) return m_upWaitingPassengers;
    if (direction == Direction::Down) return m_downWaitingPassengers;
    throw std::invalid_argument("等待队列方向必须为上行或下行");
}

bool Floor::EnqueueBatch(const std::vector<PassengerId>& ids, Direction direction)
{
    if (direction != Direction::Up && direction != Direction::Down) return false;
    // 先校验全部 id，通过后再统一入队，保证整批成功或整批失败，不留下部分状态。
    std::unordered_set<PassengerId> seen;
    seen.reserve(ids.size());
    for (PassengerId id : ids)
    {
        if (id < 0 || Contains(id) || !seen.insert(id).second) return false;
    }
    auto& queue = direction == Direction::Up ? m_upWaitingPassengers : m_downWaitingPassengers;
    queue.insert(queue.end(), ids.begin(), ids.end());
    return true;
}

bool Floor::Contains(PassengerId id) const noexcept
{
    if (id < 0) return false;
    for (const auto* queue : { &m_upWaitingPassengers, &m_downWaitingPassengers })
        if (std::find(queue->begin(), queue->end(), id) != queue->end()) return true;
    return false;
}
