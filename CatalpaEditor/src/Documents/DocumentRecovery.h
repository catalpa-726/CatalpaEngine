#pragma once
#include "Documents/Document.h"

namespace Catalpa::Editor {
    // 原子保存只写项目内容根；恢复副本写独立目录，不把源素材当作者文件覆盖。
    class DocumentRecovery {
      public:
        static constexpr size_t MaxAuthorBytes = 16 * 1024 * 1024;
        static constexpr size_t MaxAnimationBytes = 96 * 1024 * 1024;
        // 大动画和二进制使用 base64；128 MiB 正文编码另预留包装空间，旧文本恢复仍兼容。
        static constexpr size_t MaxRecoveryBytes = 132 * 1024 * 1024;
        static size_t AuthorLimit(std::string_view type) noexcept;
        static std::string_view AuthorSuffix(std::string_view type) noexcept;
        static std::string AuthorType(std::string_view extension, std::string_view source);
        static bool ValidateAuthorSource(std::string_view type, std::string_view source, std::string& error);
        static bool DecodeCheckpoint(std::string_view envelope, std::string& type, std::string& content,
                                     std::string& error);
        static bool Save(Document&, const std::filesystem::path& target,
                         const std::filesystem::path& contentRoot, std::string& error);
        static bool Checkpoint(const Document&, const std::filesystem::path& recoveryRoot,
                               std::string& error);
        static bool Read(const std::filesystem::path&, std::string&, std::string& error,
                         size_t maxBytes = MaxAuthorBytes);
    };
}
