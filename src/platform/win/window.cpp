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
#include <shlobj.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <iterator>
#include <algorithm>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct WinState {
  App app;
  bool rendering = false;
  std::wstring configPath;
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
  std::vector<std::wstring> registeredFontPaths;
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
  input.command = -1;
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

std::wstring appDirectory() {
  wchar_t path[32768] = {};
  DWORD size = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  if (!size || size >= std::size(path)) return L".";
  std::wstring result(path);
  size_t separator = result.find_last_of(L"\\/");
  if (separator != std::wstring::npos) result.resize(separator);
  return result;
}

void loadBundledFonts(WinState& state) {
  std::wstring directory = appDirectory() + L"\\fonts\\";
  for (const wchar_t* name : {L"Inter-Regular.ttf", L"SourceHanSansSC-Regular.otf", L"SourceHanSansTC-Regular.otf"}) {
    std::wstring path = directory + name;
    if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr)) state.registeredFontPaths.push_back(path);
  }
  SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
}

HFONT fontFor(WinState& state, int px, bool cjk = false) {
  if (px < 8) px = 8;
  bool traditional = zh::language() == zh::Language::TraditionalChinese;
  int key = cjk ? -px : px;
  for (size_t i = 0; i < state.fontPx.size(); ++i) {
    if (state.fontPx[i] == key) return state.fonts[i];
  }
  const wchar_t* family = cjk ? (traditional ? L"Source Han Sans TC" : L"Source Han Sans SC") : L"Inter";
  HFONT font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
  state.fonts.push_back(font);
  state.fontPx.push_back(key);
  return font;
}

bool isChinese(const std::wstring& text, size_t index) {
  uint32_t cp = text[index];
  if (cp >= 0xD800 && cp <= 0xDBFF && index + 1 < text.size() &&
      text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF) {
    cp = 0x10000 + ((cp - 0xD800) << 10) + (text[index + 1] - 0xDC00);
  }
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F) ||
         (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
         (cp >= 0xFF00 && cp <= 0xFFEF);
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
    SetTextColor(state.memDc, RGB((text.rgb >> 16) & 255, (text.rgb >> 8) & 255, text.rgb & 255));
    RECT rect{text.x, text.y, text.x + text.w, text.y + text.h};
    std::wstring wide = utf8ToWide(text.utf8);
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    format |= text.center ? DT_CENTER : DT_LEFT;
    int saved = SaveDC(state.memDc);
    IntersectClipRect(state.memDc, rect.left, rect.top, rect.right, rect.bottom);
    int total = 0;
    for (size_t pos = 0; pos < wide.size();) {
      bool cjk = isChinese(wide, pos);
      size_t end = pos + 1;
      if (wide[pos] >= 0xD800 && wide[pos] <= 0xDBFF && end < wide.size()) ++end;
      while (end < wide.size() && isChinese(wide, end) == cjk) {
        size_t next = end + 1;
        if (wide[end] >= 0xD800 && wide[end] <= 0xDBFF && next < wide.size()) ++next;
        end = next;
      }
      HFONT runFont = fontFor(state, text.fontPx, cjk);
      HGDIOBJ oldFont = SelectObject(state.memDc, runFont);
      SIZE extent{};
      GetTextExtentPoint32W(state.memDc, wide.data() + pos, static_cast<int>(end - pos), &extent);
      total += extent.cx;
      SelectObject(state.memDc, oldFont);
      pos = end;
    }
    int x = text.x + (text.center ? std::max(0, (text.w - total) / 2) : 0);
    for (size_t pos = 0; pos < wide.size();) {
      bool cjk = isChinese(wide, pos);
      size_t end = pos + 1;
      if (wide[pos] >= 0xD800 && wide[pos] <= 0xDBFF && end < wide.size()) ++end;
      while (end < wide.size() && isChinese(wide, end) == cjk) {
        size_t next = end + 1;
        if (wide[end] >= 0xD800 && wide[end] <= 0xDBFF && next < wide.size()) ++next;
        end = next;
      }
      HFONT runFont = fontFor(state, text.fontPx, cjk);
      HGDIOBJ oldFont = SelectObject(state.memDc, runFont);
      RECT runRect{ x, text.y, text.x + text.w, text.y + text.h };
      DrawTextW(state.memDc, wide.data() + pos, static_cast<int>(end - pos), &runRect,
                (format & ~DT_CENTER) | DT_LEFT);
      SIZE extent{};
      GetTextExtentPoint32W(state.memDc, wide.data() + pos, static_cast<int>(end - pos), &extent);
      x += extent.cx;
      SelectObject(state.memDc, oldFont);
      pos = end;
    }
    RestoreDC(state.memDc, saved);
  }
  BitBlt(target, 0, 0, frame.width, frame.height, state.memDc, 0, 0, SRCCOPY);
}

