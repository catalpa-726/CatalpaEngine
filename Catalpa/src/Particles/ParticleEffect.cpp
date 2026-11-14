#include "catpch.h"

#include "Particles/ParticleEffect.h"

#include "Assets/AssetManager.h"
#include "Core/Serialization/AuthorJson.h"
#include "Core/Math/TransformMath.h"
#include "Materials/MaterialValidation.h"

namespace Catalpa {
	namespace {
		bool Finite(const Math::Vec3& value) {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Color(const Math::Vec4& value) {
			for (int i = 0; i < 4; ++i)
				if (!std::isfinite(value[i]) || value[i] < 0 || value[i] > (i == 3 ? 1.f : 65504.f))
					return false;
			return true;
		}

		AuthorJson::Json Array(const Math::Vec4& value) {
			return AuthorJson::Json::array({value.r, value.g, value.b, value.a});
		}

		Math::Vec4 Vec4(const AuthorJson::Json& value) {
			if (!value.is_array() || value.size() != 4)
				throw std::invalid_argument("Particle color needs four linear RGBA values");
			return {float(AuthorJson::Number(value[0])), float(AuthorJson::Number(value[1])),
			    float(AuthorJson::Number(value[2])), float(AuthorJson::Number(value[3]))};
		}
	} /* 命名空间 */

	bool ValidateParticleEffect(const ParticleEffect& effect, std::string& error) {
		if (effect.maxParticles == 0 || effect.maxParticles > MaximumParticleCapacity) {
			error = "Particle pool capacity must be within the Renderer budget of 1..16384";
			return false;
		}
		if (!effect.material.HasValue() || effect.maxParticles == 0 || effect.maxParticles > 16384 || !effect.seed ||
		    !std::isfinite(effect.emissionRate) || effect.emissionRate < 0 || effect.emissionRate > 65536 ||
		    !std::isfinite(effect.duration) || effect.duration <= 0 || effect.duration > 3600) {
			error = "Particle identity, seed, capacity or emission budget is invalid";
			return false;
		}
		if (!std::isfinite(effect.minLifetime) || !std::isfinite(effect.maxLifetime) || effect.minLifetime < .001f ||
		    effect.maxLifetime < effect.minLifetime || effect.maxLifetime > 3600 || !std::isfinite(effect.minSize) ||
		    !std::isfinite(effect.maxSize) || effect.minSize <= 0 || effect.maxSize < effect.minSize ||
		    effect.maxSize > 1000 || !std::isfinite(effect.endSizeScale) || effect.endSizeScale < 0 ||
		    effect.endSizeScale > 100) {
			error = "Particle lifetime/size interval is invalid";
			return false;
		}
		if (!Finite(effect.minVelocity) || !Finite(effect.maxVelocity) || !Finite(effect.gravity) ||
		    !Color(effect.startColor) || !Color(effect.endColor) || !std::isfinite(effect.minAngularVelocity) ||
		    !std::isfinite(effect.maxAngularVelocity) || effect.minAngularVelocity > effect.maxAngularVelocity ||
		    std::abs(effect.minAngularVelocity) > 10000 || std::abs(effect.maxAngularVelocity) > 10000) {
			error = "Particle velocity, gravity or color is invalid";
			return false;
		}
		for (int i = 0; i < 3; ++i)
			if (effect.minVelocity[i] > effect.maxVelocity[i] || std::abs(effect.minVelocity[i]) > 10000 ||
			    std::abs(effect.maxVelocity[i]) > 10000 || std::abs(effect.gravity[i]) > 10000) {
				error = "Particle motion exceeds its bounded metres/seconds domain";
				return false;
			}
		if (effect.materialVersion.HasValue()) {
			if (effect.materialVersion.GetId() != effect.material) {
				error = "Particle material fixed identity mismatch";
				return false;
			}
			if (!ValidateMaterialInstance(*effect.materialVersion.Get(), error))
				return false;
			const auto& state = effect.materialVersion.Get()->renderState;
			if (state.blend == MaterialBlendMode::Opaque || state.depthWrite) {
				error = "Particle billboard material must use alpha/additive blending and disable depth writes";
				return false;
			}
		}
		error.clear();
		return true;
	}

