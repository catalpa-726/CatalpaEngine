#pragma once

#include "Core/IO/VirtualPath.h"

namespace Catalpa {

    // 定位的是作者来源还是目标平台派生文件；不表示加载状态或 GPU 驻留。
    enum class AssetLocationKind { Source, Cooked };

    // 资产的规范虚拟地址；真实根目录由 VFS 挂载决定，不依赖进程工作目录。
    // 当前仅定位文件，包条目读取在后续包系统接入，不暴露无消费者的 packageEntry。
    struct AssetLocator {
        VirtualPath path;
        AssetLocationKind kind = AssetLocationKind::Source;

        [[nodiscard]] bool HasValue() const noexcept {
            return path.HasValue() && (kind == AssetLocationKind::Source || kind == AssetLocationKind::Cooked);
        }
    };

}
