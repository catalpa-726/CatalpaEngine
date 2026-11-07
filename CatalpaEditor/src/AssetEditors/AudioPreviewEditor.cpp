#include "AssetEditors/AudioPreviewEditor.h"
#include "Core/Application.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioService.h"
#include "imgui.h"

namespace Catalpa::Editor {
    void AudioPreviewEditor::Close(Application& app) {
        if (m_Voice.HasValue()) {
            std::string error;
            if (!app.GetAudio().Retire(m_Voice, error))
                CAT_CORE_WARN("Audio preview retirement: {0}", error);
        }
        // 服务接管有界退役请求；命令暂满时不丢失句柄，也不消费其他宿主的 ack。
        m_Voice = {};
        m_Document = {};
        m_Paused = false;
    }
    void AudioPreviewEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text,
                                  std::string& error) {
        if (m_Document != doc.Id()) {
            Close(app);
            m_Document = doc.Id();
        }
        AssetVersionRef<AudioClipAsset> clip;
        bool fallback = false;
        if (!app.GetAssets().Acquire(doc.Asset(), clip, fallback, error))
            return;
        auto& audio = app.GetAudio();
        const auto* value = clip.Get();
        ImGui::TextWrapped("%s", value->sourceUri.c_str());
        ImGui::Text("48 kHz %s | %.2f s | %s", text("audio.stereo", "stereo", nullptr).c_str(),
                    double(value->Frames()) / 48000,
                    (value->Streaming() ? text("audio.disk_chunks", "disk chunks", nullptr)
                                        : text("audio.resident_pcm", "resident PCM", nullptr))
                        .c_str());
        ImGui::TextUnformatted((audio.IsNull()
                                    ? text("audio.backend_null", "Null backend (silent simulation)", nullptr)
                                    : text("audio.backend_real", "miniaudio playback device", nullptr))
                                   .c_str());
        ImGui::SliderFloat(text("audio.gain", "Gain", "AudioGain").c_str(), &m_Gain, 0, 2);
        ImGui::SliderFloat(text("audio.pitch", "Pitch", "AudioPitch").c_str(), &m_Pitch, .125f, 4);
        ImGui::Checkbox(text("audio.loop", "Loop", "AudioLoop").c_str(), &m_Loop);
        if (ImGui::Button(text("audio.play", "Play", "AudioPlay").c_str())) {
            if (m_Voice.HasValue() && !audio.Stop(m_Voice, error))
                return;
            AudioVoiceSettings settings;
            settings.gain = m_Gain;
            settings.pitch = m_Pitch;
            settings.loop = m_Loop;
            if (!audio.Play(clip, settings, m_Voice, error))
                return;
            m_Paused = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(text("audio.pause", "Pause / resume", "AudioPause").c_str()) &&
            m_Voice.HasValue()) {
            if (!audio.Pause(m_Voice, !m_Paused, error))
                return;
            m_Paused = !m_Paused;
        }
        ImGui::SameLine();
        if (ImGui::Button(text("audio.stop", "Stop", "AudioStop").c_str()) && m_Voice.HasValue()) {
            if (!audio.Stop(m_Voice, error))
                return;
            m_Voice = {};
        }
        for (const auto& voice : audio.Snapshot())
            if (voice.handle == m_Voice) {
                double seconds = double(voice.cursor) / 48000;
                const double min = 0, max = double(voice.frames) / 48000;
                if (ImGui::SliderScalar(text("audio.seek", "Seek (seconds)", "AudioSeek").c_str(),
                                        ImGuiDataType_Double, &seconds, &min, &max, "%.2f"))
                    if (!audio.Seek(m_Voice, std::min(voice.frames, uint64_t(seconds * 48000)), error))
                        return;
                ImGui::Text("%s %llu | %s", text("audio.voice", "Voice", nullptr).c_str(),
                            static_cast<unsigned long long>(voice.handle.value),
                            (voice.playing ? text("audio.playing", "playing", nullptr)
                                           : text("audio.paused_finished", "paused / finished", nullptr))
                                .c_str());
            }
        if (!audio.Diagnostic().empty())
            ImGui::TextWrapped("%s", audio.Diagnostic().c_str());
    }
} // namespace Catalpa::Editor