void redraw(WinState& state) {
  if (state.rendering) return;
  state.rendering = true;
  struct RenderGuard { bool& flag; ~RenderGuard() { flag = false; } } guard{state.rendering};
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


std::wstring pickSnapshot(HWND owner, bool save) {
  wchar_t buffer[32768] = {};
  OPENFILENAMEW file{}; file.lStructSize = sizeof(file); file.hwndOwner = owner;
  file.lpstrFilter = L"SpaceViewer snapshot (*.svs)\0*.svs\0\0";
  file.lpstrFile = buffer; file.nMaxFile = 32768; file.lpstrDefExt = L"svs";
  file.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
               (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  return (save ? GetSaveFileNameW(&file) : GetOpenFileNameW(&file)) ? buffer : L"";
}

std::vector<VolumeEntry> listVolumes();
struct ScanChoice { std::string path; std::vector<VolumeEntry> drives; bool snapshot = false; };
INT_PTR CALLBACK scanChoiceProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* choice = reinterpret_cast<ScanChoice*>(GetWindowLongPtrW(dialog, DWLP_USER));
  if (message == WM_INITDIALOG) {
    choice = reinterpret_cast<ScanChoice*>(lParam); SetWindowLongPtrW(dialog, DWLP_USER, lParam);
    SetWindowTextW(dialog, utf8ToWide(zh::chooseSpaceTitle()).c_str());
    SetDlgItemTextW(dialog, 203, utf8ToWide(zh::pathButton()).c_str());
    SetDlgItemTextW(dialog, IDOK, utf8ToWide(zh::open()).c_str());
    SetDlgItemTextW(dialog, 205, utf8ToWide(zh::pathPrompt()).c_str());
    HWND tab = GetDlgItem(dialog, 201);
    TCITEMW item{}; item.mask = TCIF_TEXT;
    std::wstring drivesTab = utf8ToWide(zh::drivesOrPath());
    std::wstring snapshotTab = utf8ToWide(zh::snapshot());
    item.pszText = drivesTab.data(); TabCtrl_InsertItem(tab, 0, &item);
    item.pszText = snapshotTab.data(); TabCtrl_InsertItem(tab, 1, &item);
    HWND list = GetDlgItem(dialog, 202);
    ListView_SetIconSpacing(list, 96, 78);
    for (size_t i = 0; i < choice->drives.size(); ++i) {
      std::wstring path = utf8ToWide(choice->drives[i].path);
      SHFILEINFOW info{};
      auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_LARGEICON));
      if (images) ListView_SetImageList(list, images, LVSIL_NORMAL);
      std::wstring label = L"(" + path.substr(0,2) + L")";
      LVITEMW entry{}; entry.mask = LVIF_TEXT | LVIF_IMAGE; entry.iItem = static_cast<int>(i);
      entry.pszText = label.data(); entry.iImage = info.iIcon; ListView_InsertItem(list, &entry);
      ListView_SetItemPosition(list,static_cast<int>(i),16+static_cast<int>(i)*96,12);
    }
    SetDlgItemTextW(dialog, 204, utf8ToWide(choice->path).c_str());
    RECT parent{}, rect{}; GetWindowRect(GetParent(dialog), &parent); GetWindowRect(dialog, &rect);
    SetWindowPos(dialog, nullptr, parent.left + ((parent.right-parent.left)-(rect.right-rect.left))/2,
                 parent.top + ((parent.bottom-parent.top)-(rect.bottom-rect.top))/2, 0,0,SWP_NOSIZE|SWP_NOZORDER);
    return TRUE;
  }
  if (!choice) return FALSE;
  if (message == WM_NOTIFY) {
    auto* info = reinterpret_cast<NMHDR*>(lParam);
    if (info->idFrom == 201 && info->code == TCN_SELCHANGE) {
      choice->snapshot = TabCtrl_GetCurSel(GetDlgItem(dialog,201)) == 1;
      ShowWindow(GetDlgItem(dialog,202), choice->snapshot ? SW_HIDE : SW_SHOW);
      SetDlgItemTextW(dialog,205,choice->snapshot ? utf8ToWide(zh::snapshotHelp()).c_str() : utf8ToWide(zh::pathPrompt()).c_str());
      SetDlgItemTextW(dialog,203,choice->snapshot ? utf8ToWide(zh::chooseSnapshot()).c_str() : utf8ToWide(zh::pathButton()).c_str());
      SetDlgItemTextW(dialog,IDOK,choice->snapshot ? utf8ToWide(zh::openSnapshot()).c_str() : utf8ToWide(zh::open()).c_str());
      SetDlgItemTextW(dialog,204,L"");
      return TRUE;
    }
    if (info->idFrom == 202 && (info->code == LVN_ITEMCHANGED || info->code == NM_DBLCLK)) {
      int selected = ListView_GetNextItem(GetDlgItem(dialog,202),-1,LVNI_SELECTED);
      if (selected >= 0 && selected < static_cast<int>(choice->drives.size())) {
        const auto& drive = choice->drives[selected];
        SetDlgItemTextW(dialog,204,utf8ToWide(drive.path).c_str());
        std::string summary = formatSize(drive.free) + " / " + formatSize(drive.total);
        SetDlgItemTextW(dialog,205,utf8ToWide(summary).c_str());
        if (info->code == NM_DBLCLK) SendMessageW(dialog,WM_COMMAND,IDOK,0);
      }
      return TRUE;
    }
  }
  if (message == WM_COMMAND) {
    if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog,0); return TRUE; }
    if (LOWORD(wParam) == 203) {
      if (choice->snapshot) {
        std::wstring path = pickSnapshot(dialog,false);
        if (!path.empty()) SetDlgItemTextW(dialog,204,path.c_str());
      } else {
        std::string path;
        if (showFolder(dialog,path)) SetDlgItemTextW(dialog,204,utf8ToWide(path).c_str());
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDOK) {
      wchar_t path[32768]; GetDlgItemTextW(dialog,204,path,32768);
      DWORD attr = GetFileAttributesW(path);
      if (attr == INVALID_FILE_ATTRIBUTES || (!choice->snapshot && !(attr & FILE_ATTRIBUTE_DIRECTORY))) {
        MessageBoxW(dialog,utf8ToWide(zh::accessDenied()).c_str(),utf8ToWide(zh::chooseSpaceTitle()).c_str(),MB_OK|MB_ICONINFORMATION); return TRUE;
      }
      choice->path = wideToUtf8(path); EndDialog(dialog,1); return TRUE;
    }
  }
  return FALSE;
}
bool showScanChoice(HWND owner, std::string& path) {
  ScanChoice choice; choice.path = path; choice.drives = listVolumes();
  bool ok = DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(200),owner,scanChoiceProc,
                             reinterpret_cast<LPARAM>(&choice)) == 1;
  if (ok) path = choice.path;
  return ok;
}