	bool ReadParticleEffect(std::string_view text, ParticleEffect& output, std::string& error) try {
		const auto json = AuthorJson::Parse(text);
		AuthorJson::Fields(json,
		    {"schemaVersion", "material", "seed", "maxParticles", "emissionRate", "duration", "loop", "localSpace",
		        "lifetime", "size", "velocityMin", "velocityMax", "gravity", "startColor", "endColor", "endSizeScale",
		        "angularVelocity"});
		if (AuthorJson::Unsigned(json.at("schemaVersion")) != 1)
			throw std::invalid_argument("Unsupported particle schema");
		ParticleEffect value;
		value.material = AuthorJson::Identity<AssetId>(json.at("material"));
		const auto seed = AuthorJson::Unsigned(json.at("seed")),
		           capacity = AuthorJson::Unsigned(json.at("maxParticles"));
		if (seed > UINT32_MAX || capacity > 16384)
			throw std::invalid_argument("Particle integer overflow");
		value.seed = uint32_t(seed);
		value.maxParticles = uint32_t(capacity);
		value.emissionRate = AuthorJson::Number(json.at("emissionRate"));
		value.duration = AuthorJson::Number(json.at("duration"));
		value.loop = json.at("loop").get<bool>();
		value.localSpace = json.at("localSpace").get<bool>();
		const auto interval = [&](const char* key, float& minimum, float& maximum) {
			const auto& values = json.at(key);
			if (!values.is_array() || values.size() != 2)
				throw std::invalid_argument(std::string("Particle interval requires two values: ") + key);
			minimum = float(AuthorJson::Number(values[0]));
			maximum = float(AuthorJson::Number(values[1]));
		};
		interval("lifetime", value.minLifetime, value.maxLifetime);
		interval("size", value.minSize, value.maxSize);
		interval("angularVelocity", value.minAngularVelocity, value.maxAngularVelocity);
		value.minVelocity = AuthorJson::Vec3(json.at("velocityMin"));
		value.maxVelocity = AuthorJson::Vec3(json.at("velocityMax"));
		value.gravity = AuthorJson::Vec3(json.at("gravity"));
		value.startColor = Vec4(json.at("startColor"));
		value.endColor = Vec4(json.at("endColor"));
		value.endSizeScale = float(AuthorJson::Number(json.at("endSizeScale")));
		if (!ValidateParticleEffect(value, error))
			return false;
		output = std::move(value);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool WriteParticleEffect(const ParticleEffect& value, std::string& output, std::string& error) try {
		if (!ValidateParticleEffect(value, error))
			return false;
		using Json = AuthorJson::Json;
		Json json{{"schemaVersion", 1}, {"material", value.material.ToString()}, {"seed", value.seed},
		    {"maxParticles", value.maxParticles}, {"emissionRate", value.emissionRate}, {"duration", value.duration},
		    {"loop", value.loop}, {"localSpace", value.localSpace},
		    {"lifetime", {value.minLifetime, value.maxLifetime}}, {"size", {value.minSize, value.maxSize}},
		    {"velocityMin", AuthorJson::Array(value.minVelocity)},
		    {"velocityMax", AuthorJson::Array(value.maxVelocity)}, {"gravity", AuthorJson::Array(value.gravity)},
		    {"startColor", Array(value.startColor)}, {"endColor", Array(value.endColor)},
		    {"endSizeScale", value.endSizeScale},
		    {"angularVelocity", {value.minAngularVelocity, value.maxAngularVelocity}}};
		output = json.dump(2);
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool RegisterParticleLoader(AssetManager& assets, std::string& error) {
		AssetLoader loader;
		if (!AssetType::TryParse("particle_effect", loader.type))
			return false;
		loader.cppType = typeid(ParticleEffect);
		loader.decode = [](const AssetContainer& container, const AssetDecodeContext& context, AssetLoadResult& output,
		                    std::string& error) {
			if (container.payloadVersion != 1 || container.payload.size() > 1024 * 1024 ||
			    context.dependencies.size() != 1 || context.cancellation.IsCancellationRequested()) {
				error = "Particle payload version, size or dependency count rejected";
				return false;
			}
			ParticleEffect value;
			if (!ReadParticleEffect(
			        {reinterpret_cast<const char*>(container.payload.data()), container.payload.size()}, value, error))
				return false;
			const auto& material = context.dependencies.front();
			if (material.id != value.material || material.cppType != typeid(MaterialInstance) ||
			    !AssetVersionRef<MaterialInstance>::TryCreate(material.id, material.version,
			        std::static_pointer_cast<const MaterialInstance>(material.value), value.materialVersion, error) ||
			    !ValidateParticleEffect(value, error))
				return false;
			output = MakeAssetLoadResult<ParticleEffect>(
			    std::make_shared<const ParticleEffect>(std::move(value)), sizeof(ParticleEffect));
			error.clear();
			return true;
		};
		return assets.RegisterLoader(std::move(loader), error);
	}

	float ParticlePool::Random() {
		m_Random ^= m_Random << 13;
		m_Random ^= m_Random >> 17;
		m_Random ^= m_Random << 5;
		return float(m_Random >> 8) * (1.0f / 16777216.0f);
	}

	bool ParticlePool::Reset(const ParticleEffect& effect, uint32_t seed, std::string& error) try {
		if (!seed) {
			error = "Particle seed must be nonzero";
			return false;
		}
		if (!ValidateParticleEffect(effect, error))
			return false;
		std::vector<Particle> particles(effect.maxParticles);
		m_Effect = effect;
		m_Particles.swap(particles);
		m_Random = seed;
		m_Live = 0;
		m_Time = 0;
		m_Fraction = 0;
		m_Serial = 0;
		m_Dropped = 0;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool ParticlePool::Tick(double dt, bool emitting, const Math::Mat4& emitter, std::string& error) try {
		if (m_Particles.empty() || !std::isfinite(dt) || dt < 0 || dt > .25) {
			error = "Particle update requires initialized capacity and seconds in [0,.25]";
			return false;
		}
		Math::Mat4 checked;
		if (!Math::ComposeWorld(Math::Mat4{1}, emitter, checked).IsSuccess()) {
			error = "Particle emitter transform must be finite affine";
			return false;
		}
		const float seconds = float(dt);
		for (auto& particle : m_Particles)
			if (particle.alive) {
				particle.age += seconds;
				if (particle.age >= particle.lifetime) {
					particle.alive = false;
					--m_Live;
					continue;
				}
				particle.position += particle.velocity * seconds + m_Effect.gravity * (.5f * seconds * seconds);
				particle.velocity += m_Effect.gravity * seconds;
				particle.rotation += particle.angularVelocity * seconds;
				if (!Finite(particle.position) || !std::isfinite(particle.rotation)) {
					error = "Particle integration produced a non-finite result";
					return false;
				}
			}
		double emissionSeconds = 0;
		if (emitting) {
			if (m_Effect.loop)
				emissionSeconds = dt;
			else
				emissionSeconds = std::min(dt, std::max(0.0, m_Effect.duration - m_Time));
		}
		m_Time += dt;
		if (m_Effect.loop && m_Time >= m_Effect.duration)
			m_Time = std::fmod(m_Time, m_Effect.duration);
		const double total = m_Fraction + emissionSeconds * m_Effect.emissionRate;
		const auto requested = uint64_t(std::floor(total));
		m_Fraction = total - double(requested);
		size_t slot = 0;
		for (uint64_t i = 0; i < requested; ++i) {
			while (slot < m_Particles.size() && m_Particles[slot].alive)
				++slot;
			if (slot == m_Particles.size()) {
				if (requested - i > UINT64_MAX - m_Dropped) {
					error = "Particle dropped-emission counter exhausted";
					return false;
				}
				m_Dropped += requested - i;
				break;
			}
			if (m_Serial >= (uint64_t(1) << 63) - 1) {
				error = "Particle spawn serial exhausted";
				return false;
			}
			auto& particle = m_Particles[slot++];
			particle = {};
			particle.alive = true;
			particle.serial = ++m_Serial;
			particle.lifetime = std::lerp(m_Effect.minLifetime, m_Effect.maxLifetime, Random());
			particle.size = std::lerp(m_Effect.minSize, m_Effect.maxSize, Random());
			particle.rotation = Random() * 6.283185307179586f;
			particle.angularVelocity = std::lerp(m_Effect.minAngularVelocity, m_Effect.maxAngularVelocity, Random());
			for (int c = 0; c < 3; ++c)
				particle.velocity[c] = std::lerp(m_Effect.minVelocity[c], m_Effect.maxVelocity[c], Random());
			if (!m_Effect.localSpace) {
				particle.position = Math::Vec3(emitter * Math::Vec4(0, 0, 0, 1));
				particle.velocity = Math::Vec3(emitter * Math::Vec4(particle.velocity, 0));
			}
			if (!Finite(particle.position) || !Finite(particle.velocity)) {
				error = "Particle spawn transform exceeds finite metres/seconds range";
				return false;
			}
			++m_Live;
		}
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	std::vector<ParticleBillboard> ParticlePool::Snapshot(const Math::Mat4& emitter) const {
		std::vector<ParticleBillboard> result;
		result.reserve(m_Live);
		float scale = 1;
		if (m_Effect.localSpace)
			scale = float(std::max({std::hypot(double(emitter[0].x), double(emitter[0].y), double(emitter[0].z)),
			    std::hypot(double(emitter[1].x), double(emitter[1].y), double(emitter[1].z)),
			    std::hypot(double(emitter[2].x), double(emitter[2].y), double(emitter[2].z))}));
		for (const auto& particle : m_Particles)
			if (particle.alive) {
				const float alpha = std::clamp(particle.age / particle.lifetime, 0.f, 1.f);
				const auto position =
				    m_Effect.localSpace ? Math::Vec3(emitter * Math::Vec4(particle.position, 1)) : particle.position;
				const float size = particle.size * std::lerp(1.f, m_Effect.endSizeScale, alpha) * scale;
				if (!Finite(position) || !std::isfinite(size))
					throw std::invalid_argument("Particle world snapshot exceeds finite position/size range");
				result.push_back({position, size, particle.rotation,
				    glm::mix(m_Effect.startColor, m_Effect.endColor, alpha), particle.serial});
			}
		return result;
	}
} /* 命名空间 Catalpa */
