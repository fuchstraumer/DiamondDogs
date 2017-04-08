#pragma once
#ifndef MIN_H
#define MIN_H
#include "../Base.h"

namespace cnoise {

	namespace combiners {

		class Min : public Module {
		public:

			Min(const int width, const int height, Module* in0, Module* in1);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};

		class Min3D : public Module3D {
		public:

			Min3D(const int width, const int height, Module3D* in0 = nullptr, Module3D* in1 = nullptr);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};

	}

}

#endif // !MIN_H
