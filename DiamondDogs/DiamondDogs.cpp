// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Shader.h"
#include "util\Viewport.h"
#include "mesh\SpherifiedCube.h"

int main(){
	
	SpherifiedCube test(12);
	test.Spherify();
	test.Model = glm::scale(test.Model, glm::vec3(10.0f));
	Viewport MainWindow(SCR_WIDTH, SCR_HEIGHT);
	RenderObject testObj(test, MainWindow.CoreProgram);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINES);
	MainWindow.AddRenderObject(testObj, "core");

	MainWindow.Use();
	
    return 0;
}

