// filepath: modules/ui/control_panel_theme.h
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace omnirender::ui {

struct ThemeResources {
    HBRUSH bg_brush       = nullptr;
    HBRUSH card_brush     = nullptr;
    HBRUSH darkbtn_brush  = nullptr;
    HPEN   border_pen     = nullptr;

    HFONT  font_title     = nullptr;
    HFONT  font_subtitle  = nullptr;
    HFONT  font_body      = nullptr;
    HFONT  font_bold      = nullptr;
    HFONT  font_small     = nullptr;
};

void InitTheme(ThemeResources& theme);
void CleanupTheme(ThemeResources& theme);
void DrawLaunchButton(const DRAWITEMSTRUCT& dis, HFONT font_bold);
void DrawSecondaryButton(const DRAWITEMSTRUCT& dis, const wchar_t* label, HFONT font_body);
LRESULT HandleCtlColorStatic(HDC hdc, const ThemeResources& theme);
LRESULT HandleCtlColorEdit(HDC hdc, const ThemeResources& theme);
LRESULT HandleCtlColorBtn(HDC hdc, const ThemeResources& theme);

}  // namespace omnirender::ui
