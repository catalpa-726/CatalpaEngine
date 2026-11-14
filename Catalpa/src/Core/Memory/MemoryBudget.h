#pragma once
#include "catpch.h"
namespace Catalpa {
	namespace BudgetDetail {
		struct State {
			size_t limit = 0;
			std::atomic_size_t used{0}, peak{0}, rejected{0};
		};
	}
	/*
	 * 内存配额随实际所有者保留，包括仍被持有的旧版资产快照。
	 * 解码结束可归还预留的临时空间，已有预留不能通过 Shrink 增长。
	 */
	class MemoryReservation {
	public:
		MemoryReservation() = default;
		MemoryReservation(MemoryReservation&& other) noexcept { Swap(other); }
		MemoryReservation& operator=(MemoryReservation&& other) noexcept {
			MemoryReservation temporary(std::move(other));
			Swap(temporary);
			return *this;
		}
		MemoryReservation(const MemoryReservation&) = delete;
		MemoryReservation& operator=(const MemoryReservation&) = delete;
		~MemoryReservation() {
			if (m_State)
				m_State->used.fetch_sub(m_Bytes, std::memory_order_acq_rel);
		}
		[[nodiscard]] size_t Bytes() const noexcept { return m_Bytes; }
		/**
		 * @brief 缩小已有预留并归还释放的字节预算。
		 *
		 * @param[in] bytes 调用后保留的预留字节数。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		bool Shrink(size_t bytes) noexcept {
			if (!m_State || bytes > m_Bytes)
				return false;
			m_State->used.fetch_sub(m_Bytes - bytes, std::memory_order_acq_rel);
			m_Bytes = bytes;
			return true;
		}

	private:
		friend class MemoryBudget;
		void Swap(MemoryReservation& other) noexcept {
			m_State.swap(other.m_State);
			std::swap(m_Bytes, other.m_Bytes);
		}
		std::shared_ptr<BudgetDetail::State> m_State;
		size_t m_Bytes = 0;
	};
	struct MemoryBudgetStats {
		size_t limit = 0, used = 0, peak = 0, rejected = 0;
	};
	class MemoryBudget {
	public:
		explicit MemoryBudget(size_t limit) : m_State(std::make_shared<BudgetDetail::State>()) {
			m_State->limit = limit;
		}
		/**
		 * @brief 在上限内原子预留内存并向调用方交付预留句柄。
		 *
		 * @param[in] bytes 需要计入当前内存预算的字节数。
		 * @param[out] output 成功时接收持有本次预算配额的预留对象。
		 *
		 * @return 完成上述操作时为 true，操作未成功时为 false。
		 */
		bool TryReserve(size_t bytes, MemoryReservation& output) noexcept {
			size_t current = m_State->used.load(std::memory_order_acquire);
			for (;;) {
				if (bytes > m_State->limit || current > m_State->limit - bytes) {
					m_State->rejected.fetch_add(1);
					return false;
				}
				if (m_State->used.compare_exchange_weak(current, current + bytes, std::memory_order_acq_rel))
					break;
			}
			size_t peak = m_State->peak.load();
			while (peak < current + bytes && !m_State->peak.compare_exchange_weak(peak, current + bytes)) {
			}
			MemoryReservation reservation;
			reservation.m_State = m_State;
			reservation.m_Bytes = bytes;
			output = std::move(reservation);
			return true;
		}
		[[nodiscard]] MemoryBudgetStats GetStats() const noexcept {
			return {m_State->limit, m_State->used.load(), m_State->peak.load(), m_State->rejected.load()};
		}

	private:
		std::shared_ptr<BudgetDetail::State> m_State;
	};
}
