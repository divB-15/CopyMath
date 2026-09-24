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
