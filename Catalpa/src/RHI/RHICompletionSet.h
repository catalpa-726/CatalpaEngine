#pragma once

#include "catpch.h"

#include "RHICompletionPoint.h"
#include "RHIError.h"

namespace Catalpa {

	/*
	 * 整批 last-use 集合：同一 device epoch/queue 只保留最大序列。
	 * 不同queue之间没有可比较的全局sequence，不能用最后一条graphics点替代整组要求。
	 */
	class RHICompletionSet {
	public:
		/**
		 * @brief 预留同时跟踪不同设备与队列组合的容量。
		 *
		 * @param[in] queueCount 预期同时跟踪的队列身份数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Reserve(size_t queueCount) noexcept {
			try {
				m_Points.reserve(queueCount);
			} catch (const std::bad_alloc&) {
				return {.code = RHIErrorCode::OutOfMemory, .operation = "RHICompletionSet::Reserve"};
			} catch (const std::length_error&) {
				return {.code = RHIErrorCode::OutOfMemory, .operation = "RHICompletionSet::Reserve"};
			}
			return {};
		}

		/**
		 * @brief 将完成点并入集合，同一设备同一队列只保留最大序列号。
		 *
		 * @param[in] point 本次使用所要求的完成点。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		[[nodiscard]] bool TryInclude(const RHICompletionPoint& point) noexcept {
			if (!point.HasValue())
				return false;
			for (auto& existing : m_Points) {
				if (existing.device == point.device && existing.queue == point.queue) {
					existing.sequence = std::max(existing.sequence, point.sequence);
					return true;
				}
			}
			if (m_Points.size() == m_Points.capacity())
				return false;
			m_Points.push_back(point);
			return true;
		}

		[[nodiscard]] std::span<const RHICompletionPoint> GetPoints() const noexcept { return m_Points; }

		void Clear() noexcept { m_Points.clear(); }

	private:
		/* 提交前预留容量。合并完成点不分配；清空 CPU 记录不代表 GPU 已完成。 */
		std::vector<RHICompletionPoint> m_Points;
	};

}
