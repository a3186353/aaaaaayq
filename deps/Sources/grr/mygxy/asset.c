/*
 * asset.c - native helpers for CDN asset composition.
 *
 * The Lua side keeps business rules and cache keys. This module only handles
 * hot pixel work: BMP palette extraction and 256-color palette composition.
 */
#include "lua_proxy.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define MYGXY_API __declspec(dllexport)
#else
#define MYGXY_API LUAMOD_API
#endif

typedef struct AssetColor
{
    double r;
    double g;
    double b;
    double a;
} AssetColor;

typedef struct AssetSegment
{
    int minX;
    int maxX;
    int flag;
    double property[16]; /* 1..15 */
} AssetSegment;

static double asset_clamp(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static unsigned char asset_byte(double value)
{
    int v = (int)floor(value);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (unsigned char)v;
}

static int asset_read_u32le(const unsigned char* p, size_t len, size_t off, unsigned int* out)
{
    if (!p || off + 4 > len || !out)
        return 0;
    *out = (unsigned int)p[off]
        | ((unsigned int)p[off + 1] << 8)
        | ((unsigned int)p[off + 2] << 16)
        | ((unsigned int)p[off + 3] << 24);
    return 1;
}

static const unsigned char* asset_palette_pixels_from_bmp(
    const unsigned char* data, size_t len, size_t* out_len)
{
    unsigned int pixel_offset = 0;
    if (!data || len < 54 || !out_len)
        return NULL;
    if (!asset_read_u32le(data, len, 10, &pixel_offset))
        return NULL;
    if (pixel_offset == 0 || (size_t)pixel_offset >= len)
        return NULL;
    len -= (size_t)pixel_offset;
    data += pixel_offset;
    if (len >= 1024)
    {
        *out_len = 1024;
        return data;
    }
    if (len >= 768)
    {
        *out_len = 768;
        return data;
    }
    return NULL;
}

static const unsigned char* asset_palette_pixels_any(
    const unsigned char* data, size_t len, size_t* out_len)
{
    const unsigned char* pixels = asset_palette_pixels_from_bmp(data, len, out_len);
    if (pixels)
        return pixels;
    if (!out_len)
        return NULL;
    if (len == 1024 || len == 768)
    {
        *out_len = len;
        return data;
    }
    return NULL;
}

static int asset_decode_colors(
    const unsigned char* data, size_t len, AssetColor colors[256])
{
    size_t pixels_len = 0;
    const unsigned char* pixels = asset_palette_pixels_any(data, len, &pixels_len);
    if (!pixels)
        return 0;

    if (pixels_len >= 1024)
    {
        int i;
        for (i = 0; i < 256; i++)
        {
            size_t off = (size_t)i * 4;
            colors[i].b = pixels[off + 0];
            colors[i].g = pixels[off + 1];
            colors[i].r = pixels[off + 2];
            colors[i].a = pixels[off + 3];
        }
    }
    else
    {
        int i;
        for (i = 0; i < 256; i++)
        {
            size_t off = (size_t)i * 3;
            colors[i].b = pixels[off + 0];
            colors[i].g = pixels[off + 1];
            colors[i].r = pixels[off + 2];
            colors[i].a = 255;
        }
    }
    return 1;
}

static void asset_rgb_to_hsl(const AssetColor* rgb, double* out_h, double* out_s, double* out_l)
{
    double r = rgb->r;
    double g = rgb->g;
    double b = rgb->b;
    double max_value = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double min_value = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double h = 0.0;
    double s = 0.0;
    double l = (max_value + min_value) / 2.0;

    if (max_value != min_value)
    {
        double delta = max_value - min_value;
        if (r == max_value)
            h = (g - b) / delta;
        else if (g == max_value)
            h = 2.0 + (b - r) / delta;
        else
            h = 4.0 + (r - g) / delta;

        h /= 6.0;
        if (h < 0.0)
            h += 1.0;

        if (l < 0.5)
            s = delta / (max_value + min_value);
        else
            s = delta / (2.0 - max_value - min_value);
    }

    *out_h = h;
    *out_s = s;
    *out_l = l;
}

static double asset_hue_to_rgb(double p, double q, double t)
{
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;

    if (t < 1.0 / 6.0)
        return p + (q - p) * t * 6.0;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3.0)
        return p + (q - p) * (4.0 - 6.0 * t);
    return p;
}

