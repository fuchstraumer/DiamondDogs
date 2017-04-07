#pragma once
#ifndef CONTEXT_H
#define CONTEXT_H
#include "rendering\shader.h"
#include "rendering\camera.h"
#include "../util/lodeTexture.h"
/*
	
	CONTEXT_H

	Defines a single rendering context for viewing imported meshes and models.
	Each mesh has different render modes, and they can be toggled
	independently.

*/

static const glm::vec3 UP(0.0f, 1.0f, 0.0f);

class Context {
public:

	// Performs setup and initialization for a Context object
	Context(GLfloat width, GLfloat height);

	// Activates this Context and runs it until the escape key is pressed
	void Use();

	// GLFW function for processing mouse button actions
	static void MouseButtonCallback(GLFWwindow* window, int button, int code, int mods);

	// GLFW function for updating/getting mouse position for each tick
	static void MousePosCallback(GLFWwindow* window, double mouse_x, double mouse_y);

	// GLFW function for processing mouse or trackpad scrolls
	static void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset);

	// GLFW function for processing keyboard presses
	static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	// Render all objects attached to this Context
	virtual void Render() = 0;

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

	// View matrix
	glm::mat4 View;

	// Projection matrix. Only matrix that should be kept as part of the window, as 
	// the dimensions of the window affect the projection matrix
	glm::mat4 Projection;

	// Camera position
	glm::vec3 CameraPosition;

};

#endif // !Context_H
