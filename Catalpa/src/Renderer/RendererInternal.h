#pragma once

#include "Renderer/Renderer.h"
#include "Renderer/Lighting/LightData.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHIQueryDesc.h"
#include "Renderer/Passes/SkinningPass.h"
#include "Renderer/Passes/GpuScenePasses.h"

namespace Catalpa {
	struct Renderer::DrawResources {
		std::shared_ptr<MaterialGpuResource> material;
		std::array<std::shared_ptr<TextureGpuResource>, 5> textures;
		std::shared_ptr<PipelineResource> forward, depth, shadow, picking;
		bool reversedZ = false, mirrored = false;
		std::array<SamplerHandle, 5> samplers{};
	};

	struct Renderer::InstanceGpu {
		std::shared_ptr<GeometryGpuResource> geometry;
		std::vector<DrawResources> materials;
	};

	/* 整组附件共享生命周期，UI 显示旧令牌或读回旧帧时仍可借用同一组资源。 */
	struct Renderer::Targets {
		struct Image {
			TextureHandle texture{};
			TextureViewHandle view{};
		};
		RHIDevice* device = nullptr;
		Extent2D extent{};
		uint64_t generation = 0;
		Image hdr, depth, display, shadow, picking, pickingDepth;
		~Targets() {
			if (!device)
				return;
			for (auto* image : {&hdr, &depth, &display, &shadow, &picking, &pickingDepth}) {
				device->ReleaseTextureView(image->view);
				device->ReleaseTexture(image->texture);
			}
		}
	};

	struct Renderer::Surface {
		struct FrameBuffers {
			BufferHandle lights{}, shadowView{}, debug{};
			uint64_t debugCapacity = 0;
			uint32_t debugVertexCount = 0;
			std::vector<SkinningFrame> skins;
			GpuSceneFrame gpu;
		};
		RHIDevice& device;
		RenderScene scene;
		GameUIPass gameUI;
		std::shared_ptr<const UIDrawData> gameUIDraw;
		RendererFrameContext uniforms;
		std::shared_ptr<Targets> targets;
		std::shared_ptr<Targets> publishedTargets; /* 跳帧/resize时显示帧仍对应旧目标，不能读取未提交的新目标。 */
		std::vector<InstanceGpu> instances;
		std::array<std::shared_ptr<TextureGpuResource>, 4> environment;
		AssetVersionRef<EnvironmentAsset> environmentVersion;
		SamplerHandle environmentSampler{};
		std::vector<FrameBuffers> frames;
		DirectionalShadowView shadow;
		RenderSurfaceSnapshot published;
		std::vector<RenderInstanceId> publishedIdentities;
		std::vector<RenderInstanceId> pendingIdentities;
		std::shared_ptr<TextureGpuResource> previewImage;
		uint64_t token = 0;
		bool imageOnly = false;
		bool prepared = false;
		RHIStatus lastFailure{};
		uint32_t imageMip = 0, imageChannels = 15;
		std::shared_ptr<PipelineResource> skybox, tonemap, present, debug;

		explicit Surface(RHIDevice& value, TextureGpuCache& textures)
		    : device(value), gameUI(value, textures), uniforms(value), frames(value.GetFrameCount()) {}
		~Surface() {
			for (auto& frame : frames) {
				device.ReleaseBuffer(frame.lights);
				device.ReleaseBuffer(frame.shadowView);
				device.ReleaseBuffer(frame.debug);
			}
		}
	};

	struct Renderer::QuerySlot {
		std::shared_ptr<RHIQueryPool> pool;
		RHICompletionPoint completion{};
	};
}
