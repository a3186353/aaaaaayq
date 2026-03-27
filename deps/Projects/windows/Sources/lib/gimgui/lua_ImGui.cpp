// lua_ImGui.cpp - ImGui Lua bindings for GGELUA engine
// Reconstructed from Lua-side API analysis
//
// The Lua side uses: local im = require 'gimgui'
// Then calls im.Begin(), im.Button(), etc.
// Also uses GIM = require 'gimgui' for context/frame management.

#include "help.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

// ==== Context & Lifecycle ====

static SDL_Renderer* g_Renderer = NULL; // saved for Render()

static int l_CreateContext(lua_State* L) {
    ImGuiContext* ctx = ImGui::CreateContext();
    lua_pushlightuserdata(L, ctx);
    return 1;
}

static int l_SetCurrentContext(lua_State* L) {
    ImGuiContext* ctx = (ImGuiContext*)lua_touserdata(L, 1);
    ImGui::SetCurrentContext(ctx);
    return 0;
}

static int l_InitForSDLRenderer(lua_State* L) {
    // Arg1: SDL_Window userdata (from gsdl2, stored as SDL_Window** inside userdata)
    // Arg2: SDL_Renderer userdata (from gsdl2, stored as SDL_Renderer** inside userdata)
    SDL_Window* window = *(SDL_Window**)luaL_checkudata(L, 1, "SDL_Window");
    SDL_Renderer* renderer = *(SDL_Renderer**)luaL_checkudata(L, 2, "SDL_Renderer");
    g_Renderer = renderer; // save for Render()
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    return 0;
}

static int l_Event(lua_State* L) {
    // Arg1: SDL_Event* (userdata with metatable "SDL_Event")
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    // Never consume SDL_QUIT or SDL_WINDOWEVENT/CLOSE events
    // so the main loop can handle window closing
    if (ev->type == SDL_QUIT) {
        ImGui_ImplSDL2_ProcessEvent(ev); // let ImGui see it, but don't consume
        lua_pushboolean(L, 0);
        return 1;
    }
    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_CLOSE) {
        ImGui_ImplSDL2_ProcessEvent(ev);
        lua_pushboolean(L, 0);
        return 1;
    }
    bool consumed = ImGui_ImplSDL2_ProcessEvent(ev);
    lua_pushboolean(L, consumed);
    return 1;
}

static int l_NewFrame(lua_State* L) {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    return 0;
}

static int l_EndFrame(lua_State* L) {
    ImGui::EndFrame();
    return 0;
}

static int l_Render(lua_State* L) {
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_Renderer);
    return 0;
}

// ==== Style ====

static int l_StyleColorsLight(lua_State* L) {
    ImGui::StyleColorsLight();
    return 0;
}

static int l_StyleColorsClassic(lua_State* L) {
    ImGui::StyleColorsClassic();
    return 0;
}

static int l_StyleColorsDark(lua_State* L) {
    ImGui::StyleColorsDark();
    return 0;
}

// ==== Font ====

static int l_AddFontFromFileTTF(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    float size_pixels = (float)luaL_optnumber(L, 2, 14.0);
    
    ImGuiIO& io = ImGui::GetIO();
    
    // For CJK characters support
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x206F, // General Punctuation
        0x3000, 0x30FF, // CJK Symbols and Punctuation, Hiragana, Katakana
        0x31F0, 0x31FF, // Katakana Phonetic Extensions
        0xFF00, 0xFFEF, // Half-width characters
        0x4e00, 0x9FAF, // CJK Ideographs
        0x0,
    };
    
    ImFontConfig config;
    config.OversampleH = 2;  // Horizontal oversampling for better quality
    config.OversampleV = 1;
    config.PixelSnapH = true; // Pixel-aligned rendering for sharper text
    
    ImFont* font = io.Fonts->AddFontFromFileTTF(filename, size_pixels, &config, ranges);
    if (font) {
        lua_pushlightuserdata(L, font);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// ==== Window ====

// im.Begin(name, [p_open_table], [flags]) -> bool
// p_open_table is a Lua table where [1] is the bool value
static int l_Begin(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    
    if (lua_gettop(L) >= 3) {
        // Begin(name, p_open_table, flags)
        bool p_open = true;
        if (lua_istable(L, 2)) {
            lua_geti(L, 2, 1);
            p_open = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 3, 0);
        bool visible = ImGui::Begin(name, &p_open, flags);
        // Write back p_open
        if (lua_istable(L, 2)) {
            lua_pushboolean(L, p_open);
            lua_seti(L, 2, 1);
        }
        lua_pushboolean(L, visible);
    } else if (lua_gettop(L) >= 2 && lua_isnumber(L, 2)) {
        // Begin(name, flags)
        ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0);
        bool visible = ImGui::Begin(name, NULL, flags);
        lua_pushboolean(L, visible);
    } else {
        bool visible = ImGui::Begin(name);
        lua_pushboolean(L, visible);
    }
    return 1;
}

