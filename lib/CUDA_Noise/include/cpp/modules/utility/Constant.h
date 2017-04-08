#pragma once
#ifndef CONSTANT_H
#define CONSTANT_H
#include "../Base.h"

namespace cnoise {

	namespace utility {

		class Constant : public Module {
		public:
			Constant(const int width, const int height, const float value);

			virtual size_t GetSourceModuleCount() const override;

			virtual void Generate() override;
		};
	}

}

#endif // !CONSTANT_H
