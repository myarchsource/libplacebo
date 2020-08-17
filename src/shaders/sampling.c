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
#include "shaders.h"

const struct pl_deband_params pl_deband_default_params = {
    .iterations = 1,
    .threshold  = 4.0,
    .radius     = 16.0,
    .grain      = 6.0,
};

// Helper function to compute the src/dst sizes and upscaling ratios
static bool setup_src(struct pl_shader *sh, const struct pl_sample_src *src,
                      ident_t *src_tex, ident_t *pos, ident_t *size, ident_t *pt,
                      float *ratio_x, float *ratio_y, int *components,
                      float *scale, bool resizeable, const char **fn)
{
    pl_assert(pl_tex_params_dimension(src->tex->params) == 2);
    float src_w = pl_rect_w(src->rect);
    float src_h = pl_rect_h(src->rect);
    src_w = PL_DEF(src_w, src->tex->params.w);
    src_h = PL_DEF(src_h, src->tex->params.h);

    int out_w = PL_DEF(src->new_w, fabs(src_w));
    int out_h = PL_DEF(src->new_h, fabs(src_h));

    if (ratio_x)
        *ratio_x = out_w / fabs(src_w);
    if (ratio_y)
        *ratio_y = out_h / fabs(src_h);
    if (scale)
        *scale = PL_DEF(src->scale, 1.0);

    if (components) {
        const struct pl_fmt *fmt = src->tex->params.format;
        *components = PL_DEF(src->components, fmt->num_components);
    }

    if (resizeable)
        out_w = out_h = 0;
    if (!sh_require(sh, PL_SHADER_SIG_NONE, out_w, out_h))
        return false;

    struct pl_rect2df rect = {
        .x0 = src->rect.x0,
        .y0 = src->rect.y0,
        .x1 = src->rect.x0 + src_w,
        .y1 = src->rect.y0 + src_h,
    };

    if (fn)
        *fn = sh_tex_fn(sh, src->tex);

    *src_tex = sh_bind(sh, src->tex, "src_tex", &rect, pos, size, pt);
    return true;
}

void pl_shader_deband(struct pl_shader *sh, const struct pl_sample_src *src,
                      const struct pl_deband_params *params)
{
    if (src->tex->params.sample_mode != PL_TEX_SAMPLE_LINEAR) {
        SH_FAIL(sh, "Debanding requires sample_mode = PL_TEX_SAMPLE_LINEAR!");
        return;
    }

    float scale;
    ident_t tex, pos, pt;
    const char *fn;
    if (!setup_src(sh, src, &tex, &pos, NULL, &pt, NULL, NULL, NULL, &scale, true, &fn))
        return;

    GLSL("vec4 color;\n");
    GLSL("// pl_shader_deband\n");
    GLSL("{\n");
    params = PL_DEF(params, &pl_deband_default_params);

    ident_t prng, state;
    prng = sh_prng(sh, true, &state);

    GLSL("vec2 pos = %s;       \n"
         "vec4 avg, diff;      \n"
         "color = %s(%s, pos); \n",
         pos, fn, tex);

    // Helper function: Compute a stochastic approximation of the avg color
    // around a pixel, given a specified radius
    ident_t average = sh_fresh(sh, "average");
    GLSLH("vec4 %s(vec2 pos, float range, inout float %s) {     \n"
          // Compute a random angle and distance
          "    float dist = %s * range;                         \n"
          "    float dir  = %s * %f;                            \n"
          "    vec2 o = dist * vec2(cos(dir), sin(dir));        \n"
          // Sample at quarter-turn intervals around the source pixel
          "    vec4 sum = vec4(0.0);                            \n"
          "    sum += %s(%s, pos + %s * vec2( o.x,  o.y)); \n"
          "    sum += %s(%s, pos + %s * vec2(-o.x,  o.y)); \n"
          "    sum += %s(%s, pos + %s * vec2(-o.x, -o.y)); \n"
          "    sum += %s(%s, pos + %s * vec2( o.x, -o.y)); \n"
          // Return the (normalized) average
          "    return 0.25 * sum;                               \n"
          "}\n",
          average, state, prng, prng, M_PI * 2,
          fn, tex, pt, fn, tex, pt, fn, tex, pt, fn, tex, pt);

    // For each iteration, compute the average at a given distance and
    // pick it instead of the color if the difference is below the threshold.
    for (int i = 1; i <= params->iterations; i++) {
        GLSL("avg = %s(pos, %f, %s);                                    \n"
             "diff = abs(color - avg);                                  \n"
             "color = mix(avg, color, %s(greaterThan(diff, vec4(%f)))); \n",
             average, i * params->radius, state,
             sh_bvec(sh, 4), params->threshold / (1000 * i * scale));
    }

    GLSL("color *= vec4(%f);\n", scale);

    // Add some random noise to smooth out residual differences
    if (params->grain > 0) {
        GLSL("vec3 noise = vec3(%s, %s, %s);         \n"
             "color.rgb += %f * (noise - vec3(0.5)); \n",
             prng, prng, prng, params->grain / 1000.0);
    }

    GLSL("}\n");
}

