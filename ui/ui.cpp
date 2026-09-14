#include "ui/ui.hpp"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <deque>

#include "patch/patch.hpp"

struct log_line {
	std::string m_text;
	log_kind m_kind;
};

static const COLORREF k_c_bg        = RGB(13, 12, 18);
static const COLORREF k_c_panel     = RGB(22, 20, 30);
static const COLORREF k_c_panel_hi  = RGB(32, 28, 42);
static const COLORREF k_c_border    = RGB(60, 30, 90);
static const COLORREF k_c_accent    = RGB(155, 70, 235);
static const COLORREF k_c_accent_hi = RGB(190, 110, 255);
static const COLORREF k_c_text      = RGB(232, 228, 240);
static const COLORREF k_c_muted     = RGB(140, 135, 160);
static const COLORREF k_c_ok        = RGB(110, 220, 140);
static const COLORREF k_c_fail      = RGB(255, 90, 110);
static const COLORREF k_c_input_bg  = RGB(16, 14, 22);

static HWND g_hwnd = nullptr;
static HWND g_edit = nullptr;
static WNDPROC g_edit_orig_proc = nullptr;

static HFONT g_fnt_title = nullptr;
static HFONT g_fnt_body  = nullptr;
static HFONT g_fnt_small = nullptr;
static HFONT g_fnt_btn   = nullptr;

static std::mutex g_log_mx;
static std::deque<log_line> g_log;

static RECT g_rc_patch{};
static RECT g_rc_close{};
static RECT g_rc_min{};
static RECT g_rc_footer{};

static bool g_hover_patch  = false;
static bool g_hover_close  = false;
static bool g_hover_min    = false;
static bool g_hover_footer = false;
static bool g_press_patch  = false;

static bool g_dragging = false;
static POINT g_drag_offset{};

HWND ui_get_hwnd() { return g_hwnd; }
HWND ui_get_edit() { return g_edit; }

