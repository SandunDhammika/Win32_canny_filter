#define NOMINMAX
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr wchar_t kWindowClassName[] = L"Win32CannyFilterWindow";
constexpr wchar_t kWindowTitle[] = L"Win32 Canny Filter (Clipboard)";

constexpr int kControlAreaHeight = 58;
constexpr int kMargin = 8;

enum MenuId : UINT {
    IDM_CLIP_PASTE_INPUT = 1001,
    IDM_CLIP_PASTE_OUTPUT,
    IDM_CLIP_COPY_INPUT,
    IDM_CLIP_COPY_OUTPUT,
    IDM_RUN_CANNY,
    IDM_EXIT
};

enum ControlId : int {
    IDC_EDIT_LOW = 2001,
    IDC_EDIT_HIGH,
    IDC_BTN_APPLY
};

struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool Empty() const {
        return width <= 0 || height <= 0 || pixels.empty();
    }
};

struct AppState {
    GrayImage input;
    GrayImage output;
    int lowThreshold = 50;
    int highThreshold = 120;

    HWND hEditLow = nullptr;
    HWND hEditHigh = nullptr;
    HWND hBtnApply = nullptr;
};

AppState g_state;

int ClampThreshold(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

void SetThresholdEdits(HWND hwnd) {
    wchar_t buf[16] = {};
    _snwprintf_s(buf, _TRUNCATE, L"%d", g_state.lowThreshold);
    SetWindowTextW(g_state.hEditLow, buf);
    _snwprintf_s(buf, _TRUNCATE, L"%d", g_state.highThreshold);
    SetWindowTextW(g_state.hEditHigh, buf);
    InvalidateRect(hwnd, nullptr, TRUE);
}

bool ReadThresholdsFromEdits() {
    wchar_t buf[32] = {};
    GetWindowTextW(g_state.hEditLow, buf, static_cast<int>(std::size(buf)));
    int low = _wtoi(buf);

    ZeroMemory(buf, sizeof(buf));
    GetWindowTextW(g_state.hEditHigh, buf, static_cast<int>(std::size(buf)));
    int high = _wtoi(buf);

    low = ClampThreshold(low);
    high = ClampThreshold(high);

    if (low > high) {
        std::swap(low, high);
    }

    g_state.lowThreshold = low;
    g_state.highThreshold = high;
    return true;
}

GrayImage BitmapToGray(HBITMAP hBitmap) {
    GrayImage img;
    if (!hBitmap) {
        return img;
    }

    BITMAP bm = {};
    if (!GetObjectW(hBitmap, sizeof(bm), &bm)) {
        return img;
    }

    const int w = bm.bmWidth;
    const int h = bm.bmHeight;
    if (w <= 0 || h <= 0) {
        return img;
    }

    HDC hdc = GetDC(nullptr);
    HDC memdc = CreateCompatibleDC(hdc);
    HGDIOBJ oldObj = SelectObject(memdc, hBitmap);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> bgra(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    if (!GetDIBits(memdc, hBitmap, 0, static_cast<UINT>(h), bgra.data(), &bmi, DIB_RGB_COLORS)) {
        SelectObject(memdc, oldObj);
        DeleteDC(memdc);
        ReleaseDC(nullptr, hdc);
        return img;
    }

    SelectObject(memdc, oldObj);
    DeleteDC(memdc);
    ReleaseDC(nullptr, hdc);

    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4u;
            const uint8_t b = bgra[i + 0];
            const uint8_t g = bgra[i + 1];
            const uint8_t r = bgra[i + 2];
            const int gray = (77 * r + 150 * g + 29 * b) >> 8;
            img.pixels[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(gray);
        }
    }

    return img;
}

HBITMAP GrayToBitmap(const GrayImage& img) {
    if (img.Empty()) {
        return nullptr;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.width;
    bmi.bmiHeader.biHeight = -img.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (!hBitmap || !dibBits) {
        if (hBitmap) {
            DeleteObject(hBitmap);
        }
        return nullptr;
    }

    auto* out = static_cast<uint8_t*>(dibBits);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const uint8_t v = img.pixels[static_cast<size_t>(y) * img.width + x];
            const size_t i = (static_cast<size_t>(y) * img.width + x) * 4u;
            out[i + 0] = v;
            out[i + 1] = v;
            out[i + 2] = v;
            out[i + 3] = 255;
        }
    }

    return hBitmap;
}

