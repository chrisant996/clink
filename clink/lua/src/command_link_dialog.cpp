// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include <windowsx.h>
#include <CommCtrl.h>
#include "command_link_dialog.h"

// Define UPDATE_PROMPT_NO_ACTIVATE to make the update prompt not receive
// activation unless "clink update" was run manually.
#define UPDATE_PROMPT_NO_ACTIVATE

#ifndef BCM_SETNOTE
#define BCM_SETNOTE                 (BCM_FIRST + 0x0009)
#define Button_SetNote(hwnd, psz)   (BOOL)SNDMSG((hwnd), BCM_SETNOTE, 0, (LPARAM)(psz))
#define BS_COMMANDLINK              0x0000000EL
#define BS_DEFCOMMANDLINK           0x0000000FL
#endif

constexpr int32 c_max_buttons = 5;
enum
{
    IDC_BACKGROUND  = 990,
    IDC_MESSAGE,

    IDC_BUTTON1     = 1000,
    IDC_BUTTON2,
    IDC_BUTTON3,
    IDC_BUTTON4,
    IDC_BUTTON5,
};

command_link_dialog::command_link_dialog()
{
}

command_link_dialog::~command_link_dialog()
{
    if (m_hfont_normal)
        DeleteObject(m_hfont_normal);
    if (m_hfont_bold)
        DeleteObject(m_hfont_bold);
}

void command_link_dialog::add(int32 choice, const char* caption1, const char* caption2, cld_callback_t handler)
{
    assert(choice);
    button_details details;
    details.m_choice = choice;
    details.m_caption1 = caption1;
    details.m_caption2 = (caption2 && *caption2) ? caption2 : "";
    details.m_command_link = false;
    details.m_handler = handler;
    m_buttons.emplace_back(std::move(details));
}