static int l_End(lua_State* L) {
    ImGui::End();
    return 0;
}

// ==== Child Windows ====

static int l_BeginChild(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    float w = (float)luaL_optnumber(L, 2, 0);
    float h = (float)luaL_optnumber(L, 3, 0);
    bool border = lua_toboolean(L, 4) != 0;
    ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 5, 0);
    bool r = ImGui::BeginChild(id, ImVec2(w, h), border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None, flags);
    lua_pushboolean(L, r);
    return 1;
}

static int l_EndChild(lua_State* L) {
    ImGui::EndChild();
    return 0;
}

static int l_BeginChildFrame(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    float w = (float)luaL_optnumber(L, 2, 0);
    float h = (float)luaL_optnumber(L, 3, 0);
    ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 4, 0);
    ImGuiID id = ImGui::GetID(str_id);
    // BeginChildFrame was removed in newer ImGui, use BeginChild with FrameStyle
    bool r = ImGui::BeginChild(id, ImVec2(w, h), ImGuiChildFlags_FrameStyle, flags);
    lua_pushboolean(L, r);
    return 1;
}

static int l_EndChildFrame(lua_State* L) {
    ImGui::EndChild();
    return 0;
}

// ==== Window Utilities ====

static int l_SetNextWindowPos(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
    return 0;
}

static int l_SetNextWindowSize(lua_State* L) {
    float w = (float)luaL_checknumber(L, 1);
    float h = (float)luaL_checknumber(L, 2);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
    return 0;
}

static int l_SetNextWindowBgAlpha(lua_State* L) {
    float alpha = (float)luaL_checknumber(L, 1);
    ImGui::SetNextWindowBgAlpha(alpha);
    return 0;
}

static int l_GetWindowSize(lua_State* L) {
    ImVec2 sz = ImGui::GetWindowSize();
    lua_pushnumber(L, sz.x);
    lua_pushnumber(L, sz.y);
    return 2;
}

static int l_GetWindowPos(lua_State* L) {
    ImVec2 pos = ImGui::GetWindowPos();
    lua_pushnumber(L, pos.x);
    lua_pushnumber(L, pos.y);
    return 2;
}

static int l_GetWindowContentRegionMax(lua_State* L) {
    ImVec2 r = ImGui::GetContentRegionAvail();
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    return 2;
}

// ==== Cursor / Layout ====

static int l_SetCursorPos(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    ImGui::SetCursorPos(ImVec2(x, y));
    return 0;
}

static int l_GetCursorPos(lua_State* L) {
    ImVec2 pos = ImGui::GetCursorPos();
    lua_pushnumber(L, pos.x);
    lua_pushnumber(L, pos.y);
    return 2;
}

static int l_SameLine(lua_State* L) {
    float offset_from_start = (float)luaL_optnumber(L, 1, 0);
    float spacing = (float)luaL_optnumber(L, 2, -1);
    ImGui::SameLine(offset_from_start, spacing);
    return 0;
}

static int l_Separator(lua_State* L) {
    ImGui::Separator();
    return 0;
}

static int l_AlignTextToFramePadding(lua_State* L) {
    ImGui::AlignTextToFramePadding();
    return 0;
}

static int l_GetContentRegionAvail(lua_State* L) {
    ImVec2 r = ImGui::GetContentRegionAvail();
    lua_pushnumber(L, r.x);
    lua_pushnumber(L, r.y);
    return 2;
}

// ==== Scrolling ====

static int l_GetScrollY(lua_State* L) {
    lua_pushnumber(L, ImGui::GetScrollY());
    return 1;
}

static int l_GetScrollMaxY(lua_State* L) {
    lua_pushnumber(L, ImGui::GetScrollMaxY());
    return 1;
}

static int l_SetScrollHereY(lua_State* L) {
    float center_y_ratio = (float)luaL_optnumber(L, 1, 0.5f);
    ImGui::SetScrollHereY(center_y_ratio);
    return 0;
}

// ==== ID ====

static int l_PushID(lua_State* L) {
    if (lua_isstring(L, 1)) {
        ImGui::PushID(lua_tostring(L, 1));
    } else {
        ImGui::PushID((int)luaL_checkinteger(L, 1));
    }
    return 0;
}

static int l_PopID(lua_State* L) {
    ImGui::PopID();
    return 0;
}

// ==== Text ====

static int l_TextWrapped(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    ImGui::TextWrapped("%s", text);
    return 0;
}

static int l_TextDisabled(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    ImGui::TextDisabled("%s", text);
    return 0;
}

static int l_TextColored(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    float r = (float)luaL_optnumber(L, 2, 1.0);
    float g = (float)luaL_optnumber(L, 3, 1.0);
    float b = (float)luaL_optnumber(L, 4, 1.0);
    float a = (float)luaL_optnumber(L, 5, 1.0);
    ImGui::TextColored(ImVec4(r, g, b, a), "%s", text);
    return 0;
}

static int l_TextUnformatted(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    ImGui::TextUnformatted(text);
    return 0;
}

