// ============================================================================
//  test_util.cpp — util 模块的单元测试
//
//  只测**不碰界面、不碰全局变量**的纯逻辑：
//    · ToLf / ToCrlf  —— 换行规范化（v1.3 修「导言区显示成一行」的关键）
//    · DirNameOf      —— 取父目录
//    · AbsFromExeDir  —— 相对路径展开（只看“绝对路径原样返回”这类无需真 exe 的分支）
//
//  不引第三方测试框架：一个 CHECK 宏 + 计数就够了，失败时打印文件行号并返回非 0，
//  CTest 据此判红。以后想换 Catch2 / GoogleTest，替换本文件即可，CMake 不用动。
// ============================================================================

#include "util.h"

#include <windows.h>
#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

// 仅用于把 wstring 打到控制台（用 Win32 转 UTF-8，避免受控制台代码页影响）
static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

#define CHECK(cond)                                                     \
    do {                                                                \
        if (cond) {                                                     \
            ++g_pass;                                                   \
        } else {                                                        \
            ++g_fail;                                                   \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

#define CHECK_WSTR(actual, expect)                                              \
    do {                                                                        \
        const std::wstring a_ = (actual);                                       \
        const std::wstring e_ = (expect);                                       \
        if (a_ == e_) {                                                         \
            ++g_pass;                                                           \
        } else {                                                                \
            ++g_fail;                                                           \
            printf("FAIL %s:%d\n  actual: [%s]\n  expect: [%s]\n", __FILE__,     \
                   __LINE__, Narrow(a_).c_str(), Narrow(e_).c_str());           \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// ToLf：CRLF / 单独 CR / 单独 LF 全部统一成 LF
// ---------------------------------------------------------------------------
static void TestToLf() {
    CHECK_WSTR(ToLf(L""), L"");
    CHECK_WSTR(ToLf(L"abc"), L"abc");
    CHECK_WSTR(ToLf(L"a\r\nb\r\nc"), L"a\nb\nc");   // 标准 CRLF
    CHECK_WSTR(ToLf(L"a\nb\nc"), L"a\nb\nc");       // 已经是 LF：不动
    CHECK_WSTR(ToLf(L"a\rb\rc"), L"a\nb\nc");       // 老式 Mac 的单独 CR
    CHECK_WSTR(ToLf(L"a\r\n\nb"), L"a\n\nb");       // 空行不能多出换行
    CHECK_WSTR(ToLf(L"\r\n"), L"\n");
    CHECK_WSTR(ToLf(L"\n\r"), L"\n\n");             // \n 然后单独的 \r → 两行
}

// ---------------------------------------------------------------------------
// ToCrlf：反过来统一成 CRLF（EDIT 控件只认这个）
// ---------------------------------------------------------------------------
static void TestToCrlf() {
    CHECK_WSTR(ToCrlf(L""), L"");
    CHECK_WSTR(ToCrlf(L"\\usepackage{physics}"), L"\\usepackage{physics}");
    CHECK_WSTR(ToCrlf(L"a\nb"), L"a\r\nb");         // 配置里的 LF → 进编辑框的 CRLF
    CHECK_WSTR(ToCrlf(L"a\r\nb"), L"a\r\nb");       // 已经是 CRLF：不动
    CHECK_WSTR(ToCrlf(L"a\rb"), L"a\r\nb");         // 单独 CR 也补全
    CHECK_WSTR(ToCrlf(L"a\n\nb"), L"a\r\n\r\nb");   // 空行保留
}

// ---------------------------------------------------------------------------
// 往返：两个方向各自幂等，且互转后语义不变
// ---------------------------------------------------------------------------
static void TestRoundTrip() {
    const std::wstring sample =
        L"\\usepackage{mathtools}\r\n\r\n\\let\\d\\relax\n\\newcommand{\\d}{\\mathrm{d}}\r\n";

    // 幂等：同一个函数连做两遍，结果不该再变
    CHECK(ToLf(ToLf(sample)) == ToLf(sample));
    CHECK(ToCrlf(ToCrlf(sample)) == ToCrlf(sample));

    // 往返：CRLF 化再 LF 化，得到的就是“纯 LF 版原文”
    CHECK(ToLf(ToCrlf(sample)) == ToLf(sample));
    // 反过来：LF 化再 CRLF 化 = 直接 CRLF 化
    CHECK(ToCrlf(ToLf(sample)) == ToCrlf(sample));

    // 换行符个数不能变（导言区“显示成一行”就是栽在这上面）
    std::wstring lf = ToLf(sample);
    size_t lines = 0;
    for (wchar_t c : lf) if (c == L'\n') ++lines;
    CHECK(lines == 4);
    CHECK(lf.find(L'\r') == std::wstring::npos);   // 转完不该再有 CR
}

// ---------------------------------------------------------------------------
// DirNameOf：取父目录
// ---------------------------------------------------------------------------
static void TestDirNameOf() {
    CHECK_WSTR(DirNameOf(L"D:\\pics\\logs.txt"), L"D:\\pics");
    CHECK_WSTR(DirNameOf(L"D:\\pics\\a\\b.png"), L"D:\\pics\\a");
    CHECK_WSTR(DirNameOf(L"C:\\a"), L"C:\\");            // 盘符根要保留反斜杠
    CHECK_WSTR(DirNameOf(L"\\x"), L"\\");                // 单个反斜杠开头
    CHECK_WSTR(DirNameOf(L"plain.txt"), L"");            // 没有分隔符
    CHECK_WSTR(DirNameOf(L"D:/forward/slash.txt"), L"D:/forward");  // 正斜杠也算
}

// ---------------------------------------------------------------------------
// AbsFromExeDir：只测“绝对路径原样返回”，相对分支依赖真实 exe 目录，不在这里测
// ---------------------------------------------------------------------------
static void TestAbsFromExeDir() {
    CHECK_WSTR(AbsFromExeDir(L""), L"");
    CHECK_WSTR(AbsFromExeDir(L"D:\\pics\\out"), L"D:\\pics\\out");
    CHECK_WSTR(AbsFromExeDir(L"C:\\"), L"C:\\");
    CHECK_WSTR(AbsFromExeDir(L"\\\\server\\share\\x"), L"\\\\server\\share\\x");

    // 相对路径会被拼到 exe 目录后面，所以结果一定比输入长、且以分隔符结尾的目录开头
    std::wstring rel = AbsFromExeDir(L"export");
    CHECK(rel.size() > 6);
    CHECK(rel.find(L"export") != std::wstring::npos);
}

int main() {
    TestToLf();
    TestToCrlf();
    TestRoundTrip();
    TestDirNameOf();
    TestAbsFromExeDir();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
