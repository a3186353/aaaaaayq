/*
 * asset.c - native helpers for CDN asset composition.
 *
 * The Lua side keeps business rules and cache keys. This module only handles
 * hot pixel work: BMP palette extraction and 256-color palette composition.
 */
#include "lua_proxy.h"

#include <ctype.h>
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

typedef enum AssetColorKind
{
    ASSET_COLOR_EMPTY = 0,
    ASSET_COLOR_TABLE,
    ASSET_COLOR_PIPE,
    ASSET_COLOR_PLAIN,
    ASSET_COLOR_PACKED
} AssetColorKind;

typedef struct AssetColorChoice
{
    int values[64];
    int count;
    AssetColorKind kind;
} AssetColorChoice;

static double asset_get_number_field_or(lua_State* L, int table_idx, const char* key, double fallback);

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

static int asset_get_number_field_any(lua_State* L, int table_idx, const char* key1, const char* key2, double* out)
{
    if (asset_get_number_field(L, table_idx, key1, out))
        return 1;
    if (key2 && asset_get_number_field(L, table_idx, key2, out))
        return 1;
    return 0;
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

    if (!asset_get_number_field_any(L, table_idx, "minX", "min", &minX))
        minX = 0.0;
    if (!asset_get_number_field_any(L, table_idx, "maxX", "max", &maxX))
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

static int asset_segment_compare_minX(const void* a, const void* b)
{
    const AssetSegment* sa = (const AssetSegment*)a;
    const AssetSegment* sb = (const AssetSegment*)b;
    return (sa->minX > sb->minX) - (sa->minX < sb->minX);
}

static AssetSegment* asset_clone_segments(const AssetSegment* src, int count)
{
    AssetSegment* out;
    if (!src || count <= 0)
        return NULL;
    out = (AssetSegment*)malloc((size_t)count * sizeof(AssetSegment));
    if (!out)
        return NULL;
    memcpy(out, src, (size_t)count * sizeof(AssetSegment));
    return out;
}

static int asset_read_palette_variant_segments(
    lua_State* L, int palettes_idx, int variant_one_based, int sort_by_minX,
    AssetSegment** out_segments, int* out_count)
{
    int ok;
    *out_segments = NULL;
    *out_count = 0;
    if (!lua_istable(L, palettes_idx) || variant_one_based < 1)
        return 0;

    palettes_idx = lua_absindex(L, palettes_idx);
    lua_geti(L, palettes_idx, (lua_Integer)variant_one_based);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }

    ok = asset_read_segments(L, -1, out_segments, out_count);
    lua_pop(L, 1);
    if (!ok)
        return 0;
    if (sort_by_minX && *out_segments && *out_count > 1)
        qsort(*out_segments, (size_t)*out_count, sizeof(AssetSegment), asset_segment_compare_minX);
    return 1;
}

static int asset_has_pipe(const char* s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        if (s[i] == '|')
            return 1;
    }
    return 0;
}

