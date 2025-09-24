#include "PlatformTypes.hpp"

const char* DisplayColorCapabilities::GetDetectedColorSpaceName() const
{
    switch (DetectedColorSpace)
    {
        case ColorSpace::sRGB_Nonlinear: return "sRGB";
        case ColorSpace::Display_P3_Nonlinear: return "Display P3";
        case ColorSpace::Extended_sRGB_Linear: return "Extended sRGB Linear";
        case ColorSpace::Display_P3_Linear: return "Display P3 Linear";
        case ColorSpace::DCI_P3_Nonlinear: return "DCI-P3";
        case ColorSpace::BT709_Linear: return "BT.709 Linear";
        case ColorSpace::BT709_Nonlinear: return "BT.709";
        case ColorSpace::BT2020_Linear: return "Rec.2020";
        case ColorSpace::HDR10_ST2084: return "HDR10 (ST.2084)";
        case ColorSpace::HDR10_HLG: return "HDR10 (HLG)";
        case ColorSpace::Extended_sRGB_Nonlinear: return "Extended sRGB";
        case ColorSpace::PassThrough: return "Pass Through";
        case ColorSpace::DisplayNativeAMD: return "AMD FreeSync Premium Pro";
        default: return "Unknown";
    }
}