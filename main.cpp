// ============================================================================
//  CopyMath — main.cpp
//
//  程序主入口与界面层。整个程序分成 6 个模块：
//
//      main.cpp      界面、窗口过程、渲染调度线程（本文件）
//      render.cpp    调 TeX Live 把 LaTeX 渲染成位图
//      config.cpp    读写 CopyMath.cfg 配置
//      clipboard.cpp 把图片以 CF_DIB 写入剪贴板
//      log.cpp       把每次“复制/保存”的公式追加到日志文件
//      util.cpp      UTF-16 <-> UTF-8 转换、递归建目录
//
//  技术要点：
//    · 纯 Win32 API + GDI+，不使用 Qt 等任何第三方 GUI 库；
//    · 公式排版外包给本机 TeX Live（pdflatex + pdftocairo），保真度 100%；
//    · 渲染放在后台线程，界面不卡；
//    · 手动渲染（F5 / 按钮），不随输入实时触发。
//
//  快捷键：F5 渲染　F6 重置（含清空输入）　Ctrl+D / F7 复制图片
//            Ctrl+S 保存图片　Ctrl+A 全选
//
//  ---------------------------------------------------------------------------
//  v1.2 的六处改动：
//    1) 默认导言区换成 mathtools,amssymb,physics,bm,mathrsfs,esint,tensor,yhmath
//       （见 config.h 的 kDefaultPreamble）；
//    2) 新增“总是置顶”切换按钮（状态记进配置文件）；
//    3) 复制/保存的图片分辨率（DPI）改成可在设置里手动调整，默认仍是 300；
//       —— 剪贴板 CF_DIB 与 PNG 都会写上这个 DPI，粘进 Word 的物理尺寸才对得上；
//    4) 新增可选的“自动记录日志”：每次复制/保存都把源码与时间追加到 logs.txt；
//    5) 默认窗口尺寸调小（1000×400 → 880×340），并让按钮行在窄窗口下自动换行；
//    6) 程序名统一成 CopyMath：exe / ico / cfg 文件名与版本信息都用大写 C、M。
//  ---------------------------------------------------------------------------
// ============================================================================

// ---- 版本号 ---------------------------------------------------------------
//  同时体现在 exe 文件属性（app.rc 的 VERSIONINFO）与窗口标题上，两者请保持一致。
#define COPYMATH_VERSION L"1.2"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>     // SHBrowseForFolder（选择文件夹对话框）
#include <shlwapi.h>    // PathFileExists 等路径工具
#include <gdiplus.h>    // 图片显示与保存
#include <process.h>
#include <string>
#include <cwchar>
#include <cstdlib>
#include "config.h"
#include "render.h"
#include "clipboard.h"
#include "log.h"
#include "util.h"

// ---- 控件 / 消息 ID -------------------------------------------------------
// 主窗口控件
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

// 设置对话框内的控件 ID
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

// 设置窗口的客户区设计尺寸（96 DPI 下的逻辑像素，实际会按 DPI 缩放）。
// 布局在 WM_CREATE 里一次算完，窗口做成固定大小，所以这里只写一份即可。
static const int kSetClientW = 660;
static const int kSetClientH = 604;

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

// 日志写入的三种结果：没启用 / 写成功 / 写失败（失败时界面要提醒用户）
enum LogResult { LOG_OFF, LOG_OK, LOG_FAIL };

// ---- 小工具 ---------------------------------------------------------------

// 把“按 96 DPI 设计”的像素尺寸换算到当前 DPI。
// 开启 DPI 感知后系统不再帮我们缩放，所以所有界面尺寸都要自己乘这个系数，
// 否则在高分屏上控件会显得又窄又矮、文字被裁掉。
static int Px(int v) { return (int)(v * g_dpiScale + 0.5); }

