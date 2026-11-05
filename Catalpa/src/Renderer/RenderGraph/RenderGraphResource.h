#pragma once

#include "Core/Memory/GenerationalHandle.h"
#include "RHI/RHIResourceState.h"

// 定义图内资源身份，复用已有 GenerationalHandle。图内句柄与设备资源句柄分别管理，避免跨图误用
namespace Catalpa {

    template<typename Tag>
    struct RGHandle {
        uint64_t graph = 0;
        GenerationalHandle<Tag> slot{};

        // 这里只检查身份是否有值，归属和索引由 RenderGraph 检查。
        [[nodiscard]] constexpr bool HasValue() const noexcept { return graph != 0 && slot.HasValue(); }
        constexpr bool operator==(const RGHandle&) const = default;
    };

    struct RGBufferTag;
    struct RGTextureTag;
    struct RGPassTag;

    using RGBuffer = RGHandle<RGBufferTag>;
    using RGTexture = RGHandle<RGTextureTag>;
    using RGPass = RGHandle<RGPassTag>;

    enum class RGResourceKind { Buffer, Texture };
    enum class RGAccessMode { Read, Write, ReadWrite };
    // 写入可完全初始化范围、保留已有初始化，或丢弃其内容；绘制不假定覆盖全部像素。
    enum class RGWriteInitialization { Preserve, Initialize, Discard };

    // 编译器的物理范围目录：buffer 按字节，texture 按 layer*mipLevels+mip 登记初始化。
    // 子资源的内容状态与完整唯一视图的布局分开跟踪；附件仍限单 mip、单层。
    struct RGResourceDeclaration {
        RGResourceKind kind = RGResourceKind::Buffer;
        uint64_t size = 0;
        std::vector<RHIBufferRange> initialized;
    };

    // 同一份 pass 参数派生访问与录制；resource 是本次目录索引，不是设备句柄。
    struct RGResourceAccess {
        uint32_t resource = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        RGAccessMode mode = RGAccessMode::Read;
        RGWriteInitialization initialization = RGWriteInitialization::Preserve;
    };

    // 副作用 pass（包括提交后统一认领的上传）即使没有当前画面消费者也必须执行。
    struct RGPassDeclaration {
        std::vector<RGResourceAccess> accesses;
        bool sideEffect = false;
    };

    enum class RGDependencyKind { RAW, WAR, WAW, Explicit };
    struct RGDependency {
        uint32_t before = 0;
        uint32_t after = 0;
        RGDependencyKind kind = RGDependencyKind::Explicit;
        bool operator==(const RGDependency&) const = default;
    };

    // 每个切分范围对应真实读取版本/新写版本；0 是导入版本，非设备状态 revision。
    struct RGAccessVersion {
        uint32_t pass = 0;
        uint32_t access = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t inputVersion = 0;
        uint64_t outputVersion = 0;
    };

    // 不可执行的 CPU 编译信息：稳定执行序列、依赖及用于诊断的范围版本。
    struct RGCompiledSchedule {
        std::vector<uint32_t> passOrder;
        std::vector<RGDependency> dependencies;
        std::vector<RGAccessVersion> versions;
        uint32_t culledPassCount = 0;
    };

}
