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

void Billboard3D::BuildRenderData(const float & temperature){
	// Set frame counter to zero
	frame = 0;
	// Bind Program
	Program->Use();
	// Setup up VAO
	glGenVertexArrays(1, &this->VAO);
	glBindVertexArray(this->VAO);
	// Gen and bind main buffer object
	glGenBuffers(1, &this->VBO);
	glBindBuffer(GL_ARRAY_BUFFER, this->VBO);
	center[0] = Position.x;
	center[1] = Position.y;
	center[2] = Position.z;
	// Populate buffer with data
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), &quad_vertices, GL_STATIC_DRAW);
	// Set elements/attributes of the VAO
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	// Final model matrix. Use lambda defined earlier. Order of operations/multiplication matters
	// since this is matrix math.
	model = buildModelMatrix(Position, Scale, Angle);
	// Normally, getting this element is actually fairly expensive
	normTransform = glm::transpose(glm::inverse(model));
	// Acquire locations of uniforms and set them appropriately.
	GLuint modelLoc = Program->GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
	// Pass temperature value
	GLuint tempLoc = Program->GetUniformLocation("temperature");
	glUniform1i(tempLoc, temperature);
	// Unbind vertex array
	glBindVertexArray(0);
}

void Billboard3D::Render(const glm::mat4 & view, const glm::mat4 & projection) {
	Program->Use();
	glBindVertexArray(this->VAO);
	// Set size of billboard
	GLuint sizeLoc = Program->GetUniformLocation("size");
	glUniform2f(sizeLoc, Radius, Radius);
	// Pass in the matrices we need
	GLuint viewLoc = Program->GetUniformLocation("view");
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	GLuint projLoc = Program->GetUniformLocation("projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	// Extract vectors we need from the view matrix
	glm::vec3 camera_right = glm::vec3(view[0][0], view[1][0], view[2][0]);
	glm::vec3 camera_up = glm::vec3(view[0][1], view[1][1], view[2][1]);
	GLuint cRLoc = Program->GetUniformLocation("cameraRight");
	GLuint cULoc = Program->GetUniformLocation("cameraUp");
	glUniform3f(cRLoc, camera_right.x, camera_right.y, camera_right.z);
	glUniform3f(cULoc, camera_up.x, camera_up.y, camera_up.z);
	// Set last few uniforms
	GLuint centerLoc = Program->GetUniformLocation("center");
	glUniform3f(centerLoc, center.x, center.y, center.z);
	// If frame counter is equal to limits of numeric precision,
	if (frame < std::numeric_limits<uint64_t>::max()) {
		// Reset frame counter
		frame = 0;
	}
	// Otherwise, increment it
	else {
		frame++;
	}
	// Set frame value in Program
	GLuint frameLoc = Program->GetUniformLocation("frame");
	glUniform1i(frameLoc, frame);
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
