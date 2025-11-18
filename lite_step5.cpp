/****************************************************************************
 * 标题: Lite复现 - 步骤5: 文档编辑系统
 * 文件: lite_step5.cpp
 * 版本: 0.1
 * 作者: AEGLOVE
 * 日期: 2025-11-18
 * 功能: 实现基本的文档编辑功能,包括光标、选择和文本操作
 * 依赖: Win32 API, Lua 5.5, stb_truetype
 * 环境: Windows11 x64, VS2022, C++17, Unicode字符集
 * 编码: utf8 with BOM (代码页65001)
****************************************************************************/

#if 1
#define NOMINMAX
#include <windows.h>  
#include <iostream>  
#include <cstdint>  
#include <vector>  
#include <fstream>  
#include <queue>  
#include <string>  
#include <cstring>  
#include <algorithm>  

#define STB_TRUETYPE_IMPLEMENTATION  
#include "stb_truetype.h"  

extern "C" {
#include "lua.h"  
#include "lualib.h"  
#include "lauxlib.h"  
}

#pragma comment(lib, "lua55.lib")  

// ============ 渲染缓存常量 ============  
constexpr int32_t CELLS_X = 80;
constexpr int32_t CELLS_Y = 50;
constexpr int32_t CELL_SIZE = 96;
constexpr int32_t COMMAND_BUF_SIZE = 1024 * 512;
constexpr uint32_t HASH_INITIAL = 2166136261u;

// ============ 命令类型 ============  
enum CommandType {
    CMD_DRAW_RECT,
    CMD_DRAW_TEXT,
    CMD_SET_CLIP
};

// ============ 数据结构 ============  
struct RenRect {
    int32_t x, y, width, height;
};

struct RenColor {
    uint8_t r, g, b, a;
};

struct Command {
    CommandType type;
    int32_t size;
    RenRect rect;
    RenColor color;
    char text[256];
};

struct Event {
    std::string type;
    std::vector<std::string> strArgs;
    std::vector<int32_t> intArgs;
};
static std::queue<Event> g_eventQueue;

// 添加pushEvent函数定义  
void pushEvent(const Event& evt) {
    g_eventQueue.push(evt);
}

struct DocLine {
    std::string text;
};

struct DocSelection {
    int32_t line1, col1;  // 选择起点  
    int32_t line2, col2;  // 选择终点  
};

struct Document {
    std::vector<DocLine> lines;
    DocSelection selection;
    bool dirty;

    Document() : dirty(false) {
        lines.push_back({ "" }); // 至少有一行  
        selection = { 1, 1, 1, 1 };
    }
};

static Document g_doc;

// ============ 全局变量 ============  
static HWND g_hwnd = nullptr;
static lua_State* g_L = nullptr;
static HDC g_memDC = nullptr;
static HBITMAP g_memBitmap = nullptr;
static uint8_t* g_pixels = nullptr;
static int32_t g_width = 800;
static int32_t g_height = 600;


// 字体相关  
static stbtt_fontinfo g_font;
static std::vector<uint8_t> g_fontBuffer;
static float g_fontSize = 20.0f;

// ============ 文档操作函数 ============  
void docSanitizePosition(int32_t& line, int32_t& col) {
    line = std::max(1, std::min(line, static_cast<int32_t>(g_doc.lines.size())));
    col = std::max(1, std::min(col, static_cast<int32_t>(g_doc.lines[line - 1].text.length()) + 1));
}

void docSetSelection(int32_t line1, int32_t col1, int32_t line2, int32_t col2) {
    docSanitizePosition(line1, col1);
    docSanitizePosition(line2, col2);
    g_doc.selection = { line1, col1, line2, col2 };
}

bool docHasSelection() {
    return !(g_doc.selection.line1 == g_doc.selection.line2 &&
        g_doc.selection.col1 == g_doc.selection.col2);
}

void docGetSelection(int32_t& line1, int32_t& col1, int32_t& line2, int32_t& col2, bool sort = false) {
    line1 = g_doc.selection.line1;
    col1 = g_doc.selection.col1;
    line2 = g_doc.selection.line2;
    col2 = g_doc.selection.col2;

    if (sort && (line1 > line2 || (line1 == line2 && col1 > col2))) {
        std::swap(line1, line2);
        std::swap(col1, col2);
    }
}

