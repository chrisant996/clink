// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "attributes.h" // for RGB_t

#include <cmath>
#include <array>
#include <algorithm>
#include <assert.h>

constexpr double M_PI = 3.14159265358979323846;

// Lab color space representation.
struct Lab_t
{
    double L;
    double a;
    double b;
};

// LCH (cylindrical Lab) representation.
struct LCH_t
{
    double L;
    double C;
    double H;
};

// XYZ representation.
struct XYZ_t
{
    double X;
    double Y;
    double Z;
};

// Minimum chroma to consider hue meaningful.
// Below this, colors are essentially gray.
static const double MIN_CHROMA_FOR_HUE = 2.0;

inline double gammaCorrect(double channel)
{
    if (channel <= 0.04045)
        return channel / 12.92;
    else
        return pow((channel + 0.055) / 1.055, 2.4);
}

// Convert RGB to XYZ color space (D65 illuminant).
void RGBtoXYZ(const RGB_t& rgb, XYZ_t& xyz)
{
    // Normalize RGB to [0, 1].
    double r = rgb.r / 255.0;
    double g = rgb.g / 255.0;
    double b = rgb.b / 255.0;

    // Apply gamma correction (sRGB).
    r = gammaCorrect(r);
    g = gammaCorrect(g);
    b = gammaCorrect(b);

    // Convert to XYZ (D65 illuminant).
    xyz.X = r * 0.4124564 + g * 0.3575761 + b * 0.1804375;
    xyz.Y = r * 0.2126729 + g * 0.7151522 + b * 0.0721750;
    xyz.Z = r * 0.0193339 + g * 0.1191920 + b * 0.9503041;
}

inline double f(double t)
{
    const double delta = 6.0 / 29.0;
    if (t > delta * delta * delta)
        return pow(t, 1.0 / 3.0);
    else
        return t / (3.0 * delta * delta) + 4.0 / 29.0;
}

// Convert XYZ to Lab color space.
Lab_t XYZtoLab(const XYZ_t& xyz)
{
    // D65 reference white point.
    // Observer 2°, Illuminant D65 (CIE 1931).
    const double Xn = 0.95047;
    const double Yn = 1.00000;
    const double Zn = 1.08883;

    double fx = f(xyz.X / Xn);
    double fy = f(xyz.Y / Yn);
    double fz = f(xyz.Z / Zn);

    Lab_t lab;
    lab.L = 116.0 * fy - 16.0;
    lab.a = 500.0 * (fx - fy);
    lab.b = 200.0 * (fy - fz);

    return lab;
}

// Convert RGB to Lab.
Lab_t RGBtoLab(const RGB_t& rgb)
{
    XYZ_t xyz;
    RGBtoXYZ(rgb, xyz);
    return XYZtoLab(xyz);
}

// Convert Lab to LCH.
LCH_t LabtoLCH(const Lab_t& lab)
{
    LCH_t lch;
    lch.L = lab.L;
    lch.C = sqrt(lab.a * lab.a + lab.b * lab.b);
    lch.H = atan2(lab.b, lab.a) * 180.0 / M_PI;

    // Normalize hue to [0, 360).
    if (lch.H < 0)
        lch.H += 360.0;

    return lch;
}

inline double compute_h_prime(double a_prime, double b)
{
    if (a_prime == 0 && b == 0)
        return 0.0;
    double h = atan2(b, a_prime) * 180.0 / M_PI;
    if (h < 0)
        h += 360.0;
    return h;
}

