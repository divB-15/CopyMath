// ============================================================================
//  main_window.cpp — 主窗口的窗口过程
//
//  WM_CREATE 建控件、WM_SIZE 手写布局（按钮自动换行）、WM_COMMAND 分发按钮、
//  WM_RENDER_DONE / WM_RENDER_ERROR 收后台线程的结果。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "render.h"
#include "util.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>

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
            // 退出时把临时文件清掉，并把 tmp\ 目录也删掉（只有空目录才会被删成功），
            // 这样程序文件夹里不会留下 formula.aux 之类的残渣。
            CleanRenderCache();
            RemoveDirectoryW((ExeDir() + L"tmp").c_str());
            if (g_viewBitmap) delete g_viewBitmap;
            if (g_hFontUI) { DeleteObject(g_hFontUI); g_hFontUI = NULL; }
            if (g_hFontEdit) { DeleteObject(g_hFontEdit); g_hFontEdit = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
