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
