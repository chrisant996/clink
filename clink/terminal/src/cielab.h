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

    bool operator==(lab const &lab) { return !memcmp(this, &lab, sizeof(lab)); }

    double l = 0;
    double a = 0;
    double b = 0;
};

//------------------------------------------------------------------------------
inline double pow2(double x)
{
    return x * x;
}

//------------------------------------------------------------------------------
float deltaE2(const lab& lab1, const lab& lab2);    // squared
float deltaE(const lab& lab1, const lab& lab2);     // sqrt()
float deltaE(COLORREF c1, COLORREF c2);

};
