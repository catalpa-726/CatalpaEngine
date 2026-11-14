#pragma once

#include "RenderGraphPlan.h"
#include "Core/Base.h"

/*
 * 保存本图使用的资源快照、已校验的通道参数和显式依赖声明。
 * 声明阶段不取得 GPU 资源所有权，也不录制命令或发布后端资源状态。
 */
namespace Catalpa {

	class RHIDevice;

	/* 渲染图由主线程构建。导入只借用资源，调用方须使其至少在录制前保持有效。 */
	class RenderGraphBuilder {
	public:
		/**
		 * @brief 为本次图构建分配独立身份。
		 *
		 * @param[in] device 渲染图构建期间借用的设备，用于查询导入资源及创建瞬态资源。
		 */
		CATALPA_API explicit RenderGraphBuilder(RHIDevice& device);

		RenderGraphBuilder(const RenderGraphBuilder&) = delete;
		RenderGraphBuilder& operator=(const RenderGraphBuilder&) = delete;
		RenderGraphBuilder(RenderGraphBuilder&&) = delete;
		RenderGraphBuilder& operator=(RenderGraphBuilder&&) = delete;

		/**
		 * @brief 导入设备资源及状态快照，同一资源重复导入返回同一图内句柄。
		 *
		 * @param[in] buffer 由调用者保活的设备缓冲区。
		 * @param[out] resource 接收图内的缓冲区引用。
		 *
		 * @return 返回导入缓冲区及其状态快照的结果；重复导入返回已有图内句柄。
		 */
		[[nodiscard]] CATALPA_API RHIStatus ImportBuffer(BufferHandle buffer, RGBuffer& resource);
		/**
		 * @brief 仅图内使用，逻辑内容始终从未初始化开始。编译器以真实依赖边证明别名生命周期。
		 *
		 * @param[in] desc 瞬态缓冲区的大小、用途和内存域。
		 * @param[out] resource 接收图内的缓冲区引用。
		 *
		 * @return 返回创建瞬态缓冲声明的状态；实际设备资源由编译阶段安排。
		 */
		[[nodiscard]] CATALPA_API RHIStatus CreateTransientBuffer(const RHIBufferDesc&, RGBuffer& resource);
		/**
		 * @brief 取得纹理视图的状态快照并分配图内句柄。
		 *
		 * @param[in] view 由调用者保活的设备纹理视图。
		 * @param[out] resource 接收图内的纹理引用。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus ImportTexture(TextureViewHandle view, RGTexture& resource);

		/**
		 * @brief 登记一次缓冲区范围复制供编译器生成同步关系。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] source 供读取的图内缓冲区。
		 * @param[in] destination 接收数据的图内缓冲区。
		 * @param[in] copy 源偏移、目标偏移与字节数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddCopyPass(
		    const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy);
		/**
		 * @brief 登记由缓冲区向纹理上传的任务。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] source 提供上传数据的图内缓冲区。
		 * @param[in] destination 接收上传数据的纹理。
		 * @param[in] copy 缓冲区偏移与纹理区域。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddTextureUploadPass(
		    const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy);
		/**
		 * @brief 登记将纹理区域复制到读回缓冲区的任务。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] source 待读取的图内纹理。
		 * @param[in] destination 保存读回字节的图内缓冲区。
		 * @param[in] copy 目标偏移与源图像区域。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddTextureReadbackPass(
		    const char* name, RGTexture source, RGBuffer destination, const RHITextureBufferCopy& copy);
		/**
		 * @brief 验证绘制方式、附件及其资源范围，通过后将绘制步骤加入渲染图。
		 *
		 * @param[in] name 用于命令标签及诊断的通道名称，不能为空。
		 * @param[in] desc 顶点数非零时直接绘制，否则使用索引；还包含附件、视口、绑定及可选的间接参数缓冲。
		 *
		 * @return 步骤成功登记时返回成功，否则返回首个校验错误，图中不会增加该步骤。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddDrawPass(const char* name, const RGIndexedDrawDesc& desc);
		/**
		 * @brief 登记计算任务及其资源访问范围。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] desc 计算管线、工作组与资源绑定。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddComputePass(const char* name, const RGComputeDesc& desc);
		/**
		 * @brief 空场景也以真实附件Clear初始化颜色，不用零顶点绘制或旧内容掩盖未初始化。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] color 待清除的图内颜色附件。
		 * @param[in] clearColor 写入各像素的 RGBA 值。
		 *
		 * @return 返回将清屏任务加入渲染图的状态。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddClearPass(
		    const char* name, RGTexture color, std::array<float, 4> clearColor);
		/**
		 * @brief 登记用于初始化深度附件的清除任务。
		 *
		 * @param[in] name 诊断中显示的任务名。
		 * @param[in] depth 待清除的图内深度附件。
		 * @param[in] clearDepth 写入的深度值，需与投影约定一致。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddDepthClearPass(const char* name, RGTexture depth, float clearDepth);

		/**
		 * @brief 声明交换链图像为最终呈现输出。
		 *
		 * @param[in] resource 需要在呈现布局交给交换链的图内纹理。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus ExportPresent(RGTexture resource);
		/**
		 * @brief 显式导出使离屏预览免于死写剔除，并将结果转入采样布局，无须呈现到交换链。
		 *
		 * @param[in] resource 需要保留并转换到可采样布局的图内纹理。
		 *
		 * @return 返回将纹理标为图外读取并保留其最终版本的结果。
		 */
		[[nodiscard]] CATALPA_API RHIStatus ExportTexture(RGTexture resource);
		/**
		 * @brief 保留纹理并声明供后续读回使用的最终访问状态。
		 *
		 * @param[in] resource 要导出的图内读回纹理。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus ExportReadbackTexture(RGTexture resource);
		/**
		 * @brief 关联本设备的时间戳查询池，用于测量当前渲染图的 GPU 执行时间。
		 *
		 * @param[in] pool 归属同一设备且至少含两个查询槽的共享池，用于记录图的起止时间戳。
		 *
		 * @return 查询池已绑定时返回成功；图状态无效、设备不匹配或不足两个查询槽时返回错误。
		 */
		[[nodiscard]] CATALPA_API RHIStatus SetTimestampQueries(std::shared_ptr<RHIQueryPool> pool);

