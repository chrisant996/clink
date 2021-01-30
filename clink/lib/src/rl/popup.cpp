// Copyright (c) 2020 Christoper Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"

#include <core/base.h>
#include <core/str.h>
#include <core/str_iter.h>
#include <core/path.h>
#include <windows.h>
#include <rpc.h> // for UuidCreateSequential
#include <commctrl.h>
#include <shlwapi.h>
#include <VersionHelpers.h>
#include <vector>

extern "C" {
#include <readline/readline.h>
extern int _rl_menu_complete_wraparound;
};

#include "popup.h"

//------------------------------------------------------------------------------
static int ListView_GetCurSel(HWND hwnd)
{
    return ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
}

//------------------------------------------------------------------------------
static void ListView_SetCurSel(HWND hwnd, int index)
{
    int focused = ListView_GetNextItem(hwnd, -1, LVNI_FOCUSED);
    if (focused >= 0)
        ListView_SetItemState(hwnd, focused, 0, LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemState(hwnd, index, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED );
}

//------------------------------------------------------------------------------
static bool any_modifier_keys()
{
    return (GetKeyState(VK_SHIFT) < 0 ||
            GetKeyState(VK_CONTROL) < 0 ||
            GetKeyState(VK_MENU) < 0);
}



//------------------------------------------------------------------------------
static const char** s_items;
static int s_num_completions;
static int s_len_prefix;
static int s_past_flag;
static bool s_display_filter;
static bool s_descriptions;
static bool s_autosize;
static wstr<32> s_find;
static bool s_reset_find_on_next_char = false;

//------------------------------------------------------------------------------
enum class find_mode { next, previous, incremental };

//------------------------------------------------------------------------------
static bool find_in_list(HWND hwnd, find_mode mode)
{
    int dir = (find_mode::previous == mode) ? -1 : 1;
    int row = (find_mode::incremental == mode) ? 0 : max(0, ListView_GetCurSel(hwnd)) + dir;

    if (find_mode::incremental != mode)
        s_reset_find_on_next_char = true;

    str<> find;
    find = s_find.c_str();

    while (row >= 0 && row < s_num_completions)
    {
        if (StrStrI(s_items[row] + s_past_flag, find.c_str()))
        {
            ListView_EnsureVisible(hwnd, row, false);
            ListView_SetCurSel(hwnd, row);
            return true;
        }
        row += dir;
    }

    return false;
}

//------------------------------------------------------------------------------
static void reset_find()
{
    s_find.clear();
    s_reset_find_on_next_char = false;
}



//------------------------------------------------------------------------------
static bool s_modal = false;
static WNDPROC s_prev_listview_proc = nullptr;

//------------------------------------------------------------------------------
static LRESULT CALLBACK SubclassListViewWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CHAR:
        if (wParam == VK_ESCAPE)
        {
            s_modal = false;
            return 0;
        }
        if (!any_modifier_keys())
        {
            if (s_reset_find_on_next_char)
                reset_find();

            // lParam is repeat count.
            for (lParam = (WORD)lParam; lParam--; )
            {
                if (wParam >= ' ')
                {
                    // Typeable character, try to search.
                    s_find.concat((LPCWSTR)&wParam, 1);
                    if (!find_in_list(hwnd, find_mode::incremental))
                        s_find.truncate(s_find.length() - 1);
                }
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_SPACE:
            if (any_modifier_keys())
                s_reset_find_on_next_char = true;
            break;
        case 'L':
            if (GetKeyState(VK_CONTROL) < 0)
            {
                find_in_list(hwnd, GetKeyState(VK_SHIFT) < 0 ? find_mode::previous : find_mode::next);
                return 0;
            }
            break;

        case VK_UP:
            s_reset_find_on_next_char = true;
            if (_rl_menu_complete_wraparound && ListView_GetCurSel(hwnd) <= 0)
            {
                ListView_SetCurSel(hwnd, ListView_GetItemCount(hwnd) - 1);
                return 0;
            }
            break;
        case VK_DOWN:
            s_reset_find_on_next_char = true;
            if (_rl_menu_complete_wraparound)
            {
                int cursel = ListView_GetCurSel(hwnd);
                if (cursel < 0 || cursel == ListView_GetItemCount(hwnd) - 1)
                {
                    ListView_SetCurSel(hwnd, 0);
                    return 0;
                }
            }
            break;

        case VK_LEFT:
        case VK_RIGHT:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_HOME:
        case VK_END:
        case VK_ESCAPE:
            s_reset_find_on_next_char = true;
            break;

        case VK_BACK:
            // lParam is repeat count.
            for (lParam = (WORD)lParam; s_find.length() > 0 && lParam--; )
                s_find.truncate(s_find.length() - 1);
            if (s_find.length())
                find_in_list(hwnd, find_mode::incremental);
            else
                ListView_SetItemState(hwnd, ListView_GetCurSel(hwnd), LVIS_FOCUSED, LVIS_FOCUSED|LVIS_SELECTED);
            return 0;
        case VK_F3:
            find_in_list(hwnd, GetKeyState(VK_SHIFT) < 0 ? find_mode::previous : find_mode::next);
            return 0;
        }
        break;
    }

    return CallWindowProcW(s_prev_listview_proc, hwnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------
static void snap_to_workspace(HWND hwnd)
{
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hmon)
        return;

    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hmon, &mi))
        return;

    RECT rcOriginal;
    RECT rcAdjusted;
    GetWindowRect(hwnd, &rcOriginal);
    rcAdjusted = rcOriginal;

    if (rcAdjusted.right > mi.rcWork.right)
        OffsetRect(&rcAdjusted, mi.rcWork.right - rcAdjusted.right, 0);
    if (rcAdjusted.bottom > mi.rcWork.bottom)
        OffsetRect(&rcAdjusted, 0, mi.rcWork.bottom - rcAdjusted.bottom);
    if (rcAdjusted.left < mi.rcWork.left)
        OffsetRect(&rcAdjusted, mi.rcWork.left - rcAdjusted.left, 0);
    if (rcAdjusted.top < mi.rcWork.top)
        OffsetRect(&rcAdjusted, 0, mi.rcWork.top - rcAdjusted.top);

    if (memcmp(&rcOriginal, &rcAdjusted, sizeof(rcOriginal)) != 0)
        SetWindowPos(hwnd, NULL, rcAdjusted.left, rcAdjusted.top,
            rcAdjusted.right - rcAdjusted.left, rcAdjusted.bottom - rcAdjusted.top,
            SWP_NOZORDER|SWP_NOOWNERZORDER);
}



