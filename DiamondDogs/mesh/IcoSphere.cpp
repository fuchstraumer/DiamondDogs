#include "../stdafx.h"
#include "IcoSphere.h"

IcoSphere::IcoSphere(uint lod, float radius) : Mesh() {
	Max_LOD = lod;
	Radius = radius;
	// Build mesh now
	const float pi = 3.14159265358979323846264338327950288f;
	float deltaPhi = pi / Max_LOD;
	float deltaTheta = (2 * pi) / Max_LOD;
	for (float phi = 0; phi < pi; phi += deltaPhi) {
		for (float theta = 0; theta < (2 * pi) - 0.001f; theta += deltaTheta) {
			float x1 = -sinf(phi) * sinf(theta) * radius;
			float y1 = -cosf(phi) * radius;
			float z1 = -sinf(phi) * cosf(theta) * radius;

			float u1 = float(atan2(x1, z1) / (2 * pi) + 0.5);
			float v1 = float(-asin(y1) / (float)pi + 0.5);

			float x2 = -sinf(theta + deltaTheta) * sinf(phi) * radius;
			float y2 = -cosf(phi) * radius;
			float z2 = -sinf(phi) * cosf(theta + deltaTheta) * radius;

			float u2 = float(atan2(x2, z2) / (2 * pi) + 0.5);
			float v2 = float(-asin(y2) / ((float)pi) + 0.5);

			float x3 = -sinf(theta + deltaTheta) * sinf(phi + deltaPhi) * radius;
			float y3 = -cosf(phi + deltaPhi) * radius;
			float z3 = -sinf(phi + deltaPhi) * cosf(theta + deltaTheta) * radius;

			float u3 = float(atan2(x3, z3) / (2 * (float)pi) + 0.5);
			float v3 = float(-asin(y3) / (float)pi + 0.5);

			float x4 = -sinf(theta) * sinf(phi + deltaPhi) * radius;
			float y4 = -cosf(phi + deltaPhi) * radius;
			float z4 = -sinf(phi + deltaPhi) * cosf(theta) * radius;

			float u4 = float(atan2(x4, z4) / (2 * (float)pi) + 0.5);
			float v4 = float(-asin(y4) / (float)pi + 0.5);


			glm::vec3 p1(x1, y1, z1);
			glm::vec3 uv1(u1, v1, 0);
			glm::vec3 p2(x2, y2, z2);
			glm::vec3 uv2(u2, v2, 0);
			glm::vec3 p3(x3, y3, z3);
			glm::vec3 uv3(u3, v3, 0);
			glm::vec3 p4(x4, y4, z4);
			glm::vec3 uv4(u4, v4, 0);

			vertex_t v00, v01, v02, v03;
			v00.Position = p1;
			v00.UV = uv1.xy;
			v01.Position = p2;
			v01.UV = uv2.xy;
			v02.Position = p3;
			v02.UV = uv3.xy;
			v03.Position = p4;
			v03.UV = uv4.xy;

			index_t i0, i1, i2, i3;
			i0 = AddVert(VertToSphere(v00, Radius));
			i1 = AddVert(VertToSphere(v01, Radius));
			i2 = AddVert(VertToSphere(v02, Radius));
			i3 = AddVert(VertToSphere(v03, Radius));

			face_t newFace = CreateFace(i3, i2, i1, i0);
			AddFace(newFace);
		}
	}
}

void IcoSphere::BuildRenderData(){

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
	glBindVertexArray(VAO);
	// Bind the vertex buffer and then specify what data it will be loaded with
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, GetNumVerts() * sizeof(vertex_t), &(Vertices[0]), GL_DYNAMIC_DRAW);
	// Bind the element array (indice) buffer and fill it with data
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GetNumIndices() * sizeof(index_t), &(Indices.front()), GL_DYNAMIC_DRAW);
	// Pointer to the position attribute of a vertex
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)0);
	// Pointer to the normal attribute of a vertex
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)offsetof(vertex_t, Normal));
	glm::vec3 scale(Radius / 2);
	Model = glm::scale(Model, scale);
	NormTransform = glm::transpose(glm::inverse(Model));
	glBindVertexArray(0);
}

void IcoSphere::Render(ShaderProgram& shader){
	shader.Use();
	//glFrontFace(GL_CCW);
	glDepthFunc(GL_LEQUAL);
	glBindVertexArray(VAO);
	glDrawElements(GL_TRIANGLES, GetNumIndices(), GL_UNSIGNED_INT, 0);
	GLint modelLoc = glGetUniformLocation(shader.Handle, "model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Model));
	GLint normTLoc = glGetUniformLocation(shader.Handle, "normTransform");
	glUniformMatrix4fv(normTLoc, 1, GL_FALSE, glm::value_ptr(NormTransform));
	glBindVertexArray(0);
}
