#pragma once
#ifndef VIEWPORT_H
#define VIEWPORT_H
#include "shader.h"
#include "camera.h"
/*
	
	VIEWPORT_H

	Defines a viewport for viewing imported meshes and models.
	Each mesh has different render modes, and they can be toggled
	independently.

*/

static const glm::vec3 UP(0.0f, 1.0f, 0.0f);

class Viewport {
public:
	// Performs setup and initialization for a viewport object
	Viewport(GLfloat width, GLfloat height);

	// Activates this viewport and runs it until the escape key is pressed
	void Use();

	// GLFW function for processing mouse button actions
	static void MouseButtonCallback(GLFWwindow* window, int button, int code, int mods);

	// GLFW function for updating/getting mouse position for each tick
	static void MousePosCallback(GLFWwindow* window, double mouse_x, double mouse_y);

	// GLFW function for processing mouse or trackpad scrolls
	static void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset);

	// GLFW function for processing keyboard presses
	static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	// This function checks all of the possible keys (1024 in total) for actions and updates each.
	// With just the keycallback, we will be unable to handle multiple movement inputs at once so we need
	// this method in order to do so
	void UpdateMovement();
	
	// Frame times used to compensate for FPS changes
	GLfloat LastFrame, DeltaTime;

	// Width/Height of main window
	GLfloat Width, Height;

	// Pointer to window instance
	GLFWwindow* Window;

	// Objects to be rendered

	// Main camera instance
	static Camera Cam;

	// Main shader
	ShaderProgram CoreProgram;

	// Shader for wireframe: highlights vertices, if supported
	ShaderProgram WireframeProgram;

	// View matrix
	glm::mat4 View;

	// Projection matrix
	glm::mat4 Projection;

private:
	// Tracking of key presses for movement and simultaneous actions
	static bool keys[1024];
	// Previous mouse position
	static GLfloat lastX, lastY;
	// Previous mouse zoom
	static GLfloat lastZoom;

};

#endif // !VIEWPORT_H