GrayImage DibToGrayImage(const void* dibData, SIZE_T dibSize) {
    GrayImage img;
    if (!dibData || dibSize < sizeof(BITMAPINFOHEADER)) {
        return img;
    }

    const auto* bih = static_cast<const BITMAPINFOHEADER*>(dibData);
    if (bih->biSize < sizeof(BITMAPINFOHEADER) || bih->biPlanes != 1 || bih->biWidth <= 0 || bih->biHeight == 0) {
        return img;
    }

    const int width = bih->biWidth;
    const int height = (bih->biHeight < 0) ? -bih->biHeight : bih->biHeight;
    if (height <= 0) {
        return img;
    }

    size_t colorEntries = 0;
    if (bih->biBitCount <= 8) {
        colorEntries = bih->biClrUsed ? bih->biClrUsed : (1u << bih->biBitCount);
    } else if (bih->biCompression == BI_BITFIELDS && (bih->biBitCount == 16 || bih->biBitCount == 32)) {
        colorEntries = 3;
    }

    const size_t headerBytes = static_cast<size_t>(bih->biSize) + colorEntries * sizeof(RGBQUAD);
    if (headerBytes >= dibSize) {
        return img;
    }

    const auto* bits = static_cast<const uint8_t*>(dibData) + headerBytes;
    const size_t bitsSize = dibSize - headerBytes;
    if (bitsSize == 0) {
        return img;
    }

    HDC hdc = GetDC(nullptr);
    HBITMAP hBitmap = CreateDIBitmap(
        hdc,
        bih,
        CBM_INIT,
        bits,
        reinterpret_cast<const BITMAPINFO*>(bih),
        DIB_RGB_COLORS
    );
    ReleaseDC(nullptr, hdc);

    if (!hBitmap) {
        return img;
    }

    img = BitmapToGray(hBitmap);
    DeleteObject(hBitmap);
    return img;
}

HGLOBAL GrayToClipboardDib(const GrayImage& img) {
    if (img.Empty()) {
        return nullptr;
    }

    const int width = img.width;
    const int height = img.height;
    const size_t stride = static_cast<size_t>(width) * 4u;
    const size_t pixelBytes = stride * static_cast<size_t>(height);
    const size_t totalBytes = sizeof(BITMAPINFOHEADER) + pixelBytes;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, totalBytes);
    if (!hMem) {
        return nullptr;
    }

    auto* base = static_cast<uint8_t*>(GlobalLock(hMem));
    if (!base) {
        GlobalFree(hMem);
        return nullptr;
    }

    auto* bih = reinterpret_cast<BITMAPINFOHEADER*>(base);
    ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = width;
    bih->biHeight = height;
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = static_cast<DWORD>(pixelBytes);

    auto* dstBits = base + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < height; ++y) {
        uint8_t* row = dstBits + static_cast<size_t>(height - 1 - y) * stride;
        for (int x = 0; x < width; ++x) {
            const uint8_t v = img.pixels[static_cast<size_t>(y) * width + x];
            const size_t i = static_cast<size_t>(x) * 4u;
            row[i + 0] = v;
            row[i + 1] = v;
            row[i + 2] = v;
            row[i + 3] = 0;
        }
    }

    GlobalUnlock(hMem);
    return hMem;
}