//------------------------------------------------------------------------------
static const WCHAR popup_class_name[] = L"CLINK_Completion_Popup";
static HINSTANCE s_hinst = 0;
static HWND s_hwnd_popup = 0;
static HWND s_hwnd_list = 0;
static int s_current = 0;
static popup_list_result s_result;
#define IDC_LISTVIEW 4000

//------------------------------------------------------------------------------
static int rotate_index = 0;
static WCHAR rotate_strings[4][1024];

//------------------------------------------------------------------------------
inline BYTE AlphaBlend(BYTE a, BYTE b, BYTE a_alpha)
{
    return (WORD(a) * a_alpha / 255) + (WORD(b) * (255 - a_alpha) / 255);
}

//------------------------------------------------------------------------------
static COLORREF AlphaBlend(HDC hdc, BYTE fg_alpha)
{
    COLORREF bg = GetBkColor(hdc);
    COLORREF fg = GetTextColor(hdc);
    return RGB(AlphaBlend(GetRValue(fg), GetRValue(bg), fg_alpha),
               AlphaBlend(GetGValue(fg), GetGValue(bg), fg_alpha),
               AlphaBlend(GetBValue(fg), GetBValue(bg), fg_alpha));
}

//------------------------------------------------------------------------------
static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NOTIFY:
        {
            LPNMHDR pnm = (LPNMHDR)lParam;
            if (pnm->idFrom != IDC_LISTVIEW)
                goto LDefault;

            switch (pnm->code)
            {
            case NM_CUSTOMDRAW:
                {
                    LPNMLVCUSTOMDRAW plvcd = (LPNMLVCUSTOMDRAW)lParam;
                    switch (plvcd->nmcd.dwDrawStage)
                    {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                        return CDRF_NOTIFYSUBITEMDRAW;
                    case CDDS_SUBITEM|CDDS_ITEMPREPAINT:
                        if (plvcd->iSubItem == 0)
                            return CDRF_DODEFAULT;
                        plvcd->clrText = AlphaBlend(plvcd->nmcd.hdc, 0x99);
                        return CDRF_NEWFONT;
                    }
                }
                break;

            case LVN_ITEMACTIVATE:
                {
                    LPNMITEMACTIVATE pia = (LPNMITEMACTIVATE)pnm;
                    if (pia->uKeyFlags & LVKF_ALT)
                        break;
                    s_current = pia->iItem;
                    s_result = ((pia->uKeyFlags & (LVKF_CONTROL | LVKF_SHIFT)) ?
                        popup_list_result::select : popup_list_result::use);
                    s_modal = false;
                }
                break;

            case LVN_GETDISPINFOW:
                {
                    NMLVDISPINFOW *pdi = (NMLVDISPINFOW *)pnm;
                    if (pdi->item.mask & LVIF_TEXT)
                    {
                        wstr_base out(rotate_strings[rotate_index], sizeof_array(rotate_strings[rotate_index]));
                        rotate_index = (rotate_index + 1) % sizeof_array(rotate_strings);

                        out.clear();
                        if (pdi->item.iSubItem == 0)
                        {
                            bool filtered = false;
                            const char* display = s_items[pdi->item.iItem] + s_past_flag + s_len_prefix;
                            if (s_display_filter)
                            {
                                const char* ptr = display + strlen(display) + 1;
                                if (*ptr)
                                {
                                    display = ptr;
                                    filtered = true;
                                }
                            }

                            to_utf16(out, display);

                            if (!filtered &&
                                s_past_flag &&
                                IS_MATCH_TYPE_DIR(s_items[pdi->item.iItem][0]) &&
                                out.length() &&
                                !path::is_separator(out.c_str()[out.length() - 1]))
                            {
                                WCHAR sep[2] = {(unsigned char)rl_preferred_path_separator};
                                out.concat(sep);
                            }
                        }
                        else if (pdi->item.iSubItem == 1 && s_display_filter)
                        {
                            const char* ptr = s_items[pdi->item.iItem];
                            ptr += strlen(ptr) + 1;
                            ptr += strlen(ptr) + 1;
                            out.concat(L"    ");
                            to_utf16(out, ptr);
                        }

                        pdi->item.pszText = out.data();
                    }
                }
                break;

            default:
                goto LDefault;
            }
        }
        break;

    case WM_CHAR:
        if (wParam == VK_ESCAPE)
        {
            s_modal = false;
            break;
        }
        goto LDefault;

    case WM_SIZE:
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            SetWindowPos(s_hwnd_list, NULL, 0, 0, rcClient.right, rcClient.bottom, SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_DRAWFRAME);

            int cxFull = rcClient.right - rcClient.left - GetSystemMetrics(SM_CXVSCROLL);
            if (s_descriptions)
            {
                int cx = s_autosize ? LVSCW_AUTOSIZE : cxFull / 3;
                ListView_SetColumnWidth(s_hwnd_list, 0, cx);
                cx = s_autosize ? LVSCW_AUTOSIZE_USEHEADER : cxFull - cx;
                ListView_SetColumnWidth(s_hwnd_list, 1, cx);
            }
            else
            {
                ListView_SetColumnWidth(s_hwnd_list, 0, cxFull);
            }
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE)
        {
            s_modal = false;
            break;
        }
        goto LDefault;

    case WM_NCDESTROY:
        s_modal = false;
        s_hwnd_popup = 0;
        goto LDefault;

    case WM_CREATE:
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int cxFull = (rcClient.right - rcClient.left - GetSystemMetrics(SM_CXVSCROLL));

            s_hwnd_list = CreateWindowW(WC_LISTVIEWW, L"", WS_VISIBLE|WS_CHILD|LVS_SINGLESEL|LVS_NOCOLUMNHEADER|LVS_REPORT|LVS_OWNERDATA,
                                        0, 0, rcClient.right, rcClient.bottom, hwnd, (HMENU)IDC_LISTVIEW, s_hinst, NULL);
            if (!s_hwnd_list)
                return -1;

            reset_find();
            s_prev_listview_proc = (WNDPROC)SetWindowLongPtrW(s_hwnd_list, GWLP_WNDPROC, (LPARAM)(WNDPROC)SubclassListViewWndProc);

            ListView_SetExtendedListViewStyle(s_hwnd_list, LVS_EX_FULLROWSELECT|LVS_EX_INFOTIP|LVS_EX_DOUBLEBUFFER|LVS_EX_AUTOSIZECOLUMNS);

            LVCOLUMN col = {};
            col.mask = LVCF_WIDTH;
            if (s_descriptions)
            {
                col.cx = s_autosize ? LVSCW_AUTOSIZE : cxFull / 3;
                ListView_InsertColumn(s_hwnd_list, 0, &col);
                col.cx = s_autosize ? LVSCW_AUTOSIZE_USEHEADER : cxFull - col.cx;
                ListView_InsertColumn(s_hwnd_list, 1, &col);
            }
            else
            {
                col.cx = s_autosize ? LVSCW_AUTOSIZE : cxFull;
                ListView_InsertColumn(s_hwnd_list, 0, &col);
            }

            ListView_SetItemCount(s_hwnd_list, s_num_completions);

            SetFocus(s_hwnd_list);
        }
        break;

    default:
