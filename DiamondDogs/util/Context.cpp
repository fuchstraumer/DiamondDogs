#include "../stdafx.h"
#include "Context.h"

// Main camera instance
static Camera Cam(glm::vec3(0.0f, 0.0f, 3.0f));
// Tracking of key presses for movement and simultaneous actions
static bool keys[1024];
// Previous mouse position
static GLfloat lastX = (GLfloat)SCR_WIDTH / 2, lastY = (GLfloat)SCR_HEIGHT / 2;
// say mouse is init
static bool mouseInit = true;
// Previous mouse zoom
static GLfloat lastZoom;
// Skybox textures
static CubemapTexture skyboxTex(skyboxTextures, 2048);
// Skybox itself
static Skybox skybox;

Context::Context(GLfloat width, GLfloat height){
	Width = width;
	Height = height;
	texCount = 0;
	// Init GLFW
	glfwInit();

	

	// Base options

	// Set OpenGL version and profile: 3.3 Compatability
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Don't allow the window to be resize (embedded in UI)
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	// Enable 2x anti-aliasing to soften edges just slightly
	glfwWindowHint(GLFW_SAMPLES, 2);
	
	// Create the actual window instance
	Window = glfwCreateWindow(static_cast<int>(Width), static_cast<int>(Height), "DiamondDogs", nullptr, nullptr);

	// Set the input mode - make the cursor invisible and don't allow it to enter/leave freely
	glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Set callback functions
	glfwSetCursorPosCallback(Window, MousePosCallback);
	glfwSetMouseButtonCallback(Window, MouseButtonCallback);
	glfwSetScrollCallback(Window, ScrollCallback);
	glfwSetKeyCallback(Window, KeyCallback);

	// Make context current so we can continue setting up
	glfwMakeContextCurrent(Window);

	// Init GLEW - requires version from above, profile from above, and hints from above
	GLuint init = glewInit();
	if (init != GLEW_OK) {
		throw("GLEW has not initialized properly");
	}
	glewExperimental = GL_TRUE;
	glEnable(GL_DEPTH_TEST);

	// Setup test terrestrial body
	

	// Set up skybox shaders
	Shader SkyVert("./shaders/skybox/vertex.glsl", VERTEX_SHADER);
	Shader SkyFrag("./shaders/skybox/fragment.glsl", FRAGMENT_SHADER);
	skyboxProgram.Init();
	skyboxProgram.AttachShader(SkyVert);
	skyboxProgram.AttachShader(SkyFrag);
	skyboxProgram.CompleteProgram();
	std::vector<std::string> Uniforms = std::vector<std::string>{
		"projection",
		"view",
	};
	skyboxProgram.BuildUniformMap(Uniforms);

	//sceneFBO = Framebuffer(Width, Height, true);

	// Set viewport
	glViewport(0, 0, static_cast<GLsizei>(Width), static_cast<GLsizei>(Height));
	// Set the clear color - sets default background color
	glClearColor(160.0f / 255.0f, 239.0f / 255.0f, 1.0f, 1.0f);
	// Set projection matrix. This shouldn't really change during runtime.
	Projection = glm::perspective(Cam.Zoom, static_cast<GLfloat>(Width) / static_cast<GLfloat>(Height), 0.1f, 3000.0f);

	skyboxTex.BuildTexture();
	skybox.BuildRenderData();

	TestBody = Terrestrial(100.0f, 2e10, 48);
}