static AssetColor asset_hsl_to_rgb(double h, double s, double l)
{
    AssetColor out;
    out.a = 255.0;
    if (s == 0.0)
    {
        out.r = l;
        out.g = l;
        out.b = l;
        return out;
    }

    {
        double q = l <= 0.5 ? l * (1.0 + s) : l + s - l * s;
        double p = 2.0 * l - q;
        out.r = asset_hue_to_rgb(p, q, h + 1.0 / 3.0);
        out.g = asset_hue_to_rgb(p, q, h);
        out.b = asset_hue_to_rgb(p, q, h - 1.0 / 3.0);
    }
    return out;
}

static double asset_adjust_value(double value, double amount, int mode)
{
    if (mode == 1)
        value *= amount;
    else if (mode == 2)
        value += amount;
    else if (mode == 3)
        value = amount;
    return value;
}

static AssetColor asset_adjust_gamma(AssetColor color, double gamma)
{
    color.r = pow(color.r, gamma);
    color.g = pow(color.g, gamma);
    color.b = pow(color.b, gamma);
    return color;
}

static double asset_luminance(AssetColor color)
{
    return 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
}

static int asset_shift_right(long long value, int bits)
{
    return (int)(value >> bits);
}

static AssetColor asset_process_color(
    int flags, const double params[16], AssetColor color, int extra_param)
{
    AssetColor modified = color;
    int mode = 0;

    if ((flags & 2) > 0)
    {
        double hue_shift = params[10] / 1000.0;
        double saturation_shift = (params[11] - 500.0) / 500.0;
        double lightness_shift = (params[12] - 500.0) / 500.0;
        double h, s, l;
        AssetColor c = modified;

        c.r /= 255.0;
        c.g /= 255.0;
        c.b /= 255.0;
        asset_rgb_to_hsl(&c, &h, &s, &l);

        h += hue_shift;
        s += saturation_shift;
        l += lightness_shift;

        c = asset_hsl_to_rgb(h - floor(h), asset_clamp(s, 0.0, 1.0), asset_clamp(l, 0.0, 1.0));
        modified.r = floor(asset_clamp(c.r, 0.0, 1.0) * 255.0);
        modified.g = floor(asset_clamp(c.g, 0.0, 1.0) * 255.0);
        modified.b = floor(asset_clamp(c.b, 0.0, 1.0) * 255.0);
        modified.a = color.a;
        mode = 1;
    }

    if ((flags & 1) > 0)
    {
        int r0 = (int)modified.r;
        int g0 = (int)modified.g;
        int b0 = (int)modified.b;
        int r, g, b;

        if (mode != 0)
        {
            r = asset_shift_right(
                (long long)r0 * (int)params[1]
                + (((long long)g0 * (int)params[2]) << 1)
                + (long long)b0 * (int)params[3],
                8);
            if (r > 255) r = 255;

            g = asset_shift_right(
                asset_shift_right((long long)r * (int)params[4], 1)
                + (long long)g0 * (int)params[5]
                + asset_shift_right((long long)b0 * (int)params[6], 1),
                8);
            if (g > 255) g = 255;

            b = asset_shift_right(
                (long long)r * (int)params[7]
                + (((long long)g * (int)params[8]) << 1)
                + (long long)b0 * (int)params[9],
                8);
            if (b > 255) b = 255;
        }
        else
        {
            r = asset_shift_right(
                (long long)r0 * (int)params[1]
                + (((long long)g0 * (int)params[2]) << 1)
                + (long long)b0 * (int)params[3],
                8);
            g = asset_shift_right(
                asset_shift_right((long long)r0 * (int)params[4], 1)
                + (long long)g0 * (int)params[5]
                + asset_shift_right((long long)b0 * (int)params[6], 1),
                8);
            b = asset_shift_right(
                (long long)r0 * (int)params[7]
                + (((long long)g0 * (int)params[8]) << 1)
                + (long long)b0 * (int)params[9],
                8);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
        }

        modified.r = r;
        modified.g = g;
        modified.b = b;
        modified.a = color.a;
    }

    if ((flags & 4) > 0)
    {
        double r = modified.r;
        double g = modified.g;
        double b = modified.b;
        int p13 = (int)params[13];

        if (p13 < 128)
        {
            int adjustment = 128 - p13;
            r = r - asset_shift_right((long long)((int)r) * adjustment, 7) + adjustment;
            g = g - asset_shift_right((long long)((int)g) * adjustment, 7) + adjustment;
            b = b - asset_shift_right((long long)((int)b) * adjustment, 7) + adjustment;
        }
        else
        {
            int adjustment = p13 - 128;
            int denom = 256 - (adjustment << 1);
            r = r < adjustment ? 0.0 : (r > 255 - adjustment ? 255.0 :
                (((int)r - adjustment) << 8) / (double)denom);
            g = g < adjustment ? 0.0 : (g > 255 - adjustment ? 255.0 :
                (((int)g - adjustment) << 8) / (double)denom);
            b = b < adjustment ? 0.0 : (b > 255 - adjustment ? 255.0 :
                (((int)b - adjustment) << 8) / (double)denom);
            if (r > 255.0) r = 255.0;
            if (g > 255.0) g = 255.0;
            if (b > 255.0) b = 255.0;
        }

        modified.r = r;
        modified.g = g;
        modified.b = b;
        modified.a = color.a;
    }

    if ((flags & 8) > 0 && (params[14] != 0.0 || params[15] != 500.0))
    {
        double h_adjust = params[14] / 1000.0;
        int s_flag = (flags & 16) > 0 ? 1 : 2;
        int h_flag = extra_param;
        double s_adjust = (flags & 16) > 0
            ? params[15] / 1000.0 + 0.5
            : (params[15] - 500.0) / 500.0;
        AssetColor c = modified;
        double h, s, l;
        double original_luminance;
        double mid_gray_luminance;
        double adjusted_l;
        AssetColor mid;

        if (params[14] == 0.0)
            h_flag = 0;

        c.r /= 255.0;
        c.g /= 255.0;
        c.b /= 255.0;

        c = asset_adjust_gamma(c, 2.2);
        asset_rgb_to_hsl(&c, &h, &s, &l);

        s = asset_adjust_value(s, s_adjust, s_flag);
        h = asset_adjust_value(h, h_adjust, h_flag);

        original_luminance = asset_luminance(c);
        mid = asset_hsl_to_rgb(h, s, 0.5);
        mid_gray_luminance = asset_luminance(mid);
        if (original_luminance < mid_gray_luminance)
            adjusted_l = mid_gray_luminance == 0.0 ? 0.0 : original_luminance / mid_gray_luminance * 0.5;
        else
            adjusted_l = mid_gray_luminance == 1.0 ? 1.0 :
                1.0 - (1.0 - original_luminance) / (1.0 - mid_gray_luminance) * 0.5;
        adjusted_l = asset_clamp(adjusted_l, 0.0, 1.0);

        c = asset_hsl_to_rgb(h - floor(h), asset_clamp(s, 0.0, 1.0), asset_clamp(adjusted_l, 0.0, 1.0));
        c = asset_adjust_gamma(c, 1.0 / 2.2);

        modified.r = c.r * 255.0;
        modified.g = c.g * 255.0;
        modified.b = c.b * 255.0;
        modified.a = color.a;
    }

    modified.a = color.a;
    return modified;
}

