#include "stdafx.h"
#include "StarDemo.h"

demo::StarDemo::StarDemo() : Context(SCR_WIDTH, SCR_HEIGHT), demoSkybox(skyboxTextures), demoStar(2, 100.0f, 5000, Projection) {
	demoSkybox.BuildRenderData(Projection);
	demoStar.BuildCorona(glm::vec3(0.0f, 0.0f, 0.0f), demoStar.Radius, Projection);
}

void demo::StarDemo::Render(){
	demoSkybox.Render(View);
	demoStar.Render(View, Projection, CameraPosition);
}
