#include "../stdafx.h"
#include "Camera.h"
#include "glm/ext.hpp"
const float NEAR_PLANE = 0.1f, FAR_PLANE = 2000.0f;

const float MOUSE_SENSITIVITY = 0.250f;

glm::vec3 Camera::UP = glm::vec3(0.0f, 1.0f, 0.0f);

void Camera::SetupProjection(const float &FOV, const float &ASPECT_RATIO){
	projection = glm::perspective(FOV, ASPECT_RATIO, NEAR_PLANE, FAR_PLANE);
	fov = FOV;
	aspectRatio = ASPECT_RATIO;
}

void Camera::Update(const float & dt){
	glm::mat4 rot = GetRotationMatrix(yaw, pitch, roll);
	position += translation;
	// Reset translation since we have updated
	translation = glm::vec3(0.0f);
	// Re-init lookdir using our current rotation
	lookDir = glm::vec3(rot*glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
	glm::vec3 tgt = position + lookDir;
	up = glm::vec3(rot*glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
	right = glm::cross(lookDir, up);
	// Update view matrix
	view = glm::lookAt(position, tgt, up);
}

void Camera::UpdateRotation(const float &Yaw, const float &Pitch, const float &Roll){
	rotation = GetRotationMatrix(Yaw, Pitch, Roll); 
	yaw = Yaw; pitch = Pitch; roll = Roll;
	//std::cerr << "rotation: ( " << yaw << ", " << pitch << ", " << roll << " )" << std::endl;
}

const glm::mat4 Camera::GetViewMatrix(void) const{
	return view;
}

const glm::mat4 Camera::GetProjMatrix(void) const{
	return projection;
}

void Camera::SetPosition(const glm::vec3 & p){
	position = p;
}

const glm::vec3 Camera::GetPosition() const{
	return position;
}

glm::mat4 Camera::GetRotationMatrix(const float & yaw, const float & pitch, const float & roll) const{
	glm::mat4 tmp(1.0f);

	tmp = glm::rotate(tmp, roll, glm::vec3(0, 0, 1));
	tmp = glm::rotate(tmp, yaw, glm::vec3(0, 1, 0));
	tmp = glm::rotate(tmp, pitch, glm::vec3(1, 0, 0));

	return tmp;
}

void Camera::SetFOV(const float & FOV){
	fov = FOV;
}

GLfloat Camera::GetFOV() const{
	return fov;
}

void Camera::SetAspect(const float & aspect_ratio){
	aspectRatio = aspect_ratio;
}

GLfloat Camera::GetAspect() const{
	return aspectRatio;
}

void Camera::Translate(const MovementDir &dir, const GLfloat &t){
	if (dir == MovementDir::FORWARD || dir == MovementDir::BACKWARD) {
		translation += (t * lookDir);
	}
	else if (dir == MovementDir::UP || dir == MovementDir::DOWN) {
		translation += (t * up);
	}
	else if (dir == MovementDir::LEFT || dir == MovementDir::RIGHT) {
		translation += (t * right);
	}
	std::cerr << "translation:" << glm::to_string(translation) << std::endl;
}

glm::vec3 Camera::GetTranslation() const{
	return translation;
}

void Camera::ProcessMouseMovement(GLfloat xoffset, GLfloat yoffset, GLfloat zoffset){
	xoffset *= MOUSE_SENSITIVITY;
	yoffset *= MOUSE_SENSITIVITY;
	zoffset *= MOUSE_SENSITIVITY;

	this->yaw += xoffset;
	this->pitch += yoffset;
	this->roll += zoffset;

	if (constrainYaw) {
		if (yaw > 89.0f) {
			yaw = 89.0f;
		}
		if (yaw < -89.0f) {
			yaw = -89.0f;
		}
	}

	if (constrainPitch) {
		if (pitch > 89.0f) {
			pitch = 89.0f;
		}
		if (pitch < -89.0f) {
			pitch = -89.0f;
		}
	}

	if (constrainRoll) {
		if (roll > 89.0f) {
			roll = 89.0f;
		}
		if (roll < -89.0f) {
			roll = -89.0f;
		}
	}

	UpdateRotation(yaw, pitch, roll);
}


