#pragma once
#ifndef MAX_H
#define MAX_H
#include "../Base.h"

namespace cnoise {

	namespace combiners {

		class Max : public Module {
		public:

			Max(const int width, const int height, Module* in0 = nullptr, Module* in1 = nullptr);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		};

	}

}
#endif // !MAX_H
