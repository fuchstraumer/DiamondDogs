// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Shader.h"
#include "util\Viewport.h"
#include "mesh\SpherifiedCube.h"

int main(){
	Viewport MainWindow(static_cast<GLfloat>(SCR_WIDTH), static_cast<GLfloat>(SCR_HEIGHT));
	SpherifiedCube test(48);
	test.Spherify();
	for (auto face : test.Faces) {
		face.Model = glm::scale(face.Model, glm::vec3(10.0f));
		face.BuildRenderData();
		RenderObject testObj = MainWindow.CreateRenderObject(std::move(face), MainWindow.CoreProgram);
		MainWindow.AddRenderObject(std::move(testObj));
	}
	MainWindow.Use();
	
    return 0;
}

