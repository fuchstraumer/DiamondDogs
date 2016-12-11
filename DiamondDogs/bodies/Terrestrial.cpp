#include "../stdafx.h"
#include "Terrestrial.h"

static const std::vector<std::string> atmoUniforms{
	"model",
	"view",
	"projection",
	"normTransform",
	"cameraPos",
	"lightDir",
	"lightPosition",
	"cameraHeight",
	"cameraHeightSq",
	"invWavelength",
	"innerRadius",
	"innerRadiusSq",
	"outerRadius",
	"outerRadiusSq",
	"KrESun",
	"KmESun",
	"Kr4PI",
	"Km4PI",
	"scale",
	"scaleDepth",
	"scaleOverScaleDepth",
	"g",
	"g2",
	"samples",
};

// Light Variables
static const glm::vec4 lightColor(1.25, 1.25, 1.25, 1.0);
static const glm::vec4 lightPosition(100.0, 0.0, 100.0, 1.0);

static const float pi = 3.14159265358979323846264338327950288f;

Terrestrial::Terrestrial(float radius, double mass, int LOD, float atmo_radius, float atmo_density) : Body() {
	Radius = radius;
	Mass = mass;
	// Set up the main "ground" mesh and shaders

	Mesh = SpherifiedCube(LOD);
	MainShader.Init();
	Shader terrVert("./shaders/terrestrial/vertex.glsl", VERTEX_SHADER);
	Shader terrFrag("./shaders/terrestrial/fragment.glsl", FRAGMENT_SHADER);
	MainShader.AttachShader(terrVert);
	MainShader.AttachShader(terrFrag);
	MainShader.CompleteProgram();
	
	// Set up atmosphere 

	if (atmo_radius == 1.0f) {
		atmoRadius = 1.30f * Radius;
	}
	else {
		atmoRadius = atmo_radius;
	}
	atmoDensity = atmo_density;

	
	atmosphere = IcoSphere(static_cast<unsigned int>(LOD), atmoRadius);
	atmoShader.Init();
	Shader atmoVert("./shaders/atmosphere/vertex.glsl", VERTEX_SHADER);
	Shader atmoFrag("./shaders/atmosphere/fragment.glsl", FRAGMENT_SHADER);
	atmoShader.AttachShader(atmoVert);
	atmoShader.AttachShader(atmoFrag);
	atmoShader.CompleteProgram();
	glm::vec3 initDiffuse = glm::vec3(1.0f, 0.941f, 0.898f);
	SetDiffuseColor(initDiffuse);
	// Build uniform map
	MainShader.BuildUniformMap(atmoUniforms);
	atmoShader.BuildUniformMap(atmoUniforms);
	// Set the uniform attributes

	SetAtmoUniforms(atmoShader);
	SetAtmoUniforms(MainShader);
	
	for (auto&& face : Mesh.Faces) {
		face.BuildRenderData();
	}

	atmosphere.BuildRenderData();

}

void Terrestrial::SetAtmoUniforms(ShaderProgram& shader) {
	shader.Use();
	GLfloat scale = 1.0f / (atmoRadius - Radius);
	GLfloat scaleDepth = 0.25;
	GLfloat scaleOverScaleDepth = scale / scaleDepth;
	GLfloat Kr = 0.0025f;
	GLfloat Km = 0.0010f;
	GLfloat ESun = 16.0f;
	GLfloat g = -0.99f;
	glUniform3f(shader.GetUniformLocation("invWavelength"), 1.0f / powf(0.650f, 4.0f), 1.0f / powf(0.570f, 4.0f), 1.0f / powf(0.475f, 4.0f));
	glUniform1f(shader.GetUniformLocation("innerRadius"), static_cast<GLfloat>(Radius));
	glUniform1f(shader.GetUniformLocation("innerRadiusSq"), static_cast<GLfloat>(powf(Radius, 2.0f)));
	glUniform1f(shader.GetUniformLocation("outerRadius"), static_cast<GLfloat>(atmoRadius));
	glUniform1f(shader.GetUniformLocation("outerRadiusSq"), static_cast<GLfloat>(powf(atmoRadius, 2.0f)));
	glUniform1f(shader.GetUniformLocation("KrESun"), Kr * ESun);
	glUniform1f(shader.GetUniformLocation("KmESun"), Km * ESun);
	glUniform1f(shader.GetUniformLocation("Kr4PI"), Kr * 4.0f * static_cast<GLfloat>(pi));
	glUniform1f(shader.GetUniformLocation("Km4PI"), Km * 4.0f * static_cast<GLfloat>(pi));
	glUniform1f(shader.GetUniformLocation("scale"), scale);
	glUniform1f(shader.GetUniformLocation("scaleDepth"), scaleDepth);
	glUniform1f(shader.GetUniformLocation("g"), g);
	glUniform1f(shader.GetUniformLocation("g2"), g*g);
	glUniform3f(shader.GetUniformLocation("lightPosition"), lightPosition.x, lightPosition.y, lightPosition.z);
	// Amount of samples to take when getting scattering
	glUniform1i(shader.GetUniformLocation("samples"), 4);
	//glUseProgram(0);
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

void Terrestrial::Render(const glm::mat4 & view, const glm::mat4 & projection, const glm::vec3 & camera_pos){
	GLfloat cameraHeight = glm::length(camera_pos);
	glm::vec3 lightPos = glm::normalize(lightPosition.xyz - camera_pos);
	
	MainShader.Use();
	glUniform3f(MainShader.GetUniformLocation("lightDir"), lightPos.x, lightPos.y, lightPos.z);
	glUniform3f(MainShader.GetUniformLocation("cameraPos"), camera_pos.x, camera_pos.y, camera_pos.z);
	glUniform1f(MainShader.GetUniformLocation("cameraHeight"), cameraHeight);
	glUniform1f(MainShader.GetUniformLocation("cameraHeightSq"), cameraHeight*cameraHeight);
	glUniformMatrix4fv(MainShader.GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(MainShader.GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(projection));
	for (auto&& face : Mesh.Faces) {
		glUseProgram(MainShader.Handle);
		face.Render(MainShader);
	}
	// Now render atmosphere sphere
	atmoShader.Use();
	glUniform3f(atmoShader.GetUniformLocation("lightDir"), lightPos.x, lightPos.y, lightPos.z);
	glUniform3f(atmoShader.GetUniformLocation("cameraPos"), camera_pos.x, camera_pos.y, camera_pos.z);
	glUniform1f(atmoShader.GetUniformLocation("cameraHeight"), cameraHeight);
	glUniform1f(atmoShader.GetUniformLocation("cameraHeightSq"), cameraHeight);
	glUniformMatrix4fv(atmoShader.GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
	glUniformMatrix4fv(MainShader.GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(projection));
	atmosphere.Render(atmoShader);

}