static int asset_starts_hex(const char* s, size_t len)
{
    return len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

static int asset_hex_digit_value(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static long long asset_parse_integer_text(const char* s, size_t len, int* ok)
{
    char* buf;
    char* cur;
    char* end = NULL;
    long long value = 0;
    *ok = 0;
    if (!s || len == 0)
        return 0;
    buf = (char*)malloc(len + 1);
    if (!buf)
        return 0;
    memcpy(buf, s, len);
    buf[len] = '\0';
    cur = buf;
    while (*cur && isspace((unsigned char)*cur))
        cur++;
    if (asset_starts_hex(cur, strlen(cur)))
    {
        const char* p = cur + 2;
        int has_digit = 0;
        while (*p)
        {
            int digit = asset_hex_digit_value((unsigned char)*p);
            if (digit < 0)
                break;
            value = value * 16 + digit;
            has_digit = 1;
            p++;
        }
        if (has_digit)
            *ok = 1;
        free(buf);
        return value;
    }
    else
    {
        value = (long long)floor(strtod(cur, &end));
        if (end && end != cur)
            *ok = 1;
    }
    free(buf);
    return value;
}

static void asset_color_choice_add(AssetColorChoice* choice, double value)
{
    if (choice->count >= (int)(sizeof(choice->values) / sizeof(choice->values[0])))
        return;
    choice->values[choice->count++] = (int)floor(value);
}

static void asset_parse_pipe_values(const char* s, size_t len, AssetColorChoice* out)
{
    size_t start = 0;
    size_t i;
    char* buf = (char*)malloc(len + 1);
    if (!buf)
        return;
    memcpy(buf, s, len);
    buf[len] = '\0';
    for (i = 0; i <= len; i++)
    {
        if (buf[i] == '|' || buf[i] == '\0')
        {
            char saved = buf[i];
            char* end = NULL;
            double value;
            buf[i] = '\0';
            value = strtod(buf + start, &end);
            asset_color_choice_add(out, end && end != buf + start ? value : 0.0);
            buf[i] = saved;
            start = i + 1;
        }
    }
    free(buf);
}

static void asset_parse_color_choice(lua_State* L, int idx, AssetColorChoice* out)
{
    int type;
    memset(out, 0, sizeof(*out));
    out->kind = ASSET_COLOR_EMPTY;
    idx = lua_absindex(L, idx);
    type = lua_type(L, idx);

    if (type == LUA_TTABLE)
    {
        lua_Unsigned len = lua_rawlen(L, idx);
        lua_Unsigned i;
        out->kind = ASSET_COLOR_TABLE;
        for (i = 1; i <= len; i++)
        {
            lua_geti(L, idx, (lua_Integer)i);
            asset_color_choice_add(out, lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0);
            lua_pop(L, 1);
        }
        return;
    }

    if (type == LUA_TNIL || type == LUA_TNONE)
        return;

    if (type == LUA_TSTRING)
    {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        int ok = 0;
        long long n;
        if (!s || len == 0)
            return;
        if (asset_has_pipe(s, len))
        {
            out->kind = ASSET_COLOR_PIPE;
            asset_parse_pipe_values(s, len, out);
            return;
        }
        if (!asset_starts_hex(s, len) && len <= 3)
        {
            n = asset_parse_integer_text(s, len, &ok);
            out->kind = ASSET_COLOR_PLAIN;
            asset_color_choice_add(out, ok ? (double)n : 0.0);
            return;
        }
        n = asset_parse_integer_text(s, len, &ok);
        if (!ok || n <= 0)
        {
            out->kind = ASSET_COLOR_PACKED;
            return;
        }
        out->kind = ASSET_COLOR_PACKED;
        asset_color_choice_add(out, (double)((n >> 24) & 0xFF));
        asset_color_choice_add(out, (double)((n >> 16) & 0xFF));
        asset_color_choice_add(out, (double)((n >> 8) & 0xFF));
        asset_color_choice_add(out, (double)(n & 0xFF));
        return;
    }

    if (type == LUA_TNUMBER)
    {
        double raw = lua_tonumber(L, idx);
        long long n;
        if (raw >= 0.0 && raw <= 999.0)
        {
            out->kind = ASSET_COLOR_PLAIN;
            asset_color_choice_add(out, raw);
            return;
        }
        n = (long long)floor(raw);
        out->kind = ASSET_COLOR_PACKED;
        if (n <= 0)
            return;
        asset_color_choice_add(out, (double)((n >> 24) & 0xFF));
        asset_color_choice_add(out, (double)((n >> 16) & 0xFF));
        asset_color_choice_add(out, (double)((n >> 8) & 0xFF));
        asset_color_choice_add(out, (double)(n & 0xFF));
    }
}

static int asset_color_is_default(const AssetColorChoice* choice)
{
    int i;
    if (!choice || choice->kind == ASSET_COLOR_EMPTY)
        return 1;
    if (choice->kind == ASSET_COLOR_PIPE || choice->kind == ASSET_COLOR_TABLE || choice->kind == ASSET_COLOR_PLAIN)
    {
        for (i = 0; i < choice->count; i++)
        {
            if (choice->values[i] != 0)
                return 0;
        }
        return 1;
    }
    for (i = 0; i < choice->count; i++)
    {
        if (choice->values[i] > 1)
            return 0;
    }
    return 1;
}

static int asset_hs_has_changed(lua_State* L, int hs_idx)
{
    lua_Unsigned len;
    lua_Unsigned i;
    if (!lua_istable(L, hs_idx))
        return 0;
    hs_idx = lua_absindex(L, hs_idx);
    len = lua_rawlen(L, hs_idx);
    for (i = 1; i <= len; i++)
    {
        double h = 0.0;
        double s = 1.0;
        lua_geti(L, hs_idx, (lua_Integer)i);
        if (lua_istable(L, -1))
        {
            h = asset_get_number_field_or(L, -1, "h", 0.0);
            s = asset_get_number_field_or(L, -1, "s", 1.0);
        }
        lua_pop(L, 1);
        if (h != 0.0 || s != 1.0)
            return 1;
    }
    return 0;
}

static void asset_apply_hs_to_segments(lua_State* L, AssetSegment* segments, int count, int hs_idx)
{
    int i;
    if (!segments || count <= 0 || !lua_istable(L, hs_idx))
        return;
    hs_idx = lua_absindex(L, hs_idx);
    for (i = 0; i < count; i++)
    {
        double h = 0.0;
        double s = 1.0;
        lua_geti(L, hs_idx, (lua_Integer)i + 1);
        if (lua_istable(L, -1))
        {
            double minX = 0.0;
            double maxX = 0.0;
            if (asset_get_number_field_any(L, -1, "min", "minX", &minX))
                segments[i].minX = (int)minX;
            if (asset_get_number_field_any(L, -1, "max", "maxX", &maxX))
                segments[i].maxX = (int)maxX;
            h = asset_get_number_field_or(L, -1, "h", 0.0);
            s = asset_get_number_field_or(L, -1, "s", 1.0);
            if (h != 0.0 || s != 1.0)
            {
                segments[i].flag |= 8 | 16;
                segments[i].property[14] = asset_clamp(h, 0.0, 1000.0);
                segments[i].property[15] = asset_clamp(500.0 + (s - 1.0) * 125.0, 0.0, 1000.0);
            }
        }
        lua_pop(L, 1);
    }
}

static int asset_get_pp_range(lua_State* L, int pp_idx, int i, int* out_min, int* out_max)
{
    double minX = 0.0;
    double maxX = 0.0;
    int has_max = 0;
    if (!lua_istable(L, pp_idx) || !out_min || !out_max)
        return 0;
    pp_idx = lua_absindex(L, pp_idx);

    lua_geti(L, pp_idx, i);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }
    minX = asset_get_number_field_or(L, -1, "a", 0.0);
    has_max = asset_get_number_field(L, -1, "b", &maxX);
    lua_pop(L, 1);

    if (!has_max || maxX <= minX)
    {
        lua_geti(L, pp_idx, i + 1);
        if (lua_istable(L, -1))
            maxX = asset_get_number_field_or(L, -1, "a", 256.0);
        else
            maxX = 256.0;
        lua_pop(L, 1);
    }

    minX = asset_clamp(floor(minX), 0.0, 255.0);
    maxX = asset_clamp(floor(maxX), minX + 1.0, 256.0);
    *out_min = (int)minX;
    *out_max = (int)maxX;
    return 1;
}

static int asset_build_pp_range_default(lua_State* L, int pp_idx, AssetSegment** out_segments, int* out_count)
{
    lua_Unsigned len;
    lua_Unsigned i;
    AssetSegment* out;
    int count = 0;
    *out_segments = NULL;
    *out_count = 0;
    if (!lua_istable(L, pp_idx))
        return 0;
    pp_idx = lua_absindex(L, pp_idx);
    len = lua_rawlen(L, pp_idx);
    if (len == 0)
        return 0;
    out = (AssetSegment*)calloc((size_t)len, sizeof(AssetSegment));
    if (!out)
        return 0;
    for (i = 1; i <= len; i++)
    {
        int minX = 0;
        int maxX = 0;
        if (asset_get_pp_range(L, pp_idx, (int)i, &minX, &maxX))
        {
            out[count].minX = minX;
            out[count].maxX = maxX;
            out[count].flag = 0;
            count++;
        }
    }
    if (count == 0)
    {
        free(out);
        return 0;
    }
    *out_segments = out;
    *out_count = count;
    return 1;
}

static int asset_build_pp_scheme(lua_State* L, const AssetColorChoice* choice,
    const AssetSegment* base_segments, int base_count, int pp_idx, int use_pp_range,
    AssetSegment** out_segments, int* out_count)
{
    int count;
    int i;
    int hit = 0;
    AssetSegment* out;
    int out_n = 0;
    if (!lua_istable(L, pp_idx))
        return 0;
    pp_idx = lua_absindex(L, pp_idx);
    count = use_pp_range ? (int)lua_rawlen(L, pp_idx) : base_count;
    if (count <= 0)
        return 0;
    out = (AssetSegment*)calloc((size_t)count, sizeof(AssetSegment));
    if (!out)
        return 0;

    for (i = 0; i < count; i++)
    {
        AssetSegment seg;
        int idx;
        int valid = 1;
        memset(&seg, 0, sizeof(seg));
        if (use_pp_range)
        {
            int minX = 0;
            int maxX = 0;
            if (!asset_get_pp_range(L, pp_idx, i + 1, &minX, &maxX))
                valid = 0;
            seg.minX = minX;
            seg.maxX = maxX;
        }
        else if (i < base_count)
        {
            seg = base_segments[i];
        }
        else
        {
            valid = 0;
        }
        if (!valid)
            continue;

        idx = (choice->kind == ASSET_COLOR_PLAIN)
            ? (choice->count > 0 ? choice->values[0] : 0)
            : (i < choice->count ? choice->values[i] : 0);

        lua_geti(L, pp_idx, i + 1);
        if (lua_istable(L, -1))
        {
            lua_geti(L, -1, idx);
            if (lua_istable(L, -1))
            {
                double iFlag = 0.0;
                lua_getfield(L, -1, "iFlag");
                if (!lua_isnil(L, -1))
                {
                    int n;
                    iFlag = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                    seg.flag = (int)iFlag;
                    for (n = 1; n <= 15; n++)
                    {
                        lua_geti(L, -2, n);
                        seg.property[n] = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
                        lua_pop(L, 1);
                    }
                    hit = 1;
                }
                lua_pop(L, 1); /* iFlag */
            }
            lua_pop(L, 1); /* scheme */
        }
        lua_pop(L, 1); /* pp[i] */
        out[out_n++] = seg;
    }

    if (!hit || out_n == 0)
    {
        free(out);
        return 0;
    }
    *out_segments = out;
    *out_count = out_n;
    return 1;
}

static int asset_build_json_scheme(lua_State* L, int palettes_idx, const AssetColorChoice* choice,
    AssetSegment** out_segments, int* out_count)
{
    *out_segments = NULL;
    *out_count = 0;
    if (!lua_istable(L, palettes_idx))
        return 0;
    palettes_idx = lua_absindex(L, palettes_idx);

    if (choice->kind == ASSET_COLOR_PLAIN)
    {
        int idx = choice->count > 0 ? choice->values[0] : 0;
        return asset_read_palette_variant_segments(L, palettes_idx, idx + 1, 1, out_segments, out_count)
            && *out_count > 0;
    }

    if (choice->kind == ASSET_COLOR_PIPE || choice->kind == ASSET_COLOR_TABLE)
    {
        lua_Unsigned base_len;
        lua_Unsigned i;
        int hit = 0;
        int count = 0;
        AssetSegment* out;

        lua_geti(L, palettes_idx, 1);
        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            return 0;
        }
        base_len = lua_rawlen(L, -1);
        lua_pop(L, 1);
        if (base_len == 0)
            return 0;
        out = (AssetSegment*)malloc((size_t)base_len * sizeof(AssetSegment));
        if (!out)
            return 0;

        for (i = 1; i <= base_len; i++)
        {
            int idx = (i <= (lua_Unsigned)choice->count) ? choice->values[i - 1] : 0;
            if (idx != 0)
            {
                lua_geti(L, palettes_idx, idx + 1);
                if (lua_istable(L, -1))
                    hit = 1;
                else
                {
                    lua_pop(L, 1);
                    lua_geti(L, palettes_idx, 1);
                }
            }
            else
            {
                lua_geti(L, palettes_idx, 1);
            }

            if (lua_istable(L, -1))
            {
                lua_geti(L, -1, (lua_Integer)i);
                if (lua_istable(L, -1) && asset_read_segment_at(L, -1, &out[count]))
                    count++;
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        if (!hit || count == 0)
        {
            free(out);
            return 0;
        }
        qsort(out, (size_t)count, sizeof(AssetSegment), asset_segment_compare_minX);
        *out_segments = out;
        *out_count = count;
        return 1;
    }
    return 0;
}

static int asset_build_color_hs_segments(const AssetColorChoice* choice,
    const AssetSegment* base_segments, int base_count,
    AssetSegment** out_segments, int* out_count)
{
    int i;
    int base_value;
    AssetSegment* out = asset_clone_segments(base_segments, base_count);
    if (!out)
        return 0;
    base_value = (choice->kind == ASSET_COLOR_PIPE || choice->kind == ASSET_COLOR_TABLE || choice->kind == ASSET_COLOR_PLAIN) ? 0 : 1;
    for (i = 0; i < base_count; i++)
    {
        int v = i < choice->count ? choice->values[i] : 0;
        if (v > base_value)
        {
            int delta = v - base_value;
            int h = (delta * 111) % 1000;
            int s_steps = delta / 2;
            if (s_steps > 4) s_steps = 4;
            out[i].flag |= 8 | 16;
            out[i].property[14] = (double)h;
            out[i].property[15] = asset_clamp(500.0 + ((1.0 + s_steps) - 1.0) * 125.0, 0.0, 1000.0);
        }
    }
    *out_segments = out;
    *out_count = base_count;
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

static int asset_push_cjson_decoded(lua_State* L, const char* data, size_t len)
{
    int top = lua_gettop(L);

    lua_getglobal(L, "require");
    lua_pushstring(L, "cjson");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_settop(L, top);
        return 0;
    }

    lua_getfield(L, -1, "decode");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, top);
        return 0;
    }

    lua_pushlstring(L, data, len);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_settop(L, top);
        return 0;
    }

    /* Replace the cjson module table with the decoded result. */
    lua_copy(L, top + 2, top + 1);
    lua_settop(L, top + 1);
    return 1;
}

