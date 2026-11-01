# CatalpaEngine 完整文件清单与逐文件职责

整理日期：2026-11-01。配套：[完整架构、Vulkan与第三方库方案](catalpa_engine_architecture.md)。

导航：[完整文件树](#file-tree) · [统计与模块索引](#file-index) · [总体设计](catalpa_engine_architecture.md)

## 阅读规则与范围

项目根目录：`F:\C\work\shader\Engine\CatalpaEngine`。下表路径均相对该根目录；每行是一个具体文件，`.h`与`.cpp`分开列。**这是目标设计清单，不是本轮创建的源代码清单。** 本轮仅新增两份Markdown。

“现有文件”只表示路径存在，职责列仍是目标责任，不意味着当前实现完备；“现有空文件”明确仍为空。“规划文件”尚未创建。阶段表示最早落地时间，高阶优化可延后。

完整范围为本方案自有文件。第三方上游源码由所锁版本管理，不逐文件复制；VS/构建/DDC/Cooked属于生成物，不伪造固定数量，也未删除。具体用户素材不可预先穷举，示例资源已给具体文件名。

P0：基础与正确提交；P1：静态3D；P2：编辑与交付；P3：动态世界；P4：按需扩展。

<a id="file-tree"></a>

## 完整文件树

下树展开本方案全部783个文件条目，不省略实现文件；无后缀标记的叶节点为规划文件，尚未创建。职责、依赖和阶段在后续对应模块表中逐项说明。第三方上游源码和可变生成物的范围说明见本文末尾。

```text
CatalpaEngine/
├── .github/
│   └── workflows/
│       └── windows.yml
├── Catalpa/
│   ├── Resources/
│   │   ├── Materials/
│   │   │   ├── DefaultPbr.material.json
│   │   │   ├── DefaultUnlit.material.json
│   │   │   └── Error.material.json
│   │   ├── Shaders/
│   │   │   ├── Common/
│   │   │   │   ├── Bindings.glsl
│   │   │   │   ├── Lighting.glsl
│   │   │   │   ├── Math.glsl
│   │   │   │   └── Pbr.glsl
│   │   │   ├── DebugDraw.frag
│   │   │   ├── DebugDraw.vert
│   │   │   ├── Depth.vert
│   │   │   ├── DepthPyramid.comp
│   │   │   ├── Fullscreen.vert
│   │   │   ├── GameUI.frag
│   │   │   ├── GameUI.vert
│   │   │   ├── GpuCulling.comp
│   │   │   ├── LightCulling.comp
│   │   │   ├── Mesh.vert
│   │   │   ├── Particles.frag
│   │   │   ├── Particles.vert
│   │   │   ├── Pbr.frag
│   │   │   ├── Picking.frag
│   │   │   ├── Present.frag
│   │   │   ├── Shadow.vert
│   │   │   ├── Skinning.comp
│   │   │   ├── Skybox.frag
│   │   │   ├── Skybox.vert
│   │   │   ├── Terrain.frag
│   │   │   ├── Terrain.vert
│   │   │   ├── Tonemap.frag
│   │   │   ├── Triangle.frag
│   │   │   ├── Triangle.vert
│   │   │   └── Unlit.frag
│   │   ├── Textures/
│   │   │   ├── Error.png
│   │   │   ├── FlatNormal.png
│   │   │   └── White.png
│   │   └── BuiltinAssets.json
│   ├── src/
│   │   ├── Animation/
│   │   │   ├── AnimationAssetLoader.cpp
│   │   │   ├── AnimationAssetLoader.h
│   │   │   ├── AnimationClip.h
│   │   │   ├── AnimationEvent.h
│   │   │   ├── AnimationPlayer.cpp
│   │   │   ├── AnimationPlayer.h
│   │   │   ├── AnimationSampler.cpp
│   │   │   ├── AnimationSampler.h
│   │   │   ├── AnimationTrack.h
│   │   │   ├── AnimatorState.h
│   │   │   ├── BlendTreeAsset.h
│   │   │   ├── BlendTreeEvaluator.cpp
│   │   │   ├── BlendTreeEvaluator.h
│   │   │   ├── Pose.h
│   │   │   ├── PoseBlend.cpp
│   │   │   ├── PoseBlend.h
│   │   │   ├── SkeletonAsset.h
│   │   │   ├── SkeletonValidation.cpp
│   │   │   └── SkeletonValidation.h
│   │   ├── Assets/
│   │   │   ├── AssetChangeEvent.h
│   │   │   ├── AssetCodec.h
│   │   │   ├── AssetContainerFormat.h
│   │   │   ├── AssetContainerReader.cpp
│   │   │   ├── AssetContainerReader.h
│   │   │   ├── AssetDependencyGraph.cpp
│   │   │   ├── AssetDependencyGraph.h
│   │   │   ├── AssetHandle.h
│   │   │   ├── AssetId.h
│   │   │   ├── AssetLoader.h
│   │   │   ├── AssetLoadResult.h
│   │   │   ├── AssetLocator.h
│   │   │   ├── AssetManager.cpp
│   │   │   ├── AssetManager.h
│   │   │   ├── AssetManifest.cpp
│   │   │   ├── AssetManifest.h
│   │   │   ├── AssetRecord.h
│   │   │   ├── AssetRegistry.cpp
│   │   │   ├── AssetRegistry.h
│   │   │   ├── AssetStatus.h
│   │   │   ├── AssetType.h
│   │   │   ├── PackageFormat.h
│   │   │   ├── PackageReader.cpp
│   │   │   └── PackageReader.h
│   │   ├── Audio/
│   │   │   ├── Backends/
│   │   │   │   ├── Miniaudio/
│   │   │   │   │   ├── MiniaudioBackend.cpp
│   │   │   │   │   └── MiniaudioBackend.h
│   │   │   │   ├── Null/
│   │   │   │   │   ├── NullAudioBackend.cpp
│   │   │   │   │   └── NullAudioBackend.h
│   │   │   │   └── IAudioBackend.h
│   │   │   ├── AudioAssetLoader.cpp
│   │   │   ├── AudioAssetLoader.h
│   │   │   ├── AudioClipAsset.h
│   │   │   ├── AudioCommandQueue.cpp
│   │   │   ├── AudioCommandQueue.h
│   │   │   ├── AudioCommands.h
│   │   │   ├── AudioService.cpp
│   │   │   ├── AudioService.h
│   │   │   ├── AudioStreamService.cpp
│   │   │   ├── AudioStreamService.h
│   │   │   └── AudioTypes.h
│   │   ├── Camera/
│   │   │   ├── CameraLens.h
│   │   │   ├── CameraMatrices.h
│   │   │   ├── CameraProjection.cpp
│   │   │   ├── CameraProjection.h
│   │   │   ├── Frustum.cpp
│   │   │   └── Frustum.h
│   │   ├── Core/
│   │   │   ├── Diagnostics/
│   │   │   │   ├── CrashReport.cpp
│   │   │   │   ├── CrashReport.h
│   │   │   │   ├── Profiling.cpp
│   │   │   │   └── Profiling.h
│   │   │   ├── Id/
│   │   │   │   ├── StableId.h
│   │   │   │   ├── StableIdGenerator.cpp
│   │   │   │   └── StableIdGenerator.h
│   │   │   ├── IO/
│   │   │   │   ├── AtomicFileWriter.cpp
│   │   │   │   ├── AtomicFileWriter.h
│   │   │   │   ├── FileWatcher.cpp
│   │   │   │   ├── FileWatcher.h
│   │   │   │   ├── VirtualFileSystem.cpp
│   │   │   │   ├── VirtualFileSystem.h
│   │   │   │   └── VirtualPath.h
│   │   │   ├── Jobs/
│   │   │   │   ├── CancellationToken.h
│   │   │   │   ├── JobHandle.h
│   │   │   │   ├── JobSystem.cpp
│   │   │   │   └── JobSystem.h
│   │   │   ├── Math/
│   │   │   │   ├── CoordinateConvention.h
│   │   │   │   ├── MathConfig.h
│   │   │   │   ├── MathTypes.h
│   │   │   │   ├── TransformMath.cpp
│   │   │   │   └── TransformMath.h
│   │   │   ├── Memory/
│   │   │   │   ├── FrameArena.cpp
│   │   │   │   ├── FrameArena.h
│   │   │   │   ├── GenerationalHandle.h
│   │   │   │   └── MemoryBudget.h
│   │   │   ├── Serialization/
│   │   │   │   ├── Archive.h
│   │   │   │   ├── BinaryArchive.cpp
│   │   │   │   ├── BinaryArchive.h
│   │   │   │   ├── JsonArchive.cpp
│   │   │   │   └── JsonArchive.h
│   │   │   ├── Time/
│   │   │   │   ├── FrameTime.h
│   │   │   │   ├── SimulationClock.cpp
│   │   │   │   └── SimulationClock.h
│   │   │   ├── Application.cpp [现有]
│   │   │   ├── Application.h [现有]
│   │   │   ├── Base.h [现有]
│   │   │   ├── Deltatime.h [现有]
│   │   │   ├── Entry.h [现有]
│   │   │   ├── Input.h [现有]
│   │   │   ├── KeyCodes.h [现有]
│   │   │   ├── MouseCodes.h [现有]
│   │   │   ├── PlatformServices.h
│   │   │   ├── Result.h
│   │   │   ├── Version.h
│   │   │   └── Window.h [现有]
│   │   ├── ECS/
│   │   │   ├── ComponentRegistry.cpp
│   │   │   ├── ComponentRegistry.h
│   │   │   ├── EntityCommandBuffer.cpp
│   │   │   ├── EntityCommandBuffer.h
│   │   │   ├── EntityId.h
│   │   │   ├── System.h
│   │   │   ├── SystemContext.h
│   │   │   ├── SystemScheduler.cpp
│   │   │   ├── SystemScheduler.h
│   │   │   ├── World.cpp
│   │   │   └── World.h
│   │   ├── Events/
│   │   │   ├── ApplicationEvent.h [现有]
│   │   │   ├── Event.h [现有]
│   │   │   ├── KeyEvent.h [现有]
│   │   │   └── MouseEvent.h [现有]
│   │   ├── Geometry/
│   │   │   ├── Bounds.cpp
│   │   │   ├── Bounds.h
│   │   │   ├── GeometryAssetLoader.cpp
│   │   │   ├── GeometryAssetLoader.h
│   │   │   ├── MeshAsset.h
│   │   │   ├── MeshData.h
│   │   │   ├── MeshLod.h
│   │   │   ├── MeshSkinBinding.h
│   │   │   ├── MeshValidation.cpp
│   │   │   ├── MeshValidation.h
│   │   │   ├── ModelAsset.h
│   │   │   ├── PrimitiveMesh.cpp
│   │   │   ├── PrimitiveMesh.h
│   │   │   ├── Submesh.h
│   │   │   └── VertexLayout.h
│   │   ├── Images/
│   │   │   ├── ImageAsset.h
│   │   │   ├── ImageAssetLoader.cpp
│   │   │   ├── ImageAssetLoader.h
│   │   │   ├── ImageData.h
│   │   │   ├── ImageOperations.cpp
│   │   │   ├── ImageOperations.h
│   │   │   ├── ImageTypes.h
│   │   │   ├── ImageValidation.cpp
│   │   │   ├── ImageValidation.h
│   │   │   └── PixelFormat.h
│   │   ├── Layer/
│   │   │   ├── Imgui/
│   │   │   │   ├── ImGuiBuild.cpp [现有]
│   │   │   │   ├── ImGuiLayer.cpp [现有]
│   │   │   │   └── ImGuiLayer.h [现有]
│   │   │   ├── Layer.cpp [现有]
│   │   │   ├── Layer.h [现有]
│   │   │   ├── LayerStack.cpp [现有]
│   │   │   └── LayerStack.h [现有]
│   │   ├── Lighting/
│   │   │   ├── EnvironmentAsset.h
│   │   │   ├── EnvironmentAssetLoader.cpp
│   │   │   ├── EnvironmentAssetLoader.h
│   │   │   ├── EnvironmentSettings.h
│   │   │   ├── LightParameters.h
│   │   │   ├── LightUnits.h
│   │   │   ├── LightValidation.cpp
│   │   │   └── LightValidation.h
│   │   ├── Log/
│   │   │   ├── Log.cpp [现有]
│   │   │   └── Log.h [现有]
│   │   ├── Materials/
│   │   │   ├── MaterialAssetLoader.cpp
│   │   │   ├── MaterialAssetLoader.h
│   │   │   ├── MaterialDefinition.h
│   │   │   ├── MaterialInstance.cpp
│   │   │   ├── MaterialInstance.h
│   │   │   ├── MaterialParameter.h
│   │   │   ├── MaterialRenderState.h
│   │   │   ├── MaterialTypes.h
│   │   │   ├── MaterialValidation.cpp
│   │   │   └── MaterialValidation.h
│   │   ├── Navigation/
│   │   │   ├── Backends/
│   │   │   │   ├── Detour/
│   │   │   │   │   ├── DetourNavigationBackend.cpp
│   │   │   │   │   └── DetourNavigationBackend.h
│   │   │   │   └── INavigationBackend.h
│   │   │   ├── NavigationQuery.h
│   │   │   ├── NavigationTypes.h
│   │   │   ├── NavigationWorld.cpp
│   │   │   ├── NavigationWorld.h
│   │   │   ├── NavMeshAsset.h
│   │   │   ├── NavMeshAssetLoader.cpp
│   │   │   └── NavMeshAssetLoader.h
│   │   ├── Networking/
│   │   │   ├── Backends/
│   │   │   │   └── Loopback/
│   │   │   │       ├── LoopbackTransport.cpp
│   │   │   │       └── LoopbackTransport.h
│   │   │   ├── INetworkTransport.h
│   │   │   ├── NetworkMessage.h
│   │   │   ├── NetworkSession.cpp
│   │   │   ├── NetworkSession.h
│   │   │   └── NetworkTypes.h
│   │   ├── Particles/
│   │   │   ├── ParticleAssetLoader.cpp
│   │   │   ├── ParticleAssetLoader.h
│   │   │   ├── ParticleEffectAsset.h
│   │   │   ├── ParticleSimulation.cpp
│   │   │   ├── ParticleSimulation.h
│   │   │   └── ParticleTypes.h
│   │   ├── Physics/
│   │   │   ├── Backends/
│   │   │   │   ├── Jolt/
│   │   │   │   │   ├── JoltContactListener.cpp
│   │   │   │   │   ├── JoltContactListener.h
│   │   │   │   │   ├── JoltConversions.h
│   │   │   │   │   ├── JoltJobSystemAdapter.cpp
│   │   │   │   │   ├── JoltJobSystemAdapter.h
│   │   │   │   │   ├── JoltPhysicsBackend.cpp
│   │   │   │   │   └── JoltPhysicsBackend.h
│   │   │   │   └── IPhysicsBackend.h
│   │   │   ├── CharacterController.cpp
│   │   │   ├── CharacterController.h
│   │   │   ├── CollisionAssetLoader.cpp
│   │   │   ├── CollisionAssetLoader.h
│   │   │   ├── CollisionShapeAsset.h
│   │   │   ├── PhysicsEvents.h
│   │   │   ├── PhysicsMaterialAsset.h
│   │   │   ├── PhysicsQuery.h
│   │   │   ├── PhysicsService.cpp
│   │   │   ├── PhysicsService.h
│   │   │   ├── PhysicsTypes.h
│   │   │   ├── PhysicsWorld.cpp
│   │   │   └── PhysicsWorld.h
│   │   ├── Platform/
│   │   │   ├── OpenGL/
│   │   │   │   └── OpenGL.h [现有空文件]
│   │   │   ├── Vulkan/
│   │   │   │   ├── Integrations/
│   │   │   │   │   ├── VulkanImGuiBackend.cpp
│   │   │   │   │   └── VulkanImGuiBackend.h
│   │   │   │   ├── Vulkan.h [现有空文件]
│   │   │   │   ├── VulkanBackend.cpp
│   │   │   │   ├── VulkanBackend.h
│   │   │   │   ├── VulkanBarrierCompiler.cpp
│   │   │   │   ├── VulkanBarrierCompiler.h
│   │   │   │   ├── VulkanBinarySemaphore.cpp
│   │   │   │   ├── VulkanBinarySemaphore.h
│   │   │   │   ├── VulkanBinding.cpp
│   │   │   │   ├── VulkanBinding.h
│   │   │   │   ├── VulkanBuffer.cpp
│   │   │   │   ├── VulkanBuffer.h
│   │   │   │   ├── VulkanCommandList.cpp
│   │   │   │   ├── VulkanCommandList.h
│   │   │   │   ├── VulkanCommandPool.cpp
│   │   │   │   ├── VulkanCommandPool.h
│   │   │   │   ├── VulkanConversions.cpp
│   │   │   │   ├── VulkanConversions.h
│   │   │   │   ├── VulkanDebugUtils.cpp
│   │   │   │   ├── VulkanDebugUtils.h
│   │   │   │   ├── VulkanDescriptorAllocator.cpp
│   │   │   │   ├── VulkanDescriptorAllocator.h
│   │   │   │   ├── VulkanDevice.cpp
│   │   │   │   ├── VulkanDevice.h
│   │   │   │   ├── VulkanFence.cpp
│   │   │   │   ├── VulkanFence.h
│   │   │   │   ├── VulkanFrameContext.cpp
│   │   │   │   ├── VulkanFrameContext.h
│   │   │   │   ├── VulkanHeaders.h
│   │   │   │   ├── VulkanInstance.cpp
│   │   │   │   ├── VulkanInstance.h
│   │   │   │   ├── VulkanLoader.cpp
│   │   │   │   ├── VulkanLoader.h
│   │   │   │   ├── VulkanMemoryAllocator.cpp
│   │   │   │   ├── VulkanMemoryAllocator.h
│   │   │   │   ├── VulkanPhysicalDevice.cpp
│   │   │   │   ├── VulkanPhysicalDevice.h
│   │   │   │   ├── VulkanPipeline.cpp
│   │   │   │   ├── VulkanPipeline.h
│   │   │   │   ├── VulkanPipelineCache.cpp
│   │   │   │   ├── VulkanPipelineCache.h
│   │   │   │   ├── VulkanQueryPool.cpp
│   │   │   │   ├── VulkanQueryPool.h
│   │   │   │   ├── VulkanQueue.cpp
│   │   │   │   ├── VulkanQueue.h
│   │   │   │   ├── VulkanResourceRegistry.cpp
│   │   │   │   ├── VulkanResourceRegistry.h
│   │   │   │   ├── VulkanResourceRetirement.cpp
│   │   │   │   ├── VulkanResourceRetirement.h
│   │   │   │   ├── VulkanResourceStateTracker.cpp
│   │   │   │   ├── VulkanResourceStateTracker.h
│   │   │   │   ├── VulkanSampler.cpp
│   │   │   │   ├── VulkanSampler.h
│   │   │   │   ├── VulkanShader.cpp
│   │   │   │   ├── VulkanShader.h
│   │   │   │   ├── VulkanSubmissionTracker.cpp
│   │   │   │   ├── VulkanSubmissionTracker.h
│   │   │   │   ├── VulkanSurface.cpp
│   │   │   │   ├── VulkanSurface.h
│   │   │   │   ├── VulkanSwapchain.cpp
│   │   │   │   ├── VulkanSwapchain.h
│   │   │   │   ├── VulkanTexture.cpp
│   │   │   │   ├── VulkanTexture.h
│   │   │   │   ├── VulkanTimelineSemaphore.cpp
│   │   │   │   └── VulkanTimelineSemaphore.h
│   │   │   ├── Windows/
│   │   │   │   ├── WindowsFileWatcher.cpp
│   │   │   │   ├── WindowsFileWatcher.h
│   │   │   │   ├── WindowsPlatform.cpp
│   │   │   │   └── WindowsPlatform.h
│   │   │   └── WindowsWindow/
│   │   │       ├── WindowsInput.cpp [现有]
│   │   │       ├── WindowsInput.h [现有]
│   │   │       ├── WindowsWindow.cpp [现有]
│   │   │       └── WindowsWindow.h [现有]
│   │   ├── Renderer/
│   │   │   ├── Lighting/
│   │   │   │   ├── LightCulling.cpp
│   │   │   │   ├── LightCulling.h
│   │   │   │   └── LightData.h
│   │   │   ├── Passes/
│   │   │   │   ├── DebugDrawPass.cpp
│   │   │   │   ├── DebugDrawPass.h
│   │   │   │   ├── DepthPrepass.cpp
│   │   │   │   ├── DepthPrepass.h
│   │   │   │   ├── DepthPyramidPass.cpp
│   │   │   │   ├── DepthPyramidPass.h
│   │   │   │   ├── ForwardPass.cpp
│   │   │   │   ├── ForwardPass.h
│   │   │   │   ├── GameUIPass.cpp
│   │   │   │   ├── GameUIPass.h
│   │   │   │   ├── GpuCullingPass.cpp
│   │   │   │   ├── GpuCullingPass.h
│   │   │   │   ├── LightCullingPass.cpp
│   │   │   │   ├── LightCullingPass.h
│   │   │   │   ├── ParticlePass.cpp
│   │   │   │   ├── ParticlePass.h
│   │   │   │   ├── PickingPass.cpp
│   │   │   │   ├── PickingPass.h
│   │   │   │   ├── PresentPass.cpp
│   │   │   │   ├── PresentPass.h
│   │   │   │   ├── ShadowPass.cpp
│   │   │   │   ├── ShadowPass.h
│   │   │   │   ├── SkinningPass.cpp
│   │   │   │   ├── SkinningPass.h
│   │   │   │   ├── SkyboxPass.cpp
│   │   │   │   ├── SkyboxPass.h
│   │   │   │   ├── TerrainPass.cpp
│   │   │   │   ├── TerrainPass.h
│   │   │   │   ├── TonemapPass.cpp
│   │   │   │   ├── TonemapPass.h
│   │   │   │   ├── TransparentPass.cpp
│   │   │   │   └── TransparentPass.h
│   │   │   ├── Readback/
│   │   │   │   ├── ReadbackManager.cpp
│   │   │   │   └── ReadbackManager.h
│   │   │   ├── RenderGraph/
│   │   │   │   ├── RenderGraph.cpp
│   │   │   │   ├── RenderGraph.h
│   │   │   │   ├── RenderGraphBuilder.cpp
│   │   │   │   ├── RenderGraphBuilder.h
│   │   │   │   ├── RenderGraphCompiler.cpp
│   │   │   │   ├── RenderGraphCompiler.h
│   │   │   │   ├── RenderGraphExecutor.cpp
│   │   │   │   ├── RenderGraphExecutor.h
│   │   │   │   ├── RenderGraphPass.h
│   │   │   │   ├── RenderGraphPlan.h
│   │   │   │   ├── RenderGraphResource.h
│   │   │   │   ├── RenderGraphTransientResources.cpp
│   │   │   │   └── RenderGraphTransientResources.h
│   │   │   ├── Resources/
│   │   │   │   ├── GeometryGpuCache.cpp
│   │   │   │   ├── GeometryGpuCache.h
│   │   │   │   ├── MaterialGpuCache.cpp
│   │   │   │   ├── MaterialGpuCache.h
│   │   │   │   ├── PipelineLibrary.cpp
│   │   │   │   ├── PipelineLibrary.h
│   │   │   │   ├── TextureGpuCache.cpp
│   │   │   │   └── TextureGpuCache.h
│   │   │   ├── UI/
│   │   │   │   ├── ImGuiBackend.h
│   │   │   │   ├── ImGuiRenderer.cpp
│   │   │   │   └── ImGuiRenderer.h
│   │   │   ├── Uploads/
│   │   │   │   ├── UploadScheduler.cpp
│   │   │   │   └── UploadScheduler.h
│   │   │   ├── Visibility/
│   │   │   │   ├── VisibilitySystem.cpp
│   │   │   │   └── VisibilitySystem.h
│   │   │   ├── DebugDraw.cpp
│   │   │   ├── DebugDraw.h
│   │   │   ├── FrameScheduler.cpp
│   │   │   ├── FrameScheduler.h
│   │   │   ├── Renderer.cpp
│   │   │   ├── Renderer.h [现有空文件]
│   │   │   ├── RendererConfig.h
│   │   │   ├── RendererFrameContext.cpp
│   │   │   ├── RendererFrameContext.h
│   │   │   ├── RenderFramePacket.h
│   │   │   ├── RenderScene.cpp
│   │   │   ├── RenderScene.h
│   │   │   ├── RenderSurface.cpp
│   │   │   ├── RenderSurface.h
│   │   │   └── RenderView.h
│   │   ├── RHI/
│   │   │   ├── RHIBackend.h
│   │   │   ├── RHIBackendFactory.cpp
│   │   │   ├── RHIBackendFactory.h
│   │   │   ├── RHIBindingDesc.h
│   │   │   ├── RHIBufferDesc.h
│   │   │   ├── RHICapabilities.h
│   │   │   ├── RHICommandList.h
│   │   │   ├── RHICommandTypes.h
│   │   │   ├── RHICompletionPoint.h
│   │   │   ├── RHICompletionSet.h
│   │   │   ├── RHIDevice.h
│   │   │   ├── RHIDeviceDesc.h
│   │   │   ├── RHIError.h
│   │   │   ├── RHIFormat.h
│   │   │   ├── RHIGpuDependency.h
│   │   │   ├── RHIHandle.h
│   │   │   ├── RHIPipelineDesc.h
│   │   │   ├── RHIQueryDesc.h
│   │   │   ├── RHIQueue.h
│   │   │   ├── RHIRenderingDesc.h
│   │   │   ├── RHIResourceAccess.h
│   │   │   ├── RHISamplerDesc.h
│   │   │   ├── RHIShaderDesc.h
│   │   │   ├── RHISubmission.h
│   │   │   ├── RHISwapchain.h
│   │   │   ├── RHISwapchainDesc.h
│   │   │   ├── RHITextureDesc.h
│   │   │   └── RHITypes.h
│   │   ├── Runtime/
│   │   │   ├── Input/
│   │   │   │   ├── ActionMap.h
│   │   │   │   ├── InputActions.cpp
│   │   │   │   └── InputActions.h
│   │   │   ├── Systems/
│   │   │   │   ├── AnimationSystem.cpp
│   │   │   │   ├── AnimationSystem.h
│   │   │   │   ├── AudioExtractSystem.cpp
│   │   │   │   ├── AudioExtractSystem.h
│   │   │   │   ├── CameraSystem.cpp
│   │   │   │   ├── CameraSystem.h
│   │   │   │   ├── NativeGameplaySystem.cpp
│   │   │   │   ├── NativeGameplaySystem.h
│   │   │   │   ├── NavigationSystem.cpp
│   │   │   │   ├── NavigationSystem.h
│   │   │   │   ├── ParticleSystem.cpp
│   │   │   │   ├── ParticleSystem.h
│   │   │   │   ├── PhysicsSystem.cpp
│   │   │   │   ├── PhysicsSystem.h
│   │   │   │   ├── RenderExtractSystem.cpp
│   │   │   │   ├── RenderExtractSystem.h
│   │   │   │   ├── ScriptSystem.cpp
│   │   │   │   ├── ScriptSystem.h
│   │   │   │   ├── TerrainSystem.cpp
│   │   │   │   ├── TerrainSystem.h
│   │   │   │   ├── TransformSystem.cpp
│   │   │   │   ├── TransformSystem.h
│   │   │   │   ├── UISystem.cpp
│   │   │   │   └── UISystem.h
│   │   │   ├── EngineConfig.h
│   │   │   ├── EngineRuntime.cpp
│   │   │   ├── EngineRuntime.h
│   │   │   ├── GameplayModule.cpp
│   │   │   ├── GameplayModule.h
│   │   │   ├── RuntimeRegistration.cpp
│   │   │   ├── RuntimeRegistration.h
│   │   │   └── RuntimeServices.h
│   │   ├── Scene/
│   │   │   ├── Components/
│   │   │   │   ├── AnimatorComponent.h
│   │   │   │   ├── AudioListenerComponent.h
│   │   │   │   ├── AudioSourceComponent.h
│   │   │   │   ├── CameraComponent.h
│   │   │   │   ├── CharacterControllerComponent.h
│   │   │   │   ├── ColliderComponent.h
│   │   │   │   ├── EnvironmentComponent.h
│   │   │   │   ├── IdentityComponent.h
│   │   │   │   ├── LightComponent.h
│   │   │   │   ├── MeshRendererComponent.h
│   │   │   │   ├── NavigationAgentComponent.h
│   │   │   │   ├── ParticleEmitterComponent.h
│   │   │   │   ├── RelationshipComponent.h
│   │   │   │   ├── RigidBodyComponent.h
│   │   │   │   ├── ScriptComponent.h
│   │   │   │   ├── SkinnedMeshRendererComponent.h
│   │   │   │   ├── TerrainComponent.h
│   │   │   │   ├── TransformComponent.h
│   │   │   │   └── UIComponent.h
│   │   │   ├── EntityReference.h
│   │   │   ├── ModelInstantiation.cpp
│   │   │   ├── ModelInstantiation.h
│   │   │   ├── PersistentEntityId.h
│   │   │   ├── PrefabAsset.h
│   │   │   ├── PrefabInstantiation.cpp
│   │   │   ├── PrefabInstantiation.h
│   │   │   ├── Scene.cpp
│   │   │   ├── Scene.h
│   │   │   ├── SceneAsset.h
│   │   │   ├── SceneHandle.h
│   │   │   ├── SceneManager.cpp
│   │   │   ├── SceneManager.h
│   │   │   ├── SceneSerializer.cpp
│   │   │   └── SceneSerializer.h
│   │   ├── Scripting/
│   │   │   ├── Backends/
│   │   │   │   ├── Lua/
│   │   │   │   │   ├── LuaBindings.cpp
│   │   │   │   │   ├── LuaBindings.h
│   │   │   │   │   ├── LuaScriptBackend.cpp
│   │   │   │   │   └── LuaScriptBackend.h
│   │   │   │   └── IScriptBackend.h
│   │   │   ├── ScriptApi.h
│   │   │   ├── ScriptAsset.h
│   │   │   ├── ScriptAssetLoader.cpp
│   │   │   ├── ScriptAssetLoader.h
│   │   │   ├── ScriptCommands.h
│   │   │   ├── ScriptRuntime.cpp
│   │   │   ├── ScriptRuntime.h
│   │   │   └── ScriptValue.h
│   │   ├── Shaders/
│   │   │   ├── ShaderAsset.h
│   │   │   ├── ShaderLibrary.cpp
│   │   │   ├── ShaderLibrary.h
│   │   │   ├── ShaderMetadata.h
│   │   │   └── ShaderVariantKey.h
│   │   ├── Terrain/
│   │   │   ├── TerrainAsset.h
│   │   │   ├── TerrainAssetLoader.cpp
│   │   │   ├── TerrainAssetLoader.h
│   │   │   ├── TerrainService.cpp
│   │   │   ├── TerrainService.h
│   │   │   ├── TerrainTileStreamer.cpp
│   │   │   ├── TerrainTileStreamer.h
│   │   │   └── TerrainTypes.h
│   │   ├── UI/
│   │   │   ├── Backends/
│   │   │   │   ├── RmlUi/
│   │   │   │   │   ├── RmlUiBackend.cpp
│   │   │   │   │   ├── RmlUiBackend.h
│   │   │   │   │   ├── RmlUiFileInterface.cpp
│   │   │   │   │   ├── RmlUiFileInterface.h
│   │   │   │   │   ├── RmlUiRenderInterface.cpp
│   │   │   │   │   ├── RmlUiRenderInterface.h
│   │   │   │   │   ├── RmlUiSystemInterface.cpp
│   │   │   │   │   └── RmlUiSystemInterface.h
│   │   │   │   └── IUIBackend.h
│   │   │   ├── FontAsset.h
│   │   │   ├── UIAssetLoader.cpp
│   │   │   ├── UIAssetLoader.h
│   │   │   ├── UIContext.cpp
│   │   │   ├── UIContext.h
│   │   │   ├── UIDocumentAsset.h
│   │   │   ├── UIDrawData.h
│   │   │   └── UIEvent.h
│   │   ├── Catalpa.h [现有]
│   │   ├── catpch.cpp [现有]
│   │   └── catpch.h [现有]
│   └── third_party/
│       ├── glad/
│       │   └── generator.json
│       └── README.md
├── CatalpaBox/
│   └── src/
│       ├── BoxGameplay.cpp
│       ├── BoxGameplay.h
│       └── catalpaBox.cpp [现有]
├── CatalpaEditor/
│   └── src/
│       ├── Application/
│       │   ├── EditorApplication.cpp
│       │   ├── EditorApplication.h
│       │   ├── EditorProject.cpp
│       │   ├── EditorProject.h
│       │   ├── EditorSelection.cpp
│       │   └── EditorSelection.h
│       ├── AssetEditors/
│       │   ├── AnimationPreviewEditor.cpp
│       │   ├── AnimationPreviewEditor.h
│       │   ├── AssetEditorRegistry.cpp
│       │   ├── AssetEditorRegistry.h
│       │   ├── AudioPreviewEditor.cpp
│       │   ├── AudioPreviewEditor.h
│       │   ├── IAssetEditor.h
│       │   ├── MaterialEditor.cpp
│       │   ├── MaterialEditor.h
│       │   ├── TexturePreviewEditor.cpp
│       │   └── TexturePreviewEditor.h
│       ├── Commands/
│       │   ├── CommandHistory.cpp
│       │   ├── CommandHistory.h
│       │   ├── EditorCommand.h
│       │   ├── SceneCommands.cpp
│       │   └── SceneCommands.h
│       ├── Documents/
│       │   ├── Document.h
│       │   ├── DocumentManager.cpp
│       │   ├── DocumentManager.h
│       │   ├── PrefabDocument.cpp
│       │   ├── PrefabDocument.h
│       │   ├── SceneDocument.cpp
│       │   └── SceneDocument.h
│       ├── Panels/
│       │   ├── ConsolePanel.cpp
│       │   ├── ConsolePanel.h
│       │   ├── ContentBrowserPanel.cpp
│       │   ├── ContentBrowserPanel.h
│       │   ├── HierarchyPanel.cpp
│       │   ├── HierarchyPanel.h
│       │   ├── InspectorPanel.cpp
│       │   ├── InspectorPanel.h
│       │   ├── ProjectSettingsPanel.cpp
│       │   ├── ProjectSettingsPanel.h
│       │   ├── SceneViewportPanel.cpp
│       │   └── SceneViewportPanel.h
│       ├── Viewport/
│       │   ├── EditorCameraController.cpp
│       │   ├── EditorCameraController.h
│       │   ├── PickingController.cpp
│       │   ├── PickingController.h
│       │   ├── TransformGizmo.cpp
│       │   └── TransformGizmo.h
│       └── main.cpp
├── CatalpaPlayer/
│   └── src/
│       ├── main.cpp
│       ├── PlayerHost.cpp
│       └── PlayerHost.h
├── Config/
│   ├── AssetBuildProfiles.json
│   ├── AudioDefaults.json
│   ├── EngineDefaults.json
│   ├── PackageProfile.json
│   └── RendererProfiles.json
├── docx/
│   ├── catalpa_engine_architecture.md [本轮新增文档]
│   ├── catalpa_engine_file_manifest.md [本轮新增文档]
│   └── deepseek_vulkan_arch.md [现有]
├── Projects/
│   └── Sandbox/
│       ├── Content/
│       │   ├── Audio/
│       │   │   └── Example.wav
│       │   ├── Materials/
│       │   │   └── Example.material.json
│       │   ├── Models/
│       │   │   ├── ExampleMesh.bin
│       │   │   └── ExampleMesh.gltf
│       │   ├── Prefabs/
│       │   │   └── ExampleObject.prefab.json
│       │   ├── Scenes/
│       │   │   ├── DynamicScene.scene.json
│       │   │   └── StaticScene.scene.json
│       │   ├── Scripts/
│       │   │   └── Example.lua
│       │   ├── Textures/
│       │   │   └── Checker.png
│       │   ├── UI/
│       │   │   ├── Hud.rcss
│       │   │   └── Hud.rml
│       │   └── asset_metadata.json
│       └── Sandbox.catalpaproj
├── Schemas/
│   ├── asset-metadata.schema.json
│   ├── material.schema.json
│   ├── package-manifest.schema.json
│   ├── project.schema.json
│   └── scene.schema.json
├── Script/
│   ├── Premake/
│   │   ├── Dependencies.lua
│   │   ├── Options.lua
│   │   └── Targets.lua
│   ├── build.ps1
│   ├── build_assets.py
│   ├── check_architecture.py
│   ├── check_public_headers.py
│   ├── generate_project.ps1
│   └── package_player.py
├── Tests/
│   ├── Animation/
│   │   └── AnimationSamplingTests.cpp
│   ├── Assets/
│   │   ├── AssetLifecycleTests.cpp
│   │   └── PackageValidationTests.cpp
│   ├── Audio/
│   │   └── AudioLifecycleTests.cpp
│   ├── Camera/
│   │   └── ProjectionFrustumTests.cpp
│   ├── Core/
│   │   ├── ArchiveBoundsTests.cpp
│   │   ├── HandleTests.cpp
│   │   └── JobShutdownTests.cpp
│   ├── ECS/
│   │   ├── SystemSchedulerTests.cpp
│   │   └── WorldTests.cpp
│   ├── Editor/
│   │   └── UndoRedoTests.cpp
│   ├── Fixtures/
│   │   ├── InvalidScene.scene.json
│   │   └── MaterialTypeMismatch.material.json
│   ├── Geometry/
│   │   └── MeshValidationTests.cpp
│   ├── Integration/
│   │   └── PlayerPackageTests.cpp
│   ├── Materials/
│   │   └── MaterialSchemaTests.cpp
│   ├── Networking/
│   │   └── LoopbackTests.cpp
│   ├── Physics/
│   │   └── TransformAuthorityTests.cpp
│   ├── Renderer/
│   │   ├── FrameLifetimeTests.cpp
│   │   ├── MultiQueueTests.cpp
│   │   └── RenderGraphHazardTests.cpp
│   ├── Scene/
│   │   ├── PrefabTests.cpp
│   │   ├── SceneRoundTripTests.cpp
│   │   └── TransformHierarchyTests.cpp
│   ├── Support/
│   │   ├── FakeRHI.h
│   │   └── TestAssets.h
│   ├── Tools/
│   │   ├── CookReproducibilityTests.cpp
│   │   └── ShaderMetadataTests.cpp
│   ├── Vulkan/
│   │   ├── RetirementTests.cpp
│   │   ├── SwapchainLifecycleTests.cpp
│   │   └── ValidationSmokeTests.cpp
│   └── main.cpp
├── Tools/
│   ├── AssetCompiler/
│   │   ├── AssetCompilerOptions.cpp
│   │   ├── AssetCompilerOptions.h
│   │   └── main.cpp
│   ├── AssetInspector/
│   │   ├── AssetInspection.cpp
│   │   ├── AssetInspection.h
│   │   └── main.cpp
│   ├── AssetPipeline/
│   │   ├── Cookers/
│   │   │   ├── AnimationCooker.cpp
│   │   │   ├── AnimationCooker.h
│   │   │   ├── AudioCooker.cpp
│   │   │   ├── AudioCooker.h
│   │   │   ├── CollisionShapeCooker.cpp
│   │   │   ├── CollisionShapeCooker.h
│   │   │   ├── EnvironmentCooker.cpp
│   │   │   ├── EnvironmentCooker.h
│   │   │   ├── MaterialCooker.cpp
│   │   │   ├── MaterialCooker.h
│   │   │   ├── MeshCooker.cpp
│   │   │   ├── MeshCooker.h
│   │   │   ├── ModelCooker.cpp
│   │   │   ├── ModelCooker.h
│   │   │   ├── ParticleEffectCooker.cpp
│   │   │   ├── ParticleEffectCooker.h
│   │   │   ├── PrefabCooker.cpp
│   │   │   ├── PrefabCooker.h
│   │   │   ├── SceneCooker.cpp
│   │   │   ├── SceneCooker.h
│   │   │   ├── ScriptCooker.cpp
│   │   │   ├── ScriptCooker.h
│   │   │   ├── ShaderCooker.cpp
│   │   │   ├── ShaderCooker.h
│   │   │   ├── SkeletonCooker.cpp
│   │   │   ├── SkeletonCooker.h
│   │   │   ├── TerrainCooker.cpp
│   │   │   ├── TerrainCooker.h
│   │   │   ├── TextureCooker.cpp
│   │   │   ├── TextureCooker.h
│   │   │   ├── UIDocumentCooker.cpp
│   │   │   └── UIDocumentCooker.h
│   │   ├── Importers/
│   │   │   ├── AnimationImporter.cpp
│   │   │   ├── AnimationImporter.h
│   │   │   ├── AudioImporter.cpp
│   │   │   ├── AudioImporter.h
│   │   │   ├── GltfImporter.cpp
│   │   │   ├── GltfImporter.h
│   │   │   ├── MaterialImporter.cpp
│   │   │   ├── MaterialImporter.h
│   │   │   ├── ParticleEffectImporter.cpp
│   │   │   ├── ParticleEffectImporter.h
│   │   │   ├── PrefabImporter.cpp
│   │   │   ├── PrefabImporter.h
│   │   │   ├── SceneImporter.cpp
│   │   │   ├── SceneImporter.h
│   │   │   ├── ScriptImporter.cpp
│   │   │   ├── ScriptImporter.h
│   │   │   ├── ShaderImporter.cpp
│   │   │   ├── ShaderImporter.h
│   │   │   ├── SkeletonImporter.cpp
│   │   │   ├── SkeletonImporter.h
│   │   │   ├── TerrainImporter.cpp
│   │   │   ├── TerrainImporter.h
│   │   │   ├── TextureImporter.cpp
│   │   │   ├── TextureImporter.h
│   │   │   ├── UIDocumentImporter.cpp
│   │   │   └── UIDocumentImporter.h
│   │   ├── Navigation/
│   │   │   ├── RecastNavMeshBaker.cpp
│   │   │   └── RecastNavMeshBaker.h
│   │   ├── AssetArchiveWriter.cpp
│   │   ├── AssetArchiveWriter.h
│   │   ├── AssetBuildCache.cpp
│   │   ├── AssetBuildCache.h
│   │   ├── AssetBuildContext.cpp
│   │   ├── AssetBuildContext.h
│   │   ├── AssetBuildTypes.h
│   │   ├── AssetCooker.cpp
│   │   ├── AssetCooker.h
│   │   ├── AssetDependencyScanner.cpp
│   │   ├── AssetDependencyScanner.h
│   │   ├── AssetImportRegistry.cpp
│   │   ├── AssetImportRegistry.h
│   │   └── IAssetImporter.h
│   ├── Packager/
│   │   ├── main.cpp
│   │   ├── PackageBuilder.cpp
│   │   └── PackageBuilder.h
│   └── ShaderCompiler/
│       ├── GlslangCompiler.cpp
│       ├── GlslangCompiler.h
│       ├── main.cpp
│       ├── ShaderCompileJob.cpp
│       ├── ShaderCompileJob.h
│       ├── SpirvReflection.cpp
│       ├── SpirvReflection.h
│       ├── SpirvValidator.cpp
│       └── SpirvValidator.h
├── .clang-format
├── .editorconfig
├── .gitignore
├── build_project.bat [现有]
├── Dependencies.lock.json
├── premake5.lua [现有]
├── README.md
├── ThirdPartyNotices.md
├── vcpkg-configuration.json
└── vcpkg.json
```

<a id="file-index"></a>

## 统计与模块索引

共 **783个文件条目**；当前自有源码覆盖 **33/33**。状态：现有文件 33，规划文件 745，现有空文件 3，本轮新增文档 2。

阶段分布：P0 217，P1 218，P2 126，P3 151，P4 71。

| 编号 | 模块 | 文件数 |
|---|---|---:|
| 01 | [Build](#module-01) | 24 |
| 02 | [PublicAndPCH](#module-02) | 1 |
| 03 | [Core](#module-03) | 47 |
| 04 | [Log](#module-04) | 2 |
| 05 | [Events](#module-05) | 4 |
| 06 | [Layer](#module-06) | 7 |
| 07 | [ECS](#module-07) | 11 |
| 08 | [Scene](#module-08) | 34 |
| 09 | [Runtime](#module-09) | 35 |
| 10 | [Assets](#module-10) | 24 |
| 11 | [Geometry](#module-11) | 15 |
| 12 | [Images](#module-12) | 10 |
| 13 | [Materials](#module-13) | 10 |
| 14 | [Camera](#module-14) | 6 |
| 15 | [Lighting](#module-15) | 8 |
| 16 | [Animation](#module-16) | 19 |
| 17 | [Physics](#module-17) | 21 |
| 18 | [Audio](#module-18) | 16 |
| 19 | [Navigation](#module-19) | 10 |
| 20 | [Scripting](#module-20) | 13 |
| 21 | [UI](#module-21) | 17 |
| 22 | [Particles](#module-22) | 6 |
| 23 | [Terrain](#module-23) | 8 |
| 24 | [Networking](#module-24) | 7 |
| 25 | [Shaders](#module-25) | 5 |
| 26 | [RHI](#module-26) | 28 |
| 27 | [Renderer](#module-27) | 80 |
| 28 | [Platform](#module-28) | 75 |
| 29 | [CatalpaEditor](#module-29) | 48 |
| 30 | [Tools/AssetPipeline](#module-30) | 74 |
| 31 | [Tools/AssetCompiler](#module-31) | 3 |
| 32 | [Tools/ShaderCompiler](#module-32) | 9 |
| 33 | [Tools/Packager](#module-33) | 3 |
| 34 | [Tools/AssetInspector](#module-34) | 3 |
| 35 | [Applications](#module-35) | 6 |
| 36 | [ConfigurationAndContent](#module-36) | 23 |
| 37 | [BuiltinResources](#module-37) | 36 |
| 38 | [Tests](#module-38) | 32 |
| 39 | [Documentation](#module-39) | 3 |

目录不是DLL一一映射。Core/Application与Public入口属于高层组合目标；基础Core目标不得依赖它们。表中依赖边界与正文共同构成实现契约。

<a id="module-01"></a>

## 01. Build

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/catpch.h` | 现有文件 | P0 | 保留工程预编译头，限于稳定常用标准库和基础设施头，减少领域相互包含。 | 不得把Vulkan、Editor或所有Components全量塞入预编译头掩盖真实依赖。 |
| `Catalpa/src/catpch.cpp` | 现有文件 | P0 | 保留预编译头生成翻译单元，保持构建机制与现有Premake工程一致。 | 只包含catpch.h，不引入运行时初始化副作用。 |
| `premake5.lua` | 现有文件 | P0 | 保留workspace；按模块定义源文件、私有include和依赖，正式发布配置单独校验。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `build_project.bat` | 现有文件 | P0 | 保留Windows生成入口，报告失败，不隐式清理用户文件。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `README.md` | 规划文件 | P0 | 记录工具链、构建/运行与架构文档索引。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `.gitignore` | 规划文件 | P0 | 声明生成物忽略范围，不删除现有文件。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `.editorconfig` | 规划文件 | P0 | 统一UTF-8/换行/缩进。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `.clang-format` | 规划文件 | P0 | 代码格式配置，不要求全量重排旧代码。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Dependencies.lock.json` | 规划文件 | P0 | 记录库来源/tag/commit/校验/选项及runtime/tools/dev归属。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `ThirdPartyNotices.md` | 规划文件 | P0 | 记录实际分发版本的许可与传递依赖notices。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `vcpkg.json` | 规划文件 | P2 | 声明由vcpkg管理的依赖/features，避免与vendor重复。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `vcpkg-configuration.json` | 规划文件 | P2 | 固定registry/baseline以复现构建。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/Premake/Options.lua` | 规划文件 | P0 | 定义后端/工具/profiling开关并拒绝不合法组合。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/Premake/Dependencies.lua` | 规划文件 | P0 | 统一第三方路径/宏/CRT，不全量导出include。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/Premake/Targets.lua` | 规划文件 | P0 | 维护各模块目标、ShaderCompilerCore/CLI及独立ImGui适配依赖。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/generate_project.ps1` | 规划文件 | P0 | 调用锁定Premake并检查生成结果。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/build.ps1` | 规划文件 | P0 | 按明确目标/配置/工具链构建并报告退出码。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/check_public_headers.py` | 规划文件 | P1 | 编译临时单头探针，检查无PCH时公共头自包含。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/check_architecture.py` | 规划文件 | P1 | 检查后端泄露/依赖环/runtime反向依赖Editor。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/build_assets.py` | 规划文件 | P1 | 调用锁定AssetCompiler，隔离源/缓存/cooked输出。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Script/package_player.py` | 规划文件 | P2 | 按manifest导出运行包，不通过删除源码整理发行目录。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `.github/workflows/windows.yml` | 规划文件 | P2 | 构建和CPU测试；GPU集成使用另配硬件runner。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Catalpa/third_party/README.md` | 规划文件 | P0 | 说明vendor来源和与包管理的边界；上游源由锁版本管理。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |
| `Catalpa/third_party/glad/generator.json` | 规划文件 | P0 | 固定Vulkan loader的生成器/API/扩展列表，新增入口可复现。 | 构建/文档设施；本轮未创建或执行此规划脚本。 |

<a id="module-02"></a>

## 02. PublicAndPCH

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Catalpa.h` | 现有文件 | P0 | 保留客户端聚合头，只导出稳定应用与运行时入口，避免包含所有领域实现和后端头。 | 允许聚合稳定公共契约；不公开Vk类型、第三方设备对象或内部ECS存储布局。 |

<a id="module-03"></a>

## 03. Core

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Core/Base.h` | 现有文件 | P0 | 保留现有DLL导出、基础宏和绑定约定；明确断言仅用于程序不变量，不替代可恢复错误返回。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Application.h` | 现有文件 | P0 | 保留当前位置，定义应用主循环、窗口、LayerStack及EngineRuntime的所有权；这是Core中的高层编排特例。 | 允许依赖Window、Layer、Runtime公共入口；禁止直接持有Vk对象或承担资产加载算法。 |
| `Catalpa/src/Core/Application.cpp` | 现有文件 | P0 | 实现平台事件泵、应用启停与引擎tick调用，按显式顺序停止任务、释放运行时服务再销毁窗口。 | 允许依赖Window、Layer、Runtime公共入口；禁止直接持有Vk对象或承担资产加载算法。 |
| `Catalpa/src/Core/Window.h` | 现有文件 | P0 | 保留窗口接口和创建入口，声明逻辑尺寸、framebuffer像素尺寸、事件及原生窗口借用契约。 | 依赖基础类型与Events；不创建图形设备，VSync只表达呈现偏好并交由呈现服务兑现。 |
| `Catalpa/src/Core/Input.h` | 现有文件 | P0 | 保留物理键鼠查询入口，定义输入快照与焦点语义；游戏action映射由Gameplay层完成。 | 依赖KeyCodes、MouseCodes与基础类型；不依赖World、Editor或Vulkan。 |
| `Catalpa/src/Core/KeyCodes.h` | 现有文件 | P0 | 保留平台无关键码定义及必要转换边界，避免业务层散布GLFW常量。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/MouseCodes.h` | 现有文件 | P0 | 保留鼠标按键标识，明确与滚轮轴、光标位置等连续数据的区别。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Entry.h` | 现有文件 | P0 | 保留CreateApplication入口适配；建立日志与异常/错误出口，保证应用对象有明确所有权。 | 只依赖Application及Log公共入口；不得在头文件中初始化Vulkan或音频设备。 |
| `Catalpa/src/Core/Deltatime.h` | 现有文件 | P0 | 保留现有时间值类型，明确内部秒单位及与SimulationClock的关系，避免重复引入另一套TimeStep。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Result.h` | 规划文件 | P0 | 定义结构化成功/失败结果、错误类别与上下文，支持跨初始化阶段传播和保留底层错误码。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Version.h` | 规划文件 | P0 | 定义引擎与内容格式的版本值及比较规则，不把资产schema版本等同程序版本。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Time/SimulationClock.h` | 规划文件 | P0 | 声明固定步长、累积时间、暂停、时间缩放与插值alpha的计算契约。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Time/SimulationClock.cpp` | 规划文件 | P0 | 计算每帧应执行的固定tick并限制失控补帧，保持真实时间与模拟时间独立。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Time/FrameTime.h` | 规划文件 | P0 | 定义真实帧时长、模拟tick编号和插值alpha的值类型，作为系统执行上下文输入。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Math/MathTypes.h` | 规划文件 | P0 | 先包含MathConfig再提供向量/矩阵/四元数别名；数学库内存布局不直接作为磁盘或GPU ABI。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Math/CoordinateConvention.h` | 规划文件 | P0 | 声明手性、forward/up、矩阵乘法和存储约定、角度单位及世界单位换算配置。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Math/TransformMath.h` | 规划文件 | P0 | 声明TRS组合、父子变换和安全求逆的数学接口，明确非均匀缩放及奇异矩阵行为。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Math/TransformMath.cpp` | 规划文件 | P0 | 实现符合统一矩阵约定的变换运算，对不可逆输入返回可诊断结果。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Jobs/JobHandle.h` | 规划文件 | P0 | 定义CPU任务完成凭据及依赖引用；明确它不代表GPU完成或音频播放结束。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Jobs/CancellationToken.h` | 规划文件 | P0 | 定义协作取消与停止请求的读取契约，不承诺强行终止正在执行的工作。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Jobs/JobSystem.h` | 规划文件 | P0 | 声明CPU任务提交、依赖、等待、任务组取消与关闭接口。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Jobs/JobSystem.cpp` | 规划文件 | P0 | 适配enkiTS执行任务并收束任务组，处理取消和错误；避免另建竞争线程池，关闭后不回调失效服务。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/IO/VirtualPath.h` | 规划文件 | P0 | 定义规范化虚拟路径与挂载定位值，区别资产ID、操作系统路径和包内路径。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/IO/VirtualFileSystem.h` | 规划文件 | P0 | 声明只读文件/包挂载及字节读取接口，让资产加载不依赖当前工作目录。 | 依赖Core及文件读取适配契约；包格式读取器由更上层注册，不依赖Renderer或Editor。 |
| `Catalpa/src/Core/IO/VirtualFileSystem.cpp` | 规划文件 | P0 | 解析挂载优先级并调度后端读取，返回路径、权限、缺失与内容校验错误。 | 依赖Core及文件读取适配契约；包格式读取器由更上层注册，不依赖Renderer或Editor。 |
| `Catalpa/src/Core/Serialization/Archive.h` | 规划文件 | P1 | 定义字段、数组、版本与错误上下文的归档读写契约，不规定某个组件或资产的字段。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Serialization/BinaryArchive.h` | 规划文件 | P1 | 声明有边界检查的二进制归档读写器及显式字节序规则。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Serialization/BinaryArchive.cpp` | 规划文件 | P1 | 执行长度、溢出和版本边界检查，将结果写入独立输出而不覆盖原始数据。 | 只依赖标准库及明确选用的基础数学库；不得依赖ECS、资产领域、Renderer或Vulkan。 |
| `Catalpa/src/Core/Math/MathConfig.h` | 规划文件 | P0 | 在任何GLM头前统一深度、对齐和配置宏，配合CoordinateConvention维护数学契约。 | 仅构建基础配置。 |
| `Catalpa/src/Core/Id/StableId.h` | 规划文件 | P0 | 稳定128位ID字节表示和文本协议基础；AssetId/PersistentEntityId保持独立强类型。 | 标准整数类型，不混同EntityId。 |
| `Catalpa/src/Core/Id/StableIdGenerator.h` | 规划文件 | P1 | 声明ID生成/解析与错误检查。 | Core平台服务。 |
| `Catalpa/src/Core/Id/StableIdGenerator.cpp` | 规划文件 | P1 | 利用平台随机能力生成ID并检查编码；资产重新cook不重新编号。 | Core平台服务。 |
| `Catalpa/src/Core/Memory/GenerationalHandle.h` | 规划文件 | P0 | 定义index/generation句柄模板基础，owner仍验证所属设备/世界/服务。 | Core值类型，不统一所有资源身份。 |
| `Catalpa/src/Core/Memory/FrameArena.h` | 规划文件 | P0 | 声明对齐线性暂存和显式reset契约。 | Core内存，不查询GPU。 |
| `Catalpa/src/Core/Memory/FrameArena.cpp` | 规划文件 | P0 | 实现容量检查/分配/reset；全部消费者完成由调用者证明，不按帧号猜测。 | Core内存，不查询GPU。 |
| `Catalpa/src/Core/Memory/MemoryBudget.h` | 规划文件 | P1 | 定义CPU、GPU表现、流送与暂存预算的统计值和阈值。 | 各领域负责实际分配与上报。 |
| `Catalpa/src/Core/Diagnostics/Profiling.h` | 规划文件 | P0 | 声明可关闭的CPU分析范围与命名协议。 | Core与可选Tracy，无Vk公共头。 |
| `Catalpa/src/Core/Diagnostics/Profiling.cpp` | 规划文件 | P0 | 在Profiling构建适配Tracy；GPU query接入留在RHI/后端。 | Core与可选Tracy，无Vk公共头。 |
| `Catalpa/src/Core/Diagnostics/CrashReport.h` | 规划文件 | P2 | 声明受控失败/异常上下文报告。 | Core/Log/平台。 |
| `Catalpa/src/Core/Diagnostics/CrashReport.cpp` | 规划文件 | P2 | 输出构建版本和受限诊断；不承诺从任意损坏状态恢复。 | Core/Log/平台。 |
| `Catalpa/src/Core/PlatformServices.h` | 规划文件 | P0 | 声明路径、时钟、动态库与基础系统信息契约。 | 由WindowsPlatform实现，不暴露Win32。 |
| `Catalpa/src/Core/IO/FileWatcher.h` | 规划文件 | P2 | 声明文件变更通知与关闭协议；通知不代表写入已完成。 | 平台/Jobs，不依赖Assets。 |
| `Catalpa/src/Core/IO/FileWatcher.cpp` | 规划文件 | P2 | 合并通知并请求消费者重新校验内容，不在监视线程修改资产。 | 平台/Jobs，不依赖Assets。 |
| `Catalpa/src/Core/IO/AtomicFileWriter.h` | 规划文件 | P2 | 声明指定作者文档或派生文件的事务性输出。 | Core IO，目标策略由调用者指定。 |
| `Catalpa/src/Core/IO/AtomicFileWriter.cpp` | 规划文件 | P2 | 临时写入校验后替换目标，失败返回错误；不擅自覆盖导入原素材。 | Core IO，目标策略由调用者指定。 |
| `Catalpa/src/Core/Serialization/JsonArchive.h` | 规划文件 | P1 | 声明配置与作者文档JSON读写及字段错误路径。 | Archive/Core，JSON依赖私有。 |
| `Catalpa/src/Core/Serialization/JsonArchive.cpp` | 规划文件 | P1 | 适配nlohmann/json，检查类型/大小/版本；不承担每tick大数据传输。 | Archive/Core，JSON依赖私有。 |

<a id="module-04"></a>

## 04. Log

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Log/Log.h` | 现有文件 | P0 | 保留日志公共入口，定义等级、分类和初始化/关闭契约，支持结构化错误输出。 | 依赖Core和spdlog；不让日志依赖Renderer、World或资产管理器。 |
| `Catalpa/src/Log/Log.cpp` | 现有文件 | P0 | 实现现有spdlog适配与日志sink配置，保证关闭顺序和多线程输出可诊断。 | 依赖Core和spdlog；不让日志依赖Renderer、World或资产管理器。 |

<a id="module-05"></a>

## 05. Events

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Events/Event.h` | 现有文件 | P0 | 保留事件基类/类别与分发约定，明确同步借用事件的寿命及Handled语义。 | 只依赖Core；异步任务不保存当前栈上Event引用。 |
| `Catalpa/src/Events/ApplicationEvent.h` | 现有文件 | P0 | 保留关闭、逻辑窗口/像素尺寸和焦点等应用事件值，区分最小化与交换链错误。 | 依赖Event与基础值类型；不携带VkSurfaceKHR或后端资源。 |
| `Catalpa/src/Events/KeyEvent.h` | 现有文件 | P0 | 保留按下、释放、重复与文本输入等键盘事件数据，明确物理键与字符输入的区别。 | 依赖Event和Core键码；不处理游戏action或编辑器快捷键策略。 |
| `Catalpa/src/Events/MouseEvent.h` | 现有文件 | P0 | 保留按键、位置和滚动事件，声明坐标空间及缩放语义。 | 依赖Event和基础值类型；业务坐标转换由UI/viewport处理。 |

<a id="module-06"></a>

## 06. Layer

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Layer/Layer.h` | 现有文件 | P0 | 保留应用扩展的attach/detach、update、event及UI回调协议，区分它与ECS系统。 | 依赖Core/Events；Layer可以调用Runtime公共服务，不替代SystemScheduler。 |
| `Catalpa/src/Layer/Layer.cpp` | 现有文件 | P0 | 实现Layer基础命名与默认行为，不持有图形设备或自动注册游戏系统。 | 依赖Core/Events；Layer可以调用Runtime公共服务，不替代SystemScheduler。 |
| `Catalpa/src/Layer/LayerStack.h` | 现有文件 | P0 | 保留Layer/Overlay的容器接口，明确push、pop、detach及对象所有权转移。 | 依赖Layer和Core；不承担GPU提交、World阶段排序或音频混音。 |
| `Catalpa/src/Layer/LayerStack.cpp` | 现有文件 | P0 | 实现稳定事件遍历和显式移除/析构行为，避免回调期间修改容器导致失效。 | 依赖Layer和Core；不承担GPU提交、World阶段排序或音频混音。 |
| `Catalpa/src/Layer/Imgui/ImGuiLayer.h` | 现有文件 | P2 | 保留Dear ImGui上下文和应用UI生命周期入口，接收渲染桥提供的集成接口。 | 允许依赖ImGui、Window及UI/Renderer公共桥；具体Vulkan backend集成由受控后端桥负责。 |
| `Catalpa/src/Layer/Imgui/ImGuiLayer.cpp` | 现有文件 | P2 | 实现ImGui新帧、输入转交、绘制数据输出和有序关闭，不在这里持有VkDevice或VkDescriptorPool。 | 允许依赖ImGui、Window及UI/Renderer公共桥；具体Vulkan backend集成由受控后端桥负责。 |
| `Catalpa/src/Layer/Imgui/ImGuiBuild.cpp` | 现有文件 | P2 | 保留现有构建集成位置，明确ImGui backend源码只编译一次；具体后端文件在各自集成目标注册。 | 不在这里重复包含Vulkan backend实现；不得与Platform/Vulkan集成文件重复定义符号。 |

<a id="module-07"></a>

## 07. ECS

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/ECS/EntityId.h` | 规划文件 | P0 | 定义带World身份和generation的运行期实体引用与无效值，区别场景持久化ID。 | 只依赖Core；禁止保存裸registry索引作为跨World或跨异步任务的永久身份。 |
| `Catalpa/src/ECS/World.h` | 规划文件 | P0 | 声明实体和组件存储、有效性检查及受控查询入口，明确World独占其registry。 | 依赖Core和选定ECS库；不依赖SceneManager、Renderer、Physics SDK或音频backend。 |
| `Catalpa/src/ECS/World.cpp` | 规划文件 | P0 | 实现所选ECS库适配和实体生命周期校验，为系统提供查询而不加载资产或创建设备对象。 | 依赖Core和选定ECS库；不依赖SceneManager、Renderer、Physics SDK或音频backend。 |
| `Catalpa/src/ECS/EntityCommandBuffer.h` | 规划文件 | P0 | 声明延迟创建/销毁实体、增删组件和引用修复的命令，规定应用阶段。 | 依赖World和组件注册契约；命令不得保存短命组件引用。 |
| `Catalpa/src/ECS/EntityCommandBuffer.cpp` | 规划文件 | P0 | 在安全边界验证generation并按确定顺序提交结构变更，报告失败而非在并行遍历时直接重排存储。 | 依赖World和组件注册契约；命令不得保存短命组件引用。 |
| `Catalpa/src/ECS/System.h` | 规划文件 | P0 | 定义系统阶段、组件读写资源声明与执行入口；系统可以是轻量函数适配，不强制复杂继承树。 | 依赖Core和World契约；不得把GPU依赖或音频设备回调纳入同一调度模型。 |
| `Catalpa/src/ECS/SystemContext.h` | 规划文件 | P0 | 定义World访问、时间和受限服务上下文注入协议，系统显式声明所需服务。 | 依赖Core及服务契约；不提供可任意访问全部单例的全局服务定位器。 |
| `Catalpa/src/ECS/SystemScheduler.h` | 规划文件 | P0 | 声明CPU系统注册、阶段、读写依赖和结构变更提交点。 | 依赖ECS/Core Jobs；不决定RenderGraph barrier或vkQueueSubmit顺序。 |
| `Catalpa/src/ECS/SystemScheduler.cpp` | 规划文件 | P0 | 构建并验证CPU执行依赖，借用JobSystem运行无冲突任务，检查依赖环与不合法跨阶段写入。 | 依赖ECS/Core Jobs；不决定RenderGraph barrier或vkQueueSubmit顺序。 |
| `Catalpa/src/ECS/ComponentRegistry.h` | 规划文件 | P1 | 声明组件稳定类型ID、字段schema及序列化/编辑扩展注册入口。 | 依赖Core；领域组件和编辑适配在Runtime/Editor注册，ECS基础模块不反向包含它们。 |
| `Catalpa/src/ECS/ComponentRegistry.cpp` | 规划文件 | P1 | 维护类型注册与版本信息并拒绝重复或冲突注册，为SceneSerializer和Editor提供元数据。 | 依赖Core；领域组件和编辑适配在Runtime/Editor注册，ECS基础模块不反向包含它们。 |

<a id="module-08"></a>

## 08. Scene

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Scene/PersistentEntityId.h` | 规划文件 | P1 | 定义场景保存使用的稳定实体ID，区别可复用的EntityId及资产ID。 | 依赖Core；实例化Prefab/Model时必须重新映射实体引用。 |
| `Catalpa/src/Scene/EntityReference.h` | 规划文件 | P1 | 定义持久化实体引用及运行期解析结果，支持缺失目标、跨场景策略和generation校验。 | 依赖PersistentEntityId、ECS EntityId；不暴露组件地址。 |
| `Catalpa/src/Scene/Scene.h` | 规划文件 | P1 | 定义一个场景实例对World、SystemScheduler及场景系统状态的所有权，以及加载/激活/暂停/卸载状态。 | 依赖ECS、Assets契约和受限运行时服务；RenderScene/AudioScene通过句柄关联。 |
| `Catalpa/src/Scene/Scene.cpp` | 规划文件 | P1 | 管理场景生命周期并收束该场景的任务与提取输出，借用共享领域服务而不拥有VkDevice。 | 依赖ECS、Assets契约和受限运行时服务；RenderScene/AudioScene通过句柄关联。 |
| `Catalpa/src/Scene/SceneManager.h` | 规划文件 | P1 | 声明场景加载、激活、切换、卸载及编辑/运行World分离策略。 | 依赖Scene及Assets公共接口；不让加载线程直接创建GPU资源。 |
| `Catalpa/src/Scene/SceneManager.cpp` | 规划文件 | P1 | 维护场景实例所有权和异步加载状态，保证卸载后没有任务回调失效World。 | 依赖Scene及Assets公共接口；不让加载线程直接创建GPU资源。 |
| `Catalpa/src/Scene/SceneAsset.h` | 规划文件 | P1 | 定义保存后的场景实体、组件字段和外部资产引用数据，带schema版本。 | 依赖Assets身份和归档值类型；不保存registry布局、运行期EntityId或设备句柄。 |
| `Catalpa/src/Scene/SceneSerializer.h` | 规划文件 | P1 | 声明场景保存、加载、schema迁移和实体引用重映射接口。 | 依赖Scene、ComponentRegistry和归档接口；运行期组件字段需显式排除。 |
| `Catalpa/src/Scene/SceneSerializer.cpp` | 规划文件 | P1 | 依据组件注册信息验证字段与资产引用，构建新场景实例并输出诊断，不覆盖源文件。 | 依赖Scene、ComponentRegistry和归档接口；运行期组件字段需显式排除。 |
| `Catalpa/src/Scene/PrefabAsset.h` | 规划文件 | P2 | 定义可复用实体层级、组件字段及局部实体引用的资产数据。 | 依赖Scene序列化契约和Assets；不保存已经实例化的World指针。 |
| `Catalpa/src/Scene/PrefabInstantiation.h` | 规划文件 | P2 | 声明Prefab实例化、实体ID重映射和明确支持的参数覆盖策略。 | 依赖World、PrefabAsset和组件注册；继承链和复杂override合并不作为首版默认功能。 |
| `Catalpa/src/Scene/PrefabInstantiation.cpp` | 规划文件 | P2 | 将Prefab展开到目标World并修复内部引用，失败时撤销尚未发布的实例化结果。 | 依赖World、PrefabAsset和组件注册；继承链和复杂override合并不作为首版默认功能。 |
| `Catalpa/src/Scene/ModelInstantiation.h` | 规划文件 | P1 | 声明ModelAsset节点、mesh和材质绑定转换为场景实体的接口。 | 依赖Scene/ECS及Geometry ModelAsset；不创建顶点缓冲或VkPipeline。 |
| `Catalpa/src/Scene/ModelInstantiation.cpp` | 规划文件 | P1 | 建立实体层级和材质槽引用，保留导入变换语义，不复制或改写原始MeshData。 | 依赖Scene/ECS及Geometry ModelAsset；不创建顶点缓冲或VkPipeline。 |
| `Catalpa/src/Scene/Components/IdentityComponent.h` | 规划文件 | P1 | 定义实体持久化ID和编辑名称，运行期EntityId仍由World管理。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/TransformComponent.h` | 规划文件 | P1 | 保存局部TRS和明确的派生世界变换/dirty状态约定，声明数学单位及唯一写入阶段。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/RelationshipComponent.h` | 规划文件 | P1 | 保存父子实体关系和合法性约束，拒绝自引用、跨World引用及层级环。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/MeshRendererComponent.h` | 规划文件 | P1 | 保存静态MeshAsset引用、材质槽覆盖及实例可见/投影选项，不复制顶点数据。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/CameraComponent.h` | 规划文件 | P1 | 保存CameraLens、启用/优先级和视图目标配置，viewport像素尺寸由具体RenderView提供。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/LightComponent.h` | 规划文件 | P1 | 保存方向/点/聚光类型和明确单位的光强、颜色、范围与阴影偏好。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/EnvironmentComponent.h` | 规划文件 | P2 | 保存EnvironmentAsset、背景和曝光等场景环境引用/参数，不持有预过滤GPU贴图。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/SkinnedMeshRendererComponent.h` | 规划文件 | P3 | 保存mesh、skeleton及Animator关联和材质槽，明确骨骼映射与缺失动画fallback。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/AnimatorComponent.h` | 规划文件 | P3 | 保存动画资产引用及每实体播放状态/CPU pose所有权，不共享可变播放时间。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/RigidBodyComponent.h` | 规划文件 | P3 | 保存body类型、质量/阻尼/约束与受控runtime body句柄，声明dynamic/kinematic变换权威。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/ColliderComponent.h` | 规划文件 | P3 | 保存碰撞形状/CollisionShapeAsset引用、局部姿态和过滤配置，不持有Physics SDK对象。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/AudioSourceComponent.h` | 规划文件 | P3 | 保存clip引用、空间/循环/音量设置和受控voice引用，Play/Stop通过有序命令提交。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/AudioListenerComponent.h` | 规划文件 | P3 | 保存listener启用与优先级，禁止多个有效listener时静默选择不确定结果。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/ScriptComponent.h` | 规划文件 | P3 | 可选：保存脚本资产/类型和可序列化参数，脚本实例由Scripting运行时管理。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/CharacterControllerComponent.h` | 规划文件 | P4 | 可选：保存角色控制器形状、运动配置与受控实例句柄，不与dynamic body同时争夺transform。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/ParticleEmitterComponent.h` | 规划文件 | P4 | 可选：保存粒子效果资产引用和发射参数，粒子模拟/绘制由独立效果服务处理。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/TerrainComponent.h` | 规划文件 | P4 | 可选：保存terrain资产、尺度和分块配置，不把所有地形顶点塞入ECS。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/NavigationAgentComponent.h` | 规划文件 | P4 | 可选：保存导航代理配置、目标和受控状态，不包含导航库原生对象。 | 仅保存CPU领域数据、AssetHandle和EntityId；不得持有Vk对象或第三方运行时设备指针。 |
| `Catalpa/src/Scene/Components/UIComponent.h` | 规划文件 | P3 | 定义UI文档引用、可见性、交互配置与实例句柄。 | 仅CPU数据和资产句柄，不包含RmlUi、GPU对象或UIContext所有权。 |
| `Catalpa/src/Scene/SceneHandle.h` | 规划文件 | P1 | 定义SceneManager验证的实例句柄/generation；文档借用句柄而不二次拥有World。 | Core值类型。 |

<a id="module-09"></a>

## 09. Runtime

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Runtime/EngineRuntime.h` | 规划文件 | P0 | 声明JobSystem、Assets、SceneManager、Renderer及按需Physics/Audio/Script/UI服务的所有权和启停；任务设施晚于依赖它的服务关闭。 | 允许依赖各领域公共接口；此处是组合根，不把组合根的依赖反向扩散到Core/ECS。 |
| `Catalpa/src/Runtime/EngineRuntime.cpp` | 规划文件 | P0 | 执行服务初始化、模拟tick、数据提取及关闭状态机，按依赖收束任务并保留错误上下文。 | 允许依赖各领域公共接口；此处是组合根，不把组合根的依赖反向扩散到Core/ECS。 |
| `Catalpa/src/Runtime/EngineConfig.h` | 规划文件 | P0 | 定义模拟步长、目标设备能力、内容挂载、音频启用及世界单位等可验证配置。 | 依赖领域公共描述类型；不要求客户端配置Vk结构体。 |
| `Catalpa/src/Runtime/RuntimeServices.h` | 规划文件 | P0 | 定义系统可借用的领域服务集合、场景上下文和服务有效期，避免系统自行访问Application单例。 | 依赖各领域公共契约；服务不持有系统短命组件引用。 |
| `Catalpa/src/Runtime/RuntimeRegistration.h` | 规划文件 | P1 | 声明内建资产加载器、组件schema和标准系统注册入口。 | 允许依赖内建领域和Runtime Systems；Editor专用元数据在Editor另行注册。 |
| `Catalpa/src/Runtime/RuntimeRegistration.cpp` | 规划文件 | P1 | 在组合根装配领域适配器，检查重复注册和缺失依赖，保持Assets/ECS基础层独立。 | 允许依赖内建领域和Runtime Systems；Editor专用元数据在Editor另行注册。 |
| `Catalpa/src/Runtime/Systems/TransformSystem.h` | 规划文件 | P1 | 声明层级验证、dirty传播和世界变换更新阶段，明确多写入者协调规则。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/TransformSystem.cpp` | 规划文件 | P1 | 在结构变更提交后校验层级，按父先子后更新dirty世界变换；提取前更新受影响Camera/bounds并诊断非有限/奇异输入。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/CameraSystem.h` | 规划文件 | P1 | 声明从CameraComponent和最终Transform构建CameraMatrices的桥接接口。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/CameraSystem.cpp` | 规划文件 | P1 | 验证lens与目标viewport，生成视图/投影及逆矩阵，为RenderView提供稳定值数据。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/RenderExtractSystem.h` | 规划文件 | P1 | 声明World到RenderFramePacket的提取，包含mesh、材质、light、camera和稳定实体映射。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/RenderExtractSystem.cpp` | 规划文件 | P1 | 在模拟阶段完成后复制/版本化所需数据与资产引用，不让render线程遍历可变registry。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/AnimationSystem.h` | 规划文件 | P3 | 声明Animator推进、pose采样、动画事件与root motion输出所在阶段。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/AnimationSystem.cpp` | 规划文件 | P3 | 推进每实体状态、采样骨骼姿态并将root motion按规则交给模拟，准备可提取的骨骼数据。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/PhysicsSystem.h` | 规划文件 | P3 | 声明FixedUpdate内PhysicsPre/Sim/Post三个受调度阶段，分别处理kinematic/命令同步、模拟、dynamic回写及碰撞事件交接。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/PhysicsSystem.cpp` | 规划文件 | P3 | 按固定tick分阶段执行PhysicsPre同步、PhysicsSim步进和PhysicsPost回写，处理body销毁及场景卸载，避免与动画/Gameplay无序覆盖Transform。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/AudioExtractSystem.h` | 规划文件 | P3 | 声明listener/emitter快照和Play/Stop等离散命令的独立交接契约。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/AudioExtractSystem.cpp` | 规划文件 | P3 | 提取最终空间状态、验证listener选择，并在实体/场景卸载时发送带身份的停止命令。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。 |
| `Catalpa/src/Runtime/Systems/NativeGameplaySystem.h` | 规划文件 | P3 | 声明原生Gameplay模块参与World阶段调度和命令提交的入口。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。具体游戏规则位于客户端工程，不写入Catalpa运行库。 |
| `Catalpa/src/Runtime/Systems/NativeGameplaySystem.cpp` | 规划文件 | P3 | 调度客户端注册的原生逻辑，按读写声明运行并将结构变更送入EntityCommandBuffer。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。具体游戏规则位于客户端工程，不写入Catalpa运行库。 |
| `Catalpa/src/Runtime/Systems/ScriptSystem.h` | 规划文件 | P3 | 可选：声明脚本实例创建、更新、事件和销毁与World阶段的对应。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。语言VM属于Scripting backend，ECS不直接include其头文件。 |
| `Catalpa/src/Runtime/Systems/ScriptSystem.cpp` | 规划文件 | P3 | 调用受限Scripting运行时并转换为受验证World命令，处理脚本失败与场景卸载。 | 依赖ECS/Scene组件及对应领域公共服务；不直接调用vk*，不保留跨结构变更的组件指针。语言VM属于Scripting backend，ECS不直接include其头文件。 |
| `Catalpa/src/Runtime/Systems/ParticleSystem.h` | 规划文件 | P4 | 声明ParticleEmitter到模拟实例及绘制快照的唯一World桥。 | 依赖ECS/Scene、ParticleSimulation与Renderer公开packet；不拥有GPU对象。 |
| `Catalpa/src/Runtime/Systems/ParticleSystem.cpp` | 规划文件 | P4 | 安全阶段同步发射器、调度模拟并生成稳定渲染快照。 | 依赖ECS/Scene、ParticleSimulation与Renderer公开packet；不拥有GPU对象。 |
| `Catalpa/src/Runtime/Systems/TerrainSystem.h` | 规划文件 | P4 | 声明Terrain组件到领域服务、Renderer和可选Physics的World桥。 | 依赖ECS/Scene和公开领域服务；不接触backend原生状态。 |
| `Catalpa/src/Runtime/Systems/TerrainSystem.cpp` | 规划文件 | P4 | 提交值配置，消费就绪tile并安全更新表现与碰撞。 | 依赖ECS/Scene和公开领域服务；不接触backend原生状态。 |
| `Catalpa/src/Runtime/Systems/UISystem.h` | 规划文件 | P3 | 声明UIComponent到UIContext的数据与事件桥接。 | 依赖ECS/Scene和UI公开服务；UIContext及RmlUi不读取World。 |
| `Catalpa/src/Runtime/Systems/UISystem.cpp` | 规划文件 | P3 | 安全阶段提交值模型，校验返回事件并通过命令缓冲修改场景。 | 依赖ECS/Scene和UI公开服务；UIContext及RmlUi不读取World。 |
| `Catalpa/src/Runtime/Systems/NavigationSystem.h` | 规划文件 | P4 | 声明NavigationAgent到NavigationWorld的请求与结果桥接。 | 依赖ECS/Scene、NavigationWorld和可选Physics；不替代游戏AI。 |
| `Catalpa/src/Runtime/Systems/NavigationSystem.cpp` | 规划文件 | P4 | 调度路径请求并安全应用结果，与Physics明确移动权威。 | 依赖ECS/Scene、NavigationWorld和可选Physics；不替代游戏AI。 |
| `Catalpa/src/Runtime/GameplayModule.h` | 规划文件 | P1 | 声明游戏模块注册、系统安装、启停和上下文协议。 | ECS/Runtime公开接口。 |
| `Catalpa/src/Runtime/GameplayModule.cpp` | 规划文件 | P1 | 按安全阶段调度客户端逻辑；实际游戏类型保留在产品项目。 | ECS/Runtime公开接口。 |
| `Catalpa/src/Runtime/Input/ActionMap.h` | 规划文件 | P1 | 定义键轴到具名action的映射、上下文与重绑定数据。 | Core输入值，不用GLFW常量。 |
| `Catalpa/src/Runtime/Input/InputActions.h` | 规划文件 | P1 | 声明模拟tick的action快照与UI/游戏输入路由。 | Core/Input与ActionMap。 |
| `Catalpa/src/Runtime/Input/InputActions.cpp` | 规划文件 | P1 | 处理按下/保持/释放、焦点丢失和轴值；避免多个tick重复消费边沿。 | Core/Input与ActionMap。 |

<a id="module-10"></a>

## 10. Assets

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Assets/AssetId.h` | 规划文件 | P0 | 定义持久化资产身份及无效值，身份独立于路径、内存地址和GPU资源编号。 | 只依赖Core；不包含任何具体资产类型或设备对象。 |
| `Catalpa/src/Assets/AssetHandle.h` | 规划文件 | P0 | 定义AssetHandle<T>逻辑资产引用与加载访问，以及AssetVersionRef<T>不可变版本保活契约；snapshot、GPU cache和audio voice取得版本引用后不随热重载自动切换。 | 依赖AssetId和基础类型；逻辑handle本身不证明旧版本被保活，固定版本引用由AssetManager取得并明确释放；不把GPU上传完成状态塞进通用handle。 |
| `Catalpa/src/Assets/AssetType.h` | 规划文件 | P0 | 定义稳定资产类型标识和加载器注册键，避免运行期typeid值被当持久化schema。 | 只依赖Core；具体Mesh/Image/Shader等在领域注册。 |
| `Catalpa/src/Assets/AssetLocator.h` | 规划文件 | P0 | 定义资产ID解析到虚拟路径/包条目的定位记录，支持来源和cooked版本区分。 | 依赖Core VFS与AssetId；加载代码不依赖当前工作目录。 |
| `Catalpa/src/Assets/AssetStatus.h` | 规划文件 | P0 | 定义Unloaded/Loading/Ready/Failed等CPU资产状态和重试上下文。 | 只依赖Core；Ready不代表GPU resident或音频voice可播放。 |
| `Catalpa/src/Assets/AssetRecord.h` | 规划文件 | P0 | 定义资产类型、版本、依赖、locator和内容校验元数据。 | 依赖Assets基础值类型；不持有领域服务或设备句柄。 |
| `Catalpa/src/Assets/AssetLoadResult.h` | 规划文件 | P0 | 定义加载后数据所有权、诊断、依赖发现和取消结果的交接。 | 依赖Core Result及Assets契约；不得返回指向临时读取缓冲的悬空指针。 |
| `Catalpa/src/Assets/AssetLoader.h` | 规划文件 | P0 | 声明类型加载器接口，区分读取/解析与主线程发布，不包含具体领域加载实现。 | 依赖VFS、Jobs和AssetLoadResult；不得创建GPU或音频设备对象。 |
| `Catalpa/src/Assets/AssetRegistry.h` | 规划文件 | P0 | 声明资产元数据登记、稳定ID查询和locator解析入口。 | 依赖Core及Assets基础类型；领域注册由Runtime/Tools执行。 |
| `Catalpa/src/Assets/AssetRegistry.cpp` | 规划文件 | P0 | 维护资产索引并检测ID/路径冲突，提供确定的缺失与版本错误报告。 | 依赖Core及Assets基础类型；领域注册由Runtime/Tools执行。 |
| `Catalpa/src/Assets/AssetManager.h` | 规划文件 | P0 | 声明异步请求、引用保持、加载状态、版本发布及卸载入口。 | 依赖AssetRegistry、AssetLoader、Core Jobs/VFS；不依赖Renderer/Physics/Audio backend。 |
| `Catalpa/src/Assets/AssetManager.cpp` | 规划文件 | P0 | 调度读取/解析任务并安全发布不可变资产版本，失败保留诊断，关闭时取消并收束回调。 | 依赖AssetRegistry、AssetLoader、Core Jobs/VFS；不依赖Renderer/Physics/Audio backend。 |
| `Catalpa/src/Assets/AssetDependencyGraph.h` | 规划文件 | P2 | 声明资产依赖记录、受影响版本查询及加载/烘焙依赖环策略。 | 依赖Assets身份和Core容器；不推导RenderGraph资源依赖。 |
| `Catalpa/src/Assets/AssetDependencyGraph.cpp` | 规划文件 | P2 | 检查依赖环与失效传播，为热重载和卸载提供明确依赖集合。 | 依赖Assets身份和Core容器；不推导RenderGraph资源依赖。 |
| `Catalpa/src/Assets/AssetManifest.h` | 规划文件 | P2 | 声明运行期资产集合、类型/版本/依赖与包定位的manifest读写契约。 | 依赖Core归档和Assets值类型；工具生成与运行期读取共享schema，不包含Editor。 |
| `Catalpa/src/Assets/AssetManifest.cpp` | 规划文件 | P2 | 校验manifest内容边界、重复ID和缺失依赖，拒绝不兼容格式。 | 依赖Core归档和Assets值类型；工具生成与运行期读取共享schema，不包含Editor。 |
| `Catalpa/src/Assets/AssetChangeEvent.h` | 规划文件 | P2 | 定义资产新版本发布、失效或失败通知，接收方自行更新领域表现缓存。 | 依赖AssetId/版本/错误值；事件不携带Vk对象或World组件引用。 |
| `Catalpa/src/Assets/AssetCodec.h` | 规划文件 | P1 | 声明cooked资产的类型化编解码注册契约和格式版本策略。 | 依赖Core归档；具体Mesh/Image/Shader/Audio codec在领域或Tools注册。 |
| `Catalpa/src/Assets/AssetContainerFormat.h` | 规划文件 | P1 | 定义单资产容器的类型、版本、字节序、依赖、块表和校验字段。 | Core值类型，供工具/runtime共享。 |
| `Catalpa/src/Assets/AssetContainerReader.h` | 规划文件 | P1 | 声明单资产头/块/依赖的有界只读访问。 | Core IO与AssetContainerFormat。 |
| `Catalpa/src/Assets/AssetContainerReader.cpp` | 规划文件 | P1 | 检查offset/长度/溢出/校验并保活字节view；领域loader再解码具体内容。 | Core IO与AssetContainerFormat。 |
| `Catalpa/src/Assets/PackageFormat.h` | 规划文件 | P2 | 定义多资产包索引、分块和版本编码规范，区别单资产格式。 | PackageReader/Packager共享，不dump结构体。 |
| `Catalpa/src/Assets/PackageReader.h` | 规划文件 | P2 | 声明运行包索引、块读取和有界解压。 | Core/IO、PackageFormat、可选zstd。 |
| `Catalpa/src/Assets/PackageReader.cpp` | 规划文件 | P2 | 校验包结构、版本、长度/哈希再交付内容，解压受预算限制。 | Core/IO、PackageFormat、可选zstd。 |

<a id="module-11"></a>

## 11. Geometry

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Geometry/VertexLayout.h` | 规划文件 | P1 | 定义CPU顶点stream、semantic、format、stride和offset，明确interleaved/planar布局。 | 依赖Core；使用引擎格式枚举，不暴露VkVertexInput描述。 |
| `Catalpa/src/Geometry/Submesh.h` | 规划文件 | P1 | 定义索引/顶点范围、拓扑和材质槽索引，明确范围单位及baseVertex语义。 | 依赖Core和Geometry类型；材质仅用槽位/资产引用，不持有pipeline。 |
| `Catalpa/src/Geometry/MeshData.h` | 规划文件 | P1 | 定义拥有内存的CPU顶点/索引流、submesh和局部bounds，明确索引类型及字节长度。 | 依赖Core与Geometry值类型；不持有GPU缓冲或Scene实例。 |
| `Catalpa/src/Geometry/MeshAsset.h` | 规划文件 | P1 | 定义可复用mesh资产及LOD、skin binding等关联，不包含每实体Transform。 | 依赖Assets契约和MeshData；骨骼引用仅使用资产身份，不依赖动画运行时。 |
| `Catalpa/src/Geometry/ModelAsset.h` | 规划文件 | P1 | 定义导入模型的节点层级、局部变换、mesh实例与材质引用，区别World中的场景实例。 | 依赖Geometry、Materials公共数据与Assets句柄；不依赖ECS或SceneManager。 |
| `Catalpa/src/Geometry/MeshLod.h` | 规划文件 | P2 | 定义LOD层级、误差/切换指标和各层mesh数据引用，不默认生成或覆盖原始网格。 | 依赖Geometry及Assets基础类型；选择策略由Renderer，生成由Tools负责。 |
| `Catalpa/src/Geometry/MeshSkinBinding.h` | 规划文件 | P3 | 定义vertex joint/weight语义、逆bind矩阵和mesh到skeleton骨骼映射。 | 依赖Core Math和资产身份；验证归一化策略必须显式，不能静默改原始权重。 |
| `Catalpa/src/Geometry/Bounds.h` | 规划文件 | P1 | 声明AABB/Sphere数据及从几何/变换构造、合并的规则。 | 只依赖Core Math；不依赖renderer的帧状态。 |
| `Catalpa/src/Geometry/Bounds.cpp` | 规划文件 | P1 | 实现有限值检查、保守变换和bounds合并，正确处理非均匀缩放及空几何。 | 只依赖Core Math；不依赖renderer的帧状态。 |
| `Catalpa/src/Geometry/MeshValidation.h` | 规划文件 | P1 | 声明mesh stream长度、index范围、submesh、layout和可选skin数据的验证入口。 | 依赖Geometry和Core Result；修复/优化必须由显式工具任务决定。 |
| `Catalpa/src/Geometry/MeshValidation.cpp` | 规划文件 | P1 | 检查越界、stride/offset、格式和有限值，返回问题清单，默认不修正源MeshData。 | 依赖Geometry和Core Result；修复/优化必须由显式工具任务决定。 |
| `Catalpa/src/Geometry/PrimitiveMesh.h` | 规划文件 | P1 | 声明用于测试和编辑器的立方体/平面等基础几何生成参数。 | 依赖Geometry与Core Math；输出走同一mesh验证和上传路径。 |
| `Catalpa/src/Geometry/PrimitiveMesh.cpp` | 规划文件 | P1 | 按明确尺寸、绕序、法线和UV约定生成新MeshData，不使用硬编码GPU对象。 | 依赖Geometry与Core Math；输出走同一mesh验证和上传路径。 |
| `Catalpa/src/Geometry/GeometryAssetLoader.h` | 规划文件 | P1 | 声明Mesh/Model/LOD/skin的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Geometry/GeometryAssetLoader.cpp` | 规划文件 | P1 | 有界解析并校验Mesh/Model/LOD/skin及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-12"></a>

## 12. Images

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Images/ImageTypes.h` | 规划文件 | P0 | 定义颜色空间、alpha、通道语义和图像用途，引用唯一PixelFormat而不再维护重复格式枚举。 | Core及Images/PixelFormat；不依赖RHI。 |
| `Catalpa/src/Images/ImageData.h` | 规划文件 | P1 | 定义拥有内存的像素mip/array/face数据与行/层pitch，明确每块数据的尺寸和字节数。 | 依赖Core和ImageTypes；不持有采样器或VkImage。 |
| `Catalpa/src/Images/ImageAsset.h` | 规划文件 | P1 | 定义可加载图像资产、来源元数据与运行期mip集合，支持纹理复用。 | 依赖Assets契约及ImageData；GPU驻留与采样器策略由Renderer负责。 |
| `Catalpa/src/Images/ImageValidation.h` | 规划文件 | P1 | 声明图像尺寸、格式、pitch、mip及压缩块布局检查接口。 | 依赖Images和Core Result；不得修改原始图像数据。 |
| `Catalpa/src/Images/ImageValidation.cpp` | 规划文件 | P1 | 检查尺寸溢出、数据长度、mip链与颜色空间语义冲突，报告而非静默转换。 | 依赖Images和Core Result；不得修改原始图像数据。 |
| `Catalpa/src/Images/ImageOperations.h` | 规划文件 | P1 | 声明显式生成新ImageData的尺寸/格式/颜色空间处理与mip构建操作。 | 依赖Images及选定图像处理库；由Tools调用，runtime不隐式预处理原始素材。 |
| `Catalpa/src/Images/ImageOperations.cpp` | 规划文件 | P1 | 在显式选项下执行处理并保留来源，正确区分sRGB、法线和线性数据用途，不覆盖输入。 | 依赖Images及选定图像处理库；由Tools调用，runtime不隐式预处理原始素材。 |
| `Catalpa/src/Images/PixelFormat.h` | 规划文件 | P0 | 定义CPU图像与RHI共享的有限格式ID、block尺寸与基础属性；用途和设备支持另行验证。 | Core值类型，无RHI/Vk。 |
| `Catalpa/src/Images/ImageAssetLoader.h` | 规划文件 | P1 | 声明图像mip/array/cube与KTX2的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Images/ImageAssetLoader.cpp` | 规划文件 | P1 | 有界解析并校验图像mip/array/cube与KTX2及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-13"></a>

## 13. Materials

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Materials/MaterialTypes.h` | 规划文件 | P1 | 定义shading model、alpha mode和纹理semantic等CPU领域枚举。 | 只依赖Core；不是Vulkan状态枚举逐项复制。 |
| `Catalpa/src/Materials/MaterialParameter.h` | 规划文件 | P1 | 定义参数名称/稳定ID、数值类型、默认值和纹理资产引用，禁止无schema任意字节覆盖。 | 依赖Core、Assets句柄与Images类型；不包含descriptor编号。 |
| `Catalpa/src/Materials/MaterialRenderState.h` | 规划文件 | P1 | 定义材质允许声明的cull、depth、blend等策略及可覆盖范围。 | 依赖Core与Materials类型；实际pass兼容和pipeline选择由Renderer决定。 |
| `Catalpa/src/Materials/MaterialDefinition.h` | 规划文件 | P1 | 定义材质族、shader family/入口引用、参数schema、domain与受限渲染状态策略。 | 依赖Shaders公共资产身份及Materials数据；不持有VkPipeline或renderer缓存。 |
| `Catalpa/src/Materials/MaterialInstance.h` | 规划文件 | P1 | 声明definition引用、数值/纹理参数覆盖和每实例版本的CPU数据接口。 | 依赖MaterialDefinition和MaterialValidation；GPU参数块与descriptor由MaterialGpuCache生成。 |
| `Catalpa/src/Materials/MaterialInstance.cpp` | 规划文件 | P1 | 验证并应用允许的参数覆盖、更新版本及默认值，保持共享definition不可变。 | 依赖MaterialDefinition和MaterialValidation；GPU参数块与descriptor由MaterialGpuCache生成。 |
| `Catalpa/src/Materials/MaterialValidation.h` | 规划文件 | P1 | 声明definition、instance、shader反射接口之间的参数类型和能力验证。 | 依赖Materials及Shaders反射公共数据；不创建或缓存GPU pipeline。 |
| `Catalpa/src/Materials/MaterialValidation.cpp` | 规划文件 | P1 | 检查缺失字段、类型/纹理语义、状态覆盖限制和schema版本，返回可诊断错误。 | 依赖Materials及Shaders反射公共数据；不创建或缓存GPU pipeline。 |
| `Catalpa/src/Materials/MaterialAssetLoader.h` | 规划文件 | P1 | 声明definition/instance/schema的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Materials/MaterialAssetLoader.cpp` | 规划文件 | P1 | 有界解析并校验definition/instance/schema及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-14"></a>

## 14. Camera

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Camera/CameraLens.h` | 规划文件 | P1 | 定义透视/正交lens、FOV弧度、近远面与reversed-Z等显式参数。 | 依赖Core Math；不绑定Window或Editor camera controller。 |
| `Catalpa/src/Camera/CameraProjection.h` | 规划文件 | P1 | 声明按CoordinateConvention构造/验证投影及jitter的数学接口。 | 依赖CameraLens和Core Math；不includeVulkan头，Y方向适配只由约定的一处完成。 |
| `Catalpa/src/Camera/CameraProjection.cpp` | 规划文件 | P1 | 实现透视/正交投影和参数检查，明确深度范围、有限/无限远面与jitter应用位置。 | 依赖CameraLens和Core Math；不includeVulkan头，Y方向适配只由约定的一处完成。 |
| `Catalpa/src/Camera/CameraMatrices.h` | 规划文件 | P1 | 定义camera位置、朝向、view/projection/逆矩阵及必要历史值的稳定快照。 | 依赖Core Math和CameraLens；不持有Entity组件指针或交换链。 |
| `Catalpa/src/Camera/Frustum.h` | 规划文件 | P1 | 声明按既定clip-space和矩阵约定提取/测试视锥的接口。 | 依赖CameraProjection和Geometry Bounds公共值；不写World或GPU状态。 |
| `Catalpa/src/Camera/Frustum.cpp` | 规划文件 | P1 | 实现平面归一化及bounds测试，处理reversed-Z/无限远面而不混用公式。 | 依赖CameraProjection和Geometry Bounds公共值；不写World或GPU状态。 |

<a id="module-15"></a>

## 15. Lighting

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Lighting/LightParameters.h` | 规划文件 | P1 | 定义方向/点/聚光源的颜色、强度、范围、锥角和阴影偏好数据。 | 依赖Core Math；不包含shadow-map或GPU light buffer。 |
| `Catalpa/src/Lighting/LightUnits.h` | 规划文件 | P1 | 声明各光源强度采用的物理或明确标注的艺术单位、世界长度换算及颜色线性约定。 | 依赖Core；不能在没有光源类型/分布前提时将lumen、candela、lux直接混用。 |
| `Catalpa/src/Lighting/EnvironmentAsset.h` | 规划文件 | P2 | 定义天空/环境图像资产引用、方向和环境参数，区别源环境数据与GPU预过滤表现。 | 依赖Images/Assets公共契约；不持有cubemap VkImage或渲染pass。 |
| `Catalpa/src/Lighting/LightValidation.h` | 规划文件 | P1 | 声明光源参数、单位、范围及聚光锥角合法性检查。 | 依赖Lighting数据与Core Result；不依赖Renderer实现。 |
| `Catalpa/src/Lighting/LightValidation.cpp` | 规划文件 | P1 | 检查负强度、非有限颜色/方向和不合法锥角，输出问题而非静默夹取输入。 | 依赖Lighting数据与Core Result；不依赖Renderer实现。 |
| `Catalpa/src/Lighting/EnvironmentSettings.h` | 规划文件 | P1 | 定义场景天空/雾/曝光参数，区别EnvironmentAsset内容和GPU表现。 | Core/Math和资产句柄，无Vk。 |
| `Catalpa/src/Lighting/EnvironmentAssetLoader.h` | 规划文件 | P1 | 声明环境贴图/预计算的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Lighting/EnvironmentAssetLoader.cpp` | 规划文件 | P1 | 有界解析并校验环境贴图/预计算及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-16"></a>

## 16. Animation

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Animation/SkeletonAsset.h` | 规划文件 | P3 | 定义骨骼稳定标识、父子层级和bind pose的可复用资产数据。 | 依赖Assets与Core Math；不持有World实体或GPU bone buffer。 |
| `Catalpa/src/Animation/AnimationTrack.h` | 规划文件 | P3 | 定义带时间单位和插值模式的translation/rotation/scale及可选morph轨道。 | 依赖Core Math；四元数轨道与线性数值轨道使用各自明确插值语义。 |
| `Catalpa/src/Animation/AnimationClip.h` | 规划文件 | P3 | 定义clip时长、轨道、skeleton绑定与事件标记，不保存每个实例的播放时间。 | 依赖AnimationTrack、Assets和AnimationEvent；可复用且发布后不可变。 |
| `Catalpa/src/Animation/AnimationEvent.h` | 规划文件 | P3 | 定义clip事件标识、时刻和载荷，明确跨循环、多tick采样及事件交接。 | 依赖Core值类型；事件不保存组件地址或直接调用音频backend。 |
| `Catalpa/src/Animation/Pose.h` | 规划文件 | P3 | 定义每实例local/model pose和必要前后姿态缓冲，明确bone index与skeleton映射。 | 依赖Core Math和SkeletonAsset公共数据；GPU上传由render提取后执行。 |
| `Catalpa/src/Animation/AnimatorState.h` | 规划文件 | P3 | 定义每实例播放时间、速度、循环、当前clip及CPU pose状态，禁止多个实体共享可变时间。 | 依赖Animation数据及Assets句柄；不持有Scene、设备或第三方动画库对象。 |
| `Catalpa/src/Animation/AnimationSampler.h` | 规划文件 | P3 | 声明按时间采样轨道、构造local pose和处理边界的算法接口。 | 依赖Animation数据和Core Math；不修改AnimationClip原始轨道。 |
| `Catalpa/src/Animation/AnimationSampler.cpp` | 规划文件 | P3 | 实现插值、循环和关键帧边界检查，明确缺失轨道fallback与非有限输入报告。 | 依赖Animation数据和Core Math；不修改AnimationClip原始轨道。 |
| `Catalpa/src/Animation/AnimationPlayer.h` | 规划文件 | P3 | 声明推进AnimatorState、提取事件和可选root motion的接口。 | 依赖AnimatorState与AnimationSampler；root motion由Runtime系统与Physics协调。 |
| `Catalpa/src/Animation/AnimationPlayer.cpp` | 规划文件 | P3 | 按模拟delta推进播放并输出采样需求、事件和运动增量，不直接写World Transform。 | 依赖AnimatorState与AnimationSampler；root motion由Runtime系统与Physics协调。 |
| `Catalpa/src/Animation/PoseBlend.h` | 规划文件 | P3 | 声明基于同一skeleton映射的pose混合与权重语义。 | 依赖Pose和Core Math；BlendTree/复杂状态机属于可选上层。 |
| `Catalpa/src/Animation/PoseBlend.cpp` | 规划文件 | P3 | 执行translation/scale混合和旋转插值，检查权重及骨骼兼容，不静默改变源pose。 | 依赖Pose和Core Math；BlendTree/复杂状态机属于可选上层。 |
| `Catalpa/src/Animation/SkeletonValidation.h` | 规划文件 | P3 | 声明骨骼层级、bind pose、clip target和mesh skin binding兼容性检查。 | 依赖Animation与Geometry skin公共数据；不自动重定向或覆盖原始骨骼数据。 |
| `Catalpa/src/Animation/SkeletonValidation.cpp` | 规划文件 | P3 | 检测层级环、骨骼映射缺失、非法变换及不兼容clip，输出具体bone/track诊断。 | 依赖Animation与Geometry skin公共数据；不自动重定向或覆盖原始骨骼数据。 |
| `Catalpa/src/Animation/BlendTreeAsset.h` | 规划文件 | P4 | 可选：定义已验证的混合树节点、参数schema与clip引用；仅在需要多clip驱动时引入。 | 依赖Animation/Assets公共数据；必须配套执行器和循环校验，不能把空节点类视为完成。 |
| `Catalpa/src/Animation/AnimationAssetLoader.h` | 规划文件 | P3 | 声明骨架/clip/joint映射的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Animation/AnimationAssetLoader.cpp` | 规划文件 | P3 | 有界解析并校验骨架/clip/joint映射及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Animation/BlendTreeEvaluator.h` | 规划文件 | P4 | 声明BlendTree参数、无环依赖与pose输出。 | AnimationSampler/PoseBlend/AnimatorState。 |
| `Catalpa/src/Animation/BlendTreeEvaluator.cpp` | 规划文件 | P4 | 校验骨架，采样/混合到实例Pose，不持有World/GPU对象。 | AnimationSampler/PoseBlend/AnimatorState。 |

<a id="module-17"></a>

## 17. Physics

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Physics/PhysicsTypes.h` | 规划文件 | P3 | 定义BodyHandle、WorldHandle、碰撞层和物理世界配置；明确米、秒及轴约定。 | 依赖Core数学与句柄；查询结构在PhysicsQuery定义，不暴露Jolt类型。 |
| `Catalpa/src/Physics/PhysicsMaterialAsset.h` | 规划文件 | P3 | 定义摩擦、恢复系数及合并策略的只读资产数据。 | 依赖Assets标识；不同于渲染MaterialDefinition，不保存SDK对象。 |
| `Catalpa/src/Physics/CollisionShapeAsset.h` | 规划文件 | P3 | 定义可烘焙碰撞形状、几何引用和版本化参数。 | 依赖Geometry与Assets数据；不保存Jolt Shape指针。 |
| `Catalpa/src/Physics/PhysicsEvents.h` | 规划文件 | P3 | 定义接触开始、持续、结束与触发器事件的值数据。 | 只含句柄、数值和事件ID；SDK回调不能直接操作ECS。 |
| `Catalpa/src/Physics/PhysicsService.h` | 规划文件 | P3 | 声明SDK共享资源、PhysicsWorld创建销毁及共享碰撞形状服务。 | 依赖Core/Jobs、Assets和PhysicsWorld；不读取World或ECS，不依赖Editor。 |
| `Catalpa/src/Physics/PhysicsService.cpp` | 规划文件 | P3 | 协调后端工厂、共享初始化与物理世界生命周期，返回受控世界句柄。 | 依赖Core/Jobs、Assets和PhysicsWorld；不读取World或ECS，不依赖Editor。 |
| `Catalpa/src/Physics/Backends/IPhysicsBackend.h` | 规划文件 | P3 | 定义身体创建、更新、查询、事件取出和关闭的私有后端契约。 | 只使用引擎数据；世界backend与SDK共享上下文的寿命由PhysicsService协调。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltPhysicsBackend.h` | 规划文件 | P3 | 声明Jolt单个物理世界、身体映射和形状缓存的私有实现。 | 私有依赖Jolt；初期采用CPU物理，精度、SIMD和构建宏与库一致。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltPhysicsBackend.cpp` | 规划文件 | P3 | 管理Body、Shape、查询与模拟；共享SDK上下文由服务创建并在所有世界关闭后释放。 | 私有依赖Jolt；初期采用CPU物理，精度、SIMD和构建宏与库一致。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltContactListener.h` | 规划文件 | P3 | 声明接触与激活通知的私有监听器。 | 私有依赖Jolt和PhysicsEvents；不把后端回调对象传到ECS。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltContactListener.cpp` | 规划文件 | P3 | 线程安全地收集引擎事件，在Runtime安全阶段消费；回调不增删实体。 | 私有依赖Jolt和PhysicsEvents；不把后端回调对象传到ECS。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltJobSystemAdapter.h` | 规划文件 | P3 | 声明Jolt作业到引擎JobSystem的适配。 | 私有依赖Jolt和Core/Jobs；不得用RHI完成点表达CPU任务依赖。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltJobSystemAdapter.cpp` | 规划文件 | P3 | 适配依赖和CPU完成等待，避免创建另一套不受控线程池。 | 私有依赖Jolt和Core/Jobs；不得用RHI完成点表达CPU任务依赖。 |
| `Catalpa/src/Physics/Backends/Jolt/JoltConversions.h` | 规划文件 | P3 | 实现受控的坐标、旋转、形状参数和碰撞过滤内联转换。 | 私有头；检查单位、矩阵方向和精度，不传播到组件。 |
| `Catalpa/src/Physics/PhysicsWorld.h` | 规划文件 | P3 | 声明单个物理世界的身体、命令、查询及模拟接口。 | PhysicsService创建管理；依赖PhysicsQuery和IPhysicsBackend，SDK对象不离开backend。 |
| `Catalpa/src/Physics/PhysicsWorld.cpp` | 规划文件 | P3 | 持有私有backend、Body映射和事件队列；消费值命令并输出状态，不读取ECS。 | PhysicsService创建管理；依赖PhysicsQuery和IPhysicsBackend，SDK对象不离开backend。 |
| `Catalpa/src/Physics/PhysicsQuery.h` | 规划文件 | P3 | 定义Raycast、Sweep、Overlap请求、过滤和命中结果。 | 依赖PhysicsTypes与Core数学；只返回BodyHandle和值，不返回Jolt指针。 |
| `Catalpa/src/Physics/CollisionAssetLoader.h` | 规划文件 | P3 | 声明碰撞形状/物理材质的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Physics/CollisionAssetLoader.cpp` | 规划文件 | P3 | 有界解析并校验碰撞形状/物理材质及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Physics/CharacterController.h` | 规划文件 | P4 | 声明角色运动意图、碰撞约束和结果。 | PhysicsWorld/Query，Runtime桥接。 |
| `Catalpa/src/Physics/CharacterController.cpp` | 规划文件 | P4 | 调用私有角色能力，返回接地/位置信息；不直接写World或同时创建竞争dynamic权威。 | PhysicsWorld/Query，Runtime桥接。 |

<a id="module-18"></a>

## 18. Audio

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Audio/AudioTypes.h` | 规划文件 | P3 | 定义VoiceHandle、总线、播放状态和设备采样配置。 | 只含引擎类型；明确音频采样帧不同于GPU渲染帧。 |
| `Catalpa/src/Audio/AudioClipAsset.h` | 规划文件 | P3 | 定义音频编码、通道、采样率、时长及流送块元数据。 | 依赖Assets只读字节与标识；不包含ma_sound和解码器实例。 |
| `Catalpa/src/Audio/AudioCommands.h` | 规划文件 | P3 | 定义播放、停止、Seek及空间属性更新的有界值命令。 | 明确序号、所有权与失效规则；离散命令不能像过期渲染快照随意丢弃。 |
| `Catalpa/src/Audio/AudioService.h` | 规划文件 | P3 | 声明播放、设备、总线和关闭服务，接受值命令与空间快照。 | 依赖IAudioBackend及AudioStreamService；不读取ECS，不等待GPU完成。 |
| `Catalpa/src/Audio/AudioService.cpp` | 规划文件 | P3 | 管理voice、后端、流送与stop确认，消费命令并返回事件。 | 依赖IAudioBackend及AudioStreamService；不读取ECS，不等待GPU完成。 |
| `Catalpa/src/Audio/AudioCommandQueue.h` | 规划文件 | P3 | 声明有界音频命令队列及容量、满队列、停止和确认协议。 | 依赖Core并发基础；独立于Renderer帧队列，保留Play/Stop顺序。 |
| `Catalpa/src/Audio/AudioCommandQueue.cpp` | 规划文件 | P3 | 实现预分配消息传递；实时消费路径不等待普通job、不随意分配或同步log。 | 依赖Core并发基础；独立于Renderer帧队列，保留Play/Stop顺序。 |
| `Catalpa/src/Audio/AudioStreamService.h` | 规划文件 | P3 | 声明解码、预读及流送缓冲的生命周期。 | 依赖Assets/VFS与Core/Jobs；callback只消费就绪数据，stop确认后才释放缓冲。 |
| `Catalpa/src/Audio/AudioStreamService.cpp` | 规划文件 | P3 | 非实时worker读取资产并填充就绪缓冲，上报欠载、取消与解码错误。 | 依赖Assets/VFS与Core/Jobs；callback只消费就绪数据，stop确认后才释放缓冲。 |
| `Catalpa/src/Audio/Backends/IAudioBackend.h` | 规划文件 | P3 | 定义设备启动、命令消费、状态和停止确认的私有契约。 | 只使用引擎音频值数据；允许明确禁用音频的Null后端。 |
| `Catalpa/src/Audio/Backends/Miniaudio/MiniaudioBackend.h` | 规划文件 | P3 | 声明miniaudio设备、mixer与voice池的私有状态。 | 私有依赖miniaudio；对象地址稳定，原生ABI不跨模块公开。 |
| `Catalpa/src/Audio/Backends/Miniaudio/MiniaudioBackend.cpp` | 规划文件 | P3 | 管理输出回调和设备；callback不start/stop/uninit设备，控制线程处理关闭与确认。 | 私有依赖miniaudio；对象地址稳定，原生ABI不跨模块公开。 |
| `Catalpa/src/Audio/Backends/Null/NullAudioBackend.h` | 规划文件 | P3 | 声明无音频设备或明确禁用输出时的Null后端。 | 实现IAudioBackend；真实设备初始化失败须报告，不静默伪装成功。 |
| `Catalpa/src/Audio/Backends/Null/NullAudioBackend.cpp` | 规划文件 | P3 | 维持无声状态、句柄释放与stop确认，供工具或headless运行使用。 | 实现IAudioBackend；真实设备初始化失败须报告，不静默伪装成功。 |
| `Catalpa/src/Audio/AudioAssetLoader.h` | 规划文件 | P3 | 声明clip块/seek/采样信息的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Audio/AudioAssetLoader.cpp` | 规划文件 | P3 | 有界解析并校验clip块/seek/采样信息及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-19"></a>

## 19. Navigation

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Navigation/NavigationTypes.h` | 规划文件 | P4 | 定义Agent配置、区域标记、世界句柄与导航单位。 | 只用引擎数学；不暴露Detour结构。 |
| `Catalpa/src/Navigation/NavMeshAsset.h` | 规划文件 | P4 | 定义tile目录、烘焙参数、版本及导航字节块引用。 | 依赖Assets；源几何在Tools处理，Runtime不导入模型。 |
| `Catalpa/src/Navigation/NavigationWorld.h` | 规划文件 | P4 | 声明导航世界、tile装载、路径请求与结果接口。 | 依赖INavigationBackend、NavMeshAsset与Core/Jobs；不读取World或ECS。 |
| `Catalpa/src/Navigation/NavigationWorld.cpp` | 规划文件 | P4 | 管理tile和有界查询，返回值结果，不直接移动场景实体。 | 依赖INavigationBackend、NavMeshAsset与Core/Jobs；不读取World或ECS。 |
| `Catalpa/src/Navigation/Backends/INavigationBackend.h` | 规划文件 | P4 | 定义tile装载、查询与关闭的私有后端契约。 | 只使用引擎请求和结果；Recast构建不进入运行时。 |
| `Catalpa/src/Navigation/Backends/Detour/DetourNavigationBackend.h` | 规划文件 | P4 | 声明Detour网格、tile与查询上下文的私有持有者。 | 私有依赖Detour；Crowd与TileCache按需，不能等同完整AI或世界流送。 |
| `Catalpa/src/Navigation/Backends/Detour/DetourNavigationBackend.cpp` | 规划文件 | P4 | 装载烘焙tile并执行查询，隔离并发查询上下文与网格修改时序。 | 私有依赖Detour；Crowd与TileCache按需，不能等同完整AI或世界流送。 |
| `Catalpa/src/Navigation/NavigationQuery.h` | 规划文件 | P4 | 定义路径、最近点及区域查询的请求ID、预算和结果。 | 依赖NavigationTypes；World同步与移动权威由Runtime桥协调。 |
| `Catalpa/src/Navigation/NavMeshAssetLoader.h` | 规划文件 | P4 | 声明导航tile/agent参数的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Navigation/NavMeshAssetLoader.cpp` | 规划文件 | P4 | 有界解析并校验导航tile/agent参数及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-20"></a>

## 20. Scripting

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Scripting/ScriptAsset.h` | 规划文件 | P3 | 定义脚本源码或目标版本字节码、模块入口与导出信息。 | 依赖Assets；记录Lua版本，不假设跨版本字节码兼容。 |
| `Catalpa/src/Scripting/ScriptValue.h` | 规划文件 | P3 | 定义脚本边界允许的数值、字符串、资产和实体句柄值。 | 依赖Core句柄；明确复制与寿命，不传组件裸引用。 |
| `Catalpa/src/Scripting/ScriptCommands.h` | 规划文件 | P3 | 定义脚本请求的引擎操作值对象，稳定句柄表达目标。 | 依赖ScriptValue；由Runtime桥校验并转换为EntityCommandBuffer。 |
| `Catalpa/src/Scripting/ScriptRuntime.h` | 规划文件 | P3 | 声明VM、模块、实例句柄、受保护调用及重载服务。 | 依赖ScriptApi、Assets和IScriptBackend；不读取World，VM有明确owner线程。 |
| `Catalpa/src/Scripting/ScriptRuntime.cpp` | 规划文件 | P3 | 管理VM和实例，返回值命令及错误；重载失败不发布半初始化实例。 | 依赖ScriptApi、Assets和IScriptBackend；不读取World，VM有明确owner线程。 |
| `Catalpa/src/Scripting/ScriptApi.h` | 规划文件 | P3 | 定义暴露给脚本的窄服务表、句柄校验与命令提交契约。 | 服务表由Runtime实现；不向Scripting暴露World、Renderer内部或Editor对象。 |
| `Catalpa/src/Scripting/Backends/IScriptBackend.h` | 规划文件 | P3 | 定义模块装载、实例调用、错误取出与关闭契约。 | 仅ScriptValue及引擎句柄；公共接口不出现Lua类型。 |
| `Catalpa/src/Scripting/Backends/Lua/LuaScriptBackend.h` | 规划文件 | P3 | 声明Lua状态、注册表引用与受控allocator的私有实现。 | 私有依赖锁定Lua版本；sol2仅经编译器及Lua组合验证后作为可选便利层。 |
| `Catalpa/src/Scripting/Backends/Lua/LuaScriptBackend.cpp` | 规划文件 | P3 | 管理加载、保护调用、GC预算和实例寿命；同一state不并发进入。 | 私有依赖锁定Lua版本；sol2仅经编译器及Lua组合验证后作为可选便利层。 |
| `Catalpa/src/Scripting/Backends/Lua/LuaBindings.h` | 规划文件 | P3 | 声明ScriptApi到Lua的注册与类型转换。 | 私有依赖Lua及ScriptApi；不在组件头文件注册绑定。 |
| `Catalpa/src/Scripting/Backends/Lua/LuaBindings.cpp` | 规划文件 | P3 | 校验句柄和失效对象，转换调用错误，阻止脚本长期持有失效C++对象。 | 私有依赖Lua及ScriptApi；不在组件头文件注册绑定。 |
| `Catalpa/src/Scripting/ScriptAssetLoader.h` | 规划文件 | P3 | 声明模块/字节码版本的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Scripting/ScriptAssetLoader.cpp` | 规划文件 | P3 | 有界解析并校验模块/字节码版本及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-21"></a>

## 21. UI

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/UI/UIDocumentAsset.h` | 规划文件 | P3 | 定义RML、样式、字体与图片依赖的运行时UI资产。 | 依赖Assets；RmlUi文档和上下文仅在backend持有。 |
| `Catalpa/src/UI/UIEvent.h` | 规划文件 | P3 | 定义键鼠、文本输入及UI动作的值事件。 | 输入来自平台层；文本、DPI和IME能力按目标平台验证。 |
| `Catalpa/src/UI/UIContext.h` | 规划文件 | P3 | 声明文档、数据模型、输入、布局及绘制数据服务。 | 依赖IUIBackend、UIDrawData与Assets；不读取ECS或Editor。 |
| `Catalpa/src/UI/UIContext.cpp` | 规划文件 | P3 | 维护文档与值模型，输出绘制快照并返回值事件；关闭前确认引用已退役。 | 依赖IUIBackend、UIDrawData与Assets；不读取ECS或Editor。 |
| `Catalpa/src/UI/Backends/IUIBackend.h` | 规划文件 | P3 | 定义文档装载、数据更新、输入、绘制提取及能力查询契约。 | 仅引擎类型；未支持的UI效果须明确诊断，不能宣称完整浏览器或完整RmlUi渲染能力。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiBackend.h` | 规划文件 | P3 | 声明RmlUi上下文、文档和数据模型的私有持有者。 | 私有依赖RmlUi；不让其原生对象进入组件或公共SDK。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiBackend.cpp` | 规划文件 | P3 | 管理生命周期与事件回调；文档释放与渲染快照退休分开，暴露实际支持能力。 | 私有依赖RmlUi；不让其原生对象进入组件或公共SDK。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiFileInterface.h` | 规划文件 | P3 | 声明UI资源到Assets/VFS的私有只读访问适配。 | 私有依赖RmlUi与Assets/VFS；不访问Editor导入中间目录。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiFileInterface.cpp` | 规划文件 | P3 | 装载样式、字体与图片依赖，拒绝无效路径并保持资源版本存活。 | 私有依赖RmlUi与Assets/VFS；不访问Editor导入中间目录。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiRenderInterface.h` | 规划文件 | P3 | 声明几何、纹理、translation/transform和scissor到UIDrawData的转换。 | 私有依赖RmlUi与Renderer/RHI；clip mask、layer、filter等高级效果须逐项声明支持范围。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiRenderInterface.cpp` | 规划文件 | P3 | 输出带变换与裁剪的批次，匹配生成纹理的预乘alpha混合；动态字形纹理经RHI上传并安全退休。 | 私有依赖RmlUi与Renderer/RHI；clip mask、layer、filter等高级效果须逐项声明支持范围。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiSystemInterface.h` | 规划文件 | P3 | 声明计时、日志、剪贴板与平台服务适配。 | 依赖Core/Platform；RmlUi类型只在backend可见，不操控Editor状态。 |
| `Catalpa/src/UI/Backends/RmlUi/RmlUiSystemInterface.cpp` | 规划文件 | P3 | 转发受控平台服务并遵守UI线程归属，字体shaping/fallback与IME按产品目标验证。 | 依赖Core/Platform；RmlUi类型只在backend可见，不操控Editor状态。 |
| `Catalpa/src/UI/FontAsset.h` | 规划文件 | P3 | 定义字体字节、face、字重、字符覆盖和fallback依赖信息。 | 依赖Assets只读数据；字体解析、shaping状态和字形图集归私有backend及Renderer资源服务。 |
| `Catalpa/src/UI/UIDrawData.h` | 规划文件 | P3 | 定义稳定UI批次：顶点索引、纹理引用、translation/transform、scissor与必要绘制状态。 | Renderer可消费并保活资源版本；明确预乘alpha/色彩空间及高级效果能力，不暴露RmlUi或Vk类型。 |
| `Catalpa/src/UI/UIAssetLoader.h` | 规划文件 | P3 | 声明文档/样式/字体的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/UI/UIAssetLoader.cpp` | 规划文件 | P3 | 有界解析并校验文档/样式/字体及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-22"></a>

## 22. Particles

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Particles/ParticleTypes.h` | 规划文件 | P4 | 定义发射形状、模拟参数和基础粒子状态。 | 只用引擎数学；不承诺完整GPU粒子或图形化特效系统。 |
| `Catalpa/src/Particles/ParticleEffectAsset.h` | 规划文件 | P4 | 定义发射器、曲线、材质引用及效果格式版本。 | 依赖Assets与Materials句柄；不持有可变粒子池或GPU资源。 |
| `Catalpa/src/Particles/ParticleSimulation.h` | 规划文件 | P4 | 声明有界CPU粒子实例池及值参数模拟入口。 | 依赖Core数学、Jobs和ParticleEffectAsset；不读取ECS或World。 |
| `Catalpa/src/Particles/ParticleSimulation.cpp` | 规划文件 | P4 | 推进发射、寿命和运动并输出稳定状态，明确容量与失效策略。 | 依赖Core数学、Jobs和ParticleEffectAsset；不读取ECS或World。 |
| `Catalpa/src/Particles/ParticleAssetLoader.h` | 规划文件 | P4 | 声明效果/曲线/容量的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Particles/ParticleAssetLoader.cpp` | 规划文件 | P4 | 有界解析并校验效果/曲线/容量及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-23"></a>

## 23. Terrain

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Terrain/TerrainTypes.h` | 规划文件 | P4 | 定义tile坐标、层级、采样间距和请求状态。 | 单位与Scene/Physics一致；不承诺无限世界或行星地形。 |
| `Catalpa/src/Terrain/TerrainAsset.h` | 规划文件 | P4 | 定义高度tile、材质层及LOD元数据的资产引用。 | 依赖Assets只读数据；原始高度素材由Tools导入。 |
| `Catalpa/src/Terrain/TerrainService.h` | 规划文件 | P4 | 声明地形实例、就绪tile及可见集合的领域服务。 | 依赖TerrainAsset和TerrainTileStreamer；不读取ECS，场景与Physics由Runtime桥协调。 |
| `Catalpa/src/Terrain/TerrainService.cpp` | 规划文件 | P4 | 根据相机与配置快照协调tile、LOD与退休并返回值结果。 | 依赖TerrainAsset和TerrainTileStreamer；不读取ECS，场景与Physics由Runtime桥协调。 |
| `Catalpa/src/Terrain/TerrainTileStreamer.h` | 规划文件 | P4 | 声明有界tile请求、CPU缓存预算与异步通知。 | 依赖Assets/VFS及Core/Jobs；CPU tile缓存退休不同于GPU资源退休。 |
| `Catalpa/src/Terrain/TerrainTileStreamer.cpp` | 规划文件 | P4 | 读取烘焙tile，限制在途任务和内存占用并正确处理取消。 | 依赖Assets/VFS及Core/Jobs；CPU tile缓存退休不同于GPU资源退休。 |
| `Catalpa/src/Terrain/TerrainAssetLoader.h` | 规划文件 | P4 | 声明tile/层级/高度的cooked解析及AssetLoader适配。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |
| `Catalpa/src/Terrain/TerrainAssetLoader.cpp` | 规划文件 | P4 | 有界解析并校验tile/层级/高度及依赖，返回CPU版本与诊断；不创建GPU/voice/body实例。 | Assets通用协议和本域类型；RuntimeRegistration注册。 |

<a id="module-24"></a>

## 24. Networking

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Networking/NetworkTypes.h` | 规划文件 | P4 | 定义连接句柄、通道、可靠性及transport错误状态。 | 只定义传输契约，不等同replication、预测或服务器权威。 |
| `Catalpa/src/Networking/NetworkMessage.h` | 规划文件 | P4 | 定义有界消息字节、协议版本及连接归属。 | 显式序列化；不发送组件内存布局或第三方对象指针。 |
| `Catalpa/src/Networking/INetworkTransport.h` | 规划文件 | P4 | 定义连接、发送、轮询及关闭的最低transport扩展接口。 | 不依赖ECS/Renderer；真实跨机器后端需独立选择和验证。 |
| `Catalpa/src/Networking/NetworkSession.h` | 规划文件 | P4 | 声明transport生命周期、消息预算与应用消费阶段。 | 依赖INetworkTransport和Core队列；不隐含自动状态复制。 |
| `Catalpa/src/Networking/NetworkSession.cpp` | 规划文件 | P4 | 驱动轮询并在安全阶段交付消息，网络线程不直接修改World。 | 依赖INetworkTransport和Core队列；不隐含自动状态复制。 |
| `Catalpa/src/Networking/Backends/Loopback/LoopbackTransport.h` | 规划文件 | P4 | 声明进程内回环transport，供会话与协议集成验证。 | 实现INetworkTransport；GameNetworkingSockets为后续可选适配，不承诺成熟多人产品能力。 |
| `Catalpa/src/Networking/Backends/Loopback/LoopbackTransport.cpp` | 规划文件 | P4 | 实现有界内存连接和消息投递，明确没有跨机器网络能力。 | 实现INetworkTransport；GameNetworkingSockets为后续可选适配，不承诺成熟多人产品能力。 |

<a id="module-25"></a>

## 25. Shaders

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Shaders/ShaderAsset.h` | 规划文件 | P1 | 定义已烘焙shader资产，保存bytecode blob、entry/stage、metadata与内容版本引用；不保存VkShaderModule或编译器对象。 | 依赖Assets身份/存储、Core和ShaderMetadata；不依赖Renderer、Vulkan或源码编译器。 |
| `Catalpa/src/Shaders/ShaderMetadata.h` | 规划文件 | P1 | 定义可序列化shader接口schema：bindings、push constants、block成员offset/stride、vertex I/O与target environment，含schema版本。 | 仅依赖Core和RHI中立值类型；工具反射输出必须转换为本schema，不泄漏SPIRV-Reflect结构。 |
| `Catalpa/src/Shaders/ShaderVariantKey.h` | 规划文件 | P1 | 定义shader内容、defines/feature选项、entry、target与编译设置摘要组成的稳定variant键，不使用进程指针或临时type hash。 | 依赖Core哈希/值类型；编译器版本和include内容依赖由Tools纳入构建键。 |
| `Catalpa/src/Shaders/ShaderLibrary.h` | 规划文件 | P1 | 声明已烘焙shader资产与variant的加载、查询、版本失效和CPU字节码保留接口。 | 依赖Assets、Core及Shaders值类型；不在runtime强链接glslang/DXC/Slang，也不持有Vk对象。 |
| `Catalpa/src/Shaders/ShaderLibrary.cpp` | 规划文件 | P1 | 注册ShaderAsset的AssetLoader解析适配，通过AssetContainerReader校验字节码/Metadata/schema后由AssetManager发布；管理variant查询，源码编译由Tools完成。 | 依赖Assets、Core及Shaders值类型；不在runtime强链接glslang/DXC/Slang，也不持有Vk对象。 |

<a id="module-26"></a>

## 26. RHI

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/RHI/RHITypes.h` | 规划文件 | P0 | 定义字节偏移、Extent、Rect、DeviceId、QueueId等RHI基础值类型及明确单位，不承载所有领域枚举。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIError.h` | 规划文件 | P0 | 定义RHI错误码、诊断信息与可失败结果契约，区分不支持、无效描述、内存不足和设备丢失。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIHandle.h` | 规划文件 | P0 | 定义Buffer、Texture、View、Sampler、Shader、Pipeline、Binding等强类型带generation句柄及无效值；句柄本身不隐含资源所有权。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIBackend.h` | 规划文件 | P0 | 定义GraphicsBackend选择和后端标识；后端选择仅在设备创建和能力协商阶段生效。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHICapabilities.h` | 规划文件 | P0 | 定义设备特性、格式支持、对齐、绑定上限与队列能力查询结果，不把枚举存在等同于硬件支持。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIDeviceDesc.h` | 规划文件 | P0 | 定义设备创建参数、adapter偏好、required/optional能力与调试配置；窗口以Core不透明窗口令牌或surface服务注入。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIFormat.h` | 规划文件 | P0 | 引用单一Images/PixelFormat.h中的像素格式定义，提供RHI共享别名和按用途查询的格式能力描述，不再定义第二套格式枚举；区分格式身份与设备支持情况。 | 仅依赖Images/PixelFormat.h这一轻量值类型头、Core及RHI；不依赖图像解码器、Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIResourceAccess.h` | 规划文件 | P0 | 定义图与后端共同使用的stage/access、子资源范围和资源使用语义；不保存全局当前状态或VkImageLayout数值。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIBufferDesc.h` | 规划文件 | P0 | 定义buffer大小、用途flags、memory意图和映射要求；尺寸/偏移单位均为字节。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHITextureDesc.h` | 规划文件 | P0 | 定义纹理尺寸、mip、层、sample count、用途与独立view描述；禁止隐式丢弃子资源范围。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHISamplerDesc.h` | 规划文件 | P1 | 定义过滤、寻址、各向异性、比较采样与LOD范围，设备限制由创建时验证。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIShaderDesc.h` | 规划文件 | P0 | 定义已编译shader字节码、entry point、stage与已验证接口元数据引用，不依赖源码编译器类型。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIPipelineDesc.h` | 规划文件 | P0 | 定义graphics/compute pipeline状态、shader组合和attachment格式契约；不把业务材质语义放入RHI。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIBindingDesc.h` | 规划文件 | P0 | 定义绑定布局、binding类型、资源数组、更新范围与动态offset契约，屏蔽descriptor pool和VkDescriptorSet。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIRenderingDesc.h` | 规划文件 | P0 | 定义开始渲染所需attachment/view、load/store、clear、render area与sample count；不向上层暴露原生renderpass/framebuffer对象。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHICommandTypes.h` | 规划文件 | P0 | 定义copy/blit/resolve、draw/dispatch参数与barrier描述，区分buffer字节范围与texture子资源范围。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHISubmission.h` | 规划文件 | P0 | 定义队列提交的command lists、GPU wait/signal与接受结果；可指定细粒度stage的GPU依赖不能缩窄整批CPU完成点语义，失败提交不得产生有效完成点。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIGpuDependency.h` | 规划文件 | P0 | 定义可供后续GPU提交等待的依赖令牌及目标stage，包含binary消费约束或timeline值语义；不充当CPU完成证明。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHICompletionPoint.h` | 规划文件 | P0 | 定义host可查询的整批提交完成点，包含device epoch、queue身份和单调序列；必须覆盖被标记提交的全部资源使用，不等同于任意stage signal或跨队列GPU wait。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHICompletionSet.h` | 规划文件 | P0 | 定义多队列last-use完成点集合、合并与已完成判定规则，为帧复用和资源释放表达全部完成条件。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHISwapchainDesc.h` | 规划文件 | P0 | 定义呈现格式/模式、extent、图像数量偏好、acquire/present结果及重建原因；显式表达最小化、过期和surface丢失。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIQueryDesc.h` | 规划文件 | P2 | 定义timestamp/统计query范围、结果状态及timestamp period等单位；可选能力不足时明确返回不支持。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHICommandList.h` | 规划文件 | P0 | 声明命令录制接口及begin/end状态、渲染作用域、拷贝、绑定和barrier契约；录制不直接修改已提交资源状态。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIDevice.h` | 规划文件 | P0 | 声明资源创建、句柄校验、映射、完成查询和释放请求接口；Release转交后端retirement，不承诺CPU调用时立即销毁。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIQueue.h` | 规划文件 | P0 | 声明队列提交与完成观察接口，串行化同一底层queue的提交；保留已编译计划要求的队列顺序。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHISwapchain.h` | 规划文件 | P0 | 声明acquire/present/resize接口与外部图像借用契约，向调用方返回GPU依赖及有效图像令牌。 | 仅依赖Core/Math和RHI值类型；公共头不包含Vulkan、VMA、GLFW或Renderer。 |
| `Catalpa/src/RHI/RHIBackendFactory.h` | 规划文件 | P0 | 声明后端工厂、注册入口和显式选择结果；上游仅通过此契约获得RHIDevice。 | 公共头仅依赖RHI；实现的链接组合可引用backend注册入口，backend私有头不传播给客户端。 |
| `Catalpa/src/RHI/RHIBackendFactory.cpp` | 规划文件 | P0 | 实现显式注册的后端工厂表和选择，验证请求并传播失败；实际Vulkan注册由Runtime组合根注入，不在RHI库中硬编码链接所有后端。 | 仅RHI/Core；接受注册函数值，不导出后端私有头。 |

<a id="module-27"></a>

## 27. Renderer

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Renderer/RendererConfig.h` | 规划文件 | P0 | 定义渲染路径、目标分辨率、quality与可选效果配置；默认值与设备能力协商后生效。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderFramePacket.h` | 规划文件 | P1 | 定义抽取阶段交付的不可变帧包，包含CameraMatrices、实例前后变换、lights、Geometry/Materials资产引用和骨骼pose数据；禁止保存ECS组件裸指针。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderView.h` | 规划文件 | P1 | 定义CameraMatrices、viewport、裁剪与RenderSurface输出目标引用，明确坐标/深度约定，不自行查询游戏世界。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphResource.h` | 规划文件 | P0 | 定义graph逻辑资源ID、导入/瞬态资源描述及子资源访问范围，逻辑ID不等于RHI资源句柄。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphPass.h` | 规划文件 | P0 | 定义pass声明、read/write、queue意图与录制回调契约；未声明的外部副作用须显式标注。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphPlan.h` | 规划文件 | P0 | 定义编译后不可变批次顺序、barrier/ownership计划、GPU依赖边、资源寿命及预期末状态；它是计划而非后端已提交状态。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Lighting/LightData.h` | 规划文件 | P1 | 定义directional/point/spot的渲染数据、强度/范围单位和阴影参数，与ECS light组件及GPU packing分别建契约。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/UI/ImGuiBackend.h` | 规划文件 | P2 | 声明ImGui绘制后端适配契约和纹理注册令牌，不含Vulkan；具体适配由独立integration目标注入。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Renderer.h` | 现有空文件 | P0 | 声明renderer入口、注入RHI设备与RenderFramePacket消费接口，不持有World或Editor。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Renderer.cpp` | 规划文件 | P0 | 按配置准备资源、生成views与passes，编译并执行graph，将帧结果和异步诊断返回调用方，不直接调用Vulkan。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderScene.h` | 规划文件 | P1 | 声明render线程自有场景代理、实例/灯光缓存及抽取包版本，不依赖ECS存储布局。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderScene.cpp` | 规划文件 | P1 | 应用RenderFramePacket增量并维护渲染代理身份，删除实例仅释放渲染引用，GPU资源仍按RHI完成条件退出。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderSurface.h` | 规划文件 | P2 | 声明只暴露引擎handle/RHI引用的离屏视口输出接口，保存extent、格式和代际；供Editor或游戏视图使用，不含Vk或原生descriptor。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderSurface.cpp` | 规划文件 | P2 | 创建和重建离屏RHI attachments，保留旧代到相关提交完成，发布可供UI采样的输出引用而非原生descriptor。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RendererFrameContext.h` | 规划文件 | P0 | 声明CPU帧暂存、帧包引用、graph实例与本帧CompletionSet；与VulkanFrameContext职责分离。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RendererFrameContext.cpp` | 规划文件 | P0 | 管理renderer帧slot的暂存分配和资产保留，确认本slot全部完成后复用，不通过swapchain image数量推断安全。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/FrameScheduler.h` | 规划文件 | P0 | 声明已编译RenderGraphPlan的录制/提交执行入口及提交失败策略；没有任意重排队列依赖的权限。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/FrameScheduler.cpp` | 规划文件 | P0 | 只按不可变plan批次顺序提交command lists并传递GPU依赖/CPU完成点；中途失败停止依赖后续批次，保留已接受批次的权威状态和资源引用，不把整张graph伪装成可完全回滚事务。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraph.h` | 规划文件 | P0 | 声明graph生命周期、pass/resource登记与编译入口，只拥有当前graph声明数据。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraph.cpp` | 规划文件 | P0 | 组织声明、编译、执行阶段与graph代际，校验编译后不可变性，清理临时数据不直接销毁原生GPU对象。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphBuilder.h` | 规划文件 | P0 | 声明pass构建器与资源read/write/import接口，要求明确range、usage和外部依赖。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphBuilder.cpp` | 规划文件 | P0 | 生成受验证的逻辑资源版本和访问边，拒绝非法range/未初始化读取/不兼容写入，不靠隐藏barrier补救错误声明。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphCompiler.h` | 规划文件 | P0 | 声明从资源访问声明到RenderGraphPlan的编译器，输入包含导入资源状态基线与队列能力。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphCompiler.cpp` | 规划文件 | P0 | 分析依赖、检测cycle、剔除无效pass、分配队列批次并推导barriers/ownership与跨队列GPU依赖；不修改后端已接受末状态。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphExecutor.h` | 规划文件 | P0 | 声明依据已编译计划录制pass命令和局部状态增量的执行器。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphExecutor.cpp` | 规划文件 | P0 | 按计划调用pass回调并记录barrier/rendering命令，可在独立command pool规则下并行录制；提交顺序仍完全由FrameScheduler执行计划。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphTransientResources.h` | 规划文件 | P1 | 声明graph逻辑资源到物理RHI资源的租用表及完成点驱动的复用池；P4可按显式alias契约扩展。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/RenderGraph/RenderGraphTransientResources.cpp` | 规划文件 | P1 | 按plan创建/租用瞬态images/buffers并绑定物理句柄，关联全部使用完成点；释放调用RHI，不在renderer实现原生deleter。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/GeometryGpuCache.h` | 规划文件 | P1 | 声明Geometry/MeshAsset到vertex/index及后续meshlet等RHI资源的派生缓存与版本键。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/GeometryGpuCache.cpp` | 规划文件 | P1 | 消费Geometry资产数据并安排上传、缓存命中和版本替换；旧GPU几何只通过RHI释放，原始资产保持不变。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/TextureGpuCache.h` | 规划文件 | P1 | 声明TextureAsset版本、view/sampler需求到RHI纹理资源的派生缓存，记录mip就绪状态。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/TextureGpuCache.cpp` | 规划文件 | P1 | 消费已解码/转码的纹理资产并发起上传，验证format/颜色空间/子资源；热替换保留旧引用直到GPU完成。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/MaterialGpuCache.h` | 规划文件 | P1 | 声明Materials/MaterialDefinition及MaterialInstance到参数buffer、binding及shader variant的GPU派生缓存。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/MaterialGpuCache.cpp` | 规划文件 | P1 | 按材质schema/ShaderMetadata验证参数packing并创建binding，提交更新与替换；不把descriptor set回写进可序列化材质资产。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/PipelineLibrary.h` | 规划文件 | P1 | 声明shader variants、材质域、vertex layout与render-target格式组成的高层pipeline键。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Resources/PipelineLibrary.cpp` | 规划文件 | P1 | 选择已烘焙shader组合并请求/预热RHI pipeline，按内容版本失效缓存；原生VkPipelineCache仍由Vulkan后端独立管理。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Uploads/UploadScheduler.h` | 规划文件 | P0 | 声明buffer/image上传请求、CPU源数据保留、staging区间和上传ticket；ticket不等同已完成或可直接GPU等待。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Uploads/UploadScheduler.cpp` | 规划文件 | P0 | 构造graph可见copy批次并管理staging内存，成功提交后发布GpuDependency/CompletionPoint；消费端声明依赖，完成后才复用源与staging。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Readback/ReadbackManager.h` | 规划文件 | P2 | 声明异步readback请求、ticket、格式/row pitch和CPU结果生命周期，避免UI线程直接阻塞GPU。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Readback/ReadbackManager.cpp` | 规划文件 | P2 | 生成graph readback copy、等待对应完成点后invalidate/map并交付CPU数据；取消/失败也正确释放保留引用。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Visibility/VisibilitySystem.h` | 规划文件 | P1 | 声明基于RenderScene数据的可见性、bounds、draw list和排序接口，不遍历World。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Visibility/VisibilitySystem.cpp` | 规划文件 | P1 | 执行CPU frustum culling与稳定draw分组，校验bounds和matrix约定；P4 GPU culling作为可选pass扩展而非改变基础语义。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Lighting/LightCulling.h` | 规划文件 | P3 | 声明cluster/tile灯光分配数据、空间划分与buffer预算，显式表达溢出策略。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Lighting/LightCulling.cpp` | 规划文件 | P3 | 从渲染灯光数据生成GPU灯光列表及culling pass输入，验证view-space约定和上限，不能静默丢弃超限灯光。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DepthPrepass.h` | 规划文件 | P1 | 声明深度预通道资源访问、camera与可见draw输入，效果可按路径禁用。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DepthPrepass.cpp` | 规划文件 | P1 | 通过RHI录制depth-only draw并声明depth写入/后续读取用途，保持alpha test与主材质可见性一致。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/ForwardPass.h` | 规划文件 | P1 | 声明静态3D opaque/masked主渲染pass的attachments、光照和材质输入。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/ForwardPass.cpp` | 规划文件 | P1 | 将可见draw与pipeline/material binding录制到RHI，显式声明颜色/深度及采样资源访问，不直接加载资产或访问ECS。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/ShadowPass.h` | 规划文件 | P1 | 声明阴影views、atlas/depth资源及caster列表输入，区分阴影数据和业务light组件。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/ShadowPass.cpp` | 规划文件 | P1 | 录制阴影depth passes并输出shadow sampling依赖，验证投影、bias和范围，atlas重建经RHI延迟释放。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/TonemapPass.h` | 规划文件 | P1 | 声明HDR输入、曝光与显示输出颜色空间契约，避免把sRGB编码重复应用。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/TonemapPass.cpp` | 规划文件 | P1 | 录制tone mapping及必要的输出变换，读取显式曝光参数并写指定输出格式；不隐式修改原始纹理色值。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/PresentPass.h` | 规划文件 | P0 | 声明最终颜色到swapchain输出的copy/合成计划和呈现前访问要求。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/PresentPass.cpp` | 规划文件 | P0 | 录制最终输出及需要的呈现状态转换，把acquire/present GPU依赖交计划与RHI；不在pass内部私自vkQueuePresent。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/SkyboxPass.h` | 规划文件 | P2 | 声明环境贴图/skybox绘制资源、相机旋转与深度策略。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/SkyboxPass.cpp` | 规划文件 | P2 | 通过RHI绘制背景并维持HDR/颜色空间和depth约定；环境预处理由资产/工具或独立明确pass完成。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/TransparentPass.h` | 规划文件 | P2 | 声明透明队列、混合模式和depth读取契约，避免把opaque排序规则套用于透明对象。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/TransparentPass.cpp` | 规划文件 | P2 | 按选定透明策略排序/分组并录制RHI draw，明确blend与depth-write行为；P4 OIT须新增独立方案。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/PickingPass.h` | 规划文件 | P2 | 声明GPU拾取ID输出和RenderScene身份映射，不将ECS指针编码到GPU。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/PickingPass.cpp` | 规划文件 | P2 | 录制对象ID pass并提交readback请求，返回身份与版本供调用方重新校验；不直接选择Editor实体。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DebugDrawPass.h` | 规划文件 | P2 | 声明调试线/形状等渲染数据与暂存容量，不把日志或Editor数据结构绑定进renderer。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DebugDrawPass.cpp` | 规划文件 | P2 | 分配本帧debug geometry并录制RHI draw，溢出可诊断，数据完成后再复用。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/UI/ImGuiRenderer.h` | 规划文件 | P2 | 声明由外部注入ImGuiBackend的UI渲染协调器和纹理注册生命周期，不包含原生Vulkan。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/UI/ImGuiRenderer.cpp` | 规划文件 | P2 | 把ImGui draw data作为graph pass协调输入/输出与纹理引用，将具体录制委托后端适配器；不操作Editor窗口或ECS。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/SkinningPass.h` | 规划文件 | P3 | 声明mesh bind数据、骨骼pose palette与可见实例输入，定义GPU蒙皮结果和前后帧数据契约。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/SkinningPass.cpp` | 规划文件 | P3 | 从RenderFramePacket中的pose数据安排palette上传并录制compute或选定vertex蒙皮路径，显式声明输出顶点/法线及消费barrier；不读取AnimationSystem或ECS。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/LightCullingPass.h` | 规划文件 | P3 | 声明tile/cluster灯光剔除compute pass的深度、灯光buffer、分区与输出计数资源访问。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/LightCullingPass.cpp` | 规划文件 | P3 | 按LightCulling准备的数据录制compute并发布forward采样/存储读取依赖，报告列表溢出，不在pass内暗改批次顺序。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DepthPyramidPass.h` | 规划文件 | P3 | 声明depth到分层深度纹理的生成契约，明确reversed-Z与min/max归约选择及mip访问范围。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/DepthPyramidPass.cpp` | 规划文件 | P3 | 逐mip录制深度归约并声明读写依赖，供可选遮挡剔除使用；不能在深度约定不同的情况下复用错误历史结果。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/GpuCullingPass.h` | 规划文件 | P4 | 声明可选GPU实例/meshlet剔除与indirect draw输出，列明所需RHI能力和CPU可见性后备路径。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/Passes/GpuCullingPass.cpp` | 规划文件 | P4 | 按设备能力录制GPU剔除，声明depth pyramid、instance和indirect buffer访问；不可用时显式选择CPU路径，不改变基础可见性语义。 | 仅依赖RHI、Core/Math、Shaders及领域资产值类型；输入RenderFramePacket，不依赖World、ECS registry、Editor或Vk类型。 |
| `Catalpa/src/Renderer/DebugDraw.h` | 规划文件 | P2 | 声明线段、包围盒的有界线程交接数据。 | Core/Math，不保存World指针。 |
| `Catalpa/src/Renderer/DebugDraw.cpp` | 规划文件 | P2 | 合并调试请求到帧包，消费者释放后再复用。 | Core/Math，不保存World指针。 |
| `Catalpa/src/Renderer/Passes/GameUIPass.h` | 规划文件 | P3 | 声明UIDrawData的transform/scissor/预乘alpha批次的资源访问。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |
| `Catalpa/src/Renderer/Passes/GameUIPass.cpp` | 规划文件 | P3 | 经RHI录制UIDrawData的transform/scissor/预乘alpha批次，依赖进入RenderGraph；不在pass做阻塞加载。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |
| `Catalpa/src/Renderer/Passes/ParticlePass.h` | 规划文件 | P4 | 声明粒子实例/材质/深度与透明颜色的资源访问。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |
| `Catalpa/src/Renderer/Passes/ParticlePass.cpp` | 规划文件 | P4 | 经RHI录制粒子实例/材质/深度与透明颜色，依赖进入RenderGraph；不在pass做阻塞加载。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |
| `Catalpa/src/Renderer/Passes/TerrainPass.h` | 规划文件 | P4 | 声明就绪terrain tile/材质层/深度颜色的资源访问。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |
| `Catalpa/src/Renderer/Passes/TerrainPass.cpp` | 规划文件 | P4 | 经RHI录制就绪terrain tile/材质层/深度颜色，依赖进入RenderGraph；不在pass做阻塞加载。 | Renderer/RHI及稳定值快照，无ECS或第三方UI状态。 |

<a id="module-28"></a>

## 28. Platform

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/src/Platform/Vulkan/Vulkan.h` | 现有空文件 | P0 | 现有空占位头；规划保留为后端内部兼容入口并汇总最小声明，不代表已实现Vulkan渲染能力，不安装为上游公共SDK。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanHeaders.h` | 规划文件 | P0 | 统一GLAD生成Vulkan头、平台宏与VMA所需包含顺序，确保一种加载方案；不在此重复定义loader实现。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBackend.h` | 规划文件 | P0 | 声明RHI工厂对应的Vulkan创建入口和失败清理边界。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBackend.cpp` | 规划文件 | P0 | 组装instance、adapter、device和可选surface服务；失败时按所有权逆序回滚，将完成设备以RHI接口交付。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanLoader.h` | 规划文件 | P0 | 声明Vulkan运行库/instance/device函数加载阶段与加载结果；隔离现有GLAD全局分派策略。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanLoader.cpp` | 规划文件 | P0 | 执行GLAD loader初始化和分阶段入口加载，检查所需core/extension函数；记录生成配置，避免混用volk或重复函数定义。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanInstance.h` | 规划文件 | P0 | 声明VkInstance及debug messenger的唯一所有者和所启用instance扩展列表。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanInstance.cpp` | 规划文件 | P0 | 创建instance、验证layer/extension、建立调试回调并清理实例级对象；不拥有业务窗口或设备资源。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPhysicalDevice.h` | 规划文件 | P0 | 声明adapter候选、队列族拓扑、设备扩展与物理能力查询接口，不拥有VkDevice。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPhysicalDevice.cpp` | 规划文件 | P0 | 枚举候选并按required能力筛选，查询formats/limits/features，生成逻辑设备创建计划和能力报告。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDevice.h` | 规划文件 | P0 | 声明RHIDevice实现及资源registry、submission tracker、retirement、allocator的生命周期顺序。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDevice.cpp` | 规划文件 | P0 | 创建逻辑设备与子系统，验证资源描述和device epoch，处理device-lost与关停；所有延迟释放请求集中进入VulkanResourceRetirement。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSurface.h` | 规划文件 | P0 | 声明surface所有者及对Core不透明窗口令牌的适配，不向RHI外传VkSurfaceKHR。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSurface.cpp` | 规划文件 | P0 | 借助平台窗口适配创建surface、查询呈现支持，处理surface失效与重建；不创建或销毁业务窗口。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSwapchain.h` | 规划文件 | P0 | 声明RHISwapchain实现，区分外部借用swapchain images与引擎自有images及逐图像呈现同步对象。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSwapchain.cpp` | 规划文件 | P0 | 处理acquire SUCCESS/SUBOPTIMAL的已获取协议与TIMEOUT/NOT_READY无图像分支；带oldSwapchain创建即使失败也使旧代退役，不再acquire旧代。按GPU/present证据退出旧代；无呈现完成扩展时保留保守退出的规范限制，不声称WaitIdle完整证明。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanQueue.h` | 规划文件 | P0 | 声明RHIQueue实现、queue family/queue映射与提交事务边界。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanQueue.cpp` | 规划文件 | P0 | submit前验证并预留跟踪记录/引用，串行化VkQueue访问；成功后以不再分配失败的commit更新权威状态。规范保证无副作用的失败可回滚预留，device lost使epoch失效并停止复用，不能伪装成功。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanCommandPool.h` | 规划文件 | P0 | 声明按队列族、录制线程和复用代际划分的command pool所有者，不允许线程无约束共享。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanCommandPool.cpp` | 规划文件 | P0 | 分配/回收command buffers，确认关联完成集合后reset池；退出时把需延迟清理的原生池交retirement。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanCommandList.h` | 规划文件 | P0 | 声明RHICommandList实现、录制状态、局部访问记录及录制期间资源保留集合。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanCommandList.cpp` | 规划文件 | P0 | 翻译绘制/拷贝/rendering命令，消费VulkanBarrierCompiler校验编码的结果录制vkCmdPipelineBarrier2，并保存本地终态/引用；不重新推导graph依赖或提前修改全局已提交状态。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanFrameContext.h` | 规划文件 | P0 | 声明一轮可复用后端临时资源的容器，保存command pool、descriptor暂存分配和复用所需CompletionSet。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanFrameContext.cpp` | 规划文件 | P0 | 按实际提交完成条件等待/复用后端帧资源，避免把frame slot与swapchain image index等同，不直接销毁registry内资源。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanMemoryAllocator.h` | 规划文件 | P0 | 声明VMA封装、allocation记录、预算统计、映射与non-coherent范围处理接口。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanMemoryAllocator.cpp` | 规划文件 | P0 | 创建allocator与buffer/image分配，按对齐/atom size执行flush/invalidate；仅在retirement确认安全后释放allocation，不自行按帧计数释放。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBuffer.h` | 规划文件 | P0 | 定义buffer原生记录、VMA allocation、容量、用途、映射信息及借用/拥有标记；真正拥有权由registry管理。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBuffer.cpp` | 规划文件 | P0 | 创建和映射buffer，验证offset/size及映射可见性；释放仅形成retirement载荷，原生buffer与allocation成对清理。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanTexture.h` | 规划文件 | P0 | 定义image及image view记录、mip/layer/aspect范围和外部图像所有权，不在记录中单独维护另一份全局layout。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanTexture.cpp` | 规划文件 | P0 | 创建images/views并检查view兼容性，注册外部swapchain图像为借用资源；retirement保证view先于其拥有image/allocation清理。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSampler.h` | 规划文件 | P1 | 定义sampler原生记录及标准化描述键，区别句柄引用与缓存命中。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSampler.cpp` | 规划文件 | P1 | 创建并校验过滤/各向异性/比较采样能力；缓存返回registry句柄，释放交retirement，不绕过last-use。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanShader.h` | 规划文件 | P0 | 定义已烘焙SPIR-V的shader module记录及entry/stage关联，不包含glslang等源码编译器。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanShader.cpp` | 规划文件 | P0 | 验证输入字节长度/接口约定，创建shader modules并供pipeline创建使用；销毁由registry/retirement统一处理。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPipeline.h` | 规划文件 | P0 | 定义graphics/compute pipeline原生记录与layout、attachment兼容信息。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPipeline.cpp` | 规划文件 | P0 | 把已验证RHI pipeline描述翻译为Vulkan创建参数，正确处理dynamic rendering格式兼容；替换后旧pipeline延迟退休。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPipelineCache.h` | 规划文件 | P1 | 声明原生VkPipelineCache与后端pipeline描述缓存索引，和Renderer的材质/variant选择明确区分。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanPipelineCache.cpp` | 规划文件 | P1 | 复用已创建pipeline句柄并读写匹配设备/driver的cache blob，处理失效或损坏缓存；pipeline本体仍归registry和retirement。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDescriptorAllocator.h` | 规划文件 | P0 | 声明descriptor pools、寿命分区与分配失败策略；区分帧临时集合和长期材质集合。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDescriptorAllocator.cpp` | 规划文件 | P0 | 按布局需求分配/回收descriptor空间，关联其GPU last-use，扩容保持错误可见；pool reset/销毁须经完成条件验证。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBinding.h` | 规划文件 | P0 | 定义descriptor set layout、pipeline layout和binding set记录，以及资源引用和layout契约。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBinding.cpp` | 规划文件 | P0 | 创建布局、更新descriptor并校验类型/数组/动态偏移；引用资源保留到相应提交完成，layout/set/pool退出统一走retirement。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceRegistry.h` | 规划文件 | P0 | 声明各强类型资源池、generation/device校验、拥有记录与录制/提交引用计数；为GPU对象唯一登记入口。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceRegistry.cpp` | 规划文件 | P0 | 实现创建登记、retain/release、句柄失效和last-use合并；最后CPU引用释放后把原生对象载荷移交唯一retirement而非立即销毁。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanConversions.h` | 规划文件 | P0 | 声明RHI格式、usage、stage/access及pipeline值到Vulkan的纯显式映射；仅转换已定义值，不推导hazard或创建另一套同步策略。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanConversions.cpp` | 规划文件 | P0 | 映射枚举/flags并拒绝不支持组合，不依赖数值相同或静默降级；barrier的范围、release/acquire与同步作用域编码由VulkanBarrierCompiler负责。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceStateTracker.h` | 规划文件 | P0 | 声明已接受提交的每个buffer范围/image子资源状态、queue ownership和版本，保留last-writer与并行readers frontier及其GPU依赖；区分ownership release待acquire状态，不拥有graph计划状态。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceStateTracker.cpp` | 规划文件 | P0 | 以已接受的访问frontier校验plan前提，成功submit后commit范围状态/writer/readers及pending transfer增量；后续写须依赖全部相关并行读者，录制/取消/无副作用的失败提交不污染权威状态。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSubmissionTracker.h` | 规划文件 | P0 | 声明每queue已提交/已完成序列、device epoch和完成观察记录；retirement完成点必须对应覆盖整批工作的signal，不能复用任意细粒度stage依赖值。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanSubmissionTracker.cpp` | 规划文件 | P0 | 成功接受后登记序列与资源保留，配置timeline时使用覆盖整批的ALL_COMMANDS signal生成retirement完成点，或使用整批fence；推进已完成值并释放提交保留，不直接销毁GPU资源。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanFence.h` | 规划文件 | P0 | 声明VkFence包装、未提交/已提交/完成状态与CPU等待契约，供fence完成追踪配置或必要CPU等待使用，禁止未安全完成时reset。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanFence.cpp` | 规划文件 | P0 | 创建/等待/查询并池化fence；供submission tracker确认完成，退出原生对象通过retirement，不向上层泄漏VkFence。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBinarySemaphore.h` | 规划文件 | P0 | 声明binary semaphore所有者及一次signal/wait消费状态，覆盖acquire、submit和present不同使用域。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanBinarySemaphore.cpp` | 规划文件 | P0 | 跟踪acquire/submit/present的binary signal与wait消费及安全复用；present OUT_OF_DATE/SURFACE_LOST的wait仍被视为已入队，区别于保证无副作用的内存分配失败；退出交retirement。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanTimelineSemaphore.h` | 规划文件 | P0 | 声明能力协商后可选启用的timeline包装、signal/query/wait契约；与CPU完成点和GPU等待依赖保持语义区分，不要求同时使用fence完成路径。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanTimelineSemaphore.cpp` | 规划文件 | P0 | 实现timeline创建/查询/CPU等待和signal编码；单调值由SubmissionTracker协调，细粒度GPU依赖与ALL_COMMANDS整批完成标记分别登记，禁止host人工signal伪造GPU完成。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceRetirement.h` | 规划文件 | P0 | 声明唯一延迟回收入口，载荷包含原生对象、allocation、依赖顺序以及GPU/present完成条件。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanResourceRetirement.cpp` | 规划文件 | P0 | 验证全部退休条件后按view/image/allocation等依赖顺序真正销毁，统一处理旧swapchain代际和关停；其他模块不能再建竞争deleter。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDebugUtils.h` | 规划文件 | P0 | 声明validation消息接收、对象命名、debug label和后端诊断接口，不控制资源所有权。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanDebugUtils.cpp` | 规划文件 | P0 | 安装/转发validation回调，标注command与对象，保留message severity和原始诊断，避免将错误静默过滤为成功。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanQueryPool.h` | 规划文件 | P2 | 声明timestamp/query pool记录、范围分配和结果完成条件，提供RHI query能力实现。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/VulkanQueryPool.cpp` | 规划文件 | P2 | 创建及录制query命令，按完成点读取结果并转换明确时间单位；reset和回收不得覆盖GPU未完成query。 | VulkanBackend私有依赖RHI、Core、统一VulkanHeaders、现有GLAD及VMA；不得依赖World、Editor或Renderer业务。 |
| `Catalpa/src/Platform/Vulkan/Integrations/VulkanImGuiBackend.h` | 规划文件 | P2 | 声明实现Renderer/UI/ImGuiBackend的Vulkan适配器，私有包含imgui_impl_vulkan及Vulkan类型。 | 作为独立integration构建目标依赖Renderer.UI接口与VulkanBackend私有设施；VulkanBackend核心不反向依赖Renderer，Editor不包含此头。 |
| `Catalpa/src/Platform/Vulkan/Integrations/VulkanImGuiBackend.cpp` | 规划文件 | P2 | 初始化/录制官方ImGui Vulkan backend并适配纹理令牌、字体上传和resize；纹理退出及整体shutdown动作在全部相关提交完成后由唯一retirement触发，不提前释放库自管GPU对象。 | 作为独立integration构建目标依赖Renderer.UI接口与VulkanBackend私有设施；VulkanBackend核心不反向依赖Renderer，Editor不包含此头。 |
| `Catalpa/src/Platform/Windows/WindowsPlatform.h` | 规划文件 | P0 | 声明Windows OS服务实现与native窗口令牌适配，限定为平台能力，不夹带Renderer或Audio业务。 | 依赖Core平台服务契约及Win32；不依赖RHI、Renderer、World或Editor。 |
| `Catalpa/src/Platform/Windows/WindowsPlatform.cpp` | 规划文件 | P0 | 实现路径/时钟/动态库等Core平台服务及需要的Win32句柄转换；所有Windows类型只在私有实现或明确平台头出现。 | 依赖Core平台服务契约及Win32；不依赖RHI、Renderer、World或Editor。 |
| `Catalpa/src/Platform/WindowsWindow/WindowsWindow.h` | 现有文件 | P0 | 现有窗口实现声明；规划继续实现Core Window接口，唯一持有GLFW窗口并提供不透明窗口令牌，不创建Vulkan设备。 | 依赖Core Window/Input/Events及GLFW；不向公共头传播GLFWwindow或Vk类型。 |
| `Catalpa/src/Platform/WindowsWindow/WindowsWindow.cpp` | 现有文件 | P0 | 在现有实现基础上规范GLFW初始化、事件泵、DPI/resize/minimize及关停；窗口创建hint须在GLFW初始化后正确设置，surface创建留后端适配。 | 依赖Core Window/Input/Events及GLFW；不向公共头传播GLFWwindow或Vk类型。 |
| `Catalpa/src/Platform/WindowsWindow/WindowsInput.h` | 现有文件 | P0 | 现有输入适配声明；规划以引擎KeyCode/MouseCode和窗口身份查询输入，不依赖全局renderer。 | 依赖Core Input/KeyCodes/MouseCodes及私有GLFW窗口服务，不依赖RHI。 |
| `Catalpa/src/Platform/WindowsWindow/WindowsInput.cpp` | 现有文件 | P0 | 在现有实现基础上实现窗口输入状态与鼠标/键盘转换，约束主线程查询和窗口失效；不直接处理ECS游戏行为。 | 依赖Core Input/KeyCodes/MouseCodes及私有GLFW窗口服务，不依赖RHI。 |
| `Catalpa/src/Platform/OpenGL/OpenGL.h` | 现有空文件 | P4 | 现有空占位头；仅标记未来可选OpenGL backend入口，当前不代表已实现、不纳入Vulkan首后端必需依赖。 | 将来若启用仅实现RHI并私有依赖OpenGL loader；不能让上游同时包含OpenGL/Vulkan原生头。 |
| `Catalpa/src/Platform/Vulkan/VulkanBarrierCompiler.h` | 规划文件 | P0 | 声明将已编译plan的访问、范围、layout与ownership转换编码为VkDependencyInfo及barrier数组的私有接口；不拥有全局状态或重排依赖。 | 仅接收RHI barrier描述和经ResourceStateTracker校验的状态基线，依赖VulkanConversions及私有Vulkan类型；不include Renderer的RenderGraphPlan，不依赖World或Editor。 |
| `Catalpa/src/Platform/Vulkan/VulkanBarrierCompiler.cpp` | 规划文件 | P0 | 编码并验证plan指定的stage/access、image子资源、buffer范围及配对release/acquire，检查queue支持和layout合法性；输出由CommandList录制，不猜测未声明读写或自行安排提交。 | 仅接收RHI barrier描述和经ResourceStateTracker校验的状态基线，依赖VulkanConversions及私有Vulkan类型；不include Renderer的RenderGraphPlan，不依赖World或Editor。 |
| `Catalpa/src/Platform/Windows/WindowsFileWatcher.h` | 规划文件 | P2 | 声明Windows目录通知、取消与overflow状态。 | Core/IO与Win32。 |
| `Catalpa/src/Platform/Windows/WindowsFileWatcher.cpp` | 规划文件 | P2 | 适配系统通知并处理重命名/丢失/退出，无关闭后回调。 | Core/IO与Win32。 |

<a id="module-29"></a>

## 29. CatalpaEditor

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `CatalpaEditor/src/main.cpp` | 规划文件 | P2 | 创建EditorApplication并统一处理启动错误与退出码。 | 依赖EditorApplication和引擎Application；Runtime不反向依赖Editor。 |
| `CatalpaEditor/src/Application/EditorApplication.h` | 规划文件 | P2 | 声明编辑器服务、主循环、面板和文档的所有权。 | 依赖Runtime公开服务及Tools；不把Editor链接进Player。 |
| `CatalpaEditor/src/Application/EditorApplication.cpp` | 规划文件 | P2 | 组装引擎、ImGui与AssetPipeline，按确认协议关闭文档并停止运行时。 | 依赖Runtime公开服务及Tools；不把Editor链接进Player。 |
| `CatalpaEditor/src/Application/EditorProject.h` | 规划文件 | P2 | 声明项目路径、内容根、目标平台及构建配置。 | 依赖Core路径及序列化；不得打开项目时覆盖源素材。 |
| `CatalpaEditor/src/Application/EditorProject.cpp` | 规划文件 | P2 | 读写编辑器项目配置，区分原素材、DDC、Cooked和发布包目录。 | 依赖Core路径及序列化；不得打开项目时覆盖源素材。 |
| `CatalpaEditor/src/Application/EditorSelection.h` | 规划文件 | P2 | 声明文档作用域内的实体与资产选择状态。 | 依赖Document ID和引擎句柄，不保存全局World裸指针。 |
| `CatalpaEditor/src/Application/EditorSelection.cpp` | 规划文件 | P2 | 校验句柄并处理文档切换、删除和异步关闭后的失效。 | 依赖Document ID和引擎句柄，不保存全局World裸指针。 |
| `CatalpaEditor/src/Documents/Document.h` | 规划文件 | P2 | 定义文档ID、脏标记、保存及异步关闭确认状态。 | Editor专有；Closing不等于已销毁，Runtime资产不依赖文档。 |
| `CatalpaEditor/src/Documents/DocumentManager.h` | 规划文件 | P2 | 声明文档打开、激活、保存和分阶段关闭协调。 | 依赖Document与AssetEditorRegistry；不绕过SceneManager或AudioService所有权。 |
| `CatalpaEditor/src/Documents/DocumentManager.cpp` | 规划文件 | P2 | 管理未保存状态与Closing状态，等待预览stop及资源释放确认后移除文档。 | 依赖Document与AssetEditorRegistry；不绕过SceneManager或AudioService所有权。 |
| `CatalpaEditor/src/Documents/SceneDocument.h` | 规划文件 | P2 | 声明编辑SceneHandle、独立Play SceneHandle与文档变更事务。 | 持SceneHandle而非拥有World裸指针；不自动把Play状态写回作者场景。 |
| `CatalpaEditor/src/Documents/SceneDocument.cpp` | 规划文件 | P2 | 通过SceneManager创建加载场景；stop/close请求先停预览，收到停止及提取引用退出确认后才释放句柄。 | 持SceneHandle而非拥有World裸指针；不自动把Play状态写回作者场景。 |
| `CatalpaEditor/src/Documents/PrefabDocument.h` | 规划文件 | P2 | 声明Prefab编辑根、覆盖项和文档依赖。 | 依赖Runtime Prefab格式和EditorCommand，不固化runtime entity ID。 |
| `CatalpaEditor/src/Documents/PrefabDocument.cpp` | 规划文件 | P2 | 保存版本化Prefab描述，校验嵌套与覆盖；预览关闭须等待场景停止确认。 | 依赖Runtime Prefab格式和EditorCommand，不固化runtime entity ID。 |
| `CatalpaEditor/src/Commands/EditorCommand.h` | 规划文件 | P2 | 定义可撤销命令、失败结果和合并策略。 | 仅Editor；执行前校验Document ID、SceneHandle和目标句柄。 |
| `CatalpaEditor/src/Commands/CommandHistory.h` | 规划文件 | P2 | 声明按文档隔离的undo/redo栈和编辑事务。 | 依赖EditorCommand和Document；不是模拟回滚系统。 |
| `CatalpaEditor/src/Commands/CommandHistory.cpp` | 规划文件 | P2 | 执行、撤销并合并命令，失败时保持历史与文档一致。 | 依赖EditorCommand和Document；不是模拟回滚系统。 |
| `CatalpaEditor/src/Commands/SceneCommands.h` | 规划文件 | P2 | 声明创建删除实体、属性更新和重父子关系命令。 | 依赖SceneDocument及ECS命令接口，不绕过层级循环和句柄校验。 |
| `CatalpaEditor/src/Commands/SceneCommands.cpp` | 规划文件 | P2 | 记录稳定标识与前值，在安全阶段执行并支持撤销。 | 依赖SceneDocument及ECS命令接口，不绕过层级循环和句柄校验。 |
| `CatalpaEditor/src/Panels/HierarchyPanel.h` | 规划文件 | P2 | 声明场景层级浏览、选择及重父子交互。 | 依赖SceneDocument、EditorSelection和ImGui，不直接变更World结构。 |
| `CatalpaEditor/src/Panels/HierarchyPanel.cpp` | 规划文件 | P2 | 经SceneCommands提交修改并显示循环或失效错误。 | 依赖SceneDocument、EditorSelection和ImGui，不直接变更World结构。 |
| `CatalpaEditor/src/Panels/InspectorPanel.h` | 规划文件 | P2 | 声明实体与组件属性显示和编辑入口。 | 依赖SceneDocument、CommandHistory及反射服务；不编辑backend原生状态。 |
| `CatalpaEditor/src/Panels/InspectorPanel.cpp` | 规划文件 | P2 | 依据类型元数据生成控件，经undo事务提交有效修改。 | 依赖SceneDocument、CommandHistory及反射服务；不编辑backend原生状态。 |
| `CatalpaEditor/src/Panels/ContentBrowserPanel.h` | 规划文件 | P2 | 声明资产浏览、导入设置及构建状态展示。 | 依赖资产注册及Tools；保留原素材只读来源。 |
| `CatalpaEditor/src/Panels/ContentBrowserPanel.cpp` | 规划文件 | P2 | 发起AssetPipeline任务并显示依赖和失败，UI线程不执行完整Cook。 | 依赖资产注册及Tools；保留原素材只读来源。 |
| `CatalpaEditor/src/Panels/ConsolePanel.h` | 规划文件 | P2 | 声明日志过滤、错误定位及构建诊断视图。 | 依赖Core日志订阅和ImGui，不重新定义Runtime日志策略。 |
| `CatalpaEditor/src/Panels/ConsolePanel.cpp` | 规划文件 | P2 | 消费有界日志快照并关联资产与源位置，不阻塞日志生产线程。 | 依赖Core日志订阅和ImGui，不重新定义Runtime日志策略。 |
| `CatalpaEditor/src/Panels/SceneViewportPanel.h` | 规划文件 | P2 | 声明离屏视口、分辨率和ImGui纹理桥接。 | 依赖Renderer公开RenderSurface和Editor交互服务，不直接拥有VkImage。 |
| `CatalpaEditor/src/Panels/SceneViewportPanel.cpp` | 规划文件 | P2 | 消费RenderSurface并协调相机、拾取及gizmo；关闭后等待在途预览退出。 | 依赖Renderer公开RenderSurface和Editor交互服务，不直接拥有VkImage。 |
| `CatalpaEditor/src/Panels/ProjectSettingsPanel.h` | 规划文件 | P2 | 声明项目、平台及资产构建设置面板。 | 依赖EditorProject和工具配置；Runtime仅消费运行配置。 |
| `CatalpaEditor/src/Panels/ProjectSettingsPanel.cpp` | 规划文件 | P2 | 校验配置并事务保存，不静默修改素材或升级资产格式。 | 依赖EditorProject和工具配置；Runtime仅消费运行配置。 |
| `CatalpaEditor/src/Viewport/EditorCameraController.h` | 规划文件 | P2 | 声明编辑相机导航、焦点及输入占用策略。 | 依赖平台输入快照和Camera数据，不成为Runtime游戏相机系统。 |
| `CatalpaEditor/src/Viewport/EditorCameraController.cpp` | 规划文件 | P2 | 转换视口输入为编辑相机参数，隔离Play预览输入。 | 依赖平台输入快照和Camera数据，不成为Runtime游戏相机系统。 |
| `CatalpaEditor/src/Viewport/PickingController.h` | 规划文件 | P2 | 声明异步拾取、SceneHandle及视口版本校验。 | 依赖Renderer公开异步结果和EditorSelection，不无条件阻塞等待GPU。 |
| `CatalpaEditor/src/Viewport/PickingController.cpp` | 规划文件 | P2 | 消费延迟readback并拒绝过期场景、视口或实体结果。 | 依赖Renderer公开异步结果和EditorSelection，不无条件阻塞等待GPU。 |
| `CatalpaEditor/src/Viewport/TransformGizmo.h` | 规划文件 | P2 | 声明gizmo坐标模式、吸附和拖拽事务。 | 依赖SceneDocument和CommandHistory，矩阵遵守Math契约。 |
| `CatalpaEditor/src/Viewport/TransformGizmo.cpp` | 规划文件 | P2 | 通过ImGuizmo生成SceneCommands并合并连续拖拽undo。 | 依赖SceneDocument和CommandHistory，矩阵遵守Math契约。 |
| `CatalpaEditor/src/AssetEditors/IAssetEditor.h` | 规划文件 | P2 | 定义资产编辑器打开、绘制、保存及关闭确认接口。 | Editor专有；不给Runtime资产对象添加UI虚函数。 |
| `CatalpaEditor/src/AssetEditors/AssetEditorRegistry.h` | 规划文件 | P2 | 声明资产类型到编辑器工厂的注册。 | 依赖IAssetEditor与DocumentManager，不混入Runtime AssetManager。 |
| `CatalpaEditor/src/AssetEditors/AssetEditorRegistry.cpp` | 规划文件 | P2 | 创建资产文档并管理关联，拒绝重复注册和未知类型。 | 依赖IAssetEditor与DocumentManager，不混入Runtime AssetManager。 |
| `CatalpaEditor/src/AssetEditors/MaterialEditor.h` | 规划文件 | P2 | 声明材质参数、shader兼容诊断及预览文档。 | 依赖MaterialDefinition/MaterialInstance、Shader元数据和Renderer公开预览。 |
| `CatalpaEditor/src/AssetEditors/MaterialEditor.cpp` | 规划文件 | P2 | 验证参数与纹理，经工具保存描述并异步更新预览；关闭等待预览退出。 | 依赖MaterialDefinition/MaterialInstance、Shader元数据和Renderer公开预览。 |
| `CatalpaEditor/src/AssetEditors/AnimationPreviewEditor.h` | 规划文件 | P3 | 声明骨架、clip与基础混合预览。 | 依赖Animation资产和Renderer预览；不持有ozz实例，不承诺完整动画蓝图。 |
| `CatalpaEditor/src/AssetEditors/AnimationPreviewEditor.cpp` | 规划文件 | P3 | 通过动画服务检查兼容性并播放；关闭时等待预览Scene停止确认。 | 依赖Animation资产和Renderer预览；不持有ozz实例，不承诺完整动画蓝图。 |
| `CatalpaEditor/src/AssetEditors/AudioPreviewEditor.h` | 规划文件 | P3 | 声明音频元数据、导入设置与试听控制。 | 依赖Audio公开服务和AssetPipeline，UI线程不持有ma_sound或解码状态。 |
| `CatalpaEditor/src/AssetEditors/AudioPreviewEditor.cpp` | 规划文件 | P3 | 通过AudioService创建预览voice；close先发stop，收到确认后再释放clip及文档引用。 | 依赖Audio公开服务和AssetPipeline，UI线程不持有ma_sound或解码状态。 |
| `CatalpaEditor/src/AssetEditors/TexturePreviewEditor.h` | 规划文件 | P2 | 声明纹理通道、mip和色彩空间预览参数。 | 依赖Images/ImageAsset和Renderer/Resources；源图保持只读，不直接调用Vulkan。 |
| `CatalpaEditor/src/AssetEditors/TexturePreviewEditor.cpp` | 规划文件 | P2 | 显示烘焙结果与导入诊断，通过Renderer资源句柄显示。 | 依赖Images/ImageAsset和Renderer/Resources；源图保持只读，不直接调用Vulkan。 |

<a id="module-30"></a>

## 30. Tools/AssetPipeline

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tools/AssetPipeline/AssetBuildTypes.h` | 规划文件 | P1 | 定义目标平台、源哈希、构建键、依赖、产物和诊断数据。 | 只依赖Core及Runtime资产格式，工具版本与schema进入构建键。 |
| `Tools/AssetPipeline/IAssetImporter.h` | 规划文件 | P1 | 定义只读源导入、依赖发现及规范化中间产物契约。 | 源素材只读，输出仅写独立Derived/Cooked目录。 |
| `Tools/AssetPipeline/AssetImportRegistry.h` | 规划文件 | P1 | 声明格式和资产类型到导入器的注册表。 | 依赖IAssetImporter，外部格式库不传播到Runtime。 |
| `Tools/AssetPipeline/AssetImportRegistry.cpp` | 规划文件 | P1 | 校验注册唯一性并明确选取规则，禁止静默降级解析。 | 依赖IAssetImporter，外部格式库不传播到Runtime。 |
| `Tools/AssetPipeline/AssetBuildContext.h` | 规划文件 | P1 | 声明源路径、输出根、目标、取消和诊断服务。 | 依赖Core路径/VFS，不在原素材路径写导入结果。 |
| `Tools/AssetPipeline/AssetBuildContext.cpp` | 规划文件 | P1 | 约束输出路径并以临时文件加原子提交发布完整产物。 | 依赖Core路径/VFS，不在原素材路径写导入结果。 |
| `Tools/AssetPipeline/AssetDependencyScanner.h` | 规划文件 | P1 | 声明资产与shader依赖图及循环诊断。 | 依赖Importer及资产ID，不能仅按修改时间判断一致。 |
| `Tools/AssetPipeline/AssetDependencyScanner.cpp` | 规划文件 | P1 | 发现缺失、循环和变更内容，为增量构建生成稳定输入。 | 依赖Importer及资产ID，不能仅按修改时间判断一致。 |
| `Tools/AssetPipeline/AssetBuildCache.h` | 规划文件 | P1 | 声明按内容、工具版本、配置与目标隔离的派生缓存。 | 依赖AssetBuildContext，区别于Renderer GPU派生缓存。 |
| `Tools/AssetPipeline/AssetBuildCache.cpp` | 规划文件 | P1 | 校验产物完整性，取消或失败产物不进入有效缓存。 | 依赖AssetBuildContext，区别于Renderer GPU派生缓存。 |
| `Tools/AssetPipeline/AssetCooker.h` | 规划文件 | P1 | 声明导入、目标烘焙、依赖排序和发布协调。 | Editor和CLI共享本库；依赖Core/Jobs，不依赖Editor类型。 |
| `Tools/AssetPipeline/AssetCooker.cpp` | 规划文件 | P1 | 执行有界计划，调用类型Cooker并保留错误，只发布完整输出。 | Editor和CLI共享本库；依赖Core/Jobs，不依赖Editor类型。 |
| `Tools/AssetPipeline/AssetArchiveWriter.h` | 规划文件 | P1 | 声明版本化资产头、块表、依赖和校验写出。 | 共享Assets/AssetContainerFormat与Core归档，runtime由AssetContainerReader读取；只写独立派生产物。 |
| `Tools/AssetPipeline/AssetArchiveWriter.cpp` | 规划文件 | P1 | 编码字节并检查长度、对齐与哈希，不直接dump C++结构。 | 共享Assets/AssetContainerFormat与Core归档，runtime由AssetContainerReader读取；只写独立派生产物。 |
| `Tools/AssetPipeline/Importers/GltfImporter.h` | 规划文件 | P1 | 声明glTF的mesh、Model节点、材质及关联骨架动画只读导入。 | 私有依赖fastgltf；ModelCooker编码ModelAsset，各类型Cooker编码自己的资产。 |
| `Tools/AssetPipeline/Importers/GltfImporter.cpp` | 规划文件 | P1 | 产出规范化中间数据，记录坐标与单位转换，共享稳定源节点映射。 | 私有依赖fastgltf；ModelCooker编码ModelAsset，各类型Cooker编码自己的资产。 |
| `Tools/AssetPipeline/Importers/TextureImporter.h` | 规划文件 | P1 | 声明源图片解码与纹理语义设置。 | 私有依赖选定图片解码库，不改源图片。 |
| `Tools/AssetPipeline/Importers/TextureImporter.cpp` | 规划文件 | P1 | 生成派生像素数据并保留原哈希、通道及色彩空间信息。 | 私有依赖选定图片解码库，不改源图片。 |
| `Tools/AssetPipeline/Importers/AudioImporter.h` | 规划文件 | P3 | 声明音频源读取与转码配置。 | 私有依赖解码库，工具链不进入Player。 |
| `Tools/AssetPipeline/Importers/AudioImporter.cpp` | 规划文件 | P3 | 提取通道、采样率、时长和流送需求，仅产生派生数据。 | 私有依赖解码库，工具链不进入Player。 |
| `Tools/AssetPipeline/Importers/SkeletonImporter.h` | 规划文件 | P3 | 声明骨架层级、绑定姿态和稳定joint映射导入。 | 依赖GltfImporter映射，与AnimationImporter共享骨架对应关系。 |
| `Tools/AssetPipeline/Importers/SkeletonImporter.cpp` | 规划文件 | P3 | 从共享模型中间表示提取骨架，校验层级、命名与逆绑定矩阵。 | 依赖GltfImporter映射，与AnimationImporter共享骨架对应关系。 |
| `Tools/AssetPipeline/Importers/AnimationImporter.h` | 规划文件 | P3 | 声明clip、轨道、插值与事件规范化导入。 | 依赖SkeletonImporter映射，不把有损处理当原始数据修正。 |
| `Tools/AssetPipeline/Importers/AnimationImporter.cpp` | 规划文件 | P3 | 转换明确时间与坐标单位，验证目标骨架并记录所有重采样配置。 | 依赖SkeletonImporter映射，不把有损处理当原始数据修正。 |
| `Tools/AssetPipeline/Importers/SceneImporter.h` | 规划文件 | P2 | 声明场景源描述及模型实例的只读导入。 | 依赖Runtime Scene格式和资产注册，不调用Editor面板。 |
| `Tools/AssetPipeline/Importers/SceneImporter.cpp` | 规划文件 | P2 | 解析稳定实体ID、层级、组件schema与引用并报告循环或缺失。 | 依赖Runtime Scene格式和资产注册，不调用Editor面板。 |
| `Tools/AssetPipeline/Importers/PrefabImporter.h` | 规划文件 | P2 | 声明Prefab描述、覆盖项及依赖导入。 | 依赖Runtime Prefab格式，不固化runtime entity ID。 |
| `Tools/AssetPipeline/Importers/PrefabImporter.cpp` | 规划文件 | P2 | 校验嵌套、稳定局部ID和覆盖路径，生成规范中间表示。 | 依赖Runtime Prefab格式，不固化runtime entity ID。 |
| `Tools/AssetPipeline/Importers/MaterialImporter.h` | 规划文件 | P1 | 声明材质描述、shader变体和纹理依赖导入。 | 依赖Materials/MaterialDefinition与MaterialInstance，不创建GPU Pipeline。 |
| `Tools/AssetPipeline/Importers/MaterialImporter.cpp` | 规划文件 | P1 | 依据最终Shader元数据校验参数及绑定并生成规范描述。 | 依赖Materials/MaterialDefinition与MaterialInstance，不创建GPU Pipeline。 |
| `Tools/AssetPipeline/Importers/ShaderImporter.h` | 规划文件 | P1 | 声明源码、include、入口和变体的构建协调。 | 统一使用Catalpa/src/Shaders/ShaderAsset.h和ShaderMetadata。 |
| `Tools/AssetPipeline/Importers/ShaderImporter.cpp` | 规划文件 | P1 | 调用shader_compiler_core收集编译、反射、依赖与诊断，不再造编译器。 | 统一使用Catalpa/src/Shaders/ShaderAsset.h和ShaderMetadata。 |
| `Tools/AssetPipeline/Navigation/RecastNavMeshBaker.h` | 规划文件 | P4 | 声明几何、Agent和tile烘焙配置。 | 私有依赖Recast；Runtime只依赖Detour及NavMeshAsset。 |
| `Tools/AssetPipeline/Navigation/RecastNavMeshBaker.cpp` | 规划文件 | P4 | 从规范场景碰撞几何生成导航tile与版本元数据。 | 私有依赖Recast；Runtime只依赖Detour及NavMeshAsset。 |
| `Tools/AssetPipeline/Importers/ParticleEffectImporter.h` | 规划文件 | P4 | 声明粒子描述、曲线和依赖导入。 | 依赖ParticleEffectAsset，不承诺GPU图编译器。 |
| `Tools/AssetPipeline/Importers/ParticleEffectImporter.cpp` | 规划文件 | P4 | 校验容量、参数和材质，产出最低CPU效果配置。 | 依赖ParticleEffectAsset，不承诺GPU图编译器。 |
| `Tools/AssetPipeline/Importers/TerrainImporter.h` | 规划文件 | P4 | 声明高度源、tile和LOD派生配置。 | 依赖TerrainAsset，原始高度只读，不静默平滑。 |
| `Tools/AssetPipeline/Importers/TerrainImporter.cpp` | 规划文件 | P4 | 生成独立高度tile及边界一致性数据，记录重采样参数。 | 依赖TerrainAsset，原始高度只读，不静默平滑。 |
| `Tools/AssetPipeline/Importers/UIDocumentImporter.h` | 规划文件 | P3 | 声明RML、样式、字体及图片依赖导入。 | 依赖UIDocumentAsset与FontAsset，不在构建时启动Runtime UI设备。 |
| `Tools/AssetPipeline/Importers/UIDocumentImporter.cpp` | 规划文件 | P3 | 解析资源引用与能力要求，诊断缺失及不支持效果，发布UI资产。 | 依赖UIDocumentAsset与FontAsset，不在构建时启动Runtime UI设备。 |
| `Tools/AssetPipeline/Importers/ScriptImporter.h` | 规划文件 | P3 | 声明脚本版本、入口与模块依赖导入。 | 依赖ScriptAsset和锁定Lua工具版本。 |
| `Tools/AssetPipeline/Importers/ScriptImporter.cpp` | 规划文件 | P3 | 校验脚本并按策略保留源或生成目标版本字节码，不执行项目启动逻辑。 | 依赖ScriptAsset和锁定Lua工具版本。 |
| `Tools/AssetPipeline/Cookers/MeshCooker.h` | 规划文件 | P1 | 声明规范mesh到Runtime几何的转换。 | 依赖Geometry/MeshAsset，布局遵守Renderer输入契约。 |
| `Tools/AssetPipeline/Cookers/MeshCooker.cpp` | 规划文件 | P1 | 编码顶点、索引、submesh和包围盒，优化参数与误差可追溯。 | 依赖Geometry/MeshAsset，布局遵守Renderer输入契约。 |
| `Tools/AssetPipeline/Cookers/TextureCooker.h` | 规划文件 | P1 | 声明mip、压缩格式及平台纹理烘焙。 | 依赖Images/ImageAsset及选定压缩库，不覆盖源图。 |
| `Tools/AssetPipeline/Cookers/TextureCooker.cpp` | 规划文件 | P1 | 按明确语义生成mip和目标块，校验尺寸与色彩空间。 | 依赖Images/ImageAsset及选定压缩库，不覆盖源图。 |
| `Tools/AssetPipeline/Cookers/SkeletonCooker.h` | 规划文件 | P3 | 声明骨架中间表示的运行时编码。 | 私有依赖ozz离线库，Runtime资产不暴露工具类型。 |
| `Tools/AssetPipeline/Cookers/SkeletonCooker.cpp` | 规划文件 | P3 | 构建ozz骨架或选定runtime结构，写joint映射和版本。 | 私有依赖ozz离线库，Runtime资产不暴露工具类型。 |
| `Tools/AssetPipeline/Cookers/AnimationCooker.h` | 规划文件 | P3 | 声明clip的采样、压缩及骨架兼容产物。 | 私有依赖ozz离线库，保留处理参数及源映射。 |
| `Tools/AssetPipeline/Cookers/AnimationCooker.cpp` | 规划文件 | P3 | 按显式误差配置编码动画，验证时长、轨道与Skeleton版本。 | 私有依赖ozz离线库，保留处理参数及源映射。 |
| `Tools/AssetPipeline/Cookers/SceneCooker.h` | 规划文件 | P2 | 声明场景中间表示到运行时Scene资产的编码。 | 依赖Runtime Scene格式，不序列化World内存。 |
| `Tools/AssetPipeline/Cookers/SceneCooker.cpp` | 规划文件 | P2 | 编码组件schema、稳定引用和依赖，拒绝Editor对象进入包。 | 依赖Runtime Scene格式，不序列化World内存。 |
| `Tools/AssetPipeline/Cookers/PrefabCooker.h` | 规划文件 | P2 | 声明Prefab与覆盖规则的运行时编码。 | 依赖Runtime Prefab格式，保留作者源与烘焙版本关系。 |
| `Tools/AssetPipeline/Cookers/PrefabCooker.cpp` | 规划文件 | P2 | 编码局部ID、嵌套引用和覆盖并验证实例化依赖顺序。 | 依赖Runtime Prefab格式，保留作者源与烘焙版本关系。 |
| `Tools/AssetPipeline/Cookers/AudioCooker.h` | 规划文件 | P3 | 声明音频转码、流送分块及目标编码。 | 依赖AudioClipAsset，离线完成转码，不在callback执行。 |
| `Tools/AssetPipeline/Cookers/AudioCooker.cpp` | 规划文件 | P3 | 生成clip块和seek信息，验证采样单位、时长与通道。 | 依赖AudioClipAsset，离线完成转码，不在callback执行。 |
| `Tools/AssetPipeline/Cookers/ShaderCooker.h` | 规划文件 | P1 | 声明编译产物到唯一ShaderAsset的打包规则。 | 唯一Runtime类型为Shaders/ShaderAsset.h，不另造shader资产系统。 |
| `Tools/AssetPipeline/Cookers/ShaderCooker.cpp` | 规划文件 | P1 | 封装最终SPIR-V、反射、变体键、工具版本和依赖哈希。 | 唯一Runtime类型为Shaders/ShaderAsset.h，不另造shader资产系统。 |
| `Tools/AssetPipeline/Cookers/ModelCooker.h` | 规划文件 | P2 | 声明Model节点、mesh实例与材质槽的编码。 | 依赖Geometry/ModelAsset、MeshCooker和MaterialDefinition，Model不同于实例化Scene。 |
| `Tools/AssetPipeline/Cookers/ModelCooker.cpp` | 规划文件 | P2 | 编码稳定节点ID、局部变换和依赖，验证层级与引用并保留源映射。 | 依赖Geometry/ModelAsset、MeshCooker和MaterialDefinition，Model不同于实例化Scene。 |
| `Tools/AssetPipeline/Cookers/MaterialCooker.h` | 规划文件 | P1 | 声明材质schema/参数/纹理引用的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/MaterialCooker.cpp` | 规划文件 | P1 | 校验并编码材质schema/参数/纹理引用到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/EnvironmentCooker.h` | 规划文件 | P1 | 声明环境预计算/坐标/色彩的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/EnvironmentCooker.cpp` | 规划文件 | P1 | 校验并编码环境预计算/坐标/色彩到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/CollisionShapeCooker.h` | 规划文件 | P3 | 声明碰撞形状/backend兼容的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/CollisionShapeCooker.cpp` | 规划文件 | P3 | 校验并编码碰撞形状/backend兼容到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/ParticleEffectCooker.h` | 规划文件 | P4 | 声明粒子曲线/容量/材质的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/ParticleEffectCooker.cpp` | 规划文件 | P4 | 校验并编码粒子曲线/容量/材质到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/TerrainCooker.h` | 规划文件 | P4 | 声明tile/LOD边界/高度的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/TerrainCooker.cpp` | 规划文件 | P4 | 校验并编码tile/LOD边界/高度到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/UIDocumentCooker.h` | 规划文件 | P3 | 声明UI/样式/字体依赖的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/UIDocumentCooker.cpp` | 规划文件 | P3 | 校验并编码UI/样式/字体依赖到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/ScriptCooker.h` | 规划文件 | P3 | 声明脚本/语言及字节码版本的运行时编码规则。 | AssetContainerFormat与对应领域类型。 |
| `Tools/AssetPipeline/Cookers/ScriptCooker.cpp` | 规划文件 | P3 | 校验并编码脚本/语言及字节码版本到独立cooked容器，保留参数/工具版本；不覆盖源。 | AssetContainerFormat与对应领域类型。 |

<a id="module-31"></a>

## 31. Tools/AssetCompiler

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tools/AssetCompiler/main.cpp` | 规划文件 | P1 | 解析命令并调用共享AssetPipeline，返回可诊断退出码。 | 不依赖Editor，不覆盖源素材。 |
| `Tools/AssetCompiler/AssetCompilerOptions.h` | 规划文件 | P1 | 声明项目、平台、过滤、缓存与输出参数。 | 依赖工具配置，参数仅控制明确构建计划。 |
| `Tools/AssetCompiler/AssetCompilerOptions.cpp` | 规划文件 | P1 | 验证路径与配置冲突，拒绝派生输出根覆盖原素材目录。 | 依赖工具配置，参数仅控制明确构建计划。 |

<a id="module-32"></a>

## 32. Tools/ShaderCompiler

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tools/ShaderCompiler/main.cpp` | 规划文件 | P1 | 提供独立shader CLI及结构化诊断和退出码。 | 只链接shader_compiler_core；核心库不能链接本main。 |
| `Tools/ShaderCompiler/ShaderCompileJob.h` | 规划文件 | P1 | 声明入口、目标、变体、include和结果契约。 | 核心可被AssetPipeline链接，不反向依赖Editor或AssetPipeline。 |
| `Tools/ShaderCompiler/ShaderCompileJob.cpp` | 规划文件 | P1 | 协调glslang、validate、可选优化和最终反射，构建键覆盖完整配置与依赖。 | 核心可被AssetPipeline链接，不反向依赖Editor或AssetPipeline。 |
| `Tools/ShaderCompiler/GlslangCompiler.h` | 规划文件 | P1 | 声明GLSL到Vulkan SPIR-V的离线适配。 | 私有依赖glslang；Runtime不加载源码编译器，DXC/Slang仅为未来候选。 |
| `Tools/ShaderCompiler/GlslangCompiler.cpp` | 规划文件 | P1 | 编译选定入口和target环境，统一矩阵、布局和binding约定。 | 私有依赖glslang；Runtime不加载源码编译器，DXC/Slang仅为未来候选。 |
| `Tools/ShaderCompiler/SpirvReflection.h` | 规划文件 | P1 | 声明绑定、push constant和输入输出反射结果。 | 私有依赖SPIRV-Reflect，输出统一ShaderMetadata。 |
| `Tools/ShaderCompiler/SpirvReflection.cpp` | 规划文件 | P1 | 对最终字节码提取元数据并检查布局，不推导业务pass依赖。 | 私有依赖SPIRV-Reflect，输出统一ShaderMetadata。 |
| `Tools/ShaderCompiler/SpirvValidator.h` | 规划文件 | P1 | 声明SPIR-V环境检查与诊断接口。 | 仅工具依赖；验证通过不证明shader算法或GPU同步正确。 |
| `Tools/ShaderCompiler/SpirvValidator.cpp` | 规划文件 | P1 | 调用SPIRV-Tools校验产物，优化后再次验证并拒绝非法发布。 | 仅工具依赖；验证通过不证明shader算法或GPU同步正确。 |

<a id="module-33"></a>

## 33. Tools/Packager

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tools/Packager/main.cpp` | 规划文件 | P2 | 读取打包配置，调用PackageBuilder并返回发布状态。 | 只消费有效Cooked产物，不修改原素材或编辑器项目。 |
| `Tools/Packager/PackageBuilder.h` | 规划文件 | P2 | 声明依赖闭包、包清单、校验及平台分发布局。 | 依赖Runtime包格式，不把Editor、importer或完整编译器打入Player。 |
| `Tools/Packager/PackageBuilder.cpp` | 规划文件 | P2 | 收集资产与Runtime文件，检查缺失并原子发布独立包目录。 | 依赖Runtime包格式，不把Editor、importer或完整编译器打入Player。 |

<a id="module-34"></a>

## 34. Tools/AssetInspector

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tools/AssetInspector/main.cpp` | 规划文件 | P2 | 提供只读资产及包检查CLI和结构化错误输出。 | 不升级、修复或重写检查对象。 |
| `Tools/AssetInspector/AssetInspection.h` | 规划文件 | P2 | 声明资产头、块、schema、依赖与校验检查。 | 依赖Runtime格式解析，不实例化World、音频设备或GPU资源。 |
| `Tools/AssetInspector/AssetInspection.cpp` | 规划文件 | P2 | 检测截断、越界、版本和哈希错误，只读解析。 | 依赖Runtime格式解析，不实例化World、音频设备或GPU资源。 |

<a id="module-35"></a>

## 35. Applications

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `CatalpaBox/src/catalpaBox.cpp` | 现有文件 | P0 | 保留示例应用入口，经EngineRuntime调用，不include Vulkan后端。 | Catalpa公开接口。 |
| `CatalpaBox/src/BoxGameplay.h` | 规划文件 | P1 | 声明样例世界/相机/模型系统安装。 | Runtime/Scene公开API。 |
| `CatalpaBox/src/BoxGameplay.cpp` | 规划文件 | P1 | 注册示例Gameplay并引用样例资产，业务不进入引擎基础。 | Runtime/Scene公开API。 |
| `CatalpaPlayer/src/main.cpp` | 规划文件 | P2 | 独立Player入口，读取发布配置并报告错误。 | 不依赖Editor/Importer。 |
| `CatalpaPlayer/src/PlayerHost.h` | 规划文件 | P2 | 声明包挂载、初始场景与游戏模块拥有者。 | Runtime/PackageReader。 |
| `CatalpaPlayer/src/PlayerHost.cpp` | 规划文件 | P2 | 建立Runtime、加载入口场景并有序退出，不编译源shader。 | Runtime/PackageReader。 |

<a id="module-36"></a>

## 36. ConfigurationAndContent

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Config/EngineDefaults.json` | 规划文件 | P0 | 模拟时间/预算/日志/基础同步默认配置。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Config/RendererProfiles.json` | 规划文件 | P1 | 能力要求、呈现和画质fallback。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Config/AudioDefaults.json` | 规划文件 | P3 | 设备、采样与voice/stream预算。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Config/AssetBuildProfiles.json` | 规划文件 | P1 | 模型/纹理/shader目标与派生参数。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Config/PackageProfile.json` | 规划文件 | P2 | 发行入口、资产闭包、允许文件与运行依赖。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Schemas/asset-metadata.schema.json` | 规划文件 | P1 | 资产ID/类型/导入参数/依赖schema。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Schemas/scene.schema.json` | 规划文件 | P1 | 稳定实体ID与组件字段版本。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Schemas/material.schema.json` | 规划文件 | P1 | definition/instance参数与状态约束。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Schemas/project.schema.json` | 规划文件 | P2 | 内容根/启动场景/模块与构建配置。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Schemas/package-manifest.schema.json` | 规划文件 | P2 | 包索引/版本/校验/依赖规则。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Sandbox.catalpaproj` | 规划文件 | P1 | 示例项目入口与Content根配置。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Scenes/StaticScene.scene.json` | 规划文件 | P1 | 多实例/多材质静态示例场景。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Scenes/DynamicScene.scene.json` | 规划文件 | P3 | 动画/物理/空间音频示例场景。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Prefabs/ExampleObject.prefab.json` | 规划文件 | P2 | 稳定局部ID与覆盖示例。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Materials/Example.material.json` | 规划文件 | P1 | 示例PBR参数与纹理引用。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Models/ExampleMesh.gltf` | 规划文件 | P1 | 有明确来源许可的模型源。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Models/ExampleMesh.bin` | 规划文件 | P1 | 配套glTF外部buffer源。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Textures/Checker.png` | 规划文件 | P1 | 显式sRGB语义的示例纹理源。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Audio/Example.wav` | 规划文件 | P3 | 已知采样/通道的音频测试源。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/UI/Hud.rml` | 规划文件 | P3 | 游戏UI示例布局。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/UI/Hud.rcss` | 规划文件 | P3 | 配套UI样式。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/Scripts/Example.lua` | 规划文件 | P3 | 受检查entity API示例。 | 作者内容或配置；导入原素材只读，派生产物另存。 |
| `Projects/Sandbox/Content/asset_metadata.json` | 规划文件 | P1 | 样例源的稳定ID/许可来源/参数登记。 | 作者内容或配置；导入原素材只读，派生产物另存。 |

<a id="module-37"></a>

## 37. BuiltinResources

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Catalpa/Resources/Shaders/Common/Bindings.glsl` | 规划文件 | P0 | 资源布局与GPU ABI。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Common/Math.glsl` | 规划文件 | P1 | 统一坐标/矩阵辅助。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Common/Pbr.glsl` | 规划文件 | P1 | PBR评估函数。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Common/Lighting.glsl` | 规划文件 | P1 | 光照/阴影共享定义。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Triangle.vert` | 规划文件 | P0 | 最小顶点shader。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Triangle.frag` | 规划文件 | P0 | 最小片元shader。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Mesh.vert` | 规划文件 | P1 | 顶点布局/实例/view变换。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Pbr.frag` | 规划文件 | P1 | 标准PBR与alpha模式。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Unlit.frag` | 规划文件 | P1 | Unlit和错误回退。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Depth.vert` | 规划文件 | P1 | 深度预通道，变形与颜色通道一致。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Shadow.vert` | 规划文件 | P1 | 阴影视图变换。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Fullscreen.vert` | 规划文件 | P1 | 全屏后处理入口。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Tonemap.frag` | 规划文件 | P1 | 曝光/HDR/输出色彩转换。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Present.frag` | 规划文件 | P1 | 离屏到呈现附件的必要转换。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Skybox.vert` | 规划文件 | P1 | 环境方向与深度。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Skybox.frag` | 规划文件 | P1 | 环境采样与曝光。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Picking.frag` | 规划文件 | P2 | 稳定实例映射ID输出。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/DebugDraw.vert` | 规划文件 | P2 | 调试几何变换。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/DebugDraw.frag` | 规划文件 | P2 | 调试颜色。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/GameUI.vert` | 规划文件 | P3 | UI像素坐标与transform。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/GameUI.frag` | 规划文件 | P3 | 预乘alpha纹理和裁剪。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Skinning.comp` | 规划文件 | P3 | 骨骼变形输出。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/LightCulling.comp` | 规划文件 | P3 | 有容量上限的灯光列表。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/DepthPyramid.comp` | 规划文件 | P4 | 保守层级深度。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/GpuCulling.comp` | 规划文件 | P4 | 可见性/间接draw数据。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Particles.vert` | 规划文件 | P4 | 粒子billboard。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Particles.frag` | 规划文件 | P4 | 粒子透明材质。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Terrain.vert` | 规划文件 | P4 | tile几何/高度。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Shaders/Terrain.frag` | 规划文件 | P4 | 地形层混合与光照。 | 工具输入；Player消费SPIR-V与metadata，不依赖源编译器。 |
| `Catalpa/Resources/Materials/DefaultPbr.material.json` | 规划文件 | P1 | PBR定义与默认参数。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/Materials/DefaultUnlit.material.json` | 规划文件 | P1 | Unlit定义与shader引用。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/Materials/Error.material.json` | 规划文件 | P1 | 可见的失败回退材质。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/Textures/White.png` | 规划文件 | P1 | 白色fallback源。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/Textures/FlatNormal.png` | 规划文件 | P1 | 平坦法线fallback数据源。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/Textures/Error.png` | 规划文件 | P1 | 诊断纹理源。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |
| `Catalpa/Resources/BuiltinAssets.json` | 规划文件 | P1 | 内置资源ID/许可/依赖/导入参数登记。 | 通过统一AssetPipeline，不在资产层创建Vk对象。 |

<a id="module-38"></a>

## 38. Tests

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `Tests/main.cpp` | 规划文件 | P0 | Catch2入口与测试初始化。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Support/FakeRHI.h` | 规划文件 | P0 | 可控提交成功/失败/完成值替身。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Support/TestAssets.h` | 规划文件 | P1 | 临时资产与坏数据构造，原素材只读。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Core/HandleTests.cpp` | 规划文件 | P0 | generation/owner/陈旧引用。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Core/JobShutdownTests.cpp` | 规划文件 | P0 | 取消/收束/关闭后回调。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Core/ArchiveBoundsTests.cpp` | 规划文件 | P1 | offset/长度/截断/字节序。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/ECS/WorldTests.cpp` | 规划文件 | P1 | 实体/跨World/结构安全点。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/ECS/SystemSchedulerTests.cpp` | 规划文件 | P1 | 读写冲突/阶段/依赖环。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Scene/SceneRoundTripTests.cpp` | 规划文件 | P1 | 稳定ID/字段/引用往返。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Scene/TransformHierarchyTests.cpp` | 规划文件 | P1 | 父子矩阵/环/非均匀缩放。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Scene/PrefabTests.cpp` | 规划文件 | P2 | 局部ID/覆盖/失败回滚。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Geometry/MeshValidationTests.cpp` | 规划文件 | P1 | 索引/stride/submesh/skin。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Materials/MaterialSchemaTests.cpp` | 规划文件 | P1 | 参数/default/shader布局。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Camera/ProjectionFrustumTests.cpp` | 规划文件 | P1 | 投影/逆矩阵/深度/剔除一致。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Assets/AssetLifecycleTests.cpp` | 规划文件 | P1 | 加载/取消/依赖/版本保活。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Assets/PackageValidationTests.cpp` | 规划文件 | P2 | 坏包/解压上限/schema/校验。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Renderer/RenderGraphHazardTests.cpp` | 规划文件 | P0 | RAW/WAR/WAW/子资源/外部访问。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Renderer/FrameLifetimeTests.cpp` | 规划文件 | P0 | 帧包保活/未提交/slot复用。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Renderer/MultiQueueTests.cpp` | 规划文件 | P4 | 完成集合/readers frontier/ownership。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Vulkan/RetirementTests.cpp` | 规划文件 | P0 | 真实完成后销毁与失败提交。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Vulkan/SwapchainLifecycleTests.cpp` | 规划文件 | P0 | slot/image/resize/present同步。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Vulkan/ValidationSmokeTests.cpp` | 规划文件 | P0 | 真实GPU最小绘制/上传/回读/关闭。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Animation/AnimationSamplingTests.cpp` | 规划文件 | P3 | 骨骼映射/时间/混合/root motion。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Physics/TransformAuthorityTests.cpp` | 规划文件 | P3 | dynamic/kinematic/teleport/fixed step。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Audio/AudioLifecycleTests.cpp` | 规划文件 | P3 | 顺序/stop确认/clip卸载/Null后端。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Editor/UndoRedoTests.cpp` | 规划文件 | P2 | 事务/失败撤销/World隔离。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Tools/CookReproducibilityTests.cpp` | 规划文件 | P2 | 缓存键/版本/原素材哈希不变。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Tools/ShaderMetadataTests.cpp` | 规划文件 | P1 | 最终SPIR-V与元数据/CPU GPU ABI。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Networking/LoopbackTests.cpp` | 规划文件 | P4 | 有界消息与会话关闭，不验证跨机器replication。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Integration/PlayerPackageTests.cpp` | 规划文件 | P2 | 无Editor/源/SDK依赖的运行包。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Fixtures/InvalidScene.scene.json` | 规划文件 | P1 | 错误场景只读输入。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |
| `Tests/Fixtures/MaterialTypeMismatch.material.json` | 规划文件 | P1 | 类型错误材质只读输入。 | 未来独立测试目标，CPU/GPU分开；本轮未创建或运行。 |

<a id="module-39"></a>

## 39. Documentation

| 具体文件 | 状态 | 阶段 | 职责 | 依赖与边界 |
|---|---|---|---|---|
| `docx/deepseek_vulkan_arch.md` | 现有文件 | P0 | 保留其他AI参考，不执行其中命令。 | 文档，不代表源实现已完成。 |
| `docx/catalpa_engine_architecture.md` | 本轮新增文档 | P0 | 本轮总体设计、证据、职责、同步、选库与验收。 | 文档，不代表源实现已完成。 |
| `docx/catalpa_engine_file_manifest.md` | 本轮新增文档 | P0 | 本轮逐文件职责/阶段/依赖清单。 | 文档，不代表源实现已完成。 |

## 清单之外的生成物

| 类别 | 管理方式 |
|---|---|
| 工程文件 | 当前`CatalpaEngine.sln`及各目标vcxproj/filters由Premake生成，不另行手工维护第二套依赖图。 |
| 编译产物 | `bin`、`bin-int`由平台/配置/目标决定；发行按PackageProfile及实际链接依赖收集。 |
| Shader产物 | 如`Triangle.vert.spv`及metadata，由compiler/target/variant key区分，不假设只有一个变体。 |
| Cooked资产 | 按AssetId/平台/版本生成，包索引定位；reader/writer/schema已逐项列出。 |
| DerivedDataCache | 内容键命名的可重建派生缓存，不是唯一源数据。 |
| 第三方 | Dependencies.lock或vcpkg锁定上游版本、选项、实际源清单与许可。 |

上述产物取决于构建配置和用户内容，没有诚实可预写的固定最终全集。本方案负责它们的脚本、schema、输入示例和runtime读取契约已经逐项列入。

## 实施规则

不要照表一次创建空类。按阶段形成真实可验证闭环；小文件可以在实现时合并，但不能丢失责任、线程/生命周期契约或测试边界。可选领域的最低扩展入口不是完整AAA功能承诺。
