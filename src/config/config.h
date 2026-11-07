#pragma once
#include <string>

// ============================================================================
//  config.h — 配置数据结构
//
//  所有设置都会被持久化到 exe 同目录的 CopyMath.cfg（UTF-8 文本）：
//    · TeX Live 的 bin 目录（必须包含 pdflatex.exe 与 pdftocairo.exe）
//    · 导出图片的默认目录
//    · 导言区（\usepackage 与自定义命令），用于支持额外宏包与自定义符号
//    · 输出分辨率 DPI（复制与保存共用）
//    · 是否自动记录日志、日志文件位置
//    · 窗口是否“总是置顶”
//
//  v1.2 相对 v1.1 新增了后四项设置。
// ============================================================================

// 出厂默认导言区：一次引入数学排版里最常用的几个宏包。
//    mathtools  amsmath 的增强版（会自动加载 amsmath）
//    amssymb    额外数学符号（会自动加载 amsfonts）
//    physics    物理公式常用命令（\dv \pdv \bra \ket \qty ...）
//    bm         粗体数学
//    mathrsfs   花体字母 \mathscr
//    esint      更多积分号（\oiint \oiiint ...）
//    tensor     张量指标 \tensor / \indices
//    yhmath     大尺寸括号、根号等（Y&Y 数学扩展）
//  用户可以在“设置 → 导言区”里随意增删。
extern const wchar_t* const kDefaultPreamble;

// 输出分辨率的允许范围（同时被设置对话框的校验与 render.cpp 的钳制使用）
const int kDpiMin = 72;
const int kDpiMax = 1200;
const int kDpiDefault = 300;

struct Config {
    std::wstring texlive;     // TeX Live 的 bin 目录，例如 C:\texlive\2024\bin\windows
    std::wstring exportPath;  // “保存图片”写出的目录；为空则提示用户先去设置
    std::wstring preamble;    // 导言区内容，会被原样插入生成的 .tex 文档

    // ---- v1.2 新增 ----
    int  dpi = kDpiDefault;   // 出图分辨率：pdftocairo 的 -r 参数，复制与保存共用
    bool logEnabled = false;  // 是否把每次“复制/保存”的公式追加到日志文件
    std::wstring logPath;     // 日志文件路径，**默认留空**（由用户在“设置”里指定）；
                              // 只填文件夹时，程序会在该文件夹下用 logs.txt
    bool alwaysOnTop = false; // 窗口是否置顶（对应主界面的“总是置顶”按钮）
};

// 读取配置；文件不存在或打开失败返回 false（此时调用方应填入默认值）。
// 注意：解析失败不会报错，未出现在文件里的字段保持调用方传入的值不变
// （所以调用方应先给 cfg 一份“出厂默认”）。
// 唯一例外是 preamble：文件里出现 PREAMBLE_START 块时，该块会**整体替换**
// 调用方预置的值（而不是追加），避免默认宏包与用户宏包重复。
bool LoadConfig(Config& cfg);

// 写出配置。返回 false 表示文件无法写入（例如目录只读）。
bool SaveConfig(const Config& cfg);
