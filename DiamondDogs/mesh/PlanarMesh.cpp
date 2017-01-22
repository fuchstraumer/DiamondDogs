#include "stdafx.h"
#include "PlanarMesh.h"

PlanarMesh::PlanarMesh(const uint & lod, const CardinalFace & f){
	Max_LOD = lod;
	float xAngle, yAngle, zAngle;
	// This controls the size we step in angular terms
	const float dAngle = ((90.0f * (3.14159265f)) / 180.0f) / (static_cast<float>(Max_LOD));
	// Initial angle
	const float aInitial = 45.0f * (3.14159265f) / 180.0f;
	// Amount to reserve in our containers
	int reserveAmt = (Max_LOD + 1) * (Max_LOD + 1);
	Vertices.reserve(reserveAmt);
	Face = f;
	// Build a face depending on which direction it corresponds to (of the six cardinal types)
	switch (Face) {
	case CardinalFace::BACK:
		// Build the vertices for this mesh based on our desired Max_LOD (or, amt of subdivisions)
		// NOTE: Using Max_LOD + 1 is VERY important.
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				/*
					We are going to generate points along the 90-degree arc defining a single
					face. Each face will use different angular axes. Since we are converting to 
					radians (see the above, where we multiplied by 90/45 and divided by 180) the 
					range traversed isn't 0-90 but actually -45 - 45, spanning from 0 - pi on a 
					unit circle. 

					The coordinates are going to be the tangent of the corresponding angle, 
					and the unused coord (one not using angles) will just be 1.0/-1.0
				*/
				// We're traversing the X-axis in the negative x direction here
				xAngle = aInitial - (dAngle * i);
				// We're traversing the Y-axis in the positive y direciton here
				yAngle = -aInitial + (dAngle * j);
				vertex_t newVert;
				float u, v;
				u = static_cast<float>(tan(xAngle) / 2.0f) + 0.5f;
				v = static_cast<float>(tan(yAngle) / 2.0f) + 0.5f;
				newVert.UV.x = -u;
				newVert.UV.y = v;
				newVert.Position.x = static_cast<float>(tan(xAngle));
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = 1.0f;
				// Add this vertex to the mesh
				AddVert(newVert);
			}
		}
		break;
	case CardinalFace::RIGHT:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				yAngle = aInitial - (dAngle * j);
				zAngle = aInitial - (dAngle * i);
				vertex_t newVert;
				float u, v;
				u = static_cast<float>(tan(zAngle) / 2.0f) + 0.5f;
				v = static_cast<float>(tan(yAngle) / 2.0f) + 0.5f;
				newVert.UV.x = u;
				newVert.UV.y = v;
				newVert.Position.x = 1.0f;
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = static_cast<float>(tan(zAngle));
				AddVert(newVert);
			}
		}
		break;
	case CardinalFace::FRONT:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				xAngle = -aInitial + (dAngle * i);
				yAngle = -aInitial + (dAngle * j);
				vertex_t newVert;
				float u, v;
				u = (static_cast<float>(tan(xAngle)) / 2.0f) + 0.5f;
				v = (static_cast<float>(tan(yAngle)) / 2.0f) + 0.5f;
				newVert.UV.x = u;
				newVert.UV.y = v;
				newVert.Position.x = static_cast<float>(tan(xAngle));
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = -1.0f;
				AddVert(newVert);
			}
		}
		break;
	case CardinalFace::LEFT:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				yAngle = -aInitial + (dAngle * i);
				zAngle = aInitial - (dAngle * j);
				vertex_t newVert;
				float u, v;
				u = static_cast<float>(tan(zAngle) / 2.0f) + 0.5f;
				v = static_cast<float>(tan(yAngle) / 2.0f) + 0.5f;
				newVert.UV.x = -u;
				newVert.UV.y = v;
				newVert.Position.x = -1.0f;
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = static_cast<float>(tan(zAngle));
				AddVert(newVert);
			}
		}
		break;
	case CardinalFace::TOP:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				xAngle = aInitial - (dAngle * i);
				zAngle = aInitial - (dAngle * j);
				vertex_t newVert;
				float u, v;
				u = static_cast<float>(tan(xAngle) / 2.0f) + 0.5f;
				v = static_cast<float>(tan(zAngle) / 2.0f) + 0.5f;
				newVert.UV.x = v;
				newVert.UV.y = -u;
				newVert.Position.x = static_cast<float>(tan(xAngle));
				newVert.Position.y = 1.0f;
				newVert.Position.z = static_cast<float>(tan(zAngle));
				AddVert(newVert);
			}
		}
		break;
	case CardinalFace::BOTTOM:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				xAngle = aInitial - (dAngle * i);
				zAngle = -aInitial + (dAngle * j);
				vertex_t newVert;
				newVert.Position.x = static_cast<float>(tan(xAngle));
				newVert.Position.y = -1.0f;
				newVert.Position.z = static_cast<float>(tan(zAngle));
				AddVert(newVert);
			}
		}
		break;
	}
	Vertices.shrink_to_fit();
}
