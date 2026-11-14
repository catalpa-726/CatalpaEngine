#pragma once
#include "catpch.h"

#include "TestRunner.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa::Tests::AssetTest {
	/* 使用小型真实载荷观测异步作业的所有权，无须创建 GPU 对象。 */
	struct Value {
		uint32_t number = 0;
		std::thread::id decodedOn;
	};
	inline AssetId Id(uint64_t low) {
		return AssetId::FromWords(0x4341545445535400ULL, low);
	}
	/**
	 * @brief 返回测试数值载荷使用的资源类型。
	 *
	 * @return 测试载荷的已解析资产类型 test_value。
	 */
	inline AssetType Type() {
		AssetType value;
		if (!AssetType::TryParse("test_value", value))
			throw std::logic_error("Invalid test type");
		return value;
	}
	inline AssetManagerConfig Config() {
		return {4096, 4096, 128 * 1024, 8};
	}
	/**
	 * @brief 构造包含整数测试值和内容版本的资产容器。
	 *
	 * @param[in] id 用于定位目标的资产标识。
	 * @param[in] number 写入测试容器载荷的无符号整数。
	 * @param[in] version 写入测试容器头的内容版本。
	 *
	 * @return 含指定身份、版本和一个 U32 数值载荷的测试容器。
	 */
	inline AssetContainer Container(AssetId id, uint32_t number, uint64_t version = 1) {
		AssetContainer value;
		value.id = id;
		value.type = Type();
		value.version = version;
		BinaryWriter writer;
		writer.U32(number);
		value.payload = writer.Data();
		return value;
	}
	/**
	 * @brief 创建能解码测试值并记录解码线程的加载器。
	 *
	 * @param[in] before 可选的解码回调，取得载荷数值与取消令牌，用于协调测试线程。
	 *
	 * @return 已配置 test_value 类型和解码回调的加载器；成功解码时载荷会记录实际执行线程。
	 */
	inline AssetLoader Loader(std::function<void(uint32_t, const CancellationToken&)> before = {}) {
		AssetLoader loader;
		loader.type = Type();
		loader.cppType = std::type_index(typeid(Value));
		loader.decode = [before = std::move(before)](const AssetContainer& container, const AssetDecodeContext& context,
		                    AssetLoadResult& output, std::string& error) {
			BinaryReader reader(container.payload);
			uint32_t number;
			if (container.payloadVersion != 1 || !reader.U32(number) || !reader.AtEnd()) {
				error = "Invalid test payload";
				return false;
			}
			if (before)
				before(number, context.cancellation);
			output = MakeAssetLoadResult<Value>(
			    std::make_shared<const Value>(Value{number, std::this_thread::get_id()}), sizeof(Value));
			error.clear();
			return true;
		};
		return loader;
	}
	/* 仅清理本次调用独占创建的临时目录及明确列出的条目。 */
	struct Fixture {
		std::filesystem::path root;
		std::vector<std::filesystem::path> files;
		~Fixture() {
			std::error_code ignored;
			for (const auto& file : files)
				std::filesystem::remove(file, ignored);
			if (!root.empty())
				std::filesystem::remove(root, ignored);
		}
		/**
		 * @brief 创建本次用例独占的临时资源目录。
		 *
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Create(std::string& error) {
			const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
			for (unsigned attempt = 0; attempt < 8; ++attempt) {
				StableId unique;
				if (!GenerateStableId(unique, error))
					return false;
				const auto candidate = parent / ("Catalpa-Assets-" + unique.ToString());
				if (std::filesystem::create_directory(candidate)) {
					root = candidate;
					return true;
				}
			}
			error = "Cannot create exclusive asset fixture directory";
			return false;
		}
		/**
		 * @brief 序列化测试容器并生成对应的资源记录。
		 *
		 * @param[in] container 要序列化到测试目录的资产容器，含类型、版本、依赖及载荷。
		 * @param[out] record 接收落盘容器的标识、版本、路径和散列。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Save(const AssetContainer& container, AssetRecord& record, std::string& error) {
			std::vector<std::byte> bytes;
			if (!WriteAssetContainer(container, bytes, error, 4096))
				return false;
			const auto name = container.id.ToString() + ".asset";
			const auto target = root / name;
			if (std::find(files.begin(), files.end(), target) == files.end())
				files.push_back(target);
			if (!AtomicFileWriter::Write(target, bytes, error))
				return false;
			AssetRecord candidate;
			candidate.id = container.id;
			candidate.type = container.type;
			candidate.version = container.version;
			candidate.locator.kind = AssetLocationKind::Cooked;
			if (!VirtualPath::Parse("test:/" + name, candidate.locator.path, error) ||
			    !ComputeAssetContentHash(bytes, candidate.contentHash, error))
				return false;
			for (const auto& dependency : container.dependencies)
				candidate.dependencies.push_back(dependency.id);
			record = std::move(candidate);
			return true;
		}
		bool Mount(VirtualFileSystem& filesOut, std::string& error) const {
			return filesOut.MountReadOnly("test", root, 0, error);
		}
	};
	/**
	 * @brief 两个编辑器 GPU 验收共用的独占 PNG 和 PBR 材质输入。
	 *
	 * @param[in,out] fixture 持有临时项目及待生成纹理、材质素材的测试夹具。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return PNG 和材质两个测试文件均写入独占临时目录时为 true。
	 */
	bool CreateThumbnailSources(Fixture& fixture, std::string& error);

	/**
	 * @brief 驱动资源管理器直至待处理工作完成或达到测试上限。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 *
	 * @return 五秒内观察到资源管理器的待处理工作数降到零时为 true。
	 */
	inline bool Drain(AssetManager& manager) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do {
			manager.Pump();
			if (manager.GetPendingCount() == 0)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (std::chrono::steady_clock::now() < deadline);
		return false;
	}
	/**
	 * @brief 在超时前驱动指定测试资源的异步加载。
	 *
	 * @param[in] manager 提供资产查找和版本访问的管理器。
	 * @param[in] id 用于定位目标的资产标识。
	 *
	 * @return 五秒内资源离开加载和等待依赖状态时为 true；加载失败也会结束等待。
	 */
	inline bool Await(AssetManager& manager, AssetId id) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		do {
			manager.Pump();
			const auto state = manager.GetStatus(id);
			if (state != AssetStatus::Loading && state != AssetStatus::WaitingForDependencies)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (std::chrono::steady_clock::now() < deadline);
		return false;
	}
	/* 有界等待允许已取消的旧作业稍后返回，以验证加载代次检查。 */
	struct Gate {
		std::atomic_bool entered{false}, release{false};
		/**
		 * @brief 通知测试线程已到达同步点并等待放行。
		 */
		void Enter() {
			entered.store(true, std::memory_order_release);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			while (!release.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		/**
		 * @brief 等待工作线程发出到达同步点的信号。
		 *
		 * @return 两秒内观察到工作线程发出的入口信号时为 true。
		 */
		bool AwaitEntry() const {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			return entered.load(std::memory_order_acquire);
		}
	};
}
