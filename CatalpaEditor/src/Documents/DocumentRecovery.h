#pragma once
#include "Documents/Document.h"

namespace Catalpa::Editor {
    // 原子保存只写项目内容根；恢复副本写独立目录，不把源素材当作者文件覆盖。
    class DocumentRecovery {
      public:
        static constexpr size_t MaxAuthorBytes = 16 * 1024 * 1024;
        // JSON 字符串中的控制字符最多转义成六个字节，恢复包装不能复用作者正文上限。
        static constexpr size_t MaxRecoveryBytes = 100 * 1024 * 1024;
        static bool Save(Document&, const std::filesystem::path& target,
                         const std::filesystem::path& contentRoot, std::string& error);
        static bool Checkpoint(const Document&, const std::filesystem::path& recoveryRoot,
                               std::string& error);
        static bool Read(const std::filesystem::path&, std::string&, std::string& error,
                         size_t maxBytes = MaxAuthorBytes);
    };
}