LDefault:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

//------------------------------------------------------------------------------
struct enum_console_child_windows_info
{
    LONG cyTitle;
    LONG cChildren;
};

//------------------------------------------------------------------------------
static BOOL CALLBACK enum_console_child_windows(HWND hwnd, LPARAM lParam)
{
    enum_console_child_windows_info* info = (enum_console_child_windows_info*)lParam;
    WCHAR title[32];
    if (GetClassNameW(hwnd, title, sizeof_array(title)))
    {
        if (wcscmp(title, L"DRAG_BAR_WINDOW_CLASS") == 0)
        {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            info->cyTitle = rc.bottom - rc.top;
            return false;
        }
    }
    return (++info->cChildren < 8);
}

//------------------------------------------------------------------------------
static LONG get_console_cell_height(HWND hwndConsole, RECT* prc, const CONSOLE_SCREEN_BUFFER_INFO* pcsbi)
{
    // Windows Terminal uses XAML, and its client window rect includes the title
    // bar.  So try to compensate by looking for DRAG_BAR_WINDOW_CLASS.
    enum_console_child_windows_info info = {};
    EnumChildWindows(hwndConsole, enum_console_child_windows, LPARAM(&info));
    prc->top += info.cyTitle;

    // Calculate the cell height from the window surface height and the number
    // of lines in the visible console display window.
    LONG cLines = pcsbi->srWindow.Bottom + 1 - pcsbi->srWindow.Top;
    LONG cyCell = (prc->bottom - prc->top) / cLines;

    // Inset the window coordinates if there seems to be a margin.
    LONG margin = ((prc->bottom - prc->top) - (cLines * cyCell)) / 2;
    InflateRect(prc, -margin, -margin);
    return cyCell;
}