void docInsert(int32_t line, int32_t col, const std::string& text) {
    docSanitizePosition(line, col);

    if (text.find('\n') != std::string::npos) {
        // 处理多行插入  
        std::vector<std::string> newLines;
        size_t start = 0;
        size_t pos = 0;
        while ((pos = text.find('\n', start)) != std::string::npos) {
            newLines.push_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        newLines.push_back(text.substr(start));

        std::string& currentLine = g_doc.lines[line - 1].text;
        std::string before = currentLine.substr(0, col - 1);
        std::string after = currentLine.substr(col - 1);

        currentLine = before + newLines[0];
        for (size_t i = 1; i < newLines.size(); i++) {
            g_doc.lines.insert(g_doc.lines.begin() + line + i - 1, { newLines[i] });
        }
        g_doc.lines[line + newLines.size() - 2].text += after;

        docSetSelection(line + newLines.size() - 1,
            static_cast<int32_t>(newLines.back().length()) + 1,
            line + newLines.size() - 1,
            static_cast<int32_t>(newLines.back().length()) + 1);
    }
    else {
        // 单行插入  
        g_doc.lines[line - 1].text.insert(col - 1, text);
        docSetSelection(line, col + text.length(), line, col + text.length());
    }

    g_doc.dirty = true;
}

void docRemove(int32_t line1, int32_t col1, int32_t line2, int32_t col2) {
    docSanitizePosition(line1, col1);
    docSanitizePosition(line2, col2);

    if (line1 > line2 || (line1 == line2 && col1 > col2)) {
        std::swap(line1, line2);
        std::swap(col1, col2);
    }

    if (line1 == line2) {
        // 同一行删除  
        g_doc.lines[line1 - 1].text.erase(col1 - 1, col2 - col1);
    }
    else {
        // 跨行删除  
        std::string before = g_doc.lines[line1 - 1].text.substr(0, col1 - 1);
        std::string after = g_doc.lines[line2 - 1].text.substr(col2 - 1);
        g_doc.lines[line1 - 1].text = before + after;
        g_doc.lines.erase(g_doc.lines.begin() + line1, g_doc.lines.begin() + line2);
    }

    docSetSelection(line1, col1, line1, col1);
    g_doc.dirty = true;
}

void docTextInput(const std::string& text) {
    if (docHasSelection()) {
        int32_t line1, col1, line2, col2;
        docGetSelection(line1, col1, line2, col2, true);
        docRemove(line1, col1, line2, col2);
    }

    int32_t line, col, _, __;
    docGetSelection(line, col, _, __);
    docInsert(line, col, text);
}

// ============ 新增Lua API函数 ============  
static int luaApi_docGetText(lua_State* L) {
    int32_t lineNum = static_cast<int32_t>(luaL_checkinteger(L, 1));
    if (lineNum >= 1 && lineNum <= static_cast<int32_t>(g_doc.lines.size())) {
        lua_pushstring(L, g_doc.lines[lineNum - 1].text.c_str());
        return 1;
    }
    return 0;
}

static int luaApi_docGetLineCount(lua_State* L) {
    lua_pushinteger(L, g_doc.lines.size());
    return 1;
}

static int luaApi_docGetSelection(lua_State* L) {
    lua_pushinteger(L, g_doc.selection.line1);
    lua_pushinteger(L, g_doc.selection.col1);
    lua_pushinteger(L, g_doc.selection.line2);
    lua_pushinteger(L, g_doc.selection.col2);
    return 4;
}

static int luaApi_docSetSelection(lua_State* L) {
    int32_t line1 = static_cast<int32_t>(luaL_checkinteger(L, 1));
    int32_t col1 = static_cast<int32_t>(luaL_checkinteger(L, 2));
    int32_t line2 = static_cast<int32_t>(luaL_optinteger(L, 3, line1));
    int32_t col2 = static_cast<int32_t>(luaL_optinteger(L, 4, col1));
    docSetSelection(line1, col1, line2, col2);
    return 0;
}

static int luaApi_docTextInput(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    docTextInput(text);
    return 0;
}

static int luaApi_docInsert(lua_State* L) {
    int32_t line = static_cast<int32_t>(luaL_checkinteger(L, 1));
    int32_t col = static_cast<int32_t>(luaL_checkinteger(L, 2));
    const char* text = luaL_checkstring(L, 3);
    docInsert(line, col, text);
    return 0;
}

static int luaApi_docRemove(lua_State* L) {
    int32_t line1 = static_cast<int32_t>(luaL_checkinteger(L, 1));
    int32_t col1 = static_cast<int32_t>(luaL_checkinteger(L, 2));
    int32_t line2 = static_cast<int32_t>(luaL_checkinteger(L, 3));
    int32_t col2 = static_cast<int32_t>(luaL_checkinteger(L, 4));
    docRemove(line1, col1, line2, col2);
    return 0;
}

static int luaApi_getTextWidth(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);

    float scale = stbtt_ScaleForPixelHeight(&g_font, g_fontSize);
    float width = 0.0f;

    while (*text) {
        int32_t advance, lsb;
        stbtt_GetCodepointHMetrics(&g_font, *text, &advance, &lsb);
        width += advance * scale;
        ++text;
    }

    lua_pushnumber(L, width);
    return 1;
}



