#include "stdafx.h"
#include "StarDemo.h"

demo::StarDemo::StarDemo() : Context((GLfloat)SCR_WIDTH, (GLfloat)SCR_HEIGHT), demoSkybox(skyboxTextures), demoStar(4, 300.0f, 1000, Projection) {
	demoSkybox.BuildRenderData(Projection);
}

void demo::StarDemo::Render(){
	demoSkybox.Render(View);
	demoStar.Render(View, Projection, CameraPosition);
}
