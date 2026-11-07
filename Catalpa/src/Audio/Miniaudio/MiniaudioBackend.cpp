#include "catpch.h"
#include "Audio/Miniaudio/MiniaudioBackend.h"
#include <miniaudio.h>
#include "Audio/AudioStreamService.h"
namespace Catalpa {
    namespace {
        struct StreamSource {
            ma_data_source_base base{};
            AudioStreamBuffer* buffer = nullptr;
            uint64_t frames = 0;
        };
        ma_result StreamRead(ma_data_source* source, void* out, ma_uint64 frames, ma_uint64* read) {
            auto& state = *static_cast<StreamSource*>(source);
            ma_uint64 total = 0;
            while (total < frames) {
                const auto request = ma_uint32(std::min<ma_uint64>(4096, frames - total));
                const auto count =
                    state.buffer->Read(out ? static_cast<float*>(out) + size_t(total) * 2 : nullptr, request);
                total += count;
                if (count < request)
                    break;
            }
            if (read)
                *read = total;
            return total < frames ? MA_AT_END : MA_SUCCESS;
        }
        // miniaudio 初始化可查询当前位置；真正 seek 由 owner 脱离节点后重建 FIFO。
        ma_result StreamSeek(ma_data_source* source, ma_uint64 frame) {
            return static_cast<StreamSource*>(source)->buffer->Cursor() == frame ? MA_SUCCESS
                                                                                 : MA_NOT_IMPLEMENTED;
        }
        ma_result StreamFormat(ma_data_source*, ma_format* format, ma_uint32* channels, ma_uint32* rate,
                               ma_channel* map, size_t capacity) {
            if (format)
                *format = ma_format_f32;
            if (channels)
                *channels = 2;
            if (rate)
                *rate = 48000;
            if (map)
                ma_channel_map_init_standard(ma_standard_channel_map_default, map, capacity, 2);
            return MA_SUCCESS;
        }
        ma_result StreamCursor(ma_data_source* source, ma_uint64* cursor) {
            if (cursor)
                *cursor = static_cast<StreamSource*>(source)->buffer->Cursor();
            return MA_SUCCESS;
        }
        ma_result StreamLength(ma_data_source* source, ma_uint64* length) {
            if (length)
                *length = static_cast<StreamSource*>(source)->frames;
            return MA_SUCCESS;
        }
        ma_result StreamLoop(ma_data_source*, ma_bool32) {
            return MA_SUCCESS;
        }
        const ma_data_source_vtable StreamTable{
            StreamRead, StreamSeek, StreamFormat, StreamCursor, StreamLength, StreamLoop, 0};

        // ma_audio_buffer_ref::cursor 不是原子值，不能让主线程直接读 callback 写入的游标。
        struct ResidentSource {
            ma_data_source_base base{};
            ma_audio_buffer_ref data{};
            std::atomic_uint64_t cursor{0};
            std::atomic_bool loop{false};
            uint64_t frames = 0;
        };
        ma_result ResidentRead(ma_data_source* source, void* out, ma_uint64 frames, ma_uint64* read) {
            auto& state = *static_cast<ResidentSource*>(source);
            const auto count = ma_audio_buffer_ref_read_pcm_frames(
                &state.data, out, frames, state.loop.load(std::memory_order_relaxed));
            state.cursor.store(state.data.cursor, std::memory_order_release);
            if (read)
                *read = count;
            return count < frames ? MA_AT_END : MA_SUCCESS;
        }
        ma_result ResidentSeek(ma_data_source* source, ma_uint64 frame) {
            auto& state = *static_cast<ResidentSource*>(source);
            const auto result = ma_audio_buffer_ref_seek_to_pcm_frame(&state.data, frame);
            if (result == MA_SUCCESS)
                state.cursor.store(frame, std::memory_order_release);
            return result;
        }
        ma_result ResidentCursor(ma_data_source* source, ma_uint64* cursor) {
            if (cursor)
                *cursor = static_cast<ResidentSource*>(source)->cursor.load(std::memory_order_acquire);
            return MA_SUCCESS;
        }
        ma_result ResidentLength(ma_data_source* source, ma_uint64* length) {
            if (length)
                *length = static_cast<ResidentSource*>(source)->frames;
            return MA_SUCCESS;
        }
        ma_result ResidentLoop(ma_data_source* source, ma_bool32 loop) {
            static_cast<ResidentSource*>(source)->loop.store(loop != 0, std::memory_order_release);
            return MA_SUCCESS;
        }
        const ma_data_source_vtable ResidentTable{
            ResidentRead, ResidentSeek, StreamFormat, ResidentCursor, ResidentLength, ResidentLoop, 0};

