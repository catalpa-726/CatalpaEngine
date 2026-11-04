#pragma once

#include "RenderGraphPlan.h"
#include "Core/Base.h"

// 持有本图资源快照、受检 Pass 参数和显式依赖声明。
// 声明不创建 GPU owner、不录制命令、不发布后端资源状态。
namespace Catalpa {

    class RHIDevice;

    // 当前由主线程构建；导入不取得 owner，录制前由调用方保持资源有效。
    class RenderGraphBuilder {
    public:
        // 为本次图构建分配独立身份。
        CATALPA_API explicit RenderGraphBuilder(RHIDevice& device);

        RenderGraphBuilder(const RenderGraphBuilder&) = delete;
        RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;
        RenderGraphBuilder(RenderGraphBuilder&&) = delete;
        RenderGraphBuilder& operator=(RenderGraphBuilder&&) = delete;

        // 导入设备资源及状态快照，同一资源重复导入返回同一图内句柄。
        [[nodiscard]] CATALPA_API RHIStatus ImportBuffer(BufferHandle buffer, RGBuffer& resource);
        [[nodiscard]] CATALPA_API RHIStatus ImportTexture(TextureViewHandle view, RGTexture& resource);

        // 登记复制或索引绘制，同时保存访问推导所需的参数。
        [[nodiscard]] CATALPA_API RHIStatus AddCopyPass(const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy);
        // 登记整幅、紧密排列的颜色纹理上传。
        [[nodiscard]] CATALPA_API RHIStatus AddTextureUploadPass(const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy);
        [[nodiscard]] CATALPA_API RHIStatus AddDrawPass(const char* name, const RGIndexedDrawDesc& desc);
        // 空场景也以真实附件Clear初始化颜色，不用零顶点绘制或旧内容掩盖未初始化。
        [[nodiscard]] CATALPA_API RHIStatus AddClearPass(const char* name, RGTexture color, std::array<float, 4> clearColor);

        // 指定最终呈现输出，并结束声明阶段。
        [[nodiscard]] CATALPA_API RHIStatus ExportPresent(RGTexture resource);

        // 返回最后声明的 pass，用于补充不能由资源访问推导的先后关系。
        [[nodiscard]] CATALPA_API RGPass GetLastPass() const noexcept;
        [[nodiscard]] CATALPA_API RHIStatus AddDependency(RGPass before, RGPass after);

    private:
        friend class RenderGraphCompiler;
        struct BufferResource {
            BufferHandle handle{};
            RHIBufferStateSnapshot snapshot{};
        };

        struct TextureResource {
            TextureViewHandle handle{};
            RHITextureStateSnapshot snapshot{};
        };

        enum class PassKind { Copy, TextureUpload, Draw, Clear };

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
            RGTexture clearTexture{};
            std::array<float, 4> clearColor{};
        };

        // 检查图仍处于允许添加声明的阶段。
        [[nodiscard]] RHIStatus CheckBuilding(const char* operation) const;

        // 校验图身份、代次和索引，再解析本图资源。
        [[nodiscard]] const BufferResource* Resolve(RGBuffer resource) const;
        [[nodiscard]] const TextureResource* Resolve(RGTexture resource) const;

        // 从录制参数生成同源范围访问，纹理目前只描述整个 mip0/layer0。
        void Describe(std::vector<RGResourceDeclaration>& resources, std::vector<RGPassDeclaration>& passes) const;

        RHIDevice& m_Device;
        uint64_t m_Id = 0;
        bool m_Compiled = false;

        std::vector<BufferResource> m_Buffers;
        std::vector<TextureResource> m_Textures;
        std::vector<Pass> m_Passes;
        RGTexture m_Present{};
        std::vector<RGDependency> m_Dependencies;
    };

}