// 渲染缓存相关  
static uint32_t g_cellsBuf1[CELLS_X * CELLS_Y];
static uint32_t g_cellsBuf2[CELLS_X * CELLS_Y];
static uint32_t* g_cellsPrev = g_cellsBuf1;
static uint32_t* g_cells = g_cellsBuf2;
static RenRect g_rectBuf[CELLS_X * CELLS_Y / 2];
static char g_commandBuf[COMMAND_BUF_SIZE];
static int32_t g_commandBufIdx = 0;
static RenRect g_screenRect = { 0, 0, 800, 600 };
static RenRect g_clipRect = { 0, 0, 800, 600 };

// ============ 辅助函数 ============  
inline int32_t minInt(int32_t a, int32_t b) { return a < b ? a : b; }
inline int32_t maxInt(int32_t a, int32_t b) { return a > b ? a : b; }

// FNV-1a哈希函数  
void hashData(uint32_t* h, const void* data, int32_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (size--) {
        *h = (*h ^ *p++) * 16777619;
    }
}

inline int32_t cellIdx(int32_t x, int32_t y) {
    return x + y * CELLS_X;
}

bool rectsOverlap(RenRect a, RenRect b) {
    return b.x + b.width >= a.x && b.x <= a.x + a.width &&
        b.y + b.height >= a.y && b.y <= a.y + a.height;
}

RenRect intersectRects(RenRect a, RenRect b) {
    int32_t x1 = maxInt(a.x, b.x);
    int32_t y1 = maxInt(a.y, b.y);
    int32_t x2 = minInt(a.x + a.width, b.x + b.width);
    int32_t y2 = minInt(a.y + a.height, b.y + b.height);
    return { x1, y1, maxInt(0, x2 - x1), maxInt(0, y2 - y1) };
}

RenRect mergeRects(RenRect a, RenRect b) {
    int32_t x1 = minInt(a.x, b.x);
    int32_t y1 = minInt(a.y, b.y);
    int32_t x2 = maxInt(a.x + a.width, b.x + b.width);
    int32_t y2 = maxInt(a.y + a.height, b.y + b.height);
    return { x1, y1, x2 - x1, y2 - y1 };
}

// ============ 命令缓冲管理 ============  
Command* pushCommand(CommandType type, int32_t size) {
    Command* cmd = reinterpret_cast<Command*>(g_commandBuf + g_commandBufIdx);
    int32_t n = g_commandBufIdx + size;
    if (n > COMMAND_BUF_SIZE) {
        std::cerr << "警告: 命令缓冲区已满" << std::endl;
        return nullptr;
    }
    g_commandBufIdx = n;
    memset(cmd, 0, sizeof(Command));
    cmd->type = type;
    cmd->size = size;
    return cmd;
}

