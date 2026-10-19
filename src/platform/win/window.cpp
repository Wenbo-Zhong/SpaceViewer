#include "app.hpp"
#include "text.hpp"
#include "util.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct WinState {
  App app;
  HWND hwnd = nullptr;
  Input input;
  Host host;
  HDC memDc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ oldBitmap = nullptr;
  void* bits = nullptr;
  int bmpW = 0;
  int bmpH = 0;
  std::vector<HFONT> fonts;
  std::vector<int> fontPx;
  wchar_t highSurrogate = 0;
  LARGE_INTEGER qpf{};
};

double nowSec(WinState& state) {
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return static_cast<double>(counter.QuadPart) / static_cast<double>(state.qpf.QuadPart);
}

void enableHighDpi() {
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using SetContext = BOOL(WINAPI*)(HANDLE);
  auto setContext = reinterpret_cast<SetContext>(GetProcAddress(user, "SetProcessDpiAwarenessContext"));
  if (setContext) {
    if (setContext(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) return;
  }
  HMODULE shcore = LoadLibraryW(L"shcore.dll");
  if (shcore) {
    using SetAware = HRESULT(WINAPI*)(int);
    auto setAware = reinterpret_cast<SetAware>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if (setAware && SUCCEEDED(setAware(2))) return;
  }
  SetProcessDPIAware();
}

int windowDpi(HWND hwnd) {
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using GetDpi = UINT(WINAPI*)(HWND);
  auto getDpi = reinterpret_cast<GetDpi>(GetProcAddress(user, "GetDpiForWindow"));
  if (getDpi) {
    UINT dpi = getDpi(hwnd);
    if (dpi >= 96) return static_cast<int>(dpi);
  }
  HDC dc = GetDC(hwnd);
  int dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(hwnd, dc);
  return dpi > 0 ? dpi : 96;
}

void clearEdges(Input& input) {
  input.leftPressed = false;
  input.rightPressed = false;
  input.keyBackspace = false;
  input.keyDelete = false;
  input.keyEnter = false;
  input.keyTab = false;
  input.keyEsc = false;
  input.keyLeft = false;
  input.keyRight = false;
  input.keyHome = false;
  input.keyEnd = false;
  input.keyF5 = false;
  input.keyUpNav = false;
  input.keyBackNav = false;
  input.keyForwardNav = false;
  input.copy = false;
  input.paste = false;
  input.selectAll = false;
  input.openShortcut = false;
  input.typed.clear();
}

HFONT fontFor(WinState& state, int px) {
  if (px < 8) px = 8;
  for (size_t i = 0; i < state.fontPx.size(); ++i) {
    if (state.fontPx[i] == px) return state.fonts[i];
  }
  HFONT font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
  state.fonts.push_back(font);
  state.fontPx.push_back(px);
  return font;
}

void ensureBitmap(WinState& state, int width, int height) {
  if (state.memDc && state.bmpW == width && state.bmpH == height) return;
  if (state.memDc) {
    SelectObject(state.memDc, state.oldBitmap);
    DeleteObject(state.bitmap);
    DeleteDC(state.memDc);
    state.memDc = nullptr;
    state.bitmap = nullptr;
  }
  HDC screen = GetDC(state.hwnd);
  state.memDc = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  state.bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &state.bits, nullptr, 0);
  state.oldBitmap = SelectObject(state.memDc, state.bitmap);
  ReleaseDC(state.hwnd, screen);
  state.bmpW = width;
  state.bmpH = height;
}

void present(WinState& state, const Frame& frame, HDC target) {
  if (frame.width <= 0 || frame.height <= 0) return;
  ensureBitmap(state, frame.width, frame.height);
  if (!state.bits) return;
  std::memcpy(state.bits, frame.bgra.data(), frame.bgra.size() * sizeof(uint32_t));
  SetBkMode(state.memDc, TRANSPARENT);
  for (const Frame::Text& text : frame.texts) {
    HFONT font = fontFor(state, text.fontPx);
    HGDIOBJ old = SelectObject(state.memDc, font);
    SetTextColor(state.memDc, RGB((text.rgb >> 16) & 255, (text.rgb >> 8) & 255, text.rgb & 255));
    RECT rect{text.x, text.y, text.x + text.w, text.y + text.h};
    std::wstring wide = utf8ToWide(text.utf8);
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    format |= text.center ? DT_CENTER : DT_LEFT;
    int saved = SaveDC(state.memDc);
    IntersectClipRect(state.memDc, rect.left, rect.top, rect.right, rect.bottom);
    DrawTextW(state.memDc, wide.c_str(), static_cast<int>(wide.size()), &rect, format);
    RestoreDC(state.memDc, saved);
    SelectObject(state.memDc, old);
  }
  BitBlt(target, 0, 0, frame.width, frame.height, state.memDc, 0, 0, SRCCOPY);
}

