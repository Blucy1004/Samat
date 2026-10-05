#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace jm {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Matrix4 {
    std::array<float, 16> values{};

    static Matrix4 identity() {
        Matrix4 result{};
        result.values[0] = result.values[5] = result.values[10] = result.values[15] = 1.0F;
        return result;
    }

    static Matrix4 translation(float x, float y, float z) {
        Matrix4 result = identity();
        result.values[12] = x;
        result.values[13] = y;
        result.values[14] = z;
        return result;
    }

    static Matrix4 scale(float x, float y, float z) {
        Matrix4 result{};
        result.values[0] = x;
        result.values[5] = y;
        result.values[10] = z;
        result.values[15] = 1.0F;
        return result;
    }

    static Matrix4 rotationX(float angle) {
        Matrix4 result = identity();
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        result.values[5] = cosine;
        result.values[6] = sine;
        result.values[9] = -sine;
        result.values[10] = cosine;
        return result;
    }

    static Matrix4 rotationY(float angle) {
        Matrix4 result = identity();
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        result.values[0] = cosine;
        result.values[2] = -sine;
        result.values[8] = sine;
        result.values[10] = cosine;
        return result;
    }

    static Matrix4 rotationZ(float angle) {
        Matrix4 result = identity();
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        result.values[0] = cosine;
        result.values[1] = sine;
        result.values[4] = -sine;
        result.values[5] = cosine;
        return result;
    }

    static Matrix4 perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane) {
        Matrix4 result{};
        const float focalLength = 1.0F / std::tan(verticalFovRadians * 0.5F);
        result.values[0] = focalLength / aspect;
        result.values[5] = focalLength;
        result.values[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
        result.values[11] = -1.0F;
        result.values[14] = (2.0F * farPlane * nearPlane) / (nearPlane - farPlane);
        return result;
    }

    static Matrix4 orthographic(float left, float right, float bottom, float top) {
        Matrix4 result = identity();
        result.values[0] = 2.0F / (right - left);
        result.values[5] = 2.0F / (top - bottom);
        result.values[10] = -1.0F;
        result.values[12] = -(right + left) / (right - left);
        result.values[13] = -(top + bottom) / (top - bottom);
        return result;
    }

    static Matrix4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        const Vec3 forward = normalize({target.x - eye.x, target.y - eye.y, target.z - eye.z});
        const Vec3 side = normalize(cross(forward, up));
        const Vec3 correctedUp = cross(side, forward);

        Matrix4 result = identity();
        result.values[0] = side.x;
        result.values[4] = side.y;
        result.values[8] = side.z;
        result.values[1] = correctedUp.x;
        result.values[5] = correctedUp.y;
        result.values[9] = correctedUp.z;
        result.values[2] = -forward.x;
        result.values[6] = -forward.y;
        result.values[10] = -forward.z;
        result.values[12] = -dot(side, eye);
        result.values[13] = -dot(correctedUp, eye);
        result.values[14] = dot(forward, eye);
        return result;
    }

    friend Matrix4 operator*(const Matrix4& left, const Matrix4& right) {
        Matrix4 result{};
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                for (int index = 0; index < 4; ++index) {
                    result.values[static_cast<std::size_t>(column * 4 + row)] +=
                        left.values[static_cast<std::size_t>(index * 4 + row)] *
                        right.values[static_cast<std::size_t>(column * 4 + index)];
                }
            }
        }
        return result;
    }

private:
    static Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    static float dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vec3 normalize(Vec3 value) {
        const float length = std::sqrt(dot(value, value));
        return {value.x / length, value.y / length, value.z / length};
    }
};

} // namespace jm
