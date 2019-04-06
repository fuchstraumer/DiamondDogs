#pragma once
#ifndef TEST_FIXTURE_FPS_CAMERA_HPP
#define TEST_FIXTURE_FPS_CAMERA_HPP
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/quaternion.hpp"

struct PerspectiveCamera {

    static PerspectiveCamera& Get();

	void LookAt(const glm::vec3& dir, const glm::vec3& up, const glm::vec3& position);

    float FarPlane() const noexcept;
    float NearPlane() const noexcept;
    float FOV() const noexcept;
    const glm::quat& Orientation() const noexcept;
    const glm::vec3& Position() const noexcept;
    const glm::mat4& ProjectionMatrix() const noexcept;
    const glm::mat4& ViewMatrix() const noexcept;

    void SetOrientation(glm::quat _orientation);

    static void UpdateMouseMovement();

private:

    void updateProjection() const;
    void updateView() const;

    float fovY{ 0.50f * glm::half_pi<float>() };
    float zNear{ 0.1f };
    float zFar{ 3000.0f };
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
    mutable glm::mat4 projection{};
    mutable glm::mat4 view{};
    mutable bool projectionUpdated{ false };
    mutable bool viewUpdated{ false };

};

#endif //!TEST_FIXTURE_FPS_CAMERA_HPP