bool nextCommand(Command** prev) {
    if (*prev == nullptr) {
        *prev = reinterpret_cast<Command*>(g_commandBuf);
    }
    else {
        *prev = reinterpret_cast<Command*>(reinterpret_cast<char*>(*prev) + (*prev)->size);
    }
    return *prev != reinterpret_cast<Command*>(g_commandBuf + g_commandBufIdx);
}

// ============ 渲染缓存API ============  
void rencacheSetClipRect(RenRect rect) {
    Command* cmd = pushCommand(CMD_SET_CLIP, sizeof(Command));
    if (cmd) {
        cmd->rect = intersectRects(rect, g_screenRect);
        g_clipRect = cmd->rect;
    }
}

void rencacheDrawRect(RenRect rect, RenColor color) {
    if (!rectsOverlap(g_screenRect, rect)) return;
    Command* cmd = pushCommand(CMD_DRAW_RECT, sizeof(Command));
    if (cmd) {
        cmd->rect = rect;
        cmd->color = color;
    }
}

void rencacheDrawText(const char* text, int32_t x, int32_t y, RenColor color) {
    // 简化版本:假设固定字符宽度  
    int32_t textWidth = static_cast<int32_t>(strlen(text)) * 10;
    RenRect rect = { x, y, textWidth, 20 };

    if (!rectsOverlap(g_screenRect, rect)) return;
    Command* cmd = pushCommand(CMD_DRAW_TEXT, sizeof(Command));
    if (cmd) {
        cmd->rect = rect;
        cmd->color = color;
        strncpy_s(cmd->text, text, sizeof(cmd->text) - 1);
    }
}

void rencacheInvalidate() {
    memset(g_cellsPrev, 0xff, sizeof(g_cellsBuf1));
}

void rencacheBeginFrame() {
    // 检查屏幕尺寸是否改变  
    if (g_screenRect.width != g_width || g_screenRect.height != g_height) {
        g_screenRect.width = g_width;
        g_screenRect.height = g_height;
        rencacheInvalidate();
    }
}

// 更新重叠单元格的哈希值  
void updateOverlappingCells(RenRect r, uint32_t h) {
    int32_t x1 = r.x / CELL_SIZE;
    int32_t y1 = r.y / CELL_SIZE;
    int32_t x2 = (r.x + r.width) / CELL_SIZE;
    int32_t y2 = (r.y + r.height) / CELL_SIZE;

    for (int32_t y = y1; y <= y2; y++) {
        for (int32_t x = x1; x <= x2; x++) {
            if (x >= 0 && x < CELLS_X && y >= 0 && y < CELLS_Y) {
                int32_t idx = cellIdx(x, y);
                hashData(&g_cells[idx], &h, sizeof(h));
            }
        }
    }
}

// 推送脏矩形  
void pushRect(RenRect r, int32_t* count) {
    // 尝试与现有矩形合并  
    for (int32_t i = *count - 1; i >= 0; i--) {
        RenRect* rp = &g_rectBuf[i];
        if (rectsOverlap(*rp, r)) {
            *rp = mergeRects(*rp, r);
            return;
        }
    }
    // 无法合并,添加新矩形  
    g_rectBuf[(*count)++] = r;
}

// ============ 实际渲染函数 ============  
void drawTextActual(const char* text, int32_t x, int32_t y, RenColor color) {
    float scale = stbtt_ScaleForPixelHeight(&g_font, g_fontSize);
    int32_t ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    int32_t baseline = static_cast<int32_t>(ascent * scale);
    float xpos = static_cast<float>(x);

    while (*text) {
        int32_t advance, lsb;
        stbtt_GetCodepointHMetrics(&g_font, *text, &advance, &lsb);
        int32_t x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&g_font, *text, scale, scale, &x0, &y0, &x1, &y1);

        int32_t w = x1 - x0;
        int32_t h = y1 - y0;

        if (w > 0 && h > 0) {
            std::vector<uint8_t> bitmap(w * h);
            stbtt_MakeCodepointBitmap(&g_font, bitmap.data(), w, h, w, scale, scale, *text);

            for (int32_t py = 0; py < h; ++py) {
                for (int32_t px = 0; px < w; ++px) {
                    int32_t dx = static_cast<int32_t>(xpos) + x0 + px;
                    int32_t dy = y + baseline + y0 + py;

                    if (dx >= g_clipRect.x && dx < g_clipRect.x + g_clipRect.width &&
                        dy >= g_clipRect.y && dy < g_clipRect.y + g_clipRect.height &&
                        dx >= 0 && dx < g_width && dy >= 0 && dy < g_height) {
                        uint8_t alpha = bitmap[py * w + px];
                        int32_t idx = (dy * g_width + dx) * 4;
                        // Alpha混合  
                        float a = alpha / 255.0f;
                        g_pixels[idx + 0] = static_cast<uint8_t>((1 - a) * g_pixels[idx + 0] + a * color.b);
                        g_pixels[idx + 1] = static_cast<uint8_t>((1 - a) * g_pixels[idx + 1] + a * color.g);
                        g_pixels[idx + 2] = static_cast<uint8_t>((1 - a) * g_pixels[idx + 2] + a * color.r);
                    }
                }
            }
        }
        xpos += advance * scale;
        ++text;
    }
}