//------------------------------------------------------------------------------
static HWND create_popup_window(HWND hwndConsole, LPCSTR title, const CONSOLE_SCREEN_BUFFER_INFO* pcsbi)
{
    // Register the window class if it hasn't been yet.
    static bool s_registered = false;
    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = PopupWndProc;
        wc.hInstance = s_hinst;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.lpszClassName = popup_class_name;
        if (!RegisterClassW(&wc))
            return NULL;
        s_registered = 1;
    }

    // Compute position and size for the window.
    RECT rcPopup;
    GetClientRect(hwndConsole, &rcPopup);
    MapWindowPoints(hwndConsole, NULL, (LPPOINT)&rcPopup, 2);
    LONG cyCell = get_console_cell_height(hwndConsole, &rcPopup, pcsbi);
    rcPopup.top += cyCell * (pcsbi->dwCursorPosition.Y + 1 - pcsbi->srWindow.Top);
    if (rcPopup.top > rcPopup.bottom)
    {
        // Console APIs aren't rich enough to accurately identify the cell
        // height (or the pixel coordinates of the console rendering surface).
        // So in case the cell height math is inaccurate, clip the top so it
        // doesn't go past the window bounds.
        rcPopup.top = rcPopup.bottom;
    }
    rcPopup.bottom = rcPopup.top + 200;

    // Create the window.
    wstr<> wtitle;
    wtitle = title;
    HWND hwndPopup = CreateWindowW(popup_class_name, wtitle.c_str(),
                                   WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU,
                                   rcPopup.left, rcPopup.top, rcPopup.right - rcPopup.left, rcPopup.bottom - rcPopup.top,
                                   hwndConsole, NULL, s_hinst, NULL);
    if (!hwndPopup)
        return NULL;

    RECT rcItem;
    RECT rcPopupClient;
    ListView_GetItemRect(s_hwnd_list, 0, &rcItem, LVIR_BOUNDS);
    GetClientRect(hwndPopup, &rcPopupClient);

    // Set cur sel *after* getting rect for item 0, otherwise item 0 may not be
    // the top visible item and its coordinates could be negative.
    ListView_SetCurSel(s_hwnd_list, s_current);
    ListView_EnsureVisible(s_hwnd_list, s_current, false);

    LONG cyPopup = rcItem.top + (rcItem.bottom - rcItem.top) * 10; // The items.
    cyPopup += (rcPopup.bottom - rcPopup.top) - (rcPopupClient.bottom - rcPopupClient.top); // The caption and chrome.
    rcPopup.bottom = rcPopup.top + cyPopup;
    SetWindowPos(hwndPopup, NULL, rcPopup.left, rcPopup.top, rcPopup.right - rcPopup.left, rcPopup.bottom - rcPopup.top, SWP_NOOWNERZORDER|SWP_NOZORDER);
    snap_to_workspace(hwndPopup);

    ShowWindow(hwndPopup, SW_SHOW);
    return hwndPopup;
}

