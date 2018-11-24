#include "PerspectiveCamera.hpp"
#include "RenderingContext.hpp"
#include "Swapchain.hpp"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtx/rotate_normalized_axis.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "imgui/imgui.h"

inline glm::quat rotate_vec(glm::vec3 from, glm::vec3 to) {
    from = glm::normalize(from);
    to = glm::normalize(to);

    const float cos_angle = glm::dot(from, to);
    if (abs(cos_angle) > 0.99999f) {
        if (cos_angle > 0.99999f) {
            return glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f };
        }
        else {
            glm::vec3 rot = glm::cross(glm::vec3{ 1.0f,0.0f,0.0f }, from);
            if (glm::dot(rot, rot) > 0.001f) {
                rot = glm::normalize(rot);
            }
            else {
                rot = glm::normalize(glm::cross(glm::vec3{ 0.0f,1.0f,0.0f }, from));
            }
            return glm::quat(0.0f, rot);
        }
    }

    glm::vec3 rot = glm::normalize(glm::cross(from, to));
    glm::vec3 half_vec = glm::normalize(from + to);
    float half_cos_range = glm::clamp(glm::dot(half_vec, from), 0.0f, 1.0f);
    float sin_half_angle = sqrtf(1.0f - half_cos_range * half_cos_range);
    return glm::quat{ half_cos_range, rot * sin_half_angle };
}

inline glm::quat rotate_vec_axis(glm::vec3 from, glm::vec3 to, glm::vec3 axis) {
    axis = glm::normalize(axis);
    from = glm::normalize(glm::cross(axis, from));
    to = glm::normalize(glm::cross(axis, to));

    if (glm::dot(to, from) < -0.9999f) {
        return glm::quat{ 0.0f, axis };
    }

    float quat_sign = glm::sign(glm::dot(axis, glm::cross(from, to)));
    glm::vec3 half_vec = glm::normalize(from + to);
    float cos_half_range = glm::clamp(glm::dot(half_vec, from), 0.0f, 1.0f);
    float sin_half_range = quat_sign * sqrtf(1.0f - cos_half_range * cos_half_range);
    return glm::quat{ cos_half_range, axis * sin_half_range };
}

inline glm::quat look_at(const glm::vec3& dir, const glm::vec3& up) {
    static const glm::vec3 z{ 0.0f, 0.0f, 1.0f };
    static const glm::vec3 y{ 0.0f, 1.0f, 0.0f };
    glm::vec3 norm_dir = glm::normalize(dir);
    glm::vec3 right = glm::cross(norm_dir, up);
    glm::vec3 actual_up = glm::cross(right, norm_dir);
    glm::quat look_transform = rotate_vec(norm_dir, z);
    glm::quat up_transform = rotate_vec_axis(look_transform * actual_up, y, z);
    return glm::quat{ up_transform * look_transform };
}

PerspectiveCamera& PerspectiveCamera::Get() {
    static PerspectiveCamera camera;
    return camera;
}

float PerspectiveCamera::FarPlane() const noexcept {
    return zFar;
}

float PerspectiveCamera::NearPlane() const noexcept {
    return zNear;
}

float PerspectiveCamera::FOV() const noexcept {
    return fovY;
}

const glm::quat & PerspectiveCamera::Orientation() const noexcept {
    return orientation;
}

const glm::vec3 & PerspectiveCamera::Position() const noexcept {
    return position;
}

const glm::mat4& PerspectiveCamera::ProjectionMatrix() const noexcept {
    if (!projectionUpdated) {
        updateProjection();
    }
    return projection;
}

const glm::mat4& PerspectiveCamera::ViewMatrix() const noexcept {
    if (!viewUpdated) {
        updateView();
    }
    return view;
}

void PerspectiveCamera::SetOrientation(glm::quat _orientation) {
    orientation = std::move(_orientation);
    viewUpdated = false;
}

void PerspectiveCamera::UpdateMouseMovement() {
    PerspectiveCamera& instance = Get();
    const ImGuiIO& io = ImGui::GetIO();
    const float& dx = io.MouseDelta.x;
    const float& dy = io.MouseDelta.y; 
    glm::quat pitch = glm::angleAxis(dy * 0.02f, glm::vec3{ 1.0f, 0.0f, 0.0f });
    glm::quat yaw = glm::angleAxis(dx * 0.02f, glm::vec3{ 0.0f, 1.0f, 0.0f });
    instance.SetOrientation(glm::quat{ glm::normalize(pitch * instance.Orientation() * yaw) });
}

void PerspectiveCamera::updateProjection() const {
    auto& ctxt = RenderingContext::Get();
    const float width = static_cast<float>(ctxt.Swapchain()->Extent().width);
    const float height = static_cast<float>(ctxt.Swapchain()->Extent().height);
    projection = glm::perspectiveFov(fovY, width, height, zNear, zFar);
    projectionUpdated = true;
}

void PerspectiveCamera::updateView() const {
    view = glm::mat4_cast(orientation) * glm::translate(-position);
    viewUpdated = true;
}