void redraw(WinState& state) {
  RECT rect{};
  GetClientRect(state.hwnd, &rect);
  state.input.windowW = rect.right - rect.left;
  state.input.windowH = rect.bottom - rect.top;
  state.input.dpi = static_cast<float>(windowDpi(state.hwnd));
  state.input.timeSec = nowSec(state);
  state.input.modCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  state.input.modShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  state.input.modAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  Frame frame;
  state.app.update(state.input, state.host, frame);
  HDC dc = GetDC(state.hwnd);
  present(state, frame, dc);
  ReleaseDC(state.hwnd, dc);
  MenuRequest menu;
  if (state.app.takeMenu(menu)) {
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING | (menu.canOpen ? 0 : MF_GRAYED), 1, utf8ToWide(zh::open()).c_str());
    AppendMenuW(popup, MF_STRING | (menu.canReveal ? 0 : MF_GRAYED), 2, utf8ToWide(zh::openLocation()).c_str());
    AppendMenuW(popup, MF_STRING | (menu.canCopy ? 0 : MF_GRAYED), 3, utf8ToWide(zh::copyPath()).c_str());
    AppendMenuW(popup, MF_STRING | (menu.canZoom ? 0 : MF_GRAYED), 4, utf8ToWide(zh::enterFolder()).c_str());
    POINT point{menu.x, menu.y};
    ClientToScreen(state.hwnd, &point);
    int command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, state.hwnd, nullptr);
    DestroyMenu(popup);
    state.app.applyMenu(static_cast<MenuCommand>(command), state.host);
    clearEdges(state.input);
    InvalidateRect(state.hwnd, nullptr, FALSE);
  }
  clearEdges(state.input);
  if (state.app.animating()) SetTimer(state.hwnd, 1, 16, nullptr);
  else KillTimer(state.hwnd, 1);
}

void setClipboard(HWND hwnd, const std::string& text) {
  std::wstring wide = utf8ToWide(text);
  if (!OpenClipboard(hwnd)) return;
  EmptyClipboard();
  size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory) {
    void* dest = GlobalLock(memory);
    if (dest) {
      std::memcpy(dest, wide.c_str(), bytes);
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

std::string getClipboard(HWND hwnd) {
  std::string out;
  if (!OpenClipboard(hwnd)) return out;
  HANDLE memory = GetClipboardData(CF_UNICODETEXT);
  if (memory) {
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(memory));
    if (text) {
      out = wideToUtf8(text);
      GlobalUnlock(memory);
    }
  }
  CloseClipboard();
  return out;
}

bool showFolder(HWND hwnd, std::string& path) {
  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr) || !dialog) return false;
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dialog->SetTitle(utf8ToWide(zh::pickTitle()).c_str());
  bool ok = false;
  if (SUCCEEDED(dialog->Show(hwnd))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
      PWSTR display = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &display)) && display) {
        path = wideToUtf8(display);
        CoTaskMemFree(display);
        ok = !path.empty();
      }
      item->Release();
    }
  }
  dialog->Release();
  return ok;
}

bool showSave(HWND hwnd, std::string& path) {
  IFileSaveDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr) || !dialog) return false;
  COMDLG_FILTERSPEC specs[2];
  std::wstring csv = utf8ToWide(zh::csvType());
  std::wstring all = utf8ToWide(zh::allFiles());
  specs[0].pszName = csv.c_str();
  specs[0].pszSpec = L"*.csv";
  specs[1].pszName = all.c_str();
  specs[1].pszSpec = L"*.*";
  dialog->SetFileTypes(2, specs);
  dialog->SetDefaultExtension(L"csv");
  dialog->SetFileName(L"usage.csv");
  dialog->SetTitle(utf8ToWide(zh::saveTitle()).c_str());
  bool ok = false;
  if (SUCCEEDED(dialog->Show(hwnd))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
      PWSTR display = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &display)) && display) {
        path = wideToUtf8(display);
        CoTaskMemFree(display);
        ok = !path.empty();
      }
      item->Release();
    }
  }
  dialog->Release();
  return ok;
}

