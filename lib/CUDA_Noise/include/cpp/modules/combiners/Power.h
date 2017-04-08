#pragma once
#ifndef POWER_H
#define POWER_H
#include "../Base.h"

namespace cnoise {

	namespace combiners {

		class Power : public Module {
		public:

			Power(const int width, const int height, Module* in0, Module* in1);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};

	}

}

#endif // !POWER_H
