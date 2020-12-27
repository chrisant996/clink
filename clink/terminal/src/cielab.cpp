// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "cielab.h"

#include <math.h>

//------------------------------------------------------------------------------
struct xyz
{
    xyz() = default;
    xyz(COLORREF c) { from_rgb(c); }

    void from_rgb(COLORREF c);
    COLORREF to_rgb() const;

    double x = 0;
    double y = 0;
    double z = 0;

    // RGB to XYZ conversion math shared by each channel
    static double SRGBtoLinear(BYTE val)
    {
        double d = double(val) / 255;
        if (d > 0.04045)        d = pow(((d + 0.055) / 1.055), 2.4);
        else                    d = (d / 12.92);
        return max(min(d, 1.0), 0.0);
    }

    // XYZ to RGB conversion math shared by each channel
    static BYTE LinearToSRGB(double val)
    {
        if (val > 0.0031308)    val = (1.055 * pow(val, (double(1) / 2.4)) - 0.055);
        else                    val = (val * 12.92);
        return BYTE(max(min(val * 255.0, 255.0), 0.0));
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

//------------------------------------------------------------------------------
COLORREF xyz::to_rgb() const
{
    double rLinear = x * 3.2406 + y * -1.5372 + z * -0.4986;
    double gLinear = x * -0.9689 + y * 1.8758 + z * 0.0415;
    double bLinear = x * 0.0557 + y * -0.2040 + z * 1.0570;

    return RGB(LinearToSRGB(rLinear),
               LinearToSRGB(gLinear),
               LinearToSRGB(bLinear));
}



namespace cie
{

//------------------------------------------------------------------------------
void lab::from_rgb(COLORREF c)
{
    xyz xyz(c);

    double x = xyz.x / 100; // Equal Energy Reference-X
    double y = xyz.y / 100; // Equal Energy Reference-Y
    double z = xyz.z / 100; // Equal Energy Reference-Z

    if (x > 0.008856)   x = pow(x, 1.0 / 3);
    else                x = (7.787 * x) + (16.0 / 116);
    if (y > 0.008856)   y = pow(y, 1.0 / 3);
    else                y = (7.787 * y) + (16.0 / 116);
    if (z > 0.008856)   z = pow(z, 1.0 / 3);
    else                z = (7.787 * z) + (16.0 / 116);

    l = (116.0 * y) - 16;
    a = 500.0 * (x - y);
    b = 200.0 * (y - z);
}

//------------------------------------------------------------------------------
float deltaE2(const lab& lab1, const lab& lab2)
{
    double const deltaE2 = (pow2(lab1.l - lab2.l) +
                            pow2(lab1.a - lab2.a) +
                            pow2(lab1.b - lab2.b));

    return float(deltaE2) * 100;
}

//------------------------------------------------------------------------------
float deltaE(const lab& lab1, const lab& lab2)
{
    double const deltaE = sqrt(pow2(lab1.l - lab2.l) +
                               pow2(lab1.a - lab2.a) +
                               pow2(lab1.b - lab2.b));

    return float(deltaE) * 100;
}

//------------------------------------------------------------------------------
float deltaE(COLORREF c1, COLORREF c2)
{
    lab lab1(c1);
    lab lab2(c2);

    return deltaE(lab1, lab2);
}

};
