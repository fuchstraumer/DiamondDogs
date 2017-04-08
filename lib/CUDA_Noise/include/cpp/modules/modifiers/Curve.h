#pragma once
#ifndef CURVE_H
#define CURVE_H
#include "..\Base.h"
/*

	Modifier module - Curve

	An expandable class that curves data. Has an internal vector of control points
	that can be expanded and is used to define the curve. This vector is passed
	to the CUDA kernel.

	NOTE: Unlike other modules, this module MUST be setup before using, if a vector
	of control points is not supplied in the constructor. This is due ot how we have
	to set everything in CUDA up.

*/

namespace cnoise {

	namespace modifiers {

		class Curve : public Module {
		public:

			// Doesn't add any control points. Empty constructor.
			Curve(int width, int height);

			// Adds control points from given vector and makes sure kernel is good to go ASAP
			Curve(int width, int height, const std::vector<ControlPoint>& init_points);

			// Adds a control point
			void AddControlPoint(float input_val, float output_val);

			virtual size_t GetSourceModuleCount() const override;

			// Get control points (non-mutable)
			std::vector<ControlPoint> GetControlPoints() const;

			// Set control points
			void SetControlPoints(const std::vector<ControlPoint>& pts);

			// Clear control points
			void ClearControlPoints();

			// Generate data by launching the CUDA kernel
			virtual void Generate() override;

		protected:

			// Control points.
			std::vector<ControlPoint> controlPoints;
		};

		class Curve3D : public Module3D {
		public:

			Curve3D(int width, int height);

			// init with control points
			Curve3D(int width, int height, const std::vector<ControlPoint>& init_points);

			// Adds a control point
			void AddControlPoint(float input_value, float output_value);
			void AddControlPoint(const ControlPoint& pt);

			virtual size_t GetSourceModuleCount() const override;

			// Get control points
			std::vector<ControlPoint> GetControlPoints() const;

			// Set control points
			void SetControlPoints(const std::vector<ControlPoint>& pts);

			// Clear points
			void ClearControlPoints();

			virtual void Generate() override;

		private:

			std::vector<ControlPoint> controlPoints;
		};

	}

}
#endif // !CURVE_H