bool pl_shader_sample_direct(struct pl_shader *sh, const struct pl_sample_src *src)
{
    float scale;
    ident_t tex, pos;
    const char *fn;
    if (!setup_src(sh, src, &tex, &pos, NULL, NULL, NULL, NULL, NULL, &scale, true, &fn))
        return false;

    GLSL("// pl_shader_sample_direct          \n"
         "vec4 color = vec4(%f) * %s(%s, %s); \n",
         scale, fn, tex, pos);
    return true;
}

static void bicubic_calcweights(struct pl_shader *sh, const char *t, const char *s)
{
    // Explanation of how bicubic scaling with only 4 texel fetches is done:
    //   http://www.mate.tue.nl/mate/pdfs/10318.pdf
    //   'Efficient GPU-Based Texture Interpolation using Uniform B-Splines'
    GLSL("vec4 %s = vec4(-0.5, 0.1666, 0.3333, -0.3333) * %s \n"
         "          + vec4(1, 0, -0.5, 0.5);                 \n"
         "%s = %s * %s + vec4(0.0, 0.0, -0.5, 0.5);          \n"
         "%s = %s * %s + vec4(-0.6666, 0, 0.8333, 0.1666);   \n"
         "%s.xy /= %s.zw;                                    \n"
         "%s.xy += vec2(1.0 + %s, 1.0 - %s);                 \n",
         t, s,
         t, t, s,
         t, t, s,
         t, t,
         t, s, s);
}

bool pl_shader_sample_bicubic(struct pl_shader *sh, const struct pl_sample_src *src)
{
    if (src->tex->params.sample_mode != PL_TEX_SAMPLE_LINEAR) {
        SH_FAIL(sh, "Trying to use fast bicubic sampling from a texture without "
                "PL_TEX_SAMPLE_LINEAR");
        return false;
    }

    ident_t tex, pos, size, pt;
    float rx, ry, scale;
    const char *fn;
    if (!setup_src(sh, src, &tex, &pos, &size, &pt, &rx, &ry, NULL, &scale, true, &fn))
        return false;

    if (rx < 1 || ry < 1) {
        PL_TRACE(sh, "Using fast bicubic sampling when downscaling. This "
                 "will most likely result in nasty aliasing!");
    }

    GLSL("// pl_shader_sample_bicubic                   \n"
         "vec4 color = vec4(0.0);                       \n"
         "{                                             \n"
         "vec2 pos  = %s;                               \n"
         "vec2 pt   = %s;                               \n"
         "vec2 size = %s;                               \n"
         "vec2 fcoord = fract(pos * size + vec2(0.5));  \n",
         pos, pt, size);

    bicubic_calcweights(sh, "parmx", "fcoord.x");
    bicubic_calcweights(sh, "parmy", "fcoord.y");

    GLSL("vec4 cdelta;                              \n"
         "cdelta.xz = parmx.rg * vec2(-pt.x, pt.x); \n"
         "cdelta.yw = parmy.rg * vec2(-pt.y, pt.y); \n"
         // first y-interpolation
         "vec4 ar = %s(%s, pos + cdelta.xy);        \n"
         "vec4 ag = %s(%s, pos + cdelta.xw);        \n"
         "vec4 ab = mix(ag, ar, parmy.b);           \n"
         // second y-interpolation
         "vec4 br = %s(%s, pos + cdelta.zy);        \n"
         "vec4 bg = %s(%s, pos + cdelta.zw);        \n"
         "vec4 aa = mix(bg, br, parmy.b);           \n"
         // x-interpolation
         "color = vec4(%f) * mix(aa, ab, parmx.b);  \n"
         "}                                         \n",
         fn, tex, fn, tex, fn, tex, fn, tex, scale);
    return true;
}