int32 command_link_dialog::do_modal(HWND parent, int16 width, const char* title, const char* message)
{
    assert(!m_buttons.empty());
    m_title = title;
    m_message = message;
    m_width = width;
    m_choice = 0;
    m_continue = true;

    struct FULL_DLGTEMPLATE : DLGTEMPLATE
    {
        WORD _menu;
        WORD _class;
        WORD _title;
        WORD _point_size;
        WCHAR _font_name[128];
    };

    FULL_DLGTEMPLATE dlgtemplate = {};
    dlgtemplate.style = DS_CENTER|DS_MODALFRAME|WS_CAPTION|WS_SYSMENU;
    dlgtemplate.cx = width;
    dlgtemplate.cy = 40;

    static bool s_commctrl = false;
    if (!s_commctrl)
    {
        HMODULE hlib = LoadLibrary("comctl32.dll");
        if (hlib)
        {
            union
            {
                FARPROC proc[1];
                struct {
                    BOOL (WINAPI* InitCommonControlsEx)(const INITCOMMONCONTROLSEX* picce);
                };
            } comctl32;

            comctl32.proc[0] = GetProcAddress(hlib, "InitCommonControlsEx");
            if (comctl32.proc[0])
            {
                INITCOMMONCONTROLSEX icce = { sizeof(icce), ICC_LINK_CLASS };
                assert(comctl32.InitCommonControlsEx(&icce));
            }
        }
        s_commctrl = true;
    }

    const HWND hdlg = CreateDialogIndirectParamW(0, &dlgtemplate, parent, static_dlgproc, LPARAM(this));
    assert(hdlg);
    assert(hdlg == m_hdlg);

    if (m_hdlg)
    {
        LockSetForegroundWindow(LSFW_LOCK);

#ifdef UPDATE_PROMPT_NO_ACTIVATE
        ShowWindow(m_hdlg, SW_SHOWNOACTIVATE);
#else
        ShowWindow(m_hdlg, SW_SHOWNORMAL);
#endif

        FLASHWINFO fwi = { sizeof(fwi) };
        fwi.hwnd = m_hdlg;
        fwi.dwFlags = FLASHW_TRAY|FLASHW_TIMERNOFG;
        FlashWindowEx(&fwi);

        MSG msg;
        while (m_continue && m_hdlg)
        {
            if (!GetMessageW(&msg, NULL, 0, 0))
            {
                PostQuitMessage(0);
                end_dialog(0);
                break;
            }
            else if (!IsDialogMessageW(m_hdlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (m_hdlg)
            DestroyWindow(m_hdlg);
        assert(!m_hdlg);
    }

    return m_choice;
}

INT_PTR command_link_dialog::dlgproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CTLCOLORSTATIC:
        {
            const UINT idc = GetDlgCtrlID(HWND(lParam));
            switch (idc)
            {
            case IDC_BACKGROUND:
            case IDC_MESSAGE:
                SetTextColor(HDC(wParam), GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(HDC(wParam), GetSysColor(COLOR_WINDOW));
                return INT_PTR(GetSysColorBrush(COLOR_WINDOW));
            }
        }
        goto LDefault;

    case WM_DRAWITEM:
        if (wParam >= IDC_BUTTON1 && wParam <= IDC_BUTTON5)
        {
            const DRAWITEMSTRUCT* pdis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            owner_draw(pdis->hwndItem, pdis->hDC, pdis->itemState, uint32(wParam - IDC_BUTTON1));
            break;
        }
        goto LDefault;

    case WM_NOTIFY:
        {
            const NMHDR* pnm = reinterpret_cast<NMHDR*>(lParam);
            switch (pnm->code)
            {
            case NM_CUSTOMDRAW:
                switch(pnm->idFrom)
                {
                case IDC_BUTTON1:
                case IDC_BUTTON2:
                case IDC_BUTTON3:
                case IDC_BUTTON4:
                case IDC_BUTTON5:
                    custom_draw(reinterpret_cast<const NMCUSTOMDRAW*>(pnm), uint32(pnm->idFrom - IDC_BUTTON1));
                    return true;
                }
                break;
            }
        }
        goto LDefault;

    case WM_INITDIALOG:
        {
            assert(GetAncestor(m_hdlg, GA_ROOT) == m_hdlg);

            constexpr int32 c_xmargin = 8;
            constexpr int32 c_ymargin = 8;
            constexpr float c_scale_bold_font = 1.25f;

            const HDC hdc = GetDC(m_hdlg);

            // Make font.
            {
                LOGFONT lf;
                NONCLIENTMETRICS ncm = { sizeof(ncm) - sizeof(ncm.iPaddedBorderWidth) };
                SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);
                lf = ncm.lfMessageFont;
                m_hfont_normal = CreateFontIndirect(&lf);
                lf.lfWeight = FW_BOLD;
                lf.lfHeight = int32(lf.lfHeight * c_scale_bold_font);
                m_hfont_bold = CreateFontIndirect(&lf);

                TEXTMETRIC tm;
                const HFONT hfontOld = SelectFont(hdc, m_hfont_normal);
                GetTextMetrics(hdc, &tm);
                m_cy_normal = tm.tmHeight;
                SelectFont(hdc, m_hfont_bold);
                GetTextMetrics(hdc, &tm);
                m_cy_bold = tm.tmHeight;
                SelectFont(hdc, hfontOld);
            }

            // Init title and message text.
            RECT rcCtl;
            LONG y = (c_ymargin * 2);
            if (m_title.length())
            {
                SetWindowText(m_hdlg, m_title.c_str());
                m_title.clear();
            }
            if (m_message.length())
            {
                GetClientRect(m_hdlg, &rcCtl);
                InflateRect(&rcCtl, 0 - (c_xmargin * 2), 0);
                rcCtl.top = y;
                rcCtl.bottom = rcCtl.top + m_cy_normal;

                HWND hctl = CreateWindowEx(0, WC_STATIC, m_message.c_str(), SS_LEFT|WS_CHILD|WS_VISIBLE, rcCtl.left, rcCtl.top, rcCtl.right - rcCtl.left, 1, m_hdlg, HMENU(IDC_MESSAGE), 0, nullptr);
                if (hctl)
                {
                    SendMessage(hctl, WM_SETFONT, WPARAM(m_hfont_normal), 0);

                    RECT rcText = rcCtl;
                    const HFONT hfontOld = SelectFont(hdc, m_hfont_normal);
                    DrawText(hdc, m_message.c_str(), m_message.length(), &rcText, DT_CALCRECT|DT_WORDBREAK);
                    if (rcText.bottom < rcCtl.bottom)
                    {
                        rcText = rcCtl;
                        DrawText(hdc, m_message.c_str(), m_message.length(), &rcText, DT_CALCRECT|DT_SINGLELINE);
                    }
                    SelectFont(hdc, hfontOld);

                    rcCtl.bottom = rcCtl.top + (rcText.bottom - rcText.top);
                    SetWindowPos(hctl, 0, rcCtl.left, rcCtl.top, rcCtl.right - rcCtl.left, rcCtl.bottom - rcCtl.top, SWP_NOACTIVATE|SWP_NOZORDER);
                }

                y = rcCtl.bottom + (c_ymargin * 2);
            }

            ReleaseDC(m_hdlg, hdc);

            // Initialize the buttons as either Command Link buttons or as
            // simple Owner Draw buttons, depending on the OS version (as
            // inferred by whether BCM_SETNOTE succeeds).
            const size_t num = std::min<size_t>(5, m_buttons.size());
            for (size_t ii = 0; ii < num; ++ii)
            {
                GetClientRect(m_hdlg, &rcCtl);
                InflateRect(&rcCtl, -c_xmargin, 0);
                rcCtl.top = y;
                rcCtl.bottom = y + m_cy_bold + m_cy_normal + 24;

                UINT flags = BS_PUSHBUTTON|WS_TABSTOP|WS_CHILD|WS_VISIBLE;
#ifdef UPDATE_PROMPT_NO_ACTIVATE
                if (!ii)
                    flags |= BS_DEFPUSHBUTTON;
#endif

                HWND hctl = CreateWindowEx(0, WC_BUTTON, m_buttons[ii].m_caption1.c_str(), BS_COMMANDLINK|flags, rcCtl.left, rcCtl.top, rcCtl.right - rcCtl.left, rcCtl.bottom - rcCtl.top, m_hdlg, HMENU(UINT_PTR(IDC_BUTTON1 + ii)), 0, nullptr);
                m_buttons[ii].m_command_link = hctl && SendMessage(hctl, BCM_SETNOTE, 0, LPARAM(m_buttons[ii].m_caption2.c_str()));
                if (!m_buttons[ii].m_command_link)
                {
                    if (hctl)
                        DestroyWindow(hctl);
                    hctl = CreateWindowEx(0, WC_BUTTON, m_buttons[ii].m_caption1.c_str(), BS_OWNERDRAW|flags, rcCtl.left, rcCtl.top, rcCtl.right - rcCtl.left, rcCtl.bottom - rcCtl.top, m_hdlg, HMENU(UINT_PTR(IDC_BUTTON1 + ii)), 0, nullptr);
                }

                y = rcCtl.bottom + c_ymargin;
            }

            const int32 background_height = y;

            // Create IDCANCEL button.
            {
                y += c_ymargin;
                rcCtl.top = y;
                rcCtl.bottom = rcCtl.top + m_cy_normal + (c_ymargin * 2);
                rcCtl.left = rcCtl.right - 80;
                HWND hctl = CreateWindowEx(0, WC_BUTTON, "Cancel", BS_PUSHBUTTON|WS_TABSTOP|WS_CHILD|WS_VISIBLE|WS_GROUP, rcCtl.left, rcCtl.top, rcCtl.right - rcCtl.left, rcCtl.bottom - rcCtl.top, m_hdlg, HMENU(UINT_PTR(IDCANCEL)), 0, nullptr);
                SendMessage(hctl, WM_SETFONT, WPARAM(m_hfont_normal), 0);
                y = rcCtl.bottom + c_ymargin;
            }

            // Create background control.
            {
                HWND hctl = CreateWindowEx(0, WC_STATIC, "", SS_LEFT|WS_CHILD|WS_VISIBLE|WS_GROUP, 0, 0, rcCtl.right + rcCtl.left, background_height, m_hdlg, HMENU(IDC_BACKGROUND), 0, nullptr);
                SetWindowPos(hctl, 0, 0, 0, 0, 0, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
            }

            // Adjust the dialog size to fit the buttons.
            RECT rcClient;
            RECT rcWindow;
            GetClientRect(m_hdlg, &rcClient);
            GetWindowRect(m_hdlg, &rcWindow);
            const LONG adjust = y - (rcClient.bottom - rcClient.top);
            rcWindow.top -= adjust / 2;
            rcWindow.bottom += adjust - adjust / 2;
            SetWindowPos(m_hdlg, 0, rcWindow.left, rcWindow.top, rcWindow.right - rcWindow.left, rcWindow.bottom - rcWindow.top, SWP_NOACTIVATE|SWP_NOZORDER);

#ifdef UPDATE_PROMPT_NO_ACTIVATE
            return 0;
#endif
        }
        break;

    case WM_CLOSE:
        // The Close [X] button and SC_CLOSE post a WM_CLOSE message.
        end_dialog(0);
        break;

    default:
LDefault:
        return false;
    }

    return true;
}

bool command_link_dialog::on_command(WORD id, HWND hwnd, WORD code)
{
    switch (id)
    {
    case IDC_BUTTON1:
    case IDC_BUTTON2:
    case IDC_BUTTON3:
    case IDC_BUTTON4:
    case IDC_BUTTON5:
        {
            const uint32 index = id - IDC_BUTTON1;
            if (m_buttons[index].m_handler && !m_buttons[index].m_handler(m_hdlg, index))
                break;
            end_dialog(m_buttons[index].m_choice);
        }
        break;

    case IDCANCEL:
        end_dialog(0);
        break;

    default:
        return false;
    }

    return true;
}

void command_link_dialog::owner_draw(HWND hwnd, HDC hdc, DWORD dwState, uint32 index)
{
    NMCUSTOMDRAW nm = {};
    nm.hdc = hdc;
    nm.dwDrawStage = CDDS_POSTPAINT;
    ::GetClientRect(hwnd, &nm.rc);
    nm.hdr.hwndFrom = hwnd;
    nm.hdr.idFrom = IDC_BUTTON1 + index;
    nm.hdr.code = NM_CUSTOMDRAW;

    UINT dfcs = 0;
    if (dwState & ODS_SELECTED)
    {
        dfcs |= DFCS_PUSHED;
        nm.uItemState = CDIS_SELECTED;
    }

    DrawFrameControl(hdc, &nm.rc, DFC_BUTTON, DFCS_BUTTONPUSH|dfcs);

    custom_draw(&nm, index);

    if (dwState & ODS_FOCUS)
    {
        RECT rc = nm.rc;
        InflateRect(&rc, -3, -3);

        const int oldMode = SetBkMode(hdc, OPAQUE);
        DrawFocusRect(hdc, &rc);
        SetBkMode(hdc, oldMode);
    }
}

void command_link_dialog::custom_draw(NMCUSTOMDRAW const* pnm, uint32 index)
{
    assert(pnm->hdr.code == NM_CUSTOMDRAW);

    const button_details& choice = m_buttons[index];

    switch (pnm->dwDrawStage)
    {
    case CDDS_PREERASE:
        FillRect(pnm->hdc, &pnm->rc, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(pnm->hdc, TRANSPARENT);
        SetWindowLong(m_hdlg, DWLP_MSGRESULT, choice.m_command_link ? CDRF_DODEFAULT : CDRF_DODEFAULT|CDRF_NOTIFYPOSTPAINT);
        break;

    case CDDS_PREPAINT:
        if (!choice.m_command_link)
            SetWindowLong(m_hdlg, DWLP_MSGRESULT, CDRF_NOTIFYPOSTPAINT);
        break;

    case CDDS_POSTPAINT:
        if (!choice.m_command_link)
        {
            RECT rc = pnm->rc;
            InflateRect(&rc, -16, -4);

            if (pnm->uItemState & CDIS_SELECTED)
                OffsetRect(&rc, 1, 1);

            const int32 cy = m_cy_bold + m_cy_normal;
            if (rc.bottom - rc.top > cy)
            {
                rc.top += (rc.bottom - rc.top - cy) / 2;
                rc.bottom = rc.top + cy;
            }

            const int32 modeBkOld = SetBkMode(pnm->hdc, TRANSPARENT);
            const HFONT hfontOld = SelectFont(pnm->hdc, m_hfont_bold);

            DrawText(pnm->hdc, choice.m_caption1.c_str(), choice.m_caption1.length(), &rc, DT_SINGLELINE);

            rc.top += m_cy_bold;
            SelectFont(pnm->hdc, m_hfont_normal);

            DrawTextW(pnm->hdc, choice.m_caption2.c_str(), choice.m_caption2.length(), &rc, DT_END_ELLIPSIS|DT_SINGLELINE|DT_NOPREFIX);

            SelectFont(pnm->hdc, hfontOld);
            SetBkMode(pnm->hdc, modeBkOld);
        }
        break;
    }
}

void command_link_dialog::end_dialog(int32 choice)
{
    m_choice = choice;

    if (m_continue)
    {
        m_continue = false;
        PostMessage(m_hdlg, WM_NULL, 0, 0);
    }
}

INT_PTR CALLBACK command_link_dialog::static_dlgproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    command_link_dialog* pThis;

    if (msg == WM_INITDIALOG)
    {
        pThis = reinterpret_cast<command_link_dialog*>(lParam);
        SetWindowLongPtr(hwnd, DWLP_USER, DWORD_PTR(pThis));
        pThis->m_hdlg = hwnd;
    }
    else
    {
        pThis = reinterpret_cast<command_link_dialog*>(GetWindowLongPtr(hwnd, DWLP_USER));
    }

    if (!pThis)
        return false;

    assert(pThis->m_hdlg == hwnd);

    const INT_PTR lResult = pThis->dlgproc(msg, wParam, lParam);

    if (msg == WM_DESTROY)
    {
        assert(!pThis->m_continue);
    }
    else if (msg == WM_NCDESTROY)
    {
        SetWindowLongPtr(hwnd, DWLP_USER, 0);
        pThis->m_hdlg = 0;
    }

    // Must return actual result in order for things like WM_CTLCOLORLISTBOX
    // to work.
    if (lResult || msg == WM_INITDIALOG)
        return lResult;

    if (msg == WM_COMMAND)
    {
        const WORD id = GET_WM_COMMAND_ID(wParam, lParam);
        const HWND hwnd = GET_WM_COMMAND_HWND(wParam, lParam);
        const WORD code = GET_WM_COMMAND_CMD(wParam, lParam);
        return pThis->on_command(id, hwnd, code);
    }

    return false;
}
