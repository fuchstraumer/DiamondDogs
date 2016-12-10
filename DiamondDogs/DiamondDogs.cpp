// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Shader.h"
#include "util\Context.h"
#include "mesh\SpherifiedCube.h"

int main(){
	Context MainWindow(static_cast<GLfloat>(SCR_WIDTH), static_cast<GLfloat>(SCR_HEIGHT));
	SpherifiedCube test(128);
	test.Spherify();
	for (auto&& face : test.Faces) {
		face.Model = glm::scale(face.Model, glm::vec3(100.0f));
		face.BuildRenderData();
		RenderObject testObj = MainWindow.CreateRenderObject(std::move(face), MainWindow.WireframeProgram);
		MainWindow.AddRenderObject(std::move(testObj));
	}
	MainWindow.Use();
	
    return 0;
}

