#pragma once
#include <windows.h>
#include <gdiplus.h>

// ============================================================================
//  clipboard.h — 把公式图片写入系统剪贴板
// ============================================================================

// 把一个 GDI+ Bitmap 以 CF_DIB 格式放进剪贴板。
//
// 为什么选 CF_DIB（而不是 CF_BITMAP / CF_DIBV5 / PNG）：
//   CF_DIB 是“设备无关位图”的内存结构，兼容性最好 —— Word、微信、画图、
//   PowerPoint 都能直接粘贴；CF_BITMAP 是 GDI 句柄，跨进程较麻烦；PNG 格式
//   很多老程序不认。缺点是 CF_DIB 不带 Alpha，所以图片是白底不透明的。
//
// dpi 会写进 BITMAPINFOHEADER 的 biXPelsPerMeter / biYPelsPerMeter。
// 这不是可有可无的字段：Word / PowerPoint 粘贴图片时会按它换算“物理尺寸”，
// 不填的话（=0）有些版本会按 96 DPI 处理，300 DPI 的公式粘进文档后会显得特别大。
//
// hwnd 为剪贴板所有者窗口（可为 NULL）；成功返回 true。
bool CopyBitmapToClipboard(HWND hwnd, Gdiplus::Bitmap* bmp, int dpi);
