#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

// ============================================================================
//  render.h — 渲染核心：把一段 LaTeX 数学公式变成一张位图
//
//  实现思路：不自己写排版引擎，而是“借用本机安装的 TeX Live”：
//      拼接 .tex 文档 → 调 pdflatex 生成 PDF → 调 pdftocairo 转成 PNG
//      → 用 GDI+ 读入 PNG 得到 Bitmap
//  好处是排版质量 = 真 LaTeX（100% 保真）；代价是运行时依赖 TeX Live。
//
//  这是本模块对外唯一的主入口（另有 CleanRenderCache 供“重置”使用）。
// ============================================================================

// 把一段 LaTeX 数学内容渲染成 GDI+ Bitmap。
//
// 参数：
//   latex       —— 公式源码。若用户自带 $$..$$ / \[..\] / $..$ 定界符，会先自动剥离；
//                  剥完按**内联数学** $\displaystyle ...$ 排版，而不是 \[ ... \]
//                  —— 后者生成的盒子宽度是整整一行（\hsize），公式在其中居中，
//                  图的两侧会多出一大片与公式无关的空白；
//                  含 \\ 的多行内容改用 gathered 包裹（宽度 = 最宽一行）；
//                  若是 align/equation 等“外层数学环境”，则直接使用、不再包裹。
//   preamble    —— 导言区（\usepackage 与自定义命令），原样插入文档头部。
//   texliveBin  —— 含 pdflatex.exe / pdftocairo.exe 的目录。
//   errorOut    —— 出错时写入中文错误说明（可为 nullptr）。
//
// 返回：成功时返回 new 出来的 Bitmap（**调用者负责 delete**）；
//       位图已经过“按墨迹二次裁剪”（TrimToInk），四周只留 3px 空白，
//       尺寸即公式本身的大小；
//       失败返回 nullptr。空输入返回 nullptr 且 errorOut 为空串（表示“无内容”，
//       不是错误），调用方据此显示占位提示而不是报错。
Gdiplus::Bitmap* RenderFormula(const std::wstring& latex,
                               const std::wstring& preamble,
                               const std::wstring& texliveBin,
                               std::wstring* errorOut);

// 清空渲染缓存：删除临时目录里所有 formula.* 文件。
// 供“重置”按钮使用——把上一次编译的残留（尤其是出错的 .aux）清干净，
// 避免它们影响下一次渲染。
void CleanRenderCache();