INT_PTR CALLBACK optionsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* options = reinterpret_cast<ViewOptions*>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message == WM_INITDIALOG) {
    options = reinterpret_cast<ViewOptions*>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowTextW(dialog, utf8ToWide(zh::configure()).c_str());
    SetDlgItemTextW(dialog,211,utf8ToWide(zh::optionsFileGroups()).c_str());
    SetDlgItemTextW(dialog,212,utf8ToWide(zh::optionsShowFree()).c_str());
    SetDlgItemTextW(dialog,213,utf8ToWide(zh::optionsShowUnknown()).c_str());
    SetWindowTextW(GetDlgItem(dialog,-1),utf8ToWide(zh::optionsDetail()).c_str());
    SetDlgItemTextW(dialog,IDOK,utf8ToWide(zh::ok()).c_str());
    SetDlgItemTextW(dialog,IDCANCEL,utf8ToWide(zh::cancel()).c_str());
    CheckDlgButton(dialog,211,options->fileGroups ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog,212,options->showFree ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog,213,options->showUnknown ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(dialog,214,options->detail,FALSE); return TRUE;
  }
  if (message == WM_COMMAND && options) {
    if (LOWORD(wParam) == IDCANCEL) { EndDialog(dialog,0); return TRUE; }
    if (LOWORD(wParam) == IDOK) {
      options->fileGroups = IsDlgButtonChecked(dialog,211) == BST_CHECKED;
      options->showFree = IsDlgButtonChecked(dialog,212) == BST_CHECKED;
      options->showUnknown = IsDlgButtonChecked(dialog,213) == BST_CHECKED;
      options->detail = std::max(0,std::min(6,static_cast<int>(GetDlgItemInt(dialog,214,nullptr,FALSE))));
      EndDialog(dialog,1); return TRUE;
    }
  }
  return FALSE;
}

