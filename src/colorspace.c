/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>

#include "common.h"

bool pl_color_system_is_ycbcr_like(enum pl_color_system sys)
{
    switch (sys) {
    case PL_COLOR_SYSTEM_UNKNOWN:
    case PL_COLOR_SYSTEM_RGB:
    case PL_COLOR_SYSTEM_XYZ:
        return false;
    case PL_COLOR_SYSTEM_BT_601:
    case PL_COLOR_SYSTEM_BT_709:
    case PL_COLOR_SYSTEM_SMPTE_240M:
    case PL_COLOR_SYSTEM_BT_2020_NC:
    case PL_COLOR_SYSTEM_BT_2020_C:
    case PL_COLOR_SYSTEM_BT_2100_PQ:
    case PL_COLOR_SYSTEM_BT_2100_HLG:
    case PL_COLOR_SYSTEM_YCGCO:
        return true;
    case PL_COLOR_SYSTEM_COUNT: break;
    };

    abort();
}

bool pl_color_system_is_linear(enum pl_color_system sys)
{
    switch (sys) {
    case PL_COLOR_SYSTEM_UNKNOWN:
    case PL_COLOR_SYSTEM_RGB:
    case PL_COLOR_SYSTEM_BT_601:
    case PL_COLOR_SYSTEM_BT_709:
    case PL_COLOR_SYSTEM_SMPTE_240M:
    case PL_COLOR_SYSTEM_BT_2020_NC:
    case PL_COLOR_SYSTEM_YCGCO:
        return true;
    case PL_COLOR_SYSTEM_BT_2020_C:
    case PL_COLOR_SYSTEM_BT_2100_PQ:
    case PL_COLOR_SYSTEM_BT_2100_HLG:
    case PL_COLOR_SYSTEM_XYZ:
        return false;
    case PL_COLOR_SYSTEM_COUNT: break;
    };

    abort();
}

enum pl_color_system pl_color_system_guess_ycbcr(int width, int height)
{
    if (width >= 1280 || height > 576) {
        // Typical HD content
        return PL_COLOR_SYSTEM_BT_709;
    } else {
        // Typical SD content
        return PL_COLOR_SYSTEM_BT_601;
    }
}

bool pl_bit_encoding_equal(const struct pl_bit_encoding *b1,
                           const struct pl_bit_encoding *b2)
{
    return b1->sample_depth == b2->sample_depth &&
           b1->color_depth  == b2->color_depth &&
           b1->bit_shift    == b2->bit_shift;
}

const struct pl_color_repr pl_color_repr_unknown = {0};

const struct pl_color_repr pl_color_repr_rgb = {
    .sys    = PL_COLOR_SYSTEM_RGB,
    .levels = PL_COLOR_LEVELS_PC,
};

const struct pl_color_repr pl_color_repr_sdtv = {
    .sys    = PL_COLOR_SYSTEM_BT_601,
    .levels = PL_COLOR_LEVELS_TV,
};

const struct pl_color_repr pl_color_repr_hdtv = {
    .sys    = PL_COLOR_SYSTEM_BT_709,
    .levels = PL_COLOR_LEVELS_TV,
};

const struct pl_color_repr pl_color_repr_uhdtv = {
    .sys    = PL_COLOR_SYSTEM_BT_2020_NC,
    .levels = PL_COLOR_LEVELS_TV,
};

const struct pl_color_repr pl_color_repr_jpeg = {
    .sys    = PL_COLOR_SYSTEM_BT_601,
    .levels = PL_COLOR_LEVELS_PC,
};

bool pl_color_repr_equal(const struct pl_color_repr *c1,
                         const struct pl_color_repr *c2)
{
    return c1->sys    == c2->sys &&
           c1->levels == c2->levels &&
           c1->alpha  == c2->alpha &&
           pl_bit_encoding_equal(&c1->bits, &c2->bits);
}

static struct pl_bit_encoding pl_bit_encoding_merge(const struct pl_bit_encoding *orig,
                                                    const struct pl_bit_encoding *new)
{
    return (struct pl_bit_encoding) {
        .sample_depth = PL_DEF(orig->sample_depth, new->sample_depth),
        .color_depth  = PL_DEF(orig->color_depth,  new->color_depth),
        .bit_shift    = PL_DEF(orig->bit_shift,    new->bit_shift),
    };
}

void pl_color_repr_merge(struct pl_color_repr *orig,
                         const struct pl_color_repr *new)
{
    *orig = (struct pl_color_repr) {
        .sys    = PL_DEF(orig->sys,    new->sys),
        .levels = PL_DEF(orig->levels, new->levels),
        .alpha  = PL_DEF(orig->alpha,  new->alpha),
        .bits   = pl_bit_encoding_merge(&orig->bits, &new->bits),
    };
}