void ui_log(const std::string& s, log_kind k)
{
	{
		std::lock_guard<std::mutex> lk(g_log_mx);
		g_log.push_back({ s, k });

		if (g_log.size() > 128)
			g_log.pop_front();
	}

	if (g_hwnd)
		InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void fill_rect(HDC dc, RECT r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FillRect(dc, &r, b);
	DeleteObject(b);
}

static void draw_rect_border(HDC dc, RECT r, COLORREF c)
{
	HPEN p = CreatePen(PS_SOLID, 1, c);
	HGDIOBJ old_pen = SelectObject(dc, p);
	HGDIOBJ old_br  = SelectObject(dc, GetStockObject(NULL_BRUSH));

	Rectangle(dc, r.left, r.top, r.right, r.bottom);

	SelectObject(dc, old_pen);
	SelectObject(dc, old_br);
	DeleteObject(p);
}

static void draw_text_line(HDC dc, const char* s, RECT r, COLORREF c, HFONT f,
	UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
	HGDIOBJ old_f = SelectObject(dc, f);
	SetTextColor(dc, c);
	SetBkMode(dc, TRANSPARENT);
	DrawTextA(dc, s, -1, &r, fmt);
	SelectObject(dc, old_f);
}

static void draw_vgradient(HDC dc, RECT r, COLORREF top, COLORREF bot)
{
	TRIVERTEX v[2];
	v[0].x = r.left;
	v[0].y = r.top;
	v[0].Red   = (COLOR16)(GetRValue(top) << 8);
	v[0].Green = (COLOR16)(GetGValue(top) << 8);
	v[0].Blue  = (COLOR16)(GetBValue(top) << 8);
	v[0].Alpha = 0;
	v[1].x = r.right;
	v[1].y = r.bottom;
	v[1].Red   = (COLOR16)(GetRValue(bot) << 8);
	v[1].Green = (COLOR16)(GetGValue(bot) << 8);
	v[1].Blue  = (COLOR16)(GetBValue(bot) << 8);
	v[1].Alpha = 0;
	GRADIENT_RECT gr = { 0, 1 };
	GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

static void paint_window(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);

	RECT rc;
	GetClientRect(hwnd, &rc);

	HDC mem = CreateCompatibleDC(dc);
	HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
	HGDIOBJ old_bmp = SelectObject(mem, bmp);

	fill_rect(mem, rc, k_c_bg);

	RECT r_border = rc;
	draw_rect_border(mem, r_border, k_c_border);

	RECT r_title = { 0, 0, rc.right, 40 };
	draw_vgradient(mem, r_title, RGB(30, 14, 46), RGB(16, 8, 24));

	RECT r_accent = { 0, 40, rc.right, 42 };
	fill_rect(mem, r_accent, k_c_accent);

	RECT r_dot = { 14, 16, 22, 24 };
	HBRUSH bd = CreateSolidBrush(k_c_accent);
	HGDIOBJ old_b = SelectObject(mem, bd);
	HPEN pn = CreatePen(PS_SOLID, 1, k_c_accent_hi);
	HGDIOBJ old_p = SelectObject(mem, pn);
	Ellipse(mem, r_dot.left, r_dot.top, r_dot.right, r_dot.bottom);
	SelectObject(mem, old_b);
	SelectObject(mem, old_p);
	DeleteObject(bd);
	DeleteObject(pn);

	RECT r_txt = { 32, 0, rc.right - 90, 40 };
	draw_text_line(mem, "sv_pureLevel  \xB7  override", r_txt, k_c_text, g_fnt_title);

	g_rc_min   = { rc.right - 70, 8, rc.right - 42, 32 };
	g_rc_close = { rc.right - 34, 8, rc.right - 8,  32 };
	fill_rect(mem, g_rc_min,   g_hover_min   ? k_c_panel_hi : k_c_panel);
	fill_rect(mem, g_rc_close, g_hover_close ? k_c_accent   : k_c_panel);
	draw_text_line(mem, "_", g_rc_min,   k_c_text, g_fnt_btn,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	draw_text_line(mem, "X", g_rc_close, k_c_text, g_fnt_btn,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT r_main = { 12, 54, rc.right - 12, rc.bottom - 12 };
	fill_rect(mem, r_main, k_c_panel);
	draw_rect_border(mem, r_main, RGB(50, 36, 78));

	RECT r_hdr = { r_main.left + 12, r_main.top + 8, r_main.right - 12, r_main.top + 28 };
	draw_text_line(mem, "STATUS", r_hdr, k_c_accent, g_fnt_small);

	RECT r_log = { r_main.left + 12, r_main.top + 30, r_main.right - 12, r_main.top + 210 };
	fill_rect(mem, r_log, k_c_input_bg);
	draw_rect_border(mem, r_log, RGB(40, 28, 60));

	{
		std::lock_guard<std::mutex> lk(g_log_mx);
		int y = r_log.top + 8;
		int line_h = 18;
		int max_lines = (r_log.bottom - r_log.top - 16) / line_h;
		int start = (int)g_log.size() - max_lines;

		if (start < 0)
			start = 0;

		for (int i = start; i < (int)g_log.size(); ++i) {
			const auto& ln = g_log[i];
			COLORREF tag_c = k_c_muted;
			const char* tag = " * ";

			if (ln.m_kind == LOG_OK) {
				tag_c = k_c_ok;
				tag = " + ";
			}

			if (ln.m_kind == LOG_FAIL) {
				tag_c = k_c_fail;
				tag = " ! ";
			}

			RECT r_tag = { r_log.left + 8, y, r_log.left + 34, y + line_h };
			draw_text_line(mem, tag, r_tag, tag_c, g_fnt_body);

			RECT r_msg = { r_log.left + 36, y, r_log.right - 8, y + line_h };
			draw_text_line(mem, ln.m_text.c_str(), r_msg, k_c_text, g_fnt_body);

			y += line_h;
		}
	}

	RECT r_hdr2 = { r_main.left + 12, r_main.top + 222, r_main.right - 12, r_main.top + 242 };
	draw_text_line(mem, "PURE LEVEL", r_hdr2, k_c_accent, g_fnt_small);

	RECT r_input = { r_main.left + 12, r_main.top + 246, r_main.left + 180, r_main.top + 282 };
	fill_rect(mem, r_input, k_c_input_bg);
	draw_rect_border(mem, r_input, RGB(80, 40, 130));

	g_rc_patch = { r_main.left + 194, r_main.top + 246, r_main.right - 12, r_main.top + 282 };
	COLORREF top = g_hover_patch ? k_c_accent_hi : k_c_accent;
	COLORREF bot = g_hover_patch ? RGB(120, 50, 190) : RGB(100, 40, 165);

	if (g_press_patch) {
		top = RGB(100, 40, 165);
		bot = RGB(70, 24, 120);
	}

	draw_vgradient(mem, g_rc_patch, top, bot);
	draw_rect_border(mem, g_rc_patch, RGB(200, 130, 255));

	if (patch_is_working())
		draw_text_line(mem, "WORKING...", g_rc_patch, RGB(255, 255, 255),
			g_fnt_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	else
		draw_text_line(mem, "APPLY", g_rc_patch, RGB(255, 255, 255),
			g_fnt_btn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT r_foot = { r_main.left + 12, r_main.bottom - 22, r_main.right - 12, r_main.bottom - 6 };
	g_rc_footer = r_foot;
	COLORREF foot_col = g_hover_footer ? k_c_accent_hi : k_c_muted;
	draw_text_line(mem, "by Deku  \xB7  github.com/dekus", r_foot, foot_col,
		g_fnt_small, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

	BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
	SelectObject(mem, old_bmp);
	DeleteObject(bmp);
	DeleteDC(mem);
	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_CHAR) {
		if (!((w >= '0' && w <= '9') || w == '-' || w == VK_BACK || w == VK_RETURN))
			return 0;

		if (w == VK_RETURN) {
			char buf[32]{};
			GetWindowTextA(g_edit, buf, sizeof(buf));
			patch_request(atoi(buf));
			return 0;
		}
	}

	return CallWindowProcA(g_edit_orig_proc, h, m, w, l);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
	switch (msg) {
	case WM_CREATE: {
		g_edit = CreateWindowExA(0, "EDIT", "-1",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
			0, 0, 10, 10, hwnd, (HMENU)1001, GetModuleHandleA(nullptr), nullptr);
		SendMessageA(g_edit, WM_SETFONT, (WPARAM)g_fnt_body, TRUE);
		g_edit_orig_proc = (WNDPROC)SetWindowLongPtrA(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
		return 0;
	}

	case WM_SIZE: {
		RECT rc;
		GetClientRect(hwnd, &rc);
		RECT r_main = { 12, 54, rc.right - 12, rc.bottom - 12 };
		MoveWindow(g_edit, r_main.left + 18, r_main.top + 253, 160, 24, TRUE);
		return 0;
	}

	case WM_CTLCOLOREDIT: {
		HDC dc = (HDC)w;
		SetTextColor(dc, k_c_text);
		SetBkColor(dc, k_c_input_bg);
		static HBRUSH br = CreateSolidBrush(k_c_input_bg);
		return (LRESULT)br;
	}

	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		paint_window(hwnd);
		return 0;

	case WM_MOUSEMOVE: {
		POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
		bool hp = PtInRect(&g_rc_patch, p);
		bool hc = PtInRect(&g_rc_close, p);
		bool hm = PtInRect(&g_rc_min, p);
		bool hf = PtInRect(&g_rc_footer, p);

		if (hp != g_hover_patch || hc != g_hover_close
			|| hm != g_hover_min || hf != g_hover_footer) {
			g_hover_patch  = hp;
			g_hover_close  = hc;
			g_hover_min    = hm;
			g_hover_footer = hf;
			SetCursor(LoadCursorA(nullptr, hf ? IDC_HAND : IDC_ARROW));
			InvalidateRect(hwnd, nullptr, FALSE);
		}

		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&tme);

		if (g_dragging) {
			POINT scr = p;
			ClientToScreen(hwnd, &scr);
			SetWindowPos(hwnd, nullptr, scr.x - g_drag_offset.x,
				scr.y - g_drag_offset.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		}

		return 0;
	}

	case WM_MOUSELEAVE:
		g_hover_patch = g_hover_close = g_hover_min = g_hover_footer = false;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;

	case WM_LBUTTONDOWN: {
		POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };

		if (PtInRect(&g_rc_close, p)) {
			PostQuitMessage(0);
			return 0;
		}

		if (PtInRect(&g_rc_min, p)) {
			ShowWindow(hwnd, SW_MINIMIZE);
			return 0;
		}

		if (PtInRect(&g_rc_footer, p)) {
			ShellExecuteA(nullptr, "open", "https://github.com/dekus",
				nullptr, nullptr, SW_SHOWNORMAL);
			return 0;
		}

		if (PtInRect(&g_rc_patch, p)) {
			g_press_patch = true;
			SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

		if (p.y < 40) {
			g_dragging = true;
			g_drag_offset = p;
			SetCapture(hwnd);
		}

		return 0;
	}

	case WM_LBUTTONUP: {
		POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
		bool was_press = g_press_patch;
		g_press_patch = false;
		g_dragging = false;
		ReleaseCapture();
		InvalidateRect(hwnd, nullptr, FALSE);

		if (was_press && PtInRect(&g_rc_patch, p)) {
			char buf[32]{};
			GetWindowTextA(g_edit, buf, sizeof(buf));
			patch_request(atoi(buf));
		}

		return 0;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcA(hwnd, msg, w, l);
}

bool ui_create(HINSTANCE inst)
{
	g_fnt_title = CreateFontA(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
	g_fnt_body = CreateFontA(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Consolas");
	g_fnt_small = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
	g_fnt_btn = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

	WNDCLASSA wc{};
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = "pureLvlUI";

	if (!RegisterClassA(&wc))
		return false;

	int w = 520;
	int h = 400;
	int sx = GetSystemMetrics(SM_CXSCREEN);
	int sy = GetSystemMetrics(SM_CYSCREEN);

	g_hwnd = CreateWindowExA(WS_EX_TOPMOST, "pureLvlUI",
		"sv_pureLevel override",
		WS_POPUP | WS_VISIBLE,
		(sx - w) / 2, (sy - h) / 2, w, h,
		nullptr, nullptr, inst, nullptr);

	if (!g_hwnd)
		return false;

	SetClassLongA(g_hwnd, GCL_STYLE, GetClassLongA(g_hwnd, GCL_STYLE) | CS_DROPSHADOW);
	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);
	return true;
}

void ui_run_message_loop()
{
	MSG m;

	while (GetMessageA(&m, nullptr, 0, 0)) {
		TranslateMessage(&m);
		DispatchMessageA(&m);
	}
}
