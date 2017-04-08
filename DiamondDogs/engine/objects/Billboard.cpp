#include "stdafx.h"
#include "Billboard.h"

auto buildModelMatrix = [](glm::vec3 position, glm::vec3 scale, glm::vec3 angle)->glm::mat4 {
	glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
	glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), angle.z, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 result = scaleMatrix * rotX * rotY * rotZ * translationMatrix;
	return result;
};

Billboard3D::Billboard3D(const float & radius, const glm::vec3 & pos, const glm::vec3 & scale, const glm::vec3 & angle) : Position(pos), Scale(scale), Angle(angle), Radius(radius) {}

Billboard3D::Billboard3D(Billboard3D && other) noexcept : Program(std::move(other.Program)), Radius(std::move(other.Radius)), Scale(other.Scale), Position(other.Position), Angle(other.Angle), starColor(other.starColor), VAO(std::move(other.VAO)), VBO(std::move(other.VBO)), model(other.model), normTransform(other.normTransform) {
}

Billboard3D& Billboard3D::operator=(Billboard3D&& other) noexcept {
	Program = (std::move(other.Program));
	Radius = (std::move(other.Radius)); 
	Scale = other.Scale; 
	Position = other.Position; 
	Angle = other.Angle; 
	starColor = other.starColor;
	VAO = std::move(other.VAO); 
	VBO = std::move(other.VBO); 
	model = other.model; 
	normTransform = other.normTransform;
	return *this;
}

void Billboard3D::BuildRenderData(){
	// Bind Program
	Program.Use();
	// Upload data to VBO
	glNamedBufferData(VBO[0], sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
	// Setup up VAO
	glEnableVertexArrayAttrib(VAO[0], 0);
	glVertexArrayVertexBuffer(VAO[0], 0, VBO[0], 0, 3 * sizeof(GLfloat));
	glVertexArrayAttribFormat(VAO[0], 0, 3, GL_FLOAT, GL_FALSE, 0);

	// Final model matrix. Use lambda defined earlier. Order of operations/multiplication matters
	// since this is matrix math.
	model = buildModelMatrix(Position, Scale, Angle);

	// Acquire locations of uniforms and set them appropriately.
	GLuint modelLoc = Program.GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
	GLuint centerLoc = Program.GetUniformLocation("center");
	glUniform3f(centerLoc, Position.x, Position.y, Position.z);
	GLuint sizeLoc = Program.GetUniformLocation("size");
	glUniform2f(sizeLoc, Radius, Radius);
}

void Billboard3D::Render(const glm::mat4 & view, const glm::mat4 & projection) {
	Program.Use();
	glBindVertexArray(VAO[0]);
	// Pass in the matrices we need
	GLuint viewLoc = Program.GetUniformLocation("view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	GLuint projLoc = Program.GetUniformLocation("projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	// Extract vectors we need from the view matrix
	glm::vec3 camera_right = glm::vec3(view[0][0], view[1][0], view[2][0]);
	glm::vec3 camera_up = glm::vec3(view[0][1], view[1][1], view[2][1]);
	GLuint cRLoc = Program.GetUniformLocation("cameraRight");
	GLuint cULoc = Program.GetUniformLocation("cameraUp");
	glUniform3f(cRLoc, camera_right.x, camera_right.y, camera_right.z);
	glUniform3f(cULoc, camera_up.x, camera_up.y, camera_up.z);
	// Draw the billboard
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Billboard3D::UpdateModelMatrix(glm::vec3 position = glm::vec3(0.0f), glm::vec3 scale = glm::vec3(1.0f), glm::vec3 angle = glm::vec3(0.0f)) {
	if (position != glm::vec3(0.0f)) {
		Position = position;
	}
	if (scale != glm::vec3(1.0f)) {
		Scale = scale;
	}
	if (angle != glm::vec3(0.0f)) {
		Angle = angle;
	}
	model = buildModelMatrix(Position, Scale, Angle);
}