static void asset_set_integer_field(lua_State* L, int idx, const char* key, lua_Integer value)
{
    idx = lua_absindex(L, idx);
    lua_pushinteger(L, value);
    lua_setfield(L, idx, key);
}

static void asset_set_number_field(lua_State* L, int idx, const char* key, lua_Number value)
{
    idx = lua_absindex(L, idx);
    lua_pushnumber(L, value);
    lua_setfield(L, idx, key);
}

static double asset_get_number_field_or(lua_State* L, int table_idx, const char* key, double fallback)
{
    double value = fallback;
    table_idx = lua_absindex(L, table_idx);
    lua_getfield(L, table_idx, key);
    if (lua_isnumber(L, -1))
        value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

static int asset_lua_parse_atlas_frames(lua_State* L)
{
    int top = lua_gettop(L);
    size_t json_len = 0;
    const char* json = luaL_checklstring(L, 1, &json_len);
    int decoded_idx, mc_idx, animate_idx, frames_idx, res_idx, result_idx;
    lua_Unsigned frame_count, i;
    double frame_rate;
    int max_w = 0;
    int max_h = 0;

    if (!asset_push_cjson_decoded(L, json, json_len) || !lua_istable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return 1;
    }
    decoded_idx = lua_absindex(L, -1);

    lua_getfield(L, decoded_idx, "mc");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return 1;
    }
    mc_idx = lua_absindex(L, -1);

    lua_getfield(L, mc_idx, "animate");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return 1;
    }
    animate_idx = lua_absindex(L, -1);

    lua_getfield(L, animate_idx, "frames");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return 1;
    }
    frames_idx = lua_absindex(L, -1);

    lua_getfield(L, decoded_idx, "res");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        lua_pushnil(L);
        return 1;
    }
    res_idx = lua_absindex(L, -1);

    frame_count = lua_rawlen(L, frames_idx);
    if (!asset_get_number_field(L, decoded_idx, "frameRate", &frame_rate)
        && !asset_get_number_field(L, animate_idx, "frameRate", &frame_rate))
    {
        frame_rate = 8.0;
    }

    lua_createtable(L, (int)frame_count, 7);
    result_idx = lua_absindex(L, -1);
    asset_set_integer_field(L, result_idx, "group", 1);
    asset_set_integer_field(L, result_idx, "frame", (lua_Integer)frame_count);
    asset_set_number_field(L, result_idx, "frameRate", (lua_Number)frame_rate);
    asset_set_integer_field(L, result_idx, "x", 0);
    asset_set_integer_field(L, result_idx, "y", 0);

    for (i = 1; i <= frame_count; i++)
    {
        lua_geti(L, frames_idx, (lua_Integer)i);
        if (lua_istable(L, -1))
        {
            int f_idx = lua_absindex(L, -1);
            lua_getfield(L, f_idx, "res");
            lua_gettable(L, res_idx);
            if (lua_istable(L, -1))
            {
                int res_info_idx = lua_absindex(L, -1);
                double sx = asset_get_number_field_or(L, res_info_idx, "x", 0.0);
                double sy = asset_get_number_field_or(L, res_info_idx, "y", 0.0);
                double sw = asset_get_number_field_or(L, res_info_idx, "w", 0.0);
                double sh = asset_get_number_field_or(L, res_info_idx, "h", 0.0);
                double fx = asset_get_number_field_or(L, f_idx, "x", 0.0);
                double fy = asset_get_number_field_or(L, f_idx, "y", 0.0);
                double z = asset_get_number_field_or(L, f_idx, "z", 0.0);

                lua_createtable(L, 0, 7);
                asset_set_number_field(L, -1, "sx", (lua_Number)sx);
                asset_set_number_field(L, -1, "sy", (lua_Number)sy);
                asset_set_number_field(L, -1, "sw", (lua_Number)sw);
                asset_set_number_field(L, -1, "sh", (lua_Number)sh);
                asset_set_number_field(L, -1, "key_x", (lua_Number)(-fx));
                asset_set_number_field(L, -1, "key_y", (lua_Number)(-fy));
                asset_set_number_field(L, -1, "z", (lua_Number)z);
                lua_seti(L, result_idx, (lua_Integer)i);

                if ((int)sw > max_w) max_w = (int)sw;
                if ((int)sh > max_h) max_h = (int)sh;
            }
            lua_pop(L, 1); /* res_info */
        }
        lua_pop(L, 1); /* frame entry */
    }

    asset_set_integer_field(L, result_idx, "width", max_w);
    asset_set_integer_field(L, result_idx, "height", max_h);

    lua_copy(L, result_idx, top + 1);
    lua_settop(L, top + 1);
    return 1;
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

