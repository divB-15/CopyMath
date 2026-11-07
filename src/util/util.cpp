#include "util.h"

// ---------------------------------------------------------------------------
// 宽字符 → UTF-8
//
// 标准两步式用法：第一次调用只为问长度（输出缓冲传 NULL），第二次才真正转换。
// 这样不需要猜测缓冲区大小。
// ---------------------------------------------------------------------------
std::string WToU8(const std::wstring& s) {
    if (s.empty()) return std::string();

    // 第一步：传 NULL 拿所需字节数（不含结尾的 '\0'）
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);

    // 第二步：真正转换到长度为 n 的字符串里
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n, NULL, NULL);
    return r;
}

// ---------------------------------------------------------------------------
// UTF-8 → 宽字符（同样是“先问长度、再转换”两步式）
// ---------------------------------------------------------------------------
std::wstring U8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();

    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);

    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n);
    return r;
}

// ---------------------------------------------------------------------------
// 递归创建目录
//
// 思路：从头到尾累加路径，遇到分隔符就把「到这一级为止」的路径拿去 CreateDirectory。
// 已经存在的层级会失败，忽略即可；最后用 GetFileAttributesW 复核结果，
// 因此“本来就存在”和“刚刚建好”都算成功。
//
// 特殊前缀的处理（单独创建没有意义，跳过）：
//   · "C:\"    —— 盘符根
//   · "\\"     —— UNC 路径的开头
//   · "\\server\" —— 只到主机名的那一级
// ---------------------------------------------------------------------------
bool EnsureDirectory(const std::wstring& dir) {
    if (dir.empty()) return false;

    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    std::wstring acc;
    for (size_t i = 0; i < dir.size(); ++i) {
        acc += dir[i];
        if (dir[i] != L'\\' && dir[i] != L'/') continue;

        if (acc == L"\\\\") continue;                              // UNC 前缀
        if (acc.size() == 3 && acc[1] == L':') continue;           // "C:\"
        if (acc[0] == L'\\' && acc.find(L'\\', 2) == std::wstring::npos) continue;  // "\\server\"

        CreateDirectoryW(acc.c_str(), NULL);                       // 失败无所谓，最后统一复核
    }
    CreateDirectoryW(dir.c_str(), NULL);                           // 末尾没有分隔符的那一级

    attr = GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// ---------------------------------------------------------------------------
// exe 所在目录（带结尾反斜杠）
// ---------------------------------------------------------------------------
std::wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0) return L"";
    std::wstring s(buf, n);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p + 1);   // 截到最后一个分隔符（含）
    return s;
}

// 相对路径按 exe 目录展开；绝对路径原样返回
std::wstring AbsFromExeDir(const std::wstring& path) {
    if (path.empty()) return L"";
    // 已经是绝对路径：盘符 "C:..." 或 UNC "\\server\..."
    if (path.size() >= 2 && path[1] == L':') return path;
    if (path[0] == L'\\' || path[0] == L'/') return path;
    return ExeDir() + path;
}

// 换行符规范化：统一成 CRLF（给 EDIT 控件用）
std::wstring ToCrlf(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') {
            r += L"\r\n";
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;   // 已经是 \r\n，跳过 \n
        } else if (c == L'\n') {
            r += L"\r\n";
        } else {
            r += c;
        }
    }
    return r;
}

// 换行符规范化：统一成 LF（写文件 / 拼 LaTeX 源用）
std::wstring ToLf(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') {
            r += L'\n';
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;   // \r\n 只算一次
        } else {
            r += c;
        }
    }
    return r;
}

// 取出父目录：最后一个分隔符之前的部分（不含分隔符）。
// 例： <目录>\logs.txt  →  <目录> ；X:\a  →  盘符根（保留成 "X:\ "）
// 注意：注释里不要以反斜杠结尾，C++ 会把下一行也并进注释。
std::wstring DirNameOf(const std::wstring& filePath) {
    size_t p = filePath.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    if (p == 0) return filePath.substr(0, 1);                 // "\x"
    if (p == 2 && filePath[1] == L':') return filePath.substr(0, 3);  // "C:\x" → "C:\"
    return filePath.substr(0, p);
}