void drawRectActual(RenRect rect, RenColor color) {
    RenRect clipped = intersectRects(rect, g_clipRect);
    for (int32_t y = clipped.y; y < clipped.y + clipped.height; y++) {
        for (int32_t x = clipped.x; x < clipped.x + clipped.width; x++) {
            if (x >= 0 && x < g_width && y >= 0 && y < g_height) {
                int32_t idx = (y * g_width + x) * 4;
                g_pixels[idx + 0] = static_cast<uint8_t>((1 - color.a / 255.0f) * g_pixels[idx + 0] + (color.a / 255.0f) * color.b);
                g_pixels[idx + 1] = static_cast<uint8_t>((1 - color.a / 255.0f) * g_pixels[idx + 1] + (color.a / 255.0f) * color.g);
                g_pixels[idx + 2] = static_cast<uint8_t>((1 - color.a / 255.0f) * g_pixels[idx + 2] + (color.a / 255.0f) * color.r);
            }
        }
    }
}

// ============ 渲染缓存结束帧 ============  
void rencacheEndFrame() {
    // 1. 更新单元格哈希值  
    Command* cmd = nullptr;
    RenRect cr = g_screenRect;
    while (nextCommand(&cmd)) {
        if (cmd->type == CMD_SET_CLIP) {
            cr = cmd->rect;
        }
        RenRect r = intersectRects(cmd->rect, cr);
        if (r.width == 0 || r.height == 0) {
            continue;
        }
        uint32_t h = HASH_INITIAL;
        hashData(&h, cmd, cmd->size);
        updateOverlappingCells(r, h);
    }

    // 2. 检测改变的单元格并推送脏矩形  
    int32_t rectCount = 0;
    int32_t maxX = g_screenRect.width / CELL_SIZE + 1;
    int32_t maxY = g_screenRect.height / CELL_SIZE + 1;
    for (int32_t y = 0; y < maxY; y++) {
        for (int32_t x = 0; x < maxX; x++) {
            int32_t idx = cellIdx(x, y);
            if (g_cells[idx] != g_cellsPrev[idx]) {
                pushRect({ x, y, 1, 1 }, &rectCount);
            }
            g_cellsPrev[idx] = HASH_INITIAL;
        }
    }

    // 3. 将单元格坐标转换为像素坐标  
    for (int32_t i = 0; i < rectCount; i++) {
        RenRect* r = &g_rectBuf[i];
        r->x *= CELL_SIZE;
        r->y *= CELL_SIZE;
        r->width *= CELL_SIZE;
        r->height *= CELL_SIZE;
        *r = intersectRects(*r, g_screenRect);
    }

    // 4. 重绘脏矩形区域  
    for (int32_t i = 0; i < rectCount; i++) {
        RenRect r = g_rectBuf[i];
        g_clipRect = r;

        cmd = nullptr;
        while (nextCommand(&cmd)) {
            switch (cmd->type) {
            case CMD_SET_CLIP:
                g_clipRect = intersectRects(cmd->rect, r);
                break;
            case CMD_DRAW_RECT:
                drawRectActual(cmd->rect, cmd->color);
                break;
            case CMD_DRAW_TEXT:
                drawTextActual(cmd->text, cmd->rect.x, cmd->rect.y, cmd->color);
                break;
            }
        }
    }

    // 5. 交换单元格缓冲区并重置命令缓冲  
    uint32_t* tmp = g_cells;
    g_cells = g_cellsPrev;
    g_cellsPrev = tmp;
    g_commandBufIdx = 0;
}

