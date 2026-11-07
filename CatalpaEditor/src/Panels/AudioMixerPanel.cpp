#include "Panels/AudioMixerPanel.h"
#include "Core/Application.h"
#include "Audio/AudioService.h"
#include "imgui.h"
namespace Catalpa::Editor {
    void AudioMixerPanel::Draw(Application& app, const PanelText& text, std::string& error) {
        auto& audio = app.GetAudio();
        if (ImGui::SliderFloat(text("audio.master", "Master gain", "AudioMaster").c_str(), &m_Master, 0, 2))
            if (!audio.SetMasterGain(m_Master, error))
                return;
        ImGui::TextUnformatted((audio.IsNull()
                                    ? text("audio.backend_null", "Null backend (silent simulation)", nullptr)
                                    : text("audio.backend_real", "miniaudio playback device", nullptr))
                                   .c_str());
        const auto voices = audio.Snapshot();
        ImGui::Text("%s: %zu", text("audio.active_voices", "Active voices", nullptr).c_str(), voices.size());
        for (const auto& voice : voices) {
            ImGui::PushID(static_cast<int>(voice.handle.value));
            ImGui::Text("%llu | %.2f / %.2f s | gain %.2f",
                        static_cast<unsigned long long>(voice.handle.value), double(voice.cursor) / 48000,
                        double(voice.frames) / 48000, voice.gain);
            ImGui::SameLine();
            if (ImGui::SmallButton(text("audio.stop", "Stop", "StopVoice").c_str()))
                audio.Stop(voice.handle, error);
            ImGui::PopID();
        }
        if (!audio.Diagnostic().empty())
            ImGui::TextWrapped("%s", audio.Diagnostic().c_str());
    }
} // namespace Catalpa::Editor
