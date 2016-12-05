// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Camera.h"
#include "util\Shader.h"
// Callbacks for GLFW
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void mouseCallback(GLFWwindow* window, double xpos, double ypos);
// Make sure movement can happen constantly with multiple keys at once
void Do_Movement();
// Time change trackers for consistent updates/fps
GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;

// Init camera instance
// Setup camera object
Camera camera;
bool keys[1024];
GLfloat lastX = (GLfloat)SCR_WIDTH / 2, lastY = (GLfloat)SCR_HEIGHT / 2;
bool firstMouse = true;

int main(){

	// Init GLFW 
	glfwInit();

	// Set our options for GLFW
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GL_MULTISAMPLE, 4);

	// Init GLEW to get openGL functions and pointers working
	glewExperimental = GL_TRUE;
	glewInit();

	// Create a GLFWwindow object that we can use for GLFW's functions
	GLFWwindow* MainWindow = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "DiamondDogs", nullptr, nullptr);
	glfwMakeContextCurrent(MainWindow);

	// Set key callbacks for input handling
	glfwSetKeyCallback(MainWindow, keyCallback);
	glfwSetScrollCallback(MainWindow, scrollCallback);
	glfwSetCursorPosCallback(MainWindow, mouseCallback);

	// Set input mode, controlling where cursor can and can't go
	glfwSetInputMode(MainWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Enable depth testing and anti-aliasing
	glEnable(GL_DEPTH_TEST | GL_MULTISAMPLE);

	// Set main viewport 
	glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);


	// Build and compile shaders
	
	Shader CoreGeo("./shaders/core/geometry.glsl", GEOMETRY_SHADER);
	Shader CoreFrag("./shaders/code/fragment.glsl", FRAGMENT_SHADER);
	Shader CoreVert("./shaders/core/vertex.glsl", VERTEX_SHADER);
	ShaderProgram CoreShader;
	CoreShader.AttachShader(CoreVert);
	CoreShader.AttachShader(CoreGeo);
	CoreShader.AttachShader(CoreFrag);
	CoreShader.CompleteProgram();
	std::vector<std::string> Uniforms{
		"normTransform",
		"model",
		"view",
		"projection",
	};
	CoreShader.BuildUniformMap(Uniforms);

	// Build map of required uniforms

	// TODO

	// Prepare Mesh for rendering

	// TODO

	// Setup Skybox

	// TODO

	// Get projection matrix from camera object and pass it to the shaders

	// Main loop
	while (!glfwWindowShouldClose(MainWindow)) {
		// Get actual DT/frametime to compensate for this in movement func
		GLfloat CurrentFrame = (GLfloat)glfwGetTime();
		deltaTime = CurrentFrame - lastFrame;
		lastFrame = CurrentFrame;

		// Event handling
		glfwPollEvents();
		// TODO: Actually handling events (movement)

		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		// Setup view matrix

	}
    return 0;
}


// Is called whenever a key is pressed/released via GLFW
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mode) {
	//cout << key << endl;
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GL_TRUE);
	}
	if (key >= 0 && key < 1024) {
		if (action == GLFW_PRESS)
			keys[key] = true;
		else if (action == GLFW_RELEASE)
			keys[key] = false;
	}
	if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_PRESS) {
		camera.speed += 80;
	}
	if (key == GLFW_KEY_LEFT_SHIFT && action == GLFW_RELEASE) {
		camera.speed -= 80;
	}

	if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_PRESS) {
		camera.speed -= 6;
	}
	if (key == GLFW_KEY_LEFT_CONTROL && action == GLFW_RELEASE) {
		camera.speed += 6;
	}
}

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
	if (firstMouse)
	{
		lastX = (GLfloat)xpos;
		lastY = (GLfloat)ypos;
		firstMouse = false;
	}

	GLfloat xoffset = (GLfloat)xpos - lastX;
	GLfloat yoffset = lastY - (GLfloat)ypos;  // Reversed since y-coordinates go from bottom to left

	lastX = (GLfloat)xpos;
	lastY = (GLfloat)ypos;

	//camera.ProcessMouseMovement(xoffset, yoffset);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
	//camera.ProcessMouseScroll((GLfloat)yoffset);
}

