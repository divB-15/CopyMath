// ============================================================================
//  render_controller.cpp — 渲染调度
//
//  界面只管“请求渲染”，这里负责：
//    · 用 g_gen 版本号 + g_rendering 忙标志保证同一时刻只有一个渲染线程；
//    · 渲染期间用户又改了输入时丢弃旧结果、重渲最新内容；
//    · 结果（位图 / 错误）通过自定义窗口消息回传主线程。
//  真正的 LaTeX 调用在 render\render.cpp。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "render.h"
#include <windows.h>
#include <gdiplus.h>
#include <string>

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
