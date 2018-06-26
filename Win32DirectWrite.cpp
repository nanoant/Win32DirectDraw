// just build with: cl Win32DirectWrite.cpp

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <ShellScalingApi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// Include the v6 common controls in the manifest
#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")

DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);

#define DPI_AWARENESS_CONTEXT_UNAWARE              ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE         ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE    ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-3)
typedef DPI_AWARENESS_CONTEXT (WINAPI* SetThreadDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
typedef LRESULT (WINAPI* GetDpiForMonitorProc)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

static wchar_t* wszText = _wcsdup(L"1. m_pRenderTarget->DrawText(\"Hello World\");");
UINT32 cTextLength = (UINT32)wcslen(wszText);

const UINT baseDpi = 96;
UINT dpiX = baseDpi;
UINT dpiY = baseDpi;
FLOAT dpiScaleX = 1.0f;
FLOAT dpiScaleY = 1.0f;

HWND CreateMainWindow(HINSTANCE hInstance);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CreateDirectWrite();
BOOL GetDesktopDpi();

BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct);
void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
void OnSize(HWND hwnd, UINT state, int cx, int cy);
void OnDpiChanged(HWND hwnd, UINT newXDpi, UINT newYDpi, RECT const &newScaledWindowRect);
void OnPaint(HWND hwnd);
void OnDestroy(HWND hwnd);

BOOL RecreateRenderTarget(const HWND hwnd, const D2D1_SIZE_U& size);

void Debug(LPCTSTR lpszFormat, ...);

