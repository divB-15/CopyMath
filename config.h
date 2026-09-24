#pragma once
#include <string>

// ============================================================================
//  config.h — 配置数据结构
//
//  三项设置会被持久化到 exe 同目录的 copymath.cfg（UTF-8 文本）：
//    · TeX Live 的 bin 目录（必须包含 pdflatex.exe 与 pdftocairo.exe）
//    · 导出图片的默认目录
//    · 导言区（\usepackage 与自定义命令），用于支持额外宏包与自定义符号
// ============================================================================

struct Config {
    std::wstring texlive;     // TeX Live 的 bin 目录，例如 C:\texlive\2024\bin\windows
    std::wstring exportPath;  // “保存图片”写出的目录；为空则提示用户先去设置
    std::wstring preamble;    // 导言区内容，会被原样插入生成的 .tex 文档
};

// 读取配置；文件不存在或打开失败返回 false（此时调用方应填入默认值）。
// 注意：解析失败不会报错，只是对应字段保持默认。
bool LoadConfig(Config& cfg);

// 写出配置。返回 false 表示文件无法写入（例如目录只读）。
bool SaveConfig(const Config& cfg);
