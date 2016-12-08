#include "Mesh.h"
#include "glm/gtx/vector_angle.hpp"

__inline float maptosphere(float const &a, float const &b, float const &c) {
	float result;
	result = a * sqrt(1 - (b*b / 2.0f) - (c*c / 2.0f) + ((b*b)*(c*c) / 3.0f));
	return result;
}



void Mesh::Clear() {
	Vertices.clear(); Indices.clear();
	Triangles.clear(); Faces.clear();
	Edges.clear();
	Vertices.shrink_to_fit();
	Indices.shrink_to_fit();
	Triangles.shrink_to_fit();
	Faces.shrink_to_fit();
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

// Get face at index and return a reference
const face_t& Mesh::GetFace(index_t f_index) const {
	return Faces[f_index];
}

// Get tri at index and return a reference
const triangle_t & Mesh::GetTri(index_t t_index) const {
	return Triangles[t_index];
}
// Add vert and return index to it
index_t Mesh::AddVert(const vertex_t & vert) {
	Vertices.push_back(vert);
	return (index_t)Vertices.size() - 1;
}

// Add triangle using three indices and return a reference to this triangle
index_t Mesh::AddTriangle(const index_t &i0, const index_t &i1, const index_t &i2){
	Indices.push_back(i0);
	Indices.push_back(i1);
	Indices.push_back(i2);
	triangle_t newTri(i0, i1, i2);
	Triangles.push_back(newTri);
	index_t val = (index_t)Triangles.size() - 1;
	// The triangle actually stores the edges, as well.
	// The value the key points to tells us which triangle
	// this edge is used by
	// This is a multimap, so multiple copies of the same 
	// key value can exist but will have different values.
	// This lets us find all triangles that use an edge.
	Edges.insert(std::pair<edge_key, index_t>(newTri.e0, val));
	Edges.insert(std::pair<edge_key, index_t>(newTri.e1, val));
	Edges.insert(std::pair<edge_key, index_t>(newTri.e2, val));
	return val;
}

// Add face and return index to it
index_t Mesh::AddFace(const face_t& face) {
	Faces.push_back(face);
	return (index_t)Faces.size() - 1;
}

face_t Mesh::CreateFace(const index_t &i0, const index_t &i1, const index_t &i2, const index_t &i3) {
	index_t t0 = AddTriangle(i0, i1, i3);
	index_t t1 = AddTriangle(i1, i2, i3);
	return face_t(t0, t1);
}

face_t Mesh::CreateFace(const index_t& t0, const index_t& t1) const {
	return face_t(t0, t1);
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

void Mesh::SubdivideTriangles() {
	Mesh temp = *this;
	for (auto triangle : this->Triangles) {
		// Find the longest edge in each triangle and split that.
		auto&& edgeToSplit = LongestEdge(triangle);
		index_t splitIndex = temp.SplitEdge(edgeToSplit);
		face_t subdiv = temp.CreateFace(triangle.i0, triangle.i1, triangle.i2, splitIndex);
		temp.AddFace(subdiv);
	}
	this->Clear();
	*this = std::move(temp);
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
	result.Normal = in.Position - glm::vec3(0.0f);
	result.Position = glm::normalize(result.Normal);
	return result;
}

glm::vec3 Mesh::PointToUnitSphere(const glm::vec3 &in) const {
	glm::vec3 res;
	res = glm::normalize(in);
	glm::vec3 dir = res - glm::vec3(0.0f);
	res = glm::normalize(dir);
	return res;
}

vertex_t Mesh::GetMiddlePoint(const index_t &i0, const index_t &i1) {
	vertex_t res;
	auto&& v0 = GetVertex(i0);
	auto&& v1 = GetVertex(i1);
	res.Position = (v0.Position + v1.Position) / 2.0f;
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
	err = glGetError();
	Model = glm::translate(Model, Position);
	NormTransform = glm::transpose(glm::inverse(Model));
	glBindVertexArray(0);
	meshBuilt = true;
}

void Mesh::Render(ShaderProgram & shader){
	GLenum err;
	glGetError();
	shader.Use();
	glBindVertexArray(VAO);
	//glBindBuffer(GL_ARRAY_BUFFER, VBO);
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	err = glGetError();
	glDrawElements(GL_TRIANGLES, GetNumIndices(), GL_UNSIGNED_INT, 0);
	GLint modelLoc = shader.GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(Model));
	GLint normTLoc = shader.GetUniformLocation("normTransform");
	glUniformMatrix4fv(normTLoc, 1, GL_FALSE, glm::value_ptr(NormTransform));
	err = glGetError();
	glBindVertexArray(0);
}
