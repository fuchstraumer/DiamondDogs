#include "stdafx.h"
#include "engine\Context.h"
#include "bodies\Terrestrial.h"

int main() {
	Context MainWindow(static_cast<GLfloat>(SCR_WIDTH), static_cast<GLfloat>(SCR_HEIGHT));
	MainWindow.Use();
	return 0;
}