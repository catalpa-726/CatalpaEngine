#include "catpch.h"

#include "ECS/SystemScheduler.h"

#include "ECS/SystemContext.h"
#include "Scene/Scene.h"
#include "Log/Log.h"

namespace Catalpa {
	namespace {

		bool ValidPhase(SystemPhase phase) noexcept {
			return phase >= SystemPhase::Gameplay && phase <= SystemPhase::Extract;
		}

		/**
		 * @brief 比较两个系统的资源访问声明，识别需要排序的读写冲突。
		 * 仅声明同名资源才产生 CPU 数据冲突；读/读不需要排序。
		 *
		 * @param[in] first 首个系统访问声明。
		 * @param[in] second 与其比较的系统访问声明。
		 *
		 * @return 两者访问同一资源且至少一方写入时为 true。
		 */
		bool Conflicts(const SystemDesc& first, const SystemDesc& second) noexcept {
			for (const auto& left : first.accesses)
				for (const auto& right : second.accesses)
					if (left.resource == right.resource &&
					    (left.mode == SystemAccessMode::Write || right.mode == SystemAccessMode::Write))
						return true;
			return false;
		}

	}

	SystemScheduler::SystemScheduler() : m_OwnerThread(std::this_thread::get_id()) {}

	SystemScheduler::~SystemScheduler() noexcept {
		if (m_Running) {
			if (Log::GetCoreLogger())
				CAT_CORE_CRITICAL("Cannot destroy a running SystemScheduler");
			std::abort();
		}
	}

	WorldStatus SystemScheduler::Fail(
	    WorldError code, const char* message, std::string_view first, std::string_view second) noexcept {
		if (!message)
			message = "System operation returned no diagnostic";
		/* 诊断内存不足不改变原错误；status 的 message 始终来自静态/调用方契约。 */
		try {
			std::string diagnostic(message);
			if (!first.empty()) {
				diagnostic += ": ";
				diagnostic += first;
			}
			if (!second.empty()) {
				diagnostic += " / ";
				diagnostic += second;
			}
			m_LastDiagnostic.swap(diagnostic);
		} catch (...) {
			m_LastDiagnostic.clear();
		}
		return {code, message};
	}

	WorldStatus SystemScheduler::CanMutate() noexcept {
		if (m_OwnerThread != std::this_thread::get_id())
			return {WorldError::Busy, "SystemScheduler belongs to another thread"};
		if (m_Running)
			return Fail(WorldError::Busy, "SystemScheduler is executing a phase");
		return {};
	}

	bool SystemScheduler::HasSystem(std::string_view id) const noexcept {
		if (m_OwnerThread != std::this_thread::get_id())
			return false;
		return std::any_of(m_Systems.begin(), m_Systems.end(), [id](const auto& system) { return system.id == id; });
	}

	WorldStatus SystemScheduler::Add(SystemDesc system) {
		if (const WorldStatus status = CanMutate(); !status.IsSuccess())
			return status;
		if (system.id.empty() || !system.execute || !ValidPhase(system.phase))
			return Fail(WorldError::InvalidArgument, "System requires a nonempty identity, valid phase and callback",
			    system.id);
		if (HasSystem(system.id))
			return Fail(WorldError::AlreadyExists, "System identity is already registered", system.id);
		for (std::size_t i = 0; i < system.accesses.size(); ++i) {
			const auto& access = system.accesses[i];
			if (access.resource.empty() ||
			    (access.mode != SystemAccessMode::Read && access.mode != SystemAccessMode::Write))
				return Fail(
				    WorldError::InvalidArgument, "System has an invalid resource access declaration", system.id);
			for (std::size_t j = 0; j < i; ++j)
				if (system.accesses[j].resource == access.resource)
					return Fail(WorldError::InvalidArgument, "System declares the same resource more than once",
					    system.id, access.resource);
		}
		for (const auto& dependency : system.after)
			if (dependency.empty() || dependency == system.id)
				return Fail(WorldError::InvalidArgument, "System has an empty or self dependency", system.id);
		try {
			m_Systems.push_back(std::move(system));
		} catch (const std::bad_alloc&) {
			return Fail(WorldError::AllocationFailed, "Cannot allocate a system registration");
		} catch (const std::length_error&) {
			return Fail(WorldError::CapacityExceeded, "System registration count exceeds container capacity");
		}
		m_Compiled = false;
		m_LastDiagnostic.clear();
		return {};
	}

	WorldStatus SystemScheduler::Remove(std::string_view id) {
		if (const WorldStatus status = CanMutate(); !status.IsSuccess())
			return status;
		const auto found =
		    std::find_if(m_Systems.begin(), m_Systems.end(), [id](const auto& system) { return system.id == id; });
		if (found == m_Systems.end())
			return Fail(WorldError::InvalidArgument, "System identity is not registered", id);
		for (const auto& system : m_Systems)
			if (std::any_of(system.after.begin(), system.after.end(),
			        [id](const auto& dependency) { return dependency == id; }))
				return Fail(WorldError::Busy, "Another system still depends on this registration", id, system.id);
		m_Systems.erase(found);
		m_Compiled = false;
		m_LastDiagnostic.clear();
		return {};
	}

