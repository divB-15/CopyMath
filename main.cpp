// ============================================================================
//  CopyMath — main.cpp
//
//  程序主入口与界面层。整个程序分成 5 个模块：
//
//      main.cpp      界面、窗口过程、渲染调度线程（本文件）
//      render.cpp    调 TeX Live 把 LaTeX 渲染成位图
//      config.cpp    读写 copymath.cfg 配置
//      clipboard.cpp 把图片以 CF_DIB 写入剪贴板
//      util.cpp      UTF-16 <-> UTF-8 转换
//
//  技术要点：
//    · 纯 Win32 API + GDI+，不使用 Qt 等任何第三方 GUI 库；
//    · 公式排版外包给本机 TeX Live（pdflatex + pdftocairo），保真度 100%；
//    · 渲染放在后台线程，界面不卡；
//    · 手动渲染（F5 / 按钮），不随输入实时触发。
//
//  快捷键：F5 渲染　F6 重置（含清空输入）　Ctrl+D / F7 复制图片　Ctrl+A 全选
// ============================================================================

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

// ---- 全局状态 -------------------------------------------------------------
HINSTANCE g_hInst = NULL;

// 主窗口及其控件
HWND g_hMain = NULL, g_hEdit = NULL, g_hView = NULL;
HWND g_hBtnRender = NULL, g_hBtnReset = NULL, g_hBtnCopy = NULL,
     g_hBtnSave = NULL, g_hBtnSettings = NULL, g_hStatus = NULL;

// 设置对话框及其控件
HWND g_hSettings = NULL, g_hSetTex = NULL, g_hSetExp = NULL, g_hSetPre = NULL;

HACCEL g_hAccel = NULL;        // 加速键表（F5 / F6 / Ctrl+D / F7）
HFONT  g_hFontUI = NULL;       // 界面字体（微软雅黑，按 DPI 缩放）
HFONT  g_hFontEdit = NULL;     // 输入框字体（略大，便于阅读代码）
double g_dpiScale = 1.0;       // DPI 缩放系数 = LOGPIXELSY / 96
HICON  g_hIconBig = NULL;      // 程序图标（标题栏 / 任务栏）
HICON  g_hIconSmall = NULL;

// 渲染结果与状态
Gdiplus::Bitmap* g_viewBitmap = NULL;   // 当前显示的公式图（由主线程持有并负责 delete）
std::wstring g_lastError;               // 最近一次渲染错误（为空表示无错误）
bool g_hasError = false;

Config g_cfg;                  // 全局配置（TeX Live 路径 / 导出目录 / 导言区）

// 渲染线程与主线程的共享状态（用 g_cs 临界区保护）
CRITICAL_SECTION g_cs;
std::wstring g_currentTex;     // 最近一次请求渲染的源码
int g_gen = 0;                 // “版本号”：每次请求 +1，用于丢弃过期的渲染结果
bool g_rendering = false;      // 是否已有渲染线程在跑（保证同一时刻只有一个）

ULONG_PTR g_gdiToken = 0;      // GdiplusStartup 返回的令牌，退出时要交给 GdiplusShutdown

// ---- 小工具 ---------------------------------------------------------------

// 把“按 96 DPI 设计”的像素尺寸换算到当前 DPI。
// 开启 DPI 感知后系统不再帮我们缩放，所以所有界面尺寸都要自己乘这个系数，
// 否则在高分屏上控件会显得又窄又矮、文字被裁掉。
static int Px(int v) { return (int)(v * g_dpiScale + 0.5); }

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
    int n = GetWindowTextLengthW(g_hEdit);
    if (n <= 0) return L"";
    std::wstring s(n + 1, 0);
    GetWindowTextW(g_hEdit, &s[0], n + 1);
    s.resize(n);
    return s;
}

