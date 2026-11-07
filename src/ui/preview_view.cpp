// ============================================================================
//  preview_view.cpp — 右侧公式预览子窗口
//
//  自绘：白底 + 居中显示公式图；出错时把 LaTeX 报文用红字画在这里
//  （状态栏只有一行，容易被忽略）。
//  ============================================================================

#include "app.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>

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
