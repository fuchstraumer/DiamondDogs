#pragma once
#ifndef CLAMP_H
#define CLAMP_H
#include "../Base.h"

namespace cnoise {

	namespace modifiers {

		class Clamp : public Module {
		public:

			Clamp(int width, int height, float lower_bound = 0.0f, float upper_bound = 1.0f, Module* source = nullptr);

			virtual size_t GetSourceModuleCount() const override;

			virtual void Generate() override;

			float GetLowerBound() const;

			float GetUpperBound() const;

			void SetLowerBound(const float lower);

			void SetUpperBound(const float upper);

		private:

			float lowerBound, upperBound;

		};

		class Clamp3D : public Module3D {
		public:

			Clamp3D(int width, int height, float lower_bound = 0.0f, float upper_bound = 1.0f, Module3D* source = nullptr);

			virtual size_t GetSourceModuleCount() const override;

			virtual void Generate() override;

			float GetLowerBound() const;

			float GetUpperBound() const;

			void SetLowerBound(const float lower);

			void SetUpperBound(const float upper);

		private:

			float lowerBound, upperBound;
		};

	}

}
#endif // !CLAMP_H
