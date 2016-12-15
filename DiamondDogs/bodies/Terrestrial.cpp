#include "../stdafx.h"
#include "Terrestrial.h"

// Light Variables
static const glm::vec4 lightColor(1.0f, 1.0f, 0.98f, 1.0f);
static const glm::vec4 lightPosition(130.0f, 120.0f, 100.0f, 1.0f);

static constexpr float pi = 3.14159265358979f;

Terrestrial::Terrestrial(float radius, double mass, int LOD, float atmo_density) : Body() {
	Radius = radius;
	Mass = mass;
	// Set up the main "ground" mesh and shaders

	Mesh = SpherifiedCube(LOD);
	MainShader.Init();
	Shader terrVert("./shaders/wireframe/vertex.glsl", VERTEX_SHADER);
	Shader terrGeo("./shaders/wireframe/geometry.glsl", GEOMETRY_SHADER);
	Shader terrFrag("./shaders/wireframe/fragment.glsl", FRAGMENT_SHADER);
	MainShader.AttachShader(terrVert);
	MainShader.AttachShader(terrGeo);
	MainShader.AttachShader(terrFrag);
	MainShader.CompleteProgram();

	std::vector<std::string> uniforms{
		"model",
		"view",
		"projection",
		"normTransform",
		"lightPos",
		"lightColor",
	};
	
	MainShader.BuildUniformMap(uniforms);
	// Set up atmosphere 
	glUniform3f(MainShader.GetUniformLocation("lightPos"), lightPosition.x, lightPosition.y, lightPosition.z);
	glUniform3f(MainShader.GetUniformLocation("lightColor"), lightColor.x, lightColor.y, lightColor.z);

	atmoRadius = 1.30f * Radius;
	atmoDensity = atmo_density;

	// Set the uniform attributes
	Mesh.Spherify();
	for (auto&& face : Mesh.Faces) {
		face.Scale = glm::vec3(Radius);
		face.Position = glm::vec3(0.0f);
		face.BuildRenderData();
		face.Vertices.shrink_to_fit();
		face.Indices.shrink_to_fit();
		face.Triangles.shrink_to_fit();
	}

}

void Terrestrial::SetDiffuseColor(const glm::vec3 & color){
	surfaceDiffuse = glm::vec4(color.x, color.y, color.z, 1.0f);
}

glm::vec3 Terrestrial::GetDiffuseColor() const{
	return atmoSpectrum.xyz;
}

float Terrestrial::GetAtmoRadius() const{
	return atmoRadius;
}

void Terrestrial::SetAtmoRadius(const float & rad){
	atmoRadius = rad;
}

float Terrestrial::GetAtmoDensity() const{
	return atmoDensity;
}

void Terrestrial::SetAtmoDensity(const float & density){
	atmoDensity = density;
}

void Terrestrial::SetAtmoColor(const glm::vec4 & new_spectrum){
	atmoSpectrum = new_spectrum;
}

glm::vec4 Terrestrial::GetAtmoColor() const{
	return atmoSpectrum;
}

__inline float fade(float f) {
	return f*f*f*(f*(f * 6.0f - 15.0f) + 10.0f);
}

/*
	Terrain building for terrestrial planets:

	1. Build the continents with a simple low-freq billow noise func.


*/

__inline float easeCurve(float a)
{
	float a3 = a * a * a;
	float a4 = a3 * a;
	float a5 = a4 * a;
	return (6.0f * a5) - (15.0f * a4) + (10.0f * a3);
}

static float LANDMASS_PREVALENCE = 0.71f;



void Terrestrial::Render(const glm::mat4 & view, const glm::mat4 & projection, const glm::vec3 & camera_pos, int texCount){
	MainShader.Use();
	glUniformMatrix4fv(MainShader.GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(MainShader.GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	for (auto&& face : Mesh.Faces) {
		glActiveTexture(GL_TEXTURE0 + texCount);
		face.Render(MainShader);
	}
}
