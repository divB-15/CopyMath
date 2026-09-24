#pragma once
#include <windows.h>
#include <string>

// ============================================================================
//  util.h — 字符编码转换（UTF-16 宽字符 <-> UTF-8）
//
//  本项目内部一律使用 UTF-16 宽字符（std::wstring）：因为用到的 Win32 API
//  都是 W 版本（CreateWindowExW / GetWindowTextW …），它们只接受宽字符。
//
//  但有两处必须以 UTF-8 落盘：
//    1) 写给 pdflatex 的 .tex 源文件 —— LaTeX 按字节读取，UTF-8 才不会乱码；
//    2) copymath.cfg 配置文件 —— 用户可能用任何编辑器打开，UTF-8 通用。
//
//  于是需要这两个方向的转换函数。转换失败时返回空串（实际场景中几乎不会失败，
//  因为输入都来自本进程，编码一定合法）。
// ============================================================================

// 宽字符 → UTF-8 字节串。用于“写文件”。
std::string WToU8(const std::wstring& s);

// UTF-8 字节串 → 宽字符。用于“读文件”。
std::wstring U8ToW(const std::string& s);
