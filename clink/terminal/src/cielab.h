// Copyright (c) 2020 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

namespace cie
{

//------------------------------------------------------------------------------
struct lab
{
    lab() = default;
    lab(COLORREF c) { from_rgb(c); }

    void from_rgb(COLORREF c);

    double l = 0;
    double a = 0;
    double b = 0;
};

//------------------------------------------------------------------------------
// Delta E* CIE (1976)
double deltaE_2(const lab& lab1, const lab& lab2);      // skips sqrt()
//double deltaE(const lab& lab1, const lab& lab2);
//double deltaE(COLORREF c1, COLORREF c2);

//------------------------------------------------------------------------------
// Delta E 1994 gets confused too easily by luminance.
// Delta E 2000 uses a lot of trigonometric ratios, and looks too slow.

};
