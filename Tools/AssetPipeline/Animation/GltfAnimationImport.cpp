#include "catpch.h"

#include "AssetPipeline/Animation/AnimationImport.h"
#include "Animation/AnimationAssetLoader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

namespace Catalpa::Tools {
	namespace {
		bool Product(AssetBuildContext& context, AssetImportResult& result, const std::string& key,
		    const std::string& type, const std::string& name, const std::string& text,
		    std::vector<AssetId> dependencies, std::string& error) {
			if (!context.ReserveWorking(text.size(), error) || !context.WorkspaceFits(text.size(), error))
				return false;
			const auto id = context.Identity(key);
			const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
			result.assets.push_back(
			    {id, key, name, ImportedAuthorProduct{type, {bytes.begin(), bytes.end()}, std::move(dependencies)}});
			result.mappings.push_back({id, key, name, type});
			return true;
		}

		/**
		 * @brief 将 glTF 插值枚举映射为引擎动画轨道的插值方式。
		 *
		 * @param[in] value glTF 动画采样器声明的插值枚举。
		 *
		 * @return 与输入枚举对应的阶梯、线性或三次样条插值方式；未知枚举会抛出异常。
		 */
		AnimationInterpolation Mode(fastgltf::AnimationInterpolation value) {
			switch (value) {
			case fastgltf::AnimationInterpolation::Step:
				return AnimationInterpolation::Step;
			case fastgltf::AnimationInterpolation::Linear:
				return AnimationInterpolation::Linear;
			case fastgltf::AnimationInterpolation::CubicSpline:
				return AnimationInterpolation::CubicSpline;
			}
			throw std::invalid_argument("Invalid glTF interpolation");
		}
	} /* 命名空间 */

