#pragma once
#include "AssetPipeline/AssetBuildTypes.h"

namespace Catalpa::Tools {
    // glTF 外部引用是 URI；OBJ、MTL 和 FBX 提供的是文件路径。
    enum class SourceReferenceSyntax { Uri, FilePath };

    // 一个构建请求拥有独立的输入清单和工作预算，源文件统一通过只读 VFS 打开。
    class AssetBuildContext final {
      public:
        explicit AssetBuildContext(AssetBuildRequest request) : request(std::move(request)) {}

        // 解析项目配置中的根目录，检查派生目录与只读输入之间的重叠。
        bool Initialize(std::string& error);
        // 返回自有字节，并对本批次重复打开的同一输入校验内容哈希。
        bool Read(const std::filesystem::path&, std::vector<std::byte>&, std::string& error);
        // 相对 URI 从引用者所在目录解析，重映射后的目标也必须属于声明的源根。
        bool Resolve(const std::filesystem::path& owner, std::string_view reference, std::filesystem::path&,
                     std::string& error, SourceReferenceSyntax syntax = SourceReferenceSyntax::Uri);
        bool CheckCancelled(std::string& error) const;
        bool InputsUnchanged(std::string& error) const;
        // ReserveWorking 累计持有的 CPU 载荷，WorkspaceFits 检查本次临时解码所需的空间。
        bool ReserveWorking(size_t bytes, std::string& error);
        bool WorkspaceFits(size_t transientBytes, std::string& error) const;
        AssetId Identity(std::string_view key) const;
        void Diagnose(AssetDiagnosticLevel, std::string element, std::string message);

        const std::vector<AssetSourceDependency>& Dependencies() const {
            return m_Dependencies;
        }

        std::string SourceKey() const {
            return m_SourceKey;
        }

        AssetBuildRequest request;
        std::vector<AssetBuildDiagnostic> diagnostics;

      private:
        bool ReadSource(const std::filesystem::path& path, size_t byteLimit, std::vector<std::byte>& bytes,
                        std::string& error) const;
        std::vector<std::filesystem::path> m_Roots;
        std::vector<std::string> m_RootNames;
        std::vector<AssetSourceDependency> m_Dependencies;
        std::string m_SourceKey;
        size_t m_ReadBytes = 0;
        size_t m_WorkingBytes = 0;
        size_t m_OmittedDiagnostics = 0;
    };
}
