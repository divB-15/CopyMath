#include "log.h"
#include "util.h"
#include <windows.h>
#include <fstream>
#include <string>

// ============================================================================
//  log.cpp — 使用日志的落盘实现（v1.2 新增）
//
//  每条记录长这样（区块之间用一条 65 个 '=' 的分隔线隔开）：
//
//      =================================================================
//      [2026-09-28 13:20:11] 保存图片
//        分辨率: 300 DPI
//        像素  : 361 x 130
//        文件  : D:\pics\formula_20260928_132011.png
//        源码  :
//      \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
//
//  设计取舍：
//    · **追加**写入，不覆盖 —— 随时可以接着记，也方便用户自己往里补备注；
//    · 文件是 UTF-8；若是**新建**的空文件，先写一个 BOM，这样用记事本 /
//      Excel 打开中文都不会乱码（已有内容的文件不动它，免得 BOM 跑到中间）；
//    · 目录不存在会自动建（含多级）。
// ============================================================================

// 一眼能看出分块的分隔线（65 个 '='）
static const char* kSeparator =
    "=================================================================\r\n";

// 形如 2026-09-28 13:20:11 的本地时间戳
static std::wstring LogTimestamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

// 去掉首尾空白（用户可能把路径粘成 " D:\pics\ "）
static std::wstring TrimWs(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring ResolveLogFilePath(const std::wstring& configured) {
    std::wstring p = TrimWs(configured);
    if (p.empty()) return L"";

    // 情况一：以分隔符结尾 → 这是文件夹
    wchar_t last = p.back();
    if (last == L'\\' || last == L'/') return p + L"logs.txt";

    // 情况二：指向一个已经存在的目录 → 同样是文件夹
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return p + L"\\logs.txt";

    // 情况三：当成文件路径原样使用
    return p;
}

bool AppendUsageLog(const std::wstring& logFile,
                    const std::wstring& action,
                    const std::wstring& latex,
                    int dpi, int w, int h,
                    const std::wstring& imagePath) {
    if (logFile.empty()) return false;

    // 1) 父目录不存在则先建出来
    std::wstring dir = DirNameOf(logFile);
    if (!dir.empty() && !EnsureDirectory(dir)) return false;

    // 2) 打开前先判断文件是否为“新建的空文件” —— 决定要不要写 BOM
    bool needBom = true;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(logFile.c_str(), GetFileExInfoStandard, &fad)) {
        needBom = (fad.nFileSizeHigh == 0 && fad.nFileSizeLow == 0);
    }

    // 3) 追加方式打开
    std::ofstream f(logFile.c_str(), std::ios::binary | std::ios::app);
    if (!f) return false;

    if (needBom) {
        const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
        f.write(bom, 3);
    }
    f.write(kSeparator, 67);                       // 65 个 '=' + CRLF

    // 4) 拼记录正文（内部统一用 '\n'，落盘时再换成 CRLF，Windows 下更好读）
    std::wstring t = L"[" + LogTimestamp() + L"] " + action + L"\n";
    t += L"  分辨率: " + std::to_wstring(dpi) + L" DPI\n";
    t += L"  像素  : " + std::to_wstring(w) + L" x " + std::to_wstring(h) + L"\n";
    if (!imagePath.empty()) t += L"  文件  : " + imagePath + L"\n";
    t += L"  源码  :\n";
    t += latex;
    if (t.empty() || t.back() != L'\n') t += L"\n";   // 保证记录以换行收尾

    std::string u8 = WToU8(t);
    for (size_t i = 0; i < u8.size(); ++i) {
        if (u8[i] == '\n' && (i == 0 || u8[i - 1] != '\r')) f.put('\r');
        f.put(u8[i]);
    }

    return f.good();
}