static enum pl_color_levels guess_levels(const struct pl_color_repr *repr)
{
    if (repr->levels)
        return repr->levels;

    return pl_color_system_is_ycbcr_like(repr->sys)
                ? PL_COLOR_LEVELS_TV
                : PL_COLOR_LEVELS_PC;
}

float pl_color_repr_normalize(struct pl_color_repr *repr)
{
    float scale = 1.0;
    struct pl_bit_encoding *bits = &repr->bits;

    if (bits->bit_shift) {
        scale /= (1LL << bits->bit_shift);
        bits->bit_shift = 0;
    }

    int tex_bits = PL_DEF(bits->sample_depth, 8);
    int col_bits = PL_DEF(bits->color_depth,  8);

    if (guess_levels(repr) == PL_COLOR_LEVELS_TV) {
        // Limit range is always shifted directly
        scale *= (float) (1LL << tex_bits) / (1LL << col_bits);
    } else {
        // Full range always uses the full range available
        scale *= ((1LL << tex_bits) - 1.) / ((1LL << col_bits) - 1.);
    }

    bits->sample_depth = bits->color_depth;
    return scale;
}

bool pl_color_primaries_is_wide_gamut(enum pl_color_primaries prim)
{
    switch (prim) {
    case PL_COLOR_PRIM_UNKNOWN:
    case PL_COLOR_PRIM_BT_601_525:
    case PL_COLOR_PRIM_BT_601_625:
    case PL_COLOR_PRIM_BT_709:
    case PL_COLOR_PRIM_BT_470M:
        return false;
    case PL_COLOR_PRIM_BT_2020:
    case PL_COLOR_PRIM_APPLE:
    case PL_COLOR_PRIM_ADOBE:
    case PL_COLOR_PRIM_PRO_PHOTO:
    case PL_COLOR_PRIM_CIE_1931:
    case PL_COLOR_PRIM_DCI_P3:
    case PL_COLOR_PRIM_DISPLAY_P3:
    case PL_COLOR_PRIM_V_GAMUT:
    case PL_COLOR_PRIM_S_GAMUT:
        return true;
    case PL_COLOR_PRIM_COUNT: break;
    }

    abort();
}

enum pl_color_primaries pl_color_primaries_guess(int width, int height)
{
    // HD content
    if (width >= 1280 || height > 576)
        return PL_COLOR_PRIM_BT_709;

    switch (height) {
    case 576: // Typical PAL content, including anamorphic/squared
        return PL_COLOR_PRIM_BT_601_625;

    case 480: // Typical NTSC content, including squared
    case 486: // NTSC Pro or anamorphic NTSC
        return PL_COLOR_PRIM_BT_601_525;

    default: // No good metric, just pick BT.709 to minimize damage
        return PL_COLOR_PRIM_BT_709;
    }
}

float pl_color_transfer_nominal_peak(enum pl_color_transfer trc)
{
    switch (trc) {
    case PL_COLOR_TRC_UNKNOWN:
    case PL_COLOR_TRC_BT_1886:
    case PL_COLOR_TRC_SRGB:
    case PL_COLOR_TRC_LINEAR:
    case PL_COLOR_TRC_GAMMA18:
    case PL_COLOR_TRC_GAMMA22:
    case PL_COLOR_TRC_GAMMA28:
    case PL_COLOR_TRC_PRO_PHOTO:
        return 1.0;
    case PL_COLOR_TRC_PQ:       return 10000.0 / PL_COLOR_REF_WHITE;
    case PL_COLOR_TRC_HLG:      return 12.0;
    case PL_COLOR_TRC_V_LOG:    return 46.0855;
    case PL_COLOR_TRC_S_LOG1:   return 6.52;
    case PL_COLOR_TRC_S_LOG2:   return 9.212;
    case PL_COLOR_TRC_COUNT: break;
    }

    abort();
}

bool pl_color_light_is_scene_referred(enum pl_color_light light)
{
    switch (light) {
    case PL_COLOR_LIGHT_UNKNOWN:
    case PL_COLOR_LIGHT_DISPLAY:
        return false;
    case PL_COLOR_LIGHT_SCENE_HLG:
    case PL_COLOR_LIGHT_SCENE_709_1886:
    case PL_COLOR_LIGHT_SCENE_1_2:
        return true;
    case PL_COLOR_LIGHT_COUNT: break;
    }

    abort();
}

const struct pl_color_space pl_color_space_unknown = {0};

