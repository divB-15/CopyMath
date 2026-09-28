#include "clipboard.h"
#include <windows.h>
#include <gdiplus.h>

// ============================================================================
//  clipboard.cpp — 把 GDI+ Bitmap 以 CF_DIB 写入剪贴板
//
//  CF_DIB 的内存布局（一块 HGLOBAL）：
//
//      [ BITMAPINFOHEADER ][ 像素数据 ... ]
//
//  也就是“一个 BMP 文件去掉 14 字节的 BITMAPFILEHEADER”。像素数据按
//  每行 4 字节对齐（stride）排列。
//
//  我们统一按 32 位、top-down 方式组织：把 BITMAPINFOHEADER.biHeight 写成
//  负数表示“第一行在最上面”（与 GDI+ 的内存顺序一致），否则上下会颠倒。
//
//  v1.2：新增 dpi 参数，写进 biXPelsPerMeter / biYPelsPerMeter。
// ============================================================================

bool CopyBitmapToClipboard(HWND hwnd, Gdiplus::Bitmap* bmp, int dpi) {
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) return false;

    int w = bmp->GetWidth();
    int h = bmp->GetHeight();

    // 1) 用 LockBits 把像素读出来（锁定为 32bpp ARGB，保证格式统一）
    Gdiplus::BitmapData bd;
    Gdiplus::Rect r(0, 0, w, h);
    if (bmp->LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
        return false;

    // 2) 申请剪贴板用的全局内存：头 + 像素
    LONG biHeight = -(LONG)h;                            // 负值 = top-down，避免图像上下翻转
    DWORD headerSize = sizeof(BITMAPINFOHEADER);
    DWORD pixelSize = (DWORD)(bd.Stride * (UINT)h);      // bd.Stride 已含行对齐
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, headerSize + pixelSize);
    if (!hMem) { bmp->UnlockBits(&bd); return false; }

    // 3) 填 BITMAPINFOHEADER，再把像素拷到它后面
    //    1 英寸 = 0.0254 m，所以 每米像素数 = dpi / 0.0254 = dpi × 39.3700787
    LONG ppm = (LONG)((dpi > 0 ? dpi : 96) * 39.3700787 + 0.5);
    BYTE* pMem = (BYTE*)GlobalLock(hMem);
    BITMAPINFOHEADER* bi = (BITMAPINFOHEADER*)pMem;
    bi->biSize = sizeof(BITMAPINFOHEADER);
    bi->biWidth = w;
    bi->biHeight = biHeight;
    bi->biPlanes = 1;
    bi->biBitCount = 32;
    bi->biCompression = BI_RGB;                          // 不压缩
    bi->biSizeImage = pixelSize;
    bi->biXPelsPerMeter = ppm;                           // 供 Word / PPT 换算物理尺寸
    bi->biYPelsPerMeter = ppm;
    memcpy(pMem + headerSize, bd.Scan0, pixelSize);
    GlobalUnlock(hMem);
    bmp->UnlockBits(&bd);

    // 4) 交给剪贴板。
    //    注意：SetClipboardData 成功后，内存所有权转移给系统，
    //    我们不能（也不需要）再 GlobalFree，所以只在失败分支释放。
    if (!OpenClipboard(hwnd)) { GlobalFree(hMem); return false; }
    EmptyClipboard();                                    // 必须先清空，才能成为所有者
    HANDLE res = SetClipboardData(CF_DIB, hMem);
    CloseClipboard();
    if (!res) { GlobalFree(hMem); return false; }
    return true;
}