        // 引擎 callback 只消费已驻留 PCM，不访问文件、解码器、日志或 World。
        class MiniaudioBackend final : public IAudioBackend {
            struct Voice {
                AssetVersionRef<AudioClipAsset> clip;
                ResidentSource data{};
                ma_sound sound{};
                AudioVoiceSettings settings;
                bool initialized = false, streamInitialized = false, residentInitialized = false,
                     bufferInitialized = false, paused = false;
                StreamSource stream{};
                std::shared_ptr<AudioStreamBuffer> buffer;
                ~Voice() {
                    if (initialized) {
                        ma_sound_stop(&sound);
                        ma_sound_uninit(&sound);
                    }
                    if (streamInitialized)
                        ma_data_source_uninit(&stream);
                    if (residentInitialized)
                        ma_data_source_uninit(&data);
                    if (bufferInitialized)
                        ma_audio_buffer_ref_uninit(&data.data);
                }
            };
            ma_engine engine{};
            bool ready = false;
            std::map<AudioVoiceHandle, std::unique_ptr<Voice>> voices;

          public:
            bool Init(std::string& error) {
                auto config = ma_engine_config_init();
                config.sampleRate = 48000;
                config.channels = 2;
                config.listenerCount = 1;
                const auto result = ma_engine_init(&config, &engine);
                if (result != MA_SUCCESS) {
                    error = "miniaudio device initialization failed: " + std::to_string(result);
                    return false;
                }
                ready = true;
                error.clear();
                return true;
            }
            ~MiniaudioBackend() override {
                Shutdown();
            }
            bool StartResident(AudioVoiceHandle id, const AssetVersionRef<AudioClipAsset>& clip,
                               const AudioVoiceSettings& settings, uint64_t frame, bool paused,
                               std::string& error) {
                if (voices.contains(id)) {
                    error = "Duplicate miniaudio voice";
                    return false;
                }
                auto voice = std::make_unique<Voice>();
                voice->clip = clip;
                voice->settings = settings;
                voice->data.frames = clip.Get()->Frames();
                auto dataConfig = ma_data_source_config_init();
                dataConfig.vtable = &ResidentTable;
                auto result = ma_data_source_init(&dataConfig, &voice->data);
                voice->residentInitialized = result == MA_SUCCESS;
                if (result == MA_SUCCESS)
                    result = ma_audio_buffer_ref_init(ma_format_f32, 2, clip.Get()->samples.data(),
                                                      clip.Get()->Frames(), &voice->data.data);
                voice->bufferInitialized = result == MA_SUCCESS;
                if (result == MA_SUCCESS)
                    result = ResidentSeek(&voice->data, frame);
                if (result == MA_SUCCESS)
                    result = ma_sound_init_from_data_source(
                        &engine, &voice->data, settings.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION,
                        nullptr, &voice->sound);
                if (result != MA_SUCCESS) {
                    error = "Cannot create PCM voice: " + std::to_string(result);
                    return false;
                }
                voice->initialized = true;
                ma_sound_set_volume(&voice->sound, settings.gain);
                ma_sound_set_pitch(&voice->sound, settings.pitch);
                ma_sound_set_looping(&voice->sound, settings.loop);
                ma_sound_set_position(&voice->sound, settings.position.x, settings.position.y,
                                      settings.position.z);
                ma_sound_set_min_distance(&voice->sound, settings.minDistance);
                ma_sound_set_max_distance(&voice->sound, settings.maxDistance);
                voice->paused = paused;
                result = paused ? MA_SUCCESS : ma_sound_start(&voice->sound);
                if (result != MA_SUCCESS) {
                    error = "Cannot start PCM voice";
                    return false;
                }
                voices.emplace(id, std::move(voice));
                error.clear();
                return true;
            }
            bool Start(AudioVoiceHandle id, const AssetVersionRef<AudioClipAsset>& clip,
                       const AudioVoiceSettings& settings, std::string& error) override {
                return StartResident(id, clip, settings, 0, false, error);
            }
            bool StartStream(AudioVoiceHandle id, const AssetVersionRef<AudioClipAsset>& clip,
                             std::shared_ptr<AudioStreamBuffer> buffer, const AudioVoiceSettings& settings,
                             bool paused, std::string& error) override {
                if (voices.contains(id) || !buffer) {
                    error = "Invalid or duplicate stream voice";
                    return false;
                }
                auto voice = std::make_unique<Voice>();
                voice->clip = clip;
                voice->settings = settings;
                voice->buffer = std::move(buffer);
                voice->stream.buffer = voice->buffer.get();
                voice->stream.frames = clip.Get()->Frames();
                auto config = ma_data_source_config_init();
                config.vtable = &StreamTable;
                auto result = ma_data_source_init(&config, &voice->stream);
                if (result != MA_SUCCESS) {
                    error = "Cannot initialize stream data source";
                    return false;
                }
                voice->streamInitialized = true;
                result = ma_sound_init_from_data_source(
                    &engine, &voice->stream, settings.spatial ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                    &voice->sound);
                if (result != MA_SUCCESS) {
                    error = "Cannot initialize stream sound";
                    return false;
                }
                voice->initialized = true;
                ma_sound_set_volume(&voice->sound, settings.gain);
                ma_sound_set_pitch(&voice->sound, settings.pitch);
                ma_sound_set_looping(&voice->sound, false);
                ma_sound_set_position(&voice->sound, settings.position.x, settings.position.y,
                                      settings.position.z);
                ma_sound_set_min_distance(&voice->sound, settings.minDistance);
                ma_sound_set_max_distance(&voice->sound, settings.maxDistance);
                voice->paused = paused;
                if (!paused && ma_sound_start(&voice->sound) != MA_SUCCESS) {
                    error = "Cannot start stream sound";
                    return false;
                }
                voices.emplace(id, std::move(voice));
                error.clear();
                return true;
            }
            bool Stop(AudioVoiceHandle id, std::string& error) override {
                auto found = voices.find(id);
                if (found != voices.end()) {
                    // uninit 从 node graph 分离并等待正在使用节点的处理结束，之后才释放 clip。
                    voices.erase(found);
                }
                error.clear();
                return true;
            }
            bool Seek(AudioVoiceHandle id, uint64_t frame, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end() || frame > it->second->clip.Get()->Frames()) {
                    error = "Invalid voice seek";
                    return false;
                }
                // 当前 miniaudio 实现通过原子 seekTarget 交给 callback 应用；不跨线程写 data source。
                const auto result = ma_sound_seek_to_pcm_frame(&it->second->sound, frame);
                if (result != MA_SUCCESS) {
                    error = "Voice seek failed";
                    return false;
                }
                error.clear();
                return true;
            }
            bool Pause(AudioVoiceHandle id, bool paused, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end()) {
                    error = "Unknown voice";
                    return false;
                }
                const auto result =
                    paused ? ma_sound_stop(&it->second->sound) : ma_sound_start(&it->second->sound);
                if (result != MA_SUCCESS) {
                    error = "Voice pause/start failed";
                    return false;
                }
                it->second->paused = paused;
                error.clear();
                return true;
            }
            bool Position(AudioVoiceHandle id, const Math::Vec3& p, std::string& error) override {
                auto it = voices.find(id);
                if (it == voices.end()) {
                    error = "Unknown voice";
                    return false;
                }
                ma_sound_set_position(&it->second->sound, p.x, p.y, p.z);
                it->second->settings.position = p;
                error.clear();
                return true;
            }
            void Listener(const AudioListenerState& value) override {
                ma_engine_listener_set_position(&engine, 0, value.position.x, value.position.y,
                                                value.position.z);
                ma_engine_listener_set_direction(&engine, 0, value.forward.x, value.forward.y,
                                                 value.forward.z);
                ma_engine_listener_set_world_up(&engine, 0, value.up.x, value.up.y, value.up.z);
            }
            void Advance(double) override {}
            void MasterGain(float value) override {
                ma_engine_set_volume(&engine, value);
            }
            bool Healthy(std::string& error) const override {
                const auto* device = ma_engine_get_device(const_cast<ma_engine*>(&engine));
                if (!ready || !device || ma_device_get_state(device) != ma_device_state_started) {
                    error = "miniaudio playback device is unavailable or stopped";
                    return false;
                }
                error.clear();
                return true;
            }
            std::vector<AudioVoiceSnapshot> Snapshot() const override {
                std::vector<AudioVoiceSnapshot> out;
                for (const auto& [id, v] : voices) {
                    ma_uint64 cursor = 0;
                    ma_sound_get_cursor_in_pcm_frames(&v->sound, &cursor);
                    out.push_back({id, cursor, v->clip.Get()->Frames(), ma_sound_is_playing(&v->sound) != 0,
                                   v->settings.gain});
                }
                return out;
            }
            void Shutdown() noexcept override {
                voices.clear();
                if (ready)
                    ma_engine_uninit(&engine);
                ready = false;
            }
        };
    } // namespace
    std::unique_ptr<IAudioBackend> CreateMiniaudioBackend(std::string& error) {
        auto value = std::make_unique<MiniaudioBackend>();
        if (!value->Init(error))
            return {};
        return value;
    }
} // namespace Catalpa
