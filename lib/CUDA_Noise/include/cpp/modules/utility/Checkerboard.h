#pragma once
#ifndef CHECKERBOARD_H
#define CHECKERBOARD_H
#include "../Base.h"

/*

	CHECKERBOARD_H

	Defines a module that generates a checkerboard pattern in range 
	defined in the ctor. Generates a checkboard pattern of -1.0 and 
	1.0 squares.

*/

namespace cnoise {

	namespace utility {


		class Checkerboard : public Module {
		public:

			Checkerboard(const int width, const int height);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};


	}

}

#endif // !CHECKERBOARD_H