static bool filter_compat(const struct pl_filter *filter, float inv_scale,
                          int lut_entries, float cutoff,
                          const struct pl_filter_config *params)
{
    if (!filter)
        return false;
    if (filter->params.lut_entries != lut_entries)
        return false;
    if (fabs(filter->params.filter_scale - inv_scale) > 1e-3)
        return false;
    if (filter->params.cutoff != cutoff)
        return false;

    return pl_filter_config_eq(&filter->params.config, params);
}

// Subroutine for computing and adding an individual texel contribution
// If `in` is NULL, samples directly
// If `in` is set, takes the pixel from inX[idx] where X is the component,
// `in` is the given identifier, and `idx` must be defined by the caller
static void polar_sample(struct pl_shader *sh, const struct pl_filter *filter,
                         const char *fn, ident_t tex, ident_t lut, int x, int y,
                         int comps, ident_t in)
{
    // Since we can't know the subpixel position in advance, assume a
    // worst case scenario
    int yy = y > 0 ? y-1 : y;
    int xx = x > 0 ? x-1 : x;
    float dmax = sqrt(xx*xx + yy*yy);
    // Skip samples definitely outside the radius
    if (dmax >= filter->radius_cutoff)
        return;

    GLSL("d = length(vec2(%d.0, %d.0) - fcoord);\n", x, y);
    // Check for samples that might be skippable
    bool maybe_skippable = dmax >= filter->radius_cutoff - M_SQRT2;
    if (maybe_skippable)
        GLSL("if (d < %f) {\n", filter->radius_cutoff);

    // Get the weight for this pixel
    GLSL("w = %s(d * 1.0/%f); \n"
         "wsum += w;          \n",
         lut, filter->radius);

    if (in) {
        for (int n = 0; n < comps; n++)
            GLSL("color[%d] += w * %s%d[idx];\n", n, in, n);
    } else {
        GLSL("in0 = %s(%s, base + pt * vec2(%d.0, %d.0)); \n"
             "color += vec4(w) * in0;                     \n",
             fn, tex, x, y);
    }

    if (maybe_skippable)
        GLSL("}\n");
}

struct sh_sampler_obj {
    const struct pl_filter *filter;
    struct pl_shader_obj *lut;
    struct pl_shader_obj *pass2; // for pl_shader_sample_ortho
};

static void sh_sampler_uninit(const struct pl_gpu *gpu, void *ptr)
{
    struct sh_sampler_obj *obj = ptr;
    pl_shader_obj_destroy(&obj->lut);
    pl_shader_obj_destroy(&obj->pass2);
    pl_filter_free(&obj->filter);
    *obj = (struct sh_sampler_obj) {0};
}

static void fill_polar_lut(void *priv, float *data, int w, int h, int d)
{
    const struct sh_sampler_obj *obj = priv;
    const struct pl_filter *filt = obj->filter;

    pl_assert(w == filt->params.lut_entries);
    memcpy(data, filt->weights, w * sizeof(float));
}