	bool AppendGltfAnimations(AssetBuildContext& context, const fastgltf::Asset& asset, ModelAsset& model,
	    AssetImportResult& result, const PrimitiveRemaps& remaps, std::string& error) try {
		if (asset.skins.empty() && asset.animations.empty())
			return true;
		/* 排除承载网格实例的代理节点，按模型层级建立关节，并由绑定姿势计算逆绑定矩阵。 */
		std::set<StableId> proxy;
		for (const auto& instance : model.meshes)
			proxy.insert(model.nodes[instance.node].id);
		SkeletonAsset skeleton;
		std::map<StableId, uint32_t> joints;
		for (const auto& node : model.nodes)
			if (!proxy.contains(node.id)) {
				joints.emplace(node.id, uint32_t(skeleton.joints.size()));
				skeleton.joints.push_back({node.id, node.name, -1, node.local});
			}
		if (skeleton.joints.size() > MaximumSkeletonJoints) {
			error = "Selected animated glTF hierarchy exceeds 256 joints including its conversion root";
			return false;
		}
		for (const auto& node : model.nodes)
			if (joints.contains(node.id) && node.parent >= 0)
				skeleton.joints[joints.at(node.id)].parent = int32_t(joints.at(model.nodes[size_t(node.parent)].id));
		std::vector<Math::TransformTRS> locals;
		for (const auto& joint : skeleton.joints)
			locals.push_back(joint.bindLocal);
		std::vector<Math::Mat4> modelPose;
		if (!BuildModelPose(skeleton, locals, modelPose, error))
			return false;
		for (size_t i = 0; i < skeleton.joints.size(); ++i) {
			const auto status = Math::InverseAffine(modelPose[i], skeleton.joints[i].inverseBind);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
		}
		const auto skeletonId = context.Identity("skeleton");
		model.skeleton.id = skeletonId;
		std::string text;
		if (!WriteSkeleton(skeleton, text, error))
			return false;
		if (!Product(context, result, "skeleton", "skeleton", "Skeleton", text, {}, error))
			return false;

		/* 把源蒙皮的局部关节索引映射到骨架，再按各图元的顶点重映射生成蒙皮数据。 */
		for (size_t ni = 0; ni < asset.nodes.size(); ++ni) {
			const auto& node = asset.nodes[ni];
			const auto nodeId = context.Identity("node/" + std::to_string(ni)).value;
			if (!joints.contains(nodeId) || !node.meshIndex)
				continue;
			const auto owner = joints.at(nodeId);
			const fastgltf::Skin* skin = nullptr;
			std::vector<uint32_t> skinJoints;
			std::vector<Math::Mat4> inverseBinds;
			for (const auto& joint : skeleton.joints)
				inverseBinds.push_back(joint.inverseBind * modelPose[owner]);
			if (node.skinIndex) {
				skin = &asset.skins.at(*node.skinIndex);
				if (skin->joints.empty() || skin->joints.size() > MaximumSkeletonJoints) {
					error = "glTF skin joint budget";
					return false;
				}
				std::set<size_t> unique;
				for (auto index : skin->joints) {
					const auto id = context.Identity("node/" + std::to_string(index)).value;
					if (!unique.insert(index).second || !joints.contains(id)) {
						error = "glTF skin joint is duplicated or outside the selected scene";
						return false;
					}
					skinJoints.push_back(joints.at(id));
				}
				if (skin->inverseBindMatrices) {
					const auto& accessor = asset.accessors.at(*skin->inverseBindMatrices);
					if (accessor.type != fastgltf::AccessorType::Mat4 ||
					    accessor.componentType != fastgltf::ComponentType::Float ||
					    accessor.count != skinJoints.size() || accessor.normalized) {
						error = "glTF inverse-bind accessor must be FLOAT MAT4 with one element per joint";
						return false;
					}
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
					    asset, accessor, [&](auto matrix, size_t index) {
						    Math::Mat4 value;
						    for (int c = 0; c < 4; ++c)
							    for (int r = 0; r < 4; ++r)
								    value[c][r] = matrix[c][r];
						    inverseBinds[skinJoints[index]] = value;
					    });
				} else
					for (auto joint : skinJoints)
						inverseBinds[joint] = Math::Mat4{1};
			}
			const auto mi = *node.meshIndex;
			for (size_t pi = 0; pi < asset.meshes.at(mi).primitives.size(); ++pi) {
				const auto& primitive = asset.meshes[mi].primitives[pi];
				const auto position = primitive.findAttribute("POSITION");
				const auto count = asset.accessors.at(position->accessorIndex).count;
				const auto derivedCount = remaps.at({mi, pi}).size();
				if (count > 2000000 || derivedCount > 2000000 ||
				    !context.WorkspaceFits((count + derivedCount * 3) * sizeof(SkinInfluence), error)) {
					if (error.empty())
						error = "glTF skin vertex working budget exceeded";
					return false;
				}
				std::vector<SkinInfluence> source(count);
				const auto jointAccessor = primitive.findAttribute("JOINTS_0"),
				           weightAccessor = primitive.findAttribute("WEIGHTS_0");
				if (skin) {
					if (jointAccessor == primitive.attributes.end() || weightAccessor == primitive.attributes.end()) {
						error = "Skinned primitive requires JOINTS_0 and WEIGHTS_0";
						return false;
					}
					const auto& ja = asset.accessors.at(jointAccessor->accessorIndex);
					const auto& wa = asset.accessors.at(weightAccessor->accessorIndex);
					if (ja.type != fastgltf::AccessorType::Vec4 || ja.normalized || ja.count != count ||
					    (ja.componentType != fastgltf::ComponentType::UnsignedByte &&
					        ja.componentType != fastgltf::ComponentType::UnsignedShort) ||
					    wa.type != fastgltf::AccessorType::Vec4 || wa.count != count ||
					    !((wa.componentType == fastgltf::ComponentType::Float && !wa.normalized) ||
					        (wa.normalized &&
					            (wa.componentType == fastgltf::ComponentType::UnsignedByte ||
					                wa.componentType == fastgltf::ComponentType::UnsignedShort)))) {
						error = "Invalid glTF JOINTS_0/WEIGHTS_0 accessor shape or storage";
						return false;
					}
					bool valid = true;
					fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(
					    asset, ja, [&](auto value, size_t vertex) {
						    for (size_t k = 0; k < 4; ++k) {
							    if (value[k] >= skinJoints.size())
								    valid = false;
							    else
								    source[vertex].joints[k] = skinJoints[value[k]];
						    }
					    });
					if (!valid) {
						error = "glTF joint index outside skin.joints";
						return false;
					}
					fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
					    asset, wa, [&](auto value, size_t vertex) {
						    for (size_t k = 0; k < 4; ++k)
							    source[vertex].weights[k] = value[k];
					    });
				} else {
					if (jointAccessor != primitive.attributes.end() || weightAccessor != primitive.attributes.end()) {
						error = "glTF weight attributes lack a node skin";
						return false;
					}
					for (auto& influence : source)
						influence.joints[0] = owner;
				}
				MeshSkinBinding binding;
				binding.skeleton = skeletonId;
				binding.mesh = context.Identity("mesh/" + std::to_string(mi) + "/primitive/" + std::to_string(pi));
				binding.meshToSkeleton = modelPose[owner];
				for (auto index : remaps.at({mi, pi}))
					binding.influences.push_back(source.at(index));
				binding.perJointInverseBind = inverseBinds;
				if (!EncodeMeshSkinBinding(binding, text, error))
					return false;
				const auto key = "skin/node/" + std::to_string(ni) + "/primitive/" + std::to_string(pi);
				if (!Product(context, result, key, "mesh_skin_binding", asset.meshes[mi].name, text,
				        {skeletonId, binding.mesh}, error))
					return false;
				const auto proxyId =
				    context.Identity("node/" + std::to_string(ni) + "/primitive/" + std::to_string(pi)).value;
				auto instance = std::find_if(model.meshes.begin(), model.meshes.end(),
				    [&](const auto& value) { return model.nodes[value.node].id == proxyId; });
				if (instance == model.meshes.end()) {
					error = "glTF skin lost its source node mapping";
					return false;
				}
				instance->skin.id = context.Identity(key);
			}
		}
		/* 将每段动画的通道映射到骨架关节，解码采样器的时间和值并生成动画片段。 */
		for (size_t ai = 0; ai < asset.animations.size(); ++ai) {
			const auto& animation = asset.animations[ai];
			AnimationClip clip;
			clip.skeleton = skeletonId;
			clip.duration = 0;
			std::map<StableId, AnimationTrack> tracks;
			std::set<std::pair<StableId, fastgltf::AnimationPath>> targets;
			if (animation.channels.size() > MaximumSkeletonJoints * 3) {
				error = "glTF animation channel budget exceeded";
				return false;
			}
			size_t totalKeys = 0;
			for (const auto& channel : animation.channels) {
				if (!channel.nodeIndex || channel.path == fastgltf::AnimationPath::Weights) {
					error = "glTF animation must target an explicit TRS node";
					return false;
				}
				const auto id = context.Identity("node/" + std::to_string(*channel.nodeIndex)).value;
				if (!joints.contains(id)) {
					error = "Animation targets a node outside the selected scene";
					return false;
				}
				if (!targets.emplace(id, channel.path).second) {
					error = "Duplicate glTF animation target/path";
					return false;
				}
				const auto& sampler = animation.samplers.at(channel.samplerIndex);
				const auto& input = asset.accessors.at(sampler.inputAccessor);
				const auto& output = asset.accessors.at(sampler.outputAccessor);
				if (input.count > 1000000 - totalKeys) {
					error = "glTF animation key budget exceeded";
					return false;
				}
				totalKeys += input.count;
				if (!context.WorkspaceFits(totalKeys * sizeof(RotationKeyframe) * 4, error))
					return false;
				if (input.type != fastgltf::AccessorType::Scalar ||
				    input.componentType != fastgltf::ComponentType::Float || input.normalized || input.count == 0 ||
				    input.count > 1000000 || output.componentType != fastgltf::ComponentType::Float ||
				    output.normalized) {
					error = "Invalid glTF animation accessor";
					return false;
				}
				std::vector<float> times(input.count);
				fastgltf::copyFromAccessor<float>(asset, input, times.data());
				const auto interpolation = Mode(sampler.interpolation);
				const bool cubic = interpolation == AnimationInterpolation::CubicSpline;
				const size_t stride = cubic ? 3 : 1;
				if (output.count != times.size() * stride) {
					error = "glTF animation output count differs from interpolation requirements";
					return false;
				}
				for (float time : times)
					clip.duration = std::max(clip.duration, double(time));
				auto& track = tracks[id];
				track.joint = id;
				if (channel.path == fastgltf::AnimationPath::Rotation) {
					if (output.type != fastgltf::AccessorType::Vec4) {
						error = "Rotation animation requires VEC4";
						return false;
					}
					std::vector<fastgltf::math::fvec4> values(output.count);
					fastgltf::copyFromAccessor<fastgltf::math::fvec4>(asset, output, values.data());
					auto quaternion = [](const auto& v) { return Math::Quat{v[3], v[0], v[1], v[2]}; };
					track.rotation.interpolation = interpolation;
					for (size_t k = 0; k < times.size(); ++k) {
						RotationKeyframe key;
						key.time = times[k];
						key.value = quaternion(values[k * stride + (cubic ? 1 : 0)]);
						if (cubic) {
							key.inTangent = quaternion(values[k * stride]);
							key.outTangent = quaternion(values[k * stride + 2]);
						}
						track.rotation.keys.push_back(key);
					}
				} else {
					if (output.type != fastgltf::AccessorType::Vec3) {
						error = "Translation/scale animation requires VEC3";
						return false;
					}
					std::vector<fastgltf::math::fvec3> values(output.count);
					fastgltf::copyFromAccessor<fastgltf::math::fvec3>(asset, output, values.data());
					auto vector = [](const auto& v) { return Math::Vec3{v[0], v[1], v[2]}; };
					auto& destination =
					    channel.path == fastgltf::AnimationPath::Translation ? track.translation : track.scale;
					destination.interpolation = interpolation;
					for (size_t k = 0; k < times.size(); ++k) {
						VectorKeyframe key;
						key.time = times[k];
						key.value = vector(values[k * stride + (cubic ? 1 : 0)]);
						if (cubic) {
							key.inTangent = vector(values[k * stride]);
							key.outTangent = vector(values[k * stride + 2]);
						}
						destination.keys.push_back(key);
					}
				}
			}
			for (auto& [id, track] : tracks)
				clip.tracks.push_back(std::move(track));
			if (!ValidateAnimationClip(clip, &skeleton, error) || !EncodeAnimationClip(clip, text, error))
				return false;
			const auto key = "animation/" + std::to_string(ai);
			if (!Product(context, result, key, "animation_clip", animation.name, text, {skeletonId}, error))
				return false;
			model.animations.emplace_back(context.Identity(key));
		}
		context.Diagnose(AssetDiagnosticLevel::Info, "animation",
		    "glTF skin/joint indices and STEP/LINEAR/CUBICSPLINE accessors retained; derived "
		    "weights use exact original vertex remaps");
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = "glTF animation import: " + std::string(exception.what());
		return false;
	}
} /* 命名空间 Catalpa::Tools */
