#pragma once
#ifndef TERRACE_H
#define TERRACE_H
#include "../Base.h"
#include <set>
#include <functional>

namespace cnoise {

	namespace modifiers {

		class Terrace : public Module {
		public:

			Terrace(const int width, const int height);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

			// Adds control point to container.
			void AddControlPoint(const float& val);

			// Empties control points container.
			void ClearControlPoints();

			// Gets control points. (copying data from set to a vector)
			std::vector<float> GetControlPoints() const;

			// Fills control points with a uniformly terraced curve, containing "num_pts" quantity
			// of control points from 0.0 - 1.0
			void MakeControlPoints(const size_t& num_pts);

			// Inverts this terrace module.
			void SetInversion(bool inv);

			// Gets inversion status
			bool GetInversion() const;

		private:

			// Control point container. Set is implicitly sorted in ascending order, which is what we want.
			std::set<float> controlPoints;

			// Whether or not this module is inverted.
			bool inverted;
		};

		class Terrace3D : public Module3D {
		public:

			Terrace3D(const int width, const int height);

			virtual void Generate() override;

			virtual size_t GetSourceModuleCount() const override;

			// Adds control point to container.
			void AddControlPoint(const float& val);

			// Empties control points container.
			void ClearControlPoints();

			// Gets control points. (copying data from set to a vector)
			std::vector<float> GetControlPoints() const;

			// Fills control points with a uniformly terraced curve, containing "num_pts" quantity
			// of control points from 0.0 - 1.0
			void MakeControlPoints(const size_t& num_pts);

			// Inverts this terrace module.
			void SetInversion(bool inv);

			// Gets inversion status
			bool GetInversion() const;

		private:

			// Control point container. Set is implicitly sorted in ascending order, which is what we want.
			std::set<float> controlPoints;

			// Whether or not this module is inverted.
			bool inverted;
		};

	}

}

#endif // !TERRACE_H