bool pl_shader_sample_polar(struct pl_shader *sh,
                            const struct pl_sample_src *src,
                            const struct pl_sample_filter_params *params)
{
    pl_assert(params);
    if (!params->filter.polar) {
        SH_FAIL(sh, "Trying to use polar sampling with a non-polar filter?");
        return false;
    }

    const struct pl_gpu *gpu = SH_GPU(sh);
    const struct pl_tex *tex = src->tex;
    pl_assert(gpu && tex);

    bool has_compute = gpu->caps & PL_GPU_CAP_COMPUTE && !params->no_compute;
    bool flipped = src->rect.x0 > src->rect.x1 || src->rect.y0 > src->rect.y1;
    if (flipped && has_compute) {
        PL_WARN(sh, "Trying to use a flipped src.rect with polar sampling! "
                "This prevents the use of compute shaders, which is a "
                "potentially massive performance hit. If you're really sure you "
                "want this, set params.no_compute to suppress this warning.");
        has_compute = false;
    }

    int comps;
    float rx, ry, scale;
    ident_t src_tex, pos, size, pt;
    const char *fn;
    if (!setup_src(sh, src, &src_tex, &pos, &size, &pt, &rx, &ry, &comps, &scale, false, &fn))
        return false;

    struct sh_sampler_obj *obj;
    obj = SH_OBJ(sh, params->lut, PL_SHADER_OBJ_SAMPLER, struct sh_sampler_obj,
                 sh_sampler_uninit);
    if (!obj)
        return false;

    float inv_scale = 1.0 / PL_MIN(rx, ry);
    inv_scale = PL_MAX(inv_scale, 1.0);

    if (params->no_widening)
        inv_scale = 1.0;

    int lut_entries = PL_DEF(params->lut_entries, 64);
    float cutoff = PL_DEF(params->cutoff, 0.001);
    bool update = !filter_compat(obj->filter, inv_scale, lut_entries, cutoff,
                                 &params->filter);

    if (update) {
        pl_filter_free(&obj->filter);
        obj->filter = pl_filter_generate(sh->ctx, &(struct pl_filter_params) {
            .config         = params->filter,
            .lut_entries    = lut_entries,
            .filter_scale   = inv_scale,
            .cutoff         = cutoff,
        });

        if (!obj->filter) {
            // This should never happen, but just in case ..
            SH_FAIL(sh, "Failed initializing polar filter!");
            return false;
        }
    }

    ident_t lut = sh_lut(sh, &obj->lut, SH_LUT_LINEAR, lut_entries, 0, 0, 1,
                         update, obj, fill_polar_lut);
    if (!lut) {
        SH_FAIL(sh, "Failed initializing polar LUT!");
        return false;
    }

    GLSL("// pl_shader_sample_polar                     \n"
         "vec4 color = vec4(0.0);                       \n"
         "{                                             \n"
         "vec2 pos = %s, size = %s, pt = %s;            \n"
         "vec2 fcoord = fract(pos * size - vec2(0.5));  \n"
         "vec2 base = pos - pt * fcoord;                \n"
         "float w, d, wsum = 0.0;                       \n"
         "int idx;                                      \n"
         "vec4 c;                                       \n",
         pos, size, pt);

    int bound   = ceil(obj->filter->radius_cutoff);
    int offset  = bound - 1; // padding top/left
    int padding = offset + bound; // total padding

    // For performance we want to load at least as many pixels horizontally as
    // there are threads in a warp, as well as enough to take advantage of
    // shmem parallelism. However, on the other hand, to hide latency we want
    // to avoid making the kernel too large. A good size overall is 256
    // threads, which allows at least 8 to run in parallel assuming good VGPR
    // distribution. A good trade-off for the horizontal row size is 32, which
    // is the warp size on nvidia. Going up to 64 (AMD's wavefront size)
    // is not worth it even on AMD hardware.
    const int bw = 32, bh = 256 / bw;

    // We need to sample everything from base_min to base_max, so make sure
    // we have enough room in shmem
    int iw = (int) ceil(bw / rx) + padding + 1,
        ih = (int) ceil(bh / ry) + padding + 1;

    ident_t in = NULL;
    int shmem_req = iw * ih * comps * sizeof(float);
    if (has_compute && sh_try_compute(sh, bw, bh, false, shmem_req)) {
        // Compute shader kernel
        GLSL("vec2 wpos = %s_map(gl_WorkGroupID * gl_WorkGroupSize);        \n"
             "vec2 wbase = wpos - pt * fract(wpos * size - vec2(0.5));      \n"
             "ivec2 rel = ivec2(round((base - wbase) * size));              \n",
             pos);

        // Load all relevant texels into shmem
        GLSL("for (int y = int(gl_LocalInvocationID.y); y < %d; y += %d) {  \n"
             "for (int x = int(gl_LocalInvocationID.x); x < %d; x += %d) {  \n"
             "c = %s(%s, wbase + pt * vec2(x - %d, y - %d));                \n",
             ih, bh, iw, bw, fn, src_tex, offset, offset);

        in = sh_fresh(sh, "in");
        for (int c = 0; c < comps; c++) {
            GLSLH("shared float %s%d[%d];   \n", in, c, ih * iw);
            GLSL("%s%d[%d * y + x] = c[%d]; \n", in, c, iw, c);
        }

        GLSL("}}                    \n"
             "groupMemoryBarrier(); \n"
             "barrier();            \n");

        // Dispatch the actual samples
        for (int y = 1 - bound; y <= bound; y++) {
            for (int x = 1 - bound; x <= bound; x++) {
                GLSL("idx = %d * rel.y + rel.x + %d;\n",
                     iw, iw * (y + offset) + x + offset);
                polar_sample(sh, obj->filter, fn, src_tex, lut, x, y, comps, in);
            }
        }
    } else {
        // Fragment shader sampling
        for (int n = 0; n < comps; n++)
            GLSL("vec4 in%d;\n", n);

        // Iterate over the LUT space in groups of 4 texels at a time, and
        // decide for each texel group whether to use gathering or direct
        // sampling.
        for (int y = 1 - bound; y <= bound; y += 2) {
            for (int x = 1 - bound; x <= bound; x += 2) {
                // Using texture gathering is only more efficient than direct
                // sampling in the case where we expect to be able to use all
                // four gathered texels, without having to discard any. So
                // only do it if we suspsect it will be a win rather than a
                // loss.
                bool use_gather = sqrt(x*x + y*y) < obj->filter->radius_cutoff;

                // Make sure all required features are supported
                use_gather &= sh_glsl(sh).version >= 400;
                use_gather &= gpu->limits.max_gather_offset != 0;
                use_gather &= PL_MAX(x, y) <= gpu->limits.max_gather_offset;
                use_gather &= PL_MIN(x, y) >= gpu->limits.min_gather_offset;

                if (!use_gather) {
                    // Switch to direct sampling instead
                    for (int yy = y; yy <= bound && yy <= y + 1; yy++) {
                        for (int xx = x; xx <= bound && xx <= x + 1; xx++) {
                            polar_sample(sh, obj->filter, fn, src_tex, lut,
                                         xx, yy, comps, NULL);
                        }
                    }
                    continue; // next group of 4
                }

                // Gather the four surrounding texels simultaneously
                for (int n = 0; n < comps; n++) {
                    GLSL("in%d = textureGatherOffset(%s, base, "
                         "ivec2(%d, %d), %d);\n", n, src_tex, x, y, n);
                }

                // Mix in all of the points with their weights
                for (int p = 0; p < 4; p++) {
                    // The four texels are gathered counterclockwise starting
                    // from the bottom left
                    static const int xo[4] = {0, 1, 1, 0};
                    static const int yo[4] = {1, 1, 0, 0};
                    if (x+xo[p] > bound || y+yo[p] > bound)
                        continue; // next subpixel

                    GLSL("idx = %d;\n", p);
                    polar_sample(sh, obj->filter, fn, src_tex, lut,
                                 x+xo[p], y+yo[p], comps, "in");
                }
            }
        }
    }

    GLSL("color = vec4(%f / wsum) * color; \n"
         "}                                \n",
         scale);
    return true;
}

