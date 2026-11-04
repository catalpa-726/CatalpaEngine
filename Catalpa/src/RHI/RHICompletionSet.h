#pragma once

#include "catpch.h"
#include "RHICompletionPoint.h"
#include "RHIError.h"

namespace Catalpa {

    // 整批 last-use 集合：同一 device epoch/queue 只保留最大序列。
    // 当前提交路径只有 graphics queue；集合表示多个身份不意味着已实现多队列调度。
    class RHICompletionSet {
    public:
        // 在接受 GPU 工作前预留不同队列身份的总数；失败保留已有记录。
        [[nodiscard]] RHIStatus Reserve(size_t queueCount) noexcept
        {
            try { m_Points.reserve(queueCount); }
            catch (const std::bad_alloc&) {
                return { .code = RHIErrorCode::OutOfMemory, .operation = "RHICompletionSet::Reserve" };
            }
            catch (const std::length_error&) {
                return { .code = RHIErrorCode::OutOfMemory, .operation = "RHICompletionSet::Reserve" };
            }
            return {};
        }

        // 不分配：新身份需要预留容量；无效点或容量不足返回 false 且不改变集合。
        [[nodiscard]] bool TryInclude(const RHICompletionPoint& point) noexcept
        {
            if (!point.HasValue()) return false;
            for (auto& existing : m_Points) {
                if (existing.device == point.device && existing.queue == point.queue) {
                    existing.sequence = std::max(existing.sequence, point.sequence);
                    return true;
                }
            }
            if (m_Points.size() == m_Points.capacity()) return false;
            m_Points.push_back(point);
            return true;
        }

        // 只提供完成要求，不推测队列已完成；查询者必须向对应设备/队列核实每一点。
        [[nodiscard]] std::span<const RHICompletionPoint> GetPoints() const noexcept { return m_Points; }

        // 清除 CPU 要求但保留预分配容量；此操作不代表 GPU 已完成。
        void Clear() noexcept { m_Points.clear(); }

    private:
        std::vector<RHICompletionPoint> m_Points;
    };

}