void Context::Use() {
	while (!glfwWindowShouldClose(Window)) {
		glDepthFunc(GL_LESS);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		// Update frame time values
		GLfloat CurrentFrame = (GLfloat)glfwGetTime();
		DeltaTime = CurrentFrame - LastFrame;
		LastFrame = CurrentFrame;

		// Poll events, passing events to callback funcs
		glfwPollEvents();
		UpdateMovement();
		// Clear the depth and color buffers
		
		
		// How to pass in render objects? Standardize a renderable type/class?
		// -- probably, have standard mesh format and vertices already combined with
		// mesh methods to do so. As long as all drawable objects inherit from this, 
		// things should work.

		// Store drawable objects as map, where key is the name of the object and the value is a reference to the object
		// and a reference to the relevant shader program.
		
		glDepthFunc(GL_LEQUAL);

		skyboxProgram.Use();
		GLuint projLoc = skyboxProgram.GetUniformLocation("projection");
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(Projection));
		View = glm::mat4(glm::mat3(Cam.GetViewMatrix()));
		
		GLuint viewloc = skyboxProgram.GetUniformLocation("view");
		glUniformMatrix4fv(viewloc, 1, GL_FALSE, glm::value_ptr(View));
		// Use skybox shader and bind correct texture
		//skyboxProgram.Use();
		glActiveTexture(GL_TEXTURE0 + texCount);
		texCount++;

		skyboxTex.BindTexture();
		skybox.RenderSkybox();

		TestBody.Render(View, Projection, Cam.Position);
		// Before starting loop again, swap buffers (double-buffered rendering)
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glfwSwapBuffers(Window);

	}
	glfwTerminate();
}



void Context::MouseButtonCallback(GLFWwindow * window, int button, int action, int mods){
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		// Mouse down, begin to track movement for dragging
	}
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		// Mouse up, finish drag motion
	}
}

void Context::MousePosCallback(GLFWwindow * window, double mouse_x, double mouse_y){
	if (mouseInit) {
		lastX = static_cast<GLfloat>(mouse_x);
		lastY = static_cast<GLfloat>(mouse_y);
		mouseInit = false;
	}
	// Keep track of mouse position
	// Object picking?
	GLfloat xoffset = static_cast<GLfloat>(mouse_x) - lastX;
	GLfloat yoffset = static_cast<GLfloat>(mouse_y) - lastY;

	lastX = static_cast<GLfloat>(mouse_x);
	lastY = static_cast<GLfloat>(mouse_y);

	Cam.ProcessMouseMovement(xoffset, -yoffset);
}

void Context::ScrollCallback(GLFWwindow * window, double x_offset, double y_offset){
	// scroll_amount is movement along the y-axis
	double scrollAmount = y_offset;
	// change the camera's zoom based on scrollAmount
}

void Context::KeyCallback(GLFWwindow * window, int key, int scancode, int action, int mods){
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GL_TRUE);
	}
	if (key == GLFW_KEY_LEFT_ALT && action == GLFW_PRESS) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	if (key == GLFW_KEY_LEFT_ALT && action == GLFW_RELEASE) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
	// Grab all keys pressed at a given instance and set the appropriate value to true 
	if (key >= 0 && key < 1024) {
		if (action == GLFW_PRESS) {
			keys[key] = true;
		}
		else if (action == GLFW_RELEASE) {
			keys[key] = false;
		}
	}
}

void Context::UpdateMovement(){
	if (keys[GLFW_KEY_W]) {
		Cam.ProcessKeyboard(FORWARD, DeltaTime);
		std::cerr << "W key pressed" << std::endl;
	}
	if (keys[GLFW_KEY_S]) {
		Cam.ProcessKeyboard(BACKWARD, DeltaTime);
		std::cerr << "S key pressed" << std::endl;
	}
	if (keys[GLFW_KEY_A]) {
		Cam.ProcessKeyboard(LEFT, DeltaTime);
		std::cerr << "A key pressed" << std::endl;
	}
	if (keys[GLFW_KEY_D]) {
		Cam.ProcessKeyboard(RIGHT, DeltaTime);
		std::cerr << "D key pressed" << std::endl;
	}
}


void Context::Render() {
	for (auto obj : RenderObjects) {
		auto&& mesh = obj.first;
		auto&& shader = obj.second.get();
		mesh.Render(shader);
	}
}

void Context::AddRenderObject(const RenderObject obj) {
	RenderObjects.push_back(obj);
}
