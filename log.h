#pragma once
#include <string>

// ============================================================================
//  log.h — 使用日志（v1.2 新增）
//
//  目的：把每一次“复制图片 / 保存图片”的公式**追加**到一个文本文件里，
//  方便事后回溯——“刚才那个公式是怎么写的来着？”
//
//  日志是纯追加的 UTF-8 文本，每条记录一个区块，格式见 log.cpp 顶部说明。
//  是否记录由“设置 → 自动记录日志”开关控制；位置由“设置 → 日志文件路径”决定。
// ============================================================================

// 把用户在设置里填的“日志位置”解析成真正的文件路径：
//   · 空串                     → 空串（表示没配置）
//   · 以 '\' 或 '/' 结尾        → 该目录下的 logs.txt（用户只填了文件夹）
//   · 指向一个已存在的目录       → 同上
//   · 其余情况                  → 原样当作文件路径（例如 D:\pics\mylog.txt）
std::wstring ResolveLogFilePath(const std::wstring& configured);

// 追加一条使用记录。返回 false 表示目录建不出来或文件写不进去。
//
//   logFile   —— 已由 ResolveLogFilePath 处理过的完整文件路径
//   action    —— L"复制图片" / L"保存图片"
//   latex     —— 用户输入的原始公式源码（原样写入，含换行）
//   dpi       —— 本次出图分辨率
//   w, h      —— 图片像素尺寸
//   imagePath —— 保存出来的 PNG 路径；复制到剪贴板时传空串
bool AppendUsageLog(const std::wstring& logFile,
                    const std::wstring& action,
                    const std::wstring& latex,
                    int dpi, int w, int h,
                    const std::wstring& imagePath);
