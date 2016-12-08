// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Camera.h"
#include "util\Shader.h"
#include "util\Viewport.h"
#include "mesh\PlanarMesh.h"

int main(){
	
	PlanarMesh test(4, CardinalFace::BOTTOM);
	test.SubdivideForSphere();
	test.ToUnitSphere();
	
	Viewport MainWindow(SCR_WIDTH, SCR_HEIGHT);
	RenderObject testObj(test, MainWindow.CoreProgram);

	MainWindow.AddRenderObject(testObj, "core");

	MainWindow.Use();
	
    return 0;
}

