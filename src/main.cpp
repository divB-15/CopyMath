// ============================================================================
//  main.cpp — 程序入口
//
//  只做三件事：声明 DPI 感知 → 初始化（COM / GDI+ / 配置 / 字体图标 / 窗口类）
//  → 建主窗口并进消息循环。界面逻辑在 ui\，渲染调度在 app\render_controller.cpp。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "render.h"
#include "clipboard.h"
#include "log.h"
#include "util.h"
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <cwchar>

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
        // v1.3：默认导出目录改成**程序目录下的 export\**（相对路径），
        // 这样默认情况下程序产生的所有文件都在程序文件夹里，符合“绿色便携”。
        def.exportPath = L"export";
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
            // 老配置文件里 EXPORT 可能是空的（v1.2 默认留空，保存图片时会弹提示）
            if (g_cfg.exportPath.empty())   { g_cfg.exportPath = L"export";         needSave = true; }
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