static int asset_get_number_field(lua_State* L, int table_idx, const char* key, double* out)
{
    int ok = 0;
    table_idx = lua_absindex(L, table_idx);
    lua_getfield(L, table_idx, key);
    if (lua_isnumber(L, -1))
    {
        *out = lua_tonumber(L, -1);
        ok = 1;
    }
    lua_pop(L, 1);
    return ok;
}

static int asset_read_segment_at(lua_State* L, int table_idx, AssetSegment* out)
{
    double minX = 0.0;
    double maxX = 0.0;
    double flag = 0.0;
    int i;

    if (!lua_istable(L, table_idx) || !out)
        return 0;
    table_idx = lua_absindex(L, table_idx);

    if (!asset_get_number_field(L, table_idx, "minX", &minX))
        minX = 0.0;
    if (!asset_get_number_field(L, table_idx, "maxX", &maxX))
        maxX = 0.0;
    if (!asset_get_number_field(L, table_idx, "flag", &flag))
        return 0;

    lua_getfield(L, table_idx, "property");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }

    out->minX = (int)minX;
    out->maxX = (int)maxX;
    out->flag = (int)flag;
    memset(out->property, 0, sizeof(out->property));
    for (i = 1; i <= 15; i++)
    {
        lua_geti(L, -1, i);
        out->property[i] = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    return out->maxX > out->minX;
}

