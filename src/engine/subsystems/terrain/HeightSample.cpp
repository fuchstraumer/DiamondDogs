#include "engine/subsystems/terrain/HeightSample.hpp"

namespace terrain {

    bool HeightSample::operator<(const HeightSample & other) const {
        return Sample.x < other.Sample.x;
    }

    bool HeightSample::operator>(const HeightSample & other) const {
        return Sample.x > other.Sample.x;
    }

    bool HeightSample::operator==(const HeightSample & other) const {
        return Sample == other.Sample;
    }
    
}