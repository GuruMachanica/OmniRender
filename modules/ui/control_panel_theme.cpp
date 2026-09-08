// filepath: modules/ui/control_panel_theme.cpp
#include "control_panel_theme.h"

namespace omnirender::ui {

namespace {
constexpr COLORREF kBgColor        = RGB(13, 17, 23);     // #0D1117 Main Canvas
constexpr COLORREF kCardColor      = RGB(22, 27, 34);     // #161B22 Group Containers
constexpr COLORREF kBorderColor    = RGB(48, 54, 61);     // #30363D Border Lines
constexpr COLORREF kButtonGreen    = RGB(35, 134, 54);    // #238636 Primary Button
constexpr COLORREF kButtonHover    = RGB(46, 160, 67);    // #2EA043 Hover State
constexpr COLORREF kDarkBtnColor   = RGB(33, 38, 45);     // #21262D Secondary Buttons
constexpr COLORREF kDarkBtnBorder  = RGB(54, 61, 70);     // #363D46
constexpr COLORREF kTextColor      = RGB(240, 246, 252);  // #F0F6FC Crisp White
}

void InitTheme(ThemeResources& t) {
    t.bg_brush      = CreateSolidBrush(kBgColor);
    t.card_brush    = CreateSolidBrush(kCardColor);
    t.darkbtn_brush = CreateSolidBrush(kDarkBtnColor);
    t.border_pen    = CreatePen(PS_SOLID, 1, kBorderColor);

    t.font_title    = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    t.font_subtitle = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    t.font_body     = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    t.font_bold     = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    t.font_small    = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void CleanupTheme(ThemeResources& t) {
    if (t.bg_brush)      DeleteObject(t.bg_brush);
    if (t.card_brush)    DeleteObject(t.card_brush);
    if (t.darkbtn_brush) DeleteObject(t.darkbtn_brush);
    if (t.border_pen)    DeleteObject(t.border_pen);
    if (t.font_title)    DeleteObject(t.font_title);
    if (t.font_subtitle) DeleteObject(t.font_subtitle);
    if (t.font_body)     DeleteObject(t.font_body);
    if (t.font_bold)     DeleteObject(t.font_bold);
    if (t.font_small)    DeleteObject(t.font_small);
}

void DrawLaunchButton(const DRAWITEMSTRUCT& dis, HFONT font_bold) {
    bool is_down = (dis.itemState & ODS_SELECTED);
    COLORREF btn_col = is_down ? kButtonHover : kButtonGreen;
    HBRUSH hBr = CreateSolidBrush(btn_col);
    HPEN hPen = CreatePen(PS_SOLID, 1, btn_col);
    HGDIOBJ oldBr = SelectObject(dis.hDC, hBr);
    HGDIOBJ oldPen = SelectObject(dis.hDC, hPen);

    RoundRect(dis.hDC, dis.rcItem.left, dis.rcItem.top, dis.rcItem.right, dis.rcItem.bottom, 8, 8);

    SetBkMode(dis.hDC, TRANSPARENT);
    SetTextColor(dis.hDC, RGB(255, 255, 255));
    SelectObject(dis.hDC, font_bold);
    DrawTextW(dis.hDC, L"Launch Game with OmniRender", -1, const_cast<LPRECT>(&dis.rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dis.hDC, oldBr);
    SelectObject(dis.hDC, oldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

void DrawSecondaryButton(const DRAWITEMSTRUCT& dis, const wchar_t* label, HFONT font_body) {
    bool is_down = (dis.itemState & ODS_SELECTED);
    COLORREF btn_col = is_down ? RGB(45, 52, 62) : kDarkBtnColor;
    HBRUSH hBr = CreateSolidBrush(btn_col);
    HPEN hPen = CreatePen(PS_SOLID, 1, kDarkBtnBorder);
    HGDIOBJ oldBr = SelectObject(dis.hDC, hBr);
    HGDIOBJ oldPen = SelectObject(dis.hDC, hPen);

    RoundRect(dis.hDC, dis.rcItem.left, dis.rcItem.top, dis.rcItem.right, dis.rcItem.bottom, 6, 6);

    SetBkMode(dis.hDC, TRANSPARENT);
    SetTextColor(dis.hDC, kTextColor);
    SelectObject(dis.hDC, font_body);
    DrawTextW(dis.hDC, label, -1, const_cast<LPRECT>(&dis.rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dis.hDC, oldBr);
    SelectObject(dis.hDC, oldPen);
    DeleteObject(hBr);
    DeleteObject(hPen);
}

LRESULT HandleCtlColorStatic(HDC hdc, const ThemeResources& t) {
    SetBkColor(hdc, kBgColor);
    SetTextColor(hdc, kTextColor);
    return (LRESULT)t.bg_brush;
}

LRESULT HandleCtlColorEdit(HDC hdc, const ThemeResources& t) {
    SetBkColor(hdc, kCardColor);
    SetTextColor(hdc, kTextColor);
    return (LRESULT)t.card_brush;
}

LRESULT HandleCtlColorBtn(HDC hdc, const ThemeResources& t) {
    SetBkColor(hdc, kBgColor);
    SetTextColor(hdc, kTextColor);
    return (LRESULT)t.bg_brush;
}

}  // namespace omnirender::ui
