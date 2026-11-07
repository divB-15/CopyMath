// ============================================================================
//  settings_dialog.cpp — “设置”对话框
//
//  六项设置：TeX Live 目录 / 导出目录 / DPI / 日志开关 + 路径 / 导言区（多行）。
//  布局用“从上往下累加 y”的方式，插新控件不用手工重排后面的坐标。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "util.h"
#include <windows.h>
#include <shlwapi.h>
#include <string>

// 设置窗口的客户区设计尺寸（96 DPI 下的逻辑像素，实际会按 DPI 缩放）。
static const int kSetClientW = 660;
static const int kSetClientH = 604;

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
                L"导言区（\\usepackage 与自定义命令，可留空；回车换行）：",
                WS_CHILD | WS_VISIBLE | SS_LEFT, M, y, dlgW - M * 2, lblH, hwnd, (HMENU)IDS_PRE, g_hInst, NULL);
            y += lblH + gap;
            int preH = Px(170);
            // ⚠ ES_WANTRETURN 不能少（v1.3 修）：没有它，多行 EDIT 在非对话框窗口里
            //   会把回车当成“按下默认按钮”而**不插入换行**，用户根本没法分行写导言区。
            // ⚠ 文本必须以 CRLF 传入（见下面 SetWindowTextW 处的 ToCrlf），否则显示成一行。
            g_hSetPre = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
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
            // ⚠ 必须转成 CRLF 再塞进编辑框（v1.3 修的核心）：
            //   cfg 里存的是 '\n'，而多行 EDIT 只认 CRLF —— 直接 SetWindowTextW
            //   的结果是整段导言区挤成一行（换行处显示成方块/被吞掉）。
            SetWindowTextW(g_hSetPre, ToCrlf(g_cfg.preamble).c_str());
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
                g_cfg.texlive = t1; g_cfg.exportPath = t2;
                // 编辑框里取出来的是 CRLF，存进配置前统一成 LF（写 .tex 时也用 LF）
                g_cfg.preamble = ToLf(t3);
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
