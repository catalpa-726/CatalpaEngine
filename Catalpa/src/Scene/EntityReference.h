#pragma once

#include "Scene/PersistentEntityId.h"
#include "ECS/EntityId.h"

namespace Catalpa {

    // 当前Scene的运行期解析令牌；持久身份不包含地址，World身份防止跨场景误解析。
    // 序列化时不能写入world；跨场景保存/加载协议属于S10。
    struct EntityReference {
        uint64_t world = 0;
        PersistentEntityId persistent;
        EntityId resolved;
        [[nodiscard]] constexpr bool HasValue() const noexcept { return world != 0 && persistent.HasValue() && resolved.HasValue(); }
        constexpr bool operator==(const EntityReference&) const = default;
    };

}
