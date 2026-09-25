#include "render.h"
#include "util.h"
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <fstream>
#include <string>
#include <vector>
#include <cwchar>

// ============================================================================
//  render.cpp — 用 TeX Live 把 LaTeX 公式渲染成位图
//
//  完整流水线：
//
//    latex 源码
//       │  StripDelims()   剥离用户自带的定界符
//       ▼
//    formula.tex ──pdflatex──▶ formula.pdf ──pdftocairo──▶ formula.png
//       │                                                    │
//       └──────────── 读入 GDI+ Bitmap ◀─────────────────────┘
//
//  所有中间文件都放在 %TEMP%\copymath\ 下（固定文件名 formula.*），
//  渲染完成后立即删除，不留垃圾；渲染前也会先清一遍，避免旧文件干扰。
// ============================================================================

// 返回并确保存在临时工作目录：%TEMP%\copymath
static std::wstring GetTempDir() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);                 // 结尾自带反斜杠
    std::wstring dir = std::wstring(tmp) + L"copymath";
    CreateDirectoryW(dir.c_str(), NULL);         // 已存在会失败，忽略即可
    return dir;
}

// ---------------------------------------------------------------------------
// 启动一个外部进程并等它结束，返回其退出码（失败返回 -1）。
//
//  ⚠ 关键点（本项目踩过的坑）：
//  本程序是 -mwindows 的 GUI 程序，**没有控制台**。而 pdflatex / pdftocairo
//  都是控制台程序，如果启动时不显式提供标准句柄，子进程会继承到无效句柄，
//  在初始化标准流时失败并直接退出（表现为莫名的“转换失败”）。
//
//  解决办法：设 STARTF_USESTDHANDLES，把 stdout / stderr 各自重定向到
//  **独立的** NUL 句柄（不能共用一个句柄），stdin 用 GetStdHandle 拿到的
//  值（GUI 下通常为 NULL，子进程没有 stdin 反而更稳）。
// ---------------------------------------------------------------------------
static int RunCommand(const std::wstring& cmd) {
    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    std::wstring c = cmd;                        // CreateProcessW 会改写命令行，需要可写缓冲

    SECURITY_ATTRIBUTES sa; ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;                    // NUL 句柄要能被继承
    auto openNul = [&]() -> HANDLE {
        return CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    };

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = openNul();
    HANDLE hErr = openNul();                     // 与 hOut 分开，不能共用
    bool okHandles = (hOut != INVALID_HANDLE_VALUE && hErr != INVALID_HANDLE_VALUE);
    if (okHandles) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hIn;
        si.hStdOutput = hOut;
        si.hStdError = hErr;
    } else {
        if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
        if (hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
    }

    if (!CreateProcessW(NULL, &c[0], NULL, NULL, okHandles, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        if (okHandles) { CloseHandle(hOut); CloseHandle(hErr); }
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);  // 阻塞等它跑完
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (okHandles) { CloseHandle(hOut); CloseHandle(hErr); }
    return (int)code;
}

// ---------------------------------------------------------------------------
// 从 pdflatex 的 .log 里提取第一条错误信息，附带后面 1~2 行上下文。
// LaTeX 的错误行以 '!' 开头，例如：
//     ! LaTeX Error: Bad math environment delimiter.
//     l.12 \[
// 后面的行往往写着出错位置，对用户定位很有帮助，所以一并带上。
// ---------------------------------------------------------------------------
static std::wstring ReadFirstError(const std::wstring& logPath) {
    std::ifstream f(logPath.c_str(), std::ios::binary);
    if (!f) return L"";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);

    for (size_t i = 0; i < lines.size(); i++) {
        if (!lines[i].empty() && lines[i][0] == '!') {
            std::string msg = lines[i];
            for (size_t j = i + 1; j < lines.size() && j <= i + 2; j++) {
                if (lines[j].empty()) break;
                msg += " | " + lines[j];
            }
            return U8ToW(msg);
        }
    }
    return L"";
}