INT WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ INT nCmdShow) {
    // Initialize the common controls
    INITCOMMONCONTROLSEX icex;
    ZeroMemory(&icex, sizeof(icex));
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    SetThreadDpiAwarenessContextProc SetThreadDpiAwarenessContext;
    HMODULE hUser32 = GetModuleHandleW(L"user32");
    if (hUser32) {
        // EnableNonClientDpiScaling = (EnableNonClientDpiScalingProc)GetProcAddress(hUser32, "EnableNonClientDpiScaling");
        SetThreadDpiAwarenessContext = (SetThreadDpiAwarenessContextProc)GetProcAddress(hUser32, "SetThreadDpiAwarenessContext");
        if (SetThreadDpiAwarenessContext) {
            DPI_AWARENESS_CONTEXT previousDpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    } else {
        MessageBox(NULL, "user32.dll not found.", "GetModuleHandleW Error", MB_ICONERROR);
        return 1;
    }

    if (!CreateDirectWrite()) {
        MessageBox(NULL,
            "DirectWrite failed.",
            "DirectWrite Error", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateMainWindow(hInstance);
    if (hwnd == 0) {
        return 1;
    }

    GetDpiForMonitorProc GetDpiForMonitor;
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        GetDpiForMonitor = (GetDpiForMonitorProc)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (GetDpiForMonitor) {
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            dpiScaleX = (FLOAT)dpiX / baseDpi;
            dpiScaleY = (FLOAT)dpiY / baseDpi;
            Debug("Win32DirectWrite: GetDpiForMonitor dpiX=%d dpiY=%d dpiScaleX=%f dpiScaleY=%f", dpiX, dpiY, dpiScaleX, dpiScaleY);
        }
    } else {
        MessageBox(NULL, "shcore.dll not found.", "LoadLibraryW Error", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    // Run the message loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    // Register the window class.
    LPCTSTR CLASS_NAME = "Window Class";

    WNDCLASS wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create the window.
    return CreateWindowEx(0, CLASS_NAME, "Application",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        320 * dpiScaleX, 300 * dpiScaleY,
        //CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);
};

#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn) ((fn)((hwnd), LOWORD(wParam), HIWORD(wParam), *((RECT*)(lParam))), 0L)

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        HANDLE_MSG(hwnd, WM_CREATE,     OnCreate);
        HANDLE_MSG(hwnd, WM_COMMAND,    OnCommand);
        HANDLE_MSG(hwnd, WM_SIZE,       OnSize);
        HANDLE_MSG(hwnd, WM_DPICHANGED, OnDpiChanged);
        HANDLE_MSG(hwnd, WM_PAINT,      OnPaint);
        HANDLE_MSG(hwnd, WM_DESTROY,    OnDestroy);
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static ID2D1Factory* pD2DFactory = NULL;
static IDWriteFactory* pDWriteFactory = NULL;
static IDWriteTextFormat* pTextFormat = NULL;
static ID2D1HwndRenderTarget* pRT = NULL;
static ID2D1SolidColorBrush* pBlackBrush = NULL;

enum RenderingParams {
    Default = 0,
    GDIClassic,
    GDINatural,
    Natural,
    Contrast2x,
    Contrast02,
    GammaHalf,
    Gamma2x,
    GDIClassicContrast2x,
    GDIClassicContrast02,
    RenderingParamsCount
};

static IDWriteRenderingParams* pRenderingParams[RenderingParamsCount] = {};

template <class T> void SafeRelease(T **ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

FLOAT ConvertPointSizeToDIP(FLOAT points) {
    return points * 96.0f / 72.0f;
}

BOOL CreateDirectWrite() {
    return
        SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            &pD2DFactory)) &&
        GetDesktopDpi() &&
        SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pDWriteFactory))) &&
        SUCCEEDED(pDWriteFactory->CreateRenderingParams(&pRenderingParams[Default])) &&
#define CUSTOM_RENDERING_PARAMS(params, gf, ef, rm) \
        SUCCEEDED(pDWriteFactory->CreateCustomRenderingParams( \
            pRenderingParams[Default]->GetGamma() * gf, \
            pRenderingParams[Default]->GetEnhancedContrast() * ef, \
            pRenderingParams[Default]->GetClearTypeLevel(), \
            pRenderingParams[Default]->GetPixelGeometry(), \
            rm, &pRenderingParams[params]))
        CUSTOM_RENDERING_PARAMS(GDIClassic,   1.f, 1.f, DWRITE_RENDERING_MODE_GDI_CLASSIC) &&
        CUSTOM_RENDERING_PARAMS(GDINatural,   1.f, 1.f, DWRITE_RENDERING_MODE_GDI_NATURAL) &&
        CUSTOM_RENDERING_PARAMS(Natural,      1.f, 1.f, DWRITE_RENDERING_MODE_NATURAL    ) &&
        CUSTOM_RENDERING_PARAMS(Contrast2x,   1.f, 2.f, DWRITE_RENDERING_MODE_NATURAL    ) &&
        CUSTOM_RENDERING_PARAMS(Contrast02,   1.f, .2f, DWRITE_RENDERING_MODE_NATURAL    ) &&
        CUSTOM_RENDERING_PARAMS(GammaHalf,    .5f, 1.f, DWRITE_RENDERING_MODE_NATURAL    ) &&
        CUSTOM_RENDERING_PARAMS(Gamma2x,      2.f, 1.f, DWRITE_RENDERING_MODE_NATURAL    ) &&
        CUSTOM_RENDERING_PARAMS(GDIClassicContrast2x, 1.f, 2.f, DWRITE_RENDERING_MODE_GDI_CLASSIC) &&
        CUSTOM_RENDERING_PARAMS(GDIClassicContrast02, 1.f, .5f, DWRITE_RENDERING_MODE_GDI_CLASSIC) &&
        SUCCEEDED(pDWriteFactory->CreateTextFormat(
            L"Consolas", // Font family name.
            NULL,        // Font collection (NULL sets it to use the system font collection).
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            ceilf(ConvertPointSizeToDIP(8)),
            L"en-us",
            &pTextFormat));
}

BOOL GetDesktopDpi() {
    FLOAT fDpiX, fDpiY;
    pD2DFactory->GetDesktopDpi(&fDpiX, &fDpiY);
    dpiX = fDpiX;
    dpiY = fDpiY;
    dpiScaleX = fDpiX / baseDpi;
    dpiScaleY = fDpiY / baseDpi;
    Debug("Win32DirectWrite: GetDesktopDpi fDpiX=%f fDpiY=%f dpiScaleX=%f dpiScaleY=%f", fDpiX, fDpiY, dpiScaleX, dpiScaleY);
    return TRUE;
}

BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    if (!RecreateRenderTarget(hwnd, size)) {
        MessageBox(NULL,
            "OnCreate failed.",
            "OnCreate Error", MB_ICONERROR);
        return FALSE;
    }

    return TRUE;
}

void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {
    Debug("Win32DirectWrite: OnCommand hwnd=0x%X id=%d hwndCtl=0x%X codeNotify=%d", hwnd, id, hwndCtl, codeNotify);
    switch (id) {}
}

void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    Debug("Win32DirectWrite: OnSize hwnd=0x%X cx=%d cy=%d", hwnd, cx, cy);
    if (state == SIZE_RESTORED || state == SIZE_MAXIMIZED) {}
    D2D1_SIZE_U size = D2D1::SizeU(cx, cy);
    RecreateRenderTarget(hwnd, size);
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
}