const struct pl_color_space pl_color_space_srgb = {
    .primaries = PL_COLOR_PRIM_BT_709,
    .transfer  = PL_COLOR_TRC_SRGB,
    .light     = PL_COLOR_LIGHT_DISPLAY,
};

const struct pl_color_space pl_color_space_bt709 = {
    .primaries = PL_COLOR_PRIM_BT_709,
    .transfer  = PL_COLOR_TRC_BT_1886,
    .light     = PL_COLOR_LIGHT_DISPLAY,
};

const struct pl_color_space pl_color_space_hdr10 = {
    .primaries = PL_COLOR_PRIM_BT_2020,
    .transfer  = PL_COLOR_TRC_PQ,
    .light     = PL_COLOR_LIGHT_DISPLAY,
};

const struct pl_color_space pl_color_space_bt2020_hlg = {
    .primaries = PL_COLOR_PRIM_BT_2020,
    .transfer  = PL_COLOR_TRC_HLG,
    .light     = PL_COLOR_LIGHT_SCENE_HLG,
};

const struct pl_color_space pl_color_space_monitor = {
    .primaries = PL_COLOR_PRIM_BT_709, // sRGB primaries
    .transfer  = PL_COLOR_TRC_GAMMA22, // typical response
    .light     = PL_COLOR_LIGHT_DISPLAY,
};

bool pl_color_space_is_hdr(struct pl_color_space csp)
{
    float peak = pl_color_transfer_nominal_peak(csp.transfer);
    peak *= PL_DEF(csp.sig_scale, 1.0);

    return peak > 1.0;
}

void pl_color_space_merge(struct pl_color_space *orig,
                          const struct pl_color_space *new)
{
    if (!orig->primaries)
        orig->primaries = new->primaries;
    if (!orig->transfer)
        orig->transfer = new->transfer;
    if (!orig->light)
        orig->light = new->light;
    if (!orig->sig_peak)
        orig->sig_peak = new->sig_peak;
    if (!orig->sig_avg)
        orig->sig_avg = new->sig_avg;
    if (!orig->sig_scale)
        orig->sig_scale = new->sig_scale;
}

bool pl_color_space_equal(const struct pl_color_space *c1,
                          const struct pl_color_space *c2)
{
    return c1->primaries == c2->primaries &&
           c1->transfer  == c2->transfer &&
           c1->light     == c2->light &&
           c1->sig_peak  == c2->sig_peak &&
           c1->sig_avg   == c2->sig_avg &&
           c1->sig_scale == c2->sig_scale;
}

// Average light level for SDR signals. This is equal to a signal level of 0.5
// under a typical presentation gamma of about 2.0.
static const float sdr_avg = 0.25;

void pl_color_space_infer(struct pl_color_space *space)
{
    if (!space->primaries)
        space->primaries = PL_COLOR_PRIM_BT_709;
    if (!space->transfer)
        space->transfer = PL_COLOR_TRC_GAMMA22;
    if (!space->light) {
        space->light = (space->transfer == PL_COLOR_TRC_HLG)
            ? PL_COLOR_LIGHT_SCENE_HLG
            : PL_COLOR_LIGHT_DISPLAY;
    }
    if (!space->sig_peak) {
        space->sig_peak = pl_color_transfer_nominal_peak(space->transfer);

        // Exception: For HLG content, we want to infer a value of 1000 cd/m²
        // (corresponding to a peak of 10.0) instead of the true nominal peak
        // of 12.0. A peak of 1000 is considered the "reference" HLG display.
        if (space->transfer == PL_COLOR_TRC_HLG)
            space->sig_peak = 10.0;
    }

    if (!space->sig_scale)
        space->sig_scale = 1.0;

    // In theory, for HDR signals, this is typically no longer true - but
    // without adequate metadata there's not much else we can assume
    if (!space->sig_avg)
        space->sig_avg = sdr_avg / space->sig_scale;
}

const struct pl_color_adjustment pl_color_adjustment_neutral = {
    .brightness = 0.0,
    .contrast   = 1.0,
    .saturation = 1.0,
    .hue        = 0.0,
    .gamma      = 1.0,
};

void pl_chroma_location_offset(enum pl_chroma_location loc, float *x, float *y)
{
    *x = *y = 0;

    switch (loc) {
    case PL_CHROMA_LEFT:
    case PL_CHROMA_TOP_LEFT:
    case PL_CHROMA_BOTTOM_LEFT:
        *x = -0.5;
        break;
    default: break;
    }

    switch (loc) {
    case PL_CHROMA_TOP_LEFT:
    case PL_CHROMA_TOP_CENTER:
        *y = -0.5;
        break;
    default: break;
    }

    switch (loc) {
    case PL_CHROMA_BOTTOM_LEFT:
    case PL_CHROMA_BOTTOM_CENTER:
        *y = 0.5;
        break;
    default: break;
    }
}

