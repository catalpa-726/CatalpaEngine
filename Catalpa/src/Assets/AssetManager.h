#pragma once
#include "catpch.h"

#include "Core/Base.h"
#include "Core/Jobs/JobSystem.h"
#include "Core/Memory/MemoryBudget.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Assets/AssetRegistry.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetStatus.h"
#include "Assets/AssetChangeEvent.h"
namespace Catalpa {
	class PackageReader;
	struct AssetManagerConfig {
		/*
		 * 仅统计载荷容量和受限的解码工作区，不含分配器或控制块开销。
		 * 该数值表示引擎管理的 CPU 资产内存，不代表整个进程或 GPU 的内存占用。
		 */
		size_t maxFileBytes = 16 * 1024 * 1024;
		size_t maxResidentBytes = 32 * 1024 * 1024;
		size_t memoryBudgetBytes = 512 * 1024 * 1024;
		size_t maxPending = 32;
		/* 在有限的历史记录中检查已发布的资产身份与内容版本，防止该组合指向不同字节。 */
		size_t maxKnownVersions = 4096;
	};
	struct AssetSnapshot {
		AssetId id;
		uint64_t version = 0;
		AssetLoadResult asset;
		bool fallback = false;
	};
	/*
	 * 管理器由主线程持有，工作线程仅接收值快照。
	 * 重新加载、卸载或关闭后，仍存活的 CPU 快照继续持有原版本和对应内存配额。
	 * 关闭时等待未结束的回调退出；从其他线程销毁管理器会触发生命周期错误并终止程序。
	 */
	class CATALPA_API AssetManager {
	public:
		AssetManager(
		    JobSystem&, VirtualFileSystem files = {}, AssetRegistry registry = {}, AssetManagerConfig config = {});
		~AssetManager();
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;
		/**
		 * @brief 为资产类型登记解码器。
		 *
		 * @param[in] loader 待注册的资产解码器，注册后由资产管理器持有。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool RegisterLoader(AssetLoader, std::string& error);
		/**
		 * @brief 设置回退资源。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] type 查找回退资产时匹配的 C++ 载荷类型。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetFallback(AssetId, std::type_index, std::string& error);
		template <class T> bool SetFallback(AssetId id, std::string& error) {
			return SetFallback(id, std::type_index(typeid(T)), error);
		}
		/**
		 * @brief 校验替换目录的资产类型和版本，将变更传播至依赖方并撤销过期请求。
		 *
		 * @param[in] registry 待安装的完整资产注册表。与当前目录比较后，其内容转入管理器。
		 * @param[out] error 失败时写入目录冲突、线程或分配诊断。
		 *
		 * @return 新目录可安装且相关记录已处理时为 true；存在类型、版本或包会话冲突等错误时为 false。
		 */
		bool ReplaceRegistry(AssetRegistry, std::string& error);
		/**
		 * @brief 安装烘焙资产目录，必要时撤下同一挂载中已移除的资产。
		 * 主线程安装 Cooked 目录。传入完整 manifest 时，撤下同一挂载中已移除的记录。
		 *
		 * @param[in] absoluteRoot 将已构建资产映射为本地文件的绝对根目录。
		 * @param[in] catalog 待安装的资产目录，提供资源记录及其位置。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] replaceMountContents 是否撤下原挂载中已移除的目录项。
		 *
		 * @return 挂载及目录更新成功时为 true，否则为 false。
		 */
		bool InstallCookedCatalog(const std::filesystem::path& absoluteRoot, const AssetRegistry& catalog,
		    std::string& error, bool replaceMountContents = false);
		/**
		 * @brief 安装资源包，并为会话结束后恢复原挂载与目录保存快照。
		 * 烘焙版本是非零的不透明内容键，先后顺序由请求代次决定。
		 * 单次会话保存原挂载与目录，退出时等待全部读取任务结束后恢复。
		 *
		 * @param[in] package 待挂载的已打开资源包，提供目录和资产读取入口。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 资源包成功挂载并安装目录时为 true，否则为 false。
		 */
		bool InstallPackage(std::shared_ptr<const PackageReader>, std::string& error);
		/**
		 * @brief 卸载资源包。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool UninstallPackage(std::string& error);
		/**
		 * @brief 在所有者线程请求加载指定资产及其依赖。
		 *
		 * @param[in] id 目标资产标识。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] forceReload 是否重新加载已请求或已有版本的资产。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Request(AssetId, std::string& error, bool forceReload = false);
		/**
		 * @brief 取消指定资产当前的加载请求。
		 *
		 * @param[in] id 待取消的资产标识。
		 */
		void Cancel(AssetId);
		/**
		 * @brief 取消在途加载并撤下该资产当前可供查询的版本。
		 *
		 * @param[in] id 待卸载的资产标识。
		 */
		void Unload(AssetId);
		/**
		 * @brief 在所属线程回收异步作业，发布仍属于当前请求代次的结果。
		 * 仅在所属线程发布后台加载结果。请求代次不匹配或状态已离开 Loading 时，回收旧作业，不覆盖当前资产。
		 */
		void Pump();
		/**
		 * @brief 取消并等待资产加载作业完成，关闭管理器。
		 */
		void Shutdown() noexcept;
		[[nodiscard]] AssetStatus GetStatus(AssetId) const;
		[[nodiscard]] std::string GetDiagnostic(AssetId) const;
		[[nodiscard]] MemoryBudgetStats GetBudgetStats() const;
		[[nodiscard]] size_t GetPendingCount() const;
		/**
		 * @brief 按消费者游标读取环形缓冲区中仍保留的资产变更。
		 * 每个消费者保存自己的 cursor；历史溢出时 reset=true，消费者重扫派生缓存。
		 *
		 * @param[in,out] cursor 消费者下一条待读变更的序号，成功后推进到本次读取时的队尾。
		 * @param[out] output 接收从游标到队尾的变更；游标失效时接收环形缓冲区仍保留的全部变更。
		 * @param[out] reset 输出游标是否超前或历史变更已被覆盖；为真时消费者需要重新同步。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功读取并推进游标时为 true。历史不连续可以成功返回，由 reset 指示。
		 */
		bool ReadChanges(
		    uint64_t& cursor, std::vector<AssetChangeEvent>& output, bool& reset, std::string& error) const;
		/**
		 * @brief 获取指定资产当前可用的精确版本和只读载荷。
		 * 为作者侧实例化保活当前可用的精确版本。此操作不触发加载，也不选用回退版本。
		 *
		 * @param[in] id 要固定当前依赖版本的资产标识。
		 * @param[out] output 接收含标识、确切版本及只读载荷的依赖快照。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 取得有效快照时为 true；资产不可用或线程不符时为 false。
		 */
		bool GetDependencySnapshot(AssetId, AssetDependencyValue&, std::string& error) const;
		/**
		 * @brief 返回资产。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] type 所请求资产载荷的 C++ 类型，用于检查登记版本是否匹配。
		 * @param[out] output 接收 AssetSnapshot 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool GetAsset(AssetId, std::type_index, AssetSnapshot&, std::string& error) const;
		/**
		 * @brief 将引擎生成的内置资产按指定标识和版本发布到缓存。
		 * 引擎生成的资产与其他资产共用身份、版本、缓存和内存预算。
		 * 内置资产的生产者须提供递增的修订号，不得覆盖已登记的烘焙资产身份。
		 *
		 * @param[in] id 内置资产使用的稳定标识，不得与已构建目录中的条目冲突。
		 * @param[in] version 本次发布的非零内容版本，更新须使用递增版本。
		 * @param[in] value 包含资源类型、不可变载荷和驻留字节数的加载结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 载荷、版本和预算检查通过且发布成功时为 true，否则为 false。
		 */
		bool PublishBuiltin(AssetId, uint64_t version, AssetLoadResult, std::string& error);
		template <class T>
		bool PublishBuiltin(
		    AssetId id, uint64_t version, std::shared_ptr<const T> value, size_t bytes, std::string& error) {
			return PublishBuiltin(id, version, MakeAssetLoadResult<T>(std::move(value), bytes), error);
		}
		/**
		 * @brief 按目标 C++ 类型获取资产版本，并报告是否使用了替代资产。
		 *
		 * @param[in] id 目标资产标识。
		 * @param[out] output 接收持有数据的类型化版本引用。
		 * @param[in] usedFallback 成功时指明交付的是替代资产还是所请求资产。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		template <class T>
		bool Acquire(AssetId id, AssetVersionRef<T>& output, bool& usedFallback, std::string& error) const {
			AssetSnapshot snapshot;
			if (!GetAsset(id, std::type_index(typeid(T)), snapshot, error))
				return false;
			if (!AssetVersionRef<T>::TryCreate(snapshot.id, snapshot.version,
			        std::static_pointer_cast<const T>(snapshot.asset.value), output, error))
				return false;
			usedFallback = snapshot.fallback;
			return true;
		}

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
