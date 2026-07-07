#include "gui.h"

#include "common.h"
#include "window_enum.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace sc {

namespace {

constexpr int kIdList = 1001;
constexpr int kIdRefresh = 1002;
constexpr int kIdMethod = 1003;
constexpr int kIdOut = 1004;
constexpr int kIdBrowse = 1005;
constexpr int kIdCapture = 1006;
constexpr int kIdStatus = 1007;

struct GuiState {
  HWND hwnd = nullptr;
  HWND list = nullptr;
  HWND refresh = nullptr;
  HWND method = nullptr;
  HWND out = nullptr;
  HWND browse = nullptr;
  HWND capture = nullptr;
  HWND status = nullptr;
  std::vector<WindowInfo> windows;
};

GuiState *GetState(HWND hwnd) {
  return reinterpret_cast<GuiState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HMENU ControlId(int id) {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

bool IsPickableWindow(const WindowInfo &w) {
  if (!w.visible || w.iconic || w.cloaked || w.title.empty()) {
    return false;
  }
  if (!IsValidRect(w.rect)) {
    return false;
  }
  if (GetAncestor(w.hwnd, GA_ROOT) != w.hwnd) {
    return false;
  }
  return true;
}

std::wstring DefaultOutputPath() {
  wchar_t cwd[MAX_PATH] = {};
  if (!GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd)) {
    return L"screenshot_" + WideFromUtf8(BuildTimestampForFilename()) + L".png";
  }
  std::filesystem::path p(cwd);
  p /= L"screenshot_" + WideFromUtf8(BuildTimestampForFilename()) + L".png";
  return p.wstring();
}

std::wstring GetWindowTextString(HWND hwnd) {
  int len = GetWindowTextLengthW(hwnd);
  std::wstring out(static_cast<size_t>(len + 1), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd, out.data(), len + 1);
  }
  out.resize(static_cast<size_t>(len));
  return out;
}

void SetStatus(GuiState *s, const std::wstring &text) {
  SetWindowTextW(s->status, text.c_str());
}

void ResizeControls(GuiState *s) {
  RECT rc{};
  GetClientRect(s->hwnd, &rc);
  const int pad = 10;
  const int button_h = 28;
  const int out_h = 24;
  const int status_h = 22;
  const int method_w = 150;
  const int browse_w = 80;
  const int capture_w = 92;
  const int refresh_w = 80;
  const int width = rc.right - rc.left;
  const int height = rc.bottom - rc.top;

  MoveWindow(s->refresh, pad, pad, refresh_w, button_h, TRUE);
  MoveWindow(s->method, pad + refresh_w + pad, pad, method_w, 180, TRUE);
  MoveWindow(s->capture, width - pad - capture_w, pad, capture_w, button_h,
             TRUE);

  const int out_y = pad + button_h + pad;
  MoveWindow(s->out, pad, out_y, width - pad * 3 - browse_w, out_h, TRUE);
  MoveWindow(s->browse, width - pad - browse_w, out_y, browse_w, out_h, TRUE);

  const int list_y = out_y + out_h + pad;
  const int list_h = height - list_y - status_h - pad * 2;
  MoveWindow(s->list, pad, list_y, width - pad * 2, std::max(80, list_h), TRUE);
  MoveWindow(s->status, pad, height - pad - status_h, width - pad * 2, status_h,
             TRUE);
}

void InitListColumns(HWND list) {
  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH;
  col.pszText = const_cast<LPWSTR>(L"Title");
  col.cx = 360;
  ListView_InsertColumn(list, 0, &col);
  col.pszText = const_cast<LPWSTR>(L"Class");
  col.cx = 170;
  ListView_InsertColumn(list, 1, &col);
  col.pszText = const_cast<LPWSTR>(L"PID");
  col.cx = 80;
  ListView_InsertColumn(list, 2, &col);
  col.pszText = const_cast<LPWSTR>(L"Rect");
  col.cx = 180;
  ListView_InsertColumn(list, 3, &col);
}

void RefreshWindows(GuiState *s) {
  s->windows.clear();
  ListView_DeleteAllItems(s->list);

  auto all = EnumerateWindows();
  for (auto &w : all) {
    if (IsPickableWindow(w)) {
      s->windows.push_back(std::move(w));
    }
  }

  std::sort(s->windows.begin(), s->windows.end(),
            [](const WindowInfo &a, const WindowInfo &b) {
              return a.title < b.title;
            });

  for (size_t i = 0; i < s->windows.size(); ++i) {
    const auto &w = s->windows[i];
    std::wstring title = WideFromUtf8(w.title);
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = static_cast<int>(i);
    item.pszText = title.data();
    item.lParam = static_cast<LPARAM>(i);
    ListView_InsertItem(s->list, &item);

    std::wstring cls = WideFromUtf8(w.class_name);
    ListView_SetItemText(s->list, static_cast<int>(i), 1, cls.data());
    std::wstring pid = std::to_wstring(w.pid);
    ListView_SetItemText(s->list, static_cast<int>(i), 2, pid.data());
    std::wostringstream rect;
    rect << w.rect.left << L"," << w.rect.top << L" "
         << Width(w.rect) << L"x" << Height(w.rect);
    auto rect_s = rect.str();
    ListView_SetItemText(s->list, static_cast<int>(i), 3, rect_s.data());
  }

  std::wostringstream status;
  status << L"Windows: " << s->windows.size();
  SetStatus(s, status.str());
}

std::wstring QuoteArg(const std::wstring &s) {
  std::wstring out = L"\"";
  for (wchar_t ch : s) {
    if (ch == L'"') {
      out += L"\\\"";
    } else {
      out += ch;
    }
  }
  out += L"\"";
  return out;
}

std::wstring CliExePath() {
  wchar_t path[MAX_PATH] = {};
  DWORD len =
      GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
  std::filesystem::path exe(std::wstring(path, path + len));
  return (exe.parent_path() / L"screencap-cli.exe").wstring();
}

std::wstring SelectedMethod(GuiState *s) {
  int idx = static_cast<int>(SendMessageW(s->method, CB_GETCURSEL, 0, 0));
  if (idx < 0) {
    return L"wgc-window";
  }
  wchar_t buf[64] = {};
  SendMessageW(s->method, CB_GETLBTEXT, static_cast<WPARAM>(idx),
               reinterpret_cast<LPARAM>(buf));
  return buf;
}

int SelectedWindowIndex(GuiState *s) {
  int item = ListView_GetNextItem(s->list, -1, LVNI_SELECTED);
  if (item < 0) {
    return -1;
  }
  LVITEMW lv{};
  lv.mask = LVIF_PARAM;
  lv.iItem = item;
  if (!ListView_GetItem(s->list, &lv)) {
    return -1;
  }
  return static_cast<int>(lv.lParam);
}

void BrowseOutput(GuiState *s) {
  wchar_t file[MAX_PATH] = {};
  auto current = GetWindowTextString(s->out);
  wcsncpy_s(file, current.c_str(), _TRUNCATE);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = s->hwnd;
  ofn.lpstrFilter = L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = static_cast<DWORD>(std::size(file));
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameW(&ofn)) {
    SetWindowTextW(s->out, file);
  }
}

