#include "catpch.h"

#include "TestRunner.h"
#include "Animation/AnimationAssetLoader.h"
#include "Geometry/PrimitiveMesh.h"
#include "Runtime/Systems/AnimationAuthoring.h"
#include "Scene/Components/AnimatorComponent.h"
#include "Scene/Scene.h"
#include "Scene/ModelInstantiation.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabInstantiation.h"
#include "Scene/PrefabAsset.h"
#include "Scene/DomainComponents.h"

namespace Catalpa::Tests {
	void RunAnimationContractsTests(TestContext& test) {
		const auto id = [](uint64_t value) { return StableId::FromWords(0x414e494d4154494fULL, value); };
		std::string error, text;
		/* 先构造双关节绑定姿态，验证层级、逆绑定矩阵及编解码往返。 */
		SkeletonAsset skeleton;
		skeleton.joints.push_back({id(1), "Root", -1, {}});
		Math::TransformTRS child;
		child.translation = {0, 1, 0};
		Math::Mat4 inverse{1};
		inverse[3][1] = -1;
		skeleton.joints.push_back({id(2), "Tip", 0, child, inverse});
		test.Check(ValidateSkeleton(skeleton, error), "Bind hierarchy and inverse bind agree");
		auto invalid = skeleton;
		invalid.joints[0].parent = 1;
		test.Check(!ValidateSkeleton(invalid, error), "Skeleton cycles are rejected before pose evaluation");
		invalid = skeleton;
		invalid.joints[1].inverseBind = Math::Mat4{1};
		test.Check(!ValidateSkeleton(invalid, error), "Incorrect inverse-bind matrices are rejected");
		test.Check(WriteSkeleton(skeleton, text, error), "Skeleton writes author data");
		SkeletonAsset decoded;
		test.Check(ReadSkeleton(text, decoded, error) && decoded.joints[1].parent == 0,
		    "Skeleton identity and hierarchy roundtrip");
		AssetVersionRef<SkeletonAsset> fixed;
		test.Check(AssetVersionRef<SkeletonAsset>::TryCreate(
		               AssetId{id(3)}, 7, std::make_shared<const SkeletonAsset>(skeleton), fixed, error),
		    "Bind immutable skeleton version");

		/* 在同一骨架上比较插值方式，并检查旋转关键帧和重复时间点的约束。 */
		AnimationClip clip;
		clip.skeleton = fixed.GetId();
		clip.skeletonVersion = fixed;
		clip.duration = 1;
		clip.rootMotionJoint = id(1);
		AnimationTrack track;
		track.joint = id(1);
		track.translation.keys = {{0, {0, 0, 0}}, {1, {2, 0, 0}}};
		clip.tracks.push_back(track);
		clip.events = {{0, "start", ""}, {.5, "step", "left"}};
		AnimationPose sampled;
		test.Check(SampleAnimation(skeleton, clip, .25, sampled, error) &&
		        std::abs(sampled.local[0].translation.x - .5f) < 1e-6,
		    "Linear position sampling uses seconds");
		test.Check(std::abs(sampled.model[1][3].y - 1) < 1e-6, "Sampled child composes parent pose");
		auto stepped = clip;
		stepped.tracks[0].translation.interpolation = AnimationInterpolation::Step;
		test.Check(SampleAnimation(skeleton, stepped, .75, sampled, error) && sampled.local[0].translation.x == 0,
		    "Step interpolation retains the left key");
		auto cubic = clip;
		cubic.tracks[0].translation.interpolation = AnimationInterpolation::CubicSpline;
		cubic.tracks[0].translation.keys[0].outTangent = {2, 0, 0};
		cubic.tracks[0].translation.keys[1].inTangent = {2, 0, 0};
		test.Check(SampleAnimation(skeleton, cubic, .25, sampled, error) &&
		        std::abs(sampled.local[0].translation.x - .5f) < 1e-6,
		    "Cubic tangents are derivatives scaled by interval seconds");
		auto rotation = clip;
		rotation.tracks[0].rotation.keys = {{0, {1, 0, 0, 0}}, {1, {-1, 0, 0, 0}}};
		test.Check(SampleAnimation(skeleton, rotation, .5, sampled, error) &&
		        std::abs(std::abs(sampled.local[0].rotation.w) - 1) < 1e-6,
		    "Quaternion interpolation takes the shortest equivalent orientation");
		auto duplicate = clip;
		duplicate.tracks[0].translation.keys.push_back({1, {3, 0, 0}});
		test.Check(!ValidateAnimationClip(duplicate, &skeleton, error), "Duplicate key times are not silently merged");
		/* 推进播放时间，验证循环、事件与根运动在不同推进方向下的结果。 */
		AnimatorState state;
		AnimationAdvance advance;
		test.Check(AdvanceAnimation(skeleton, clip, .75, 1, true, true, state, advance, error) &&
		        advance.events.size() == 2 && std::abs(advance.rootTranslation.x - 1.5f) < 1e-6,
		    "Advance delivers initial and crossed events with root motion");
		test.Check(
		    advance.pose.local[0].translation.x == 0, "Extracted root motion is removed from the sampled root pose");
		test.Check(AdvanceAnimation(skeleton, clip, .5, 1, true, true, state, advance, error) &&
		        std::abs(state.time - .25) < 1e-9 && advance.events.size() == 1 &&
		        std::abs(advance.rootTranslation.x - 1) < 1e-6,
		    "Loop motion integrates across segments without adding the wrap discontinuity");
		state = {};
		test.Check(AdvanceAnimation(skeleton, clip, .75, -1, true, false, state, advance, error) &&
		        advance.events.size() == 1 && advance.events[0].name == "step",
		    "Reverse traversal at time zero does not duplicate the zero event");
		state = {};
		test.Check(AdvanceAnimation(skeleton, clip, 1, 1, false, false, state, advance, error) &&
		        AdvanceAnimation(skeleton, clip, 1, 1, false, false, state, advance, error) && state.time == 1 &&
		        advance.events.empty(),
		    "Non-looping clips clamp without repeating terminal events");
		test.Check(WriteAnimationClip(clip, text, error), "Clip writes channels, events and root identity");
		AnimationClip clipRoundtrip;
		test.Check(ReadAnimationClip(text, clipRoundtrip, error) && clipRoundtrip.events[1].payload == "left",
		    "Clip events and payload roundtrip");
		AssetVersionRef<AnimationClip> evaluatedClip;
		test.Check(AssetVersionRef<AnimationClip>::TryCreate(
		               AssetId{id(38)}, 1, std::make_shared<const AnimationClip>(clip), evaluatedClip, error),
		    "Bind evaluator clip version");
		ValidatedClipEvaluator evaluator;
		test.Check(evaluator.Bind(evaluatedClip, error) && evaluator.Sample(.25, sampled, error) &&
		        std::abs(sampled.local[0].translation.x - .5f) < 1e-6,
		    "Validated evaluator samples the same immutable clip using its prechecked joint mapping");
		auto damaged = clip;
		damaged.tracks[0].translation.keys.push_back({1, {3, 0, 0}});
		AssetVersionRef<AnimationClip> invalidClip;
		test.Check(AssetVersionRef<AnimationClip>::TryCreate(
		               evaluatedClip.GetId(), 2, std::make_shared<const AnimationClip>(damaged), invalidClip, error),
		    "Prepare invalid candidate clip version");
		test.Check(!evaluator.Bind(invalidClip, error) && evaluator.Clip().GetVersion() == 1 &&
		        evaluator.Sample(.25, sampled, error) && std::abs(sampled.local[0].translation.x - .5f) < 1e-6,
		    "Rejected clip rebind preserves the previously validated version and sampling state");
		auto faster = clip;
		faster.tracks[0].translation.keys[1].value.x = 4;
		AssetVersionRef<AnimationClip> collision, newVersion;
		test.Check(AssetVersionRef<AnimationClip>::TryCreate(
		               evaluatedClip.GetId(), 1, std::make_shared<const AnimationClip>(faster), collision, error) &&
		        !evaluator.Bind(collision, error),
		    "Evaluator rejects different content under an already bound identical version");
		test.Check(AssetVersionRef<AnimationClip>::TryCreate(
		               evaluatedClip.GetId(), 2, std::make_shared<const AnimationClip>(faster), newVersion, error) &&
		        evaluator.Bind(newVersion, error) && evaluator.Sample(.25, sampled, error) &&
		        std::abs(sampled.local[0].translation.x - 1) < 1e-6,
		    "A genuine clip version change validates once and replaces its cached mapping");
		test.Check(!evaluator.Sample(std::numeric_limits<double>::quiet_NaN(), sampled, error),
		    "Validated context still rejects non-finite runtime sample times");
		test.Check(EncodeAnimationClip(clip, text, error) && text.starts_with("CANI1") &&
		        ReadAnimationClip(text, clipRoundtrip, error) &&
		        clipRoundtrip.tracks[0].translation.keys[1].value.x == 2 && clipRoundtrip.events.size() == 2,
		    "Cooked clip uses explicit little-endian channel and event encoding");
		text.pop_back();
		test.Check(!ReadAnimationClip(text, clipRoundtrip, error) && clipRoundtrip.events.size() == 2,
		    "Truncated cooked clip cannot publish partial output");

		/* 真实导入会在动画根上方附加单位/轴转换；运动位移只转换一次且 local/model 仍一致。 */
		/* 引入根坐标转换，检查变换后的绑定矩阵与动画采样仍使用相同空间。 */
		SkeletonAsset converted = skeleton;
		Math::TransformTRS conversion;
		conversion.rotation = glm::angleAxis(glm::radians(90.f), Math::Vec3(0, 0, 1));
		conversion.scale = Math::Vec3(.01f);
		converted.joints[0].parent = 2;
		converted.joints.push_back({id(30), "Conversion", -1, conversion});
		std::vector<Math::TransformTRS> locals;
		for (const auto& joint : converted.joints)
			locals.push_back(joint.bindLocal);
		std::vector<Math::Mat4> convertedBind;
		test.Check(BuildModelPose(converted, locals, convertedBind, error), "Build converted source skeleton");
		for (size_t i = 0; i < converted.joints.size(); ++i)
			test.Check(Math::InverseAffine(convertedBind[i], converted.joints[i].inverseBind).IsSuccess(),
			    "Build converted inverse bind");
		auto convertedClip = clip;
		convertedClip.skeletonVersion = {};
		state = {};
		test.Check(AdvanceAnimation(converted, convertedClip, .5, 1, true, true, state, advance, error) &&
		        std::abs(advance.rootTranslation.y - .01f) < 1e-6 && std::abs(advance.rootTranslation.x) < 1e-6,
		    "Nested motion root produces metres in the converted skeleton-model basis");
		std::vector<Math::Mat4> rebuilt;
		test.Check(BuildModelPose(converted, advance.pose.local, rebuilt, error) &&
		        glm::length(Math::Vec3(rebuilt[0][3])) < 1e-6 &&
		        glm::length(Math::Vec3(advance.pose.model[0][3] - rebuilt[0][3])) < 1e-6,
		    "Root-motion removal preserves hierarchy-consistent local and model poses");

		MeshData mesh;
		test.Check(CreateCubeMeshWithTangents(1, mesh).IsSuccess(), "Reference skinning source cube is valid");
		std::vector<SkinInfluence> weights(mesh.vertexCount);
		std::vector<Math::Mat4> palette{Math::Mat4{1}};
		MeshData skinned;
		test.Check(SkinMeshReference(mesh, weights, palette, skinned, error) && skinned.vertexData == mesh.vertexData,
		    "Identity skin preserves all vertex attributes exactly");
		palette[0][0][0] = -1;
		test.Check(SkinMeshReference(mesh, weights, palette, skinned, error),
		    "Mirrored skinning keeps a valid tangent handedness and inverse-transpose normal");
		weights[0].weights[0] = .5f;
		test.Check(!SkinMeshReference(mesh, weights, palette, skinned, error),
		    "Invalid weight sums do not silently normalize source data");
		weights[0].weights[0] = 1;
		weights[0].joints[0] = 1;
		test.Check(!SkinMeshReference(mesh, weights, palette, skinned, error),
		    "Joint indices are validated against the exact palette");
		/* 显式提供源绑定矩阵，比较序列化结果、缓存调色板和 CPU 参考蒙皮。 */
		MeshSkinBinding binding;
		binding.skeleton = fixed.GetId();
		binding.mesh = AssetId{id(4)};
		binding.influences.resize(70000);
		Math::Mat4 sourceBind{1};
		sourceBind[3][0] = 3;
		binding.perJointInverseBind = {sourceBind};
		test.Check(EncodeMeshSkinBinding(binding, text, error),
		    "Cooked skin encoding supports more than JSON's 65536-entry author collection bound");
		MeshSkinBinding skinRoundtrip;
		test.Check(ReadMeshSkinBinding(text, skinRoundtrip, error) && skinRoundtrip.influences.size() == 70000 &&
		        skinRoundtrip.perJointInverseBind.size() == 1 && skinRoundtrip.perJointInverseBind[0][3][0] == 3,
		    "Binary skin count and bytes roundtrip without JSON DOM expansion");
		text.pop_back();
		test.Check(
		    !ReadMeshSkinBinding(text, skinRoundtrip, error), "Truncated binary skin cannot publish a partial stream");
		MeshAsset meshAsset;
		meshAsset.materialSlots = {"surface"};
		test.Check(CreateMeshResource(mesh, meshAsset.mesh).IsSuccess(), "Create immutable mesh for skin evaluator");
		AssetVersionRef<MeshAsset> meshVersion;
		test.Check(AssetVersionRef<MeshAsset>::TryCreate(
		               AssetId{id(39)}, 2, std::make_shared<const MeshAsset>(meshAsset), meshVersion, error),
		    "Bind exact skin mesh version");
		MeshSkinBinding liveBinding;
		liveBinding.skeleton = fixed.GetId();
		liveBinding.skeletonVersion = fixed;
		liveBinding.mesh = meshVersion.GetId();
		liveBinding.meshVersion = meshVersion;
		liveBinding.influences.resize(mesh.vertexCount);
		liveBinding.perJointInverseBind = {sourceBind, inverse};
		AssetVersionRef<MeshSkinBinding> liveVersion;
		test.Check(AssetVersionRef<MeshSkinBinding>::TryCreate(
		               AssetId{id(40)}, 4, std::make_shared<const MeshSkinBinding>(liveBinding), liveVersion, error),
		    "Bind exact source bind-shape version");
		ValidatedSkinEvaluator skinEvaluator;
		std::vector<Math::Mat4> cachedPalette, referencePalette;
		test.Check(skinEvaluator.Bind(liveVersion, error) && SampleAnimation(skeleton, clip, .25, sampled, error) &&
		        skinEvaluator.BuildPalette(sampled, cachedPalette, error) &&
		        BuildSkinningPalette(liveBinding, sampled, referencePalette, error) &&
		        std::abs(cachedPalette[0][3].x - 3.5f) < 1e-6 &&
		        glm::length(Math::Vec3(cachedPalette[0][3] - referencePalette[0][3])) < 1e-6,
		    "Validated skin context preserves source bind-shape and agrees with full-checked palette "
		    "construction");
		auto badBinding = liveBinding;
		badBinding.influences[0].weights[0] = .5f;
		AssetVersionRef<MeshSkinBinding> invalidBinding;
		test.Check(AssetVersionRef<MeshSkinBinding>::TryCreate(liveVersion.GetId(), 5,
		               std::make_shared<const MeshSkinBinding>(badBinding), invalidBinding, error) &&
		        !skinEvaluator.Bind(invalidBinding, error) && skinEvaluator.Binding().GetVersion() == 4,
		    "Failed skin rebind preserves the previous checked weight stream and bind matrices");
		auto badPose = sampled;
		badPose.model[0][3].x = std::numeric_limits<float>::quiet_NaN();
		test.Check(
		    !skinEvaluator.BuildPalette(badPose, cachedPalette, error) && std::abs(cachedPalette[0][3].x - 3.5f) < 1e-6,
		    "Cached skin evaluation still validates every runtime pose matrix before publication");

		AssetVersionRef<AnimationClip> a, b;
		auto second = clip;
		second.tracks[0].translation.keys[1].value = {4, 0, 0};
		test.Check(AssetVersionRef<AnimationClip>::TryCreate(
		               AssetId{id(5)}, 1, std::make_shared<const AnimationClip>(clip), a, error) &&
		        AssetVersionRef<AnimationClip>::TryCreate(
		            AssetId{id(6)}, 1, std::make_shared<const AnimationClip>(second), b, error),
		    "Two clips bind the same skeleton version");
		/* 构造双输入混合树，再验证阈值、骨架身份、环和编解码。 */
		BlendTreeAsset tree;
		tree.skeleton = fixed.GetId();
		tree.skeletonVersion = fixed;
		tree.parameters = {{"speed", .5f}};
		BlendTreeNode left;
		left.id = id(7);
		left.clip = a.GetId();
		left.clipVersion = a;
		BlendTreeNode right;
		right.id = id(8);
		right.clip = b.GetId();
		right.clipVersion = b;
		BlendTreeNode blend;
		blend.id = id(9);
		blend.kind = BlendNodeKind::Blend1D;
		blend.parameter = "speed";
		blend.children = {left.id, right.id};
		blend.thresholds = {0, 1};
		tree.nodes = {left, right, blend};
		tree.states = {{id(10), blend.id, "Locomotion"}};
		tree.initialState = id(10);
		BlendTreeRuntime runtime;
		test.Check(EvaluateBlendTree(tree, {}, .25, runtime, advance, error) &&
		        std::abs(advance.pose.local[0].translation.x - .75f) < 1e-6,
		    "Blend1D uses adjacent threshold weights and independent node clocks");
		runtime = {};
		test.Check(EvaluateBlendTree(tree, {}, .25, runtime, advance, error, false, 2) &&
		        std::abs(advance.pose.local[0].translation.x - 1.5f) < 1e-6,
		    "Animator playback speed scales clip clocks while retaining transition durations in seconds");
		auto wideThresholds = tree;
		wideThresholds.nodes.back().thresholds = {
		    -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
		wideThresholds.parameters["speed"] = 0;
		runtime = {};
		test.Check(EvaluateBlendTree(wideThresholds, {}, .25, runtime, advance, error) &&
		        std::abs(advance.pose.local[0].translation.x - .75f) < 1e-6,
		    "Finite wide thresholds preserve their midpoint weight without float-span overflow");
		auto wrongSkeletonIdentity = tree;
		wrongSkeletonIdentity.skeleton = AssetId{id(99)};
		test.Check(!ValidateBlendTree(wrongSkeletonIdentity, error),
		    "Blend skeleton handle must agree with its fixed skeleton identity");
		tree.layout[left.id] = {27, 39};
		auto cycle = tree;
		cycle.nodes.back().children[0] = blend.id;
		test.Check(!ValidateBlendTree(cycle, error) && error.find(blend.id.ToString()) != std::string::npos,
		    "Blend cycles identify the concrete offending node");
		tree.nodes.back().speed = 2;
		test.Check(
		    !ValidateBlendTree(tree, error), "Unused blend-node speed is rejected instead of being silently ignored");
		tree.nodes.back().speed = 1;
		test.Check(WriteBlendTree(tree, text, error), "Blend tree author serialization validates the DAG");
		BlendTreeAsset treeRoundtrip;
		test.Check(ReadBlendTree(text, treeRoundtrip, error) && treeRoundtrip.nodes.back().children.size() == 2 &&
		        treeRoundtrip.layout.at(left.id).first == 27,
		    "Blend topology and thresholds roundtrip");

		Scene scene("Animation authoring");
		PersistentEntityId entity{id(20)};
		test.Check(scene.QueueCreate(entity, "Actor", {}).IsSuccess() && scene.CommitPending().IsSuccess(),
		    "Create an animation owner at a safe point");
		std::map<std::string, ArchiveValue, std::less<>> dynamic;
		ArchiveValue::Object parameters;
		dynamic.emplace("catalpa.animator",
		    ArchiveValue{ArchiveValue::Object{{"schemaVersion", ArchiveValue{uint64_t(1)}},
		        {"animation", ArchiveValue{a.GetId().ToString()}}, {"speed", ArchiveValue{1.0}},
		        {"enabled", ArchiveValue{true}}, {"loop", ArchiveValue{true}}, {"rootMotion", ArchiveValue{false}},
		        {"parameters", ArchiveValue{parameters}}}});
		test.Check(RestoreAnimationComponents(scene, scene.Resolve(entity), dynamic, error) &&
		        scene.CommitPending().IsSuccess(),
		    "Dynamic animation author fields restore at a Scene safe point");
		std::map<std::string, ArchiveValue, std::less<>> captured;
		test.Check(CaptureAnimationComponents(scene.GetWorld(), scene.Resolve(entity), captured, error) &&
		        captured.contains("catalpa.animator"),
		    "Animation components survive author snapshot capture");

		ModelAsset imported;
		for (const auto& joint : converted.joints)
			imported.nodes.push_back({joint.id, joint.name, joint.parent, joint.bindLocal});
		Math::TransformTRS separate;
		separate.translation = {2, 0, 0};
		imported.nodes.push_back({id(31), "Second source root", -1, separate});
		AssetVersionRef<SkeletonAsset> convertedFixed;
		test.Check(AssetVersionRef<SkeletonAsset>::TryCreate(
		               AssetId{id(37)}, 11, std::make_shared<const SkeletonAsset>(converted), convertedFixed, error),
		    "Bind the converted skeleton fixture");
		imported.skeleton.id = convertedFixed.GetId();
		imported.skeletonVersion = convertedFixed;
		AssetVersionRef<ModelAsset> fixedModel;
		test.Check(AssetVersionRef<ModelAsset>::TryCreate(
		               AssetId{id(32)}, 3, std::make_shared<const ModelAsset>(imported), fixedModel, error),
		    "Bind a multiple-root model fixture");
		Scene animatedScene("Multiple source roots");
		std::shared_ptr<ModelInstantiationResult> instantiated;
		test.Check(ModelInstantiation::Queue(animatedScene, fixedModel, {}, {}, instantiated, error) &&
		        animatedScene.CommitPending().IsSuccess() && instantiated && instantiated->complete &&
		        instantiated->success && instantiated->roots.size() == 1 &&
		        instantiated->nodes.size() == imported.nodes.size(),
		    "Animated model uses one identity animator anchor and retains every source-node mapping");
		if (instantiated && instantiated->success && !instantiated->roots.empty()) {
			const auto anchor = animatedScene.Resolve(instantiated->roots.front());
			TransformComponent transform;
			AnimatorComponent owner;
			test.Check(animatedScene.GetTransform(anchor, transform).IsSuccess() &&
			        transform.local.translation == Math::Vec3(0) && transform.local.scale == Math::Vec3(1) &&
			        std::abs(transform.local.rotation.w - 1) < 1e-6 &&
			        animatedScene.GetWorld().Get(anchor, owner).IsSuccess() && owner.skeletonVersion.GetVersion() == 11,
			    "Animator anchor does not apply the source unit/axis conversion a second time");
			SceneAsset capturedScene;
			test.Check(SceneSerializer::Capture(animatedScene, capturedScene, error),
			    "Capture animated model and fixed dynamic dependencies");
			SceneManager cloned;
			SceneHandle play;
			test.Check(SceneSerializer::Instantiate(capturedScene, cloned, nullptr, play, error),
			    "Play clone restores animation without consulting a newer asset catalog");
			auto* playScene = cloned.GetScene(play);
			AnimatorComponent restored;
			if (playScene)
				test.Check(playScene->GetWorld()
				               .Get(playScene->Resolve(animatedScene.MakeReference(anchor).persistent), restored)
				               .IsSuccess() &&
				        restored.skeletonVersion.GetVersion() == 11,
				    "Play clone pins the captured skeleton generation");
			PrefabAsset prefab{capturedScene, {}};
			Scene prefabScene("Animated prefab");
			PersistentEntityId prefabRoot;
			test.Check(PrefabInstantiation::Instantiate(
			               prefabScene, AssetId{id(33)}, 2, prefab, nullptr, {}, {}, prefabRoot, error) &&
			        prefabScene.GetWorld().Get(prefabScene.Resolve(prefabRoot), restored).IsSuccess() &&
			        restored.skeletonVersion.GetVersion() == 11,
			    "Prefab snapshot preserves dynamic animation fields and fixed dependency values");
		}
		auto remapped = captured;
		const auto original = PersistentEntityId{id(34)}, replacement = PersistentEntityId{id(35)};
		remapped["catalpa.skinned_mesh"] = ArchiveValue{ArchiveValue::Object{
		    {"schemaVersion", ArchiveValue{uint64_t(1)}}, {"binding", ArchiveValue{AssetId{id(36)}.ToString()}},
		    {"animator", ArchiveValue{original.ToString()}}, {"enabled", ArchiveValue{true}}}};
		test.Check(RemapDomainEntityReferences(remapped, {{original, replacement}}, error) &&
		        std::get<std::string>(
		            std::get<ArchiveValue::Object>(remapped.at("catalpa.skinned_mesh").data).at("animator").data) ==
		            replacement.ToString(),
		    "Prefab remapping changes animator entity references and leaves asset identities intact");
	}
} /* 命名空间 Catalpa::Tests */
