#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
	/* glTF 外部引用是 URI；OBJ、MTL 和 FBX 提供的是文件路径。 */
	enum class SourceReferenceSyntax { Uri, FilePath, FbxTexture };

	/* 一个构建请求拥有独立的输入清单和工作预算，源文件统一通过只读 VFS 打开。 */
	class AssetBuildContext final {
	public:
		explicit AssetBuildContext(AssetBuildRequest request) : request(std::move(request)) {}

		/**
		 * @brief 解析并校验本次资产构建可使用的输入与输出根目录。
		 * 解析项目配置中的根目录，检查派生目录与只读输入之间的重叠。
		 *
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 输入、配置与派生目录满足路径约束且初始化完成时为 true。
		 */
		bool Initialize(std::string& error);
		/**
		 * @brief 在预算内读取源文件并记录其依赖与内容哈希。
		 * 返回自有字节，并对本批次重复打开的同一输入校验内容哈希。
		 *
		 * @param[in] input 本次构建要读取并跟踪的源文件路径。
		 * @param[out] out 成功时接收自有的源文件字节副本。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 输入在读取预算内完整读出并通过重复读取一致性检查时为 true。
		 */
		bool Read(const std::filesystem::path&, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 从本次构建的配置根读取单个配置文件，并计入依赖检查。
		 * 工具配置根来自显式shaderRoot的同级Config，读取也进入重复读/变更/DDC校验。
		 *
		 * @param[in] name 要从工具配置根读取的配置文件名。
		 * @param[out] output 成功时接收完整配置文件字节。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 配置文件名有效且指定文件成功读取时为 true。
		 */
		bool ReadConfiguration(std::string_view name, std::vector<std::byte>&, std::string& error);
		/**
		 * @brief 根据引用语法和所有者目录，解析位于允许源根内的依赖文件。
		 * 相对 URI 从引用者所在目录解析，重映射后的目标也必须属于声明的源根。
		 *
		 * @param[in] owner 声明该依赖的源文件路径。
		 * @param[in] reference 源文件中的原始依赖引用。
		 * @param[out] out 成功时接收通过源根检查和显式重映射后的依赖路径。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 * @param[in] syntax 引用按路径还是 URI 解释。
		 *
		 * @return 引用解析为允许源根中的文件时为 true，语法、重映射或路径检查失败时为 false。
		 */
		bool Resolve(const std::filesystem::path& owner, std::string_view reference, std::filesystem::path&,
		    std::string& error, SourceReferenceSyntax syntax = SourceReferenceSyntax::Uri);
		/**
		 * @brief 在读取和构建期间检查是否收到取消请求。
		 *
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 任务可以继续时为 true；已收到取消请求时为 false 并写入原因。
		 */
		bool CheckCancelled(std::string& error) const;
		/**
		 * @brief 检查输入与构建时记录的版本或内容是否一致。
		 *
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 之前读取的输入内容及已重定位的依赖路径仍一致时为 true；取消、读取失败或检测到变化时为 false。
		 */
		bool InputsUnchanged(std::string& error) const;
		/**
		 * @brief 在当前读入与工作内存预算内累计一次工作空间占用。
		 * ReserveWorking 累计持有的 CPU 载荷，WorkspaceFits 检查本次临时解码所需的空间。
		 *
		 * @param[in] bytes 本次追加预留的受管 CPU 工作内存，单位为字节。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 成功计入所请求的工作字节数时为 true，超出预算时为 false。
		 */
		bool ReserveWorking(size_t bytes, std::string& error);
		/**
		 * @brief 检查加入临时缓冲后是否仍满足本次导入的字节预算。
		 *
		 * @param[in] transientBytes 拟新增的临时工作内存字节数。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 已读入字节、已预留工作内存与本次临时缓冲的总量在预算内时为 true；此检查不追加预留。
		 */
		bool WorkspaceFits(size_t transientBytes, std::string& error) const;
		AssetId Identity(std::string_view key) const;
		/**
		 * @brief 向本次导入的诊断列表追加一条消息。
		 *
		 * @param[in] level 本条导入诊断的严重程度。
		 * @param[in] element 诊断所属的源元素，例如材质槽或几何体。
		 * @param[in] message 要报告的消息文本。
		 */
		void Diagnose(AssetDiagnosticLevel, std::string element, std::string message);

		const std::vector<AssetSourceDependency>& Dependencies() const { return m_Dependencies; }

		const std::map<std::string, std::filesystem::path, std::less<>>& RelocatedTextures() const {
			return m_RelocatedTextures;
		}

		std::string SourceKey() const { return m_SourceKey; }

		AssetBuildRequest request;
		std::vector<AssetBuildDiagnostic> diagnostics;

	private:
		bool LocateTextureFile(const std::filesystem::path& reference, std::filesystem::path& out, bool& relocated,
		    std::string& error) const;
		bool ReadSource(const std::filesystem::path& path, size_t byteLimit, std::vector<std::byte>& bytes,
		    std::string& error) const;
		std::vector<std::filesystem::path> m_Roots;
		std::vector<std::string> m_RootNames;
		std::filesystem::path m_ConfigurationRoot;
		std::vector<AssetSourceDependency> m_Dependencies;
		/* 记录原引用缺失时选择的同目录纹理，原路径恢复后需重新判定缓存。 */
		std::map<std::string, std::filesystem::path, std::less<>> m_RelocatedTextures;
		std::string m_SourceKey;
		size_t m_ReadBytes = 0;
		size_t m_WorkingBytes = 0;
		size_t m_OmittedDiagnostics = 0;
	};
}