//------------------------------------------------------------------------------
static HWND get_top_level_window(HWND hwnd)
{
    HWND hwndTop = hwnd;
    HWND hwndWalk = hwnd;

    while (hwndWalk)
    {
        hwndTop = hwndWalk;
        hwndWalk = GetParent(hwndWalk);
    }

    return hwndTop;
}

//------------------------------------------------------------------------------
static HWND get_console_window()
{
    // Use a multi-pass strategy.  If any pass acquires a non-zero window handle
    // that is visible, then return it.  Otherwise continue to the next pass.
    for (int pass = 1; true; pass++)
    {
        HWND hwndConsole = 0;
        switch (pass)
        {
        case 1:
            {
                // The first pass uses GetConsoleWindow.
                hwndConsole = GetConsoleWindow();
            }
            break;
        case 2:
            {
                // Try the old FindWindow method.  Windows Terminal returns the
                // ConPTY (pseudo console) window, which is a hidden window, so
                // it isn't usable for our purposes.

                // Save the console title.
                WCHAR title[1024];
                if (!GetConsoleTitleW(title, sizeof_array(title)))
                    break;

                // Set the console title to something unique.
                static const WCHAR hex_digit[] = L"0123456789ABCDEF";
                UUID uuid = {};
                if (FAILED(UuidCreateSequential(&uuid)))
                    break;
                wstr<> unique;
                const BYTE* uuid_bytes = LPBYTE(&uuid);
                for (size_t index = 0; index < sizeof(uuid); index++)
                {
                    WCHAR byte[2];
                    byte[0] = hex_digit[(uuid_bytes[index] & 0xf0) >> 4];
                    byte[1] = hex_digit[(uuid_bytes[index] & 0x0f) >> 0];
                    unique.concat(byte, 2);
                }
                unique.concat(L"_clink_findwindow");
                if (!SetConsoleTitleW(unique.c_str()))
                    break;

                // Make sure the title was updated (yikes).
                Sleep(50);

                // Find the unique title.
                hwndConsole = FindWindowW(nullptr, unique.c_str());

                // Restore the saved console title.
                SetConsoleTitleW(title);
            }
            break;
        default:
            // Give up.
            return 0;
        }

        if (hwndConsole)
        {
            DWORD style = GetWindowLong(hwndConsole, GWL_STYLE);
            if (style & WS_VISIBLE)
                return hwndConsole;
        }
    }
}

