#include "config.h"
#include "util.h"
#include <windows.h>
#include <fstream>
#include <string>

// ============================================================================
//  config.cpp — 读写 copymath.cfg
//
//  配置文件格式（UTF-8 纯文本）：
//
//      TEXLIVE=C:\texlive\2024\bin\windows
//      EXPORT=D:\pics
//      PREAMBLE_START
//      \usepackage{physics}
//      \newcommand{\d}{\mathrm{d}}
//      PREAMBLE_END
//
//  说明：导言区可能含 '=' 和空行，用 `键=值` 逐行解析会破坏它，所以单独用
//  PREAMBLE_START / PREAMBLE_END 两个标记把这段原样夹起来。
//  之所以不写成 INI 由系统 API 处理，是因为要完全掌控 UTF-8 与多行内容。
// ============================================================================

// 配置文件的完整路径 = exe 所在目录 + copymath.cfg
// 放在 exe 旁边（而非 %APPDATA%），方便做成绿色便携版：整个文件夹拷走即可。
static std::wstring GetConfigPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);     // 取本进程 exe 的完整路径
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p + 1);  // 截到最后一个分隔符（含）
    return s + L"copymath.cfg";
}

// 去掉首尾的空白字符（空格 / 制表 / 回车 / 换行）
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";          // 整行都是空白
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// 读取配置
//
// 逐行扫描：
//   · 遇到 PREAMBLE_START → 进入“导言区模式”，后续每行原样追加到 preamble，
//     直到遇到 PREAMBLE_END 为止（这样导言区里的 '=' 不会被误当成键值对）；
//   · 其它行按 `键=值` 解析，仅识别 TEXLIVE / EXPORT，未知键忽略（向前兼容）。
// ---------------------------------------------------------------------------
bool LoadConfig(Config& cfg) {
    std::ifstream f(GetConfigPath().c_str(), std::ios::binary);
    if (!f) return false;                        // 首次运行：文件还不存在

    // 一次性读入整个文件，再按 UTF-8 转成宽字符
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::wstring text = U8ToW(raw);

    bool inPre = false;                          // 是否处于导言区模式
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = (nl == std::wstring::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();   // 兼容 CRLF

        if (inPre) {
            if (line == L"PREAMBLE_END") inPre = false;
            else cfg.preamble += line + L"\n";
        } else if (line == L"PREAMBLE_START") {
            inPre = true;
        } else {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring k = Trim(line.substr(0, eq));
                std::wstring v = Trim(line.substr(eq + 1));
                if (k == L"TEXLIVE") cfg.texlive = v;
                else if (k == L"EXPORT") cfg.exportPath = v;
            }
        }
        if (nl == std::wstring::npos) break;     // 最后一行没有换行符
        pos = nl + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 写出配置（格式见本文件顶部说明）
// ---------------------------------------------------------------------------
bool SaveConfig(const Config& cfg) {
    std::wstring text;
    text += L"TEXLIVE=" + cfg.texlive + L"\n";
    text += L"EXPORT=" + cfg.exportPath + L"\n";
    text += L"PREAMBLE_START\n";
    text += cfg.preamble;
    // 导言区若不以换行结尾，补一个，保证 PREAMBLE_END 独占一行（否则下次读会把两行粘起来）
    if (!cfg.preamble.empty() && cfg.preamble.back() != L'\n') text += L"\n";
    text += L"PREAMBLE_END\n";

    std::ofstream f(GetConfigPath().c_str(), std::ios::binary);
    if (!f) return false;
    std::string u8 = WToU8(text);                // 统一以 UTF-8 落盘
    f.write(u8.data(), (std::streamsize)u8.size());
    return true;
}
