#ifndef VULPES_AABB_H
#define VULPES_AABB_H

#include "stdafx.h"

namespace vulpes {

    /*
        ditetrahedron-OBB method

        Uses extremal vertices of any mesh object to construct an accurate
        AABB that is both faster and more conformant than conventional techniques.

        Steps:

        1. Find set of points P, in even sets of multiple k, with min/max
        projection values along our predefined orientation axes
        2. Points are stored in normalized fashion so that any location on
        the surface of the AABB can be easily (read: functionally) retrieved/tested.
        3. 

    */

    constexpr double golden_ratio = 0.61803398875;

    struct AABB {
        glm::vec3 center;
        glm::vec3 dir0, dir1, dir2;
        glm::vec3 extents;
    };

    template<typename vert_type>
    AABB BuildAABB(const std::vector<vertex_type>& verts) {
        
    }
}

#endif //!VULPES_AABB_H