// ============ Lua API函数 ============  
static int luaApi_pollEvent(lua_State* L) {
    if (g_eventQueue.empty()) {
        return 0;
    }
    Event evt = g_eventQueue.front();
    g_eventQueue.pop();
    lua_pushstring(L, evt.type.c_str());
    int returnCount = 1;
    for (const auto& arg : evt.strArgs) {
        lua_pushstring(L, arg.c_str());
        returnCount++;
    }
    for (int32_t arg : evt.intArgs) {
        lua_pushinteger(L, arg);
        returnCount++;
    }
    return returnCount;
}

static int luaApi_beginFrame(lua_State* L) {
    rencacheBeginFrame();
    return 0;
}

static int luaApi_endFrame(lua_State* L) {
    rencacheEndFrame();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    return 0;
}

static int luaApi_drawRect(lua_State* L) {
    int32_t x = static_cast<int32_t>(luaL_checkinteger(L, 1));
    int32_t y = static_cast<int32_t>(luaL_checkinteger(L, 2));
    int32_t w = static_cast<int32_t>(luaL_checkinteger(L, 3));
    int32_t h = static_cast<int32_t>(luaL_checkinteger(L, 4));
    uint8_t r = static_cast<uint8_t>(luaL_checkinteger(L, 5));
    uint8_t g = static_cast<uint8_t>(luaL_checkinteger(L, 6));
    uint8_t b = static_cast<uint8_t>(luaL_checkinteger(L, 7));
    uint8_t a = static_cast<uint8_t>(luaL_optinteger(L, 8, 255));

    rencacheDrawRect({ x, y, w, h }, { r, g, b, a });
    return 0;
}

static int luaApi_drawText(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    int32_t x = static_cast<int32_t>(luaL_checkinteger(L, 2));
    int32_t y = static_cast<int32_t>(luaL_checkinteger(L, 3));
    uint8_t r = static_cast<uint8_t>(luaL_checkinteger(L, 4));
    uint8_t g = static_cast<uint8_t>(luaL_checkinteger(L, 5));
    uint8_t b = static_cast<uint8_t>(luaL_checkinteger(L, 6));
    uint8_t a = static_cast<uint8_t>(luaL_optinteger(L, 7, 255));

    rencacheDrawText(text, x, y, { r, g, b, a });
    return 0;
}

static void registerApis(lua_State* L) {
    static const luaL_Reg systemLib[] = {
        {"poll_event", luaApi_pollEvent},
        {nullptr, nullptr}
    };

    static const luaL_Reg rendererLib[] = {
        {"begin_frame", luaApi_beginFrame},
        {"end_frame", luaApi_endFrame},
        {"draw_rect", luaApi_drawRect},
        {"draw_text", luaApi_drawText},
        {"get_text_width", luaApi_getTextWidth},
        {nullptr, nullptr}
    };

    // 新增:文档API  
    static const luaL_Reg docLib[] = {
        {"get_text", luaApi_docGetText},
        {"get_line_count", luaApi_docGetLineCount},
        {"get_selection", luaApi_docGetSelection},
        {"set_selection", luaApi_docSetSelection},
        {"text_input", luaApi_docTextInput},
        {"insert", luaApi_docInsert},
        {"remove", luaApi_docRemove},
        {nullptr, nullptr}
    };

    luaL_newlib(L, systemLib);
    lua_setglobal(L, "system");

    luaL_newlib(L, rendererLib);
    lua_setglobal(L, "renderer");

    luaL_newlib(L, docLib);
    lua_setglobal(L, "doc");
}

// ============ 字体加载 ============  
bool loadFont(const char* fontPath) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    g_fontBuffer.resize(fileSize);
    file.read(reinterpret_cast<char*>(g_fontBuffer.data()), fileSize);
    file.close();
    return stbtt_InitFont(&g_font, g_fontBuffer.data(), 0) != 0;
}