bool PasteClipboardToImage(HWND hwnd, GrayImage& dst) {
    if (!OpenClipboard(hwnd)) {
        MessageBoxW(hwnd, L"Cannot open clipboard.", L"Clipboard", MB_ICONERROR);
        return false;
    }

    GrayImage img;

    if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE hDib = GetClipboardData(CF_DIB);
        if (hDib) {
            const void* dib = GlobalLock(hDib);
            if (dib) {
                img = DibToGrayImage(dib, GlobalSize(hDib));
                GlobalUnlock(hDib);
            }
        }
    }

    if (img.Empty() && IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hBitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        if (hBitmap) {
            img = BitmapToGray(hBitmap);
        }
    }

    CloseClipboard();

    if (img.Empty()) {
        MessageBoxW(hwnd, L"Clipboard does not contain a supported bitmap format (CF_DIB/CF_BITMAP).", L"Clipboard", MB_ICONWARNING);
        return false;
    }

    dst = std::move(img);
    return true;
}

bool CopyImageToClipboard(HWND hwnd, const GrayImage& src) {
    if (src.Empty()) {
        MessageBoxW(hwnd, L"No image available.", L"Clipboard", MB_ICONWARNING);
        return false;
    }

    HBITMAP hBitmap = GrayToBitmap(src);
    HGLOBAL hDib = GrayToClipboardDib(src);
    if (!hBitmap && !hDib) {
        MessageBoxW(hwnd, L"Failed to convert image for clipboard.", L"Clipboard", MB_ICONERROR);
        return false;
    }

    if (!OpenClipboard(hwnd)) {
        if (hBitmap) {
            DeleteObject(hBitmap);
        }
        if (hDib) {
            GlobalFree(hDib);
        }
        MessageBoxW(hwnd, L"Cannot open clipboard.", L"Clipboard", MB_ICONERROR);
        return false;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        if (hBitmap) {
            DeleteObject(hBitmap);
        }
        if (hDib) {
            GlobalFree(hDib);
        }
        MessageBoxW(hwnd, L"Failed to clear clipboard.", L"Clipboard", MB_ICONERROR);
        return false;
    }

    bool ok = false;
    if (hDib) {
        const bool setDibOk = (SetClipboardData(CF_DIB, hDib) != nullptr);
        ok = ok || setDibOk;
        if (setDibOk) {
            hDib = nullptr;
        }
    }

    if (hBitmap) {
        const bool setBitmapOk = (SetClipboardData(CF_BITMAP, hBitmap) != nullptr);
        ok = ok || setBitmapOk;
        if (setBitmapOk) {
            hBitmap = nullptr;
        }
    }

    if (!ok) {
        CloseClipboard();
        if (hBitmap) {
            DeleteObject(hBitmap);
        }
        if (hDib) {
            GlobalFree(hDib);
        }
        MessageBoxW(hwnd, L"Failed to set clipboard image data.", L"Clipboard", MB_ICONERROR);
        return false;
    }

    CloseClipboard();
    return true;
}

