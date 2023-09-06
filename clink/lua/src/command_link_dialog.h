// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include <core/str.h>
#include <vector>
#include <CommCtrl.h>

//------------------------------------------------------------------------------
class command_link_dialog
{
    struct button_details
    {
        int32           m_choice;
        str_moveable    m_caption1;
        wstr_moveable   m_caption2;
        bool            m_command_link = false;
    };

public:
                        command_link_dialog();
                        ~command_link_dialog();
    void                add(int32 choice, const char* caption1, const char* caption2=nullptr);
    int32               do_modal(HWND parent, int16 width, const char* title, const char* message);

protected:
    INT_PTR             dlgproc(UINT msg, WPARAM wParam, LPARAM lParam);
    bool                on_command(WORD id, HWND hwnd, WORD code);
    void                owner_draw(HWND hwnd, HDC hdc, DWORD dwState, uint32 index);
    void                custom_draw(NMCUSTOMDRAW const* pnm, uint32 index);
    void                end_dialog(int32 choice);

    static INT_PTR CALLBACK static_dlgproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND                m_hdlg = 0;
    std::vector<button_details> m_buttons;
    str_moveable        m_title;
    str_moveable        m_message;
    int32               m_width;
    int32               m_choice = 0;
    int32               m_cy_bold;
    int32               m_cy_normal;
    HFONT               m_hfont_bold = 0;
    HFONT               m_hfont_normal = 0;
    bool                m_continue;
};