static int asset_read_segments(lua_State* L, int idx, AssetSegment** out_segments, int* out_count)
{
    lua_Unsigned len;
    AssetSegment* segments;
    int count = 0;
    lua_Unsigned i;

    *out_segments = NULL;
    *out_count = 0;
    if (!lua_istable(L, idx))
        return 1;

    idx = lua_absindex(L, idx);
    len = lua_rawlen(L, idx);
    if (len == 0)
        return 1;

    segments = (AssetSegment*)malloc((size_t)len * sizeof(AssetSegment));
    if (!segments)
        return 0;

    for (i = 1; i <= len; i++)
    {
        lua_geti(L, idx, (lua_Integer)i);
        if (lua_istable(L, -1) && asset_read_segment_at(L, -1, &segments[count]))
            count++;
        lua_pop(L, 1);
    }

    *out_segments = segments;
    *out_count = count;
    return 1;
}

static void asset_emit_palette(lua_State* L, AssetColor colors[256], const AssetSegment* segments, int segment_count)
{
    unsigned char out[1024];
    int i;
    for (i = 0; i < 256; i++)
    {
        AssetColor c = colors[i];
        int s;
        for (s = 0; s < segment_count; s++)
        {
            const AssetSegment* seg = &segments[s];
            if (i >= seg->minX && i < seg->maxX)
                c = asset_process_color(seg->flag, seg->property, c, 2);
        }
        out[i * 4 + 0] = asset_byte(c.b);
        out[i * 4 + 1] = asset_byte(c.g);
        out[i * 4 + 2] = asset_byte(c.r);
        out[i * 4 + 3] = asset_byte(c.a);
    }
    lua_pushlstring(L, (const char*)out, sizeof(out));
}

static int asset_lua_read_palette(lua_State* L)
{
    size_t len = 0;
    size_t pixels_len = 0;
    const unsigned char* data = (const unsigned char*)luaL_checklstring(L, 1, &len);
    const unsigned char* pixels = asset_palette_pixels_from_bmp(data, len, &pixels_len);
    if (!pixels)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char*)pixels, pixels_len);
    return 1;
}

static int asset_lua_compose_palette(lua_State* L)
{
    size_t len = 0;
    const unsigned char* data = (const unsigned char*)luaL_checklstring(L, 1, &len);
    AssetColor colors[256];
    AssetSegment* segments = NULL;
    int segment_count = 0;

    if (!asset_decode_colors(data, len, colors))
    {
        lua_pushnil(L);
        return 1;
    }

    if (!asset_read_segments(L, 2, &segments, &segment_count))
    {
        lua_pushnil(L);
        return 1;
    }
    asset_emit_palette(L, colors, segments, segment_count);
    free(segments);
    return 1;
}

