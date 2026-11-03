#include "ColorSpaces.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    [[nodiscard]] color::Mat3d ConeResponseMatrix(color::ChromaticAdaptation adaptation) noexcept;
    [[nodiscard]] color::Mat3d DiagonalMatrix(const color::Vec3d& diagonal) noexcept;
    [[nodiscard]] color::Mat3d PrimariesMatrix(const color::Gamut& gamut) noexcept;
}

namespace color
{
    Vec3d Multiply(const Mat3d& matrix, const Vec3d& vector) noexcept
    {
        const std::array<std::array<double, 3>, 3>& rows = matrix.Rows;
        const double x = rows[0][0] * vector.X + rows[0][1] * vector.Y + rows[0][2] * vector.Z;
        const double y = rows[1][0] * vector.X + rows[1][1] * vector.Y + rows[1][2] * vector.Z;
        const double z = rows[2][0] * vector.X + rows[2][1] * vector.Y + rows[2][2] * vector.Z;

        return Vec3d{ x, y, z };
    }

    Mat3d Multiply(const Mat3d& left, const Mat3d& right) noexcept
    {
        Mat3d product{};

        for (int32_t row = 0; row < 3; ++row)
        {
            for (int32_t column = 0; column < 3; ++column)
            {
                double sum = 0.0;

                for (int32_t inner = 0; inner < 3; ++inner)
                {
                    sum += left.Rows[row][inner] * right.Rows[inner][column];
                }

                product.Rows[row][column] = sum;
            }
        }

        return product;
    }

    Mat3d Transpose(const Mat3d& matrix) noexcept
    {
        Mat3d transposed{};

        for (int32_t row = 0; row < 3; ++row)
        {
            for (int32_t column = 0; column < 3; ++column)
            {
                transposed.Rows[column][row] = matrix.Rows[row][column];
            }
        }

        return transposed;
    }

