#include "catpch.h"
#include "Geometry/Bounds.h"

namespace Catalpa {
    namespace {
        bool Finite(const Math::Vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        // TwoSum 给出 double 加法的舍入残差；只在向区间内部舍入时推进一ULP。
        // 与每步无条件膨胀相比，精确的 float-max 恒等变换仍可正确保留有限端点。
        double AddOutward(double left, double right, bool upper) noexcept
        {
            const double sum = left + right;
            const double recoveredRight = sum - left;
            const double error = (left - (sum - recoveredRight)) + (right - recoveredRight);
            if ((upper && error > 0.0) || (!upper && error < 0.0))
                return std::nextafter(sum, upper ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
            return sum;
        }

        // 将实数结果向包围体外舍入；上溢明确失败，不钳制真实几何。
        bool OutwardFloat(double value, bool upper, float& output) noexcept
        {
            constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value) || value < -limit || value > limit) return false;
            float rounded = static_cast<float>(value);
            if ((upper && static_cast<double>(rounded) < value) || (!upper && static_cast<double>(rounded) > value))
                rounded = std::nextafter(rounded, upper ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
            if (!std::isfinite(rounded)) return false;
            output = rounded;
            return true;
        }
    }

    Math::MathStatus ValidateBounds(const Aabb& bounds) noexcept
    {
        if (!Finite(bounds.minimum) || !Finite(bounds.maximum)) return { Math::MathError::NonFinite, "Bounds contain NaN or infinity" };
        if (!bounds.empty && (bounds.minimum.x > bounds.maximum.x || bounds.minimum.y > bounds.maximum.y || bounds.minimum.z > bounds.maximum.z))
            return { Math::MathError::OutOfRange, "Bounds minimum exceeds maximum" };
        return {};
    }

    Math::MathStatus ComputeBounds(std::span<const Math::Vec3> points, Aabb& output) noexcept
    {
        Aabb candidate;
        for (const auto& point : points) {
            if (!Finite(point)) return { Math::MathError::NonFinite, "Bounds source point is not finite" };
            if (candidate.empty) { candidate.minimum = candidate.maximum = point; candidate.empty = false; }
            else { candidate.minimum = glm::min(candidate.minimum, point); candidate.maximum = glm::max(candidate.maximum, point); }
        }
        output = candidate;
        return {};
    }

    Math::MathStatus MergeBounds(const Aabb& left, const Aabb& right, Aabb& output) noexcept
    {
        if (const auto status = ValidateBounds(left); !status.IsSuccess()) return status;
        if (const auto status = ValidateBounds(right); !status.IsSuccess()) return status;
        Aabb candidate = left.empty ? right : left;
        if (!left.empty && !right.empty) { candidate.minimum = glm::min(left.minimum, right.minimum); candidate.maximum = glm::max(left.maximum, right.maximum); }
        if (candidate.empty) candidate = {};
        output = candidate;
        return {};
    }

    Math::MathStatus TransformBounds(const Aabb& local, const Math::Mat4& world, Aabb& output) noexcept
    {
        if (const auto status = ValidateBounds(local); !status.IsSuccess()) return status;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(world[column][row])) return { Math::MathError::NonFinite, "Bounds transform is not finite" };
        if (world[0][3] != 0.0f || world[1][3] != 0.0f || world[2][3] != 0.0f || world[3][3] != 1.0f)
            return { Math::MathError::NonAffine, "Bounds transform must be affine" };
        Aabb candidate;
        if (!local.empty) {
            candidate.empty = false;
            // 每个输出坐标独立累加区间；正负系数均使用原区间，不能只变换 min/max 两点。
            for (int row = 0; row < 3; ++row) {
                double minimum = world[3][row], maximum = world[3][row];
                for (int column = 0; column < 3; ++column) {
                    const double a = static_cast<double>(world[column][row]) * local.minimum[column];
                    const double b = static_cast<double>(world[column][row]) * local.maximum[column];
                    // float×float 的积在 double 内精确；TwoSum覆盖大平移/抵消的加法舍入。
                    minimum = AddOutward(minimum, std::min(a, b), false);
                    maximum = AddOutward(maximum, std::max(a, b), true);
                }
                if (!OutwardFloat(minimum, false, candidate.minimum[row]) || !OutwardFloat(maximum, true, candidate.maximum[row]))
                    return { Math::MathError::OutOfRange, "Transformed bounds exceed finite float range" };
            }
        }
        output = candidate;
        return {};
    }

    Math::MathStatus ComputeBoundingSphere(const Aabb& bounds, BoundingSphere& output) noexcept
    {
        if (const auto status = ValidateBounds(bounds); !status.IsSuccess()) return status;
        BoundingSphere candidate;
        if (!bounds.empty) {
            candidate.empty = false;
            double squaredRadius = 0.0;
            for (int axis = 0; axis < 3; ++axis) {
                candidate.center[axis] = static_cast<float>((static_cast<double>(bounds.minimum[axis]) + bounds.maximum[axis]) * 0.5);
                const double extent = std::max(AddOutward(candidate.center[axis], -static_cast<double>(bounds.minimum[axis]), true),
                    AddOutward(bounds.maximum[axis], -static_cast<double>(candidate.center[axis]), true));
                double square = extent * extent;
                if (std::fma(extent, extent, -square) > 0.0) square = std::nextafter(square, std::numeric_limits<double>::infinity());
                squaredRadius = AddOutward(squaredRadius, square, true);
            }
            double radius = std::sqrt(squaredRadius);
            if (std::fma(radius, radius, -squaredRadius) < 0.0) radius = std::nextafter(radius, std::numeric_limits<double>::infinity());
            if (!OutwardFloat(radius, true, candidate.radius))
                return { Math::MathError::OutOfRange, "Bounding sphere radius exceeds finite float range" };
        }
        output = candidate;
        return {};
    }
}