const struct pl_raw_primaries *pl_raw_primaries_get(enum pl_color_primaries prim)
{
    /*
    Values from: ITU-R Recommendations BT.470-6, BT.601-7, BT.709-5, BT.2020-0

    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.470-6-199811-S!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.601-7-201103-I!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.709-5-200204-I!!PDF-E.pdf
    https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-0-201208-I!!PDF-E.pdf

    Other colorspaces from https://en.wikipedia.org/wiki/RGB_color_space#Specifications
    */

    // CIE standard illuminant series
#define CIE_D50 {0.34577, 0.35850}
#define CIE_D65 {0.31271, 0.32902}
#define CIE_DCI {0.31400, 0.35100}
#define CIE_C   {0.31006, 0.31616}
#define CIE_E   {1.0/3.0, 1.0/3.0}

    static const struct pl_raw_primaries primaries[] = {
        [PL_COLOR_PRIM_BT_470M] = {
            .red   = {0.670, 0.330},
            .green = {0.210, 0.710},
            .blue  = {0.140, 0.080},
            .white = CIE_C,
        },

        [PL_COLOR_PRIM_BT_601_525] = {
            .red   = {0.630, 0.340},
            .green = {0.310, 0.595},
            .blue  = {0.155, 0.070},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_BT_601_625] = {
            .red   = {0.640, 0.330},
            .green = {0.290, 0.600},
            .blue  = {0.150, 0.060},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_BT_709] = {
            .red   = {0.640, 0.330},
            .green = {0.300, 0.600},
            .blue  = {0.150, 0.060},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_BT_2020] = {
            .red   = {0.708, 0.292},
            .green = {0.170, 0.797},
            .blue  = {0.131, 0.046},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_APPLE] = {
            .red   = {0.625, 0.340},
            .green = {0.280, 0.595},
            .blue  = {0.115, 0.070},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_ADOBE] = {
            .red   = {0.640, 0.330},
            .green = {0.210, 0.710},
            .blue  = {0.150, 0.060},
            .white = CIE_D65,
        },
        [PL_COLOR_PRIM_PRO_PHOTO] = {
            .red   = {0.7347, 0.2653},
            .green = {0.1596, 0.8404},
            .blue  = {0.0366, 0.0001},
            .white = CIE_D50,
        },
        [PL_COLOR_PRIM_CIE_1931] = {
            .red   = {0.7347, 0.2653},
            .green = {0.2738, 0.7174},
            .blue  = {0.1666, 0.0089},
            .white = CIE_E,
        },
    // From SMPTE RP 431-2
        [PL_COLOR_PRIM_DCI_P3] = {
            .red   = {0.680, 0.320},
            .green = {0.265, 0.690},
            .blue  = {0.150, 0.060},
            .white = CIE_DCI,
        },
        [PL_COLOR_PRIM_DISPLAY_P3] = {
            .red   = {0.680, 0.320},
            .green = {0.265, 0.690},
            .blue  = {0.150, 0.060},
            .white = CIE_D65,
        },
    // From Panasonic VARICAM reference manual
        [PL_COLOR_PRIM_V_GAMUT] = {
            .red   = {0.730, 0.280},
            .green = {0.165, 0.840},
            .blue  = {0.100, -0.03},
            .white = CIE_D65,
        },
    // From Sony S-Log reference manual
        [PL_COLOR_PRIM_S_GAMUT] = {
            .red   = {0.730, 0.280},
            .green = {0.140, 0.855},
            .blue  = {0.100, -0.05},
            .white = CIE_D65,
        },
    };

    // This is the default assumption if no colorspace information could
    // be determined, eg. for files which have no video channel.
    if (!prim)
        prim = PL_COLOR_PRIM_BT_709;

    pl_assert(prim < PL_ARRAY_SIZE(primaries));
    return &primaries[prim];
}

// Compute the RGB/XYZ matrix as described here:
// http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
struct pl_matrix3x3 pl_get_rgb2xyz_matrix(const struct pl_raw_primaries *prim)
{
    struct pl_matrix3x3 out = {{{0}}};
    float S[3], X[4], Z[4];

    // Convert from CIE xyY to XYZ. Note that Y=1 holds true for all primaries
    X[0] = prim->red.x   / prim->red.y;
    X[1] = prim->green.x / prim->green.y;
    X[2] = prim->blue.x  / prim->blue.y;
    X[3] = prim->white.x / prim->white.y;