// 在底部状态栏显示一行提示
void SetStatus(const std::wstring& s) {
    if (g_hStatus) SetWindowTextW(g_hStatus, s.c_str());
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
        int gen; std::wstring tex, pre, texbin;
        EnterCriticalSection(&g_cs);
        gen = g_gen; tex = g_currentTex; pre = g_cfg.preamble; texbin = g_cfg.texlive;
        LeaveCriticalSection(&g_cs);

        std::wstring err;
        Gdiplus::Bitmap* bmp = RenderFormula(tex, pre, texbin, &err);

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

// 把当前预览图复制到剪贴板
void CopyViewToClipboard() {
    if (!g_viewBitmap) { SetStatus(L"尚无公式图片可复制"); return; }
    if (CopyBitmapToClipboard(g_hView, g_viewBitmap)) SetStatus(L"已复制公式图片到剪贴板");
    else SetStatus(L"复制图片失败");
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

// 把当前公式图保存成 PNG：文件名用时间戳，落到设置里的导出目录
void SaveImage() {
    if (!g_viewBitmap) { SetStatus(L"尚无公式图片可保存"); return; }

    std::wstring exp;
    EnterCriticalSection(&g_cs); exp = g_cfg.exportPath; LeaveCriticalSection(&g_cs);
    if (exp.empty()) {
        SetStatus(L"请先在“设置”中指定导出图片路径");
        MessageBoxW(g_hMain, L"请先在“设置”中指定导出图片路径。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    CreateDirectoryW(exp.c_str(), NULL);                 // 目录不存在则创建
    std::wstring name = L"formula_" + Timestamp() + L".png";
    std::wstring path = exp + L"\\" + name;

    CLSID png;
    if (!GetEncoderClsid(L"image/png", &png)) { SetStatus(L"无法获取 PNG 编码器"); return; }
    Gdiplus::Status st = g_viewBitmap->Save(path.c_str(), &png, NULL);
    if (st == Gdiplus::Ok) SetStatus(L"已保存：" + path);
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
                // 放大反而会让 300 DPI 的图变糊。
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
                DrawTextW(hdc, L"在左侧输入 LaTeX 源码，按 F5 渲染（Ctrl+D 复制公式图片）", -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            // 预览区获得焦点时，Ctrl+C 也能复制图片（平时焦点在输入框，用 Ctrl+D）
            if (w == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                CopyViewToClipboard();
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// ---- 设置对话框 -----------------------------------------------------------
// 三项设置：TeX Live bin 目录、导出目录、导言区。
// 所有控件尺寸都用 Px() 按 DPI 缩放，且用“从上往下累加 y”的方式布局，
// 以后插入新控件时不用手工重排后面所有坐标。
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            int M = Px(10);                 // 对话框边距
            int lblH = Px(22);              // 标签行高（字体放大后要跟着加高，否则被裁）
            int edH = Px(28);               // 单行输入框高
            int gap = Px(8);                // 标签与输入框之间的间距
            int blockGap = Px(16);          // 三组控件之间的间距
            int btnW = Px(90), btnH = Px(30);
            int dlgW = Px(620);             // 内容区宽度基准

            int y = M;
            int editW = dlgW - M * 2 - btnW - gap;

            // ---- 第 1 组：TeX Live bin 目录 ----
            CreateWindowExW(0, L"STATIC",
                L"TeX Live 的 bin 目录（包含 pdflatex.exe 的文件夹）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_TEX, g_hInst, NULL);
            y += lblH + gap;
            g_hSetTex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, M, y, editW, edH,
                hwnd, (HMENU)IDS_TEX + 100, g_hInst, NULL);
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
                hwnd, (HMENU)IDS_EXP + 100, g_hInst, NULL);
            CreateWindowExW(0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE,
                M + editW + gap, y, btnW, btnH, hwnd, (HMENU)IDS_BROWSE_EXP, g_hInst, NULL);
            y += edH + blockGap;

            // ---- 第 3 组：导言区（多行）----
            CreateWindowExW(0, L"STATIC",
                L"导言区（\\usepackage 与自定义命令，可留空）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_PRE, g_hInst, NULL);
            y += lblH + gap;
            int preH = Px(170);
            g_hSetPre = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                M, y, dlgW - M * 2, preH, hwnd, (HMENU)IDS_PRE + 100, g_hInst, NULL);
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
            SubclassEditForSelectAll(g_hSetTex);     // 让这几个编辑框也支持 Ctrl+A
            SubclassEditForSelectAll(g_hSetExp);
            SubclassEditForSelectAll(g_hSetPre);
            HWND kids[] = { GetDlgItem(hwnd, IDS_TEX), GetDlgItem(hwnd, IDS_EXP), GetDlgItem(hwnd, IDS_PRE),
                            GetDlgItem(hwnd, IDS_BROWSE_TEX), GetDlgItem(hwnd, IDS_BROWSE_EXP),
                            GetDlgItem(hwnd, IDS_SAVE_BTN), GetDlgItem(hwnd, IDS_CANCEL) };
            for (HWND c : kids) if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            InvalidateRect(hwnd, NULL, TRUE);        // STATIC 标签要重绘才会用上新字体

            // ---- 填入当前配置 ----
            EnterCriticalSection(&g_cs);
            SetWindowTextW(g_hSetTex, g_cfg.texlive.c_str());
            SetWindowTextW(g_hSetExp, g_cfg.exportPath.c_str());
            SetWindowTextW(g_hSetPre, g_cfg.preamble.c_str());
            LeaveCriticalSection(&g_cs);
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
            } else if (id == IDS_CANCEL) {
                DestroyWindow(hwnd);                        // 不保存，直接关掉
            } else if (id == IDS_SAVE_BTN) {
                // 读回三个输入框的内容并写进配置
                int n1 = GetWindowTextLengthW(g_hSetTex); std::wstring t1(n1 + 1, 0);
                GetWindowTextW(g_hSetTex, &t1[0], n1 + 1); t1.resize(n1);
                int n2 = GetWindowTextLengthW(g_hSetExp); std::wstring t2(n2 + 1, 0);
                GetWindowTextW(g_hSetExp, &t2[0], n2 + 1); t2.resize(n2);
                int n3 = GetWindowTextLengthW(g_hSetPre); std::wstring t3(n3 + 1, 0);
                GetWindowTextW(g_hSetPre, &t3[0], n3 + 1); t3.resize(n3);
                EnterCriticalSection(&g_cs);
                g_cfg.texlive = t1; g_cfg.exportPath = t2; g_cfg.preamble = t3;
                LeaveCriticalSection(&g_cs);
                SaveConfig(g_cfg);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // 关闭对话框：恢复主窗口可交互，并重新渲染（导言区/路径可能改了）
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
    g_hSettings = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CopyMathSettings", L"设置",
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, Px(640), Px(470), NULL, NULL, g_hInst, NULL);
    ShowWindow(g_hSettings, SW_SHOW);
    UpdateWindow(g_hSettings);
}

// ---- 编辑框子类化：补上 Ctrl+A --------------------------------------------
// Windows 的多行 EDIT 控件默认**不响应 Ctrl+A**，所以这里把窗口过程替换成
// EditProc，在里面自己处理 Ctrl+A（EM_SETSEL 0,-1 = 全选）。
// 原来的窗口过程存在窗口的 GWLP_USERDATA 里，因此同一套代码可以套在任意多个
// 编辑框上（主输入框 + 设置里的 3 个）。
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
            g_hBtnSave = CreateWindowExW(0, L"BUTTON", L"保存图片", WS_CHILD | WS_VISIBLE,
                0, 0, 120, 32, hwnd, (HMENU)IDC_SAVE, g_hInst, NULL);
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
            for (HWND c : { g_hView, g_hBtnRender, g_hBtnReset, g_hBtnCopy, g_hBtnSave, g_hBtnSettings, g_hStatus })
                SendMessageW(c, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

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

            int viewH = H - topY - btnH - statusH - pad * 3;     // 上方区域剩余高度
            if (viewH < Px(60)) viewH = Px(60);

            int leftW = W / 2 - pad / 2;
            int rightX = pad + leftW + pad;
            int rightW = W - rightX - pad;
            if (rightW < Px(60)) rightW = Px(60);

            SetWindowPos(g_hEdit, NULL, pad, topY, leftW, viewH, SWP_NOZORDER);
            SetWindowPos(g_hView, NULL, rightX, topY, rightW, viewH, SWP_NOZORDER);

            // 按钮行：从左往右依次排列，宽度按各自文字自适应
            int by = topY + viewH + pad;
            int x = pad;
            HWND btns[] = { g_hBtnRender, g_hBtnReset, g_hBtnCopy, g_hBtnSave, g_hBtnSettings };
            for (HWND b : btns) {
                int bw = FitButtonWidth(b);
                SetWindowPos(b, NULL, x, by, bw, btnH, SWP_NOZORDER);
                x += bw + pad;
            }

            SetWindowPos(g_hStatus, NULL, pad, by + btnH + pad, W - 2 * pad, statusH, SWP_NOZORDER);
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
            SetStatus(L"渲染成功");
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

    // ---- 载入配置；首次运行则自动探测 TeX Live 并写一份默认配置 ----
    if (!LoadConfig(g_cfg)) {
        g_cfg.texlive = AutoDetectTexlive();
        g_cfg.exportPath = L"";
        g_cfg.preamble = L"\\usepackage{amsmath,amssymb,amsfonts,mathtools}\n\\usepackage{xcolor,bm}\n";
        SaveConfig(g_cfg);
    } else if (g_cfg.preamble.empty()) {
        // 配置文件存在但导言区是空的：补上常用宏包，保证开箱即用
        // （用户在设置里写过内容的话不会被覆盖）
        g_cfg.preamble = L"\\usepackage{amsmath,amssymb,amsfonts,mathtools}\n\\usepackage{xcolor,bm}\n";
        SaveConfig(g_cfg);
    }

    // ---- 载入嵌进 exe 的图标（资源 ID 1，来自 app.rc / copymath.ico）----
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
    HWND hwnd = CreateWindowExW(0, L"CopyMathMain", L"CopyMath - LaTeX 公式渲染（F5 渲染 / F6 重置 / Ctrl+D 复制）",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, Px(1000), Px(400),
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // ---- 加速键表 ----
    // 用加速键（而不是在各窗口里处理按键）的好处：**与焦点无关**。
    // 光标停在输入框里时按键会被 EDIT 控件吃掉，而加速键是消息循环里
    // TranslateAccelerator 处理的，发生在消息派发之前，所以照样生效。
    //
    //   F5        → 渲染
    //   F6        → 重置（含清空输入）
    //   Ctrl+D    → 复制图片（Ctrl+C 会被输入框截走用于复制文本，故改用 Ctrl+D）
    //   F7        → 复制图片（备用键）
    ACCEL acc[4] = {
        { FVIRTKEY,            VK_F5,  (WORD)IDM_RENDER_F5 },
        { FVIRTKEY,            VK_F6,  (WORD)IDC_RESET     },
        { FCONTROL | FVIRTKEY, 'D',    (WORD)IDC_COPY      },
        { FVIRTKEY,            VK_F7,  (WORD)IDC_COPY      }
    };
    g_hAccel = CreateAcceleratorTableW(acc, 4);

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
