#ifndef UI_HPP
#define UI_HPP

#include <windows.h>
#include <string>

enum log_kind {
	LOG_INFO,
	LOG_OK,
	LOG_FAIL,
};

bool ui_create(HINSTANCE inst);
void ui_run_message_loop();
void ui_log(const std::string& s, log_kind k);

HWND ui_get_hwnd();
HWND ui_get_edit();

#endif