// ============ 窗口过程 ============  
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        g_eventQueue.push({ "quit", {}, {} });
        PostQuitMessage(0);
        return 0;

    case WM_CHAR: {
        // 处理字符输入  
        if (wParam >= 32 || wParam == '\r' || wParam == '\t') {
            char ch[2] = { static_cast<char>(wParam), '\0' };
            if (wParam == '\r') {
                docTextInput("\n");
            }
            else {
                docTextInput(ch);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        Event evt;
        evt.type = "keydown";
        evt.intArgs.push_back(static_cast<int32_t>(wParam));
        pushEvent(evt);

        // 处理特殊键  
        switch (wParam) {
        case VK_BACK: {
            if (docHasSelection()) {
                int32_t l1, c1, l2, c2;
                docGetSelection(l1, c1, l2, c2, true);
                docRemove(l1, c1, l2, c2);
            }
            else {
                int32_t line, col, _, __;
                docGetSelection(line, col, _, __);
                if (col > 1) {
                    docRemove(line, col - 1, line, col);
                }
                else if (line > 1) {
                    int32_t prevLen = g_doc.lines[line - 2].text.length() + 1;
                    docRemove(line - 1, prevLen, line, 1);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_DELETE: {
            if (docHasSelection()) {
                int32_t l1, c1, l2, c2;
                docGetSelection(l1, c1, l2, c2, true);
                docRemove(l1, c1, l2, c2);
            }
            else {
                int32_t line, col, _, __;
                docGetSelection(line, col, _, __);
                int32_t lineLen = g_doc.lines[line - 1].text.length();
                if (col <= lineLen) {
                    docRemove(line, col, line, col + 1);
                }
                else if (line < static_cast<int32_t>(g_doc.lines.size())) {
                    docRemove(line, col, line + 1, 1);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_LEFT: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            if (col > 1) {
                docSetSelection(line, col - 1, line, col - 1);
            }
            else if (line > 1) {
                int32_t prevLen = g_doc.lines[line - 2].text.length() + 1;
                docSetSelection(line - 1, prevLen, line - 1, prevLen);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_RIGHT: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            int32_t lineLen = g_doc.lines[line - 1].text.length();
            if (col <= lineLen) {
                docSetSelection(line, col + 1, line, col + 1);
            }
            else if (line < static_cast<int32_t>(g_doc.lines.size())) {
                docSetSelection(line + 1, 1, line + 1, 1);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_UP: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            if (line > 1) {
                docSetSelection(line - 1, col, line - 1, col);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_DOWN: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            if (line < static_cast<int32_t>(g_doc.lines.size())) {
                docSetSelection(line + 1, col, line + 1, col);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_HOME: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            docSetSelection(line, 1, line, 1);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case VK_END: {
            int32_t line, col, _, __;
            docGetSelection(line, col, _, __);
            int32_t lineLen = g_doc.lines[line - 1].text.length() + 1;
            docSetSelection(line, lineLen, line, lineLen);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        }
        return 0;
    }

    case WM_SIZE: {
        int32_t newWidth = LOWORD(lParam);
        int32_t newHeight = HIWORD(lParam);

        // 避免最小化时的无效尺寸  
        if (newWidth == 0 || newHeight == 0) {
            return 0;
        }

        if (newWidth != g_width || newHeight != g_height) {
            g_width = newWidth;
            g_height = newHeight;

            // 1. 更新屏幕矩形和裁剪矩形  
            g_screenRect.width = g_width;
            g_screenRect.height = g_height;
            g_clipRect.width = g_width;
            g_clipRect.height = g_height;

            // 2. 重新创建内存位图  
            if (g_memBitmap) DeleteObject(g_memBitmap);
            if (g_memDC) DeleteDC(g_memDC);

            HDC hdc = GetDC(hwnd);
            g_memDC = CreateCompatibleDC(hdc);

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = g_width;
            bmi.bmiHeader.biHeight = -g_height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            g_memBitmap = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS,
                reinterpret_cast<void**>(&g_pixels), nullptr, 0);
            SelectObject(g_memDC, g_memBitmap);
            ReleaseDC(hwnd, hdc);

            // 3. 使渲染缓存失效  
            rencacheInvalidate();

            // 4. 强制立即重绘  
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);  // 立即发送WM_PAINT  
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 调用Lua渲染函数  
        lua_getglobal(g_L, "render");
        if (lua_pcall(g_L, 0, 0, 0) != LUA_OK) {
            std::cerr << "Lua错误: " << lua_tostring(g_L, -1) << std::endl;
            lua_pop(g_L, 1);
        }

        BitBlt(hdc, 0, 0, g_width, g_height, g_memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============ 修改:主函数,添加文档渲染 ============  
int main(int argc, char** argv) {
    // 1. 初始化Lua  
    g_L = luaL_newstate();
    if (!g_L) return 1;
    luaL_openlibs(g_L);
    registerApis(g_L);

    // 2. 加载字体  
    if (!loadFont("C:/Windows/Fonts/consola.ttf")) {
        lua_close(g_L);
        return 1;
    }

    // 3. 创建窗口  
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LiteCloneWindow";
    RegisterClassEx(&wc);

    g_hwnd = CreateWindowEx(0, L"LiteCloneWindow", L"Lite Clone - Step 5: Document Editing",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        g_width, g_height, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    HDC hdc = GetDC(g_hwnd);
    g_memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_width;
    bmi.bmiHeader.biHeight = -g_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_memBitmap = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&g_pixels), nullptr, 0);
    SelectObject(g_memDC, g_memBitmap);
    ReleaseDC(g_hwnd, hdc);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // 4. 初始化文档内容  
    g_doc.lines[0].text = "Welcome to Lite Clone!";
    g_doc.lines.push_back({ "Type something..." });
    g_doc.lines.push_back({ "" });

    // 5. Lua渲染脚本  
    const char* luaCode = R"(  
        print("文档编辑系统测试")  
          
        function render()  
            renderer.begin_frame()  
              
            -- 绘制背景  
            renderer.draw_rect(0, 0, 800, 600, 255, 255, 255, 255)  
              
            -- 绘制标题  
            renderer.draw_text("Lite Clone - Document Editing", 10, 10, 0, 0, 0, 255)  
              
            -- 绘制文档内容  
            local lineCount = doc.get_line_count()  
            local line1, col1, line2, col2 = doc.get_selection()  
              
            for i = 1, lineCount do  
                local text = doc.get_text(i)  
                local y = 40 + (i - 1) * 25  
                  
                -- 绘制行号  
                renderer.draw_text(tostring(i), 10, y, 128, 128, 128, 255)  
                  
                -- 绘制文本  
                renderer.draw_text(text, 50, y, 0, 0, 0, 255)  
                  
                -- 绘制光标  
                if i == line1 and line1 == line2 then  
                    local textBeforeCursor = text:sub(1, col1 - 1)  
                    local cursorX = math.floor(50 + renderer.get_text_width(textBeforeCursor))  
                    renderer.draw_rect(cursorX, y, 2, 20, 0, 0, 255, 255)  
                end  
            end  
              
            -- 绘制状态栏  
            local status = string.format("Line %d, Col %d | Lines: %d",   
                                        line1, col1, lineCount)  
            renderer.draw_text(status, 10, 570, 64, 64, 64, 255)  
              
            renderer.end_frame()  
        end  
          
        _G.render = render  
    )";

    if (luaL_dostring(g_L, luaCode) != LUA_OK) {
        std::cerr << "Lua错误: " << lua_tostring(g_L, -1) << std::endl;
        lua_pop(g_L, 1);
    }

    // 6. 主循环  
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 调用Lua渲染函数  
        lua_getglobal(g_L, "render");
        if (lua_pcall(g_L, 0, 0, 0) != LUA_OK) {
            std::cerr << "Lua错误: " << lua_tostring(g_L, -1) << std::endl;
            lua_pop(g_L, 1);
        }

        Sleep(16); // ~60 FPS  
    }

    // 7. 清理  
    DeleteObject(g_memBitmap);
    DeleteDC(g_memDC);
    lua_close(g_L);
    return 0;
}

#endif
