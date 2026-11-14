#pragma once

namespace Catalpa {

	struct Extent2D {
		unsigned int width = 0;
		unsigned int height = 0;

		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return width == 0 || height == 0; }
		constexpr bool operator==(const Extent2D&) const = default;
	};

	/* 标识一次设备有效生命周期；设备重建后必须分配新的、不复用的值。 */
	struct DeviceId {
		uint64_t value = 0;

		[[nodiscard]] constexpr bool HasValue() const noexcept { return value != 0; }
		constexpr bool operator==(const DeviceId&) const = default;
	};

	/* 同一设备生命周期内的队列身份。 */
	struct QueueId {
		uint32_t value = 0;

		[[nodiscard]] constexpr bool HasValue() const noexcept { return value != 0; }
		constexpr bool operator==(const QueueId&) const = default;
	};

	/* 标识某个帧槽的一次使用；帧槽复用后 generation 改变。 */
	struct RHIFrameToken {
		DeviceId device{};
		uint32_t slot = std::numeric_limits<uint32_t>::max();
		uint64_t generation = 0;

		[[nodiscard]] constexpr bool HasValue() const noexcept {
			return device.HasValue() && slot != std::numeric_limits<uint32_t>::max() && generation != 0;
		}
		constexpr bool operator==(const RHIFrameToken&) const = default;
	};

	/* framebuffer 像素偏移。 */
	struct Offset2D {
		int32_t x = 0;
		int32_t y = 0;

		constexpr bool operator==(const Offset2D&) const = default;
	};

	struct Rect2D {
		Offset2D offset{};
		Extent2D extent{};

		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return extent.IsEmpty(); }
		constexpr bool operator==(const Rect2D&) const = default;
	};

	/* 位置和尺寸单位为 framebuffer 像素；深度范围无单位。 */
	struct RHIViewport {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float minDepth = 0.0f;
		float maxDepth = 1.0f;
	};

}