    Z[0] = (1 - prim->red.x   - prim->red.y)   / prim->red.y;
    Z[1] = (1 - prim->green.x - prim->green.y) / prim->green.y;
    Z[2] = (1 - prim->blue.x  - prim->blue.y)  / prim->blue.y;
    Z[3] = (1 - prim->white.x - prim->white.y) / prim->white.y;

    // S = XYZ^-1 * W
    for (int i = 0; i < 3; i++) {
        out.m[0][i] = X[i];
        out.m[1][i] = 1;
        out.m[2][i] = Z[i];
    }

    pl_matrix3x3_invert(&out);

    for (int i = 0; i < 3; i++)
        S[i] = out.m[i][0] * X[3] + out.m[i][1] * 1 + out.m[i][2] * Z[3];

    // M = [Sc * XYZc]
    for (int i = 0; i < 3; i++) {
        out.m[0][i] = S[i] * X[i];
        out.m[1][i] = S[i] * 1;
        out.m[2][i] = S[i] * Z[i];
    }

    return out;
}

struct pl_matrix3x3 pl_get_xyz2rgb_matrix(const struct pl_raw_primaries *prim)
{
    // For simplicity, just invert the rgb2xyz matrix
    struct pl_matrix3x3 out = pl_get_rgb2xyz_matrix(prim);
    pl_matrix3x3_invert(&out);
    return out;
}

// LMS<-XYZ revised matrix from CIECAM97, based on a linear transform and
// normalized for equal energy on monochrome inputs
static const struct pl_matrix3x3 m_cat97 = {{
    {  0.8562,  0.3372, -0.1934 },
    { -0.8360,  1.8327,  0.0033 },
    {  0.0357, -0.0469,  1.0112 },
}};

// M := M * XYZd<-XYZs
static void apply_chromatic_adaptation(struct pl_cie_xy src,
                                       struct pl_cie_xy dest,
                                       struct pl_matrix3x3 *mat)
{
    // If the white points are nearly identical, this is a wasteful identity
    // operation.
    if (fabs(src.x - dest.x) < 1e-6 && fabs(src.y - dest.y) < 1e-6)
        return;

    // XYZd<-XYZs = Ma^-1 * (I*[Cd/Cs]) * Ma
    // http://www.brucelindbloom.com/index.html?Eqn_ChromAdapt.html
    // For Ma, we use the CIECAM97 revised (linear) matrix
    float C[3][2];

    for (int i = 0; i < 3; i++) {
        // source cone
        C[i][0] = m_cat97.m[i][0] * pl_cie_X(src)
                + m_cat97.m[i][1] * 1
                + m_cat97.m[i][2] * pl_cie_Z(src);

        // dest cone
        C[i][1] = m_cat97.m[i][0] * pl_cie_X(dest)
                + m_cat97.m[i][1] * 1
                + m_cat97.m[i][2] * pl_cie_Z(dest);
    }

    // tmp := I * [Cd/Cs] * Ma
    struct pl_matrix3x3 tmp = {0};
    for (int i = 0; i < 3; i++)
        tmp.m[i][i] = C[i][1] / C[i][0];

    pl_matrix3x3_mul(&tmp, &m_cat97);

    // M := M * Ma^-1 * tmp
    struct pl_matrix3x3 ma_inv = m_cat97;
    pl_matrix3x3_invert(&ma_inv);
    pl_matrix3x3_mul(mat, &ma_inv);
    pl_matrix3x3_mul(mat, &tmp);
}

const struct pl_cone_params pl_vision_normal        = {PL_CONE_NONE, 1.0};
const struct pl_cone_params pl_vision_protanomaly   = {PL_CONE_L,    0.5};
const struct pl_cone_params pl_vision_protanopia    = {PL_CONE_L,    0.0};
const struct pl_cone_params pl_vision_deuteranomaly = {PL_CONE_M,    0.5};
const struct pl_cone_params pl_vision_deuteranopia  = {PL_CONE_M,    0.0};
const struct pl_cone_params pl_vision_tritanomaly   = {PL_CONE_S,    0.5};
const struct pl_cone_params pl_vision_tritanopia    = {PL_CONE_S,    0.0};
const struct pl_cone_params pl_vision_monochromacy  = {PL_CONE_LM,   0.0};
const struct pl_cone_params pl_vision_achromatopsia = {PL_CONE_LMS,  0.0};

