#include "PerspectiveCamera.hpp"
#include "RenderingContext.hpp"
#include "Swapchain.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtx/rotate_normalized_axis.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "imgui/imgui.h"
#include "GLFW/glfw3.h"

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
    static const glm::vec3 z{ 0.0f, 0.0f,-1.0f };
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

void PerspectiveCamera::Initialize(float fov, float near_plane, float far_plane, glm::vec3 pos, glm::vec3 dir)
{
    fovY = fov;
    zNear = near_plane;
    zFar = far_plane;
    position = std::move(pos);
    const static glm::vec3 upVector = glm::vec3(0.0f, 1.0f, 0.0f);
    orientation = look_at(dir, upVector);
}

void PerspectiveCamera::LookAt(const glm::vec3& dir, const glm::vec3& up, const glm::vec3& _position)
{
    orientation = look_at(dir, up);
    position = _position;
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

    auto& ctxt = RenderingContext::Get();
    const float width = static_cast<float>(ctxt.Swapchain()->Extent().width);
    const float height = static_cast<float>(ctxt.Swapchain()->Extent().height);
    projection = glm::perspectiveFov(fovY, width, height, zNear, zFar);
    return projection;
}

const glm::mat4& PerspectiveCamera::ViewMatrix() const noexcept {
    view = glm::mat4_cast(orientation) * glm::translate(-position);
    return view;
}

glm::vec3 PerspectiveCamera::FrontVector() const noexcept
{
    static const glm::vec3 zDir{ 0.0f, 0.0f, -1.0f };
    return glm::conjugate(orientation) * zDir;
}

glm::vec3 PerspectiveCamera::RightVector() const noexcept
{
    static const glm::vec3 rightDir{ 1.0f, 0.0f, 0.0f };
    return glm::conjugate(orientation) * rightDir;
}

glm::vec3 PerspectiveCamera::UpVector() const noexcept
{
    static const glm::vec3 upDir{ 0.0f, 1.0f, 0.0f };
    return glm::conjugate(orientation) * upDir;
}

void PerspectiveCamera::SetOrientation(glm::quat _orientation) {
    orientation = std::move(_orientation);
}

void PerspectiveCamera::SetPosition(glm::vec3 _pos) noexcept
{
    position = std::move(_pos);
}

void PerspectiveCamera::SetNearPlane(float near_plane) noexcept
{
    zNear = std::move(near_plane);
}

void PerspectiveCamera::SetFarPlane(float far_plane) noexcept
{
    zFar = std::move(far_plane);
}

void PerspectiveCamera::SetFOV(float fov) noexcept
{
    fovY = std::move(fov);
}

void PerspectiveCamera::UpdateMouseMovement() {
    PerspectiveCamera& instance = Get();
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || io.KeysDown[GLFW_KEY_LEFT_ALT])
    {
        return;
    }
    const float& dx = io.MouseDelta.x;
    const float& dy = io.MouseDelta.y;
    glm::quat pitch = glm::angleAxis(dy * 0.01f, glm::vec3{ 1.0f, 0.0f, 0.0f });
    glm::quat yaw = glm::angleAxis(dx * 0.01f, glm::vec3{ 0.0f, 1.0f, 0.0f });
    instance.SetOrientation(glm::quat{ glm::normalize(pitch * instance.Orientation() * yaw) });
}

void CameraController::UpdateMovement() noexcept
{
    auto& camera = PerspectiveCamera::Get();

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
    {
        return;
    }

    float currSpeed = io.KeysDown[GLFW_KEY_LEFT_SHIFT] ? MovementSpeed * 2.0f : MovementSpeed;
    const glm::vec3 frontVector = camera.FrontVector();
    const glm::vec3 rightVector = camera.RightVector();
    const glm::vec3 upVector = camera.UpVector();

    if (io.KeysDown[GLFW_KEY_W])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * frontVector * io.DeltaTime);
    }

    if (io.KeysDown[GLFW_KEY_S])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * -frontVector * io.DeltaTime);
    }

    if (io.KeysDown[GLFW_KEY_A])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * -rightVector * io.DeltaTime);
    }

    if (io.KeysDown[GLFW_KEY_D])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * rightVector * io.DeltaTime);
    }

    if (io.KeysDown[GLFW_KEY_Q])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * upVector * io.DeltaTime);
    }

    if (io.KeysDown[GLFW_KEY_Z])
    {
        camera.SetPosition(camera.Position() + MovementSpeed * -upVector * io.DeltaTime);
    }

}
