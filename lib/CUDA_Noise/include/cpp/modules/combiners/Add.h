#pragma once
#ifndef ADD_H
#define ADD_H
#include "../Base.h"

namespace cnoise {

	namespace combiners {

		class Add : public Module {
		public:

			Add(int width, int height, float add_value, Module* source = nullptr);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

		private: 

		};

	}

}

#endif // !ADD_H