	WorldStatus SystemScheduler::Clear() noexcept {
		if (const WorldStatus status = CanMutate(); !status.IsSuccess())
			return status;
		m_Systems.clear();
		m_Order.clear();
		m_LastDiagnostic.clear();
		m_Compiled = false;
		return {};
	}

	WorldStatus SystemScheduler::Compile() {
		if (const WorldStatus status = CanMutate(); !status.IsSuccess())
			return status;
		m_Compiled = false;
		try {
			const std::size_t count = m_Systems.size();
			std::vector<std::vector<bool>> edges(count, std::vector<bool>(count, false));
			for (std::size_t i = 0; i < count; ++i) {
				for (std::size_t j = 0; j < count; ++j)
					if (m_Systems[i].phase < m_Systems[j].phase)
						edges[i][j] = true;
				for (const auto& after : m_Systems[i].after) {
					const auto found = std::find_if(m_Systems.begin(), m_Systems.end(),
					    [&after](const auto& system) { return system.id == after; });
					if (found == m_Systems.end())
						return Fail(
						    WorldError::InvalidArgument, "System dependency is not registered", m_Systems[i].id, after);
					const std::size_t dependency = static_cast<std::size_t>(found - m_Systems.begin());
					if (m_Systems[dependency].phase > m_Systems[i].phase)
						return Fail(WorldError::InvalidArgument, "System dependency reverses the declared phase order",
						    m_Systems[i].id, after);
					edges[dependency][i] = true;
				}
			}

			/* 计算依赖的传递闭包，借此发现环和未明确排序的同阶段读写冲突。 */
			auto reachable = edges;
			for (std::size_t via = 0; via < count; ++via)
				for (std::size_t from = 0; from < count; ++from)
					if (reachable[from][via])
						for (std::size_t to = 0; to < count; ++to)
							if (reachable[via][to])
								reachable[from][to] = true;
			for (std::size_t i = 0; i < count; ++i) {
				if (reachable[i][i])
					return Fail(
					    WorldError::InvalidArgument, "System dependency graph contains a cycle", m_Systems[i].id);
				for (std::size_t j = i + 1; j < count; ++j)
					if (m_Systems[i].phase == m_Systems[j].phase && Conflicts(m_Systems[i], m_Systems[j]) &&
					    !reachable[i][j] && !reachable[j][i])
						return Fail(WorldError::InvalidArgument,
						    "Conflicting system accesses need an explicit dependency", m_Systems[i].id,
						    m_Systems[j].id);
			}

			/* 拓扑排序只从入度为零的系统中选择，得到可执行的顺序后再发布。 */
			std::vector<std::size_t> indegree(count, 0), order;
			std::vector<bool> emitted(count, false);
			order.reserve(count);
			for (std::size_t from = 0; from < count; ++from)
				for (std::size_t to = 0; to < count; ++to)
					if (edges[from][to])
						++indegree[to];
			while (order.size() != count) {
				std::size_t next = count;
				for (std::size_t i = 0; i < count; ++i)
					if (!emitted[i] && indegree[i] == 0) {
						next = i;
						break;
					}
				if (next == count)
					return Fail(WorldError::InvalidArgument, "System dependency graph contains a cycle");
				emitted[next] = true;
				order.push_back(next);
				for (std::size_t to = 0; to < count; ++to)
					if (edges[next][to])
						--indegree[to];
			}
			m_Order.swap(order);
			m_Compiled = true;
			m_LastDiagnostic.clear();
			return {};
		} catch (const std::bad_alloc&) {
			return Fail(WorldError::AllocationFailed, "Cannot allocate a system dependency graph");
		} catch (const std::length_error&) {
			return Fail(WorldError::CapacityExceeded, "System dependency graph exceeds container capacity");
		}
	}

	WorldStatus SystemScheduler::RunPhase(SystemPhase phase, SystemContext& context) {
		if (const WorldStatus status = CanMutate(); !status.IsSuccess())
			return status;
		if (!ValidPhase(phase))
			return Fail(WorldError::InvalidArgument, "Requested system phase is invalid");
		if (context.scene.GetWorld().IsAccessActive())
			return Fail(WorldError::Busy, "Cannot run systems while the World is borrowed");
		if (!m_Compiled)
			if (const WorldStatus status = Compile(); !status.IsSuccess())
				return status;
		m_Running = true;
		struct ResetFlag {
			bool& running;

			~ResetFlag() { running = false; }
		} reset{m_Running};
		for (const std::size_t index : m_Order) {
			const auto& system = m_Systems[index];
			if (system.phase != phase)
				continue;
			try {
				if (const WorldStatus status = system.execute(context); !status.IsSuccess())
					return Fail(status.code, status.message, system.id);
			} catch (const std::bad_alloc&) {
				return Fail(WorldError::AllocationFailed, "Scheduled system allocation failed", system.id);
			} catch (const std::exception& error) {
				return Fail(
				    WorldError::CommandFailed, "Scheduled system callback threw an exception", system.id, error.what());
			} catch (...) {
				return Fail(WorldError::CommandFailed, "Scheduled system callback threw an exception", system.id);
			}
		}
		m_LastDiagnostic.clear();
		return {};
	}

}
