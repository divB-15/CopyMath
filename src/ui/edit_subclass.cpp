// ============================================================================
//  edit_subclass.cpp — 编辑框子类化
//
//  1) 补上 Ctrl+A 全选（Win32 的多行 EDIT 默认不响应）；
//  2) 粘贴时把“只有 \n”的文本补成 CRLF（网页 / PDF 复制常见），
//     否则多行 EDIT 会把它整段当成一行显示。详见 v1.3 的修复说明。
//  ============================================================================

#include "app.h"
#include "util.h"
#include <windows.h>
#include <string>

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

// ---------------------------------------------------------------------------
// 多行编辑框的“粘贴规范化”（v1.3）
//
// 从网页 / PDF / 别的编辑器复制出来的文本常常只有 '\n' 而没有 '\r'，
// 直接贴进 Win32 的多行 EDIT 会被当成**一行**（换行处显示成方块或被吞掉）。
// 这里先看一眼剪贴板：
//   · 含“裸换行” → 自己转成 CRLF 后用 EM_REPLACESEL 插入，返回 true；
//   · 其它情况（本来就是 CRLF，或剪贴板里没有 CF_UNICODETEXT）→ 返回 false，
//     交回默认处理，这样图片 / 富文本等其它剪贴板格式完全不受影响。
// ---------------------------------------------------------------------------
static bool PasteNormalizedCrlf(HWND hEdit) {
    if (!OpenClipboard(hEdit)) return false;      // 剪贴板被别的程序占着：走默认粘贴
    bool handled = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
            std::wstring t(p);
            GlobalUnlock(h);
            bool bare = false;
            for (size_t i = 0; i < t.size() && !bare; ++i) {
                if (t[i] == L'\n' && (i == 0 || t[i - 1] != L'\r')) bare = true;
                else if (t[i] == L'\r' && (i + 1 >= t.size() || t[i + 1] != L'\n')) bare = true;
            }
            if (bare) {
                std::wstring crlf = ToCrlf(t);
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)crlf.c_str());
                handled = true;
            }
        }
    }
    CloseClipboard();
    return handled;
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_KEYDOWN && w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);   // 全选：从 0 到末尾
        return 0;
    }
    // 多行编辑框：把“只有 \n”的粘贴内容补成 CRLF，否则会挤成一行
    if (msg == WM_PASTE && (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE)) {
        if (PasteNormalizedCrlf(hwnd)) return 0;
    }
    WNDPROC old = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return CallWindowProcW(old, hwnd, msg, w, l);   // 其余消息交还原处理
}
