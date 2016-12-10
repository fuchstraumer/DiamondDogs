#include "../stdafx.h"
#include "PlanarMesh.h"

PlanarMesh::PlanarMesh(const uint & lod, const CardinalFace & f){
	Max_LOD = lod;
	float xAngle, yAngle, zAngle;
	// This controls the size we step in angular terms
	const float dAngle = ((90.0f * (3.14159265f)) / 180.0f) / (static_cast<float>(Max_LOD));
	// Initial angle
	const float aInitial = 45.0f * (3.14159265f) / 180.0f;
	int reserveAmt = (Max_LOD + 1) * (Max_LOD + 1);
	Face = f;
	// Build this face depending on which one it is
	switch (Face) {
	case CardinalFace::BACK:
		for (int i = 0; i < Max_LOD + 1; ++i) {
			for (int j = 0; j < Max_LOD + 1; ++j) {
				xAngle = aInitial - (dAngle * i);
				yAngle = -aInitial + (dAngle * j);
				vertex_t newVert;
				newVert.Position.x = static_cast<float>(tan(xAngle));
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = 1.0f;
				
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
				newVert.Position.x = 1.0f;
				newVert.Position.y = static_cast<float>(tan(yAngle));
				newVert.Position.z = static_cast<float>(tan(zAngle));
				// Temp normal, will change when we map this to a sphere
				// Do something with index?
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
	
}