// 取窗口文本（返回 std::wstring，省掉到处写“问长度 + 取内容”两行代码）
static std::wstring GetWindowTextStr(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

// 按按钮文字的实际宽度算出合适的按钮宽度（再留一点左右内边距）。
// 这样把字体调大后按钮也不会被截断。
static int FitButtonWidth(HWND hBtn) {
    HDC hdc = GetDC(hBtn);
    if (!hdc) return Px(120);
    HFONT oldf = (HFONT)SelectObject(hdc, g_hFontUI ? g_hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    wchar_t txt[256] = { 0 };
    GetWindowTextW(hBtn, txt, 256);
    RECT rc = { 0, 0, 0, 0 };
    DrawTextW(hdc, txt, -1, &rc, DT_CALCRECT | DT_SINGLELINE);   // 只量尺寸、不绘制
    SelectObject(hdc, oldf);
    ReleaseDC(hBtn, hdc);
    return (rc.right - rc.left) + Px(30);
}

// ---- 前向声明 -------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ViewProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI RenderThread(LPVOID);
void RequestRender();                  // 请求一次渲染（异步）
void ResetRenderState();               // 仅重置右侧：缓存 / 预览 / 错误
void ResetAll();                       // 左右一起重置（含清空输入框）
void RenderNow();                      // 先重置右侧，再渲染
void SubclassEditForSelectAll(HWND);   // 让编辑框支持 Ctrl+A 全选
void SetStatus(const std::wstring&);
std::wstring GetEditText();
void CopyViewToClipboard();
void SaveImage();
void OpenSettings();
bool GetEncoderClsid(const WCHAR*, CLSID*);
std::wstring Timestamp();
std::wstring AutoDetectTexlive();
std::wstring BrowseFolder(HWND, const std::wstring&);

// ---- 基础操作 -------------------------------------------------------------

// 读取左侧输入框的全部文本
std::wstring GetEditText() {
    return GetWindowTextStr(g_hEdit);
}

// 在底部状态栏显示一行提示
void SetStatus(const std::wstring& s) {
    if (g_hStatus) SetWindowTextW(g_hStatus, s.c_str());
}

// 当前配置里的 DPI（别处多次用到，抽成一个小函数）
static int CurrentDpi() {
    int d;
    EnterCriticalSection(&g_cs);
    d = g_cfg.dpi;
    LeaveCriticalSection(&g_cs);
    return (d >= kDpiMin && d <= kDpiMax) ? d : kDpiDefault;
}

// ---------------------------------------------------------------------------
// 记录一次“复制/保存”动作
//
//  只在设置里打开了“自动记录日志”时才真的写文件；写失败会返回 LOG_FAIL，
//  由调用方在状态栏提醒用户（不弹对话框，免得打断连续操作）。
// ---------------------------------------------------------------------------
static LogResult LogUsage(const wchar_t* action, const std::wstring& imagePath, int w, int h) {
    std::wstring path;
    bool enabled;
    int dpi;
    EnterCriticalSection(&g_cs);
    enabled = g_cfg.logEnabled;
    path = g_cfg.logPath;
    dpi = g_cfg.dpi;
    LeaveCriticalSection(&g_cs);

    if (!enabled) return LOG_OFF;

    std::wstring file = ResolveLogFilePath(path);
    if (file.empty()) return LOG_FAIL;
    return AppendUsageLog(file, action, GetEditText(), dpi, w, h, imagePath) ? LOG_OK : LOG_FAIL;
}

// 根据日志结果给状态栏补一句后缀
static std::wstring LogSuffix(LogResult r) {
    switch (r) {
        case LOG_OK:   return L"，已记入日志";
        case LOG_FAIL: return L"（提醒：日志写入失败，请检查“设置 → 日志文件路径”）";
        default:       return L"";
    }
}

// ---------------------------------------------------------------------------
// 请求渲染（异步）
//
// 只负责“登记一次请求 + 必要时启动线程”，真正的活儿在 RenderThread 里干。
// 机制说明：
//   · g_gen 是版本号，每次请求 +1；线程渲染完会比对版本号，若期间又有新请求
//     就丢弃本次结果、改渲染最新内容（保证最终显示的总是最新的输入）；
//   · g_rendering 保证同一时刻只有一个渲染线程，避免多个 pdflatex 抢同一个
//     临时文件。
// ---------------------------------------------------------------------------
void RequestRender() {
    std::wstring tex = GetEditText();
    bool start = false;
    EnterCriticalSection(&g_cs);
    g_gen++;
    g_currentTex = tex;
    if (!g_rendering) { g_rendering = true; start = true; }
    LeaveCriticalSection(&g_cs);
    if (start) CreateThread(NULL, 0, RenderThread, NULL, 0, NULL);
}

// ---------------------------------------------------------------------------
// 仅重置“右侧”：清渲染缓存、清预览图与错误状态。
// 注意：**不动左侧输入框**，避免用户辛苦写好的公式被清掉。
// ---------------------------------------------------------------------------
void ResetRenderState() {
    EnterCriticalSection(&g_cs);
    g_gen++;                 // 版本号 +1，让仍在进行的渲染作废
    LeaveCriticalSection(&g_cs);

    CleanRenderCache();      // 删除临时目录里的 formula.*

    if (g_viewBitmap) { delete g_viewBitmap; g_viewBitmap = NULL; }
    g_hasError = false;
    g_lastError.clear();

    InvalidateRect(g_hView, NULL, TRUE);
    SetStatus(L"已重置，按 F5（或点“渲染”）重新渲染");
}

// ---------------------------------------------------------------------------
// 渲染 = 先重置右侧，再请求渲染。
// 这样每次渲染都从干净状态开始，上一次的残留不会影响结果。
// ---------------------------------------------------------------------------
void RenderNow() {
    ResetRenderState();
    SetStatus(L"正在渲染…");
    RequestRender();
}

// ---------------------------------------------------------------------------
// 后台渲染线程
//
// 用循环而非单次执行，是为了处理“渲染途中用户又改了输入”的情况：
//   · 每轮开始时快照当前源码与版本号；
//   · 渲染完成后回到临界区比较版本号：
//       版本号变了 → 说明有更新，丢弃本次结果、继续下一轮；
//       版本号没变 → 本次结果就是最新的，发给主线程并结束。
//
// ⚠ 关键：判断“版本号是否变化”和“清除 g_rendering 忙标志”必须在**同一个
//   临界区**里完成。若分成两次进入临界区，请求有可能恰好插在中间 —— 它看到
//   忙标志为真就不开新线程，而这边随后又把忙标志清了，于是最新输入永远等不到
//   渲染，界面会一直停在旧结果上。
// ---------------------------------------------------------------------------
DWORD WINAPI RenderThread(LPVOID) {
    while (true) {
        int gen; std::wstring tex, pre, texbin; int dpi;
        EnterCriticalSection(&g_cs);
        gen = g_gen; tex = g_currentTex; pre = g_cfg.preamble;
        texbin = g_cfg.texlive; dpi = g_cfg.dpi;
        LeaveCriticalSection(&g_cs);

        std::wstring err;
        Gdiplus::Bitmap* bmp = RenderFormula(tex, pre, texbin, dpi, &err);

        bool newer;
        EnterCriticalSection(&g_cs);
        newer = (g_gen != gen);
        if (!newer) g_rendering = false;
        LeaveCriticalSection(&g_cs);

        if (newer) { if (bmp) delete bmp; continue; }   // 有更新，丢弃并重来

        if (bmp) {
            // 把位图指针交给主线程（主线程负责 delete）
            PostMessageW(g_hMain, WM_RENDER_DONE, 0, (LPARAM)bmp);
        } else {
            // 错误信息跨线程传递：堆上 new 一份，由主线程 delete
            std::wstring* pe = new std::wstring(err);
            PostMessageW(g_hMain, WM_RENDER_ERROR, 0, (LPARAM)pe);
        }
        return 0;
    }
}

// 把当前预览图复制到剪贴板（DPI 会一并写进 CF_DIB 头，见 clipboard.cpp）
void CopyViewToClipboard() {
    if (!g_viewBitmap) { SetStatus(L"尚无公式图片可复制"); return; }

    if (!CopyBitmapToClipboard(g_hView, g_viewBitmap, CurrentDpi())) {
        SetStatus(L"复制图片失败");
        return;
    }
    LogResult lr = LogUsage(L"复制图片", L"",
                            (int)g_viewBitmap->GetWidth(), (int)g_viewBitmap->GetHeight());
    SetStatus(L"已复制公式图片到剪贴板（" + std::to_wstring(CurrentDpi()) + L" DPI）" + LogSuffix(lr));
}

// 查询 GDI+ 图像编码器（保存 PNG 需要先拿到对应的 CLSID）
bool GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    Gdiplus::ImageCodecInfo* p = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!p) return false;
    Gdiplus::GetImageEncoders(num, size, p);
    bool found = false;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(p[i].MimeType, format) == 0) { *clsid = p[i].Clsid; found = true; break; }
    }
    free(p);
    return found;
}