// Calculate ΔE2000 between two Lab colors.
double DeltaE2000(const Lab_t& lab1, const Lab_t& lab2)
{
    // Calculate C (chroma).
    double C1 = sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    double C2 = sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    double C_bar = (C1 + C2) / 2.0;

    // Calculate G.
    double C_bar_7 = pow(C_bar, 7.0);
    double G = 0.5 * (1.0 - sqrt(C_bar_7 / (C_bar_7 + pow(25.0, 7.0))));

    // Calculate a'.
    double a1_prime = lab1.a * (1.0 + G);
    double a2_prime = lab2.a * (1.0 + G);

    // Calculate C'.
    double C1_prime = sqrt(a1_prime * a1_prime + lab1.b * lab1.b);
    double C2_prime = sqrt(a2_prime * a2_prime + lab2.b * lab2.b);

    // Calculate h'.
    double h1_prime = compute_h_prime(a1_prime, lab1.b);
    double h2_prime = compute_h_prime(a2_prime, lab2.b);

    // Calculate ΔL', ΔC', ΔH'.
    double delta_L_prime = lab2.L - lab1.L;
    double delta_C_prime = C2_prime - C1_prime;

    double delta_h_prime;
    if (C1_prime * C2_prime == 0)
    {
        delta_h_prime = 0;
    }
    else
    {
        double diff = h2_prime - h1_prime;
        if (fabs(diff) <= 180.0)
            delta_h_prime = diff;
        else if (diff > 180.0)
            delta_h_prime = diff - 360.0;
        else
            delta_h_prime = diff + 360.0;
    }

    double delta_H_prime = 2.0 * sqrt(C1_prime * C2_prime) * sin(delta_h_prime * M_PI / 360.0);

    // Calculate mean values.
    double L_bar_prime = (lab1.L + lab2.L) / 2.0;
    double C_bar_prime = (C1_prime + C2_prime) / 2.0;

    double h_bar_prime;
    if (C1_prime * C2_prime == 0)
    {
        h_bar_prime = h1_prime + h2_prime;
    }
    else
    {
        double sum = h1_prime + h2_prime;
        double diff = fabs(h1_prime - h2_prime);
        if (diff <= 180.0)
            h_bar_prime = sum / 2.0;
        else if (sum < 360.0)
            h_bar_prime = (sum + 360.0) / 2.0;
        else
            h_bar_prime = (sum - 360.0) / 2.0;
    }

    // Calculate T.
    double T = 1.0 - 0.17 * cos((h_bar_prime - 30.0) * M_PI / 180.0)
                   + 0.24 * cos(2.0 * h_bar_prime * M_PI / 180.0)
                   + 0.32 * cos((3.0 * h_bar_prime + 6.0) * M_PI / 180.0)
                   - 0.20 * cos((4.0 * h_bar_prime - 63.0) * M_PI / 180.0);

    // Calculate S_L, S_C, S_H.
    double L_bar_prime_minus_50_sq = (L_bar_prime - 50.0) * (L_bar_prime - 50.0);
    double S_L = 1.0 + (0.015 * L_bar_prime_minus_50_sq) / sqrt(20.0 + L_bar_prime_minus_50_sq);
    double S_C = 1.0 + 0.045 * C_bar_prime;
    double S_H = 1.0 + 0.015 * C_bar_prime * T;

    // Calculate R_T.
    double delta_theta = 30.0 * exp(-((h_bar_prime - 275.0) / 25.0) * ((h_bar_prime - 275.0) / 25.0));
    double C_bar_prime_7 = pow(C_bar_prime, 7.0);
    double R_C = 2.0 * sqrt(C_bar_prime_7 / (C_bar_prime_7 + pow(25.0, 7.0)));
    double R_T = -sin(2.0 * delta_theta * M_PI / 180.0) * R_C;

    // Calculate ΔE2000.
    // Weighting factors:  tuned for terminal color matching (default is 1.0).
    // Lower k_C and k_H values make chroma and hue differences less costly,
    // which increases the relative importance of matching hue/chroma compared
    // to lightness.  This prevents dark saturated colors from matching to
    // grays just because the lightness is similar.
    double k_L = 1.0;
    double k_C = 0.7;
    double k_H = 0.9;
    double term1 = delta_L_prime / (k_L * S_L);
    double term2 = delta_C_prime / (k_C * S_C);
    double term3 = delta_H_prime / (k_H * S_H);
    return sqrt(term1 * term1 + term2 * term2 + term3 * term3 + R_T * term2 * term3);
}

// Find the best palette match using weighted ΔE2000.
int FindBestPaletteMatch(const RGB_t& input, const RGB_t (&palette)[16])
{
    Lab_t inputLab = RGBtoLab(input);
    LCH_t inputLCH = LabtoLCH(inputLab);
    const bool inputAchromatic = (inputLCH.C < MIN_CHROMA_FOR_HUE);

    int darkestIndex = -1;
    double darkestL = std::numeric_limits<double>::infinity();

    // Calculate ΔE2000 value for each palette entry.
    Lab_t paletteLab[_countof(palette)];
    LCH_t paletteLCH[_countof(palette)];
    double deltaE[_countof(palette)];
    for (int i = 0; i < _countof(palette); i++) {
        auto& Lab = paletteLab[i];
        auto& LCH = paletteLCH[i];
        Lab = RGBtoLab(palette[i]);
        LCH = LabtoLCH(Lab);
        deltaE[i] = DeltaE2000(inputLab, Lab);

        // Find the darkest achromatic palette color (the palette's equivalent
        // to black).
        if (inputAchromatic && LCH.C < MIN_CHROMA_FOR_HUE) {
            if (darkestL > Lab.L) {
                darkestL = Lab.L;
                darkestIndex = i;
            }
        }
    }

    // Penalize the darkest achromatic color for inputs that aren't very dark.
    // This prevents mapping mid-grays like (78,78,78) to black.
    // Inflection point around RGB(58,58,58) which is L* ≈ 24.
    if (darkestIndex >= 0) {
        if (inputLab.L >= 24 && darkestL < 10.0) {
            // When input is above the threshold, be reluctant to match the
            // darkest palette color (the palette's equivalent to black)
            // unless it's much closer than the next candidate.
            const double penalty = 10;
            deltaE[darkestIndex] += penalty;
        }
    }

    // The best match is the palette entry with the lowest ΔE2000.
    int bestIndex = -1;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < _countof(palette); i++) {
        if (best > deltaE[i]) {
            best = deltaE[i];
            bestIndex = i;
        }
    }

    return bestIndex;
}