GrayImage CannyFilter(const GrayImage& src, int lowThreshold, int highThreshold) {
    GrayImage out;
    if (src.Empty() || src.width < 3 || src.height < 3) {
        return out;
    }

    const int w = src.width;
    const int h = src.height;
    const size_t total = static_cast<size_t>(w) * h;

    std::vector<float> blur(total, 0.0f);
    std::vector<float> temp(total, 0.0f);

    const int k[5] = {2, 4, 5, 4, 2};
    const float invNorm = 1.0f / 17.0f;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int i = -2; i <= 2; ++i) {
                int xx = std::clamp(x + i, 0, w - 1);
                sum += static_cast<float>(src.pixels[static_cast<size_t>(y) * w + xx]) * static_cast<float>(k[i + 2]);
            }
            temp[static_cast<size_t>(y) * w + x] = sum * invNorm;
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int i = -2; i <= 2; ++i) {
                int yy = std::clamp(y + i, 0, h - 1);
                sum += temp[static_cast<size_t>(yy) * w + x] * static_cast<float>(k[i + 2]);
            }
            blur[static_cast<size_t>(y) * w + x] = sum * invNorm;
        }
    }

    std::vector<float> mag(total, 0.0f);
    std::vector<uint8_t> dir(total, 0);

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const float a00 = blur[static_cast<size_t>(y - 1) * w + (x - 1)];
            const float a01 = blur[static_cast<size_t>(y - 1) * w + x];
            const float a02 = blur[static_cast<size_t>(y - 1) * w + (x + 1)];
            const float a10 = blur[static_cast<size_t>(y) * w + (x - 1)];
            const float a12 = blur[static_cast<size_t>(y) * w + (x + 1)];
            const float a20 = blur[static_cast<size_t>(y + 1) * w + (x - 1)];
            const float a21 = blur[static_cast<size_t>(y + 1) * w + x];
            const float a22 = blur[static_cast<size_t>(y + 1) * w + (x + 1)];

            const float gx = -a00 + a02 - 2.0f * a10 + 2.0f * a12 - a20 + a22;
            const float gy = a00 + 2.0f * a01 + a02 - a20 - 2.0f * a21 - a22;

            const float m = std::sqrt(gx * gx + gy * gy);
            mag[static_cast<size_t>(y) * w + x] = m;

            float angle = std::atan2(gy, gx) * 180.0f / 3.14159265f;
            if (angle < 0.0f) {
                angle += 180.0f;
            }

            uint8_t sector = 0;
            if ((angle >= 0.0f && angle < 22.5f) || (angle >= 157.5f && angle <= 180.0f)) {
                sector = 0;
            } else if (angle >= 22.5f && angle < 67.5f) {
                sector = 1;
            } else if (angle >= 67.5f && angle < 112.5f) {
                sector = 2;
            } else {
                sector = 3;
            }
            dir[static_cast<size_t>(y) * w + x] = sector;
        }
    }

    std::vector<float> nms(total, 0.0f);

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const float m = mag[idx];
            float m1 = 0.0f;
            float m2 = 0.0f;

            switch (dir[idx]) {
                case 0:
                    m1 = mag[idx - 1];
                    m2 = mag[idx + 1];
                    break;
                case 1:
                    m1 = mag[static_cast<size_t>(y - 1) * w + (x + 1)];
                    m2 = mag[static_cast<size_t>(y + 1) * w + (x - 1)];
                    break;
                case 2:
                    m1 = mag[static_cast<size_t>(y - 1) * w + x];
                    m2 = mag[static_cast<size_t>(y + 1) * w + x];
                    break;
                case 3:
                    m1 = mag[static_cast<size_t>(y - 1) * w + (x - 1)];
                    m2 = mag[static_cast<size_t>(y + 1) * w + (x + 1)];
                    break;
                default:
                    break;
            }

            if (m >= m1 && m >= m2) {
                nms[idx] = m;
            }
        }
    }

    std::vector<uint8_t> edgeClass(total, 0);
    std::queue<size_t> strong;

    for (size_t i = 0; i < total; ++i) {
        if (nms[i] >= static_cast<float>(highThreshold)) {
            edgeClass[i] = 2;
            strong.push(i);
        } else if (nms[i] >= static_cast<float>(lowThreshold)) {
            edgeClass[i] = 1;
        }
    }

    auto pushWeakNeighbor = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return;
        }
        const size_t nidx = static_cast<size_t>(y) * w + x;
        if (edgeClass[nidx] == 1) {
            edgeClass[nidx] = 2;
            strong.push(nidx);
        }
    };

    while (!strong.empty()) {
        const size_t idx = strong.front();
        strong.pop();

        const int y = static_cast<int>(idx / w);
        const int x = static_cast<int>(idx % w);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                pushWeakNeighbor(x + dx, y + dy);
            }
        }
    }

    out.width = w;
    out.height = h;
    out.pixels.resize(total, 0);

    for (size_t i = 0; i < total; ++i) {
        out.pixels[i] = (edgeClass[i] == 2) ? 255 : 0;
    }

    return out;
}