// 去掉首尾空白
static std::wstring TrimWs(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// 剥离用户粘贴时可能自带的数学定界符。
//
// 为什么需要：程序默认会把输入包进 \[ ... \]，如果用户从论文/Markdown 里
// 复制来的内容本身就带 \[..\] 或 $..$，就会变成嵌套，LaTeX 直接报错
// （`Bad math environment delimiter`）。所以先统一剥掉。
// ---------------------------------------------------------------------------
static std::wstring StripDelims(const std::wstring& s) {
    std::wstring r = TrimWs(s);

    // 尝试剥掉一对首尾匹配的定界符；成功返回 true
    auto strip = [&](const wchar_t* open, const wchar_t* close) -> bool {
        size_t no = wcslen(open), nc = wcslen(close);
        if (r.size() > no + nc && r.compare(0, no, open) == 0 &&
            r.compare(r.size() - nc, nc, close) == 0) {
            r = TrimWs(r.substr(no, r.size() - no - nc));
            return true;
        }
        return false;
    };

    if (strip(L"$$", L"$$")) return r;   // 行间：$$ ... $$
    if (strip(L"\\[", L"\\]")) return r; // 行间：\[ ... \]
    if (strip(L"\\(", L"\\)")) return r; // 行内：\( ... \)
    if (r.size() >= 2 && r.front() == L'$' && r.back() == L'$')   // 行内：$ ... $
        r = TrimWs(r.substr(1, r.size() - 2));
    return r;
}

// 输入里是否含换行符 \\（多行公式）。也可能出现在 matrix/cases 等内层环境里，
// 那种情况下多包一层 gathered 同样无害。
static bool HasLineBreak(const std::wstring& s) {
    return s.find(L"\\\\") != std::wstring::npos;
}

// 用户导言区里是否已经有 amsmath（mathtools 会自动加载 amsmath）
static bool PreambleHasAmsmath(const std::wstring& p) {
    return p.find(L"amsmath") != std::wstring::npos ||
           p.find(L"mathtools") != std::wstring::npos;
}

// ---------------------------------------------------------------------------
// 判断输入是否以“外层数学环境”开头。
//
// 这些环境（align / equation / gather 等）**自身就是显示数学**，会自己
// 排版成独立公式块。如果再把它们包进 \[ ... \]，LaTeX 会报错，所以对它们
// 直接原样使用、不再包裹。
// ---------------------------------------------------------------------------
static bool IsOuterMathEnv(const std::wstring& s) {
    static const wchar_t* envs[] = {
        L"equation", L"equation*", L"align", L"align*", L"alignat", L"alignat*",
        L"gather", L"gather*", L"multline", L"multline*", L"flalign", L"flalign*",
        L"eqnarray", L"eqnarray*", L"displaymath"
    };
    for (const wchar_t* e : envs) {
        std::wstring pat = std::wstring(L"\\begin{") + e + L"}";
        if (s.compare(0, pat.size(), pat) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 删除临时目录里所有 formula.* 中间文件。
//
// 重点是 .aux：pdflatex 中途报错时（例如括号没闭合）会留下一个被截断的
// .aux，下次编译读到它可能继续报错。每次渲染前清一遍最稳妥。
// 这里用“枚举已知扩展名”而不是通配符查找，行为更确定、更好调试。
// ---------------------------------------------------------------------------
static void CleanTempFiles(const std::wstring& dir) {
    static const wchar_t* exts[] = {
        L".tex", L".aux", L".log", L".pdf", L".png", L".out", L".toc",
        L".nav", L".snm", L".fls", L".synctex", L".synctex.gz", L".bbl", L".blg"
    };
    for (const wchar_t* e : exts) {
        DeleteFileW((dir + L"\\formula" + e).c_str());
    }
}

void CleanRenderCache() {
    CleanTempFiles(GetTempDir());
}

// ---------------------------------------------------------------------------
// 二次构图：把公式四周多余的空白裁掉（只留一圈很窄的视觉留白）
//
// 为什么需要它：pdflatex 的盒子宽度并不总等于公式的自然宽度。
//   \begin{align}...\end{align} 这类环境生成的盒子宽度就是 \hsize（一整行），
//   公式在其中居中/对齐，于是图的两侧会留出大片与公式无关的空白。
//   LaTeX 端我们已经用内联 $\displaystyle$ 避免了大半情况，但 align 这类
//   多列对齐环境无法内联，只能靠这里按像素扫描兜底。
//
// 做法：以 32bpp 读出像素，找出所有「非白且不透明」像素的包围盒，按包围盒裁切，
// 四周各补 margin 像素的空白。整张图没有墨迹（例如全是白色公式）时原样返回，
// 避免裁成 0×0。
//
// ⚠ 所有权约定（踩过坑）：本函数**只负责 new 出新图，绝不释放传入的 src**。
//   返回值和 src 相同时表示「没裁」；不同时由**调用方**负责 delete src。
//   曾经在这里顺手 `delete src` 又让调用方再 delete 一次，直接双重释放崩溃。
// ---------------------------------------------------------------------------
static const int kInkMarginPx = 3;   // 300 DPI 下 3px ≈ 0.72pt，仅作视觉留白

static Gdiplus::Bitmap* TrimToInk(Gdiplus::Bitmap* src, int margin) {
    if (!src || src->GetLastStatus() != Gdiplus::Ok) return src;
    int w = (int)src->GetWidth(), h = (int)src->GetHeight();
    if (w <= 0 || h <= 0) return src;

    Gdiplus::BitmapData bd;
    Gdiplus::Rect all(0, 0, w, h);
    if (src->LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
        return src;

    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const BYTE* row = (const BYTE*)bd.Scan0 + (ptrdiff_t)y * bd.Stride;  // 兼容负 stride
        int rowMinX = -1, rowMaxX = -1;
        for (int x = 0; x < w; ++x) {
            const BYTE* p = row + (size_t)x * 4;        // 内存顺序为 BGRA
            if (p[3] < 16) continue;                    // 全透明 → 背景
            if (p[0] < 250 || p[1] < 250 || p[2] < 250) {
                if (rowMinX < 0) rowMinX = x;
                rowMaxX = x;
            }
        }
        if (rowMinX < 0) continue;                      // 整行空白
        if (rowMinX < minX) minX = rowMinX;
        if (rowMaxX > maxX) maxX = rowMaxX;
        if (y < minY) minY = y;
        maxY = y;
    }
    src->UnlockBits(&bd);

    if (maxX < 0 || maxY < 0) return src;               // 没找到墨迹：保持原图

    int x0 = minX - margin, y0 = minY - margin;
    int x1 = maxX + margin, y1 = maxY + margin;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    if (x0 == 0 && y0 == 0 && cw == w && ch == h) return src;   // 本来就已经是紧的

    // 裁出来的新图用 24bpp RGB：pdftocairo 出的本来就是 24 位无透明图（白底），
    // 保持这个格式，存出来的 PNG 与 v1.0 一致，也不会白白多出一个 alpha 通道。
    Gdiplus::Bitmap* out = src->Clone(Gdiplus::Rect(x0, y0, cw, ch), PixelFormat24bppRGB);
    if (!out || out->GetLastStatus() != Gdiplus::Ok) { delete out; return src; }
    return out;   // ⚠ 这里**不能**删 src：所有权归调用方，见函数头注释
}

// ===========================================================================
//  主入口：渲染
// ===========================================================================
Gdiplus::Bitmap* RenderFormula(const std::wstring& latex,
                               const std::wstring& preamble,
                               const std::wstring& texliveBin,
                               std::wstring* errorOut) {
    if (errorOut) *errorOut = L"";

    std::wstring dir = GetTempDir();
    CleanTempFiles(dir);                          // 先清掉上次遗留
    std::wstring texFile = dir + L"\\formula.tex";
    std::wstring pdfFile = dir + L"\\formula.pdf";
    std::wstring pngBase = dir + L"\\formula";    // pdftocairo 的输出“基名”，产出 <基名>.png
    std::wstring logFile = dir + L"\\formula.log";

    // ---- 1) 规范化用户输入并拼出公式块 ----
    std::wstring body = StripDelims(latex);
    if (body.empty()) {
        // 空输入：不算错误，返回 nullptr 让界面显示占位提示
        if (errorOut) *errorOut = L"";
        return nullptr;
    }
    bool outerEnv = IsOuterMathEnv(body);

    // ---- 1b) 组装公式块：v1.1 的关键改动 —— 不再用 \[ ... \] ----
    //  \[ ... \] 生成的行间公式，盒子宽度 = \hsize（整整一行），公式在其中居中。
    //  而 preview 的 tightpage 是按「盒子」裁页面的，于是裁出来的是**整行宽**的图，
    //  两侧全是与公式无关的空白（实测：公式本身宽 335px，出图却宽 1624px）。
    //
    //  改用内联数学 $\displaystyle ...$：盒子宽度 = 公式自身的自然宽度，既紧致，
    //  又不受 \hsize 限制（用 \[ \] 时，超过一行的长公式会被 PDF 页面裁掉，这是隐患）。
    //  \displaystyle 保证行内也按行间公式的字号排版（大分式、大积分号不变小）。
    //
    //  两个例外：
    //   · 含 \\ 的多行内容 —— 行内数学里 \\ 非法，改用 gathered 包起来。gathered 的
    //     宽度天然等于最宽一行的宽度、各行居中，观感与行间公式一致；
    //   · align / equation 等外层环境 —— 无法内联，保持原样，由第 8 步 TrimToInk 裁齐。
    std::wstring mathBlock;
    bool needAmsmath = false;                      // 是否需要替用户补上 amsmath 宏包
    if (outerEnv) {
        mathBlock = body;
    } else if (HasLineBreak(body)) {
        mathBlock = L"$\\displaystyle\\begin{gathered}\n" + body + L"\n\\end{gathered}$\n";
        needAmsmath = !PreambleHasAmsmath(preamble);   // gathered 来自 amsmath
    } else {
        mathBlock = L"$\\displaystyle " + body + L"$\n";
    }

    // ---- 2) 组装完整 .tex 文档 ----
    //  用 preview 宏包的 tightpage 选项，让 PDF 的页面边界紧贴公式，
    //  这样 PDF 页面在高度方向就紧贴公式；宽度方向由第 8 步的 TrimToInk 兜底。
    std::wstring content =
        L"\\documentclass[12pt]{article}\n"
        L"\\usepackage[utf8]{inputenc}\n"
        L"\\pagestyle{empty}\n"
        + preamble +                                        // ← 用户导言区
        (needAmsmath ? L"\\usepackage{amsmath}\n" : L"") +  // ← 用到 gathered 而用户没引 amsmath 时补上
        L"\\usepackage[active,tightpage]{preview}\n"
        // 给紧致页面留 2pt 保护白边：实测不留白边时页面边框正好压在墨迹上，
        // 光栅化会把最外一圈像素（斜体挑出的笔画、根号尖端）削掉一点点。
        // 有了这圈白边，第 8 步再按墨迹裁，既不丢笔画，四周也能有统一的留白。
        L"\\setlength\\PreviewBorder{2pt}\n"
        L"\\begin{document}\n"
        L"\\begin{preview}\n"
        + mathBlock +
        L"\\end{preview}\n"
        L"\\end{document}\n";

    {
        // 以 UTF-8 写出 .tex
        std::ofstream f(texFile.c_str(), std::ios::binary);
        if (!f) { if (errorOut) *errorOut = L"无法写入临时文件"; return nullptr; }
        std::string u8 = WToU8(content);
        f.write(u8.data(), (std::streamsize)u8.size());
    }

    // ---- 3) 检查 TeX Live 可执行文件是否存在 ----
    std::wstring pdflatex = texliveBin + L"\\pdflatex.exe";
    std::wstring pdftocairo = texliveBin + L"\\pdftocairo.exe";
    if (!PathFileExistsW(pdflatex.c_str())) {
        if (errorOut) *errorOut = L"找不到 pdflatex.exe，请在“设置”中检查 TeX Live 路径";
        return nullptr;
    }

    // ---- 4) pdflatex：.tex → .pdf ----
    //    -interaction=nonstopmode : 出错不等待用户输入（否则会挂住）
    //    -halt-on-error           : 首次报错就停止，退出码非 0
    //    -output-directory=...    : 中间文件都写到临时目录
    std::wstring cmd1 = L"\"" + pdflatex + L"\" -interaction=nonstopmode -halt-on-error -output-directory=\""
                      + dir + L"\" \"" + texFile + L"\"";
    int r1 = RunCommand(cmd1);
    if (r1 != 0) {
        std::wstring err = ReadFirstError(logFile);
        if (err.empty()) err = L"pdflatex 渲染失败（未知错误）";
        if (errorOut) *errorOut = err;
        DeleteFileW(texFile.c_str());
        DeleteFileW(logFile.c_str());
        return nullptr;
    }

    // ---- 5) pdftocairo：.pdf → .png ----
    //    -png -r 300 : 输出 PNG，300 DPI（分辨率足够高，缩放/打印都清晰）
    //    -singlefile : 输出单文件 <基名>.png（否则会带页码后缀）
    std::wstring cmd2 = L"\"" + pdftocairo + L"\" -png -r 300 -singlefile \"" + pdfFile + L"\" \"" + pngBase + L"\"";
    int r2 = RunCommand(cmd2);
    std::wstring pngFile = pngBase + L".png";     // -singlefile 固定产出这个名字
    if (r2 != 0 || !PathFileExistsW(pngFile.c_str())) {
        if (errorOut) *errorOut = L"pdftocairo 转换失败，可能 TeX Live 缺少 poppler 组件";
        DeleteFileW(texFile.c_str());
        DeleteFileW(pdfFile.c_str());
        DeleteFileW(logFile.c_str());
        return nullptr;
    }

    // ---- 6) 读入 PNG 变成 GDI+ Bitmap ----
    //   GDI+ 在构造时就会把文件内容整个读进内存，所以随后可以安全删源文件。
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(pngFile.c_str());
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        if (errorOut) *errorOut = L"无法加载生成的 PNG";
        DeleteFileW(texFile.c_str());
        DeleteFileW(pdfFile.c_str());
        DeleteFileW(logFile.c_str());
        DeleteFileW(pngFile.c_str());
        return nullptr;
    }

    // ---- 7) 清理临时文件，返回位图 ----
    DeleteFileW(texFile.c_str());
    DeleteFileW(pdfFile.c_str());
    DeleteFileW(logFile.c_str());
    DeleteFileW(pngFile.c_str());

    // ---- 8) 二次构图：裁掉公式四周多余的空白 ----
    //   LaTeX 端只能保证「非外层环境」的盒子贴合公式（见 1b）；align / equation 这类
    //   环境的盒子宽度仍然是 \hsize（一整行），两侧会留出大片与公式无关的空白。
    //   这里按像素扫描出墨迹的包围盒，裁到只剩一圈很窄的留白，
    //   确保复制/保存出来的图片尺寸就是公式本身的大小。
    Gdiplus::Bitmap* trimmed = TrimToInk(bmp, kInkMarginPx);
    if (trimmed && trimmed != bmp) { delete bmp; bmp = trimmed; }
    return bmp;
}