struct pl_matrix3x3 pl_get_cone_matrix(const struct pl_cone_params *params,
                                       const struct pl_raw_primaries *prim)
{
    // LMS<-RGB := LMS<-XYZ * XYZ<-RGB
    struct pl_matrix3x3 rgb2lms = m_cat97;
    struct pl_matrix3x3 rgb2xyz = pl_get_rgb2xyz_matrix(prim);
    pl_matrix3x3_mul(&rgb2lms, &rgb2xyz);

    // LMS versions of the two opposing primaries, plus neutral
    float lms_r[3] = {1.0, 0.0, 0.0},
          lms_b[3] = {0.0, 0.0, 1.0},
          lms_w[3] = {1.0, 1.0, 1.0};

    pl_matrix3x3_apply(&rgb2lms, lms_r);
    pl_matrix3x3_apply(&rgb2lms, lms_b);
    pl_matrix3x3_apply(&rgb2lms, lms_w);

    float a, b, c = params->strength;
    struct pl_matrix3x3 distort;

    switch (params->cones) {
    case PL_CONE_NONE:
        return pl_matrix3x3_identity;

    case PL_CONE_L:
        // Solve to preserve neutral and blue
        a = (lms_b[0] - lms_b[2] * lms_w[0] / lms_w[2]) /
            (lms_b[1] - lms_b[2] * lms_w[1] / lms_w[2]);
        b = (lms_b[0] - lms_b[1] * lms_w[0] / lms_w[1]) /
            (lms_b[2] - lms_b[1] * lms_w[2] / lms_w[1]);
        assert(fabs(a * lms_w[1] + b * lms_w[2] - lms_w[0]) < 1e-6);

        distort = (struct pl_matrix3x3) {{
            {            c, (1.0 - c) * a, (1.0 - c) * b},
            {          0.0,           1.0,           0.0},
            {          0.0,           0.0,           1.0},
        }};
        break;

    case PL_CONE_M:
        // Solve to preserve neutral and blue
        a = (lms_b[1] - lms_b[2] * lms_w[1] / lms_w[2]) /
            (lms_b[0] - lms_b[2] * lms_w[0] / lms_w[2]);
        b = (lms_b[1] - lms_b[0] * lms_w[1] / lms_w[0]) /
            (lms_b[2] - lms_b[0] * lms_w[2] / lms_w[0]);
        assert(fabs(a * lms_w[0] + b * lms_w[2] - lms_w[1]) < 1e-6);

        distort = (struct pl_matrix3x3) {{
            {          1.0,           0.0,           0.0},
            {(1.0 - c) * a,             c, (1.0 - c) * b},
            {          0.0,           0.0,           1.0},
        }};
        break;

    case PL_CONE_S:
        // Solve to preserve neutral and red
        a = (lms_r[2] - lms_r[1] * lms_w[2] / lms_w[1]) /
            (lms_r[0] - lms_r[1] * lms_w[0] / lms_w[1]);
        b = (lms_r[2] - lms_r[0] * lms_w[2] / lms_w[0]) /
            (lms_r[1] - lms_r[0] * lms_w[1] / lms_w[0]);
        assert(fabs(a * lms_w[0] + b * lms_w[1] - lms_w[2]) < 1e-6);

        distort = (struct pl_matrix3x3) {{
            {          1.0,           0.0,           0.0},
            {          0.0,           1.0,           0.0},
            {(1.0 - c) * a, (1.0 - c) * b,             c},
        }};
        break;

    case PL_CONE_LM:
        // Solve to preserve neutral
        a = lms_w[0] / lms_w[2];
        b = lms_w[1] / lms_w[2];

        distort = (struct pl_matrix3x3) {{
            {            c,           0.0, (1.0 - c) * a},
            {          0.0,             c, (1.0 - c) * b},
            {          0.0,           0.0,           1.0},
        }};
        break;

    case PL_CONE_MS:
        // Solve to preserve neutral
        a = lms_w[1] / lms_w[0];
        b = lms_w[2] / lms_w[0];

        distort = (struct pl_matrix3x3) {{
            {          1.0,           0.0,           0.0},
            {(1.0 - c) * a,             c,           0.0},
            {(1.0 - c) * b,           0.0,             c},
        }};
        break;

    case PL_CONE_LS:
        // Solve to preserve neutral
        a = lms_w[0] / lms_w[1];
        b = lms_w[2] / lms_w[1];

        distort = (struct pl_matrix3x3) {{
            {            c, (1.0 - c) * a,           0.0},
            {          0.0,           1.0,           0.0},
            {          0.0, (1.0 - c) * b,             c},
        }};
        break;

    case PL_CONE_LMS: {
        // Rod cells only, which can be modelled somewhat as a combination of
        // L and M cones. Either way, this is pushing the limits of the our
        // color model, so this is only a rough approximation.
        const float w[3] = {0.3605, 0.6415, -0.002};
        assert(fabs(w[0] + w[1] + w[2] - 1.0) < 1e-6);

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                distort.m[i][j] = (1.0 - c) * w[j] * lms_w[i] / lms_w[j];
                if (i == j)
                    distort.m[i][j] += c;
            }
        }
        break;
    }

    default: abort();
    }

    // out := RGB<-LMS * distort * LMS<-RGB
    struct pl_matrix3x3 out = rgb2lms;
    pl_matrix3x3_invert(&out);
    pl_matrix3x3_mul(&out, &distort);
    pl_matrix3x3_mul(&out, &rgb2lms);

    return out;
}

