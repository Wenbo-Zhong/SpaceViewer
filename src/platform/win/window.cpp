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
    HWND tab = GetDlgItem(dialog, 201);
    TCITEMW item{}; item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"驱动器或路径"); TabCtrl_InsertItem(tab, 0, &item);
    item.pszText = const_cast<wchar_t*>(L"快照"); TabCtrl_InsertItem(tab, 1, &item);
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
      SetDlgItemTextW(dialog,205,choice->snapshot ? L"选择 SpaceViewer 快照 (.svs)，离线查看扫描结果" : L"双击磁盘开始，或输入路径后按开始");
      SetDlgItemTextW(dialog,203,choice->snapshot ? L"选择快照..." : L"路径(&P)...");
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
        MessageBoxW(dialog,L"请选择可访问的磁盘、文件夹或快照。",L"选择您的空间",MB_OK|MB_ICONINFORMATION); return TRUE;
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
  action(file,L"选择扫描路径...\tCtrl+O",HotKind::Open);
  AppendMenuW(file,MF_STRING,900,L"新窗口\tCtrl+N");
  AppendMenuW(file,MF_STRING,901,L"打开快照...");
  AppendMenuW(file,MF_STRING,902,L"保存快照...");
  action(file,L"导出当前视图 CSV...",HotKind::Export);
  AppendMenuW(file,MF_SEPARATOR,0,nullptr);
  action(file,L"重新扫描\tF5",HotKind::Rescan);
  action(file,L"刷新选中区域",HotKind::FocusRescan);
  action(file,L"停止扫描",HotKind::Stop);
  action(file,L"关闭视图",HotKind::CloseView);
  AppendMenuW(file,MF_STRING,903,L"退出");
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(file),L"文件(&F)");
  HMENU edit = CreatePopupMenu(); action(edit,L"配置...",HotKind::Configure);
  AppendMenuW(edit,MF_STRING,904,L"复制当前路径");
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(edit),L"编辑(&E)");
  HMENU view = CreatePopupMenu();
  action(view,L"后退\tAlt+Left",HotKind::Back); action(view,L"前进\tAlt+Right",HotKind::Forward);
  action(view,L"向上\tAlt+Up",HotKind::Up); action(view,L"扫描根目录\tCtrl+Home",HotKind::Home);
  AppendMenuW(view,MF_SEPARATOR,0,nullptr);
  action(view,L"减少细节\tCtrl+-",HotKind::LessDetail); action(view,L"增加细节\tCtrl++",HotKind::MoreDetail);
  action(view,L"按文件类型着色\tCtrl+G",HotKind::FileGroups);
  action(view,L"显示可用空间\tCtrl+F",HotKind::ShowFree);
  action(view,L"显示未知空间\tCtrl+U",HotKind::ShowUnknown);
  AppendMenuW(view,MF_SEPARATOR,0,nullptr);
  AppendMenuW(view,MF_STRING,909,L"水平平铺"); AppendMenuW(view,MF_STRING,910,L"垂直平铺");
  AppendMenuW(view,MF_STRING,911,L"层叠窗口"); AppendMenuW(view,MF_STRING,912,L"全部最小化");
  AppendMenuW(view,MF_STRING,905,L"最小化窗口"); AppendMenuW(view,MF_STRING,906,L"还原窗口");
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(view),L"窗口(&W)");
  HMENU help = CreatePopupMenu(); AppendMenuW(help,MF_STRING,907,L"筛选与快捷键");
  AppendMenuW(help,MF_STRING,908,L"关于 SpaceViewer");
  AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(help),L"帮助(&H)");
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
          L"单击目录原地展开，双击进入；Backspace / Alt+Up 向上。\n"
          L"Ctrl+O 选择扫描；Ctrl+N 新窗口；Ctrl+Home 根目录；F5 重扫。\n"
          L"Ctrl+- / Ctrl++ 调节细节；Ctrl+G 文件类型；Ctrl+F 可用空间；Ctrl+U 未知空间。\n"
          L"包含、排除支持 *.jpg;*.tmp 等通配符，最小大小支持 1M、1.5G。\n"
          L"快照使用 SpaceViewer .svs 格式，打开后离线查看，不监听磁盘。",
          L"筛选与快捷键",MB_OK|MB_ICONINFORMATION);
        else if (command == 908) MessageBoxW(hwnd,L"SpaceViewer\n磁盘占用矩形树图查看器\nGPL-3.0",
                                              L"关于 SpaceViewer",MB_OK|MB_ICONINFORMATION);
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
  if (state.memDc) {
    SelectObject(state.memDc, state.oldBitmap);
    DeleteObject(state.bitmap);
    DeleteDC(state.memDc);
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return 0;
}
