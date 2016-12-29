#include "Mesh.h"
#include "glm/gtx/vector_angle.hpp"

__inline float maptosphere(float const &a, float const &b, float const &c) {
	float result;
	result = a * sqrt(1 - (b*b / 2.0f) - (c*c / 2.0f) + ((b*b)*(c*c) / 3.0f));
	return result;
}

void Mesh::Clear() {
	Vertices.clear(); 
	Indices.clear();
	Triangles.clear(); 
	Edges.clear();
	Vertices.shrink_to_fit();
	Indices.shrink_to_fit();
	Triangles.shrink_to_fit();
}

// Return number of indices in this mesh
GLsizei Mesh::GetNumIndices() const {
	return static_cast<GLsizei>(Indices.size());
}

// Return number of verts in this mesh
GLsizei Mesh::GetNumVerts() const {
	return static_cast<GLsizei>(Vertices.size());
}

// Get vertex reference at index
const vertex_t & Mesh::GetVertex(index_t index) const {
	return Vertices[index];
}

// Get index reference at index
const index_t & Mesh::GetIndex(index_t index) const {
	return Indices[index];
}

// Get tri at index and return a reference
const triangle_t & Mesh::GetTri(index_t t_index) const {
	return Triangles[t_index];
}
// Add vert and return index to it
index_t Mesh::AddVert(const vertex_t & vert) {
	Vertices.push_back(vert);
	return static_cast<index_t>(Vertices.size() - 1);
}

// Add triangle using three indices and return a reference to this triangle
index_t Mesh::AddTriangle(const index_t &i0, const index_t &i1, const index_t &i2){
	Indices.push_back(i0);
	Indices.push_back(i1);
	Indices.push_back(i2);
	triangle_t newTri(i0, i1, i2);
	Triangles.push_back(std::move(newTri));
	index_t val = static_cast<index_t>(Triangles.size() - 1);
	return val;
}

index_t Mesh::AddTriangle(const triangle_t &tri) {
	Indices.push_back(tri.i0);
	Indices.push_back(tri.i1);
	Indices.push_back(tri.i2);
	Triangles.push_back(tri);
	return static_cast<index_t>(Triangles.size() - 1);
}

// Checking triangle for best edge for subdivision - returns key for searching the edges lookup
edge_key Mesh::LongestEdge(triangle_t const &tri) const {
	/*
		o\ i2
		| \
		|  o  iNew
		|   \
	i0	o----o i1

		Best edge to split will always be across from largest angle.
		This vertex will be on the edge that defines the hypotenuse
		And the connection point will be the base of this largest angle
		So, get three angles using
		edge0, edge1, edge2
	*/
	auto edges = { tri.e0, tri.e1, tri.e2 };
	float best = 0.0f;
	edge_key longest;
	// For each edge in the initalizer list, get its length
	for (auto edge : edges) {
		auto&& v0 = GetVertex(edge.first);
		auto&& v1 = GetVertex(edge.second);
		float dist = glm::distance2(v0.Position, v1.Position);
		if (dist > best) {
			best = dist;
			longest = edge;
		}
	}
	// Longest edge acquired, return it
	return longest;
}

index_t Mesh::SplitEdge(edge_key const &edge) {
	// Use input key to build what would we be the new edge.
	// first search
	auto&& v0 = GetVertex(edge.first);
	auto&& v1 = GetVertex(edge.second);
	vertex_t newVert = getMiddlePoint(v0, v1);
	index_t newIndex = AddVert(newVert);
	return newIndex;
}

vertex_t Mesh::VertToSphere(vertex_t in, float radius) const {
	vertex_t result;
	in.Position = glm::normalize(in.Position);
	result.Normal = in.Position - glm::vec3(0.0f);
	result.Position = glm::normalize(result.Normal * radius);
	return result;
}

vertex_t Mesh::VertToUnitSphere(const vertex_t & in) const{
	vertex_t result;
	result.Position = glm::normalize(in.Position);
	result.Normal = glm::normalize(result.Position - glm::vec3(0.0f));
	return result;
}

glm::vec3 Mesh::PointToUnitSphere(const glm::vec3 &in) const {
	glm::vec3 res;
	auto coordToSphere = [](float a, float b, float c)->float {
		float lambda;
		float bSq, cSq;
		bSq = b*b;
		cSq = c*c;
		lambda = std::sqrtf(1.0f - (bSq / 2.0f) - (cSq / 2.0f) + ((bSq*cSq)/3.0f));
		return a * lambda;
	};
	res.x = coordToSphere(in.x, in.y, in.z);
	res.y = coordToSphere(in.y, in.z, in.x);
	res.z = coordToSphere(in.z, in.x, in.y);
	return res;
}

vertex_t Mesh::GetMiddlePoint(const index_t &i0, const index_t &i1) const {
	vertex_t res;
	auto&& v0 = GetVertex(i0);
	auto&& v1 = GetVertex(i1);
	res.Position = (v0.Position + v1.Position) / 2.0f;
	return res;
}

void Mesh::SetTextures(const char * color_tex, const char * normal_tex, const char * spec_tex, uint width, uint height){
	texture = Texture2D(color_tex, width, height);
	normal = Texture2D(normal_tex, width, height);
	specular = Texture2D(spec_tex, width, height);
}

void Mesh::BuildTextureData() {
	texture.BuildTexture();
	normal.BuildTexture();
	specular.BuildTexture();
	hasTextures = true;
}

vertex_t Mesh::GetMiddlePoint(const vertex_t &v0, const vertex_t &v1) const {
	vertex_t res;
	res.Position = (v0.Position + v1.Position) / 2.0f;
	res.Normal = glm::normalize(res.Position - glm::vec3(0.0f));
	return res;
}

void Mesh::BuildRenderData(){

	GLenum err;
	glGetError();
	glGenVertexArrays(1, &VAO);
	err = glGetError();
	glGenBuffers(1, &VBO); glGenBuffers(1, &EBO);
	glBindVertexArray(VAO);
	// Bind the vertex buffer and then specify what data it will be loaded with
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, GetNumVerts() * sizeof(vertex_t), &(Vertices[0]), GL_STATIC_DRAW);
	// Bind the element array (indice) buffer and fill it with data
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GetNumIndices() * sizeof(index_t), &(Indices[0]), GL_STATIC_DRAW);
	// Pointer to the position attribute of a vertex
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)0);
	glEnableVertexAttribArray(0);
	err = glGetError();
	// Pointer to the normal attribute of a vertex
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)offsetof(vertex_t, Normal));
	glEnableVertexAttribArray(1);
	// Color attribute of a vertex
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (GLvoid*)offsetof(vertex_t, UV));
	glEnableVertexAttribArray(2);
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);
	glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
	glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), Angle.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), Angle.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), Angle.z, glm::vec3(0.0f, 0.0f, 1.0f));

	Model = scale * rotX * rotY * rotZ * translation;

	NormTransform = glm::transpose(glm::inverse(Model));

	// Set texture sampler locations
	// Location 0 is for the color sampler
	
	glBindVertexArray(0);
	meshBuilt = true;
}

void Mesh::Render(ShaderProgram & shader) const {
	shader.Use();
	glBindVertexArray(VAO);
	GLint modelLoc = shader.GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Model));
	GLint normTLoc = shader.GetUniformLocation("normTransform");
	glUniformMatrix4fv(normTLoc, 1, GL_FALSE, glm::value_ptr(NormTransform));
	glDrawElements(GL_TRIANGLES, GetNumIndices(), GL_UNSIGNED_INT, 0);
	glBindVertexArray(0);
}
