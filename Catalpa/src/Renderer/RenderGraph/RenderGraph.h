#pragma once

#include "RenderGraphPlan.h"

// 负责当前图的资源导入、Pass 登记和编译
// 目前使用固定顺序、单 graphics queue；上传 Pass 也进入这条顺序
namespace Catalpa {

    class RHIDevice;

    // 当前由主线程构建；导入不取得 owner，录制前由调用方保持资源有效。
    class RenderGraph {
    public:
        // 为本次图构建分配独立身份。
        explicit RenderGraph(RHIDevice& device);

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        RenderGraph(RenderGraph&&) = delete;
        RenderGraph& operator=(RenderGraph&&) = delete;

        // 导入设备资源及状态快照，同一资源重复导入返回同一图内句柄。
        [[nodiscard]] RHIStatus ImportBuffer(BufferHandle buffer, RGBuffer& resource);
        [[nodiscard]] RHIStatus ImportTexture(TextureViewHandle view, RGTexture& resource);

        // 登记复制或索引绘制，同时保存访问推导所需的参数。
        [[nodiscard]] RHIStatus AddCopyPass(const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy);
        // 登记整幅、紧密排列的颜色纹理上传。
        [[nodiscard]] RHIStatus AddTextureUploadPass(const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy);
        [[nodiscard]] RHIStatus AddDrawPass(const char* name, const RGIndexedDrawDesc& desc);

        // 指定最终呈现输出，并结束声明阶段。
        [[nodiscard]] RHIStatus ExportPresent(RGTexture resource);

        // 检查初始化和依赖，生成屏障及状态更新；不修改设备状态。
        [[nodiscard]] RHIStatus Compile(RenderGraphPlan& plan);

    private:
        struct BufferResource {
            BufferHandle handle{};
            RHIBufferStateSnapshot snapshot{};
        };

        struct TextureResource {
            TextureViewHandle handle{};
            RHITextureStateSnapshot snapshot{};
        };

        enum class PassKind { Copy, TextureUpload, Draw };

        // 保存不同 Pass 的声明参数，由 Compile 生成实际命令步骤。
        struct Pass {
            std::string name;
            PassKind kind = PassKind::Copy;
            RGBuffer source{};
            RGBuffer destination{};
            RHIBufferCopy copy{};
            RGTexture destinationTexture{};
            RHIBufferTextureCopy textureCopy{};
            uint64_t textureBytes = 0;
            RGIndexedDrawDesc draw{};
        };

        // 检查图仍处于允许添加声明的阶段。
        [[nodiscard]] RHIStatus CheckBuilding(const char* operation) const;

        // 校验图身份、代次和索引，再解析本图资源。
        [[nodiscard]] const BufferResource* Resolve(RGBuffer resource) const;
        [[nodiscard]] const TextureResource* Resolve(RGTexture resource) const;

        RHIDevice& m_Device;
        uint64_t m_Id = 0;
        bool m_Compiled = false;

        std::vector<BufferResource> m_Buffers;
        std::vector<TextureResource> m_Textures;
        std::vector<Pass> m_Passes;
        RGTexture m_Present{};
    };

}