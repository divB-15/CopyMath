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

// 取出父目录：最后一个分隔符之前的部分（不含分隔符）。
// 例： D:\pics\logs.txt  →  D:\pics ；C:\a  →  盘符根（保留成 "C:\ "）
// 注意：注释里不要以反斜杠结尾，C++ 会把下一行也并进注释。
std::wstring DirNameOf(const std::wstring& filePath) {
    size_t p = filePath.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    if (p == 0) return filePath.substr(0, 1);                 // "\x"
    if (p == 2 && filePath[1] == L':') return filePath.substr(0, 3);  // "C:\x" → "C:\"
    return filePath.substr(0, p);
}
