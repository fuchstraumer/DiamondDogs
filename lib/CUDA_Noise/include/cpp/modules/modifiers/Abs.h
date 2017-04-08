#pragma once
#ifndef ABS_H
#define ABS_H
#include "../Base.h"

namespace cnoise {

	namespace modifiers {

		class Abs : public Module {
		public:

			Abs(const size_t width, const size_t height, Module* previous);

			virtual size_t GetSourceModuleCount() const override;

			void Generate() override;

		};

		class Abs3D : public Module3D {
		public:

			Abs3D(int width, int height) : Module3D(width, height) {};

			virtual size_t GetSourceModuleCount() const override;

			virtual void Generate();

		};

	}

}

#endif // !ABS_H