struct pl_matrix3x3 pl_get_color_mapping_matrix(const struct pl_raw_primaries *src,
                                                const struct pl_raw_primaries *dst,
                                                enum pl_rendering_intent intent)
{
    // In saturation mapping, we don't care about accuracy and just want
    // primaries to map to primaries, making this an identity transformation.
    if (intent == PL_INTENT_SATURATION)
        return pl_matrix3x3_identity;

    // RGBd<-RGBs = RGBd<-XYZd * XYZd<-XYZs * XYZs<-RGBs
    // Equations from: http://www.brucelindbloom.com/index.html?Math.html
    // Note: Perceptual is treated like relative colorimetric. There's no
    // definition for perceptual other than "make it look good".

    // RGBd<-XYZd matrix
    struct pl_matrix3x3 xyz2rgb_d = pl_get_xyz2rgb_matrix(dst);

    // Chromatic adaptation, except in absolute colorimetric intent
    if (intent != PL_INTENT_ABSOLUTE_COLORIMETRIC)
        apply_chromatic_adaptation(src->white, dst->white, &xyz2rgb_d);

    // XYZs<-RGBs
    struct pl_matrix3x3 rgb2xyz_s = pl_get_rgb2xyz_matrix(src);
    pl_matrix3x3_mul(&xyz2rgb_d, &rgb2xyz_s);
    return xyz2rgb_d;
}

/* Fill in the Y, U, V vectors of a yuv-to-rgb conversion matrix
 * based on the given luma weights of the R, G and B components (lr, lg, lb).
 * lr+lg+lb is assumed to equal 1.
 * This function is meant for colorspaces satisfying the following
 * conditions (which are true for common YUV colorspaces):
 * - The mapping from input [Y, U, V] to output [R, G, B] is linear.
 * - Y is the vector [1, 1, 1].  (meaning input Y component maps to 1R+1G+1B)
 * - U maps to a value with zero R and positive B ([0, x, y], y > 0;
 *   i.e. blue and green only).
 * - V maps to a value with zero B and positive R ([x, y, 0], x > 0;
 *   i.e. red and green only).
 * - U and V are orthogonal to the luma vector [lr, lg, lb].
 * - The magnitudes of the vectors U and V are the minimal ones for which
 *   the image of the set Y=[0...1],U=[-0.5...0.5],V=[-0.5...0.5] under the
 *   conversion function will cover the set R=[0...1],G=[0...1],B=[0...1]
 *   (the resulting matrix can be converted for other input/output ranges
 *   outside this function).
 * Under these conditions the given parameters lr, lg, lb uniquely
 * determine the mapping of Y, U, V to R, G, B.
 */
static struct pl_matrix3x3 luma_coeffs(float lr, float lg, float lb)
{
    pl_assert(fabs(lr+lg+lb - 1) < 1e-6);
    return (struct pl_matrix3x3) {{
        {1, 0,                    2 * (1-lr)          },
        {1, -2 * (1-lb) * lb/lg, -2 * (1-lr) * lr/lg  },
        {1,  2 * (1-lb),          0                   },
    }};
}