static int asset_variant_nth_segment(lua_State* L, int variant_idx, int rank, AssetSegment* out)
{
    lua_Unsigned len;
    int* used;
    int selected = -1;
    int pass;

    if (!lua_istable(L, variant_idx) || rank <= 0)
        return 0;
    variant_idx = lua_absindex(L, variant_idx);
    len = lua_rawlen(L, variant_idx);
    if ((lua_Unsigned)rank > len || len == 0)
        return 0;

    used = (int*)calloc((size_t)len, sizeof(int));
    if (!used)
        return 0;

    for (pass = 1; pass <= rank; pass++)
    {
        double best_min = 0.0;
        int has_best = 0;
        lua_Unsigned i;
        selected = -1;
        for (i = 1; i <= len; i++)
        {
            double minX = 0.0;
            if (used[i - 1])
                continue;
            lua_geti(L, variant_idx, (lua_Integer)i);
            if (lua_istable(L, -1))
                asset_get_number_field(L, -1, "minX", &minX);
            lua_pop(L, 1);
            if (!has_best || minX < best_min)
            {
                best_min = minX;
                selected = (int)i;
                has_best = 1;
            }
        }
        if (selected < 1)
        {
            free(used);
            return 0;
        }
        used[selected - 1] = 1;
    }

    lua_geti(L, variant_idx, (lua_Integer)selected);
    selected = lua_istable(L, -1) && asset_read_segment_at(L, -1, out);
    lua_pop(L, 1);
    free(used);
    return selected ? 1 : 0;
}

static int asset_read_xiangrui_segments(lua_State* L, int palettes_idx, int ride_desc_idx,
    AssetSegment** out_segments, int* out_count)
{
    lua_Unsigned ride_len;
    AssetSegment* segments;
    int count = 0;
    lua_Unsigned s;
    int has_ride_desc;

    *out_segments = NULL;
    *out_count = 0;
    if (!lua_istable(L, palettes_idx))
        return 1;

    palettes_idx = lua_absindex(L, palettes_idx);
    ride_desc_idx = lua_absindex(L, ride_desc_idx);
    has_ride_desc = lua_istable(L, ride_desc_idx);
    ride_len = has_ride_desc ? lua_rawlen(L, ride_desc_idx) : 0;
    if (ride_len == 0)
        ride_len = 3;

    segments = (AssetSegment*)malloc((size_t)ride_len * sizeof(AssetSegment));
    if (!segments)
        return 0;

    for (s = 1; s <= ride_len; s++)
    {
        int ride_value = 0;
        if (has_ride_desc)
        {
            lua_geti(L, ride_desc_idx, (lua_Integer)s);
            if (lua_isnumber(L, -1))
                ride_value = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }

        lua_geti(L, palettes_idx, (lua_Integer)ride_value + 1);
        if (lua_istable(L, -1) && asset_variant_nth_segment(L, -1, (int)s, &segments[count]))
            count++;
        lua_pop(L, 1);
    }

    *out_segments = segments;
    *out_count = count;
    return 1;
}

static int asset_lua_compose_xiangrui_palette(lua_State* L)
{
    size_t len = 0;
    const unsigned char* data = (const unsigned char*)luaL_checklstring(L, 1, &len);
    AssetColor colors[256];
    AssetSegment* segments = NULL;
    int segment_count = 0;

    if (!asset_decode_colors(data, len, colors))
    {
        lua_pushnil(L);
        return 1;
    }

    if (!asset_read_xiangrui_segments(L, 2, 3, &segments, &segment_count))
    {
        lua_pushnil(L);
        return 1;
    }
    asset_emit_palette(L, colors, segments, segment_count);
    free(segments);
    return 1;
}

static const luaL_Reg ASSET_FUNCS[] = {
    {"read_palette", asset_lua_read_palette},
    {"compose_palette", asset_lua_compose_palette},
    {"compose_xiangrui_palette", asset_lua_compose_xiangrui_palette},
    {NULL, NULL},
};

MYGXY_API int luaopen_mygxy_asset(lua_State* L)
{
    lua_createtable(L, 0, 3);
    luaL_setfuncs(L, ASSET_FUNCS, 0);
    return 1;
}