static int asset_push_palette_pixels_any(lua_State* L, const unsigned char* data, size_t len)
{
    size_t pixels_len = 0;
    const unsigned char* pixels = asset_palette_pixels_any(data, len, &pixels_len);
    if (!pixels)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char*)pixels, pixels_len);
    return 1;
}

static int asset_lua_compose_normal_palette(lua_State* L)
{
    int top = lua_gettop(L);
    size_t bmp_len = 0;
    size_t json_len = 0;
    const unsigned char* bmp = (const unsigned char*)luaL_checklstring(L, 1, &bmp_len);
    const char* json = luaL_optlstring(L, 2, NULL, &json_len);
    int hs_idx = lua_absindex(L, 4);
    int pp_idx = lua_absindex(L, 5);
    int use_pp_range = lua_toboolean(L, 6);
    int decoded_idx;
    int palettes_idx;
    int base_count = 0;
    int selected_count = 0;
    int has_hs_change;
    int has_selected = 0;
    AssetColor colors[256];
    AssetColorChoice choice;
    AssetSegment* base_segments = NULL;
    AssetSegment* selected_segments = NULL;

    if (!asset_decode_colors(bmp, bmp_len, colors))
    {
        lua_pushnil(L);
        return 1;
    }

    if (!json || json_len == 0 || !asset_push_cjson_decoded(L, json, json_len) || !lua_istable(L, -1))
    {
        lua_settop(L, top);
        return asset_push_palette_pixels_any(L, bmp, bmp_len);
    }
    decoded_idx = lua_absindex(L, -1);

    lua_getfield(L, decoded_idx, "palettes");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, top);
        return asset_push_palette_pixels_any(L, bmp, bmp_len);
    }
    palettes_idx = lua_absindex(L, -1);

    if (!asset_read_palette_variant_segments(L, palettes_idx, 1, 0, &base_segments, &base_count) || base_count <= 0)
    {
        free(base_segments);
        lua_settop(L, top);
        return asset_push_palette_pixels_any(L, bmp, bmp_len);
    }

    asset_parse_color_choice(L, 3, &choice);
    has_hs_change = asset_hs_has_changed(L, hs_idx);

    if (has_hs_change)
    {
        if (use_pp_range && asset_build_pp_range_default(L, pp_idx, &selected_segments, &selected_count))
        {
            asset_apply_hs_to_segments(L, selected_segments, selected_count, hs_idx);
            has_selected = 1;
        }
        else
        {
            selected_segments = asset_clone_segments(base_segments, base_count);
            selected_count = base_count;
            if (selected_segments)
            {
                asset_apply_hs_to_segments(L, selected_segments, selected_count, hs_idx);
                has_selected = 1;
            }
        }
    }
    else if (!asset_color_is_default(&choice))
    {
        if (asset_build_pp_scheme(L, &choice, base_segments, base_count, pp_idx, use_pp_range,
                &selected_segments, &selected_count))
        {
            has_selected = 1;
        }
        else if (asset_build_json_scheme(L, palettes_idx, &choice, &selected_segments, &selected_count))
        {
            has_selected = 1;
        }
        else if (asset_build_color_hs_segments(&choice, base_segments, base_count,
                &selected_segments, &selected_count))
        {
            has_selected = 1;
        }
    }
    else
    {
        selected_segments = asset_clone_segments(base_segments, base_count);
        selected_count = base_count;
        has_selected = selected_segments != NULL;
    }

    free(base_segments);
    if (!has_selected || !selected_segments)
    {
        lua_settop(L, top);
        return asset_push_palette_pixels_any(L, bmp, bmp_len);
    }

    lua_settop(L, top);
    asset_emit_palette(L, colors, selected_segments, selected_count);
    free(selected_segments);
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