// 生成形如 20260924_083012 的时间戳，用作保存文件名
std::wstring Timestamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d%02d%02d_%02d%02d%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

// ---------------------------------------------------------------------------
// 把当前公式图保存成 PNG：文件名用时间戳，落到设置里的导出目录。
//
// 保存前会用 SetResolution 把设置里的 DPI 写进位图对象 —— GDI+ 的 PNG 编码器
// 会据此写出 pHYs 块，这样图片的“物理尺寸”信息与出图分辨率一致
// （粘进 Word / 插进 PDF 时才不会忽大忽小）。
// ---------------------------------------------------------------------------
void SaveImage() {
    if (!g_viewBitmap) { SetStatus(L"尚无公式图片可保存"); return; }

    std::wstring exp;
    EnterCriticalSection(&g_cs); exp = g_cfg.exportPath; LeaveCriticalSection(&g_cs);
    if (exp.empty()) {
        SetStatus(L"请先在“设置”中指定导出图片路径");
        MessageBoxW(g_hMain, L"请先在“设置”中指定导出图片路径。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!EnsureDirectory(exp)) {          // 目录不存在则递归创建（支持多级路径）
        SetStatus(L"导出目录无法创建：" + exp);
        MessageBoxW(g_hMain, (L"无法创建导出目录：\n" + exp).c_str(), L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring name = L"formula_" + Timestamp() + L".png";
    std::wstring path = exp + L"\\" + name;

    CLSID png;
    if (!GetEncoderClsid(L"image/png", &png)) { SetStatus(L"无法获取 PNG 编码器"); return; }

    int dpi = CurrentDpi();
    g_viewBitmap->SetResolution((Gdiplus::REAL)dpi, (Gdiplus::REAL)dpi);
    Gdiplus::Status st = g_viewBitmap->Save(path.c_str(), &png, NULL);
    if (st == Gdiplus::Ok) {
        // 顺带报出像素尺寸与分辨率，便于确认图片已经贴紧公式（而不是带着一大片白边）
        std::wstring size = L"（" + std::to_wstring(g_viewBitmap->GetWidth())
                          + L" × " + std::to_wstring(g_viewBitmap->GetHeight())
                          + L" px, " + std::to_wstring(dpi) + L" DPI）";
        LogResult lr = LogUsage(L"保存图片", path,
                                (int)g_viewBitmap->GetWidth(), (int)g_viewBitmap->GetHeight());
        SetStatus(L"已保存：" + path + size + LogSuffix(lr));
    }
    else SetStatus(L"保存失败（错误码 " + std::to_wstring((long)st) + L"）");
}

// ---------------------------------------------------------------------------
// 自动探测 TeX Live 的 bin 目录：
//   1) 先在系统 PATH 里找 pdflatex.exe（适用于已把 TeX 加入 PATH 的情况）；
//   2) 再依次检查几个常见的安装位置。
// 找不到则返回空串，由用户在“设置”里手动指定。
// ---------------------------------------------------------------------------
std::wstring AutoDetectTexlive() {
    wchar_t buf[MAX_PATH];
    if (SearchPathW(NULL, L"pdflatex.exe", NULL, MAX_PATH, buf, NULL)) {
        std::wstring s(buf);
        size_t p = s.find_last_of(L"\\/");
        if (p != std::wstring::npos) return s.substr(0, p);
    }
    const wchar_t* tries[] = {
        L"C:\\texlive\\2024\\bin\\windows",
        L"C:\\texlive\\2023\\bin\\windows",
        L"C:\\texlive\\2022\\bin\\windows"
    };
    for (int i = 0; i < 3; i++) {
        if (PathFileExistsW((std::wstring(tries[i]) + L"\\pdflatex.exe").c_str())) return tries[i];
    }
    return L"";
}

// 弹出“选择文件夹”对话框，返回所选路径（取消则返回空串）
std::wstring BrowseFolder(HWND owner, const std::wstring& title) {
    BROWSEINFOW bi; ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (!pid) return L"";
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    SHGetPathFromIDListW(pid, buf);
    CoTaskMemFree(pid);                                  // SHBrowseForFolder 分配的内存要归还
    return std::wstring(buf);
}

// ---- 公式预览子窗口（右侧） ----------------------------------------------
// 自绘窗口：白底，居中显示公式图；出错时用红字显示 LaTeX 报文。
LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));   // 先用白底擦干净

            if (g_hasError) {
                // 渲染失败：把报错直接画在预览区（状态栏只有一行，容易被忽略）
                SetTextColor(hdc, RGB(200, 40, 40));
                SetBkMode(hdc, TRANSPARENT);
                RECT tr = rc;
                tr.left += 12; tr.right -= 12; tr.top += 12; tr.bottom -= 12;
                HFONT old = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
                DrawTextW(hdc, L"渲染失败：", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
                tr.top += 20;
                DrawTextW(hdc, g_lastError.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_WORDBREAK);
                SelectObject(hdc, old);
            } else if (g_viewBitmap && g_viewBitmap->GetLastStatus() == Gdiplus::Ok) {
                // 等比缩放，让公式完整放进窗口并居中；上限 1.0 —— 只缩小不放大，
                // 放大反而会让高 DPI 的图变糊。
                int bw = g_viewBitmap->GetWidth(), bh = g_viewBitmap->GetHeight();
                int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
                float scale = (float)cw / bw;
                float sy = (float)ch / bh;
                float s = (scale < sy) ? scale : sy;
                if (s > 1.0f) s = 1.0f;
                int dw = (int)(bw * s), dh = (int)(bh * s);
                int dx = (cw - dw) / 2, dy = (ch - dh) / 2;

                Gdiplus::Graphics g(hdc);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);  // 高质量缩放
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.DrawImage(g_viewBitmap, rc.left + dx, rc.top + dy, dw, dh);
            } else {
                // 还没有图：显示操作提示
                SetTextColor(hdc, RGB(120, 120, 120));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, L"在左侧输入 LaTeX 源码，按 F5 渲染（Ctrl+D 复制图片 / Ctrl+S 保存图片）", -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            // 预览区获得焦点时补一手（平时焦点在输入框，那里由加速键表统一处理）：
            // Ctrl+C 复制图片、Ctrl+S 保存图片。
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (w == 'C') { CopyViewToClipboard(); return 0; }
                if (w == 'S') { SaveImage();            return 0; }
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// ---- 设置对话框 -----------------------------------------------------------
// 六项设置，从上往下：
//   1. TeX Live 的 bin 目录
//   2. 导出图片目录
//   3. 输出分辨率（DPI）            ← v1.2 新增
//   4. 自动记录日志 + 日志文件路径   ← v1.2 新增
//   5. 导言区（多行）
// 所有控件尺寸都用 Px() 按 DPI 缩放，且用“从上往下累加 y”的方式布局，
// 以后插入新控件时不用手工重排后面所有坐标。
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            int M = Px(10);                 // 对话框边距
            int lblH = Px(22);              // 标签行高（字体放大后要跟着加高，否则被裁）
            int edH = Px(28);               // 单行输入框高
            int gap = Px(8);                // 标签与输入框之间的间距
            int blockGap = Px(16);          // 各组控件之间的间距
            int btnW = Px(90), btnH = Px(30);
            int dlgW = Px(kSetClientW);     // 内容区宽度基准
            int dpiEdW = Px(90);            // DPI 输入框宽度

            int y = M;
            int editW = dlgW - M * 2 - btnW - gap;

            // ---- 第 1 组：TeX Live bin 目录 ----
            CreateWindowExW(0, L"STATIC",
                L"TeX Live 的 bin 目录（包含 pdflatex.exe 的文件夹）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_TEX, g_hInst, NULL);
            y += lblH + gap;
            g_hSetTex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, M, y, editW, edH,
                hwnd, (HMENU)(IDS_TEX + 100), g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE,
                M + editW + gap, y, btnW, btnH, hwnd, (HMENU)IDS_BROWSE_TEX, g_hInst, NULL);
            y += edH + blockGap;

            // ---- 第 2 组：导出图片目录 ----
            CreateWindowExW(0, L"STATIC",
                L"导出图片路径（点“保存图片”会写到此文件夹）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_EXP, g_hInst, NULL);
            y += lblH + gap;
            g_hSetExp = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, M, y, editW, edH,
                hwnd, (HMENU)(IDS_EXP + 100), g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE,
                M + editW + gap, y, btnW, btnH, hwnd, (HMENU)IDS_BROWSE_EXP, g_hInst, NULL);
            y += edH + blockGap;

            // ---- 第 3 组：输出分辨率（v1.2 新增）----
            CreateWindowExW(0, L"STATIC",
                L"输出分辨率（DPI，复制与保存共用）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_DPI, g_hInst, NULL);
            y += lblH + gap;
            g_hSetDpi = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | ES_RIGHT,
                M, y, dpiEdW, edH, hwnd, (HMENU)(IDS_DPI + 100), g_hInst, NULL);
            CreateWindowExW(0, L"STATIC",
                L"常用 300；调大更清晰、文件也更大（可填 72～1200）",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M + dpiEdW + gap, y + Px(5),
                dlgW - M * 2 - dpiEdW - gap, lblH, hwnd, (HMENU)IDS_DPI_HINT, g_hInst, NULL);
            y += edH + blockGap;

            // ---- 第 4 组：使用日志（v1.2 新增）----
            g_hSetLogChk = CreateWindowExW(0, L"BUTTON",
                L"自动记录日志：每次复制 / 保存公式后，把源码与时间追加到下面的文件",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, M, y, dlgW - M * 2, Px(24),
                hwnd, (HMENU)IDS_LOG_CHK, g_hInst, NULL);
            y += Px(24) + gap;
            CreateWindowExW(0, L"STATIC",
                L"日志文件路径（可只填文件夹，程序会在里面用 logs.txt）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_LOG, g_hInst, NULL);
            y += lblH + gap;
            g_hSetLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, M, y, editW, edH,
                hwnd, (HMENU)(IDS_LOG + 100), g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE,
                M + editW + gap, y, btnW, btnH, hwnd, (HMENU)IDS_BROWSE_LOG, g_hInst, NULL);
            y += edH + blockGap;

            // ---- 第 5 组：导言区（多行）----
            CreateWindowExW(0, L"STATIC",
                L"导言区（\\usepackage 与自定义命令，可留空）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_PRE, g_hInst, NULL);
            y += lblH + gap;
            int preH = Px(170);
            g_hSetPre = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                M, y, dlgW - M * 2, preH, hwnd, (HMENU)(IDS_PRE + 100), g_hInst, NULL);
            y += preH + blockGap;

            // ---- 底部按钮 ----
            int okW = Px(120), okH = Px(34);
            CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                dlgW / 2 - okW - gap, y, okW, okH, hwnd, (HMENU)IDS_SAVE_BTN, g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                dlgW / 2 + gap, y, okW, okH, hwnd, (HMENU)IDS_CANCEL, g_hInst, NULL);

            // ---- 统一套用字体（输入框用编辑字体，其余用界面字体）----
            SendMessageW(g_hSetTex, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SendMessageW(g_hSetExp, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SendMessageW(g_hSetPre, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SendMessageW(g_hSetDpi, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SendMessageW(g_hSetLog, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SubclassEditForSelectAll(g_hSetTex);     // 让这几个编辑框也支持 Ctrl+A
            SubclassEditForSelectAll(g_hSetExp);
            SubclassEditForSelectAll(g_hSetPre);
            SubclassEditForSelectAll(g_hSetDpi);
            SubclassEditForSelectAll(g_hSetLog);
            HWND kids[] = { GetDlgItem(hwnd, IDS_TEX), GetDlgItem(hwnd, IDS_EXP), GetDlgItem(hwnd, IDS_PRE),
                            GetDlgItem(hwnd, IDS_DPI), GetDlgItem(hwnd, IDS_DPI_HINT),
                            GetDlgItem(hwnd, IDS_LOG), GetDlgItem(hwnd, IDS_LOG_CHK),
                            GetDlgItem(hwnd, IDS_BROWSE_TEX), GetDlgItem(hwnd, IDS_BROWSE_EXP),
                            GetDlgItem(hwnd, IDS_BROWSE_LOG),
                            GetDlgItem(hwnd, IDS_SAVE_BTN), GetDlgItem(hwnd, IDS_CANCEL) };
            for (HWND c : kids) if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);        // STATIC 标签要重绘才会用上新字体

            // ---- 填入当前配置 ----
            EnterCriticalSection(&g_cs);
            SetWindowTextW(g_hSetTex, g_cfg.texlive.c_str());
            SetWindowTextW(g_hSetExp, g_cfg.exportPath.c_str());
            SetWindowTextW(g_hSetPre, g_cfg.preamble.c_str());
            SetWindowTextW(g_hSetDpi, std::to_wstring(g_cfg.dpi).c_str());
            SetWindowTextW(g_hSetLog, g_cfg.logPath.c_str());
            SendMessageW(g_hSetLogChk, BM_SETCHECK, g_cfg.logEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
            LeaveCriticalSection(&g_cs);

            // 没开日志时，路径那一行先灰掉，减少干扰
            EnableWindow(g_hSetLog, SendMessageW(g_hSetLogChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(GetDlgItem(hwnd, IDS_BROWSE_LOG), IsWindowEnabled(g_hSetLog));

            SetFocus(g_hSetTex);                     // 打开就把焦点放在第一项上
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == IDS_BROWSE_TEX) {
                std::wstring p = BrowseFolder(hwnd, L"选择 TeX Live 的 bin 目录");
                if (!p.empty()) SetWindowTextW(g_hSetTex, p.c_str());
            } else if (id == IDS_BROWSE_EXP) {
                std::wstring p = BrowseFolder(hwnd, L"选择导出图片文件夹");
                if (!p.empty()) SetWindowTextW(g_hSetExp, p.c_str());
            } else if (id == IDS_BROWSE_LOG) {
                // 这里选的是“文件夹”；日志文件名固定用 logs.txt（也可手填完整文件名）
                std::wstring p = BrowseFolder(hwnd, L"选择日志文件所在文件夹（文件名用 logs.txt）");
                if (!p.empty()) SetWindowTextW(g_hSetLog, (p + L"\\logs.txt").c_str());
            } else if (id == IDS_LOG_CHK) {
                // 勾选/取消勾选：同步启用或灰掉路径输入框与浏览按钮
                bool on = SendMessageW(g_hSetLogChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(g_hSetLog, on);
                EnableWindow(GetDlgItem(hwnd, IDS_BROWSE_LOG), on);
                if (on) SetFocus(g_hSetLog);
            } else if (id == IDS_CANCEL) {
                DestroyWindow(hwnd);                        // 不保存，直接关掉
            } else if (id == IDS_SAVE_BTN) {
                // ---- 读回各输入框的内容 ----
                std::wstring t1 = GetWindowTextStr(g_hSetTex);
                std::wstring t2 = GetWindowTextStr(g_hSetExp);
                std::wstring t3 = GetWindowTextStr(g_hSetPre);
                std::wstring t4 = GetWindowTextStr(g_hSetDpi);
                std::wstring t5 = GetWindowTextStr(g_hSetLog);
                bool logOn = SendMessageW(g_hSetLogChk, BM_GETCHECK, 0, 0) == BST_CHECKED;

                // ---- 校验分辨率：必须是 72～1200 的整数 ----
                int dpi = _wtoi(t4.c_str());
                if (dpi < kDpiMin || dpi > kDpiMax) {
                    MessageBoxW(hwnd,
                        L"输出分辨率请填 72～1200 之间的整数（常用 300）。",
                        L"设置", MB_OK | MB_ICONWARNING);
                    SetFocus(g_hSetDpi);
                    SendMessageW(g_hSetDpi, EM_SETSEL, 0, -1);
                    return 0;                               // 不关闭对话框，让用户改
                }
                // ---- 校验日志：开了开关就必须给个位置 ----
                if (logOn && t5.empty()) {
                    MessageBoxW(hwnd,
                        L"已勾选“自动记录日志”，请填写日志文件路径（或先取消勾选）。",
                        L"设置", MB_OK | MB_ICONWARNING);
                    SetFocus(g_hSetLog);
                    return 0;
                }

                EnterCriticalSection(&g_cs);
                g_cfg.texlive = t1; g_cfg.exportPath = t2; g_cfg.preamble = t3;
                g_cfg.dpi = dpi; g_cfg.logPath = t5; g_cfg.logEnabled = logOn;
                LeaveCriticalSection(&g_cs);

                // ---- 顺手检查一下 TeX Live 路径，给个即时反馈（不阻止保存）----
                std::wstring pdflatex = t1 + L"\\pdflatex.exe";
                if (t1.empty() || !PathFileExistsW(pdflatex.c_str())) {
                    MessageBoxW(hwnd,
                        L"提醒：在这个目录里没找到 pdflatex.exe，渲染可能失败。\n"
                        L"请确认填的是“包含 pdflatex.exe 的那个文件夹”。\n\n"
                        L"设置已经保存，可以稍后再改。",
                        L"设置", MB_OK | MB_ICONINFORMATION);
                }

                SaveConfig(g_cfg);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // 关闭对话框：恢复主窗口可交互，并重新渲染（导言区/路径/DPI 可能改了）
            g_hSettings = NULL;
            EnableWindow(g_hMain, TRUE);
            SetForegroundWindow(g_hMain);
            RenderNow();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// 打开（或聚焦）设置对话框
void OpenSettings() {
    if (g_hSettings) { SetForegroundWindow(g_hSettings); return; }   // 已打开就不重复创建
    EnableWindow(g_hMain, FALSE);                                    // 简化处理：模态效果

    // 固定尺寸（布局在 WM_CREATE 里一次性算好，不接受拉伸），
    // 用 AdjustWindowRectEx 把“客户区尺寸”换算成“整窗尺寸” ——
    // 否则边框和标题栏会吃掉一部分，最右边的控件会被切掉。
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT rc = { 0, 0, Px(kSetClientW), Px(kSetClientH) };
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);

    g_hSettings = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CopyMathSettings", L"设置",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, g_hInst, NULL);
    ShowWindow(g_hSettings, SW_SHOW);
    UpdateWindow(g_hSettings);
}

// ---- 编辑框子类化：补上 Ctrl+A --------------------------------------------
// Windows 的多行 EDIT 控件默认**不响应 Ctrl+A**，所以这里把窗口过程替换成
// EditProc，在里面自己处理 Ctrl+A（EM_SETSEL 0,-1 = 全选）。
// 原来的窗口过程存在窗口的 GWLP_USERDATA 里，因此同一套代码可以套在任意多个
// 编辑框上（主输入框 + 设置里的 5 个）。
void SubclassEditForSelectAll(HWND hEdit) {
    if (!hEdit) return;
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetWindowLongPtrW(hEdit, GWLP_USERDATA, (LONG_PTR)old);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);   // 全选：从 0 到末尾
        return 0;
    }
    WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return CallWindowProcW(old, hwnd, msg, w, l);   // 其余消息交还原处理
}

// 全部重置：右侧（缓存/预览/错误）+ 左侧（清空输入框并聚焦）
void ResetAll() {
    ResetRenderState();
    if (g_hEdit) SetWindowTextW(g_hEdit, L"");
    if (g_hEdit) SetFocus(g_hEdit);
    SetStatus(L"已全部重置：输入框与预览均已清空，输入公式后按 F5 渲染");
}

// 应用“总是置顶”状态（同时把状态记进配置，下次启动照旧）
static void ApplyTopmost(bool on) {
    SetWindowPos(g_hMain, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EnterCriticalSection(&g_cs);
    g_cfg.alwaysOnTop = on;
    LeaveCriticalSection(&g_cs);
    SaveConfig(g_cfg);
}

// ---- 主窗口 ---------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            g_hMain = hwnd;

            // 左侧：多行文本框（自动换行由 Edit 控件自带，带垂直滚动条）
            g_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                0, 0, 100, 100, hwnd, (HMENU)IDC_EDIT, g_hInst, NULL);

            // 右侧：自定义类的预览窗口（见 ViewProc）
            g_hView = CreateWindowExW(0, L"FormulaView", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 0, 100, 100, hwnd, (HMENU)IDC_VIEW, g_hInst, NULL);

            // 底部按钮（宽高和位置在 WM_SIZE 里统一算）
            g_hBtnRender = CreateWindowExW(0, L"BUTTON", L"渲染 (F5)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                0, 0, 140, 32, hwnd, (HMENU)IDC_RENDER, g_hInst, NULL);
            g_hBtnReset = CreateWindowExW(0, L"BUTTON", L"重置 (F6)", WS_CHILD | WS_VISIBLE,
                0, 0, 100, 32, hwnd, (HMENU)IDC_RESET, g_hInst, NULL);
            g_hBtnCopy = CreateWindowExW(0, L"BUTTON", L"复制图片 (Ctrl+D)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 160, 32, hwnd, (HMENU)IDC_COPY, g_hInst, NULL);
            g_hBtnSave = CreateWindowExW(0, L"BUTTON", L"保存图片 (Ctrl+S)", WS_CHILD | WS_VISIBLE,
                0, 0, 120, 32, hwnd, (HMENU)IDC_SAVE, g_hInst, NULL);
            // “总是置顶”：BS_AUTOCHECKBOX | BS_PUSHLIKE —— 点一下锁定/解除，
            // 按下时按钮呈“凹下去”的状态，比小方框更显眼、也更像开关。
            g_hBtnTop = CreateWindowExW(0, L"BUTTON", L"总是置顶", WS_CHILD | WS_VISIBLE |
                BS_AUTOCHECKBOX | BS_PUSHLIKE, 0, 0, 120, 32, hwnd, (HMENU)IDC_TOPMOST, g_hInst, NULL);
            g_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"设置", WS_CHILD | WS_VISIBLE,
                0, 0, 120, 32, hwnd, (HMENU)IDC_SETTINGS, g_hInst, NULL);
            g_hStatus = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 100, 24, hwnd, (HMENU)IDC_STATUS, g_hInst, NULL);

            // 字体：微软雅黑，字号按 DPI 缩放（g_dpiScale 已在 WinMain 算好）
            if (!g_hFontUI) {
                int uiSize = (int)(18 * g_dpiScale + 0.5);
                int edSize = (int)(20 * g_dpiScale + 0.5);
                auto mkFont = [](int px) {
                    return CreateFontW(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                };
                g_hFontUI = mkFont(uiSize);
                g_hFontEdit = mkFont(edSize);
            }
            SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hFontEdit, TRUE);
            SubclassEditForSelectAll(g_hEdit);       // 输入框支持 Ctrl+A
            for (HWND c : { g_hView, g_hBtnRender, g_hBtnReset, g_hBtnCopy, g_hBtnSave,
                            g_hBtnTop, g_hBtnSettings, g_hStatus })
                SendMessageW(c, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

            // 按配置恢复“总是置顶”的按钮状态（真正的置顶在 WinMain 里应用）
            if (g_cfg.alwaysOnTop)
                SendMessageW(g_hBtnTop, BM_SETCHECK, BST_CHECKED, 0);

            // 放一个示例公式并先渲染一次，让用户一打开就看到效果
            SetWindowTextW(g_hEdit, L"\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}");
            RequestRender();
            return 0;
        }

        case WM_SIZE: {
            // 手动布局：上半部分左右各占一半，底部一行按钮 + 状态栏。
            RECT rc; GetClientRect(hwnd, &rc);
            int W = rc.right, H = rc.bottom;
            int pad = Px(10), btnH = Px(36), statusH = Px(28);
            int topY = Px(10);

            // 按钮：先量出各自宽度，再决定要排几行（窗口被拖窄时自动换行，
            // 而不是把按钮挤出窗口外）。
            HWND btns[] = { g_hBtnRender, g_hBtnReset, g_hBtnCopy,
                            g_hBtnSave, g_hBtnTop, g_hBtnSettings };
            const int nBtn = 6;
            int bw[nBtn];
            for (int i = 0; i < nBtn; i++) bw[i] = FitButtonWidth(btns[i]);

            int availW = W - 2 * pad;
            if (availW < Px(80)) availW = Px(80);

            int by = 0, rows = 1;
            auto packButtons = [&](bool apply) -> int {
                int x = 0, y = 0, r = 1;
                for (int i = 0; i < nBtn; i++) {
                    if (x > 0 && x + bw[i] > availW) { x = 0; y += btnH + pad; r++; }
                    if (apply) SetWindowPos(btns[i], NULL, pad + x, by + y, bw[i], btnH, SWP_NOZORDER);
                    x += bw[i] + pad;
                }
                return r;
            };
            rows = packButtons(false);                        // 第一遍：只数行数
            int btnArea = rows * btnH + (rows - 1) * pad;

            int viewH = H - topY - btnArea - statusH - pad * 3;   // 上方区域剩余高度
            if (viewH < Px(60)) viewH = Px(60);

            int leftW = W / 2 - pad / 2;
            int rightX = pad + leftW + pad;
            int rightW = W - rightX - pad;
            if (rightW < Px(60)) rightW = Px(60);

            SetWindowPos(g_hEdit, NULL, pad, topY, leftW, viewH, SWP_NOZORDER);
            SetWindowPos(g_hView, NULL, rightX, topY, rightW, viewH, SWP_NOZORDER);

            by = topY + viewH + pad;                          // 按钮区起始 y
            packButtons(true);                                // 第二遍：真正摆放

            SetWindowPos(g_hStatus, NULL, pad, by + btnArea + pad, W - 2 * pad, statusH, SWP_NOZORDER);
            InvalidateRect(g_hView, NULL, TRUE);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == IDC_RENDER || id == IDM_RENDER_F5) {
                RenderNow();          // 渲染 = 先重置右侧，再渲染
            } else if (id == IDC_RESET) {
                ResetAll();           // 重置 = 左右都重置
            } else if (id == IDC_COPY) {
                CopyViewToClipboard();
            } else if (id == IDC_SAVE) {
                SaveImage();
            } else if (id == IDC_TOPMOST) {
                // BS_AUTOCHECKBOX 已经自己翻转了勾选状态，这里读回来应用即可
                bool on = SendMessageW(g_hBtnTop, BM_GETCHECK, 0, 0) == BST_CHECKED;
                ApplyTopmost(on);
                SetStatus(on ? L"窗口已置顶（始终显示在最前面）"
                             : L"已取消置顶");
            } else if (id == IDC_SETTINGS) {
                OpenSettings();
            }
            return 0;
        }

        // 后台线程渲染成功：换上新的位图
        case WM_RENDER_DONE: {
            Gdiplus::Bitmap* bmp = (Gdiplus::Bitmap*)l;
            if (g_viewBitmap) delete g_viewBitmap;
            g_viewBitmap = bmp;
            g_hasError = false;
            g_lastError.clear();
            InvalidateRect(g_hView, NULL, TRUE);
            SetStatus(L"渲染成功（" + std::to_wstring(g_viewBitmap->GetWidth()) + L" × "
                      + std::to_wstring(g_viewBitmap->GetHeight()) + L" px, "
                      + std::to_wstring(CurrentDpi()) + L" DPI）");
            return 0;
        }

        // 后台线程渲染失败：把错误显示出来
        case WM_RENDER_ERROR: {
            std::wstring* err = (std::wstring*)l;
            if (err->empty()) {
                // 空输入：清空预览、回到占位提示，不当作错误
                if (g_viewBitmap) { delete g_viewBitmap; g_viewBitmap = NULL; }
                g_hasError = false;
                g_lastError.clear();
                SetStatus(L"请输入 LaTeX 公式");
            } else {
                g_lastError = *err;
                g_hasError = true;
                SetStatus(*err);
            }
            delete err;
            InvalidateRect(g_hView, NULL, TRUE);
            return 0;
        }

        case WM_DESTROY:
            if (g_viewBitmap) delete g_viewBitmap;
            if (g_hFontUI) { DeleteObject(g_hFontUI); g_hFontUI = NULL; }
            if (g_hFontEdit) { DeleteObject(g_hFontEdit); g_hFontEdit = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// ---- 程序入口 -------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInst;

    // ---- 声明 DPI 感知 ----
    // 不声明的话，在高 DPI 屏幕上 Windows 会把整个窗口当位图拉伸，界面会发虚。
    // 优先用 Per-Monitor V2（Win10 1607+），失败则退回系统级 DPI 感知。
    {
        HMODULE hU = GetModuleHandleW(L"user32.dll");
        typedef BOOL (WINAPI *PFN_SPDAC)(int);
        PFN_SPDAC pfn = hU ? (PFN_SPDAC)GetProcAddress(hU, "SetProcessDpiAwarenessContext") : NULL;
        BOOL ok = FALSE;
        if (pfn) ok = pfn(-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
        if (!ok) {
            typedef BOOL (WINAPI *PFN_SPA)(void);
            PFN_SPA pOld = hU ? (PFN_SPA)GetProcAddress(hU, "SetProcessDPIAware") : NULL;
            if (pOld) pOld();
        }
    }

    // ---- 计算 DPI 缩放系数 ----
    // 开了 DPI 感知后系统不再自动缩放，所以字号和控件尺寸都要自己乘这个系数。
    {
        int dpiY = 96;
        HDC hdc0 = GetDC(NULL);
        if (hdc0) { dpiY = GetDeviceCaps(hdc0, LOGPIXELSY); ReleaseDC(NULL, hdc0); }
        if (dpiY < 96) dpiY = 96;
        g_dpiScale = (double)dpiY / 96.0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   // SHBrowseForFolder 需要 COM
    InitializeCriticalSection(&g_cs);

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gsi, NULL);

    // ---- 载入配置 ----
    // 先摆一份“出厂默认”，LoadConfig 只覆盖文件里出现过的字段，
    // 这样老配置文件缺的新键（v1.2 的 DPI / 日志 / 置顶）也能拿到合理默认值。
    {
        Config def;                                  // 字段默认值见 config.h 的结构体定义
        def.preamble = kDefaultPreamble;
        g_cfg = def;

        if (!LoadConfig(g_cfg)) {
            // 首次运行：探测 TeX Live 并写一份默认配置
            g_cfg.texlive = AutoDetectTexlive();
            SaveConfig(g_cfg);
        } else {
            bool needSave = false;
            if (g_cfg.texlive.empty())      { g_cfg.texlive = AutoDetectTexlive(); needSave = true; }
            if (g_cfg.preamble.empty())     { g_cfg.preamble = kDefaultPreamble;   needSave = true; }
            if (g_cfg.dpi < kDpiMin || g_cfg.dpi > kDpiMax) { g_cfg.dpi = kDpiDefault; needSave = true; }
            if (needSave) SaveConfig(g_cfg);
        }
    }

    // ---- 载入嵌进 exe 的图标（资源 ID 1，来自 app.rc / CopyMath.ico）----
    g_hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    g_hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    // ---- 注册三个窗口类 ----
    WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = g_hIconBig;          // 窗口类图标 → 标题栏与任务栏都用它
    wc.hIconSm = g_hIconSmall;
    wc.lpszClassName = L"CopyMathMain";
    RegisterClassExW(&wc);

    WNDCLASSEXW wcv = { 0 }; wcv.cbSize = sizeof(wcv);
    wcv.style = CS_HREDRAW | CS_VREDRAW;
    wcv.lpfnWndProc = ViewProc;
    wcv.hInstance = hInst;
    wcv.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcv.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcv.lpszClassName = L"FormulaView";
    RegisterClassExW(&wcv);

    WNDCLASSEXW wcs = { 0 }; wcs.cbSize = sizeof(wcs);
    wcs.style = CS_HREDRAW | CS_VREDRAW;
    wcs.lpfnWndProc = SettingsProc;
    wcs.hInstance = hInst;
    wcs.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcs.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcs.hIcon = g_hIconBig;
    wcs.hIconSm = g_hIconSmall;
    wcs.lpszClassName = L"CopyMathSettings";
    RegisterClassExW(&wcs);

    // ---- 创建主窗口 ----
    // 公式多是横向排版，所以默认窗口做得“宽而矮”。尺寸也按 DPI 缩放，
    // 否则高 DPI 下按钮（宽度按文字自适应）会撑出窗口。
    // v1.2 把默认尺寸从 1000×400 收缩到 840×320：按钮行实测只需 752 逻辑像素，
    // 留出的余量足够；窗口再被拖窄时按钮行还会自动换行，不会挤丢。
    HWND hwnd = CreateWindowExW(0, L"CopyMathMain",
        L"CopyMath " COPYMATH_VERSION L" - LaTeX 公式渲染（F5 渲染 / F6 重置 / Ctrl+D 复制 / Ctrl+S 保存）",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Px(840), Px(320),
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 恢复“总是置顶”：窗口显示之后再设，避免 ShowWindow 把它顶掉
    if (g_cfg.alwaysOnTop) ApplyTopmost(true);

    // ---- 加速键表 ----
    // 用加速键（而不是在各窗口里处理按键）的好处：**与焦点无关**。
    // 光标停在输入框里时按键会被 EDIT 控件吃掉，而加速键是消息循环里
    // TranslateAccelerator 处理的，发生在消息派发之前，所以照样生效。
    //
    //   F5        → 渲染
    //   F6        → 重置（含清空输入）
    //   Ctrl+D    → 复制图片（Ctrl+C 会被输入框截走用于复制文本，故改用 Ctrl+D）
    //   F7        → 复制图片（备用键）
    //   Ctrl+S    → 保存图片（同样是输入框不占用的组合键）
    ACCEL acc[5] = {
        { FVIRTKEY,            VK_F5,  (WORD)IDM_RENDER_F5 },
        { FVIRTKEY,            VK_F6,  (WORD)IDC_RESET     },
        { FCONTROL | FVIRTKEY, 'D',    (WORD)IDC_COPY      },
        { FVIRTKEY,            VK_F7,  (WORD)IDC_COPY      },
        { FCONTROL | FVIRTKEY, 'S',    (WORD)IDC_SAVE      }
    };
    g_hAccel = CreateAcceleratorTableW(acc, 5);

    // ---- 消息循环 ----
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (g_hAccel && TranslateAcceleratorW(g_hMain, g_hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_hAccel) { DestroyAcceleratorTable(g_hAccel); g_hAccel = NULL; }

    // ---- 收尾清理 ----
    Gdiplus::GdiplusShutdown(g_gdiToken);
    DeleteCriticalSection(&g_cs);
    CoUninitialize();
    return (int)msg.wParam;
}