void RunCannyAndRefresh(HWND hwnd) {
    if (g_state.input.Empty()) {
        MessageBoxW(hwnd, L"Paste an input image first.", L"Canny", MB_ICONINFORMATION);
        return;
    }

    ReadThresholdsFromEdits();
    g_state.output = CannyFilter(g_state.input, g_state.lowThreshold, g_state.highThreshold);
    SetThresholdEdits(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void DrawGrayImage(HDC hdc, const RECT& panelRect, const GrayImage& img, const wchar_t* title) {
    HBRUSH bg = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdc, &panelRect, bg);
    DeleteObject(bg);

    Rectangle(hdc, panelRect.left, panelRect.top, panelRect.right, panelRect.bottom);

    RECT textRect = panelRect;
    textRect.left += 6;
    textRect.top += 4;
    DrawTextW(hdc, title, -1, &textRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

    if (img.Empty()) {
        RECT hint = panelRect;
        hint.top += 28;
        DrawTextW(hdc, L"No image", -1, &hint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    RECT drawRect = panelRect;
    drawRect.left += 8;
    drawRect.right -= 8;
    drawRect.top += 28;
    drawRect.bottom -= 8;

    const int areaW = drawRect.right - drawRect.left;
    const int areaH = drawRect.bottom - drawRect.top;
    if (areaW <= 0 || areaH <= 0) {
        return;
    }

    const double sx = static_cast<double>(areaW) / img.width;
    const double sy = static_cast<double>(areaH) / img.height;
    const double s = std::min(sx, sy);

    const int drawW = std::max(1, static_cast<int>(img.width * s));
    const int drawH = std::max(1, static_cast<int>(img.height * s));
    const int dx = drawRect.left + (areaW - drawW) / 2;
    const int dy = drawRect.top + (areaH - drawH) / 2;

    std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * img.height * 3u);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const uint8_t v = img.pixels[static_cast<size_t>(y) * img.width + x];
            const size_t i = (static_cast<size_t>(y) * img.width + x) * 3u;
            rgb[i + 0] = v;
            rgb[i + 1] = v;
            rgb[i + 2] = v;
        }
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.width;
    bmi.bmiHeader.biHeight = -img.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(
        hdc,
        dx,
        dy,
        drawW,
        drawH,
        0,
        0,
        img.width,
        img.height,
        rgb.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void LayoutControls(HWND hwnd) {
    RECT rc = {};
    GetClientRect(hwnd, &rc);

    const int y = 14;
    const int h = 24;

    MoveWindow(g_state.hEditLow, 120, y, 60, h, TRUE);
    MoveWindow(g_state.hEditHigh, 295, y, 60, h, TRUE);
    MoveWindow(g_state.hBtnApply, 380, y - 1, 80, h + 2, TRUE);

    InvalidateRect(hwnd, nullptr, TRUE);
}

HMENU BuildMenu() {
    HMENU menuBar = CreateMenu();
    HMENU menuClipboard = CreatePopupMenu();
    HMENU menuProcess = CreatePopupMenu();

    AppendMenuW(menuClipboard, MF_STRING, IDM_CLIP_PASTE_INPUT, L"Paste to Input\tCtrl+V");
    AppendMenuW(menuClipboard, MF_STRING, IDM_CLIP_PASTE_OUTPUT, L"Paste to Output");
    AppendMenuW(menuClipboard, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menuClipboard, MF_STRING, IDM_CLIP_COPY_INPUT, L"Copy Input\tCtrl+Shift+C");
    AppendMenuW(menuClipboard, MF_STRING, IDM_CLIP_COPY_OUTPUT, L"Copy Output\tCtrl+C");

    AppendMenuW(menuProcess, MF_STRING, IDM_RUN_CANNY, L"Run Canny\tF5");
    AppendMenuW(menuProcess, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menuProcess, MF_STRING, IDM_EXIT, L"Exit");

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(menuClipboard), L"Clipboard");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(menuProcess), L"Process");

    return menuBar;
}

void DrawTopLabels(HDC hdc) {
    RECT lowLabel{16, 16, 115, 40};
    RECT highLabel{200, 16, 292, 40};
    DrawTextW(hdc, L"Low Threshold:", -1, &lowLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, L"High Threshold:", -1, &highLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc = {};
            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_STANDARD_CLASSES;
            InitCommonControlsEx(&icc);

            SetMenu(hwnd, BuildMenu());

            g_state.hEditLow = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"50",
                WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NUMBER,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(IDC_EDIT_LOW),
                GetModuleHandleW(nullptr),
                nullptr
            );

            g_state.hEditHigh = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"120",
                WS_CHILD | WS_VISIBLE | ES_LEFT | ES_NUMBER,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(IDC_EDIT_HIGH),
                GetModuleHandleW(nullptr),
                nullptr
            );

            g_state.hBtnApply = CreateWindowExW(
                0,
                L"BUTTON",
                L"Apply",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                hwnd,
                reinterpret_cast<HMENU>(IDC_BTN_APPLY),
                GetModuleHandleW(nullptr),
                nullptr
            );

            LayoutControls(hwnd);
            return 0;
        }

        case WM_SIZE:
            LayoutControls(hwnd);
            return 0;

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT code = HIWORD(wParam);

            if (id == IDC_BTN_APPLY && code == BN_CLICKED) {
                RunCannyAndRefresh(hwnd);
                return 0;
            }

            switch (id) {
                case IDM_CLIP_PASTE_INPUT:
                    if (PasteClipboardToImage(hwnd, g_state.input)) {
                        g_state.output = CannyFilter(g_state.input, g_state.lowThreshold, g_state.highThreshold);
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                    return 0;

                case IDM_CLIP_PASTE_OUTPUT:
                    if (PasteClipboardToImage(hwnd, g_state.output)) {
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                    return 0;

                case IDM_CLIP_COPY_INPUT:
                    CopyImageToClipboard(hwnd, g_state.input);
                    return 0;

                case IDM_CLIP_COPY_OUTPUT:
                    CopyImageToClipboard(hwnd, g_state.output);
                    return 0;

                case IDM_RUN_CANNY:
                    RunCannyAndRefresh(hwnd);
                    return 0;

                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    return 0;

                default:
                    break;
            }
            break;
        }

        case WM_KEYDOWN:
            if (wParam == VK_F5) {
                RunCannyAndRefresh(hwnd);
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc = {};
            GetClientRect(hwnd, &rc);

            HBRUSH bg = CreateSolidBrush(RGB(230, 230, 230));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            DrawTopLabels(hdc);

            RECT area = rc;
            area.top += kControlAreaHeight;
            area.left += kMargin;
            area.right -= kMargin;
            area.bottom -= kMargin;

            const int split = (area.left + area.right) / 2;
            RECT leftPane = area;
            leftPane.right = split - 4;
            RECT rightPane = area;
            rightPane.left = split + 4;

            DrawGrayImage(hdc, leftPane, g_state.input, L"Input Image");
            DrawGrayImage(hdc, rightPane, g_state.output, L"Canny Output");

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"Error", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1200,
        720,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) && msg.wParam == 'V') {
                SendMessageW(hwnd, WM_COMMAND, IDM_CLIP_PASTE_INPUT, 0);
                continue;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && msg.wParam == 'C') {
                SendMessageW(hwnd, WM_COMMAND, IDM_CLIP_COPY_INPUT, 0);
                continue;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && msg.wParam == 'C') {
                SendMessageW(hwnd, WM_COMMAND, IDM_CLIP_COPY_OUTPUT, 0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
