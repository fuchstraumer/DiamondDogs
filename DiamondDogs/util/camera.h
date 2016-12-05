#pragma once
#ifndef CAMERA_H
#define CAMERA_H

/*
	CAMERA_H

	Defines base methods and members needed for any of the camera-type objects.
	Most methods can be overridden.

*/

enum class MovementDir {
	FORWARD,
	BACKWARD,
	UP, 
	DOWN,
	LEFT,
	RIGHT,
};

class Camera {
public:

	Camera() = default;
	~Camera() = default;
	
	// Sets up the projection matrix for this camera object, assuming the given FOV and aspect ratio 
	// by default. Can be overridden in case different cameras need different FOV's
	virtual void SetupProjection(const float &FOV = 80.0f, const float &ASPECT_RATIO = (16 / 9));
	
	// Update methods
	virtual void Update(const float& dt);
	// Update rotation alone - roll isn't used for many camera types
	virtual void UpdateRotation(const float &yaw, const float &pitch, const float &roll = 0.0f);

	// Returns view matrix for this camera object
	const glm::mat4 GetViewMatrix(void) const;
	// Returns projection matrix for this camera object
	const glm::mat4 GetProjMatrix(void) const;

	// Sets raw position
	virtual void SetPosition(const glm::vec3 &p);
	// Gets raw position
	virtual const glm::vec3 GetPosition() const;

	// Gets a rotation matrix
	virtual glm::mat4 GetRotationMatrix(const float& yaw, const float& pitch, const float& roll = 0.0f) const;

	// Sets the FOV, updates projection matrix
	virtual void SetFOV(const float& FOV);
	// Gets the current FOV
	virtual GLfloat GetFOV() const;

	// Sets the aspect ratio, updates projection matrix
	virtual void SetAspect(const float& aspect_ratio);
	// Gets the current aspect ratio
	virtual GLfloat GetAspect() const;

	// Translate the vector by the given amount in
	virtual void Translate(const MovementDir& dir, const GLfloat &t);
	// Returns the current translation for this camera
	virtual glm::vec3 GetTranslation() const;

	// Process mouse movement from GLFW functions
	virtual void ProcessMouseMovement(GLfloat xoffset, GLfloat yoffset, GLfloat zoffset = 0.0f);

	float speed;

protected:

	GLfloat fov, aspectRatio;
	// Defined based on world-up. Should be
	// same across all instances of camera
	static glm::vec3 UP;
	// "looking" direction - also usually "forward"
	glm::vec3 lookDir;
	// World-Right vec
	glm::vec3 right;
	// World pos
	glm::vec3 position;
	// Up for this camera currently
	glm::vec3 up;
	// Rotation matrix
	glm::mat4 rotation;
	// Individual elements of the rotation
	GLfloat yaw, pitch, roll;

	// View matrix
	glm::mat4 view;

	// Projection matrix
	glm::mat4 projection;

	// Vector used to update the position on a timestep
	glm::vec3 translation;

	// Pitch constrain booleans
	bool constrainYaw, constrainPitch, constrainRoll;
};
#endif // !CAMERA_H
