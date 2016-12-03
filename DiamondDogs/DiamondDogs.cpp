// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\camera.h"
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
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
bool keys[1024];
GLfloat lastX = (GLfloat)SCR_WIDTH / 2, lastY = (GLfloat)SCR_HEIGHT / 2;
bool firstMouse = true;

int main(){

	glewInit();
    return 0;
}