static void fill_ortho_lut(void *priv, float *data, int w, int h, int d)
{
    const struct sh_sampler_obj *obj = priv;
    const struct pl_filter *filt = obj->filter;

    pl_assert(w * h * 4 == filt->params.lut_entries * filt->row_stride);
    memcpy(data, filt->weights, w * h * 4 * sizeof(float));
}

bool pl_shader_sample_ortho(struct pl_shader *sh, int pass,
                            const struct pl_sample_src *src,
                            const struct pl_sample_filter_params *params)
{
    pl_assert(params);
    if (params->filter.polar) {
        SH_FAIL(sh, "Trying to use separated sampling with a polar filter?");
        return false;
    }

    const struct pl_gpu *gpu = SH_GPU(sh);
    const struct pl_tex *tex = src->tex;
    pl_assert(gpu && tex);

    struct pl_sample_src srcfix = *src;
    switch (pass) {
    case PL_SEP_VERT:
        srcfix.rect.x0 = 0;
        srcfix.rect.x1 = srcfix.new_w = tex->params.w;
        break;
    case PL_SEP_HORIZ:
        srcfix.rect.y0 = 0;
        srcfix.rect.y1 = srcfix.new_h = tex->params.h;
        break;
    case PL_SEP_PASSES:
    default:
        abort();
    }

    int comps;
    float ratio[2], scale;
    ident_t src_tex, pos, size, pt;
    const char *fn;
    if (!setup_src(sh, &srcfix, &src_tex, &pos, &size, &pt, &ratio[1], &ratio[0],
                   &comps, &scale, false, &fn))
    {
        return false;
    }

    // We can store a separate sampler object per dimension, so dispatch the
    // right one. This is needed for two reasons:
    // 1. Anamorphic content can have a different scaling ratio for each
    //    dimension. In particular, you could be upscaling in one and
    //    downscaling in the other.
    // 2. After fixing the source for `setup_src`, we lose information about
    //    the scaling ratio of the other component. (Although this is only a
    //    minor reason and could easily be changed with some boilerplate)
    struct sh_sampler_obj *obj;
    obj = SH_OBJ(sh, params->lut, PL_SHADER_OBJ_SAMPLER,
                 struct sh_sampler_obj, sh_sampler_uninit);
    if (!obj)
        return false;

    if (pass != 0) {
        obj = SH_OBJ(sh, &obj->pass2, PL_SHADER_OBJ_SAMPLER,
                     struct sh_sampler_obj, sh_sampler_uninit);
        assert(obj);
    }

    float inv_scale = 1.0 / ratio[pass];
    inv_scale = PL_MAX(inv_scale, 1.0);

    if (params->no_widening)
        inv_scale = 1.0;

    int lut_entries = PL_DEF(params->lut_entries, 64);
    bool update = !filter_compat(obj->filter, inv_scale, lut_entries, 0.0,
                                 &params->filter);

    if (update) {
        pl_filter_free(&obj->filter);
        obj->filter = pl_filter_generate(sh->ctx, &(struct pl_filter_params) {
            .config             = params->filter,
            .lut_entries        = lut_entries,
            .filter_scale       = inv_scale,
            .max_row_size       = gpu->limits.max_tex_2d_dim / 4,
            .row_stride_align   = 4,
        });

        if (!obj->filter) {
            // This should never happen, but just in case ..
            SH_FAIL(sh, "Failed initializing separated filter!");
            return false;
        }
    }

    int N = obj->filter->row_size; // number of samples to convolve
    int width = obj->filter->row_stride / 4; // width of the LUT texture
    ident_t lut = sh_lut(sh, &obj->lut, SH_LUT_LINEAR, width, lut_entries, 0, 4,
                         update, obj, fill_ortho_lut);
    if (!lut) {
        SH_FAIL(sh, "Failed initializing separated LUT!");
        return false;
    }

    const float dir[PL_SEP_PASSES][2] = {
        [PL_SEP_HORIZ] = {1.0, 0.0},
        [PL_SEP_VERT]  = {0.0, 1.0},
    };

    GLSL("// pl_shader_sample_ortho                        \n"
         "vec4 color = vec4(0.0);                          \n"
         "{                                                \n"
         "vec2 pos = %s, size = %s, pt = %s;               \n"
         "vec2 dir = vec2(%f, %f);                         \n"
         "pt *= dir;                                       \n"
         "vec2 fcoord2 = fract(pos * size - vec2(0.5));    \n"
         "float fcoord = dot(fcoord2, dir);                \n"
         "vec2 base = pos - fcoord * pt - pt * vec2(%d.0); \n"
         "float weight;                                    \n"
         "vec4 ws, c;                                      \n",
         pos, size, pt,
         dir[pass][0], dir[pass][1],
         N / 2 - 1);

    bool use_ar = params->antiring > 0;
    if (use_ar) {
        GLSL("vec4 hi = vec4(0.0); \n"
             "vec4 lo = vec4(1e9); \n");
    }

    // Dispatch all of the samples
    GLSL("// scaler samples\n");
    for (int n = 0; n < N; n++) {
        // Load the right weight for this instance. For every 4th weight, we
        // need to fetch another LUT entry. Otherwise, just use the previous
        if (n % 4 == 0) {
            float denom = PL_MAX(1, width - 1); // avoid division by zero
            GLSL("ws = %s(vec2(%f, fcoord));\n", lut, (n / 4) / denom);
        }
        GLSL("weight = ws[%d];\n", n % 4);

        // Load the input texel and add it to the running sum
        GLSL("c = %s(%s, base + pt * vec2(%d.0)); \n"
             "color += vec4(weight) * c;          \n",
             fn, src_tex, n);

        if (use_ar && (n == N / 2 - 1 || n == N / 2)) {
            GLSL("lo = min(lo, c); \n"
                 "hi = max(hi, c); \n");
        }
    }

    if (use_ar) {
        GLSL("color = mix(color, clamp(color, lo, hi), %f);\n",
             params->antiring);
    }

    GLSL("color *= vec4(%f);\n", scale);
    GLSL("}\n");
    return true;
}