HMENU mainMenu() {
  HMENU bar = CreateMenu();
  auto action = [](HMENU menu, const wchar_t* name, HotKind kind) {
    AppendMenuW(menu,MF_STRING,1000+static_cast<int>(kind),name);
  };
  HMENU file = CreatePopupMenu();
  action(file,utf8ToWide(zh::scanPathMenu()).c_str(),HotKind::Open);
  AppendMenuW(file,MF_STRING,900,utf8ToWide(zh::newWindow()).c_str());
  AppendMenuW(file,MF_STRING,901,utf8ToWide(zh::openSnapshot()).c_str());
  AppendMenuW(file,MF_STRING,902,utf8ToWide(zh::saveSnapshot()).c_str());
  action(file,utf8ToWide(zh::exportCurrentCsv()).c_str(),HotKind::Export);
  AppendMenuW(file,MF_SEPARATOR,0,nullptr);
  action(file,utf8ToWide(std::string(zh::rescan())+"\tF5").c_str(),HotKind::Rescan);
  action(file,utf8ToWide(zh::refreshSelection()).c_str(),HotKind::FocusRescan);
  action(file,utf8ToWide(zh::stop()).c_str(),HotKind::Stop);
  action(file,utf8ToWide(zh::closeView()).c_str(),HotKind::CloseView);
  AppendMenuW(file,MF_STRING,903,utf8ToWide(zh::quit()).c_str());
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),utf8ToWide(zh::fileMenu()).c_str());
  HMENU edit = CreatePopupMenu(); action(edit,utf8ToWide(zh::configure()).c_str(),HotKind::Configure);
  AppendMenuW(edit,MF_STRING,904,utf8ToWide(zh::copyCurrentPath()).c_str());
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(edit),utf8ToWide(zh::editMenu()).c_str());
  HMENU view = CreatePopupMenu();
  action(view,utf8ToWide(std::string(zh::back())+"\tAlt+Left").c_str(),HotKind::Back); action(view,utf8ToWide(std::string(zh::forward())+"\tAlt+Right").c_str(),HotKind::Forward);
  action(view,utf8ToWide(std::string(zh::up())+"\tAlt+Up").c_str(),HotKind::Up); action(view,utf8ToWide(zh::homeRoot()).c_str(),HotKind::Home);
  AppendMenuW(view,MF_SEPARATOR,0,nullptr);
  action(view,utf8ToWide(zh::lessDetail()).c_str(),HotKind::LessDetail); action(view,utf8ToWide(zh::moreDetail()).c_str(),HotKind::MoreDetail);
  action(view,utf8ToWide(zh::colorByType()).c_str(),HotKind::FileGroups);
  action(view,utf8ToWide(zh::showFree()).c_str(),HotKind::ShowFree);
  action(view,utf8ToWide(zh::showUnknown()).c_str(),HotKind::ShowUnknown);
  AppendMenuW(view,MF_SEPARATOR,0,nullptr);
  AppendMenuW(view,MF_STRING,909,utf8ToWide(zh::tileHorizontal()).c_str()); AppendMenuW(view,MF_STRING,910,utf8ToWide(zh::tileVertical()).c_str());
  AppendMenuW(view,MF_STRING,911,utf8ToWide(zh::cascade()).c_str()); AppendMenuW(view,MF_STRING,912,utf8ToWide(zh::minimizeAll()).c_str());
  AppendMenuW(view,MF_STRING,905,utf8ToWide(zh::minimize()).c_str()); AppendMenuW(view,MF_STRING,906,utf8ToWide(zh::restore()).c_str());
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(view),utf8ToWide(zh::windowMenu()).c_str());
  HMENU help = CreatePopupMenu(); AppendMenuW(help,MF_STRING,907,utf8ToWide(zh::helpTitle()).c_str());
  AppendMenuW(help,MF_STRING,908,utf8ToWide(zh::aboutTitle()).c_str());
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(help),utf8ToWide(zh::helpMenu()).c_str());
  return bar;
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
  if (ctrl && key == 'N') { SendMessageW(state.hwnd,WM_COMMAND,900,0); return; }
  if (ctrl && key == VK_HOME) state.input.command = static_cast<int>(HotKind::Home);
  else if (ctrl && (key == VK_OEM_PLUS || key == VK_ADD)) state.input.command = static_cast<int>(HotKind::MoreDetail);
  else if (ctrl && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) state.input.command = static_cast<int>(HotKind::LessDetail);
  else if (ctrl && key == 'G') state.input.command = static_cast<int>(HotKind::FileGroups);
  else if (ctrl && key == 'F') state.input.command = static_cast<int>(HotKind::ShowFree);
  else if (ctrl && key == 'U') state.input.command = static_cast<int>(HotKind::ShowUnknown);
  else if (ctrl && key == 'C') state.input.copy = true;
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
    case WM_APP + 2:
      if (state) {
        std::string path;
        if (showScanChoice(hwnd,path)) state->app.scanPath(path);
        InvalidateRect(hwnd,nullptr,FALSE);
      }
      return 0;
    case WM_INITMENUPOPUP:
      if (state) {
        ViewOptions options = state->app.options();
        for (auto item : {std::make_pair(HotKind::FileGroups, options.fileGroups),
                          std::make_pair(HotKind::ShowFree, options.showFree),
                          std::make_pair(HotKind::ShowUnknown, options.showUnknown)})
          CheckMenuItem(reinterpret_cast<HMENU>(wParam),1000+static_cast<int>(item.first),
                        MF_BYCOMMAND | (item.second ? MF_CHECKED : MF_UNCHECKED));
      }
      return 0;
    case WM_COMMAND:
      if (state) {
        int command = LOWORD(wParam);
        if (command >= 1000 && command <= 1000+static_cast<int>(HotKind::CloseView)) {
          state->input.command = command - 1000;
        } else if (command == 900) {
          wchar_t exe[32768]; GetModuleFileNameW(nullptr,exe,32768);
          ShellExecuteW(hwnd,L"open",exe,nullptr,nullptr,SW_SHOWNORMAL);
        } else if (command == 901 || command == 902) {
          std::wstring path = pickSnapshot(hwnd,command == 902);
          if (!path.empty()) {
            std::string error;
            bool ok = command == 902 ? state->app.saveSnapshot(wideToUtf8(path),error)
                                     : state->app.loadSnapshot(wideToUtf8(path),error);
            if (!ok) MessageBoxW(hwnd,utf8ToWide(error).c_str(),L"SpaceViewer",MB_OK|MB_ICONINFORMATION);
          }
        } else if (command == 903) { SendMessageW(hwnd,WM_CLOSE,0,0); return 0; }
        else if (command == 904) {
          int length = GetWindowTextLengthW(hwnd);
          std::wstring title(length+1,L'\0'); GetWindowTextW(hwnd,title.data(),length+1);
          auto split = title.find(L" - ");
          if (split != std::wstring::npos) setClipboard(hwnd,wideToUtf8(title.substr(split+3).c_str()));
        } else if (command == 905) ShowWindow(hwnd,SW_MINIMIZE);
        else if (command == 906) ShowWindow(hwnd,SW_RESTORE);
        else if (command >= 909 && command <= 912) {
          std::vector<HWND> windows;
          EnumWindows([](HWND window, LPARAM data)->BOOL {
            wchar_t name[64]; GetClassNameW(window,name,64);
            if (wcscmp(name,L"SpaceViewer") == 0 && IsWindowVisible(window))
              reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
            return TRUE;
          },reinterpret_cast<LPARAM>(&windows));
          if (command == 912) for (HWND window : windows) ShowWindow(window,SW_MINIMIZE);
          else if (command == 911) CascadeWindows(nullptr,0,nullptr,static_cast<UINT>(windows.size()),windows.data());
          else TileWindows(nullptr,command == 909 ? MDITILE_HORIZONTAL : MDITILE_VERTICAL,
                           nullptr,static_cast<UINT>(windows.size()),windows.data());
        }
        else if (command == 907) MessageBoxW(hwnd,
          utf8ToWide(zh::helpBody()).c_str(),
          utf8ToWide(zh::helpTitle()).c_str(),MB_OK|MB_ICONINFORMATION);
        else if (command == 908) MessageBoxW(hwnd,utf8ToWide(zh::aboutBody()).c_str(),
                                              utf8ToWide(zh::aboutTitle()).c_str(),MB_OK|MB_ICONINFORMATION);
        InvalidateRect(hwnd,nullptr,FALSE);
      }
      return 0;
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
      if (state && !state->configPath.empty()) {
        ViewOptions options = state->app.options();
        auto save = [&](const wchar_t* key, int value) {
          WritePrivateProfileStringW(L"View",key,std::to_wstring(value).c_str(),state->configPath.c_str());
        };
        save(L"Detail",options.detail); save(L"FileGroups",options.fileGroups);
        save(L"ShowFree",options.showFree); save(L"ShowUnknown",options.showUnknown);
      }
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
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};
  InitCommonControlsEx(&controls);
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  WinState state;
  loadBundledFonts(state);
  wchar_t appData[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,appData))) {
    std::wstring dir = std::wstring(appData) + L"\\SpaceViewer";
    CreateDirectoryW(dir.c_str(),nullptr);
    state.configPath = dir + L"\\settings.ini";
    ViewOptions options;
    options.detail = GetPrivateProfileIntW(L"View",L"Detail",2,state.configPath.c_str());
    options.fileGroups = GetPrivateProfileIntW(L"View",L"FileGroups",0,state.configPath.c_str()) != 0;
    options.showFree = GetPrivateProfileIntW(L"View",L"ShowFree",0,state.configPath.c_str()) != 0;
    options.showUnknown = GetPrivateProfileIntW(L"View",L"ShowUnknown",0,state.configPath.c_str()) != 0;
    state.app.setOptions(options);
  }
  QueryPerformanceFrequency(&state.qpf);
  state.host.volumes = []() { return listVolumes(); };
  state.host.pickFolder = [&state](std::string& path) { return showScanChoice(state.hwnd, path); };
  state.host.newWindow = [&state]() { SendMessageW(state.hwnd,WM_COMMAND,900,0); };
  state.host.configure = [&state]() {
    ViewOptions options = state.app.options();
    if (DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(201),state.hwnd,optionsProc,
                        reinterpret_cast<LPARAM>(&options)) == 1) state.app.setOptions(options);
  };
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
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740, nullptr, mainMenu(), cls.hInstance, &state);
  state.hwnd = hwnd;
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(
      cls.hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR)));
  state.app.setWake([hwnd]() { PostMessageW(hwnd, WM_APP + 1, 0, 0); });
  int dpi = windowDpi(hwnd);
  RECT wanted{0, 0, MulDiv(1100, dpi, 96), MulDiv(740, dpi, 96)};
  AdjustWindowRectEx(&wanted, WS_OVERLAPPEDWINDOW, TRUE, WS_EX_ACCEPTFILES);
  SetWindowPos(hwnd, nullptr, 0, 0, wanted.right - wanted.left, wanted.bottom - wanted.top,
               SWP_NOMOVE | SWP_NOZORDER);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  DragAcceptFiles(hwnd, TRUE);
  std::string path = initialPath();
  if (!path.empty()) state.app.scanPath(path);
  else PostMessageW(hwnd,WM_APP+2,0,0);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  for (HFONT font : state.fonts) DeleteObject(font);
  for (const std::wstring& path : state.registeredFontPaths) RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
  if (state.memDc) {
    SelectObject(state.memDc, state.oldBitmap);
    DeleteObject(state.bitmap);
    DeleteDC(state.memDc);
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return 0;
}