    Mat3d Inverse(const Mat3d& matrix) noexcept
    {
        const std::array<std::array<double, 3>, 3>& m = matrix.Rows;
        const double cofactor00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
        const double cofactor01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
        const double cofactor02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
        const double determinant = m[0][0] * cofactor00 + m[0][1] * cofactor01 + m[0][2] * cofactor02;
        const double scale = 1.0 / determinant;

        Mat3d inverse{};
        inverse.Rows[0][0] = cofactor00 * scale;
        inverse.Rows[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * scale;
        inverse.Rows[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * scale;
        inverse.Rows[1][0] = cofactor01 * scale;
        inverse.Rows[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * scale;
        inverse.Rows[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * scale;
        inverse.Rows[2][0] = cofactor02 * scale;
        inverse.Rows[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * scale;
        inverse.Rows[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * scale;

        return inverse;
    }

    Mat3d Identity() noexcept
    {
        return DiagonalMatrix(Vec3d{ 1.0, 1.0, 1.0 });
    }

    Vec3d XyToXyz(Xy chromaticity) noexcept
    {
        const double x = chromaticity.X / chromaticity.Y;
        const double z = (1.0 - chromaticity.X - chromaticity.Y) / chromaticity.Y;

        return Vec3d{ x, 1.0, z };
    }

    Mat3d RgbToXyz(const Gamut& gamut) noexcept
    {
        const Mat3d primaries = PrimariesMatrix(gamut);
        const Vec3d white = XyToXyz(gamut.White);
        const Vec3d scale = Multiply(Inverse(primaries), white);

        return Multiply(primaries, DiagonalMatrix(scale));
    }

    Mat3d XyzToRgb(const Gamut& gamut) noexcept
    {
        return Inverse(RgbToXyz(gamut));
    }

    Mat3d AdaptationMatrix(Xy source_white, Xy destination_white, ChromaticAdaptation adaptation) noexcept
    {
        if (adaptation == ChromaticAdaptation::None)
        {
            return Identity();
        }

        const Mat3d cone = ConeResponseMatrix(adaptation);
        const Vec3d sourceCone = Multiply(cone, XyToXyz(source_white));
        const Vec3d destinationCone = Multiply(cone, XyToXyz(destination_white));
        const Vec3d gain{ destinationCone.X / sourceCone.X, destinationCone.Y / sourceCone.Y, destinationCone.Z / sourceCone.Z };
        const Mat3d scaled = Multiply(DiagonalMatrix(gain), cone);

        return Multiply(Inverse(cone), scaled);
    }

    Mat3d RgbToRgb(const Gamut& source, const Gamut& destination, ChromaticAdaptation adaptation) noexcept
    {
        const Mat3d adapt = AdaptationMatrix(source.White, destination.White, adaptation);
        const Mat3d adaptedFromSource = Multiply(adapt, RgbToXyz(source));

        return Multiply(XyzToRgb(destination), adaptedFromSource);
    }

    double Luminance(const Gamut& gamut, const Vec3d& rgb) noexcept
    {
        const Mat3d toXyz = RgbToXyz(gamut);
        const std::array<double, 3>& luminanceRow = toXyz.Rows[1];

        return luminanceRow[0] * rgb.X + luminanceRow[1] * rgb.Y + luminanceRow[2] * rgb.Z;
    }

    Xy KelvinToXy(double kelvin) noexcept
    {
        const double t = std::clamp(kelvin, KelvinMinimum, KelvinMaximum);
        const double tSquared = t * t;

        const double uNumerator = 0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * tSquared;
        const double uDenominator = 1.0 + 8.42420235e-4 * t + 7.08145163e-7 * tSquared;
        const double vNumerator = 0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * tSquared;
        const double vDenominator = 1.0 - 2.89741816e-5 * t + 1.61456053e-7 * tSquared;
        const double u = uNumerator / uDenominator;
        const double v = vNumerator / vDenominator;

        const double scale = 2.0 * u - 8.0 * v + 4.0;

        return Xy{ 3.0 * u / scale, 2.0 * v / scale };
    }

    Vec3d KelvinToRgb(const Gamut& gamut, double kelvin) noexcept
    {
        const Vec3d xyz = XyToXyz(KelvinToXy(kelvin));

        return Multiply(XyzToRgb(gamut), xyz);
    }
}

namespace
{
    color::Mat3d ConeResponseMatrix(color::ChromaticAdaptation adaptation) noexcept
    {
        color::Mat3d matrix{};

        switch (adaptation)
        {
        case color::ChromaticAdaptation::Bradford:
            matrix.Rows = { { { 0.8951, 0.2664, -0.1614 }, { -0.7502, 1.7135, 0.0367 }, { 0.0389, -0.0685, 1.0296 } } };
            return matrix;
        case color::ChromaticAdaptation::Cat02:
            matrix.Rows = { { { 0.7328, 0.4296, -0.1624 }, { -0.7036, 1.6975, 0.0061 }, { 0.0030, 0.0136, 0.9834 } } };
            return matrix;
        default:
            break;
        }

        const double notANumber = std::numeric_limits<double>::quiet_NaN();
        const std::array<double, 3> nanRow{ notANumber, notANumber, notANumber };
        matrix.Rows = { nanRow, nanRow, nanRow };

        return matrix;
    }

    color::Mat3d DiagonalMatrix(const color::Vec3d& diagonal) noexcept
    {
        color::Mat3d matrix{};
        matrix.Rows[0][0] = diagonal.X;
        matrix.Rows[1][1] = diagonal.Y;
        matrix.Rows[2][2] = diagonal.Z;

        return matrix;
    }

    // the columns are the XYZ of the three primaries at Y = 1
    color::Mat3d PrimariesMatrix(const color::Gamut& gamut) noexcept
    {
        const color::Vec3d red = color::XyToXyz(gamut.Red);
        const color::Vec3d green = color::XyToXyz(gamut.Green);
        const color::Vec3d blue = color::XyToXyz(gamut.Blue);

        color::Mat3d matrix{};
        matrix.Rows = { { { red.X, green.X, blue.X }, { red.Y, green.Y, blue.Y }, { red.Z, green.Z, blue.Z } } };

        return matrix;
    }
}
