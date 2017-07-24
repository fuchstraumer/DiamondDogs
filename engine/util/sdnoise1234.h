#include "stdafx.h"
/*

	Original implementation credit given below.
	This was barely tweaked, mostly to include a seed.
*/


/* sdnoise1234, Simplex noise with true analytic
 * derivative in 1D to 4D.
 *
 * Copyright © 2003-2012, Stefan Gustavson
 *
 * Contact: stefan.gustavson@gmail.com
 *
 * This library is public domain software, released by the author
 * into the public domain in February 2011. You may do anything
 * you like with it. You may even remove all attributions,
 * but of course I'd appreciate it if you kept my name somewhere.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */


struct NoiseGen {

	NoiseGen();

	/** 1D simplex noise with derivative.
	 * If the last argument is not null, the analytic derivative
	 * is also calculated.
	 */
	float sdnoise1(float x, float *dnoise_dx);

	/** 2D simplex noise with derivatives.
	 * If the last two arguments are not null, the analytic derivative
	 * (the 2D gradient of the scalar noise field) is also calculated.
	 */
	float sdnoise2(float x, float y, float *dnoise_dx, float *dnoise_dy);

	/** 3D simplex noise with derivatives.
	 * If the last tthree arguments are not null, the analytic derivative
	 * (the 3D gradient of the scalar noise field) is also calculated.
	 */
	float sdnoise3(float x, float y, float z, glm::vec3* deriv);

	/** 4D simplex noise with derivatives.
	 * If the last four arguments are not null, the analytic derivative
	 * (the 4D gradient of the scalar noise field) is also calculated.
	 */
	float sdnoise4(float x, float y, float z, float w,
		float *dnoise_dx, float *dnoise_dy,
		float *dnoise_dz, float *dnoise_dw);

	std::array<uint8_t, 512> perm;
};