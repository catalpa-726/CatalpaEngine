#include "catpch.h"

#include "AssetPipeline/Importers/AudioImporter.h"

#include "Audio/AudioAssetLoader.h"

#include <miniaudio.h>
namespace Catalpa::Tools {
	bool AudioImporter::Import(AssetBuildContext& context, AssetImportResult& output, std::string& error) const {
		std::vector<std::byte> source;
		if (!context.Read(context.request.source, source, error))
			return false;
		ma_decoder decoder{};
		auto config = ma_decoder_config_init(ma_format_f32, 2, 48000);
		if (ma_decoder_init_memory(source.data(), source.size(), &config, &decoder) != MA_SUCCESS) {
			error = "Unsupported or corrupt WAV/FLAC/MP3 source";
			return false;
		}
		struct Cleanup {
			ma_decoder* value;

			~Cleanup() { ma_decoder_uninit(value); }
		} cleanup{&decoder};
		ma_uint64 frames = 0;
		if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) != MA_SUCCESS || frames == 0 ||
		    frames > uint64_t(65536) * 1024) {
			error = "Decoded audio exceeds the 1024-chunk duration budget";
			return false;
		}
		if (!context.ReserveWorking(size_t(frames) * 8 * 2, error))
			return false;
		AudioClipAsset clip;
		clip.sourceUri = context.SourceKey();
		clip.frameCount = frames;
		AssetImportResult result;
		const bool streaming = frames > 480000;
		if (!streaming)
			clip.samples.resize(size_t(frames) * 2);
		std::vector<float> scratch(65536 * 2);
		ma_uint64 cursor = 0;
		size_t index = 0;
		while (cursor < frames) {
			if (!context.CheckCancelled(error))
				return false;
			const ma_uint64 count = std::min<ma_uint64>(65536, frames - cursor);
			ma_uint64 read = 0;
			float* target = streaming ? scratch.data() : clip.samples.data() + size_t(cursor) * 2;
			const auto decoded = ma_decoder_read_pcm_frames(&decoder, target, count, &read);
			if ((decoded != MA_SUCCESS && decoded != MA_AT_END) || read != count) {
				error = "Audio decode ended before declared frame count";
				return false;
			}
			if (streaming) {
				const std::string key = "audio/chunk/" + std::to_string(index++);
				const auto id = context.Identity(key);
				std::vector<std::byte> payload;
				if (!AudioAssetLoader::EncodeChunk(std::span(target, size_t(read) * 2), payload, error))
					return false;
				clip.chunks.push_back({id});
				result.assets.push_back({id, key, key, ImportedAuthorProduct{"audio_chunk", std::move(payload), {}}});
				result.mappings.push_back({id, key, key, "audio_chunk"});
			}
			cursor += read;
		}
		std::vector<std::byte> payload;
		if (!AudioAssetLoader::Encode(clip, payload, error))
			return false;
		const auto id = context.Identity("audio_clip");
		result.primary = id;
		std::vector<AssetId> deps;
		for (const auto& d : clip.chunks)
			deps.push_back(d.id);
		result.assets.push_back(
		    {id, "audio_clip", "audio_clip", ImportedAuthorProduct{"audio_clip", std::move(payload), std::move(deps)}});
		result.mappings.push_back({id, "audio_clip", "audio_clip", "audio_clip"});
		context.Diagnose(AssetDiagnosticLevel::Info, "audio",
		    streaming ? "Derived 48 kHz stereo float32 PCM with immutable 65536-frame disk chunks and seek table"
		              : "Derived resident 48 kHz stereo float32 PCM; source bytes remain unchanged");
		output = std::move(result);
		error.clear();
		return true;
	}
} /* 命名空间 Catalpa::Tools */