static int l_SetTooltip(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    ImGui::SetTooltip("%s", text);
    return 0;
}

// ==== Button ====

// im.Button(label, [w], [h]) -> bool
static int l_Button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    float w = (float)luaL_optnumber(L, 2, 0);
    float h = (float)luaL_optnumber(L, 3, 0);
    lua_pushboolean(L, ImGui::Button(label, ImVec2(w, h)));
    return 1;
}

// im.RadioButton(label, v_table, v_button) -> bool
static int l_RadioButton(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int v_button = (int)luaL_checkinteger(L, 3);
    
    lua_geti(L, 2, 1);
    int v = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::RadioButton(label, &v, v_button);
    if (changed) {
        lua_pushinteger(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// im.Checkbox(label, v_table) -> bool
static int l_Checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    
    bool changed = ImGui::Checkbox(label, &v);
    if (changed) {
        lua_pushboolean(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// Helper to get SDL_Texture* from Lua - handles both:
// - lightuserdata (raw SDL_Texture* pointer)
// - full userdata with "SDL_Texture" metatable (SDL_Texture** inside)
static SDL_Texture* get_sdl_texture(lua_State* L, int idx) {
    // Try full userdata with "SDL_Texture" metatable first
    SDL_Texture** pp = (SDL_Texture**)luaL_testudata(L, idx, "SDL_Texture");
    if (pp) return *pp;
    // Fall back to lightuserdata (raw pointer)
    if (lua_islightuserdata(L, idx))
        return (SDL_Texture*)lua_touserdata(L, idx);
    return NULL;
}

// im.ImageButton(str_id, texture, [w, h]) -> bool
static int l_ImageButton(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    SDL_Texture* tex = get_sdl_texture(L, 2);
    if (!tex) {
        lua_pushboolean(L, false);
        return 1;
    }
    float w = (float)luaL_optnumber(L, 3, 0);
    float h = (float)luaL_optnumber(L, 4, 0);
    if (w <= 0 || h <= 0) {
        int iw, ih;
        SDL_QueryTexture(tex, NULL, NULL, &iw, &ih);
        w = (float)iw;
        h = (float)ih;
    }
    lua_pushboolean(L, ImGui::ImageButton(str_id, (ImTextureID)tex, ImVec2(w, h)));
    return 1;
}

// im.InvisibleButton(str_id, w, h) -> bool
static int l_InvisibleButton(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    float w = (float)luaL_checknumber(L, 2);
    float h = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, ImGui::InvisibleButton(str_id, ImVec2(w, h)));
    return 1;
}

// ==== Image ====

static int l_Image(lua_State* L) {
    SDL_Texture* tex = get_sdl_texture(L, 1);
    if (!tex) return 0;
    float w = (float)luaL_optnumber(L, 2, 0);
    float h = (float)luaL_optnumber(L, 3, 0);
    if (w <= 0 || h <= 0) {
        int iw, ih;
        SDL_QueryTexture(tex, NULL, NULL, &iw, &ih);
        w = (float)iw;
        h = (float)ih;
    }
    ImGui::Image((ImTextureID)tex, ImVec2(w, h));
    return 0;
}

// ==== Selectable ====

// im.Selectable(label, selected, flags) -> bool
static int l_Selectable(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool selected = lua_toboolean(L, 2) != 0;
    ImGuiSelectableFlags flags = (ImGuiSelectableFlags)luaL_optinteger(L, 3, 0);
    bool clicked = ImGui::Selectable(label, selected, flags);
    lua_pushboolean(L, clicked);
    return 1;
}

// ==== Input ====

// im.InputText(label, buf_table) -> bool
// buf_table = { "text", max_len }
static int l_InputText(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    const char* text = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_geti(L, 2, 2);
    int buf_size = (int)luaL_optinteger(L, -1, 256);
    lua_pop(L, 1);
    
    // Ensure reasonable buffer size
    if (buf_size < 1) buf_size = 256;
    if (buf_size > 65536) buf_size = 65536;
    
    char* buf = (char*)alloca(buf_size + 1);
    if (text) {
        strncpy(buf, text, buf_size);
        buf[buf_size] = 0;
    } else {
        buf[0] = 0;
    }
    
    bool changed = ImGui::InputText(label, buf, buf_size);
    if (changed) {
        lua_pushstring(L, buf);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// im.InputTextMultiline(label, buf_table, w, h, flags) -> bool
static int l_InputTextMultiline(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    float w = (float)luaL_optnumber(L, 3, 0);
    float h = (float)luaL_optnumber(L, 4, 0);
    ImGuiInputTextFlags flags = (ImGuiInputTextFlags)luaL_optinteger(L, 5, 0);
    
    lua_geti(L, 2, 1);
    const char* text = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_geti(L, 2, 2);
    int buf_size = (int)luaL_optinteger(L, -1, 1024);
    lua_pop(L, 1);
    
    if (buf_size < 1) buf_size = 1024;
    if (buf_size > 65536) buf_size = 65536;
    
    char* buf = (char*)alloca(buf_size + 1);
    if (text) {
        strncpy(buf, text, buf_size);
        buf[buf_size] = 0;
    } else {
        buf[0] = 0;
    }
    
    bool changed = ImGui::InputTextMultiline(label, buf, buf_size, ImVec2(w, h), flags);
    if (changed) {
        lua_pushstring(L, buf);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// im.InputFloat(label, buf_table) -> bool
static int l_InputFloat(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    float v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::InputFloat(label, &v);
    if (changed) {
        lua_pushnumber(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// im.InputInt(label, buf_table) -> bool
static int l_InputInt(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    int v = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::InputInt(label, &v);
    if (changed) {
        lua_pushinteger(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// ==== Slider helpers ====

#define DEFINE_SLIDER_FLOAT_N(N) \
static int l_SliderFloat##N(lua_State* L) { \
    const char* label = luaL_checkstring(L, 1); \
    luaL_checktype(L, 2, LUA_TTABLE); \
    float v_min = (float)luaL_optnumber(L, 3, 0); \
    float v_max = (float)luaL_optnumber(L, 4, 100); \
    float v[N]; \
    for (int i = 0; i < N; i++) { lua_geti(L, 2, i+1); v[i] = (float)lua_tonumber(L, -1); lua_pop(L, 1); } \
    bool changed = ImGui::SliderFloat##N(label, v, v_min, v_max); \
    if (changed) { for (int i = 0; i < N; i++) { lua_pushnumber(L, v[i]); lua_seti(L, 2, i+1); } } \
    lua_pushboolean(L, changed); \
    return 1; \
}

// SliderFloat (single)
static int l_SliderFloat(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    float v_min = (float)luaL_optnumber(L, 3, 0);
    float v_max = (float)luaL_optnumber(L, 4, 100);
    
    lua_geti(L, 2, 1);
    float v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::SliderFloat(label, &v, v_min, v_max);
    if (changed) {
        lua_pushnumber(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

DEFINE_SLIDER_FLOAT_N(2)
DEFINE_SLIDER_FLOAT_N(3)
DEFINE_SLIDER_FLOAT_N(4)

#define DEFINE_SLIDER_INT_N(N) \
static int l_SliderInt##N(lua_State* L) { \
    const char* label = luaL_checkstring(L, 1); \
    luaL_checktype(L, 2, LUA_TTABLE); \
    int v_min = (int)luaL_optinteger(L, 3, 0); \
    int v_max = (int)luaL_optinteger(L, 4, 100); \
    int v[N]; \
    for (int i = 0; i < N; i++) { lua_geti(L, 2, i+1); v[i] = (int)lua_tointeger(L, -1); lua_pop(L, 1); } \
    bool changed = ImGui::SliderInt##N(label, v, v_min, v_max); \
    if (changed) { for (int i = 0; i < N; i++) { lua_pushinteger(L, v[i]); lua_seti(L, 2, i+1); } } \
    lua_pushboolean(L, changed); \
    return 1; \
}

static int l_SliderInt(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int v_min = (int)luaL_optinteger(L, 3, 0);
    int v_max = (int)luaL_optinteger(L, 4, 100);
    
    lua_geti(L, 2, 1);
    int v = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::SliderInt(label, &v, v_min, v_max);
    if (changed) {
        lua_pushinteger(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

DEFINE_SLIDER_INT_N(2)
DEFINE_SLIDER_INT_N(3)
DEFINE_SLIDER_INT_N(4)

// ==== Drag ====

static int l_DragFloat(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    float v_speed = (float)luaL_optnumber(L, 3, 1.0);
    float v_min = (float)luaL_optnumber(L, 4, 0);
    float v_max = (float)luaL_optnumber(L, 5, 0);
    
    lua_geti(L, 2, 1);
    float v = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::DragFloat(label, &v, v_speed, v_min, v_max);
    if (changed) {
        lua_pushnumber(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

#define DEFINE_DRAG_FLOAT_N(N) \
static int l_DragFloat##N(lua_State* L) { \
    const char* label = luaL_checkstring(L, 1); \
    luaL_checktype(L, 2, LUA_TTABLE); \
    float v_speed = (float)luaL_optnumber(L, 3, 1.0); \
    float v_min = (float)luaL_optnumber(L, 4, 0); \
    float v_max = (float)luaL_optnumber(L, 5, 0); \
    float v[N]; \
    for (int i = 0; i < N; i++) { lua_geti(L, 2, i+1); v[i] = (float)lua_tonumber(L, -1); lua_pop(L, 1); } \
    bool changed = ImGui::DragFloat##N(label, v, v_speed, v_min, v_max); \
    if (changed) { for (int i = 0; i < N; i++) { lua_pushnumber(L, v[i]); lua_seti(L, 2, i+1); } } \
    lua_pushboolean(L, changed); \
    return 1; \
}

DEFINE_DRAG_FLOAT_N(2)
DEFINE_DRAG_FLOAT_N(3)
DEFINE_DRAG_FLOAT_N(4)

static int l_DragInt(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    float v_speed = (float)luaL_optnumber(L, 3, 1.0);
    int v_min = (int)luaL_optinteger(L, 4, 0);
    int v_max = (int)luaL_optinteger(L, 5, 0);
    
    lua_geti(L, 2, 1);
    int v = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    bool changed = ImGui::DragInt(label, &v, v_speed, v_min, v_max);
    if (changed) {
        lua_pushinteger(L, v);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

#define DEFINE_DRAG_INT_N(N) \
static int l_DragInt##N(lua_State* L) { \
    const char* label = luaL_checkstring(L, 1); \
    luaL_checktype(L, 2, LUA_TTABLE); \
    float v_speed = (float)luaL_optnumber(L, 3, 1.0); \
    int v_min = (int)luaL_optinteger(L, 4, 0); \
    int v_max = (int)luaL_optinteger(L, 5, 0); \
    int v[N]; \
    for (int i = 0; i < N; i++) { lua_geti(L, 2, i+1); v[i] = (int)lua_tointeger(L, -1); lua_pop(L, 1); } \
    bool changed = ImGui::DragInt##N(label, v, v_speed, v_min, v_max); \
    if (changed) { for (int i = 0; i < N; i++) { lua_pushinteger(L, v[i]); lua_seti(L, 2, i+1); } } \
    lua_pushboolean(L, changed); \
    return 1; \
}

DEFINE_DRAG_INT_N(2)
DEFINE_DRAG_INT_N(3)
DEFINE_DRAG_INT_N(4)

// ==== Combo / ListBox ====

// im.Combo(label, current_item_table, items_table) -> bool
static int l_Combo(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    int current = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // Build items string (null-separated, double-null terminated)
    int count = (int)luaL_len(L, 3);
    
    // Use callback-based combo
    struct ComboData {
        lua_State* L;
        int table_idx;
    } data = { L, 3 };
    
    auto getter = [](void* user_data, int idx) -> const char* {
        ComboData* d = (ComboData*)user_data;
        lua_geti(d->L, d->table_idx, idx + 1); // Lua 1-indexed
        const char* s = lua_tostring(d->L, -1);
        lua_pop(d->L, 1);
        return s;
    };
    
    // Convert from Lua 1-indexed to ImGui 0-indexed
    int imgui_current = current - 1;
    bool changed = ImGui::Combo(label, &imgui_current, getter, &data, count);
    if (changed) {
        lua_pushinteger(L, imgui_current + 1); // back to Lua 1-indexed
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// im.ListBox(label, current_item_table, items_table) -> bool
static int l_ListBox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    
    lua_geti(L, 2, 1);
    int current = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    int count = (int)luaL_len(L, 3);
    
    struct ListData {
        lua_State* L;
        int table_idx;
    } data = { L, 3 };
    
    auto getter = [](void* user_data, int idx) -> const char* {
        ListData* d = (ListData*)user_data;
        lua_geti(d->L, d->table_idx, idx + 1);
        const char* s = lua_tostring(d->L, -1);
        lua_pop(d->L, 1);
        return s;
    };
    
    int imgui_current = current - 1;
    bool changed = ImGui::ListBox(label, &imgui_current, getter, &data, count);
    if (changed) {
        lua_pushinteger(L, imgui_current + 1);
        lua_seti(L, 2, 1);
    }
    lua_pushboolean(L, changed);
    return 1;
}

// ==== ProgressBar ====

static int l_ProgressBar(lua_State* L) {
    float fraction = (float)luaL_checknumber(L, 1);
    float w = (float)luaL_optnumber(L, 2, -FLT_MIN);
    float h = (float)luaL_optnumber(L, 3, 0);
    const char* overlay = luaL_optstring(L, 4, NULL);
    ImGui::ProgressBar(fraction, ImVec2(w, h), overlay);
    return 0;
}

// ==== Tree ====

static int l_TreeNode(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    lua_pushboolean(L, ImGui::TreeNode(label));
    return 1;
}

static int l_TreeNodeEx(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    ImGuiTreeNodeFlags flags = (ImGuiTreeNodeFlags)luaL_optinteger(L, 2, 0);
    lua_pushboolean(L, ImGui::TreeNodeEx(label, flags));
    return 1;
}

static int l_TreePop(lua_State* L) {
    ImGui::TreePop();
    return 0;
}

static int l_SetNextItemOpen(lua_State* L) {
    bool open = lua_toboolean(L, 1) != 0;
    ImGui::SetNextItemOpen(open);
    return 0;
}

// ==== TabBar ====

static int l_BeginTabBar(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    ImGuiTabBarFlags flags = (ImGuiTabBarFlags)luaL_optinteger(L, 2, 0);
    lua_pushboolean(L, ImGui::BeginTabBar(str_id, flags));
    return 1;
}

static int l_EndTabBar(lua_State* L) {
    ImGui::EndTabBar();
    return 0;
}

static int l_BeginTabItem(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    lua_pushboolean(L, ImGui::BeginTabItem(label));
    return 1;
}

static int l_EndTabItem(lua_State* L) {
    ImGui::EndTabItem();
    return 0;
}

// ==== Table ====

static int l_BeginTable(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    int columns = (int)luaL_checkinteger(L, 2);
    ImGuiTableFlags flags = (ImGuiTableFlags)luaL_optinteger(L, 3, 0);
    lua_pushboolean(L, ImGui::BeginTable(str_id, columns, flags));
    return 1;
}

static int l_EndTable(lua_State* L) {
    ImGui::EndTable();
    return 0;
}

static int l_TableSetupColumn(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    ImGuiTableColumnFlags flags = (ImGuiTableColumnFlags)luaL_optinteger(L, 2, 0);
    ImGui::TableSetupColumn(label, flags);
    return 0;
}

static int l_TableHeadersRow(lua_State* L) {
    ImGui::TableHeadersRow();
    return 0;
}

static int l_TableNextRow(lua_State* L) {
    ImGui::TableNextRow();
    return 0;
}

static int l_TableNextColumn(lua_State* L) {
    lua_pushboolean(L, ImGui::TableNextColumn());
    return 1;
}

static int l_TableSetColumnIndex(lua_State* L) {
    int column = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, ImGui::TableSetColumnIndex(column));
    return 1;
}

// ==== Menu ====

static int l_BeginMainMenuBar(lua_State* L) {
    lua_pushboolean(L, ImGui::BeginMainMenuBar());
    return 1;
}

static int l_EndMainMenuBar(lua_State* L) {
    ImGui::EndMainMenuBar();
    return 0;
}

static int l_BeginMenuBar(lua_State* L) {
    lua_pushboolean(L, ImGui::BeginMenuBar());
    return 1;
}

static int l_EndMenuBar(lua_State* L) {
    ImGui::EndMenuBar();
    return 0;
}

// im.BeginMenu(label, [enabled]) -> bool
static int l_BeginMenu(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool enabled = (lua_gettop(L) >= 2) ? !lua_toboolean(L, 2) : true; // Note: Lua passes 是否禁止 (disabled), ImGui expects enabled
    lua_pushboolean(L, ImGui::BeginMenu(label, enabled));
    return 1;
}

static int l_EndMenu(lua_State* L) {
    ImGui::EndMenu();
    return 0;
}

// im.MenuItem(label, shortcut, selected_or_table, enabled) -> bool
static int l_MenuItem(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    const char* shortcut = lua_isstring(L, 2) ? lua_tostring(L, 2) : NULL;
    bool enabled = true;
    
    if (lua_istable(L, 3)) {
        // MenuItem with selectable state via table
        lua_geti(L, 3, 1);
        bool selected = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        
        if (lua_gettop(L) >= 4) {
            enabled = !lua_toboolean(L, 4); // 是否禁止 -> enabled
        }
        
        bool clicked = ImGui::MenuItem(label, shortcut, &selected, enabled);
        if (clicked) {
            lua_pushboolean(L, selected);
            lua_seti(L, 3, 1);
        }
        lua_pushboolean(L, clicked);
    } else {
        bool selected = lua_toboolean(L, 3) != 0;
        if (lua_gettop(L) >= 4) {
            enabled = !lua_toboolean(L, 4);
        }
        lua_pushboolean(L, ImGui::MenuItem(label, shortcut, selected, enabled));
    }
    return 1;
}

// ==== Popup ====

static int l_OpenPopup(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    ImGui::OpenPopup(str_id);
    return 0;
}

static int l_BeginPopup(lua_State* L) {
    const char* str_id = luaL_checkstring(L, 1);
    ImGuiWindowFlags flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0);
    lua_pushboolean(L, ImGui::BeginPopup(str_id, flags));
    return 1;
}

// im.BeginPopupModal(name, [p_open_table], flags) -> bool
static int l_BeginPopupModal(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    ImGuiWindowFlags flags = 0;
    
    if (lua_istable(L, 2)) {
        lua_geti(L, 2, 1);
        bool p_open = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        flags = (ImGuiWindowFlags)luaL_optinteger(L, 3, 0);
        
        bool visible = ImGui::BeginPopupModal(name, &p_open, flags);
        lua_pushboolean(L, p_open);
        lua_seti(L, 2, 1);
        lua_pushboolean(L, visible);
    } else {
        flags = (ImGuiWindowFlags)luaL_optinteger(L, 2, 0);
        lua_pushboolean(L, ImGui::BeginPopupModal(name, NULL, flags));
    }
    return 1;
}

static int l_BeginPopupContextItem(lua_State* L) {
    const char* str_id = luaL_optstring(L, 1, NULL);
    lua_pushboolean(L, ImGui::BeginPopupContextItem(str_id));
    return 1;
}

static int l_BeginPopupContextWindow(lua_State* L) {
    const char* str_id = luaL_optstring(L, 1, NULL);
    lua_pushboolean(L, ImGui::BeginPopupContextWindow(str_id));
    return 1;
}

static int l_EndPopup(lua_State* L) {
    ImGui::EndPopup();
    return 0;
}

static int l_CloseCurrentPopup(lua_State* L) {
    ImGui::CloseCurrentPopup();
    return 0;
}

// ==== Tooltip ====

static int l_BeginTooltip(lua_State* L) {
    ImGui::BeginTooltip();
    return 0;
}

static int l_EndTooltip(lua_State* L) {
    ImGui::EndTooltip();
    return 0;
}

// ==== Mouse / Input Queries ====

static int l_IsItemHovered(lua_State* L) {
    lua_pushboolean(L, ImGui::IsItemHovered());
    return 1;
}

static int l_IsItemClicked(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsItemClicked(button));
    return 1;
}

static int l_IsItemActive(lua_State* L) {
    lua_pushboolean(L, ImGui::IsItemActive());
    return 1;
}

static int l_IsMouseDown(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsMouseDown(button));
    return 1;
}

static int l_IsMouseReleased(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsMouseReleased(button));
    return 1;
}

static int l_IsMouseClicked(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsMouseClicked(button));
    return 1;
}

static int l_IsMouseDoubleClicked(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsMouseDoubleClicked(button));
    return 1;
}

static int l_IsMouseDragging(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    lua_pushboolean(L, ImGui::IsMouseDragging(button));
    return 1;
}

static int l_GetMouseDragDelta(lua_State* L) {
    ImGuiMouseButton button = (ImGuiMouseButton)luaL_optinteger(L, 1, 0);
    ImVec2 d = ImGui::GetMouseDragDelta(button);
    lua_pushnumber(L, d.x);
    lua_pushnumber(L, d.y);
    return 2;
}

static int l_GetMousePos(lua_State* L) {
    ImVec2 p = ImGui::GetMousePos();
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}

// ==== Style Colors ====

static int l_PushStyleColor(lua_State* L) {
    ImGuiCol idx = (ImGuiCol)luaL_checkinteger(L, 1);
    ImU32 col = (ImU32)luaL_checkinteger(L, 2);
    ImGui::PushStyleColor(idx, col);
    return 0;
}

static int l_PopStyleColor(lua_State* L) {
    int count = (int)luaL_optinteger(L, 1, 1);
    ImGui::PopStyleColor(count);
    return 0;
}

// ==== Disabled ====

static int l_BeginDisabled(lua_State* L) {
    bool disabled = (lua_gettop(L) >= 1) ? (lua_toboolean(L, 1) != 0) : true;
    ImGui::BeginDisabled(disabled);
    return 0;
}

static int l_EndDisabled(lua_State* L) {
    ImGui::EndDisabled();
    return 0;
}

// ==== ShowDemoWindow ====

static int l_ShowDemoWindow(lua_State* L) {
    ImGui::ShowDemoWindow();
    return 0;
}

// ==== Registration table ====

static const luaL_Reg imgui_funcs[] = {
    // Context & Lifecycle
    {"CreateContext", l_CreateContext},
    {"SetCurrentContext", l_SetCurrentContext},
    {"InitForSDLRenderer", l_InitForSDLRenderer},
    {"Event", l_Event},
    {"NewFrame", l_NewFrame},
    {"EndFrame", l_EndFrame},
    {"Render", l_Render},
    
    // Style
    {"StyleColorsLight", l_StyleColorsLight},
    {"StyleColorsClassic", l_StyleColorsClassic},
    {"StyleColorsDark", l_StyleColorsDark},
    
    // Font
    {"AddFontFromFileTTF", l_AddFontFromFileTTF},
    
    // Window
    {"Begin", l_Begin},
    {"End", l_End},
    {"BeginChild", l_BeginChild},
    {"EndChild", l_EndChild},
    {"BeginChildFrame", l_BeginChildFrame},
    {"EndChildFrame", l_EndChildFrame},
    {"SetNextWindowPos", l_SetNextWindowPos},
    {"SetNextWindowSize", l_SetNextWindowSize},
    {"SetNextWindowBgAlpha", l_SetNextWindowBgAlpha},
    {"GetWindowSize", l_GetWindowSize},
    {"GetWindowPos", l_GetWindowPos},
    {"GetWindowContentRegionMax", l_GetWindowContentRegionMax},
    
    // Cursor / Layout
    {"SetCursorPos", l_SetCursorPos},
    {"GetCursorPos", l_GetCursorPos},
    {"SameLine", l_SameLine},
    {"Separator", l_Separator},
    {"AlignTextToFramePadding", l_AlignTextToFramePadding},
    {"GetContentRegionAvail", l_GetContentRegionAvail},
    
    // Scrolling
    {"GetScrollY", l_GetScrollY},
    {"GetScrollMaxY", l_GetScrollMaxY},
    {"SetScrollHereY", l_SetScrollHereY},
    
    // ID
    {"PushID", l_PushID},
    {"PopID", l_PopID},
    
    // Text
    {"TextWrapped", l_TextWrapped},
    {"TextDisabled", l_TextDisabled},
    {"TextColored", l_TextColored},
    {"TextUnformatted", l_TextUnformatted},
    {"SetTooltip", l_SetTooltip},
    
    // Button
    {"Button", l_Button},
    {"RadioButton", l_RadioButton},
    {"Checkbox", l_Checkbox},
    {"ImageButton", l_ImageButton},
    {"InvisibleButton", l_InvisibleButton},
    
    // Image
    {"Image", l_Image},
    
    // Selectable
    {"Selectable", l_Selectable},
    
    // Input
    {"InputText", l_InputText},
    {"InputTextMultiline", l_InputTextMultiline},
    {"InputFloat", l_InputFloat},
    {"InputInt", l_InputInt},
    
    // Slider
    {"SliderFloat", l_SliderFloat},
    {"SliderFloat2", l_SliderFloat2},
    {"SliderFloat3", l_SliderFloat3},
    {"SliderFloat4", l_SliderFloat4},
    {"SliderInt", l_SliderInt},
    {"SliderInt2", l_SliderInt2},
    {"SliderInt3", l_SliderInt3},
    {"SliderInt4", l_SliderInt4},
    
    // Drag
    {"DragFloat", l_DragFloat},
    {"DragFloat2", l_DragFloat2},
    {"DragFloat3", l_DragFloat3},
    {"DragFloat4", l_DragFloat4},
    {"DragInt", l_DragInt},
    {"DragInt2", l_DragInt2},
    {"DragInt3", l_DragInt3},
    {"DragInt4", l_DragInt4},
    
    // Combo / ListBox
    {"Combo", l_Combo},
    {"ListBox", l_ListBox},
    
    // ProgressBar
    {"ProgressBar", l_ProgressBar},
    
    // Tree
    {"TreeNode", l_TreeNode},
    {"TreeNodeEx", l_TreeNodeEx},
    {"TreePop", l_TreePop},
    {"SetNextItemOpen", l_SetNextItemOpen},
    
    // TabBar
    {"BeginTabBar", l_BeginTabBar},
    {"EndTabBar", l_EndTabBar},
    {"BeginTabItem", l_BeginTabItem},
    {"EndTabItem", l_EndTabItem},
    
    // Table
    {"BeginTable", l_BeginTable},
    {"EndTable", l_EndTable},
    {"TableSetupColumn", l_TableSetupColumn},
    {"TableHeadersRow", l_TableHeadersRow},
    {"TableNextRow", l_TableNextRow},
    {"TableNextColumn", l_TableNextColumn},
    {"TableSetColumnIndex", l_TableSetColumnIndex},
    
    // Menu
    {"BeginMainMenuBar", l_BeginMainMenuBar},
    {"EndMainMenuBar", l_EndMainMenuBar},
    {"BeginMenuBar", l_BeginMenuBar},
    {"EndMenuBar", l_EndMenuBar},
    {"BeginMenu", l_BeginMenu},
    {"EndMenu", l_EndMenu},
    {"MenuItem", l_MenuItem},
    
    // Popup
    {"OpenPopup", l_OpenPopup},
    {"BeginPopup", l_BeginPopup},
    {"BeginPopupModal", l_BeginPopupModal},
    {"BeginPopupContextItem", l_BeginPopupContextItem},
    {"BeginPopupContextWindow", l_BeginPopupContextWindow},
    {"EndPopup", l_EndPopup},
    {"CloseCurrentPopup", l_CloseCurrentPopup},
    
    // Tooltip
    {"BeginTooltip", l_BeginTooltip},
    {"EndTooltip", l_EndTooltip},
    
    // Mouse / Input
    {"IsItemHovered", l_IsItemHovered},
    {"IsItemClicked", l_IsItemClicked},
    {"IsItemActive", l_IsItemActive},
    {"IsMouseDown", l_IsMouseDown},
    {"IsMouseReleased", l_IsMouseReleased},
    {"IsMouseClicked", l_IsMouseClicked},
    {"IsMouseDoubleClicked", l_IsMouseDoubleClicked},
    {"IsMouseDragging", l_IsMouseDragging},
    {"GetMouseDragDelta", l_GetMouseDragDelta},
    {"GetMousePos", l_GetMousePos},
    
    // Style Colors
    {"PushStyleColor", l_PushStyleColor},
    {"PopStyleColor", l_PopStyleColor},
    
    // Disabled
    {"BeginDisabled", l_BeginDisabled},
    {"EndDisabled", l_EndDisabled},
    
    // Demo
    {"ShowDemoWindow", l_ShowDemoWindow},
    
    {NULL, NULL}
};

int bind_lua_imgui(lua_State* L) {
    luaL_setfuncs(L, imgui_funcs, 0);
    return 0;
}