bool RunCaptureProcess(const WindowInfo &w, const std::wstring &method,
                       const std::wstring &out_path, std::wstring *error) {
  std::wostringstream cmd;
  const std::wstring cli_path = CliExePath();
  if (GetFileAttributesW(cli_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    *error = L"screencap-cli.exe was not found next to screencap.exe.";
    return false;
  }

  cmd << QuoteArg(cli_path) << L" cap --method " << method
      << L" --target window --hwnd "
      << reinterpret_cast<uintptr_t>(w.hwnd) << L" --out "
      << QuoteArg(out_path)
      << L" --overwrite --json --timeout-ms 2000 --force-alpha 255";
  std::wstring cmdline = cmd.str();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    *error = L"CreateProcess failed: " + std::to_wstring(GetLastError());
    return false;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (exit_code != 0) {
    *error = L"Capture failed. Exit code: " + std::to_wstring(exit_code);
    return false;
  }
  return true;
}

void CaptureSelected(GuiState *s) {
  int idx = SelectedWindowIndex(s);
  if (idx < 0 || idx >= static_cast<int>(s->windows.size())) {
    MessageBoxW(s->hwnd, L"Select a window first.", L"screencap",
                MB_ICONINFORMATION);
    return;
  }

  auto out_path = GetWindowTextString(s->out);
  if (out_path.empty()) {
    MessageBoxW(s->hwnd, L"Choose an output path first.", L"screencap",
                MB_ICONINFORMATION);
    return;
  }

  EnableWindow(s->capture, FALSE);
  SetStatus(s, L"Capturing...");
  UpdateWindow(s->hwnd);

  std::wstring err;
  bool ok = RunCaptureProcess(s->windows[static_cast<size_t>(idx)],
                              SelectedMethod(s), out_path, &err);
  EnableWindow(s->capture, TRUE);
  if (!ok) {
    SetStatus(s, err);
    MessageBoxW(s->hwnd, err.c_str(), L"screencap", MB_ICONERROR);
    return;
  }

  SetStatus(s, L"Saved: " + out_path);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }

  GuiState *s = GetState(hwnd);
  switch (msg) {
  case WM_CREATE: {
    s->hwnd = hwnd;
    s->refresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
                               0, 0, 0, 0, hwnd, ControlId(kIdRefresh),
                               nullptr, nullptr);
    s->method = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE |
                                               CBS_DROPDOWNLIST,
                              0, 0, 0, 0, hwnd, ControlId(kIdMethod), nullptr,
                              nullptr);
    const wchar_t *methods[] = {L"wgc-window", L"gdi-printwindow",
                                L"gdi-bitblt-windowdc", L"dxgi-window"};
    for (auto *m : methods) {
      SendMessageW(s->method, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m));
    }
    SendMessageW(s->method, CB_SETCURSEL, 0, 0);

    s->out = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0,
                             0, hwnd, ControlId(kIdOut), nullptr, nullptr);
    SetWindowTextW(s->out, DefaultOutputPath().c_str());
    s->browse = CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE, 0,
                              0, 0, 0, hwnd, ControlId(kIdBrowse), nullptr,
                              nullptr);
    s->capture = CreateWindowW(L"BUTTON", L"Capture", WS_CHILD | WS_VISIBLE,
                               0, 0, 0, 0, hwnd, ControlId(kIdCapture),
                               nullptr, nullptr);
    s->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | LVS_REPORT |
                                  LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                              0, 0, 0, 0, hwnd,
                              ControlId(kIdList), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(
        s->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    InitListColumns(s->list);
    s->status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                              0, hwnd, ControlId(kIdStatus), nullptr, nullptr);
    ResizeControls(s);
    RefreshWindows(s);
    return 0;
  }
  case WM_SIZE:
    if (s) {
      ResizeControls(s);
    }
    return 0;
  case WM_COMMAND:
    if (LOWORD(wparam) == kIdRefresh) {
      RefreshWindows(s);
      return 0;
    }
    if (LOWORD(wparam) == kIdBrowse) {
      BrowseOutput(s);
      return 0;
    }
    if (LOWORD(wparam) == kIdCapture) {
      CaptureSelected(s);
      return 0;
    }
    break;
  case WM_NOTIFY: {
    auto *hdr = reinterpret_cast<NMHDR *>(lparam);
    if (hdr->idFrom == kIdList && hdr->code == NM_DBLCLK) {
      CaptureSelected(s);
      return 0;
    }
    break;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int RunGui() {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&icc);

  HINSTANCE inst = GetModuleHandleW(nullptr);
  const wchar_t *class_name = L"ScreencapWindowPicker";
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = class_name;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);

  GuiState state;
  HWND hwnd = CreateWindowExW(0, class_name, L"screencap window picker",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, 900, 560, nullptr, nullptr, inst,
                              &state);
  if (!hwnd) {
    return 1;
  }

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

} // namespace sc