//------------------------------------------------------------------------------
popup_list_result do_popup_list(
    const char* title,
    const char** items,
    int num_items,
    int len_prefix,
    int past_flag,
    bool completing,
    bool auto_complete,
    int& current,
    str_base& out,
    bool display_filter)
{
    if (!items)
        return popup_list_result::error;

    s_past_flag = past_flag;
    s_display_filter = display_filter;
    s_descriptions = false;
    s_autosize = IsWindowsVistaOrGreater();

    out.clear();
    s_result = popup_list_result::cancel;

    if (completing)
    {
        if (num_items == 0 || !items[0])
            return popup_list_result::error;

        if (num_items == 1 || !items[1])
        {
            if (!auto_complete)
                return popup_list_result::error;

            out = items[0];
            current = 0;
            return popup_list_result::use;
        }

        items++;
        num_items--;
    }

    if (s_display_filter)
    {
        for (int i = num_items; i--;)
        {
            const char* ptr = items[i];
            size_t len_match = strlen(ptr);
            ptr += len_match + 1;
            size_t len_display = strlen(ptr);
            ptr += len_display + 1;
            if (*ptr)
            {
                s_descriptions = true;
                break;
            }
        }
    }

    // Can't show an empty popup list.
    if (num_items <= 0)
        return popup_list_result::error;

    // It must be a console in order to pop up a GUI window.
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return popup_list_result::error;

    // HMODULE and HINSTANCE are interchangeable, so get the HMODULE and cast.
    // This is needed by create_popup_window().
    s_hinst = (HINSTANCE)GetModuleHandle(NULL);

    // Get the console window handle.
    HWND hwndConsole = get_console_window();
    if (!hwndConsole)
        return popup_list_result::error;

    // Create popup list window.
    s_items = items;
    s_num_completions = num_items;
    s_len_prefix = len_prefix;
    s_current = current;
    if (s_current < 0 || s_current >= s_num_completions)
        s_current = 0;
    s_hwnd_popup = create_popup_window(hwndConsole, title, &csbi);
    if (!s_hwnd_popup)
        return popup_list_result::error;

    // Disable parent, for modality.
    HWND hwndTop = get_top_level_window(hwndConsole);
    if (hwndTop && IsWindowEnabled(hwndTop) && hwndTop != s_hwnd_popup)
        EnableWindow(hwndTop, false);
    else
        hwndTop = NULL;
    EnableWindow(hwndConsole, false);

    // Pump messages until the modal state is done (or on WM_QUIT).
    bool quit = false;
    MSG msg;
    s_modal = true;
    while (s_modal)
    {
        if (!GetMessageW(&msg, NULL, 0, 0))
        {
            quit = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Re-enable windows.
    EnableWindow(hwndConsole, true);
    if (hwndTop)
        EnableWindow(hwndTop, true);
    SetForegroundWindow(hwndConsole);
    SetFocus(hwndConsole);

    // Cleanup in case something went badly wrong above.
    if (s_hwnd_popup)
        DestroyWindow(s_hwnd_popup);

    // I think WM_QUIT shouldn't happen, and I don't know if this will work
    // properly anyway, but here's a stab at forwarding the WM_QUIT message.
    if (quit)
        PostQuitMessage(0);

    current = s_current;
    out = s_items[current];
    return s_result;
}

