#include "catpch.h"
#include "Camera/CameraProjection.h"


namespace Catalpa {
    namespace {
        using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;

        // 只转换派生结果，拒绝溢出及导致不可逆的下溢，不改写镜头输入。
        Math::MathStatus ToFloat(const DMat4& source, Math::Mat4& output) noexcept
        {
            Math::Mat4 candidate{ 0.0f };
            constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    const double value = source[column][row];
                    if (!std::isfinite(value) || std::abs(value) > limit)
                        return { Math::MathError::OutOfRange, "Camera projection exceeds finite float range" };
                    candidate[column][row] = static_cast<float>(value);
                    if (value != 0.0 && candidate[column][row] == 0.0f)
                        return { Math::MathError::OutOfRange, "Camera projection loses a nonzero coefficient" };
                }
            }
            output = candidate;
            return {};
        }

        // 对发布后的 float 值检查双向残差，而非只检查 double 候选的可逆性。
        bool IsInversePair(const Math::Mat4& a, const Math::Mat4& b) noexcept
        {
            constexpr double tolerance = 2.0e-4;
            const DMat4 products[]{ DMat4(a) * DMat4(b), DMat4(b) * DMat4(a) };
            for (const auto& product : products)
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (!std::isfinite(product[column][row])
                            || std::abs(product[column][row] - (column == row ? 1.0 : 0.0)) > tolerance) return false;
            return true;
        }
    }

    Math::MathStatus ValidateCameraLens(const CameraLens& lens) noexcept
    {
        if (lens.projection != ProjectionType::Perspective && lens.projection != ProjectionType::Orthographic)
            return { Math::MathError::OutOfRange, "Camera projection type is unsupported" };
        if (!std::isfinite(lens.nearPlane) || lens.nearPlane < 0.0f
            || (lens.projection == ProjectionType::Perspective && lens.nearPlane == 0.0f))
            return { Math::MathError::OutOfRange, "Camera near distance must be finite and positive for perspective" };
        if (lens.infiniteFar && lens.projection == ProjectionType::Orthographic)
            return { Math::MathError::OutOfRange, "Infinite orthographic depth is not an invertible linear projection" };
        if (!lens.infiniteFar && (!std::isfinite(lens.farPlane) || lens.farPlane <= lens.nearPlane))
            return { Math::MathError::OutOfRange, "Camera far distance must exceed near distance" };
        if (lens.projection == ProjectionType::Perspective) {
            if (!std::isfinite(lens.verticalFovRadians) || lens.verticalFovRadians <= 0.0f || lens.verticalFovRadians >= Math::Pi)
                return { Math::MathError::OutOfRange, "Perspective vertical FOV must be in (0, pi) radians" };
        } else if (!std::isfinite(lens.orthographicHeight) || lens.orthographicHeight <= 0.0f) {
            return { Math::MathError::OutOfRange, "Orthographic height must be finite and positive" };
        }
        return {};
    }

    Math::MathStatus BuildCameraProjection(const CameraLens& lens, const CameraViewport& viewport,
        Math::Mat4& projection, Math::Mat4& inverse) noexcept
    {
        if (&projection == &inverse) return { Math::MathError::OutOfRange, "Projection outputs must not alias" };
        if (const auto status = ValidateCameraLens(lens); !status.IsSuccess()) return status;
        if (viewport.width == 0 || viewport.height == 0)
            return { Math::MathError::OutOfRange, "Camera viewport must have nonzero pixel extent" };
        if (!std::isfinite(viewport.jitterPixelsX) || !std::isfinite(viewport.jitterPixelsY))
            return { Math::MathError::NonFinite, "Camera pixel jitter is not finite" };

        const double aspect = static_cast<double>(viewport.width) / viewport.height;
        const double nearDistance = lens.nearPlane;
        const double farDistance = lens.farPlane;
        const double jitterX = 2.0 * viewport.jitterPixelsX / viewport.width;
        const double jitterY = 2.0 * viewport.jitterPixelsY / viewport.height;
        DMat4 result{ 0.0 }, inverseResult{ 0.0 };
        if (lens.projection == ProjectionType::Perspective) {
            const double y = 1.0 / std::tan(static_cast<double>(lens.verticalFovRadians) * 0.5);
            const double x = y / aspect;
            const double signedY = viewport.flipY ? -y : y;
            // zCamera=-distance，wClip=distance，depth=-A+B/distance。
            const double a = lens.infiniteFar ? (lens.reversedZ ? 0.0 : -1.0)
                : (lens.reversedZ ? nearDistance / (farDistance - nearDistance) : farDistance / (nearDistance - farDistance));
            const double b = lens.infiniteFar ? (lens.reversedZ ? nearDistance : -nearDistance)
                : (lens.reversedZ ? nearDistance * farDistance / (farDistance - nearDistance) : nearDistance * farDistance / (nearDistance - farDistance));
            result[0][0] = x;
            result[1][1] = signedY;
            result[2][0] = -jitterX;
            result[2][1] = -jitterY;
            result[2][2] = a;
            result[2][3] = -1.0;
            result[3][2] = b;
            inverseResult[0][0] = 1.0 / x;
            inverseResult[1][1] = 1.0 / signedY;
            inverseResult[3][0] = -jitterX / x;
            inverseResult[3][1] = -jitterY / signedY;
            inverseResult[3][2] = -1.0;
            inverseResult[2][3] = 1.0 / b;
            inverseResult[3][3] = a / b;
        } else {
            const double y = 2.0 / lens.orthographicHeight;
            const double x = y / aspect;
            const double signedY = viewport.flipY ? -y : y;
            const double a = lens.reversedZ ? 1.0 / (farDistance - nearDistance) : 1.0 / (nearDistance - farDistance);
            const double b = lens.reversedZ ? farDistance / (farDistance - nearDistance) : nearDistance / (nearDistance - farDistance);
            result[0][0] = x;
            result[1][1] = signedY;
            result[2][2] = a;
            result[3][0] = jitterX;
            result[3][1] = jitterY;
            result[3][2] = b;
            result[3][3] = 1.0;
            inverseResult[0][0] = 1.0 / x;
            inverseResult[1][1] = 1.0 / signedY;
            inverseResult[2][2] = 1.0 / a;
            inverseResult[3][0] = -jitterX / x;
            inverseResult[3][1] = -jitterY / signedY;
            inverseResult[3][2] = -b / a;
            inverseResult[3][3] = 1.0;
        }

        Math::Mat4 candidate{ 1.0f }, candidateInverse{ 1.0f };
        if (const auto status = ToFloat(result, candidate); !status.IsSuccess()) return status;
        if (const auto status = ToFloat(inverseResult, candidateInverse); !status.IsSuccess()) return status;
        if (!IsInversePair(candidate, candidateInverse))
            return { Math::MathError::Singular, "Projection and inverse do not meet the finite precision residual bound" };
        projection = candidate;
        inverse = candidateInverse;
        return {};
    }

}
