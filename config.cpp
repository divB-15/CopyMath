#include "config.h"
#include "util.h"
#include <windows.h>
#include <fstream>
#include <string>
#include <cwchar>

// ============================================================================
//  config.cpp — 读写 CopyMath.cfg
//
//  配置文件格式（UTF-8 纯文本）：
//
//      TEXLIVE=C:\texlive\2024\bin\windows
//      EXPORT=D:\pics
//      DPI=300
//      LOG_ENABLED=1
//      LOG_PATH=D:\pics\logs.txt
//      TOPMOST=0
//      PREAMBLE_START
//      \usepackage{physics}
//      \newcommand{\d}{\mathrm{d}}
//      PREAMBLE_END
//
//  说明：导言区可能含 '=' 和空行，用 `键=值` 逐行解析会破坏它，所以单独用
//  PREAMBLE_START / PREAMBLE_END 两个标记把这段原样夹起来。
//  之所以不写成 INI 由系统 API 处理，是因为要完全掌控 UTF-8 与多行内容。
//
//  v1.2：文件名由 copymath.cfg 改为 CopyMath.cfg（与程序名保持一致），
//        并新增 DPI / LOG_ENABLED / LOG_PATH / TOPMOST 四个键。
// ============================================================================

// 出厂默认导言区（声明见 config.h）
const wchar_t* const kDefaultPreamble =
    L"\\usepackage{mathtools,amssymb,physics,bm,mathrsfs,esint,tensor,yhmath}\n";

// 配置文件的完整路径 = exe 所在目录 + CopyMath.cfg
// 放在 exe 旁边（而非 %APPDATA%），方便做成绿色便携版：整个文件夹拷走即可。
static std::wstring GetConfigPath() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);     // 取本进程 exe 的完整路径
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p + 1);  // 截到最后一个分隔符（含）
    return s + L"CopyMath.cfg";
}

// 去掉首尾的空白字符（空格 / 制表 / 回车 / 换行）
static std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";          // 整行都是空白
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "1" / "true" / "yes"（不分大小写）都算真；其余算假
static bool ToBool(const std::wstring& v) {
    return v == L"1" || _wcsicmp(v.c_str(), L"true") == 0 || _wcsicmp(v.c_str(), L"yes") == 0;
}

// ---------------------------------------------------------------------------
// 读取配置
//
// 逐行扫描：
//   · 遇到 PREAMBLE_START → 进入“导言区模式”，后续每行原样追加到 preamble，
//     直到遇到 PREAMBLE_END 为止（这样导言区里的 '=' 不会被误当成键值对）；
//     ⚠ 进入前会先 clear()：配置文件里的导言区是**权威内容**，直接替换掉调用方
//       预置的默认值。否则“默认导言区 + 文件里的导言区”会拼在一起，宏包重复。
//   · 其它行按 `键=值` 解析，未知键忽略（向前兼容：老配置文件缺的键保持默认）。
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
            cfg.preamble.clear();                // 文件里的导言区整体替换默认值
            inPre = true;
        } else {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring k = Trim(line.substr(0, eq));
                std::wstring v = Trim(line.substr(eq + 1));
                if (k == L"TEXLIVE") cfg.texlive = v;
                else if (k == L"EXPORT") cfg.exportPath = v;
                else if (k == L"DPI") {
                    int d = _wtoi(v.c_str());                     // 非法值一律回到默认
                    if (d < kDpiMin || d > kDpiMax) d = kDpiDefault;
                    cfg.dpi = d;
                }
                else if (k == L"LOG_ENABLED") cfg.logEnabled = ToBool(v);
                else if (k == L"LOG_PATH") cfg.logPath = v;
                else if (k == L"TOPMOST") cfg.alwaysOnTop = ToBool(v);
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
    text += L"DPI=" + std::to_wstring(cfg.dpi) + L"\n";
    text += std::wstring(L"LOG_ENABLED=") + (cfg.logEnabled ? L"1" : L"0") + L"\n";
    text += L"LOG_PATH=" + cfg.logPath + L"\n";
    text += std::wstring(L"TOPMOST=") + (cfg.alwaysOnTop ? L"1" : L"0") + L"\n";
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
