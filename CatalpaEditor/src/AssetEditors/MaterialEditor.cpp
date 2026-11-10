#include "AssetEditors/MaterialEditor.h"
#include "Commands/AssetCommands.h"
#include "Core/Application.h"
#include "Core/Id/StableIdGenerator.h"
#include "Geometry/PrimitiveMesh.h"
#include "Scene/SceneManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSubtree.h"
#include "Renderer/RenderSurface.h"
#include "imgui.h"
#include "Panels/EditorText.h"

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t MaterialSurface = 0x4341544d41544c01ULL;

        bool EditValue(MaterialValue& value) {
            const char* label = "##Value";
            const auto flags = ImGuiInputTextFlags_EnterReturnsTrue;
            return std::visit(
                [&](auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                        return ImGui::InputFloat(label, &v, 0, 0, "%.6g", flags);
                    else if constexpr (std::is_same_v<T, bool>)
                        return ImGui::Checkbox(label, &v);
                    else if constexpr (std::is_same_v<T, int32_t>)
                        return ImGui::InputScalar(label, ImGuiDataType_S32, &v, nullptr, nullptr, nullptr,
                                                  flags);
                    else if constexpr (std::is_same_v<T, uint32_t>)
                        return ImGui::InputScalar(label, ImGuiDataType_U32, &v, nullptr, nullptr, nullptr,
                                                  flags);
                    else if constexpr (std::is_same_v<T, AssetHandle<ImageAsset>>) {
                        std::array<char, 48> buffer{};
                        const std::string current = v.HasValue() ? v.id.ToString() : std::string{};
                        std::copy(current.begin(), current.end(), buffer.begin());
                        if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags))
                            return false;
                        if (buffer[0] == 0) {
                            v = {};
                            return true;
                        }
                        if (!AssetId::TryParse(buffer.data(), v.id))
                            throw std::invalid_argument("Invalid texture asset ID");
                        return true;
                    } else
                        return ImGui::InputScalarN(label, ImGuiDataType_Float, v.data(), int(v.size()),
                                                   nullptr, nullptr, "%.6g", flags);
                },
                value);
        }
    }

    void MaterialEditor::Close(Application& app) {
        app.CloseEditorPreview(MaterialSurface);
        if (app.GetScenes().GetScene(m_Scene))
            (void)app.GetScenes().Unload(m_Scene);
        m_Scene = {};
        m_Document = {};
        m_MaterialKey = {};
        m_Object = {};
        m_Bound.reset();
        m_Renderer = {};
        m_Version = 0;
    }

    bool MaterialEditor::UpdateScene(Application& app, AssetDocument& doc, std::string& error) {
        auto& scenes = app.GetScenes();
        auto check = [&](WorldStatus status) {
            if (!status.IsSuccess())
                error = status.message;
            return status.IsSuccess();
        };
        if (m_Document != doc.Id())
            Close(app);
        if (!m_Scene.HasValue()) {
            if (!check(scenes.CreateScene("Material preview", m_Scene)))
                return false;
            m_Document = doc.Id();
            auto* scene = scenes.GetScene(m_Scene);
            PersistentEntityId camera, light;
            StableId meshId;
            if (!scene || !GeneratePersistentEntityId(m_Object, error) ||
                !GeneratePersistentEntityId(camera, error) || !GeneratePersistentEntityId(light, error) ||
                !GenerateStableId(meshId, error) || !GenerateStableId(m_MaterialKey, error))
                return false;
            Math::TransformTRS pose;
            pose.translation = {0, 0, 2.5f};
            Math::TransformTRS objectPose;
            objectPose.rotation = Math::Quat(Math::Vec3(-.15f, .4f, 0));
            Math::TransformTRS lightPose;
            lightPose.rotation = Math::Quat(Math::Vec3(-.52f, -.55f, 0.f));
            if (!check(scene->QueueCreate(camera, "Preview camera", pose)) ||
                !check(scene->QueueCreate(light, "Preview light", lightPose)) ||
                !check(scene->QueueCreate(m_Object, "Preview mesh", objectPose)) ||
                !check(scene->CommitPending()))
                return false;
            CameraComponent lens;
            lens.lens.nearPlane = 0.5f;
            lens.lens.farPlane = 5.0f;
            LightComponent sun;
            sun.parameters.intensity = 4.0f;
            if (!check(scene->QueueSetCamera(scene->Resolve(camera), lens)) ||
                !check(scene->QueueSetLight(scene->Resolve(light), sun)) || !check(scene->CommitPending()))
                return false;
            MeshData data;
            auto generated = m_Sphere ? CreateSphereMeshWithTangents(.82f, data) : CreateCubeMeshWithTangents(1, data);
            if (generated.IsSuccess())
                generated = CreateMeshResource(data, m_Renderer.mesh);
            if (!generated.IsSuccess()) {
                error = generated.message;
                return false;
            }
            auto geometry = std::make_shared<MeshAsset>();
            geometry->materialSlots = {"preview"};
            geometry->mesh = m_Renderer.mesh;
            if (!AssetVersionRef<MeshAsset>::TryCreate(AssetId{meshId}, 1, geometry, m_Renderer.meshVersion,
                                                       error))
                return false;
        }
        if (m_Bound != doc.Material()) {
            auto* scene = scenes.GetScene(m_Scene);
            if (!scene || m_Version == UINT64_MAX) {
                error = "Preview scene expired";
                return false;
            }
            AssetVersionRef<MaterialInstance> fixed;
            if (!AssetVersionRef<MaterialInstance>::TryCreate(AssetId{m_MaterialKey}, m_Version + 1,
                                                              doc.Material(), fixed, error))
                return false;
            m_Renderer.materials = {fixed};
            if (!check(scene->QueueSetMeshRenderer(scene->Resolve(m_Object), m_Renderer)) ||
                !check(scene->CommitPending()))
                return false;
            ++m_Version;
            m_Bound = doc.Material();
        }
        return true;
    }

    void MaterialEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text,
                              std::string& error) {
        const auto draft = doc.Material();
        if (ImGui::BeginTable("MaterialParameters", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
          ImGui::TableSetupColumn("##Name", ImGuiTableColumnFlags_WidthStretch, .4f);
          ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch, .6f);
          for (const auto& parameter : draft->definition.Get()->parameters) {
            const auto* old = FindMaterialParameter(*draft, parameter.id);
            if (!old)
                continue;
            std::string key = "material.parameter." + parameter.id;
            for (char& c : key) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            const auto& fallback = parameter.displayName.empty() ? parameter.id : parameter.displayName;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(UiText(key.c_str(), fallback.c_str()));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            // 源采样状态不可由材质草稿覆盖；显示其含义，不把内部 bit 协议当作数值控件。
            if (parameter.id.ends_with("_sampler")) {
                if (const auto* source = std::get_if<uint32_t>(old)) {
                    const char* address[]{UiText("sampler.repeat","Repeat"),UiText("sampler.clamp","Clamp"),
                                          UiText("sampler.mirror","Mirror"),UiText("sampler.invalid","Invalid")};
                    const auto* linear = UiText("sampler.linear","Linear");
                    const auto* nearest = UiText("sampler.nearest","Nearest");
                    ImGui::TextWrapped(UiText("sampler.settings", "U %s / V %s, min %s / mag %s / mip %s"),
                        address[*source & 3u], address[(*source >> 2) & 3u],
                        (*source & 16u) ? linear : nearest, (*source & 32u) ? linear : nearest,
                        !(*source & 128u) ? UiText("sampler.off","Off") : (*source & 64u) ? linear : nearest);
                }
                continue;
            }
            auto next = *old;
            ImGui::PushID(parameter.id.c_str());
            ImGui::BeginDisabled(!parameter.allowOverride);
            try {
                if (EditValue(next)) {
                    MaterialParameterOverride edit{parameter.id, std::move(next)};
                    ChangeMaterial(doc, std::span{&edit, 1}, {}, error, &app.GetAssets());
                }
            } catch (const std::invalid_argument& exception) {
                error = exception.what();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
          ImGui::EndTable();
        }
        const auto allowed = draft->definition.Get()->allowedStateOverrides;
        auto permits = [allowed](MaterialStateOverride key) {
            return (allowed & static_cast<uint32_t>(key)) != 0;
        };
        MaterialStateOverrides states;
        int cull = static_cast<int>(draft->renderState.cull),
            blend = static_cast<int>(draft->renderState.blend);
        bool depthTest = draft->renderState.depthTest, depthWrite = draft->renderState.depthWrite;
        const char* culls[]{UiText("material.cull_none","None"), UiText("material.cull_back","Back"),
                             UiText("material.cull_front","Front")};
        const char* blends[]{UiText("material.blend_opaque","Opaque"), UiText("material.blend_alpha","Alpha"),
                              UiText("material.blend_add","Additive")};
        if (permits(MaterialStateOverride::Cull) &&
            ImGui::Combo(text("material.cull", "Cull", "MaterialCull").c_str(), &cull, culls, 3))
            states.cull = static_cast<MaterialCullMode>(cull);
        if (permits(MaterialStateOverride::Blend) &&
            ImGui::Combo(text("material.blend", "Blend", "MaterialBlend").c_str(), &blend,
                         blends, 3))
            states.blend = static_cast<MaterialBlendMode>(blend);
        if (permits(MaterialStateOverride::DepthTest) &&
            ImGui::Checkbox(text("material.depth_test", "Depth test", "MaterialDepthTest").c_str(),
                            &depthTest))
            states.depthTest = depthTest;
        if (permits(MaterialStateOverride::DepthWrite) &&
            ImGui::Checkbox(text("material.depth_write", "Depth write", "MaterialDepthWrite").c_str(),
                            &depthWrite))
            states.depthWrite = depthWrite;
        if (states.cull || states.blend || states.depthTest || states.depthWrite)
            ChangeMaterial(doc, {}, states, error);
        ImGui::SeparatorText(text("material.preview", "Preview", nullptr).c_str());
        if (ImGui::RadioButton(text("material.preview_sphere", "Sphere", "Sphere").c_str(), m_Sphere) && !m_Sphere) {
            Close(app);
            m_Sphere = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(text("material.preview_cube", "Cube", "Cube").c_str(), !m_Sphere) && m_Sphere) {
            Close(app);
            m_Sphere = false;
        }
        DrawPreview(app, doc, error);
    }

    void MaterialEditor::DrawPreview(Application& app, AssetDocument& doc, std::string& error) {
        std::string previewError;
        if (!UpdateScene(app, doc, previewError)) {
            error = std::move(previewError);
            Close(app);
            return;
        }
        const float size = std::clamp(ImGui::GetContentRegionAvail().x, 1.f, 512.f);
        const auto pixels = static_cast<uint32_t>(
            std::clamp(size * ImGui::GetWindowViewport()->FramebufferScale.x, 1.f, 2048.f));
        if (!app.SetEditorPreview(m_Scene, MaterialSurface, {pixels, pixels}, previewError)) {
            error = std::move(previewError);
            return;
        }
        RenderSurfaceSnapshot surface;
        if (app.GetEditorPreview(MaterialSurface, surface) && surface.textureToken)
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(surface.textureToken)), {size, size});
    }
}
