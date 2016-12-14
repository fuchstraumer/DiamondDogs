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
	terrainGen = NoiseGenerator(2495);
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

	BuildTerrain();
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

void Terrestrial::BuildTerrain(){
	// We will map 2D coherent noise values to a sphere, similar to how we did it previously.
	for (auto&& f : Mesh.Faces) {
		float maxHeight, minHeight;
		maxHeight = 1.05f;
		float max = 0.74f;
		minHeight = 0.95f;
		for (unsigned int i = 0; i < f.Vertices.size(); ++i) {
			auto&& pos = f.Vertices[i].Position;
			// Decide if terrain is landmass or something else
			float noise = terrainGen.SimplexBillow_3DBounded(pos.x, pos.y, pos.z, 0.0f, 0.95f, 0.5f, 12, 1.295f, 1.0f);
			noise = easeCurve(noise);
			// Terrain is a landmass
			
			if (noise > LANDMASS_PREVALENCE) {
				f.Vertices[i].Color = glm::vec3(0.1f, 0.9f, 0.3f);
				float scale = (noise - LANDMASS_PREVALENCE) / (max - LANDMASS_PREVALENCE);
				float h = terrainGen.SimplexRidged_3DBounded(pos.x, pos.y, pos.z, 0.95f, 1.05f);
				h *= scale;
				h = glm::clamp(h, 0.95f, 1.05f);
				if (h >= 1.04f) {
					f.Vertices[i].Color = glm::vec3(0.3f, 0.95f, 0.35f);
				}
				else if (h <= 0.951f) {
					f.Vertices[i].Color = glm::vec3(0.96f, 0.870f, 0.701f);
				}
			}
			// Terrain is oceanic
			else {

				f.Vertices[i].Color = glm::vec3(0.0f, 0.1f, 0.95f);
			}
		
		}
	}
}

void Terrestrial::Render(const glm::mat4 & view, const glm::mat4 & projection, const glm::vec3 & camera_pos){
	MainShader.Use();
	glUniformMatrix4fv(MainShader.GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(MainShader.GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(projection));
	
	for (auto&& face : Mesh.Faces) {
		face.Render(MainShader);
	}
}
