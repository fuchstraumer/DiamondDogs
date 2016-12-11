#pragma once
#ifndef CONTEXT_H
#define CONTEXT_H
#include "shader.h"
#include "camera.h"
#include "../mesh/Mesh.h"
#include "../mesh/Skybox.h"
#include "../util/lodeTexture.h"
#include "../util/Framebuffer.h"
/*
	
	CONTEXT_H

	Defines a single rendering context for viewing imported meshes and models.
	Each mesh has different render modes, and they can be toggled
	independently.

*/

static const std::vector<std::string> skyboxTextures{
	"./rsrc/img/skybox/right.png",
	"./rsrc/img/skybox/left.png",
	"./rsrc/img/skybox/top.png",
	"./rsrc/img/skybox/bottom.png",
	"./rsrc/img/skybox/front.png",
	"./rsrc/img/skybox/back.png",
};

static const glm::vec3 UP(0.0f, 1.0f, 0.0f);

// A renderable object consists of a mesh (mesh being an ABC), and the shader for rendering it
// Using reference wrapper means we can insert references into a container, yay!
using RenderObject = std::pair<Mesh, std::reference_wrapper<ShaderProgram>>;
// The map then has a key that is the "alias" for a shader so to speak, and a value that is a renderable object
// This is a multimap, so we can have entries that have the same key but different values, or in this case: same shader, different meshes
using RenderObjContainer = std::vector<RenderObject>;
// Because I'm too lazy to type that pair out, just defining an alias for it here too
//using RenderMapEntry = std::pair<std::string, RenderObject>;

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
	void Render();

	// Add an object to the renderable list
	void AddRenderObject(const RenderObject obj);

	// Create a renderobject and return it
	RenderObject CreateRenderObject(const Mesh& mesh, ShaderProgram& shader) {
		RenderObject res(std::cref(mesh), std::ref(shader));
		return res;
	}

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
	RenderObjContainer RenderObjects;

	// Main shader
	ShaderProgram CoreProgram;

	// Shader for wireframe: highlights vertices, if supported
	ShaderProgram WireframeProgram;

	// View matrix
	glm::mat4 View;

	// Projection matrix
	glm::mat4 Projection;

private:
	ShaderProgram skyboxProgram;
	// List of objects kept in the map
	std::vector<std::string> shaderNames;
	// Keeps track of active texture count so we know which texture unit to use
	GLuint texCount;
	// Framebuffer used for this scene
	Framebuffer sceneFBO;
};

#endif // !Context_H