void openPath(const std::string& path) {
  std::wstring wide = utf8ToWide(path);
  ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void revealPath(const std::string& path) {
  std::wstring wide = utf8ToWide(path);
  DWORD attr = GetFileAttributesW(wide.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return;
  }
  std::wstring args = L"/select,\"" + wide + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

std::vector<VolumeEntry> listVolumes() {
  std::vector<VolumeEntry> out;
  DWORD mask = GetLogicalDrives();
  for (int i = 0; i < 26; ++i) {
    if ((mask & (1u << i)) == 0) continue;
    wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
    UINT type = GetDriveTypeW(root);
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE && type != DRIVE_CDROM &&
        type != DRIVE_RAMDISK) {
      continue;
    }
    ULARGE_INTEGER avail{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeTotal{};
    bool space = GetDiskFreeSpaceExW(root, &avail, &total, &freeTotal) != FALSE;
    if (type == DRIVE_CDROM && !space) continue;
    wchar_t label[256] = {};
    GetVolumeInformationW(root, label, 256, nullptr, nullptr, nullptr, nullptr, 0);
    const char* kind = zh::fixedDisk();
    if (type == DRIVE_REMOVABLE) kind = zh::removableDisk();
    else if (type == DRIVE_REMOTE) kind = zh::networkDisk();
    else if (type == DRIVE_CDROM) kind = zh::opticalDisk();
    VolumeEntry entry;
    entry.path = wideToUtf8(root);
    std::string name = label[0] ? wideToUtf8(label) : kind;
    name += " (";
    name.push_back(static_cast<char>('A' + i));
    name += ":)";
    entry.label = name;
    if (space) {
      entry.total = total.QuadPart;
      entry.free = avail.QuadPart;
    }
    out.push_back(entry);
  }
  return out;
}

void onKey(WinState& state, WPARAM key) {
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  if (ctrl && key == 'C') state.input.copy = true;
  else if (ctrl && key == 'V') state.input.paste = true;
  else if (ctrl && key == 'A') state.input.selectAll = true;
  else if (ctrl && key == 'O') state.input.openShortcut = true;
  else if (key == VK_F5) state.input.keyF5 = true;
  else if (key == VK_BACK) state.input.keyBackspace = true;
  else if (key == VK_DELETE) state.input.keyDelete = true;
  else if (key == VK_RETURN) state.input.keyEnter = true;
  else if (key == VK_TAB) state.input.keyTab = true;
  else if (key == VK_ESCAPE) state.input.keyEsc = true;
  else if (key == VK_LEFT) {
    if (alt) state.input.keyBackNav = true;
    else state.input.keyLeft = true;
  } else if (key == VK_RIGHT) {
    if (alt) state.input.keyForwardNav = true;
    else state.input.keyRight = true;
  } else if (key == VK_UP && alt) state.input.keyUpNav = true;
  else if (key == VK_HOME) state.input.keyHome = true;
  else if (key == VK_END) state.input.keyEnd = true;
  else return;
  InvalidateRect(state.hwnd, nullptr, FALSE);
  UpdateWindow(state.hwnd);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  WinState* state = reinterpret_cast<WinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE: {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
      return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_DPICHANGED: {
      RECT* suggested = reinterpret_cast<RECT*>(lParam);
      if (suggested) {
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_TIMER:
      if (state && wParam == 1) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_SIZE:
      if (state) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONDOWN:
      if (state) {
        SetCapture(hwnd);
        state->input.mouseX = GET_X_LPARAM(lParam);
        state->input.mouseY = GET_Y_LPARAM(lParam);
        state->input.leftPressed = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
      }
      return 0;
    case WM_LBUTTONUP:
      if (GetCapture() == hwnd) ReleaseCapture();
      return 0;
    case WM_RBUTTONUP:
      if (state) {
        state->input.mouseX = GET_X_LPARAM(lParam);
        state->input.mouseY = GET_Y_LPARAM(lParam);
        state->input.rightPressed = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
      }
      return 0;
    case WM_MOUSEMOVE:
      if (state) {
        state->input.mouseX = GET_X_LPARAM(lParam);
        state->input.mouseY = GET_Y_LPARAM(lParam);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_SETCURSOR:
      if (state && LOWORD(lParam) == HTCLIENT) {
        POINT point;
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        SetCursor(LoadCursorW(nullptr, state->app.ibeam(point.x, point.y) ? IDC_IBEAM : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_KEYDOWN:
      if (state) onKey(*state, wParam);
      return 0;
    case WM_CHAR:
      if (state && (GetKeyState(VK_CONTROL) & 0x8000) == 0) {
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch >= 32) {
          std::wstring piece;
          if (ch >= 0xD800 && ch <= 0xDBFF) {
            state->highSurrogate = ch;
          } else if (ch >= 0xDC00 && ch <= 0xDFFF && state->highSurrogate) {
            piece.push_back(state->highSurrogate);
            piece.push_back(ch);
            state->highSurrogate = 0;
          } else {
            state->highSurrogate = 0;
            piece.push_back(ch);
          }
          if (!piece.empty()) {
            state->input.typed += wideToUtf8(piece);
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
          }
        }
      }
      return 0;
    case WM_DROPFILES:
      if (state) {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768];
        if (DragQueryFileW(drop, 0, path, 32767)) state->app.scanPath(wideToUtf8(path));
        DragFinish(drop);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_APP + 1:
      if (state) InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      BeginPaint(hwnd, &paint);
      if (state) redraw(*state);
      EndPaint(hwnd, &paint);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::string initialPath() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::string path;
  if (argv) {
    for (int i = 1; i < argc; ++i) {
      if (argv[i][0] != L'-') {
        path = wideToUtf8(argv[i]);
        break;
      }
    }
    LocalFree(argv);
  }
  return path;
}

}  // namespace

int spaceviewer_main(int, char**) {
  enableHighDpi();
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  WinState state;
  QueryPerformanceFrequency(&state.qpf);
  state.host.volumes = []() { return listVolumes(); };
  state.host.pickFolder = [&state](std::string& path) { return showFolder(state.hwnd, path); };
  state.host.pickSave = [&state](std::string& path) { return showSave(state.hwnd, path); };
  state.host.openPath = [](const std::string& path) { openPath(path); };
  state.host.revealPath = [](const std::string& path) { revealPath(path); };
  state.host.setClipboard = [&state](const std::string& text) { setClipboard(state.hwnd, text); };
  state.host.getClipboard = [&state]() { return getClipboard(state.hwnd); };
  state.host.setTitle = [&state](const std::string& title) {
    if (state.hwnd) SetWindowTextW(state.hwnd, utf8ToWide(title).c_str());
  };

  WNDCLASSW cls{};
  cls.lpfnWndProc = wndProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(101));
  cls.lpszClassName = L"SpaceViewer";
  cls.hbrBackground = nullptr;
  RegisterClassW(&cls);

  HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"SpaceViewer", L"SpaceViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740, nullptr, nullptr, cls.hInstance, &state);
  state.hwnd = hwnd;
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(
      cls.hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR)));
  state.app.setWake([hwnd]() { PostMessageW(hwnd, WM_APP + 1, 0, 0); });
  int dpi = windowDpi(hwnd);
  RECT wanted{0, 0, MulDiv(1100, dpi, 96), MulDiv(740, dpi, 96)};
  AdjustWindowRectEx(&wanted, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES);
  SetWindowPos(hwnd, nullptr, 0, 0, wanted.right - wanted.left, wanted.bottom - wanted.top,
               SWP_NOMOVE | SWP_NOZORDER);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  DragAcceptFiles(hwnd, TRUE);
  std::string path = initialPath();
  if (!path.empty()) state.app.scanPath(path);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  for (HFONT font : state.fonts) DeleteObject(font);
  if (state.memDc) {
    SelectObject(state.memDc, state.oldBitmap);
    DeleteObject(state.bitmap);
    DeleteDC(state.memDc);
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return 0;
}
