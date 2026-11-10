#include "catpch.h"
#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"
#include "Core/Application.h"
#include "Log/LogSubscription.h"
#include "Panels/BrowserThumbnails.h"
#include <chrono>

namespace Catalpa::Tests {
    namespace AssetTest {
        bool CreateThumbnailSources(Fixture& fixture, std::string& error) {
            if (!fixture.Create(error)) return false;
            fixture.files = {fixture.root / "swatch.png", fixture.root / "sphere.material.json"};
            // 2×2 RGBA PNG。测试输入独立于源码树和用户的作者项目。
            constexpr unsigned char png[] = {
                0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,
                0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,
                0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x13,0x49,0x44,
                0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0xf0,0x1f,0x0c,0x19,0x18,
                0xfe,0x83,0x01,0x00,0x49,0xc8,0x09,0xf7,0x14,0x63,0x32,0x9d,0x00,
                0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
            constexpr std::string_view material = R"({
                "schemaVersion": 1, "kind": "instance", "definitionVersion": 1, "revision": 1,
                "definitionId": "43415441-4c50-4100-0000-00000000000c",
                "parameters": {"base_color": [0.2, 0.8, 0.65, 1], "roughness": 0.45},
                "renderState": {"cull": "none"}
            })";
            return AtomicFileWriter::Write(fixture.files[0], std::as_bytes(std::span(png)), error) &&
                   AtomicFileWriter::Write(fixture.files[1], material, error);
        }
    }

    namespace {
        constexpr uint64_t Atlas = 0x4341545448554d42ull;

        // 与网格共用 Draw 和提交后的快照。在球心及球外取真实 GPU ID，防止空白图集冒充预览。
        class BrowserThumbnailGpuTest final : public Layer {
          public:
            BrowserThumbnailGpuTest() : Layer("BrowserThumbnailGpuTest"), m_Thumbnails(Atlas) {}

            void OnAttach() override {
                Application::Get().SetEditorViewportActive(true);
                m_Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
                std::string error;
                if (!AssetTest::CreateThumbnailSources(m_Fixture, error)) Finish(false, error);
            }
            void OnDetach() override {
                m_Thumbnails.Reset();
                Application::Get().SetEditorViewportActive(false);
            }
            void OnEvent(Event& event) override {
                if (!m_Finished && event.GetEventType() == EventType::WindowClose)
                    Finish(false, "Window closed before GPU readback completed");
            }
            void OnUpdate(Deltatime) override {
                if (m_Finished) return;
                uint64_t dropped = 0;
                for (const auto& record : m_Logs.Drain(256, dropped)) {
                    if (record.message.find("VUID-") != std::string::npos ||
                        record.message.find("SYNC-HAZARD") != std::string::npos) {
                        Finish(false, record.message);
                        return;
                    }
                }
                if (dropped) {
                    Finish(false, "GPU validation log overflow");
                    return;
                }
                m_Thumbnails.BeginFrame();
                if (std::chrono::steady_clock::now() > m_Deadline)
                    Finish(false, "Timed out waiting for thumbnail upload, material rendering or GPU readback");
            }
            void OnImGuiRender() override {
                if (m_Finished) return;
                auto& app = Application::Get();
                const auto& root = m_Fixture.root;
                ImGui::SetNextWindowSize({320, 220}, ImGuiCond_Always);
                ImGui::Begin("Editor GPU thumbnail acceptance", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
                const auto start = ImGui::GetCursorScreenPos();
                std::string imageError, materialError;
                const bool image = m_Thumbnails.Draw(root, m_Fixture.files[0], start,
                                                     {start.x + 96, start.y + 96}, imageError);
                const bool material = m_Thumbnails.Draw(root, m_Fixture.files[1],
                    {start.x + 104, start.y}, {start.x + 200, start.y + 96}, materialError);
                ImGui::Dummy({200, 96});
                ImGui::End();
                if (!imageError.empty() || !materialError.empty()) {
                    Finish(false, imageError.empty() ? materialError : imageError);
                    return;
                }
                if (!image || !material) return;

                RenderSurfaceSnapshot texture, sphere;
                if (!app.GetEditorPreview(Atlas, texture) || !app.GetEditorPreview(Atlas + 1, sphere) ||
                    !texture.textureToken || !sphere.textureToken || texture.textureToken == sphere.textureToken ||
                    texture.extent.width != 256 || texture.extent.height != 256 ||
                    sphere.extent.width != 256 || sphere.extent.height != 256) {
                    Finish(false, "Actual image and material tiles require two distinct published 256x256 GPU atlases");
                    return;
                }
                if (!m_Sequence) {
                    m_Sequence = sphere.frameSequence;
                    // 第一个格子是左上 64x64；球覆盖中心，角点只能读到背景。
                    for (size_t i = 0; i < m_Tickets.size(); ++i) {
                        const uint32_t pixel = i == 0 ? 32u : 2u;
                        PickingRequest request{i + 1, sphere.surfaceId, sphere.sceneIdentity, sphere.frameSequence,
                                               sphere.targetGeneration, pixel, pixel};
                        std::string error;
                        if (!app.RequestEditorPick(request, m_Tickets[i], error)) {
                            Finish(false, error);
                            return;
                        }
                    }
                    return;
                }
                if (sphere.frameSequence != m_Sequence) {
                    Finish(false, "Cached material atlas was redrawn after being frozen");
                    return;
                }
                for (size_t i = 0; i < m_Tickets.size(); ++i) {
                    if (m_Completed[i]) continue;
                    PickingResult result;
                    if (!app.PollEditorPick(m_Tickets[i], result)) continue;
                    if (result.status != PickingResultStatus::Ready || result.hit.HasValue() != (i == 0)) {
                        Finish(false, i == 0 ? "GPU sphere center did not hit a mesh" : "GPU sphere corner did not read background");
                        return;
                    }
                    m_Completed[i] = true;
                }
                if (m_Completed[0] && m_Completed[1])
                    Finish(true, "PNG and PBR tiles drew through ImGui; frozen GPU sphere center hit and corner was empty");
            }

          private:
            void Finish(bool passed, const std::string& detail) {
                m_Finished = true;
                if (passed) CAT_INFO("[Editor thumbnails GPU PASS] {0}", detail);
                else CAT_ERROR("[Editor thumbnails GPU FAIL] {0}", detail);
                Application::Get().RequestClose(passed ? 0 : 1);
            }
            AssetTest::Fixture m_Fixture;
            LogSubscription m_Logs;
            Editor::BrowserThumbnails m_Thumbnails;
            std::chrono::steady_clock::time_point m_Deadline;
            std::array<uint64_t, 2> m_Tickets{};
            std::array<bool, 2> m_Completed{};
            uint64_t m_Sequence = 0;
            bool m_Finished = false;
        };
    }

    std::unique_ptr<Layer> CreateBrowserThumbnailGpuTest() {
        return std::make_unique<BrowserThumbnailGpuTest>();
    }
}
