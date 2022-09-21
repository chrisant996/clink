// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "cielab.h"
#include "core/base.h"

#include <math.h>

//------------------------------------------------------------------------------
struct xyz
{
    xyz() = default;
    xyz(COLORREF c) { from_rgb(c); }

    void from_rgb(COLORREF c);

    double x = 0;
    double y = 0;
    double z = 0;

    // RGB to XYZ conversion math shared by each channel
    static double SRGBtoLinear(BYTE val)
    {
        double d = double(val) / 255;
        if (d > 0.04045)        d = pow(((d + 0.055) / 1.055), 2.4);
        else                    d = (d / 12.92);
        return max<>(min<>(d, 1.0), 0.0) * 100;
    }
};

//------------------------------------------------------------------------------
void xyz::from_rgb(COLORREF c)
{
    double rLinear = SRGBtoLinear(GetRValue(c));
    double gLinear = SRGBtoLinear(GetGValue(c));
    double bLinear = SRGBtoLinear(GetBValue(c));

    x = rLinear * 0.4124 + gLinear * 0.3576 + bLinear * 0.1805;
    y = rLinear * 0.2126 + gLinear * 0.7152 + bLinear * 0.0722;
    z = rLinear * 0.0193 + gLinear * 0.1192 + bLinear * 0.9505;
}



namespace cie
{

//------------------------------------------------------------------------------
inline double pow2(double x)
{
    return x * x;
}

//------------------------------------------------------------------------------
inline double pivot_xyz(double n)
{
    if (n > 0.008856)   return pow(n, 1.0 / 3.0);
    else                return (7.787 * n) + (16.0 / 116.0);
}

//------------------------------------------------------------------------------
void lab::from_rgb(COLORREF c)
{
    xyz xyz(c);

#if 1
    // Observer 2°, Illuminant D65 (CIE 1931)
    constexpr double ref_x =  95.047;
    constexpr double ref_y = 100.000;
    constexpr double ref_z = 108.883;
#else
    // Observer 10°, Illuminant D65 (CIE 1964)
    constexpr double ref_x =  94.811;
    constexpr double ref_y = 100.000;
    constexpr double ref_z = 107.304;
#endif

    double x = pivot_xyz(xyz.x / ref_x);
    double y = pivot_xyz(xyz.y / ref_y);
    double z = pivot_xyz(xyz.z / ref_z);

    l = (116.0 * y) - 16.0;
    a = 500.0 * (x - y);
    b = 200.0 * (y - z);
}

//------------------------------------------------------------------------------
static double deltaE_2_internal(const lab& lab1, const lab& lab2)
{
    double const deltaE2 = (pow2(lab1.l - lab2.l) +
                            pow2(lab1.a - lab2.a) +
                            pow2(lab1.b - lab2.b));

    return deltaE2;
}

//------------------------------------------------------------------------------
double deltaE_2(const lab& lab1, const lab& lab2)
{
    return deltaE_2_internal(lab1, lab2) * 100.0;
}

//------------------------------------------------------------------------------
#if 0
double deltaE(const lab& lab1, const lab& lab2)
{
    return sqrt(deltaE_2_internal(lab1, lab2)) * 100.0;
}
#endif

//------------------------------------------------------------------------------
#if 0
double deltaE(COLORREF c1, COLORREF c2)
{
    lab lab1(c1);
    lab lab2(c2);

    // Clink only needs the least delta, so sqrt() isn't necessary.
    return deltaE_2(lab1, lab2);
}
#endif

};
