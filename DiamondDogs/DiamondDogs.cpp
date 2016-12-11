// DiamondDogs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "util\Shader.h"
#include "util\Context.h"
#include "mesh\SpherifiedCube.h"
#include "bodies\Terrestrial.h"
int main(){
	Context MainWindow(static_cast<GLfloat>(SCR_WIDTH), static_cast<GLfloat>(SCR_HEIGHT));
	Terrestrial test(100.0f, 3e10, 30);
	MainWindow.Use();
	
    return 0;
}

