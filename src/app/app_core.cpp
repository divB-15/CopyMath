// ============================================================================
//  app_core.cpp — 全局状态的定义 + 几个到处都要用的小工具
//
//  程序里所有跨模块共享的变量（窗口句柄 / 配置 / 渲染结果 / 临界区）都**定义**在
//  这个文件里，声明在 app.h。这样别的模块只要 include "app.h" 就能用，
//  不会出现“每个 .cpp 各有一份同名全局”的重复定义。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "util.h"
#include <string>

// ---- 全局状态 -------------------------------------------------------------
HINSTANCE g_hInst = NULL;

// 主窗口及其控件
HWND g_hMain = NULL, g_hEdit = NULL, g_hView = NULL;
HWND g_hBtnRender = NULL, g_hBtnReset = NULL, g_hBtnCopy = NULL,
     g_hBtnSave = NULL, g_hBtnTop = NULL, g_hBtnSettings = NULL, g_hStatus = NULL;

// 设置对话框及其控件
HWND g_hSettings = NULL, g_hSetTex = NULL, g_hSetExp = NULL, g_hSetPre = NULL,
     g_hSetDpi = NULL, g_hSetLogChk = NULL, g_hSetLog = NULL;

HACCEL g_hAccel = NULL;        // 加速键表（F5 / F6 / Ctrl+D / F7 / Ctrl+S）
HFONT  g_hFontUI = NULL;       // 界面字体（微软雅黑，按 DPI 缩放）
HFONT  g_hFontEdit = NULL;     // 输入框字体（略大，便于阅读代码）
double g_dpiScale = 1.0;       // DPI 缩放系数 = LOGPIXELSY / 96
HICON  g_hIconBig = NULL;      // 程序图标（标题栏 / 任务栏）
HICON  g_hIconSmall = NULL;

// 渲染结果与状态
Gdiplus::Bitmap* g_viewBitmap = NULL;   // 当前显示的公式图（由主线程持有并负责 delete）
std::wstring g_lastError;               // 最近一次渲染错误（为空表示无错误）
bool g_hasError = false;

Config g_cfg;                  // 全局配置（路径 / 导言区 / DPI / 日志 / 置顶）

// 渲染线程与主线程的共享状态（用 g_cs 临界区保护）
CRITICAL_SECTION g_cs;
std::wstring g_currentTex;     // 最近一次请求渲染的源码
int g_gen = 0;                 // “版本号”：每次请求 +1，用于丢弃过期的渲染结果
bool g_rendering = false;      // 是否已有渲染线程在跑（保证同一时刻只有一个）

ULONG_PTR g_gdiToken = 0;      // GdiplusStartup 返回的令牌，退出时要交给 GdiplusShutdown

// 把“按 96 DPI 设计”的像素尺寸换算到当前 DPI。
// 开启 DPI 感知后系统不再帮我们缩放，所以所有界面尺寸都要自己乘这个系数，
// 否则在高分屏上控件会显得又窄又矮、文字被裁掉。
int Px(int v) { return (int)(v * g_dpiScale + 0.5); }

// 取窗口文本（返回 std::wstring，省掉到处写“问长度 + 取内容”两行代码）
std::wstring GetWindowTextStr(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

// 在底部状态栏显示一行提示
void SetStatus(const std::wstring& s) {
    if (g_hStatus) SetWindowTextW(g_hStatus, s.c_str());
}

// 当前配置里的 DPI（别处多次用到，抽成一个小函数）
int CurrentDpi() {
    int d;
    EnterCriticalSection(&g_cs);
    d = g_cfg.dpi;
    LeaveCriticalSection(&g_cs);
    return (d >= kDpiMin && d <= kDpiMax) ? d : kDpiDefault;
}

// ---- 基础操作 -------------------------------------------------------------

// 读取左侧输入框的全部文本
std::wstring GetEditText() {
    return GetWindowTextStr(g_hEdit);
}

// 应用“总是置顶”状态（同时把状态记进配置，下次启动照旧）
void ApplyTopmost(bool on) {
    SetWindowPos(g_hMain, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EnterCriticalSection(&g_cs);
    g_cfg.alwaysOnTop = on;
    LeaveCriticalSection(&g_cs);
    SaveConfig(g_cfg);
}
