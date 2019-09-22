#pragma once
#ifndef TEST_FIXTURE_FPS_CAMERA_HPP
#define TEST_FIXTURE_FPS_CAMERA_HPP
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

struct CameraController
{
    static void UpdateMovement() noexcept;
    static inline float MovementSpeed{ 10.0f };
};

struct PerspectiveCamera
{

    static PerspectiveCamera& Get();

    void Initialize(float fov, float near_plane, float far_plane, glm::vec3 pos, glm::vec3 dir);
    void LookAt(const glm::vec3& dir, const glm::vec3& up, const glm::vec3& position);

    float FarPlane() const noexcept;
    float NearPlane() const noexcept;
    float FOV() const noexcept;
    const glm::quat& Orientation() const noexcept;
    const glm::vec3& Position() const noexcept;
    const glm::mat4& ProjectionMatrix() const noexcept;
    const glm::mat4& ViewMatrix() const noexcept;
    glm::vec3 FrontVector() const noexcept;
    glm::vec3 RightVector() const noexcept;
    glm::vec3 UpVector() const noexcept;

    void SetOrientation(glm::quat _orientation);
    void SetPosition(glm::vec3 _pos) noexcept;
    void SetNearPlane(float near_plane) noexcept;
    void SetFarPlane(float far_plane) noexcept;
    void SetFOV(float fov) noexcept;

    static void UpdateMouseMovement();

private:

    float fovY{ 0.50f * glm::half_pi<float>() };
    float zNear{ 0.1f };
    float zFar{ 3000.0f };
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
    mutable glm::mat4 projection{};
    mutable glm::mat4 view{};

};

#endif //!TEST_FIXTURE_FPS_CAMERA_HPP