void OnDpiChanged(HWND hwnd, UINT newDpiX, UINT newDpiY, const RECT& newScaledWindowRect) {
    Debug("Win32DirectWrite: OnDpiChanged hwnd=0x%X newDpiX=%d newDpiY=%d", hwnd, newDpiX, newDpiY);
    dpiX = newDpiX;
    dpiY = newDpiY;
    dpiScaleX = (FLOAT)dpiX / baseDpi;
    dpiScaleY = (FLOAT)dpiY / baseDpi;
    Debug("Win32DirectWrite: dpiScaleX=%f dpiScaleY=%f", dpiScaleX, dpiScaleY);
    SetWindowPos(hwnd, HWND_TOP,
        newScaledWindowRect.left,
        newScaledWindowRect.top,
        newScaledWindowRect.right  - newScaledWindowRect.left,
        newScaledWindowRect.bottom - newScaledWindowRect.top,
        SWP_NOZORDER | SWP_NOACTIVATE); // NOTE: Will cause OnSize and RecreateRenderTarget
    RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
}

void OnPaintHDC(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    //FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW+1));
    EndPaint(hwnd, &ps);
}

void OnPaint(HWND hwnd) {
    //Debug("Win32DirectWrite: OnPaint hwnd=0x%X", hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_RECT_F layoutRect = D2D1::RectF(
        static_cast<FLOAT>(rc.left) / dpiScaleX,
        static_cast<FLOAT>(rc.top)  / dpiScaleY,
        static_cast<FLOAT>(rc.right - rc.left) / dpiScaleX,
        static_cast<FLOAT>(rc.bottom - rc.top) / dpiScaleY
    );
    pRT->BeginDraw();
    pRT->SetTransform(D2D1::IdentityMatrix());
    pRT->Clear(D2D1::ColorF(D2D1::ColorF::White));

#define RENDER_TEXT_T(s)   L##s
#define RENDER_TEXT_STR(s) RENDER_TEXT_T(#s)
#define RENDER_TEXT(index, params, ...) \
    wszText[0] = RENDER_TEXT_STR(index)[0]; \
    wszText[1] = index >= 10 ? RENDER_TEXT_STR(index)[1] : L'.'; \
    pRT->SetTransform(D2D1::Matrix3x2F::Translation(D2D1::SizeF(10.0f, 10.0f + (index - 1) * 20.0f))); \
    pRT->SetTextRenderingParams(pRenderingParams[params]); \
    pRT->DrawText( \
        wszText,     /* The string to render. */ \
        cTextLength, /* The string's length. */ \
        pTextFormat, /* The text format. */ \
        layoutRect,  /* The region of the window where the text will be rendered. */ \
        pBlackBrush, /* The brush used to draw the text. */ \
        ##__VA_ARGS__ \
    )
    RENDER_TEXT(1, Default);
    RENDER_TEXT(2, GDIClassic, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(3, GDINatural, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_NATURAL);
    RENDER_TEXT(4, GDINatural, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    RENDER_TEXT(5, Contrast02, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    RENDER_TEXT(6, Contrast02, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(7, Contrast2x, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(8, Gamma2x,    D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(9, GammaHalf,  D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(10, GDIClassicContrast02, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    RENDER_TEXT(11, GDIClassicContrast2x, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_GDI_CLASSIC);

    pRT->EndDraw();
}

void OnDestroy(HWND hwnd) {
    PostQuitMessage(0);
    SafeRelease(&pBlackBrush);
    for (int i = 0; i < RenderingParamsCount; ++i) {
        SafeRelease(&pRenderingParams[i]);
    }
    SafeRelease(&pRT);
    SafeRelease(&pTextFormat);
    SafeRelease(&pDWriteFactory);
    SafeRelease(&pD2DFactory);
}

BOOL RecreateRenderTarget(const HWND hwnd, const D2D1_SIZE_U& size) {
    SafeRelease(&pBlackBrush);
    SafeRelease(&pRT);
    D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties();
    renderTargetProperties.dpiX = dpiX;
    renderTargetProperties.dpiY = dpiY;
    return
        SUCCEEDED(pD2DFactory->CreateHwndRenderTarget(
            renderTargetProperties,
            D2D1::HwndRenderTargetProperties(
                hwnd,
                size),
            &pRT)) &&
        SUCCEEDED(pRT->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            &pBlackBrush));
}

void Debug(LPCTSTR lpszFormat, ...) {
    TCHAR szBuffer[512] = "\0";
    va_list args;
    va_start(args, lpszFormat);
    _vsnprintf(szBuffer, sizeof(szBuffer)/sizeof(*szBuffer), lpszFormat, args);
    OutputDebugString(szBuffer);
    va_end(args);
}
