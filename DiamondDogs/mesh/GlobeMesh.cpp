#include "../stdafx.h"
#include "GlobeMesh.h"
static const float pi = 3.14159265358979323846264338327950288f;
GlobeMesh::GlobeMesh(unsigned int lod){
	float deltaPhi = pi / static_cast<float>(lod);
	float deltaTheta = (2 * pi) / static_cast<float>(lod);

	for (float phi = 0.0f; phi < pi; phi += deltaPhi) {
		for (float theta = 0.0f; theta < 2.0f * pi - 0.001f; theta += deltaTheta) {

			float x1 = -sinf(phi) * sinf(theta);
			float y1 = -cosf(phi);
			float z1 = -sinf(phi) * cosf(theta);

			float u1 = float(atan2(x1, z1) / (2.0f * pi) + 0.50f);
			float v1 = float(-asin(y1) / pi + 0.50f);

			float x2 = -sinf(theta + deltaTheta) * sinf(phi);
			float y2 = -cosf(phi);
			float z2 = -sinf(phi) * cosf(theta + deltaTheta);

			float u2 = float(atan2(x2, z2) / (2.0f * pi) + 0.50f);
			float v2 = float(-asin(y2) / (pi) + 0.50f);

			float x3 = -sinf(theta + deltaTheta) * sinf(phi + deltaPhi);
			float y3 = -cosf(phi + deltaPhi);
			float z3 = -sinf(phi + deltaPhi) * cosf(theta + deltaTheta);

			float u3 = float(atan2(x3, z3) / (2.0f * pi) + 0.50f);
			float v3 = float(-asin(y3) / pi + 0.50f);

			float x4 = -sinf(theta) * sinf(phi + deltaPhi);
			float y4 = -cosf(phi + deltaPhi);
			float z4 = -sinf(phi + deltaPhi) * cosf(theta);

			float u4 = float(atan2(x4, z4) / (2.0f * pi) + 0.50f);
			float v4 = float(-asin(y4) / pi + 0.50f);


			glm::vec3 p1(x1, y1, z1);
			glm::vec2 uv1(u1, v1);
			glm::vec3 p2(x2, y2, z2);
			glm::vec2 uv2(u2, v2);
			glm::vec3 p3(x3, y3, z3);
			glm::vec2 uv3(u3, v3);
			glm::vec3 p4(x4, y4, z4);
			glm::vec2 uv4(u4, v4);

			vertex_t v01, v02, v03, v04;
			v01.Position = p1;
			v01.Normal = glm::normalize(p1 - glm::vec3(0.0f));
			v02.Position = p2;
			v02.Normal = glm::normalize(p2 - glm::vec3(0.0f));
			v03.Position = p3;
			v03.Normal = glm::normalize(p3 - glm::vec3(0.0f));
			v04.Position = p4;
			v04.Normal = glm::normalize(p4 - glm::vec3(0.0f));
			index_t i0, i1, i2, i3;
			i0 = AddVert(v01);
			i1 = AddVert(v02);
			i2 = AddVert(v03);
			i3 = AddVert(v04);

			AddTriangle(i0, i1, i2);
			AddTriangle(i0, i2, i3);
		}
	}
}
