// ============================================================================
//  actions.cpp — 主界面上的“动作”
//
//  复制图片 / 保存图片 / 重置全部 / 自动探测 TeX Live / 选文件夹 / 拼时间戳，
//  以及“使用日志”这一层封装（LogUsage / LogSuffix）。都只在主线程里跑。
//  ============================================================================

#include "app.h"
#include "config.h"
#include "log.h"
#include "clipboard.h"
#include "util.h"
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>

// 日志写入的三种结果：没启用 / 写成功 / 写失败（失败时界面要提醒用户）
enum LogResult { LOG_OFF, LOG_OK, LOG_FAIL };

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
    // v1.3：允许相对路径（例如 "export"），按 exe 所在目录展开 —— 整个文件夹搬走仍有效
    exp = AbsFromExeDir(exp);

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

// 全部重置：右侧（缓存/预览/错误）+ 左侧（清空输入框并聚焦）
void ResetAll() {
    ResetRenderState();
    if (g_hEdit) SetWindowTextW(g_hEdit, L"");
    if (g_hEdit) SetFocus(g_hEdit);
    SetStatus(L"已全部重置：输入框与预览均已清空，输入公式后按 F5 渲染");
}
