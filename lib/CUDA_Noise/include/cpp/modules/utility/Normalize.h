#pragma once
#ifndef NORMALIZE_H
#define NORMALIZE_H
#include "../Base.h"

namespace cnoise {

	namespace utility {

		class Normalize : public Module {
		public:

			Normalize(int width, int height, Module* source = nullptr);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};

	}

}

#endif // !NORMALIZE_H