		/**
		 * @brief 返回最后声明的 pass，用于补充不能由资源访问推导的先后关系。
		 *
		 * @return 返回最后添加的任务句柄；尚无任务时返回空句柄。
		 */
		[[nodiscard]] CATALPA_API RGPass GetLastPass() const noexcept;
		/**
		 * @brief 增加不能仅从资源访问关系推导的先后依赖。
		 *
		 * @param[in] before 必须先执行的任务。
		 * @param[in] after 依赖该任务的后继任务。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] CATALPA_API RHIStatus AddDependency(RGPass before, RGPass after);

	private:
		friend class RenderGraphCompiler;
		struct BufferResource {
			BufferHandle handle{};
			RHIBufferStateSnapshot snapshot{};
			bool transient = false;
		};

		struct TextureResource {
			TextureViewHandle handle{};
			RHITextureStateSnapshot snapshot{};
		};

		enum class PassKind { Copy, TextureUpload, TextureReadback, Draw, Clear, Compute };

		/* 保存不同 Pass 的声明参数，由 Compile 生成实际命令步骤。 */
		struct Pass {
			std::string name;
			PassKind kind = PassKind::Copy;
			RGBuffer source{};
			RGBuffer destination{};
			RHIBufferCopy copy{};
			RGTexture destinationTexture{};
			RHIBufferTextureCopy textureCopy{};
			uint64_t textureBytes = 0;
			RGTexture sourceTexture{};
			RHITextureBufferCopy readbackCopy{};
			RGIndexedDrawDesc draw{};
			RGTexture clearTexture{};
			std::array<float, 4> clearColor{};
			RGComputeDesc compute{};
		};

		[[nodiscard]] RHIStatus CheckBuilding(const char* operation) const;

		[[nodiscard]] const BufferResource* Resolve(RGBuffer resource) const;
		[[nodiscard]] const TextureResource* Resolve(RGTexture resource) const;

		void Describe(std::vector<RGResourceDeclaration>& resources, std::vector<RGPassDeclaration>& passes) const;

		RHIDevice& m_Device;
		uint64_t m_Id = 0;
		bool m_Compiled = false;

		std::vector<BufferResource> m_Buffers;
		std::vector<TextureResource> m_Textures;
		std::vector<Pass> m_Passes;
		RGTexture m_Present{};
		std::vector<RGTexture> m_Exports;
		std::vector<RGTexture> m_ReadbackExports;
		std::shared_ptr<RHIQueryPool> m_Queries;
		std::vector<RGDependency> m_Dependencies;
	};

}
