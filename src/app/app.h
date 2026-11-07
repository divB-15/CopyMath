// ============================================================================
//  CopyMath — app.h
//
//  全局状态与跨模块接口的唯一声明处。所有 .cpp 都 include 这个头。
//
//  模块划分（每个文件夹 = 一个职责，依赖只能自上而下）：
//
//      app/     main.cpp            程序入口：DPI 感知、初始化、消息循环
//               app_core.cpp        **全局变量定义** + Px / SetStatus / ApplyTopmost 等小工具
//               actions.cpp         复制、保存、重置、探测 TeX Live、日志封装
//               render_controller.cpp 渲染调度线程（版本号 + 忙标志）
//      ui/      main_window.cpp    主窗口过程（建控件、布局、命令分发）
//               settings_dialog.cpp “设置”对话框
//               preview_view.cpp    右侧预览子窗口（自绘）
//               edit_subclass.cpp   编辑框 Ctrl+A / 粘贴 CRLF 规范化
//      render/  render.cpp         调 pdflatex + pdftocairo 出图
//      config/  config.cpp         读写 CopyMath.cfg
//      log/     log.cpp            使用日志
//      clipboard/ clipboard.cpp    位图写进剪贴板（CF_DIB）
//      util/    util.cpp           UTF-16<->UTF-8、换行符规范化、目录/路径工具
//
//  依赖方向：ui → app → (render / config / log / clipboard / util)。
//  core 那几层（render/config/log/clipboard/util）**不认识**界面与全局变量，
//  只吃参数、返回结果，所以可以单独编译测试。
// ============================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// objbase.h 必须在 gdiplus.h 之前：WIN32_LEAN_AND_MEAN 把 ole2 系列挡掉了，
// 而 gdiplus.h 里用到的 PROPID / PropertyItem 正是 objidl.h 定义的，
// 少了它 MinGW 会报一堆 “'PROPID' has not been declared”。
#include <objbase.h>
#include <gdiplus.h>
#include <string>

#include "config.h"

// ---- 版本号 ---------------------------------------------------------------
//  同时体现在 exe 文件属性（app.rc 的 VERSIONINFO）与窗口标题上，两者请保持一致。
#define COPYMATH_VERSION L"1.3"

// ---- 主窗口控件 ID --------------------------------------------------------
#define IDC_EDIT      101   // 左侧 LaTeX 源码输入框
#define IDC_VIEW      102   // 右侧公式预览子窗口
#define IDC_COPY      103   // “复制图片”按钮
#define IDC_SAVE      104   // “保存图片”按钮
#define IDC_SETTINGS  105   // “设置”按钮
#define IDC_STATUS    106   // 底部状态栏
#define IDC_RENDER    107   // “渲染”按钮（快捷键 F5）
#define IDC_RESET     108   // “重置”按钮（快捷键 F6）：清空缓存与输入框
#define IDC_TOPMOST   109   // “总是置顶”切换按钮（v1.2）
#define IDM_RENDER_F5 400   // F5 加速键映射到的命令 ID

// 自定义窗口消息：后台渲染线程用它们把结果送回主线程
#define WM_RENDER_DONE   (WM_USER + 1)   // lParam = new 出来的 Bitmap*
#define WM_RENDER_ERROR  (WM_USER + 2)   // lParam = new 出来的 std::wstring*（错误信息）

// ---- 设置对话框控件 ID ----------------------------------------------------
#define IDS_TEX         201
#define IDS_EXP         202
#define IDS_PRE         203
#define IDS_BROWSE_TEX  204
#define IDS_BROWSE_EXP  205
#define IDS_SAVE_BTN    206
#define IDS_CANCEL      207
#define IDS_DPI         208   // “输出分辨率”标签
#define IDS_DPI_HINT    209   // 分辨率右侧的提示文字
#define IDS_LOG_CHK     210   // “自动记录日志”复选框
#define IDS_LOG         211   // “日志文件路径”标签
#define IDS_BROWSE_LOG  212

// ---- 全局状态（定义在 app_core.cpp）---------------------------------------
extern HINSTANCE g_hInst;

// 主窗口及其控件
extern HWND g_hMain, g_hEdit, g_hView;
extern HWND g_hBtnRender, g_hBtnReset, g_hBtnCopy,
            g_hBtnSave, g_hBtnTop, g_hBtnSettings, g_hStatus;

// 设置对话框及其控件
extern HWND g_hSettings, g_hSetTex, g_hSetExp, g_hSetPre,
            g_hSetDpi, g_hSetLogChk, g_hSetLog;

extern HACCEL g_hAccel;        // 加速键表（F5 / F6 / Ctrl+D / F7 / Ctrl+S）
extern HFONT  g_hFontUI;       // 界面字体（微软雅黑，按 DPI 缩放）
extern HFONT  g_hFontEdit;     // 输入框字体（略大，便于阅读代码）
extern double g_dpiScale;      // DPI 缩放系数 = LOGPIXELSY / 96
extern HICON  g_hIconBig;      // 程序图标（标题栏 / 任务栏）
extern HICON  g_hIconSmall;

// 渲染结果与状态
extern Gdiplus::Bitmap* g_viewBitmap;   // 当前显示的公式图（主线程持有并 delete）
extern std::wstring g_lastError;        // 最近一次渲染错误（为空表示无错误）
extern bool g_hasError;

extern Config g_cfg;                    // 全局配置（路径 / 导言区 / DPI / 日志 / 置顶）

// 渲染线程与主线程的共享状态（用 g_cs 临界区保护）
extern CRITICAL_SECTION g_cs;
extern std::wstring g_currentTex;       // 最近一次请求渲染的源码
extern int g_gen;                       // “版本号”：每次请求 +1，用于丢弃过期结果
extern bool g_rendering;                // 是否已有渲染线程在跑

extern ULONG_PTR g_gdiToken;            // GdiplusStartup 返回的令牌

// ---- 小工具（app_core.cpp）------------------------------------------------
int Px(int v);                                        // 96DPI 逻辑像素 → 当前 DPI
std::wstring GetWindowTextStr(HWND h);                // 取窗口文本
void SetStatus(const std::wstring& s);                // 写状态栏
int CurrentDpi();                                     // 钳制过的当前 DPI
std::wstring GetEditText();                           // 左侧输入框内容
void ApplyTopmost(bool on);                           // 置顶开关 + 记进配置

// ---- 动作（actions.cpp）---------------------------------------------------
void CopyViewToClipboard();
void SaveImage();
void ResetAll();                                      // 左右一起重置（含清空输入框）
std::wstring AutoDetectTexlive();
std::wstring BrowseFolder(HWND owner, const std::wstring& title);

// ---- 渲染调度（render_controller.cpp）-------------------------------------
void RequestRender();                  // 请求一次渲染（异步）
void ResetRenderState();               // 仅重置右侧：缓存 / 预览 / 错误
void RenderNow();                      // 先重置右侧，再渲染
DWORD WINAPI RenderThread(LPVOID);     // 后台渲染线程

// ---- 界面（ui/）-----------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ViewProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
void OpenSettings();
void SubclassEditForSelectAll(HWND hEdit);   // 让编辑框支持 Ctrl+A 全选