struct pl_transform3x3 pl_color_repr_decode(struct pl_color_repr *repr,
                                    const struct pl_color_adjustment *params)
{
    params = PL_DEF(params, &pl_color_adjustment_neutral);

    struct pl_matrix3x3 m;
    switch (repr->sys) {
    case PL_COLOR_SYSTEM_BT_709:     m = luma_coeffs(0.2126, 0.7152, 0.0722); break;
    case PL_COLOR_SYSTEM_BT_601:     m = luma_coeffs(0.2990, 0.5870, 0.1140); break;
    case PL_COLOR_SYSTEM_SMPTE_240M: m = luma_coeffs(0.2122, 0.7013, 0.0865); break;
    case PL_COLOR_SYSTEM_BT_2020_NC: m = luma_coeffs(0.2627, 0.6780, 0.0593); break;
    case PL_COLOR_SYSTEM_BT_2020_C:
        // Note: This outputs into the [-0.5,0.5] range for chroma information.
        m = (struct pl_matrix3x3) {{
            {0, 0, 1},
            {1, 0, 0},
            {0, 1, 0}
        }};
        break;
    case PL_COLOR_SYSTEM_BT_2100_PQ:
    case PL_COLOR_SYSTEM_BT_2100_HLG: {
        // Reversed from the matrix in the spec, hard-coded for efficiency
        // and precision reasons. Exact values truncated from ITU-T H-series
        // Supplement 18.
        static const float lm_t = 0.008609, lm_p = 0.111029625;
        m = (struct pl_matrix3x3) {{
            {1.0,  lm_t,  lm_p},
            {1.0, -lm_t, -lm_p},
            {1.0, 0.560031, -0.320627},
        }};
        break;
    }
    case PL_COLOR_SYSTEM_YCGCO:
        m = (struct pl_matrix3x3) {{
            {1,  -1,  1},
            {1,   1,  0},
            {1,  -1, -1},
        }};
        break;
    case PL_COLOR_SYSTEM_UNKNOWN: // fall through
    case PL_COLOR_SYSTEM_RGB:
        m = pl_matrix3x3_identity;
        break;
    case PL_COLOR_SYSTEM_XYZ: {
        // For lack of anything saner to do, just assume the caller wants
        // BT.709 primaries, which is a reasonable assumption.
        m = pl_get_xyz2rgb_matrix(pl_raw_primaries_get(PL_COLOR_PRIM_BT_709));
        break;
    }
    default: abort();
    }

    struct pl_transform3x3 out = { .mat = m };

    // Apply hue and saturation in the correct way depending on the colorspace.
    if (pl_color_system_is_ycbcr_like(repr->sys)) {
        // Hue is equivalent to rotating input [U, V] subvector around the origin.
        // Saturation scales [U, V].
        float huecos = params->saturation * cos(params->hue);
        float huesin = params->saturation * sin(params->hue);
        for (int i = 0; i < 3; i++) {
            float u = out.mat.m[i][1], v = out.mat.m[i][2];
            out.mat.m[i][1] = huecos * u - huesin * v;
            out.mat.m[i][2] = huesin * u + huecos * v;
        }
    }
    // FIXME: apply saturation for RGB

    int bit_depth = PL_DEF(repr->bits.sample_depth,
                    PL_DEF(repr->bits.color_depth, 8));

    double ymax, ymin, cmax, cmid;
    double scale = (1LL << bit_depth) / ((1LL << bit_depth) - 1.0);

    switch (guess_levels(repr)) {
    case PL_COLOR_LEVELS_TV: {
        ymax = 235 / 256. * scale;
        ymin =  16 / 256. * scale;
        cmax = 240 / 256. * scale;
        cmid = 128 / 256. * scale;
        break;
    }
    case PL_COLOR_LEVELS_PC:
        // Note: For full-range YUV, there are multiple, subtly inconsistent
        // standards. So just pick the sanest implementation, which is to
        // assume MAX_INT == 1.0.
        ymax = 1.0;
        ymin = 0.0;
        cmax = 1.0;
        cmid = 128 / 256. * scale; // *not* exactly 0.5
        break;
    default: abort();
    }

    double ymul = 1.0 / (ymax - ymin);
    double cmul = 0.5 / (cmax - cmid);

    double mul[3]   = { ymul, ymul, ymul };
    double black[3] = { ymin, ymin, ymin };

    if (pl_color_system_is_ycbcr_like(repr->sys)) {
        mul[1]   = mul[2]   = cmul;
        black[1] = black[2] = cmid;
    }

    // Contrast scales the output value range (gain)
    // Brightness scales the constant output bias (black lift/boost)
    for (int i = 0; i < 3; i++) {
        mul[i]   *= params->contrast;
        out.c[i] += params->brightness;
    }

    // Multiply in the texture multiplier and adjust `c` so that black[j] keeps
    // on mapping to RGB=0 (black to black)
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out.mat.m[i][j] *= mul[j];
            out.c[i] -= out.mat.m[i][j] * black[j];
        }
    }

    // Finally, multiply in the scaling factor required to get the color up to
    // the correct representation.
    pl_matrix3x3_scale(&out.mat, pl_color_repr_normalize(repr));

    // Update the metadata to reflect the change.
    repr->sys    = PL_COLOR_SYSTEM_RGB;
    repr->levels = PL_COLOR_LEVELS_PC;

    return out;
}

bool pl_icc_profile_equal(const struct pl_icc_profile *p1,
                          const struct pl_icc_profile *p2)
{
    // Test for presence of a pointer first
    if (!!p1->data != !!p2->data)
        return false;

    // Otherwise, test for equality of signature+len (if a profile is present)
    return !p1->data || (p1->signature == p2->signature && p1->len == p2->len);
}
