#pragma once
#include <windows.h>
#include <string>

// ============================================================================
//  util.h — 通用小工具：字符编码转换 + 目录创建
//
//  本项目内部一律使用 UTF-16 宽字符（std::wstring）：因为用到的 Win32 API
//  都是 W 版本（CreateWindowExW / GetWindowTextW …），它们只接受宽字符。
//
//  但有三处必须以 UTF-8 落盘：
//    1) 写给 pdflatex 的 .tex 源文件 —— LaTeX 按字节读取，UTF-8 才不会乱码；
//    2) CopyMath.cfg 配置文件 —— 用户可能用任何编辑器打开，UTF-8 通用；
//    3) 日志文件 logs.txt（v1.2） —— 同上，且要方便用记事本直接看。
//
//  于是需要这两个方向的转换函数。转换失败时返回空串（实际场景中几乎不会失败，
//  因为输入都来自本进程，编码一定合法）。
// ============================================================================

// 宽字符 → UTF-8 字节串。用于“写文件”。
std::string WToU8(const std::wstring& s);

// UTF-8 字节串 → 宽字符。用于“读文件”。
std::wstring U8ToW(const std::string& s);

// 递归创建目录（多级路径一次建完），已存在视为成功。
// 用 CreateDirectoryW 一次只能建一级，所以这里逐级切分后依次创建。
// 返回 true 表示调用结束时该目录确实存在。
bool EnsureDirectory(const std::wstring& dir);

// 取出一个文件路径的父目录部分（不含末尾分隔符）；没有分隔符时返回空串。
std::wstring DirNameOf(const std::wstring& filePath);

// exe 所在目录，**带结尾反斜杠**（例如 `<程序所在目录>\`）。
// 便携版的一切自有文件（配置 / 临时文件 / 默认导出目录）都以它为基准。
std::wstring ExeDir();

// 把相对路径按 exe 目录展开；绝对路径（"C:\..." / "\\server\..."）原样返回。
// 这样配置里可以写 "export"、"tmp\logs.txt" 这种相对写法，整个文件夹搬走仍有效。
std::wstring AbsFromExeDir(const std::wstring& path);

// ---------------------------------------------------------------------------
//  换行符规范化（v1.3）
//
//  ⚠ 为什么要专门写这两个函数：
//  Win32 的 EDIT 控件只把 **CR LF** 当换行。喂给它一段只有 '\n' 的文本，
//  它会把整段当成**一行**显示（换行处变成一个方块或直接丢掉字符）——这正是
//  v1.2 里「设置 → 导言区」显示不出换行的根因：配置文件里存的是 '\n'，
//  直接 SetWindowTextW 塞进多行编辑框，屏幕上就成了一长条。
//  而写回 .cfg / 拼 .tex 时用的是内存里的 '\n'，所以文件本身是对的。
//
//  约定：程序内部一律用 '\n'（跟文本文件、LaTeX 源码一致）；
//        只在**送进 EDIT 控件**前用 ToCrlf，**从 EDIT 控件取出**后用 ToLf。
// ---------------------------------------------------------------------------

// \r\n / 单独的 \r / 单独的 \n  →  一律 \r\n
std::wstring ToCrlf(const std::wstring& s);

// \r\n / 单独的 \r / 单独的 \n  →  一律 \n
std::wstring ToLf(const std::wstring& s);