static int asset_lua_build_atlas_jy(lua_State* L)
{
    size_t idx_len = 0;
    size_t json_len = 0;
    size_t pal_len = 0;
    size_t gray_len = 0;
    const char* idx_data = luaL_checklstring(L, 1, &idx_len);
    const char* json_data = luaL_checklstring(L, 2, &json_len);
    const char* pal_data = NULL;
    const char* gray_data = NULL;

    if (lua_type(L, 3) == LUA_TSTRING)
        pal_data = lua_tolstring(L, 3, &pal_len);
    if (lua_type(L, 4) == LUA_TSTRING)
        gray_data = lua_tolstring(L, 4, &gray_len);

    lua_getglobal(L, "require");
    lua_pushstring(L, "mygxy.jy");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "mygxy.jy is not callable");
        return 2;
    }

    lua_pushlstring(L, idx_data, idx_len);
    if (gray_data && gray_len > 0)
        lua_pushlstring(L, gray_data, gray_len);
    else
        lua_pushnil(L);
    if (pal_data && pal_len > 0)
        lua_pushlstring(L, pal_data, pal_len);
    else
        lua_pushnil(L);
    lua_pushlstring(L, json_data, json_len);

    if (lua_pcall(L, 4, 2, 0) != LUA_OK)
    {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 2;
}

static const luaL_Reg ASSET_FUNCS[] = {
    {"read_palette", asset_lua_read_palette},
    {"compose_palette", asset_lua_compose_palette},
    {"compose_xiangrui_palette", asset_lua_compose_xiangrui_palette},
    {"compose_normal_palette", asset_lua_compose_normal_palette},
    {"parse_atlas_frames", asset_lua_parse_atlas_frames},
    {"build_atlas_jy", asset_lua_build_atlas_jy},
    {NULL, NULL},
};

MYGXY_API int luaopen_mygxy_asset(lua_State* L)
{
    lua_createtable(L, 0, 6);
    luaL_setfuncs(L, ASSET_FUNCS, 0);
    return 1;
}
