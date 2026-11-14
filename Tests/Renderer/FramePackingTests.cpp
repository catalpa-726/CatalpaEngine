#include "TestRunner.h"
#include "Renderer/RendererFrameContext.h"
#include "RHI/RHITextureDesc.h"
#include "Camera/CameraProjection.h"
#include "Renderer/Passes/DepthPrepass.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Catalpa::Tests {
	void RunFramePackingTests(TestContext& test) {
		const std::string valid =
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[0.02,0.03,0.04,1]})";
		RendererProfile profile;
		test.Check(ParseRendererProfile(valid, profile).IsSuccess() && profile.normalDebug && profile.depthPrepass &&
		        profile.depthFormat == PixelFormat::D32_Float,
		    "Renderer profile parses explicitly selected depth/debug behavior");
		const auto savedProfile = profile;
		const auto rejectsProfile = [&](std::string_view json, std::string_view requirement) {
			test.Check(!ParseRendererProfile(json, profile).IsSuccess(), requirement);
			test.Check(profile.depthFormat == savedProfile.depthFormat &&
			        profile.depthPrepass == savedProfile.depthPrepass &&
			        profile.normalDebug == savedProfile.normalDebug && profile.clearColor == savedProfile.clearColor,
			    "Invalid renderer configuration preserves all prior profile fields");
		};
		rejectsProfile("{}", "Missing renderer fields are rejected rather than silently defaulted");
		rejectsProfile(
		    R"({"schemaVersion":2,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[0,0,0,1]})",
		    "Unknown renderer schema version is rejected");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"RGBA8_UNorm","depthPrepass":true,"normalDebug":true,"clearColor":[0,0,0,1]})",
		    "Color format is not silently accepted as the depth requirement");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[0,0,0,1],"other":1})",
		    "Unknown configuration fields are rejected");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"normalDebug":false,"clearColor":[0,0,0,1]})",
		    "Duplicate renderer configuration keys are rejected");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[0,0,0,2]})",
		    "Alpha outside the declared range is rejected without clamping");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[-1,0,0,1]})",
		    "Negative linear color is rejected");
		rejectsProfile(
		    R"({"schemaVersion":1,"profile":"default","depthFormat":"D32_Float","depthPrepass":true,"normalDebug":true,"clearColor":[1e999,0,0,1]})",
		    "Unrepresentable renderer color cannot enter the GPU data");

		/* 此夹具只检验独立packing层；Scene身份/几何契约由GeometrySnapshotTests另外验证。 */
		RenderFramePacket packet;
		packet.time.simulationSeconds = 12.5;
		packet.time.realDeltaSeconds = 0.25;
		packet.time.fixedDeltaSeconds = 0.125;
		packet.time.interpolationAlpha = 0.75;
		RenderView view;
		view.viewport.width = 640;
		view.viewport.height = 480;
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				view.matrices.viewProjection[column][row] = static_cast<float>(column * 10 + row + 1);
		view.matrices.position = {2, 3, 5};
		view.matrices.inverseViewProjection[2][3] = 7;
		view.matrices.view[1][2] = -8;
		packet.views.push_back(view);
		RenderMeshInstance first;
		first.worldMatrix[0][0] = 2;
		first.worldMatrix[1][0] = 1;
		first.worldMatrix[1][1] = 3;
		first.worldMatrix[2][2] = 4;
		first.worldMatrix[3] = {3, -2, 5, 1};
		first.normalMatrix = Math::Mat3{0};
		first.normalMatrix[0][0] = 0.5f;
		first.normalMatrix[0][1] = -1.0f / 6.0f;
		first.normalMatrix[1][1] = 1.0f / 3.0f;
		first.normalMatrix[2][2] = 0.25f;
		first.color = {2.0f, 0.25f, 0.125f, 0.5f};
		packet.instances.push_back(first);
		RenderMeshInstance second;
		second.worldMatrix[3].y = -7;
		second.color = {1, 0, 0, 1};
		packet.instances.push_back(second);
		PackedFrameUniforms packed;
		test.Check(BuildFrameUniformData(packet, profile, 256, 256, packed).IsSuccess(),
		    "A buffer larger than maxUniformBufferRange is valid when each descriptor range fits");
		if (packed.data.empty() || packed.drawOffsets.size() != 2)
			return;
		test.Check(packed.viewOffsets == std::vector<uint64_t>{256} && packed.drawOffsets[0] == 512 &&
		        packed.drawOffsets[1] == 768 && packed.data.size() == 912,
		    "Frame/View/Draw use independently aligned offsets and exact last range end");
		const auto read = [&](uint64_t offset) {
			float value;
			std::memcpy(&value, packed.data.data() + offset, sizeof(value));
			return value;
		};
		test.Check(read(0) == 12.5f && read(4) == 0.25f && read(8) == 0.125f && read(12) == 0.75f,
		    "Frame time bytes preserve explicit seconds and interpolation meaning");
		test.Check(read(16) == 1.0f && read(20) == 0.1f && read(24) == 0.0f && read(28) == 0.0f,
		    "Normal diagnostics use their own options vector without overwriting time or alpha");
		test.Check(read(256) == 1.0f && read(260) == 2.0f && read(272) == 11.0f && read(316) == 34.0f,
		    "View matrix is uploaded as four columns rather than a transposed row sequence");
		test.Check(read(320) == 2 && read(324) == 3 && read(328) == 5 && read(332) == 1,
		    "Camera position has explicit homogeneous padding");
		test.Check(read(380) == 7 && read(424) == -8,
		    "Extended View inverse VP and camera view preserve independent column-major coefficients");
		test.Check(
		    read(512) == 2 && read(528) == 1 && read(532) == 3 && read(560) == 3 && read(564) == -2 && read(568) == 5,
		    "Draw world columns preserve asymmetric shear and translation coefficients");
		test.Check(read(576) == 0.5f && read(580) == -1.0f / 6.0f && read(588) == 0 && read(596) == 1.0f / 3.0f &&
		        read(616) == 0.25f && read(620) == 0,
		    "Normal columns preserve analytic inverse-transpose and zero fourth components");
		test.Check(read(624) == 2 && read(628) == 0.25f && read(636) == 0.5f && read(820) == -7 && read(880) == 1 &&
		        read(892) == 1,
		    "Separate Draw ranges preserve HDR color/alpha and independent instance translation");
		bool zeroPadding = true;
		for (size_t byte = 32; byte < 256; ++byte)
			zeroPadding = zeroPadding && packed.data[byte] == std::byte{0};
		for (size_t byte = 464; byte < 512; ++byte)
			zeroPadding = zeroPadding && packed.data[byte] == std::byte{0};
		for (size_t byte = 656; byte < 768; ++byte)
			zeroPadding = zeroPadding && packed.data[byte] == std::byte{0};
		test.Check(zeroPadding, "Alignment gaps are initialized rather than exposing stale frame memory");
		uint32_t firstToken = 0, secondToken = 0;
		std::memcpy(&firstToken, packed.data.data() + 640, sizeof(firstToken));
		std::memcpy(&secondToken, packed.data.data() + 896, sizeof(secondToken));
		test.Check(firstToken == 1 && secondToken == 2,
		    "Picking tokens are one-based integer bytes rather than truncated entity IDs");
		auto mirrored = packet;
		mirrored.instances[1].worldMatrix[0][0] = -1;
		PackedFrameUniforms mirrorPacking;
		test.Check(BuildFrameUniformData(mirrored, profile, 256, 256, mirrorPacking).IsSuccess(),
		    "Mirrored world transform retains the same stable Draw layout");
		uint32_t mirrorBit = 0;
		if (mirrorPacking.data.size() >= 904)
			std::memcpy(&mirrorBit, mirrorPacking.data.data() + 900, sizeof(mirrorBit));
		test.Check(mirrorBit == 1,
		    "CPU double determinant produces the exact handedness bit shared by TBN and front-face selection");
		auto longTime = packet;
		longTime.time.simulationSeconds = 4096.0 * 1048576.0 + 0.25;
		PackedFrameUniforms wrapped;
		test.Check(BuildFrameUniformData(longTime, profile, 256, 256, wrapped).IsSuccess(),
		    "Long-running simulation time packs without losing the wrapped subsecond material phase");
		FrameGpuParameters wrappedTime;
		if (wrapped.data.size() >= sizeof(wrappedTime))
			std::memcpy(&wrappedTime, wrapped.data.data(), sizeof(wrappedTime));
		test.Check(wrappedTime.time[0] == 0.25f && wrappedTime.options[2] == 1048576,
		    "Time ABI records an exact bounded phase plus separate 4096-second epoch");
		float reconstructed[2]{};
		bool crossingPacked = true;
		for (size_t i = 0; i < 2; ++i) {
			auto crossing = packet;
			crossing.time.simulationSeconds = i ? 4096.25 : 4095.75;
			PackedFrameUniforms values;
			crossingPacked &= BuildFrameUniformData(crossing, profile, 256, 256, values).IsSuccess();
			FrameGpuParameters frame{};
			if (values.data.size() >= sizeof(frame))
				std::memcpy(&frame, values.data.data(), sizeof(frame));
			reconstructed[i] = frame.options[2] * 4096.0f + frame.time[0];
		}
		test.Check(crossingPacked && reconstructed[1] - reconstructed[0] == 0.5f,
		    "Graph Time remains continuous across the first epoch boundary");
		auto maximumTime = packet;
		maximumTime.time.simulationSeconds = std::numeric_limits<float>::max();
		PackedFrameUniforms maximumPacking;
		test.Check(BuildFrameUniformData(maximumTime, profile, 256, 256, maximumPacking).IsSuccess(),
		    "The largest finite float time does not overflow when epoch and phase are reconstructed");
		const auto accepted = packed;
		const auto rejectsPacking = [&](const RenderFramePacket& input, uint64_t alignment, uint64_t range,
		                                std::string_view requirement) {
			test.Check(!BuildFrameUniformData(input, profile, alignment, range, packed).IsSuccess(), requirement);
			test.Check(packed.data == accepted.data && packed.viewOffsets == accepted.viewOffsets &&
			        packed.drawOffsets == accepted.drawOffsets,
			    "Failed packing preserves the complete previous byte packet");
		};
		rejectsPacking(packet, 0, 256, "Zero UBO alignment is rejected");
		rejectsPacking(packet, 3, 256, "Non-power-of-two UBO alignment is rejected");
		rejectsPacking(packet, 256, 127, "An insufficient per-binding uniform range is rejected");
		rejectsPacking(packet, 256, 207, "Extended View range must fit even when the smaller Draw range fits");
		rejectsPacking(
		    packet, uint64_t{1} << 63, 256, "Alignment/offset arithmetic cannot wrap into a small accepted allocation");
		auto bad = packet;
		bad.views.clear();
		rejectsPacking(bad, 256, 256, "Packing requires the selected view rather than substituting an identity camera");
		bad = packet;
		bad.time.simulationSeconds =
		    std::nextafter(double(std::numeric_limits<float>::max()), std::numeric_limits<double>::infinity());
		rejectsPacking(
		    bad, 256, 256, "Time beyond the graph float domain is rejected even when its separate epoch is finite");
		bad = packet;
		bad.views.push_back(view);
		rejectsPacking(bad, 256, 256, "Multiple views cannot share a duplicate output identity");
		bad = packet;
		bad.views[0].viewport.width = 0;
		rejectsPacking(bad, 256, 256, "A zero pixel target is not packed as a drawable view");
		bad = packet;
		bad.time.simulationSeconds = std::numeric_limits<double>::max();
		rejectsPacking(bad, 256, 256, "Unrepresentable simulation seconds do not become float infinity");
		bad = packet;
		bad.time.interpolationAlpha = 1.1;
		rejectsPacking(bad, 256, 256, "Out-of-range interpolation is rejected without clamping");
		bad = packet;
		bad.instances[0].normalMatrix[1][1] = std::numeric_limits<float>::quiet_NaN();
		rejectsPacking(bad, 256, 256, "Non-finite normal data cannot enter an upload packet");
		bad = packet;
		bad.instances[1].color.w = -1;
		rejectsPacking(bad, 256, 256, "Invalid display alpha preserves the previously packed frame");
		RenderView authored;
		authored.viewport.width = 640;
		authored.viewport.height = 480;
		authored.lens.verticalFovRadians = Math::Pi * 0.5f;
		test.Check(BuildCameraProjection(authored.lens, authored.viewport, authored.matrices.projection,
		               authored.matrices.inverseProjection)
		               .IsSuccess(),
		    "Presentation fixture starts from a valid authored projection");
		authored.matrices.viewProjection = authored.matrices.projection;
		authored.matrices.inverseViewProjection = authored.matrices.inverseProjection;
		RenderView presentation;
		test.Check(BuildPresentationView(authored, {1280, 720}, presentation).IsSuccess() &&
		        presentation.viewport.width == 1280 && presentation.viewport.height == 720,
		    "Actual swapchain dimensions produce an independent presentation view");
		test.Check(std::abs(presentation.matrices.projection[0][0] - 0.5625f) < 1.0e-5f &&
		        std::abs(presentation.matrices.projection[1][1] + 1.0f) < 1.0e-5f,
		    "Presentation aspect comes from 1280x720 and projection flips Y exactly once");
		test.Check(authored.viewport.width == 640 && authored.viewport.height == 480 &&
		        std::abs(authored.matrices.projection[0][0] - 0.75f) < 1.0e-5f,
		    "Presentation resize preserves the source view dimensions and authored matrices");
		const auto retainedPresentation = presentation;
		test.Check(!BuildPresentationView(authored, {0, 720}, presentation).IsSuccess() &&
		        presentation.viewport.width == retainedPresentation.viewport.width &&
		        presentation.matrices.projection[0][0] == retainedPresentation.matrices.projection[0][0],
		    "Zero presentation extent rejects without replacing the previous view");
		auto invalidView = authored;
		invalidView.matrices.view[0][0] = 0.0f;
		test.Check(!BuildPresentationView(invalidView, {1280, 720}, presentation).IsSuccess() &&
		        presentation.viewport.width == retainedPresentation.viewport.width,
		    "Inconsistent camera view/inverse cannot silently enter a new presentation projection");
		/* 用等价的 CPU 列向量公式检查极值方向。期望值由独立的 (2,1,0)/sqrt(5) 推导，测试范围为 CPU 数学计算。 */
		const auto shadingDirection = [](float matrixScale, float localScale) {
			const Math::Vec3 source{localScale, localScale, 0};
			const float normalScale = std::max(std::abs(source.x), std::max(std::abs(source.y), std::abs(source.z)));
			const auto reduced = source / normalScale;
			const auto local = reduced / glm::length(reduced);
			const Math::Vec3 c0{matrixScale, 0, 0}, c1{0, matrixScale * 0.5f, 0}, c2{0, 0, -matrixScale * 0.25f};
			const float scale = std::max(std::abs(c0.x), std::max(std::abs(c1.y), std::abs(c2.z)));
			const auto transformed = (c0 / scale) * local.x + (c1 / scale) * local.y + (c2 / scale) * local.z;
			return transformed / glm::length(transformed);
		};
		const auto hugeDirection = shadingDirection(1.0e38f, 1.0e38f);
		const auto tinyDirection = shadingDirection(1.0e-38f, 1.0e-38f);
		const float inverseRootFive = 1.0f / std::sqrt(5.0f);
		test.Check(std::isfinite(hugeDirection.x) && std::abs(hugeDirection.x - 2 * inverseRootFive) < 1.0e-5f &&
		        std::abs(hugeDirection.y - inverseRootFive) < 1.0e-5f && hugeDirection.z == 0,
		    "Finite huge local normals and normal matrices preserve direction without intermediate infinity");
		test.Check(std::isfinite(tinyDirection.x) && std::abs(tinyDirection.x - 2 * inverseRootFive) < 1.0e-5f &&
		        std::abs(tinyDirection.y - inverseRootFive) < 1.0e-5f && tinyDirection.z == 0,
		    "Finite tiny local normals and matrices preserve direction instead of underflowing to zero");
		/*
		 * 此处的 SPIR-V 指令字流用于验证派生产物的处理过程；完整验证由工具链执行。
		 * 固定编号来自 Khronos 语法：EntryPoint 15、Variable 59、Decorate 71、MemberDecorate 72、BuiltIn 11、Position 0、Invariant 18。
		 */
		const auto vertexWords = [](bool member) {
			std::vector<uint32_t> words{0x07230203u, 0x00010500u, 0, 12, 0};
			const auto append = [&](uint16_t opcode, std::initializer_list<uint32_t> values) {
				words.push_back((static_cast<uint32_t>(values.size() + 1) << 16) | opcode);
				words.insert(words.end(), values.begin(), values.end());
			};
			append(17, {1}); /* 声明 Shader 能力。 */
			append(14, {0, 1}); /* 采用 Logical 寻址模型和 GLSL450 内存模型。 */
			append(15, {0, 10, 0x6e69616du, 0, member ? 7u : 6u}); /* 顶点入口为编号 10 的 main，同时列出输出接口。 */
			if (member) {
				append(71, {4, 2});
				append(72, {4, 0, 11, 0});
			} /* 声明 Block 装饰及成员的内置 Position 语义。 */
			else
				append(71, {6, 11, 0});
			append(19, {1}); /* 声明 void 类型。 */
			append(22, {2, 32}); /* 声明 32 位浮点类型。 */
			append(23, {3, 2, 4}); /* 声明四分量浮点向量。 */
			if (member) {
				append(30, {4, 3});
				append(32, {5, 3, 4});
				append(33, {6, 1});
				append(59, {5, 7, 3});
				append(1, {4, 8});
				append(54, {1, 10, 0, 6});
				append(248, {11});
				append(62, {7, 8});
			} else {
				append(32, {4, 3, 3});
				append(33, {5, 1});
				append(59, {4, 6, 3});
				append(1, {3, 7});
				append(54, {1, 10, 0, 5});
				append(248, {11});
				append(62, {6, 7});
			}
			append(253, {});
			append(56, {});
			return words;
		};
		const auto findInstruction = [](const std::vector<uint32_t>& words, uint16_t opcode) {
			for (size_t offset = 5; offset < words.size();) {
				const uint32_t count = words[offset] >> 16;
				if (count == 0 || count > words.size() - offset)
					return words.size();
				if ((words[offset] & 0xffffu) == opcode)
					return offset;
				offset += count;
			}
			return words.size();
		};
		for (bool member : {false, true}) {
			const auto source = vertexWords(member);
			const auto originalWords = source;
			std::vector<uint32_t> invariant{0xdeadbeefu};
			test.Check(DepthPrepass::PrepareInvariantVertexShader(source, invariant).IsSuccess(),
			    member ? "Block Output Position member gains an invariant derived copy"
			           : "Direct Output Position gains an invariant derived copy");
			const size_t extra = member ? 4 : 3;
			if (invariant.size() != source.size() + extra) {
				test.Check(false, "Invariant preparation appends exactly the required annotation words");
				continue;
			}
			size_t count = 0, addedOffset = invariant.size();
			for (size_t offset = 5; offset < invariant.size();) {
				const uint32_t length = invariant[offset] >> 16;
				const uint32_t opcode = invariant[offset] & 0xffffu;
				if (length == 0 || length > invariant.size() - offset) {
					test.Check(false, "Prepared wordstream keeps complete instruction boundaries");
					break;
				}
				if ((!member && opcode == 71 && length == 3 && invariant[offset + 1] == 6 &&
				        invariant[offset + 2] == 18) ||
				    (member && opcode == 72 && length == 4 && invariant[offset + 1] == 4 &&
				        invariant[offset + 2] == 0 && invariant[offset + 3] == 18)) {
					++count;
					addedOffset = offset;
				}
				offset += length;
			}
			test.Check(count == 1 && addedOffset < findInstruction(invariant, 19),
			    "Invariant uses the actual target and appears in annotations before types");
			if (count != 1 || addedOffset == invariant.size())
				continue;
			auto withoutAdded = invariant;
			withoutAdded.erase(withoutAdded.begin() + static_cast<std::ptrdiff_t>(addedOffset),
			    withoutAdded.begin() + static_cast<std::ptrdiff_t>(addedOffset + extra));
			test.Check(source == originalWords && withoutAdded == source && invariant[3] == source[3],
			    "Invariant preparation preserves source/header bound/every original instruction without inventing IDs or arithmetic");
			const auto once = invariant;
			test.Check(
			    DepthPrepass::PrepareInvariantVertexShader(invariant, invariant).IsSuccess() && invariant == once,
			    "Invariant preparation is idempotent and supports input/output aliasing");
		}
		const auto goodWords = vertexWords(false);
		std::vector<uint32_t> previousWords{1, 2, 3};
		const auto rejectsWords = [&](const std::vector<uint32_t>& words, std::string_view requirement) {
			test.Check(!DepthPrepass::PrepareInvariantVertexShader(words, previousWords).IsSuccess(), requirement);
			test.Check(previousWords == std::vector<uint32_t>({1, 2, 3}),
			    "Rejected vertex wordstream preserves the previous output");
		};
		auto badWords = goodWords;
		badWords[0] = 0;
		rejectsWords(badWords, "Non-SPIR-V headers cannot be scanned as instructions");
		badWords = goodWords;
		badWords[5] &= 0xffffu;
		rejectsWords(badWords, "Zero instruction word count is rejected instead of looping");
		badWords = goodWords;
		badWords.back() = (20u << 16) | 56u;
		rejectsWords(badWords, "An instruction extending past the source boundary is rejected");
		badWords = goodWords;
		badWords[findInstruction(badWords, 15) + 1] = 4;
		rejectsWords(badWords, "A fragment module is not silently prepared as a vertex module");
		badWords = goodWords;
		const auto positionAt = findInstruction(badWords, 71);
		badWords.erase(badWords.begin() + positionAt, badWords.begin() + positionAt + 4);
		rejectsWords(badWords, "Missing BuiltIn Position is rejected");
		badWords = goodWords;
		badWords[findInstruction(badWords, 59) + 3] = 1;
		rejectsWords(badWords, "A Position decoration on non-Output storage is rejected");
		badWords = goodWords;
		badWords[findInstruction(badWords, 15) + 5] = 7;
		rejectsWords(badWords, "Position must belong to the selected vertex entry interface");
		badWords = goodWords;
		badWords[findInstruction(badWords, 71) + 1] = badWords[3];
		rejectsWords(badWords, "Out-of-bound Position IDs are rejected");
		badWords = vertexWords(true);
		const auto blockAt = findInstruction(badWords, 71);
		badWords.erase(badWords.begin() + blockAt, badWords.begin() + blockAt + 3);
		rejectsWords(badWords, "A member Position requires a Block-decorated actual Output structure");
		badWords = vertexWords(true);
		badWords[findInstruction(badWords, 72) + 2] = 1;
		rejectsWords(badWords, "Position member index must name a real structure member");
		badWords = goodWords;
		const auto annotationAt = findInstruction(badWords, 71);
		badWords.insert(badWords.begin() + annotationAt, {(4u << 16) | 71u, 6, 11, 0});
		rejectsWords(badWords, "Duplicate Position candidates are rejected as ambiguous");
		badWords = goodWords;
		badWords.insert(badWords.end(), {(3u << 16) | 71u, 6, 18});
		rejectsWords(badWords, "Late annotations after the type/function sections are rejected");
		badWords = goodWords;
		const auto typeStart = findInstruction(badWords, 19);
		/* literal值看起来像OpDecorate，但属于OpConstant的operand，不能被当作指令误扫描。 */
		badWords.insert(
		    badWords.begin() + typeStart, {(4u << 16) | 21u, 8, 32, 0, (4u << 16) | 43u, 8, 9, (3u << 16) | 71u});
		std::vector<uint32_t> literalPrepared;
		test.Check(DepthPrepass::PrepareInvariantVertexShader(badWords, literalPrepared).IsSuccess() &&
		        literalPrepared.size() == badWords.size() + 3,
		    "Opcode-looking literal words are not mistaken for annotation instructions");

		uint64_t bytes = 0;
		test.Check(!TryGetTextureUploadByteSize({4, 4}, PixelFormat::D32_Float, bytes),
		    "A four-byte depth format is not accepted by the color upload contract");
	}

	void RunMultiViewPackingTests(TestContext& test) {
		RenderFramePacket packet;
		RenderView source;
		source.id = 10;
		source.lens.verticalFovRadians = Math::Pi * 0.5f;
		RenderView wide, tall;
		test.Check(BuildPresentationView(source, {640, 480}, wide).IsSuccess(),
		    "Wide view builds its own presentation aspect");
		source.id = 20;
		source.lens.reversedZ = true;
		source.matrices.view[3].x = -7;
		source.matrices.inverseView[3].x = 7;
		source.matrices.position.x = 7;
		test.Check(BuildPresentationView(source, {320, 640}, tall).IsSuccess(),
		    "Tall reverse-Z view builds an independent camera range");
		packet.views = {wide, tall};
		packet.instances.resize(2);
		packet.instances[1].worldMatrix[3].x = 9;
		PackedFrameUniforms packed;
		test.Check(BuildFrameUniformData(packet, {}, 256, 256, packed).IsSuccess(),
		    "Two views pack without dropping either camera");
		if (packed.viewOffsets.size() != 2 || packed.drawOffsets.size() != 2)
			return;
		test.Check(packed.viewOffsets == std::vector<uint64_t>{256, 512} &&
		        packed.drawOffsets == std::vector<uint64_t>{768, 1024} && packed.data.size() == 1168,
		    "Two independent View records precede one shared pair of Draw records");
		const auto read = [&](uint64_t offset) {
			float value = 0;
			std::memcpy(&value, packed.data.data() + offset, sizeof(value));
			return value;
		};
		test.Check(std::abs(read(256) - 0.75f) < 1.0e-5f && std::abs(read(512) - 2.0f) < 1.0e-5f && read(576) == 7 &&
		        read(320) == 0 && read(1072) == 9,
		    "Each view preserves its aspect and position while instances retain shared independent transforms");
		bool zeroPadding = true;
		for (size_t i = 464; i < 512; ++i)
			zeroPadding = zeroPadding && packed.data[i] == std::byte{0};
		for (size_t i = 720; i < 768; ++i)
			zeroPadding = zeroPadding && packed.data[i] == std::byte{0};
		test.Check(zeroPadding, "All gaps between multiple View records and Draw data are cleared");
		const auto accepted = packed;
		auto invalid = packet;
		invalid.views[1].matrices.viewProjection[2][1] = std::numeric_limits<float>::quiet_NaN();
		test.Check(!BuildFrameUniformData(invalid, {}, 256, 256, packed).IsSuccess() && packed.data == accepted.data &&
		        packed.viewOffsets == accepted.viewOffsets && packed.drawOffsets == accepted.drawOffsets,
		    "Invalid second view preserves all previously packed cameras and instance bytes");
		test.Check(!BuildFrameUniformData(packet, {}, uint64_t{1} << 63, 256, packed).IsSuccess() &&
		        packed.data == accepted.data,
		    "Multiple view offset arithmetic rejects uint64 overflow");
		packet.instances.clear();
		test.Check(BuildFrameUniformData(packet, {}, 256, 256, packed).IsSuccess() && packed.viewOffsets.size() == 2 &&
		        packed.drawOffsets.empty() && packed.data.size() == 720,
		    "Empty geometry still preserves both cameras without inventing Draw records");
	}
}
