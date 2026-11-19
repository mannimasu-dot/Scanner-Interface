#include "ScannerUi.h"
#include "ScannerTwain.h"
#include "ScannerWia.h"
#include "ScannerServices.h"
#include "Resource.h"
#include <commdlg.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#define SECURITY_WIN32
#include <security.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <future>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

namespace scanner {
static HINSTANCE instance;
static int UiPx(HWND window, int value) {
	using DpiFunction = UINT(WINAPI*)(HWND); static auto dpi = reinterpret_cast<DpiFunction>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	return MulDiv(value, dpi ? dpi(window) : 96, 96);
}
static HBRUSH SidebarBrush() { static HBRUSH brush = CreateSolidBrush(RGB(235, 239, 249)); return brush; }
static void ToolbarBackground(HDC dc, RECT rect) {
	for (int y = rect.top; y < rect.bottom; ++y) { int shade = 255 - 22 * (y - rect.top) / std::max(1L, rect.bottom - rect.top); HBRUSH brush = CreateSolidBrush(RGB(shade, shade, std::min(255, shade + 5))); RECT row{rect.left, y, rect.right, y + 1}; FillRect(dc, &row, brush); DeleteObject(brush); }
}
static void ToolbarButtonBackground(HDC dc, RECT rect, bool hovered, bool pressed) {
	ToolbarBackground(dc, rect);
	if (hovered) { HBRUSH fill = CreateSolidBrush(RGB(235, 239, 249)), border = CreateSolidBrush(RGB(205, 218, 237)); FillRect(dc, &rect, fill); DeleteObject(fill); if (!pressed) FrameRect(dc, &rect, border); DeleteObject(border); }
}
static LRESULT CALLBACK ToolbarHoverProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	auto hovered = reinterpret_cast<HWND*>(data);
	if (message == WM_MOUSEMOVE) {
		if (IsWindowEnabled(button) && *hovered != button) { HWND previous = *hovered; *hovered = button; if (previous) InvalidateRect(previous, nullptr, FALSE); InvalidateRect(button, nullptr, FALSE); }
		TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, button, 0}; TrackMouseEvent(&track);
	}
	if ((message == WM_MOUSELEAVE || (message == WM_ENABLE && !wParam) || message == WM_NCDESTROY) && *hovered == button) { *hovered = nullptr; InvalidateRect(button, nullptr, FALSE); }
	if (message == WM_NCDESTROY) RemoveWindowSubclass(button, ToolbarHoverProc, 2);
	return DefSubclassProc(button, message, wParam, lParam);
}
static std::wstring WideError(const char* text) {
	int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0); if (!n) return L"Unbekannter Fehler.";
	std::wstring result(n, L' '); MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), n); result.pop_back(); return result;
}
static void Error(HWND owner, const std::exception& e) { MessageBoxW(owner, WideError(e.what()).c_str(), L"Scanner – Vorgang nicht abgeschlossen", MB_OK | MB_ICONERROR); }
static std::filesystem::path HelpFilePath() {
	std::vector<wchar_t> path(32768); DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	if (!length || length >= path.size()) throw std::runtime_error("Der Programmpfad konnte nicht ermittelt werden.");
	return std::filesystem::path(std::wstring(path.data(), length)).parent_path() / L"Hilfe.html";
}
static INT_PTR CALLBACK AboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_INITDIALOG) {
		SetDlgItemTextW(dialog, 1001, L"Herzlichen Dank an Grewe Computertechnik für die ursprüngliche Idee des Scanner-Interface.\r\n\r\nNach Grewes Ausscheiden wurde die Anwendung von MASU mit Unterstützung von GitHub Copilot Chat neu umgesetzt.\r\n\r\nDie Anwendung ist frei verfügbar; der Quellcode ist auf GitHub als Open Source bereitgestellt.");
		return TRUE;
	}
	if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lParam) == GetDlgItem(dialog, 1001)) { SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0)); SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255)); return reinterpret_cast<INT_PTR>(GetStockObject(WHITE_BRUSH)); }
	if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) { EndDialog(dialog, LOWORD(wParam)); return TRUE; }
	if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
	return FALSE;
}
static HWND Control(HWND owner, const wchar_t* type, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id) {
	HWND c = CreateWindowExW(0, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, w, h, owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
	SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return c;
}
struct HelpSection { const wchar_t* heading; const wchar_t* text; };
struct HelpTopic { std::wstring heading, text; };
static std::vector<HelpTopic> ReadHelpTopics(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary); if (!input) throw std::runtime_error("Die Hilfedatei Hilfe.html konnte nicht gelesen werden.");
	std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>()); if (input.bad() || bytes.empty() || bytes.size() > 1048576) throw std::runtime_error("Die Hilfedatei ist leer, zu gross oder konnte nicht vollstaendig gelesen werden.");
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0); if (!length) throw std::runtime_error("Die Hilfedatei enthaelt ungueltiges UTF-8.");
	std::wstring html(length, L' '); if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), html.data(), length)) throw std::runtime_error("Die Hilfedatei konnte nicht dekodiert werden.");
	std::vector<HelpTopic> topics; size_t pos = 0;
	while ((pos = html.find(L"<section ", pos)) != std::wstring::npos) {
		auto start = html.find(L'>', pos), end = html.find(L"</section>", pos); if (start == std::wstring::npos || end == std::wstring::npos || start >= end) throw std::runtime_error("Ein Hilfeabschnitt ist unvollstaendig.");
		HelpTopic topic; size_t cursor = start + 1;
		while (cursor < end) {
			while (cursor < end && iswspace(html[cursor])) ++cursor; if (cursor == end) break;
			const wchar_t* close = nullptr; size_t tagLength = 0; bool heading = false;
			if (html.compare(cursor, 4, L"<h2>") == 0) { close = L"</h2>"; tagLength = 4; heading = true; }
			else if (html.compare(cursor, 4, L"<h3>") == 0) { close = L"</h3>"; tagLength = 4; }
			else if (html.compare(cursor, 3, L"<p>") == 0) { close = L"</p>"; tagLength = 3; }
			else throw std::runtime_error("Die Hilfedatei enthaelt ein nicht unterstuetztes Element.");
			auto finish = html.find(close, cursor + tagLength); if (finish == std::wstring::npos || finish > end) throw std::runtime_error("Ein Hilfetext ist unvollstaendig.");
			auto text = html.substr(cursor + tagLength, finish - cursor - tagLength); if (text.find_first_of(L"<&") != std::wstring::npos) throw std::runtime_error("Die Hilfedatei enthaelt nicht unterstuetzte Formatierung.");
			if (heading) { if (!topic.heading.empty()) throw std::runtime_error("Ein Hilfeabschnitt hat mehrere Titel."); topic.heading = std::move(text); }
			else { if (!topic.text.empty()) topic.text += L"\r\n\r\n"; topic.text += text; }
			cursor = finish + wcslen(close);
		}
		if (topic.heading.empty() || topic.text.empty()) throw std::runtime_error("Ein Hilfeabschnitt hat keinen Titel oder Text."); topics.push_back(std::move(topic)); pos = end + 10;
	}
	if (topics.empty()) throw std::runtime_error("Die Hilfedatei enthaelt keine Hilfeabschnitte."); return topics;
}
struct HelpState { const wchar_t* title; std::vector<HelpSection> sections; bool done = false; HFONT font = nullptr, headingFont = nullptr; int wheelDelta = 0; bool searchable = false; };
static void LayoutHelp(HWND window) {
	auto state = reinterpret_cast<HelpState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	RECT client{}; GetClientRect(window, &client); int margin = UiPx(window, 14), buttonWidth = UiPx(window, 90), buttonHeight = UiPx(window, 28);
	MoveWindow(GetDlgItem(window, 100), margin, margin, std::max(0L, client.right - 2 * margin), UiPx(window, 28), TRUE);
	int top = state && state->searchable ? 112 : 48;
	if (state && state->searchable) { MoveWindow(GetDlgItem(window, 102), margin, UiPx(window, 48), std::max(0L, client.right - 2 * margin), UiPx(window, 180), TRUE); MoveWindow(GetDlgItem(window, 103), margin, UiPx(window, 80), std::max(0L, client.right - 2 * margin), UiPx(window, 24), TRUE); }
	MoveWindow(GetDlgItem(window, 101), margin, UiPx(window, top), std::max(0L, client.right - 2 * margin), std::max(0L, client.bottom - UiPx(window, top + 56)), TRUE);
	MoveWindow(GetDlgItem(window, IDOK), std::max(margin, static_cast<int>(client.right) - margin - buttonWidth), std::max(margin, static_cast<int>(client.bottom) - margin - buttonHeight), buttonWidth, buttonHeight, TRUE);
}
static void RenderHelp(HWND window, const HelpState& state) {
	HWND content = GetDlgItem(window, 101); std::wstring query;
	if (state.searchable) { int n = GetWindowTextLengthW(GetDlgItem(window, 103)); query.resize(n + 1); GetDlgItemTextW(window, 103, query.data(), n + 1); query.resize(n); std::transform(query.begin(), query.end(), query.begin(), towlower); }
	int selected = state.searchable ? static_cast<int>(SendDlgItemMessageW(window, 102, CB_GETCURSEL, 0, 0)) : 0;
	SendMessageW(content, EM_SETREADONLY, FALSE, 0); SetWindowTextW(content, L"");
	for (size_t i = 0; i < state.sections.size(); ++i) {
		const auto& section = state.sections[i]; if (selected > 0 && static_cast<size_t>(selected - 1) != i) continue;
		std::wstring text = std::wstring(section.heading) + L" " + section.text; std::transform(text.begin(), text.end(), text.begin(), towlower); if (!query.empty() && text.find(query) == std::wstring::npos) continue;
		CHARFORMATW format{sizeof(format)}; format.dwMask = CFM_BOLD; format.dwEffects = CFE_BOLD;
		SendMessageW(content, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format)); SendMessageW(content, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(section.heading));
		format.dwEffects = 0; SendMessageW(content, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format)); std::wstring body = L"\r\n"; body += section.text; body += L"\r\n\r\n"; SendMessageW(content, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(body.c_str()));
	}
	SendMessageW(content, EM_SETREADONLY, TRUE, 0); SendMessageW(content, EM_SETSEL, 0, 0); SendMessageW(content, EM_SCROLLCARET, 0, 0);
}
static LRESULT CALLBACK HelpProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<HelpState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<HelpState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		NONCLIENTMETRICSW metrics{sizeof(metrics)}; if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return -1; metrics.lfMessageFont.lfHeight = -UiPx(window, 14);
		state->font = CreateFontIndirectW(&metrics.lfMessageFont); metrics.lfMessageFont.lfWeight = FW_BOLD; metrics.lfMessageFont.lfHeight = -UiPx(window, 18); state->headingFont = CreateFontIndirectW(&metrics.lfMessageFont);
		HWND heading = Control(window, L"STATIC", state->title, 0, 0, 0, 0, 0, 100);
		HWND content = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(101), instance, nullptr);
		HWND close = Control(window, L"BUTTON", L"Schließen", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, IDOK);
		if (!heading || !content || !close || !state->font || !state->headingFont) return -1;
		SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(state->headingFont), TRUE); SendMessageW(content, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE); SendMessageW(close, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
		if (state->searchable) {
			HWND topics = Control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, 102);
			HWND search = Control(window, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, 103);
			if (!topics || !search) return -1; SendMessageW(topics, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE); SendMessageW(search, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
			SendMessageW(topics, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alle Themen")); for (const auto& section : state->sections) SendMessageW(topics, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(section.heading)); SendMessageW(topics, CB_SETCURSEL, 0, 0); SendMessageW(search, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Hilfe durchsuchen"));
		}
		RenderHelp(window, *state); EnableWindow(content, FALSE); LayoutHelp(window); return 0;
	}
	if (message == WM_MOUSEWHEEL) {
		state->wheelDelta += GET_WHEEL_DELTA_WPARAM(wParam); int steps = state->wheelDelta / WHEEL_DELTA; state->wheelDelta %= WHEEL_DELTA;
		UINT lines = 0; if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) { MessageBoxW(window, L"Die Mausradeinstellung konnte nicht gelesen werden.", L"Hilfe", MB_OK | MB_ICONERROR); return 0; }
		HWND content = GetDlgItem(window, 101);
		if (lines == WHEEL_PAGESCROLL) { for (int i = 0; i < std::abs(steps); ++i) SendMessageW(content, EM_SCROLL, steps > 0 ? SB_PAGEUP : SB_PAGEDOWN, 0); }
		else SendMessageW(content, EM_LINESCROLL, 0, -static_cast<LPARAM>(steps) * lines);
		return 0;
	}
	if (message == WM_COMMAND && state->searchable && ((LOWORD(wParam) == 102 && HIWORD(wParam) == CBN_SELCHANGE) || (LOWORD(wParam) == 103 && HIWORD(wParam) == EN_CHANGE))) { RenderHelp(window, *state); return 0; }
	if (message == WM_SIZE) { LayoutHelp(window); return 0; }
	if (message == WM_GETMINMAXINFO) { auto limits = reinterpret_cast<MINMAXINFO*>(lParam); limits->ptMinTrackSize = {UiPx(window, 320), UiPx(window, 240)}; return 0; }
	if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) { DestroyWindow(window); return 0; }
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) DeleteObject(state->font); if (state->headingFont) DeleteObject(state->headingFont); state->font = state->headingFont = nullptr; state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static void ShowDialogHelp(HWND owner, const wchar_t* title, std::vector<HelpSection> sections, bool searchable = false) {
	static HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
	if (!richEdit) { MessageBoxW(owner, L"Die Hilfeanzeige konnte nicht geladen werden.", L"Hilfe", MB_OK | MB_ICONERROR); return; }
	WNDCLASSW cls{}; cls.lpfnWndProc = HelpProc; cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); cls.lpszClassName = L"ScannerDialogHelp";
	if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { MessageBoxW(owner, L"Die Hilfeanzeige konnte nicht registriert werden.", L"Hilfe", MB_OK | MB_ICONERROR); return; }
	HelpState state{title, std::move(sections)}; state.searchable = searchable; RECT parent{}; MONITORINFO monitor{sizeof(monitor)};
	if (!GetWindowRect(owner, &parent) || !GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) { MessageBoxW(owner, L"Die Position der Hilfeanzeige konnte nicht bestimmt werden.", L"Hilfe", MB_OK | MB_ICONERROR); return; }
	RECT bounds{0, 0, UiPx(owner, 610), UiPx(owner, 460)}; AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_DLGMODALFRAME);
	int width = std::min(static_cast<int>(bounds.right - bounds.left), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)); int height = std::min(static_cast<int>(bounds.bottom - bounds.top), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
	int x = std::clamp(static_cast<int>(parent.left + UiPx(owner, 30)), static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width); int y = std::clamp(static_cast<int>(parent.top + UiPx(owner, 30)), static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - height);
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls.lpszClassName, title, WS_OVERLAPPEDWINDOW, x, y, width, height, owner, nullptr, instance, &state);
	if (!window) { MessageBoxW(owner, L"Der Hilfedialog konnte nicht erstellt werden.", L"Hilfe", MB_OK | MB_ICONERROR); return; }
	EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); SetFocus(GetDlgItem(window, IDOK)); MSG msg{};
	while (!state.done) { int result = GetMessageW(&msg, nullptr, 0, 0); if (result <= 0) { DestroyWindow(window); if (!result) PostQuitMessage(static_cast<int>(msg.wParam)); else MessageBoxW(owner, L"Die Hilfeanzeige wurde wegen eines Nachrichtenfehlers beendet.", L"Hilfe", MB_OK | MB_ICONERROR); break; } if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
	EnableWindow(owner, TRUE); SetForegroundWindow(owner);
}
struct Field { std::wstring label, value; std::vector<std::wstring> choices; HWND control = nullptr; };
struct FormState { std::vector<Field>* fields = nullptr; bool done = false, accepted = false; const std::vector<Field>* defaults = nullptr; };
static LRESULT CALLBACK FormProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<FormState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<FormState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (message == WM_CREATE) {
		const size_t rows = (state->fields->size() + 1) / 2;
		for (size_t n = 0; n < state->fields->size(); ++n) {
			auto& field = (*state->fields)[n]; int column = n >= rows ? 1 : 0; int row = static_cast<int>(n % rows); int x = 14 + column * 410, y = 16 + row * 59;
			Control(window, L"STATIC", field.label, 0, x, y, 390, 19, 0);
			if (field.choices.empty()) field.control = Control(window, L"EDIT", field.value, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, y + 20, 390, 25, 100 + static_cast<int>(n));
			else {
				field.control = Control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, y + 20, 390, 300, 100 + static_cast<int>(n));
				int selected = 0; for (size_t j = 0; j < field.choices.size(); ++j) { SendMessageW(field.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(field.choices[j].c_str())); if (field.choices[j] == field.value) selected = static_cast<int>(j); } SendMessageW(field.control, CB_SETCURSEL, selected, 0);
			}
		}
		int y = 20 + static_cast<int>(rows) * 59; Control(window, L"BUTTON", L"Übernehmen", WS_TABSTOP | BS_DEFPUSHBUTTON, 580, y, 110, 30, IDOK); Control(window, L"BUTTON", L"Abbrechen", WS_TABSTOP, 704, y, 110, 30, IDCANCEL);
		if (state->defaults) Control(window, L"BUTTON", L"Zurücksetzen", WS_TABSTOP, 14, y, 140, 30, 1001);
		if (!state->fields->empty()) SetFocus((*state->fields)[0].control); return 0;
	}
	if (message == WM_COMMAND && LOWORD(wParam) == 1001 && state->defaults) {
		for (size_t n = 0; n < state->fields->size(); ++n) { auto& f = (*state->fields)[n]; const auto& value = (*state->defaults)[n].value; if (f.choices.empty()) SetWindowTextW(f.control, value.c_str()); else { auto selected = SendMessageW(f.control, CB_FINDSTRINGEXACT, -1, reinterpret_cast<LPARAM>(value.c_str())); SendMessageW(f.control, CB_SETCURSEL, selected, 0); } } return 0;
	}
	if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
		if (LOWORD(wParam) == IDOK) { for (auto& f : *state->fields) { int n = GetWindowTextLengthW(f.control); std::wstring value(size_t(n) + 1, L' '); GetWindowTextW(f.control, value.data(), n + 1); value.resize(n); f.value = std::move(value); } state->accepted = true; }
		DestroyWindow(window); return 0;
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static bool Form(HWND owner, const wchar_t* title, std::vector<Field>& fields, const std::vector<Field>* defaults = nullptr) {
	FormState state{&fields, false, false, defaults}; RECT parent{}; GetWindowRect(owner, &parent); int rows = static_cast<int>((fields.size() + 1) / 2), height = 100 + rows * 59;
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"ScannerForm", title, WS_CAPTION | WS_SYSMENU | WS_POPUP, std::max(0L, parent.left + 30), std::max(0L, parent.top + 30), 845, height, owner, nullptr, instance, &state);
	if (!window) throw std::runtime_error("Dialog konnte nicht erstellt werden."); EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); MSG msg{};
	while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	EnableWindow(owner, TRUE); SetForegroundWindow(owner); return state.accepted;
}
static double Decimal(const std::wstring& text, double minimum, double maximum) {
	std::wstring normalized = text; std::replace(normalized.begin(), normalized.end(), L',', L'.'); size_t used = 0; double value = std::stod(normalized, &used);
	if (used != normalized.size() || !std::isfinite(value) || value < minimum || value > maximum) throw std::runtime_error("Eingabewert ausserhalb des zulaessigen Bereichs."); return value;
}
static int Integer(const std::wstring& text, int minimum, int maximum) { double d = Decimal(text, minimum, maximum); if (std::floor(d) != d) throw std::runtime_error("Ganzzahl erforderlich."); return static_cast<int>(d); }
static std::vector<std::wstring> YesNo{L"Nein", L"Ja"};
static std::wstring Yn(bool b) { return b ? L"Ja" : L"Nein"; }
static bool IsYes(const Field& f) { return f.value == L"Ja"; }
static std::wstring GetIni(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key, const std::wstring& fallback) { for (DWORD size = 4096; size <= 1048576; size *= 2) { std::vector<wchar_t> value(size); auto length = GetPrivateProfileStringW(section, key, fallback.c_str(), value.data(), size, file.c_str()); if (length < size - 1) return std::wstring(value.data(), length); } throw std::runtime_error("Gespeicherte Einstellung ist zu lang."); }
static void SetIni(const std::filesystem::path& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) { if (!std::filesystem::exists(file)) { std::ofstream initial(file, std::ios::binary); const wchar_t bom = 0xfeff; initial.write(reinterpret_cast<const char*>(&bom), sizeof(bom)); initial.close(); if (!initial) throw std::runtime_error("Einstellungen konnten nicht angelegt werden."); } if (!WritePrivateProfileStringW(section, key, value.c_str(), file.c_str())) throw std::runtime_error("Einstellungen konnten nicht gespeichert werden."); }
static std::wstring EncodeSettingText(const std::wstring& text) { std::wostringstream out; out << L"u:" << std::hex << std::setfill(L'0'); for (auto c : text) out << std::setw(4) << static_cast<unsigned>(static_cast<uint16_t>(c)); return out.str(); }
static std::wstring DecodeSettingText(const std::wstring& text) { if (!text.starts_with(L"u:") || (text.size() - 2) % 4) throw std::runtime_error("Ungueltige gespeicherte Metadaten."); std::wstring out; for (size_t i = 2; i < text.size(); i += 4) { auto hex = text.substr(i, 4); if (hex.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) throw std::runtime_error("Ungueltige gespeicherte Metadaten."); out += static_cast<wchar_t>(std::stoul(hex, nullptr, 16)); } return out; }
static std::wstring DeviceKey(const std::wstring& name) { uint64_t hash = 14695981039346656037ull; for (auto c : name) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ull; } return L"Device-" + std::to_wstring(hash); }
static RECT ImageRect(const Image& image, RECT bounds, double zoom = 1, POINT pan = {}) {
	double scale = std::min(double(bounds.right - bounds.left) / image.width, double(bounds.bottom - bounds.top) / image.height) * zoom;
	int width = static_cast<int>(image.width * scale), height = static_cast<int>(image.height * scale); int x = bounds.left + (bounds.right - bounds.left - width) / 2 + pan.x, y = bounds.top + (bounds.bottom - bounds.top - height) / 2 + pan.y;
	return RECT{x, y, x + width, y + height};
}
static void DrawImage(HDC dc, const Image& image, RECT r) {
	PixelReadScope release{image.pixels};
	if (r.right <= r.left || r.bottom <= r.top) return; BITMAPINFO b{}; b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); b.bmiHeader.biWidth = image.width; b.bmiHeader.biHeight = -static_cast<LONG>(image.height); b.bmiHeader.biPlanes = 1; b.bmiHeader.biBitCount = 32; b.bmiHeader.biCompression = BI_RGB;
	SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr); StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, image.width, image.height, image.pixels.data(), &b, DIB_RGB_COLORS, SRCCOPY);
}
enum CropCommand { CropCancel = 3001, CropAccept, CropCut, CropCopy, CropFree, CropSelectAll, CropFit, CropWidth, CropOriginal, CropZoomIn, CropZoomOut, CropZoomCombo, CropHelp };
static HBITMAP ToolbarBitmap(HWND window, const wchar_t* name, int index);
static HBITMAP DisabledToolbarBitmap(HBITMAP source);
struct CropState {
	Image image; RECT selection{}, display{}; POINT start{}, panStart{}; bool drawing = false, panning = false, done = false, accepted = false, freePreview = false, changed = false;
	double zoomPercent = 100; int wheelDelta = 0, offsetX = 0, offsetY = 0, panOriginX = 0, panOriginY = 0; HMENU menu = nullptr; HBITMAP icons[6]{}, disabledIcons[6]{}; HWND hoveredToolbarButton = nullptr;
};
static RECT CropWorkArea(HWND window) { RECT area{}; GetClientRect(window, &area); area.top = UiPx(window, 36); area.bottom -= UiPx(window, 20); area.left += UiPx(window, 8); area.right -= UiPx(window, 8); return area; }
static double CropFitPercent(const CropState& state, RECT area, bool widthOnly = false) {
	double width = std::max<LONG>(1, area.right - area.left), height = std::max<LONG>(1, area.bottom - area.top);
	return 100. * (widthOnly ? width / state.image.width : std::min(width / state.image.width, height / state.image.height));
}
static void CropUpdateActions(HWND window, CropState& state) {
	bool hasSelection = state.selection.right > state.selection.left && state.selection.bottom > state.selection.top;
	bool menuChanged = false;
	HMENU actionMenu = state.menu ? GetSubMenu(state.menu, 0) : nullptr;
	HMENU editMenu = state.menu ? GetSubMenu(state.menu, 1) : nullptr;
	auto enableMenuItem = [&](HMENU menu, int command, bool enabled) {
		if (!menu) return;
		UINT current = GetMenuState(menu, command, MF_BYCOMMAND);
		if (current == static_cast<UINT>(-1) || ((current & (MF_DISABLED | MF_GRAYED)) == 0) == enabled) return;
		EnableMenuItem(menu, command, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
		menuChanged = true;
	};
	enableMenuItem(editMenu, CropCut, hasSelection);
	enableMenuItem(editMenu, CropCopy, hasSelection);
	enableMenuItem(editMenu, CropFree, hasSelection);
	enableMenuItem(actionMenu, CropAccept, state.freePreview);
	if (menuChanged) DrawMenuBar(window);
	if (HWND cut = GetDlgItem(window, CropCut)) EnableWindow(cut, hasSelection);
	if (HWND copy = GetDlgItem(window, CropCopy)) EnableWindow(copy, hasSelection);
	if (HWND free = GetDlgItem(window, CropFree)) EnableWindow(free, hasSelection);
	if (HWND accept = GetDlgItem(window, CropAccept)) EnableWindow(accept, state.freePreview);
}
static void CropLayout(HWND window, CropState& state) {
	RECT client{}; GetClientRect(window, &client); auto px = [&](int v) { return UiPx(window, v); };
	const int top = px(2), height = px(32); int x = px(5);
	const int ids[]{CropCancel, CropAccept, CropCut, CropFree, CropZoomOut, CropZoomCombo, CropZoomIn}; const int widths[]{100, 105, 92, 92, 32, 78, 32};
	for (size_t i = 0; i < std::size(ids); ++i) { if (HWND control = GetDlgItem(window, ids[i])) { MoveWindow(control, x, top, px(widths[i]), height, TRUE); x += px(widths[i] + (i == 1 || i == 3 ? 8 : 0)); } }
	if (HWND separator = GetDlgItem(window, 411)) MoveWindow(separator, px(220), top + px(3), px(2), height - px(6), TRUE);
	if (HWND separator = GetDlgItem(window, 412)) MoveWindow(separator, px(412), top + px(3), px(2), height - px(6), TRUE);
	if (HWND status = GetDlgItem(window, 410)) MoveWindow(status, 0, std::max(0L, client.bottom - px(20)), client.right, px(20), TRUE);
	RECT area = CropWorkArea(window); double scale = state.zoomPercent / 100.; int width = static_cast<int>(std::min<double>(state.image.width * scale, LONG_MAX / 2)), heightPixels = static_cast<int>(std::min<double>(state.image.height * scale, LONG_MAX / 2));
	int areaWidth = static_cast<int>(area.right - area.left), areaHeight = static_cast<int>(area.bottom - area.top); state.offsetX = std::clamp(state.offsetX, 0, std::max(0, width - areaWidth)); state.offsetY = std::clamp(state.offsetY, 0, std::max(0, heightPixels - areaHeight));
	int left = area.left + (width > areaWidth ? -state.offsetX : (areaWidth - width) / 2), topImage = area.top + (heightPixels > areaHeight ? -state.offsetY : (areaHeight - heightPixels) / 2);
	state.display = {left, topImage, left + width, topImage + heightPixels};
	if (HWND status = GetDlgItem(window, 410)) {
		auto mm = [](uint32_t pixels, double dpi) { return pixels * 25.4 / dpi; };
		std::wstring color = state.image.kind == PixelKind::Mono1 ? L"2 Farben" : state.image.kind == PixelKind::Gray8 ? L"256 Farben" : L"max. 16777216 Farben";
		std::wstring text = L"Seite 1 - " + std::to_wstring(state.image.width) + L" x " + std::to_wstring(state.image.height) + L" Pixel - " + std::to_wstring(static_cast<int>(std::lround(mm(state.image.width, state.image.dpiX)))) + L" x " + std::to_wstring(static_cast<int>(std::lround(mm(state.image.height, state.image.dpiY)))) + L" mm - " + std::to_wstring(static_cast<int>(std::lround(state.image.dpiX))) + L" dpi - " + color;
		SetWindowTextW(status, text.c_str());
	}
	CropUpdateActions(window, state);
}
static RECT CropPixelRect(const CropState& state) {
	if (state.display.right <= state.display.left || state.display.bottom <= state.display.top) throw std::runtime_error("Bildbereich ist nicht sichtbar.");
	RECT selected = state.selection;
	selected.left = std::clamp<LONG>(selected.left, state.display.left, state.display.right); selected.right = std::clamp<LONG>(selected.right, state.display.left, state.display.right);
	selected.top = std::clamp<LONG>(selected.top, state.display.top, state.display.bottom); selected.bottom = std::clamp<LONG>(selected.bottom, state.display.top, state.display.bottom);
	RECT pixels{static_cast<LONG>(int64_t(selected.left - state.display.left) * state.image.width / (state.display.right - state.display.left)), static_cast<LONG>(int64_t(selected.top - state.display.top) * state.image.height / (state.display.bottom - state.display.top)), static_cast<LONG>(int64_t(selected.right - state.display.left) * state.image.width / (state.display.right - state.display.left)), static_cast<LONG>(int64_t(selected.bottom - state.display.top) * state.image.height / (state.display.bottom - state.display.top))};
	if (pixels.right <= pixels.left || pixels.bottom <= pixels.top) throw std::runtime_error("Zuerst ein Rechteck im Bild markieren."); return pixels;
}
static HMENU CropMenu() {
	HMENU menu = CreateMenu(), action = CreatePopupMenu(), edit = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
	AppendMenuW(action, MF_STRING, CropCancel, L"Abbrechen\tEsc"); AppendMenuW(action, MF_STRING | MF_GRAYED, CropAccept, L"Übernehmen\tEnter"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(action), L"&Aktion");
	AppendMenuW(edit, MF_STRING | MF_GRAYED, CropCut, L"Ausschneiden\tEntf"); AppendMenuW(edit, MF_STRING | MF_GRAYED, CropCopy, L"Kopieren\tStrg+C"); AppendMenuW(edit, MF_STRING | MF_GRAYED, CropFree, L"Freistellen\tStrg+F"); AppendMenuW(edit, MF_SEPARATOR, 0, nullptr); AppendMenuW(edit, MF_STRING, CropSelectAll, L"Alles auswählen\tStrg+A"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Bearbeiten");
	AppendMenuW(view, MF_STRING, CropFit, L"Ganze Seite\tStrg+1"); AppendMenuW(view, MF_STRING, CropWidth, L"Optimale Breite\tStrg+B"); AppendMenuW(view, MF_STRING, CropOriginal, L"Originalgröße\tStrg+O"); AppendMenuW(view, MF_SEPARATOR, 0, nullptr); AppendMenuW(view, MF_STRING, CropZoomIn, L"Vergrößern\tStrg++"); AppendMenuW(view, MF_STRING, CropZoomOut, L"Verkleinern\tStrg+-"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Ansicht");
	AppendMenuW(help, MF_STRING, CropHelp, L"Hilfe"); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"?"); return menu;
}
static void CropRemapSelection(HWND window, CropState& state) {
	RECT pixels{}; bool hasSelection = state.selection.right > state.selection.left && state.selection.bottom > state.selection.top;
	if (hasSelection) pixels = CropPixelRect(state);
	CropLayout(window, state);
	if (hasSelection) state.selection = {state.display.left + static_cast<LONG>(int64_t(pixels.left) * (state.display.right - state.display.left) / state.image.width), state.display.top + static_cast<LONG>(int64_t(pixels.top) * (state.display.bottom - state.display.top) / state.image.height), state.display.left + static_cast<LONG>(int64_t(pixels.right) * (state.display.right - state.display.left) / state.image.width), state.display.top + static_cast<LONG>(int64_t(pixels.bottom) * (state.display.bottom - state.display.top) / state.image.height)};
	CropLayout(window, state); InvalidateRect(window, nullptr, FALSE);
}
static void CropSetZoom(HWND window, CropState& state, double percent) {
	state.zoomPercent = std::clamp(percent, 5., 800.); state.offsetX = state.offsetY = 0; wchar_t value[32]{}; swprintf_s(value, L"%.0f%%", state.zoomPercent); SetDlgItemTextW(window, CropZoomCombo, value); CropRemapSelection(window, state);
}
static void CropDrawButton(const DRAWITEMSTRUCT& draw, const CropState& state) {
	int saved = SaveDC(draw.hDC); bool disabled = (draw.itemState & ODS_DISABLED) != 0, pressed = (draw.itemState & ODS_SELECTED) != 0;
	ToolbarButtonBackground(draw.hDC, draw.rcItem, !disabled && state.hoveredToolbarButton == draw.hwndItem, pressed);
	if (pressed) FrameRect(draw.hDC, &draw.rcItem, GetSysColorBrush(COLOR_3DSHADOW));
	HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0)); if (font) SelectObject(draw.hDC, font);
	SetBkMode(draw.hDC, TRANSPARENT); SetTextColor(draw.hDC, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(25, 25, 25));
	const int id = static_cast<int>(draw.CtlID), px = 28; int index = id == CropCancel ? 0 : id == CropAccept ? 1 : id == CropCut ? 2 : id == CropFree ? 3 : id == CropZoomOut ? 4 : id == CropZoomIn ? 5 : -1;
	int x = draw.rcItem.left + UiPx(draw.hwndItem, id == CropCancel || id == CropAccept ? 5 : 2), y = draw.rcItem.top + (draw.rcItem.bottom - draw.rcItem.top - UiPx(draw.hwndItem, px)) / 2;
	if (index >= 0 && state.icons[index]) {
		HBITMAP image = disabled && state.disabledIcons[index] ? state.disabledIcons[index] : state.icons[index];
		BITMAP bitmap{}; GetObjectW(image, sizeof(bitmap), &bitmap); HDC source = CreateCompatibleDC(draw.hDC); HGDIOBJ old = SelectObject(source, image);
		if (bitmap.bmBitsPixel == 32) { BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(draw.hDC, x, y, UiPx(draw.hwndItem, px), UiPx(draw.hwndItem, px), source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend); }
		else DrawStateW(draw.hDC, nullptr, nullptr, reinterpret_cast<LPARAM>(image), 0, x, y, UiPx(draw.hwndItem, px), UiPx(draw.hwndItem, px), DST_BITMAP | (disabled ? DSS_DISABLED : DSS_NORMAL));
		SelectObject(source, old); DeleteDC(source);
	}
	const wchar_t* label = id == CropCancel ? L"Abbrechen" : id == CropAccept ? L"Übernehmen" : id == CropCut ? L"Ausschneiden" : id == CropFree ? L"Freistellen" : L"";
	if (*label) { RECT text = draw.rcItem; text.left = x + UiPx(draw.hwndItem, px + 4); DrawTextW(draw.hDC, label, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT); }
	if (draw.itemState & ODS_FOCUS) { RECT focus = draw.rcItem; InflateRect(&focus, -3, -3); DrawFocusRect(draw.hDC, &focus); }
	RestoreDC(draw.hDC, saved);
}
static LRESULT CALLBACK CropProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto s = reinterpret_cast<CropState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { s = static_cast<CropState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s)); }
	if (!s) return DefWindowProcW(window, message, wParam, lParam);
	auto act = [&](int command) {
		if (command == CropCancel) { DestroyWindow(window); return; }
		if (command == CropAccept) { if (!s->freePreview) return; s->image = Crop(s->image, CropPixelRect(*s)); s->accepted = true; DestroyWindow(window); return; }
		if (command == CropSelectAll) { s->selection = s->display; s->freePreview = false; }
		else if (command == CropCut || command == CropCopy || command == CropFree) {
			RECT pixels = CropPixelRect(*s);
			if (command == CropCut) { DeleteRectangle(s->image, pixels); s->changed = true; s->freePreview = false; s->selection = {}; }
			if (command == CropCopy) { Image copy = Crop(s->image, pixels); if (!CopyWindowsClipboard(window, copy)) throw std::runtime_error("Windows-Zwischenablage konnte nicht aktualisiert werden."); }
			if (command == CropFree) s->freePreview = true;
		}
		else if (command == CropFit) { CropSetZoom(window, *s, CropFitPercent(*s, CropWorkArea(window))); }
		else if (command == CropWidth) { CropSetZoom(window, *s, CropFitPercent(*s, CropWorkArea(window), true)); }
		else if (command == CropOriginal) { CropSetZoom(window, *s, 100.); }
		else if (command == CropZoomIn || command == CropZoomOut) { CropSetZoom(window, *s, s->zoomPercent * (command == CropZoomIn ? 1.25 : .8)); }
		CropLayout(window, *s); InvalidateRect(window, nullptr, TRUE);
	};
	switch (message) {
	case WM_CREATE: {
		s->menu = CropMenu(); if (!s->menu || !SetMenu(window, s->menu)) return -1;
		const wchar_t* resources[]{L"CROP_CANCEL", L"CROP_ACCEPT", L"CROP_CUT", L"CROP_FREE", L"CROP_ZOOMOUT", L"CROP_ZOOMIN"};
		for (int i = 0; i < 6; ++i) { s->icons[i] = ToolbarBitmap(window, resources[i], i == 0 ? 4 : i == 1 ? 4 : i == 4 ? 9 : i == 5 ? 8 : 5); s->disabledIcons[i] = DisabledToolbarBitmap(s->icons[i]); }
		const int buttons[]{CropCancel, CropAccept, CropCut, CropFree, CropZoomOut, CropZoomIn};
		const wchar_t* labels[]{L"Abbrechen", L"Übernehmen", L"Ausschneiden", L"Freistellen", L"", L""};
		for (int i = 0; i < 6; ++i) { HWND button = Control(window, L"BUTTON", labels[i], WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, buttons[i]); SetWindowSubclass(button, ToolbarHoverProc, 2, reinterpret_cast<DWORD_PTR>(&s->hoveredToolbarButton)); }
		Control(window, L"STATIC", L"", SS_ETCHEDVERT, 0, 0, 2, 24, 411); Control(window, L"STATIC", L"", SS_ETCHEDVERT, 0, 0, 2, 24, 412);
		HWND zoom = Control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | WS_VSCROLL | CBS_AUTOHSCROLL, 0, 0, 1, 120, CropZoomCombo);
		for (const wchar_t* value : {L"25%", L"50%", L"75%", L"100%", L"150%", L"200%"}) SendMessageW(zoom, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
		Control(window, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE, 0, 0, 1, 1, 410);
		CropSetZoom(window, *s, CropFitPercent(*s, CropWorkArea(window))); return 0;
	}
	case WM_SIZE: CropRemapSelection(window, *s); return 0;
	case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {UiPx(window, 520), UiPx(window, 320)}; return 0;
	case WM_INITMENUPOPUP: CropLayout(window, *s); return 0;
	case WM_DRAWITEM: CropDrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), *s); return TRUE;
	case WM_PAINT: {
		PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT client{}; GetClientRect(window, &client);
		int width = client.right - client.left, height = client.bottom - client.top;
		if (width <= 0 || height <= 0) { EndPaint(window, &paint); return 0; }
		HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : nullptr;
		if (!buffer || !bitmap) { if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer); EndPaint(window, &paint); Error(window, std::runtime_error("Vorschaupuffer konnte nicht angelegt werden.")); return 0; }
		HGDIOBJ previous = SelectObject(buffer, bitmap);
		if (!previous || previous == HGDI_ERROR) { DeleteObject(bitmap); DeleteDC(buffer); EndPaint(window, &paint); Error(window, std::runtime_error("Vorschaupuffer konnte nicht aktiviert werden.")); return 0; }
		RECT area = CropWorkArea(window); HBRUSH background = CreateSolidBrush(RGB(170, 170, 170)); if (!background) { SelectObject(buffer, previous); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(window, &paint); Error(window, std::runtime_error("Vorschaohintergrund konnte nicht angelegt werden.")); return 0; } FillRect(buffer, &area, background); DeleteObject(background);
		int saved = SaveDC(buffer); IntersectClipRect(buffer, area.left, area.top, area.right, area.bottom); DrawImage(buffer, s->image, s->display);
		if (s->freePreview && s->selection.right > s->selection.left && s->selection.bottom > s->selection.top) {
			RECT selected = s->selection; HBRUSH white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)); RECT outside[]{ {s->display.left, s->display.top, s->display.right, selected.top}, {s->display.left, selected.bottom, s->display.right, s->display.bottom}, {s->display.left, selected.top, selected.left, selected.bottom}, {selected.right, selected.top, s->display.right, selected.bottom} };
			for (auto r : outside) if (r.right > r.left && r.bottom > r.top) FillRect(buffer, &r, white);
		}
		if (s->selection.right > s->selection.left && s->selection.bottom > s->selection.top) {
			HPEN outline = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
			if (outline) { HGDIOBJ oldPen = SelectObject(buffer, outline), oldBrush = SelectObject(buffer, GetStockObject(HOLLOW_BRUSH)); Rectangle(buffer, s->selection.left, s->selection.top, s->selection.right + 1, s->selection.bottom + 1); SelectObject(buffer, oldBrush); SelectObject(buffer, oldPen); DeleteObject(outline); }
		}
		RestoreDC(buffer, saved); BitBlt(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, buffer, area.left, area.top, SRCCOPY); SelectObject(buffer, previous); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(window, &paint); return 0;
	}
	case WM_LBUTTONDOWN: {
		POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		if (PtInRect(&s->display, point)) {
			RECT dirty = s->selection; bool hadSelection = !IsRectEmpty(&dirty);
			bool repaint = s->freePreview || hadSelection;
			if (s->freePreview) dirty = CropWorkArea(window);
			else if (hadSelection) InflateRect(&dirty, 2, 2);
			s->drawing = true; s->freePreview = false; s->start = point; s->selection = {point.x, point.y, point.x, point.y};
			SetCapture(window); CropUpdateActions(window, *s); SetCursor(LoadCursorW(nullptr, IDC_CROSS));
			if (repaint) InvalidateRect(window, &dirty, FALSE);
		}
		return 0;
	}
	case WM_RBUTTONDOWN: { POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; if (PtInRect(&s->display, point)) { s->panning = true; s->panStart = point; s->panOriginX = s->offsetX; s->panOriginY = s->offsetY; SetCapture(window); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); } return 0; }
	case WM_MOUSEMOVE:
		if (s->panning) { s->offsetX = std::max<int>(0, s->panOriginX - (GET_X_LPARAM(lParam) - s->panStart.x)); s->offsetY = std::max<int>(0, s->panOriginY - (GET_Y_LPARAM(lParam) - s->panStart.y)); CropRemapSelection(window, *s); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); }
		else if (s->drawing) { RECT previous = s->selection; auto x = std::clamp<LONG>(GET_X_LPARAM(lParam), s->display.left, s->display.right), y = std::clamp<LONG>(GET_Y_LPARAM(lParam), s->display.top, s->display.bottom); s->selection = {std::min(s->start.x, x), std::min(s->start.y, y), std::max(s->start.x, x), std::max(s->start.y, y)}; RECT dirty{}; UnionRect(&dirty, &previous, &s->selection); InflateRect(&dirty, 2, 2); CropUpdateActions(window, *s); InvalidateRect(window, &dirty, FALSE); SetCursor(LoadCursorW(nullptr, IDC_CROSS)); } return 0;
	case WM_LBUTTONUP: s->drawing = false; ReleaseCapture(); CropUpdateActions(window, *s); return 0;
	case WM_RBUTTONUP: s->panning = false; ReleaseCapture(); return 0;
	case WM_CAPTURECHANGED: s->drawing = s->panning = false; return 0;
	case WM_MOUSEWHEEL: {
		s->wheelDelta += GET_WHEEL_DELTA_WPARAM(wParam); int steps = s->wheelDelta / WHEEL_DELTA; s->wheelDelta %= WHEEL_DELTA;
		if (steps) {
			if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) CropSetZoom(window, *s, s->zoomPercent * std::pow(1.25, steps));
			else { RECT work = CropWorkArea(window); if (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) s->offsetX = std::max(0, s->offsetX - steps * static_cast<int>(std::max<LONG>(16, (work.right - work.left) / 8))); else s->offsetY = std::max(0, s->offsetY - steps * static_cast<int>(std::max<LONG>(16, (work.bottom - work.top) / 8))); CropRemapSelection(window, *s); }
		}
		return 0;
	}
	case WM_COMMAND: {
		int command = LOWORD(wParam);
		try {
			if (command == CropZoomCombo && (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS)) { HWND zoom = GetDlgItem(window, CropZoomCombo); int length = GetWindowTextLengthW(zoom); std::wstring text(static_cast<size_t>(length) + 1, L'\0'); GetWindowTextW(zoom, text.data(), length + 1); text.resize(length); if (!text.empty() && text.back() == L'%') text.pop_back(); if (!text.empty()) CropSetZoom(window, *s, Decimal(text, 5, 800)); return 0; }
			if (command == CropHelp) { ShowDialogHelp(window, L"Ausschneiden und Freistellen – Hilfe", {{L"Rechteck markieren", L"Ziehen Sie mit dem Kreuz-Cursor ein Rechteck auf der Seite auf. Der gestrichelte Rahmen zeigt den markierten Bereich."}, {L"Ausschneiden", L"Löscht den Inhalt innerhalb des Rechtecks. Die übrige Seite und die Seitengröße bleiben erhalten. Zum anschließenden Übernehmen zuerst den gewünschten Bereich markieren und Freistellen wählen."}, {L"Freistellen", L"Blendet die Bereiche außerhalb des Rechtecks aus. Erst danach wird Übernehmen aktiv und ersetzt den Scan durch den Ausschnitt. Eine neue Markierung deaktiviert Übernehmen wieder."}, {L"Kopieren und Ansicht", L"Kopieren legt den markierten Bildbereich in der Zwischenablage ab. Ansicht bietet Ganze Seite, Optimale Breite, Originalgröße und Zoom. Strg+Mausrad zoomt; das Mausrad scrollt, Rechtsziehen verschiebt die vergrößerte Seite."}}); return 0; }
			if (command == CropCancel || command == CropAccept || command == CropCut || command == CropCopy || command == CropFree || command == CropSelectAll || command == CropFit || command == CropWidth || command == CropOriginal || command == CropZoomIn || command == CropZoomOut) act(command);
		} catch (const std::exception& e) { Error(window, e); }
		return 0;
	}
	case WM_KEYDOWN: if (wParam == VK_ESCAPE) act(CropCancel); else if (wParam == VK_RETURN) act(CropAccept); return 0;
	case WM_CLOSE: act(CropCancel); return 0;
	case WM_DESTROY: for (auto icon : s->icons) if (icon) DeleteObject(icon); for (auto icon : s->disabledIcons) if (icon) DeleteObject(icon); if (s->menu) { SetMenu(window, nullptr); DestroyMenu(s->menu); s->menu = nullptr; } s->done = true; return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
static bool CropDialog(HWND owner, Image& image) {
	CropState state; state.image = image; RECT parent{}; GetWindowRect(owner, &parent);
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ScannerCrop", L"Ausschneiden und Freistellen", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, std::max(0L, parent.left + 20), std::max(0L, parent.top + 20), UiPx(owner, 672), UiPx(owner, 925), owner, nullptr, instance, &state);
	if (!window) throw std::runtime_error("Zuschnittdialog konnte nicht erstellt werden."); EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); MSG msg{};
	ACCEL shortcuts[]{{FVIRTKEY, VK_ESCAPE, CropCancel}, {FVIRTKEY, VK_RETURN, CropAccept}, {FCONTROL | FVIRTKEY, 'C', CropCopy}, {FCONTROL | FVIRTKEY, 'F', CropFree}, {FCONTROL | FVIRTKEY, 'A', CropSelectAll}, {FCONTROL | FVIRTKEY, '1', CropFit}, {FCONTROL | FVIRTKEY, 'B', CropWidth}, {FCONTROL | FVIRTKEY, 'O', CropOriginal}, {FCONTROL | FVIRTKEY, VK_ADD, CropZoomIn}, {FCONTROL | FVIRTKEY, VK_SUBTRACT, CropZoomOut}, {FCONTROL | FVIRTKEY, VK_OEM_PLUS, CropZoomIn}, {FCONTROL | FVIRTKEY, VK_OEM_MINUS, CropZoomOut}, {FVIRTKEY, VK_DELETE, CropCut}};
	HACCEL accelerators = CreateAcceleratorTableW(shortcuts, static_cast<int>(std::size(shortcuts)));
	while (!state.done) { int result = GetMessageW(&msg, nullptr, 0, 0); if (result <= 0) { DestroyWindow(window); if (!result) PostQuitMessage(static_cast<int>(msg.wParam)); break; } if (!TranslateAcceleratorW(window, accelerators, &msg) && !IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
	if (accelerators) DestroyAcceleratorTable(accelerators); EnableWindow(owner, TRUE); SetForegroundWindow(owner); if (state.accepted) image = std::move(state.image); return state.accepted;
}
enum Command { Import = 100, Save, Print, Mail, Scan, SelectSource, ScanSettings, DriverScan, ExportSettings, Options, CopyMode, Areas, Brightness, DeviceEvents, DeviceRegister, Cut, Copy, Paste, Delete, RotateL, RotateR, RotateHalf, Invert, MirrorH, MirrorV, CropPage, Reverse, Backs, SelectAll, ZoomIn, ZoomOut, Fit, Width, TwoPages, FullScreen, Clear, Help, About, Exit, ResetExport, BackgroundColor, UndoEdit, RestoreOriginal, CombinedView, SymbolsView, PageView, Support, PanelMenu, EditMenu, ViewMenu, ModeMono, ModeGray, ModeColor, SmallSymbols, SourceBase = 1000 };
struct Area { std::wstring name; double width = 0, height = 0, x = 0, y = 0; };
static std::vector<Area> StandardAreas() { return {{L"A2", 420, 594}, {L"A3", 297, 420}, {L"A4", 210, 297}, {L"A5", 148, 210}, {L"A6", 105, 148}, {L"US-Letter", 216, 279}, {L"Visitenkarte", 85, 55}}; }
static bool FitsArea(const Area& a, const ScanCapabilities& c) { return c.maxWidthMm <= 0 || c.maxHeightMm <= 0 || (a.x + a.width <= c.maxWidthMm && a.y + a.height <= c.maxHeightMm); }
static std::wstring AreaNumber(double value) { std::wostringstream out; out << std::setprecision(10) << value; return out.str(); }
static std::wstring AreaLabel(const Area& a) { return a.name + L" (" + AreaNumber(a.width) + L" x " + AreaNumber(a.height) + L")"; }
struct AreaDialogState {
	bool editor = false, done = false, accepted = false, filter = false;
	std::vector<Area> areas; Area area; int editing = -1;
	const std::vector<Area>* peers = nullptr;
	HFONT font = nullptr;
};
static bool AreaDialog(HWND owner, AreaDialogState& state);
static HWND AreaControlWindow(HWND owner, const wchar_t* type, const std::wstring& text, DWORD style, int x, int y, int w, int h, int id) {
	HWND control = Control(owner, type, text, style, UiPx(owner, x), UiPx(owner, y), UiPx(owner, w), UiPx(owner, h), id);
	auto state = reinterpret_cast<AreaDialogState*>(GetWindowLongPtrW(owner, GWLP_USERDATA)); if (state && state->font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE); return control;
}
static std::wstring AreaText(HWND window, int id) {
	HWND control = GetDlgItem(window, id); int length = GetWindowTextLengthW(control); std::wstring text(size_t(length) + 1, L'\0'); GetWindowTextW(control, text.data(), length + 1); text.resize(length); return text;
}
static Area ReadAreaEditor(HWND window) {
	Area next{AreaText(window, 114), Decimal(AreaText(window, 112), 0.001, 2000), Decimal(AreaText(window, 113), 0.001, 2000), Decimal(AreaText(window, 110), 0, 2000), Decimal(AreaText(window, 111), 0, 2000)};
	if (next.name.find_first_not_of(L" \t\r\n") == std::wstring::npos || next.name.size() > 100) throw std::runtime_error("Bitte eine Bezeichnung mit 1 bis 100 Zeichen eingeben."); return next;
}
static void PopulateAreas(HWND window, const AreaDialogState& state, int selected = 0) {
	HWND list = GetDlgItem(window, 100); ListView_DeleteAllItems(list);
	for (size_t i = 0; i < state.areas.size(); ++i) {
		const auto& a = state.areas[i]; LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = const_cast<wchar_t*>(a.name.c_str()); ListView_InsertItem(list, &item);
		std::wstring cells[]{AreaNumber(a.x), AreaNumber(a.y), AreaNumber(a.width), AreaNumber(a.height), L"mm"};
		for (int column = 0; column < 5; ++column) ListView_SetItemText(list, static_cast<int>(i), column + 1, cells[column].data());
	}
	if (!state.areas.empty()) { selected = std::clamp(selected, 0, static_cast<int>(state.areas.size()) - 1); ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
	EnableWindow(GetDlgItem(window, 102), !state.areas.empty()); EnableWindow(GetDlgItem(window, 103), !state.areas.empty());
}
static LRESULT CALLBACK AreaDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<AreaDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<AreaDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		LOGFONTW font{}; GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font); font.lfHeight = UiPx(window, font.lfHeight); state->font = CreateFontIndirectW(&font);
		if (state->editor) {
			AreaControlWindow(window, L"STATIC", L"Geben Sie hier den zu scannenden Bereich ein. \"X\" und \"Y\" beziehen sich auf den linken oberen Rand des Bereichs. Alle Maße sind in mm.", 0, 14, 14, 452, 49, 0);
			const wchar_t* labels[]{L"X", L"Y", L"Breite", L"Höhe"}; double values[]{state->area.x, state->area.y, state->area.width, state->area.height};
			for (int i = 0; i < 4; ++i) {
				int x = 14 + i * 114; AreaControlWindow(window, L"STATIC", labels[i], 0, x, 76, 100, 19, 0);
				HWND edit = AreaControlWindow(window, L"EDIT", AreaNumber(values[i]), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, 98, 102, 24, 110 + i);
				HWND spin = AreaControlWindow(window, UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_ARROWKEYS, x + 84, 98, 18, 24, 210 + i); SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0); SendMessageW(spin, UDM_SETRANGE32, 0, 2000);
			}
			AreaControlWindow(window, L"STATIC", L"Bezeichnung für das Format", 0, 14, 143, 450, 20, 0);
			AreaControlWindow(window, L"EDIT", state->area.name, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 14, 166, 444, 24, 114);
			AreaControlWindow(window, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 214, 211, 76, 27, IDOK); AreaControlWindow(window, L"BUTTON", L"Abbrechen", WS_TABSTOP, 298, 211, 90, 27, IDCANCEL); AreaControlWindow(window, L"BUTTON", L"?", WS_TABSTOP, 398, 211, 60, 27, IDHELP);
		} else {
			HWND list = AreaControlWindow(window, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 14, 14, 500, 247, 100); ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
			const wchar_t* columns[]{L"Bezeichnung", L"X", L"Y", L"Breite", L"Höhe", L""}; int widths[]{180, 59, 59, 76, 76, 30};
			for (int i = 0; i < 6; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; column.pszText = const_cast<wchar_t*>(columns[i]); column.cx = UiPx(window, widths[i]); column.fmt = i > 0 && i < 5 ? LVCFMT_RIGHT : LVCFMT_LEFT; ListView_InsertColumn(list, i, &column); }
			AreaControlWindow(window, L"BUTTON", L"Neu", WS_TABSTOP, 528, 14, 100, 28, 101); AreaControlWindow(window, L"BUTTON", L"Bearbeiten", WS_TABSTOP, 528, 51, 100, 28, 102); AreaControlWindow(window, L"BUTTON", L"Löschen", WS_TABSTOP, 528, 88, 100, 28, 103);
			AreaControlWindow(window, L"BUTTON", L"Zurücksetzen", WS_TABSTOP, 14, 274, 114, 28, 104);
			HWND check = AreaControlWindow(window, L"BUTTON", L"Im Feld \"Scanbereich\" des Hauptfensters nur Einträge zeigen, deren Scanbereich vollständig innerhalb des maximalen Scanbereichs liegt.", WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, 14, 314, 614, 46, 105); SendMessageW(check, BM_SETCHECK, state->filter ? BST_CHECKED : BST_UNCHECKED, 0);
			AreaControlWindow(window, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 374, 378, 76, 28, IDOK); AreaControlWindow(window, L"BUTTON", L"Abbrechen", WS_TABSTOP, 460, 378, 100, 28, IDCANCEL); AreaControlWindow(window, L"BUTTON", L"?", WS_TABSTOP, 570, 378, 58, 28, IDHELP); PopulateAreas(window, *state);
		} return 0;
	}
	if (message == WM_NOTIFY) {
		auto header = reinterpret_cast<NMHDR*>(lParam);
		if (state->editor && header->code == UDN_DELTAPOS) {
			auto delta = reinterpret_cast<NMUPDOWN*>(lParam); int id = static_cast<int>(header->idFrom) - 100;
			try { double value = Decimal(AreaText(window, id), 0, 2000); SetDlgItemTextW(window, id, AreaNumber(std::clamp(value + delta->iDelta, 0.0, 2000.0)).c_str()); } catch (...) { SetDlgItemTextW(window, id, L"0"); } return 1;
		}
		if (!state->editor && header->idFrom == 100 && header->code == NM_DBLCLK) { SendMessageW(window, WM_COMMAND, 102, 0); return 0; }
	}
	if (message == WM_COMMAND) {
		try {
			int id = LOWORD(wParam);
			if (id == IDCANCEL) { DestroyWindow(window); return 0; }
			if (id == IDHELP) { ShowDialogHelp(window, L"Scanbereiche – Hilfe", {{L"Maße", L"Scanbereiche werden in Millimetern ab dem linken oberen Rand angegeben. X und Y bestimmen die Position; Breite und Höhe bestimmen die Größe."}, {L"Liste bearbeiten", L"Neu, Bearbeiten und Löschen ändern die Liste. Zurücksetzen stellt die Standardliste wieder her. Erst OK in der Verwaltung speichert die Änderungen."}, {L"Filter", L"Der Filter berücksichtigt X + Breite und Y + Höhe. Bei unbekannter maximaler Scanfläche bleiben die Einträge sichtbar."}}); return 0; }
			if (id == IDOK) {
				if (state->editor) {
					Area next = ReadAreaEditor(window);
					if (state->peers) for (size_t i = 0; i < state->peers->size(); ++i) if (static_cast<int>(i) != state->editing && (*state->peers)[i].name == next.name) throw std::runtime_error("Bezeichnung bereits vorhanden.");
					state->area = next;
				} else state->filter = IsDlgButtonChecked(window, 105) == BST_CHECKED;
				state->accepted = true; DestroyWindow(window); return 0;
			}
			if (!state->editor) {
				int selected = ListView_GetNextItem(GetDlgItem(window, 100), -1, LVNI_SELECTED);
				if (id == 104) { state->areas = StandardAreas(); PopulateAreas(window, *state); }
				if (id == 103 && selected >= 0) { state->areas.erase(state->areas.begin() + selected); PopulateAreas(window, *state, selected); }
				if (id == 101 || (id == 102 && selected >= 0)) {
					if (id == 101 && state->areas.size() >= 200) throw std::runtime_error("Maximal 200 Scanbereiche moeglich.");
					AreaDialogState editor; editor.editor = true; editor.peers = &state->areas; editor.editing = id == 102 ? selected : -1; if (editor.editing >= 0) editor.area = state->areas[selected];
					if (AreaDialog(window, editor)) { if (editor.editing >= 0) state->areas[selected] = editor.area; else { state->areas.push_back(editor.area); selected = static_cast<int>(state->areas.size()) - 1; } PopulateAreas(window, *state, selected); }
				}
			}
		} catch (const std::exception& e) { Error(window, e); } return 0;
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) { DeleteObject(state->font); state->font = nullptr; } state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static bool AreaDialog(HWND owner, AreaDialogState& state) {
	WNDCLASSW cls{}; cls.lpfnWndProc = AreaDialogProc; cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); cls.lpszClassName = L"ScannerAreas"; RegisterClassW(&cls);
	RECT parent{}; GetWindowRect(owner, &parent); RECT bounds{0, 0, UiPx(owner, state.editor ? 474 : 644), UiPx(owner, state.editor ? 252 : 422)}; AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls.lpszClassName, state.editor ? L"Scanbereich bearbeiten" : L"Scanbereiche bearbeiten", WS_CAPTION | WS_SYSMENU | WS_POPUP, std::max(0L, parent.left + UiPx(owner, 30)), std::max(0L, parent.top + UiPx(owner, 30)), bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, instance, &state);
	if (!window) throw std::runtime_error("Scanbereichsdialog konnte nicht erstellt werden."); EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); SetFocus(GetDlgItem(window, state.editor ? 110 : 100)); MSG msg{};
	while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	EnableWindow(owner, TRUE); SetForegroundWindow(owner); return state.accepted;
}
struct SaveDialogBase {
	bool done = false, accepted = false;
	HFONT font = nullptr;
};
static HWND SaveControl(HWND window, const wchar_t* type, const std::wstring& text, DWORD style, int x, int y, int width, int height, int id) {
	HWND control = Control(window, type, text, style, UiPx(window, x), UiPx(window, y), UiPx(window, width), UiPx(window, height), id);
	auto state = reinterpret_cast<SaveDialogBase*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (state && state->font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
	return control;
}
static void SaveDialogFont(HWND window, SaveDialogBase& state) {
	NONCLIENTMETRICSW metrics{sizeof(metrics)}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	metrics.lfMessageFont.lfHeight = -UiPx(window, 12); state.font = CreateFontIndirectW(&metrics.lfMessageFont);
}
static bool SaveModal(HWND owner, SaveDialogBase& state, WNDPROC proc, const wchar_t* title, int width, int height) {
	WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
	cls.lpszClassName = title; RegisterClassW(&cls);
	RECT parent{}; GetWindowRect(owner, &parent); RECT bounds{0, 0, UiPx(owner, width), UiPx(owner, height)};
	AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
	HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, cls.lpszClassName, title, WS_CAPTION | WS_SYSMENU | WS_POPUP,
		std::max(0L, parent.left + UiPx(owner, 30)), std::max(0L, parent.top + UiPx(owner, 30)), bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, instance, &state);
	if (!window) throw std::runtime_error("Speicherdialog konnte nicht erstellt werden.");
	EnableWindow(owner, FALSE); ShowWindow(window, SW_SHOW); SetFocus(GetNextDlgTabItem(window, nullptr, FALSE)); MSG msg{};
	while (!state.done) { int result = GetMessageW(&msg, nullptr, 0, 0); if (result <= 0) { DestroyWindow(window); if (!result) PostQuitMessage(static_cast<int>(msg.wParam)); break; } if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
	EnableWindow(owner, TRUE); SetForegroundWindow(owner); return state.accepted;
}
static void SaveButtons(HWND window, int width, int y, const wchar_t* ok = L"OK") {
	SaveControl(window, L"BUTTON", ok, WS_TABSTOP | BS_DEFPUSHBUTTON, width - 232, y, 92, 26, IDOK);
	SaveControl(window, L"BUTTON", L"Abbrechen", WS_TABSTOP, width - 132, y, 90, 26, IDCANCEL);
	SaveControl(window, L"BUTTON", L"?", WS_TABSTOP, width - 34, y, 24, 26, IDHELP);
}
static void SaveCheck(HWND window, int id, bool checked) { CheckDlgButton(window, id, checked ? BST_CHECKED : BST_UNCHECKED); }
static int SaveCombo(HWND window, int id) { return static_cast<int>(SendDlgItemMessageW(window, id, CB_GETCURSEL, 0, 0)); }
static void SaveChoices(HWND window, int id, const std::vector<std::wstring>& values, int selected) {
	for (const auto& value : values) SendDlgItemMessageW(window, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
	SendDlgItemMessageW(window, id, CB_SETCURSEL, selected, 0);
}
static const std::vector<std::wstring> CloseChoices{L"Seiten in der Seitenübersicht beibehalten", L"Seitenübersicht nach Rückfrage leeren", L"Seitenübersicht ohne Rückfrage leeren"};
static const std::vector<std::wstring> DoubleClickChoices{L"Seite vergrößern", L"Dialog zum Zuschneiden der Seite öffnen"};
struct OptionsState : SaveDialogBase { bool openAfter = false, cropDoubleClick = false; int closeMode = 2; };
static int DoubleClickCommand(bool crop, bool middle, bool control) { return middle ? (crop && !control ? 0 : ZoomOut) : (crop && !control ? CropPage : ZoomIn); }
static LRESULT CALLBACK OptionsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<OptionsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<OptionsState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		SaveDialogFont(window, *state);
		SaveControl(window, L"BUTTON", L"Nach dem Speichern erste Seite öffnen", WS_TABSTOP | BS_AUTOCHECKBOX, 12, 20, 316, 24, 100); SaveCheck(window, 100, state->openAfter);
		SaveControl(window, L"STATIC", L"Beim Beenden des Programms:", 0, 12, 66, 316, 20, 0);
		SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 12, 86, 316, 140, 101); SaveChoices(window, 101, CloseChoices, state->closeMode);
		SaveControl(window, L"STATIC", L"Bei Doppelklick mit linker Maustaste auf eine Seite:", 0, 12, 132, 316, 20, 0);
		SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 12, 152, 316, 100, 102); SaveChoices(window, 102, DoubleClickChoices, state->cropDoubleClick ? 1 : 0);
		SaveButtons(window, 340, 202); return 0;
	}
	if (message == WM_COMMAND) {
		int id = LOWORD(wParam);
		if (id == IDHELP) { ShowDialogHelp(window, L"Optionen – Hilfe", {{L"Nach dem Speichern", L"Die erste gespeicherte Datei wird mit dem zugeordneten Programm geöffnet."}, {L"Beim Beenden", L"Seiten beibehalten stellt sie beim nächsten Start wieder her. Nach Rückfrage können Sie sie löschen oder behalten; ohne Rückfrage werden sie gelöscht."}, {L"Doppelklick", L"Wählen Sie Vergrößern oder den Zuschneidedialog. Bei Zuschneiden vergrößern beziehungsweise verkleinern Strg und linker beziehungsweise mittlerer Doppelklick."}}); return 0; }
		if (id == IDOK) { int close = SaveCombo(window, 101), click = SaveCombo(window, 102); if (close < 0 || close > 2 || click < 0 || click > 1) { Error(window, std::runtime_error("Ungueltige Option.")); return 0; } state->openAfter = IsDlgButtonChecked(window, 100) == BST_CHECKED; state->closeMode = close; state->cropDoubleClick = click == 1; state->accepted = true; DestroyWindow(window); return 0; }
		if (id == IDCANCEL) { DestroyWindow(window); return 0; }
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) DeleteObject(state->font); state->font = nullptr; state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
struct NativePrintState : SaveDialogBase {
	PrintOptions options;
	std::vector<std::wstring> printers;
	bool hasSelection = false, selectedOnly = false, outputToFile = false;
	HBITMAP icon = nullptr;
};
static PrintOptions ReadPrintFields(HWND window, const NativePrintState& state) {
	int selected = SaveCombo(window, 110); if (selected < 0 || static_cast<size_t>(selected) >= state.printers.size()) throw std::runtime_error("Bitte einen verfuegbaren Drucker auswaehlen.");
	PrintOptions options = state.options; options.printer = state.printers[selected]; options.copies = Integer(AreaText(window, 115), 1, 999); options.fit = IsDlgButtonChecked(window, 113) == BST_CHECKED; options.rotate = IsDlgButtonChecked(window, 114) == BST_CHECKED; options.output.clear(); return options;
}
static LRESULT CALLBACK NativePrintProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<NativePrintState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<NativePrintState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		SaveDialogFont(window, *state);
		SaveControl(window, L"STATIC", L"", SS_OWNERDRAW, 16, 14, 32, 32, 140);
		SaveControl(window, L"BUTTON", L"Alle Seiten", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 82, 8, 270, 22, 100);
		SaveControl(window, L"BUTTON", L"Nur markierte Seiten", WS_TABSTOP | BS_AUTORADIOBUTTON, 82, 30, 270, 22, 101);
		SaveCheck(window, state->selectedOnly && state->hasSelection ? 101 : 100, true); EnableWindow(GetDlgItem(window, 101), state->hasSelection);
		SaveControl(window, L"STATIC", L"Drucker:", 0, 14, 78, 52, 22, 0);
		SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | WS_GROUP | CBS_DROPDOWNLIST | WS_VSCROLL, 70, 76, 212, 180, 110);
		auto selectedPrinter = std::find(state->printers.begin(), state->printers.end(), state->options.printer);
		SaveChoices(window, 110, state->printers, state->options.printer.empty() ? (state->printers.empty() ? -1 : 0) : (selectedPrinter == state->printers.end() ? -1 : static_cast<int>(selectedPrinter - state->printers.begin())));
		SaveControl(window, L"BUTTON", L"Einstellungen", WS_TABSTOP, 289, 76, 83, 24, 111);
		SaveControl(window, L"BUTTON", L"Ausgabe in Datei", WS_TABSTOP | BS_AUTOCHECKBOX, 70, 104, 210, 22, 112); SaveCheck(window, 112, state->outputToFile);
		SaveControl(window, L"BUTTON", L"Anpassungen", BS_GROUPBOX, 14, 148, 235, 94, 120);
		SaveControl(window, L"BUTTON", L"Seiten auf Druckfläche skalieren", WS_TABSTOP | BS_AUTOCHECKBOX, 28, 173, 214, 22, 113);
		SaveControl(window, L"BUTTON", L"Seiten drehen, wenn erforderlich", WS_TABSTOP | BS_AUTOCHECKBOX, 28, 197, 214, 22, 114);
		SaveCheck(window, 113, state->options.fit); SaveCheck(window, 114, state->options.rotate);
		SaveControl(window, L"BUTTON", L"Druckoptionen", BS_GROUPBOX, 263, 148, 109, 94, 121);
		SaveControl(window, L"STATIC", L"Exemplare:", 0, 277, 170, 85, 20, 0);
		HWND copies = SaveControl(window, L"EDIT", std::to_wstring(state->options.copies), WS_TABSTOP | WS_BORDER | ES_NUMBER, 277, 190, 64, 24, 115);
		HWND spin = SaveControl(window, UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS, 0, 0, 0, 0, 116); SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(copies), 0); SendMessageW(spin, UDM_SETRANGE32, 1, 999); SendMessageW(spin, UDM_SETPOS32, 0, state->options.copies);
		SaveControl(window, L"BUTTON", L"Drucken", WS_TABSTOP | BS_DEFPUSHBUTTON, 159, 260, 85, 24, IDOK);
		SaveControl(window, L"BUTTON", L"Abbrechen", WS_TABSTOP, 252, 260, 85, 24, IDCANCEL);
		SaveControl(window, L"BUTTON", L"?", WS_TABSTOP, 345, 260, 27, 24, IDHELP);
		EnableWindow(GetDlgItem(window, IDOK), !state->printers.empty()); EnableWindow(GetDlgItem(window, 111), !state->printers.empty()); return 0;
	}
	if (message == WM_PAINT) { PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT header{0, 0, UiPx(window, 385), UiPx(window, 62)}; FillRect(dc, &header, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))); EndPaint(window, &paint); return 0; }
	if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) { int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam)); if (id == 100 || id == 101) { SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255)); return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH)); } }
	if (message == WM_DRAWITEM && wParam == 140) {
		auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); FillRect(draw->hDC, &draw->rcItem, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		if (state->icon) { BITMAP bitmap{}; GetObjectW(state->icon, sizeof(bitmap), &bitmap); HDC source = CreateCompatibleDC(draw->hDC); auto old = SelectObject(source, state->icon); BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(draw->hDC, 0, 0, UiPx(window, 32), UiPx(window, 32), source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend); SelectObject(source, old); DeleteDC(source); } return TRUE;
	}
	if (message == WM_COMMAND) {
		try {
			int id = LOWORD(wParam);
			if (id == IDCANCEL) { DestroyWindow(window); return 0; }
			if (id == IDHELP) { ShowDialogHelp(window, L"Drucken – Hilfe", {{L"Seitenauswahl", L"Wählen Sie alle oder nur die markierten Seiten."}, {L"Drucker und Einstellungen", L"Wählen Sie einen Drucker. Einstellungen öffnet dessen Eigenschaften, beispielsweise Papierformat und Ausrichtung."}, {L"Ausgabe in Datei", L"Vor dem Drucken wird nach einer PRN-Zieldatei gefragt; deren Format bestimmt der Treiber."}, {L"Anpassungen", L"Skalieren passt Seiten an die bedruckbare Fläche an. Drehen richtet Seiten bei Bedarf um 90 Grad aus."}, {L"Exemplare", L"Die Anwendung vervielfacht die Seiten; der Treiber erhält jeweils ein Exemplar. Abbrechen druckt nichts."}}); return 0; }
			if (id == 110 && HIWORD(wParam) == CBN_SELCHANGE) state->options.deviceMode.clear();
			if (id == 111 || id == IDOK) {
				int selected = SaveCombo(window, 110); if (selected < 0 || static_cast<size_t>(selected) >= state->printers.size()) throw std::runtime_error("Bitte einen verfuegbaren Drucker auswaehlen.");
				const auto& printer = state->printers[selected];
				if (id == 111) { ConfigurePrinter(window, printer, state->options.deviceMode, true); return 0; }
				PrintOptions options = ReadPrintFields(window, *state);
				if (options.deviceMode.empty()) ConfigurePrinter(window, printer, options.deviceMode, false);
				if (IsDlgButtonChecked(window, 112) == BST_CHECKED) {
					std::vector<wchar_t> filename(32768); wcscpy_s(filename.data(), filename.size(), L"Ausgabe.prn"); OPENFILENAMEW save{}; save.lStructSize = sizeof(save); save.hwndOwner = window; save.lpstrFilter = L"Druckdatei (*.prn)\0*.prn\0\0"; save.lpstrFile = filename.data(); save.nMaxFile = static_cast<DWORD>(filename.size()); save.lpstrDefExt = L"prn"; save.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
					if (!GetSaveFileNameW(&save)) { if (CommDlgExtendedError()) throw std::runtime_error("Druckdateiauswahl konnte nicht geoeffnet werden."); return 0; }
					options.output = filename.data(); if (std::filesystem::exists(options.output)) throw std::runtime_error("Druckausgabedatei existiert bereits. Bitte einen anderen Namen waehlen.");
				}
				state->selectedOnly = state->hasSelection && IsDlgButtonChecked(window, 101) == BST_CHECKED; state->outputToFile = IsDlgButtonChecked(window, 112) == BST_CHECKED; state->options = std::move(options); state->accepted = true; DestroyWindow(window); return 0;
			}
		} catch (const std::exception& e) { Error(window, e); } return 0;
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) { DeleteObject(state->font); state->font = nullptr; } state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static void RunNativePrintTests(HWND owner, const std::function<void(bool, const wchar_t*)>& test) {
	WNDCLASSW cls{}; cls.lpfnWndProc = NativePrintProc; cls.hInstance = instance; cls.lpszClassName = L"ScannerPrintTest"; cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassW(&cls);
	auto create = [&](NativePrintState& state) { HWND dialog = CreateWindowExW(0, cls.lpszClassName, L"Drucken", WS_POPUP, 0, 0, 385, 294, owner, nullptr, instance, &state); if (!dialog) throw std::runtime_error("Druckdialogtest konnte nicht erstellt werden."); return dialog; };
	NativePrintState state; state.printers = {L"Testdrucker A", L"Testdrucker B"}; state.options.copies = 7; HWND dialog = create(state);
	test(IsDlgButtonChecked(dialog, 100) == BST_CHECKED && !IsWindowEnabled(GetDlgItem(dialog, 101)), L"Print: all pages selected and marked-pages choice disabled without selection");
	test(SaveCombo(dialog, 110) == 0 && SendDlgItemMessageW(dialog, 110, CB_GETCOUNT, 0, 0) == 2, L"Print: printer list preserves default-first order");
	test(GetDlgItemInt(dialog, 115, nullptr, FALSE) == 7 && SendDlgItemMessageW(dialog, 116, UDM_GETBUDDY, 0, 0) == reinterpret_cast<LRESULT>(GetDlgItem(dialog, 115)), L"Print: copies spinner preserves previous count and is linked to edit");
	test(IsDlgButtonChecked(dialog, 113) == BST_CHECKED && IsDlgButtonChecked(dialog, 114) == BST_CHECKED && IsDlgButtonChecked(dialog, 112) == BST_UNCHECKED, L"Print: scale and rotation defaults enabled, file output opt-in");
	for (bool fit : {false, true}) for (bool rotate : {false, true}) for (int copies : {1, 999}) {
		SaveCheck(dialog, 113, fit); SaveCheck(dialog, 114, rotate); SetDlgItemInt(dialog, 115, copies, FALSE); SendDlgItemMessageW(dialog, 110, CB_SETCURSEL, 1, 0);
		auto options = ReadPrintFields(dialog, state); test(options.printer == L"Testdrucker B" && options.copies == copies && options.fit == fit && options.rotate == rotate && options.collate == state.options.collate, L"Print: selected printer, copy boundaries and all scale/rotation combinations reach print options");
	}
	for (const auto* invalid : {L"0", L"1000", L"abc", L""}) { SetDlgItemTextW(dialog, 115, invalid); bool rejected = false; try { ReadPrintFields(dialog, state); } catch (const std::exception&) { rejected = true; } test(rejected, L"Print: invalid copies rejected before driver access"); }
	SetDlgItemInt(dialog, 115, 1, FALSE); SendDlgItemMessageW(dialog, 110, CB_SETCURSEL, -1, 0); bool rejected = false; try { ReadPrintFields(dialog, state); } catch (const std::exception&) { rejected = true; } test(rejected, L"Print: missing printer selection rejected");
	state.options.deviceMode = {1, 2, 3}; SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(110, CBN_SELCHANGE), 0); test(state.options.deviceMode.empty(), L"Print: changing printer discards previous driver's private settings");
	SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0); test(state.done && !state.accepted && state.options.copies == 7 && state.options.printer.empty(), L"Print: Cancel commits no edited options and cannot print");
	NativePrintState marked; marked.printers = {L"Testdrucker"}; marked.hasSelection = marked.selectedOnly = true; dialog = create(marked);
	test(IsWindowEnabled(GetDlgItem(dialog, 101)) && IsDlgButtonChecked(dialog, 101) == BST_CHECKED && IsDlgButtonChecked(dialog, 100) == BST_UNCHECKED, L"Print: dragged selection presets marked pages");
	SendDlgItemMessageW(dialog, 100, BM_CLICK, 0, 0); test(IsDlgButtonChecked(dialog, 100) == BST_CHECKED && IsDlgButtonChecked(dialog, 101) == BST_UNCHECKED, L"Print: radio buttons switch exclusively to all pages"); SendMessageW(dialog, WM_CLOSE, 0, 0); test(marked.done && !marked.accepted, L"Print: title-bar close cancels");
	NativePrintState restored; restored.printers = {L"Testdrucker A", L"Testdrucker B"}; restored.options.printer = L"Testdrucker B"; restored.options.copies = 4; restored.options.fit = false; restored.options.rotate = false; restored.outputToFile = true; dialog = create(restored);
	test(SaveCombo(dialog, 110) == 1 && GetDlgItemInt(dialog, 115, nullptr, FALSE) == 4 && IsDlgButtonChecked(dialog, 112) == BST_CHECKED && IsDlgButtonChecked(dialog, 113) == BST_UNCHECKED && IsDlgButtonChecked(dialog, 114) == BST_UNCHECKED, L"Print: previously saved values displayed on reopening"); SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
	NativePrintState missing; missing.printers = {L"Testdrucker A"}; missing.options.printer = L"Nicht verfuegbar"; dialog = create(missing); test(SaveCombo(dialog, 110) == -1, L"Print: unavailable saved printer is not silently replaced"); SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
	NativePrintState empty; dialog = create(empty); test(!IsWindowEnabled(GetDlgItem(dialog, IDOK)) && !IsWindowEnabled(GetDlgItem(dialog, 111)), L"Print: no printers disables Print and Settings"); SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
}
struct FormatDialogState : SaveDialogBase {
	ExportOptions options;
	int jpegQuality = 80, pdfQuality = 80;
};
static int FormatQualityLimit(Format format) { return format == Format::Jpeg ? 99 : format == Format::Png ? 9 : 100; }
static std::vector<HelpSection> FormatHelpSections(Format format) {
	if (format == Format::Jpeg) return {{L"JPEG-Qualität", L"Die Qualität liegt zwischen 0 und 99. Ein höherer Wert verbessert die Bildqualität, erzeugt aber größere Dateien."}, {L"Verlustbehaftete Speicherung", L"Die ursprünglichen Bilddaten lassen sich aus JPEG nicht exakt wiederherstellen."}};
	if (format == Format::Png) return {{L"PNG-Kompression", L"Die Kompression liegt zwischen 1 und 9. PNG bleibt unabhängig von der Stufe verlustfrei."}, {L"Dateigröße und Zeit", L"Höhere Werte verkleinern die Datei meist, können aber das Speichern verlangsamen."}};
	if (format == Format::Tiff) return {{L"Farb- und Graustufenbilder", L"Zur Wahl stehen keine Kompression, LZW und ZIP."}, {L"Schwarzweißbilder", L"Zusätzlich stehen Fax G3 und Fax G4 zur Verfügung; diese Verfahren eignen sich nur für rein binäre Bilder. Die Invertierung kehrt Schwarz und Weiß um."}};
	return {{L"Seitenformat", L"Standardmäßig wird die Originalgröße entsprechend Bildpixeln und DPI verwendet. PDF und PDF/A bieten außerdem A4 mit Einpassen oder A4 mit bedarfsgerechter Querformatwahl."}, {L"Bildqualität", L"Die Qualität reicht von 0 bis 100; bei 100 werden RGB-Bilddaten verlustfrei gespeichert. Bei geringeren Werten werden geeignete Farbbilder JPEG-komprimiert."}, {L"Seitenübersicht", L"Legt fest, ob ein PDF-Betrachter beim Öffnen eine Miniaturenübersicht zeigt."}};
}
static void ResetFormatControls(HWND window, Format format) {
	if (format == Format::Tiff) { SendDlgItemMessageW(window, 100, CB_SETCURSEL, 1, 0); SendDlgItemMessageW(window, 101, CB_SETCURSEL, 2, 0); SaveCheck(window, 102, false); }
	else if (format == Format::Pdf || format == Format::PdfA) { SendDlgItemMessageW(window, 101, CB_SETCURSEL, static_cast<int>(PdfPageMode::Original), 0); SaveCheck(window, 102, false); SetDlgItemInt(window, 100, 80, FALSE); }
	else SetDlgItemInt(window, 100, format == Format::Png ? 6 : 80, FALSE);
}
static LRESULT CALLBACK FormatDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<FormatDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<FormatDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	auto format = state->options.format;
	if (message == WM_CREATE) {
		SaveDialogFont(window, *state); SaveControl(window, L"BUTTON", L"Zurücksetzen", WS_TABSTOP | BS_FLAT, 318, 8, 102, 24, 1001);
		if (format == Format::Tiff) {
			SaveControl(window, L"STATIC", L"Komprimierungsalgorithmus für ...", 0, 12, 42, 402, 22, 0);
			SaveControl(window, L"STATIC", L"... Farb-/Graustufenbilder:", 0, 12, 70, 200, 22, 0); SaveControl(window, L"STATIC", L"... Schwarzweissbilder:", 0, 220, 70, 200, 22, 0);
			for (int i = 0; i < 2; ++i) SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 12 + i * 208, 96, 198, 180, 100 + i);
			int color = state->options.tiffColor == TiffCompression::Zip ? 2 : state->options.tiffColor == TiffCompression::Lzw ? 1 : 0;
			SaveChoices(window, 100, {L"Unkomprimiert", L"LZW", L"ZIP"}, color); SaveChoices(window, 101, {L"Unkomprimiert", L"Fax G3", L"Fax G4", L"LZW", L"ZIP"}, static_cast<int>(state->options.tiffMono));
			SaveControl(window, L"BUTTON", L"S/W-Bilder invertieren", WS_TABSTOP | BS_AUTOCHECKBOX, 12, 138, 402, 24, 102); SaveCheck(window, 102, state->options.invertMono);
		} else {
			bool pdf = format == Format::Pdf || format == Format::PdfA; int y = pdf ? 144 : 52;
			if (pdf) {
				SaveControl(window, L"STATIC", L"Seitenformat:", 0, 12, 42, 400, 20, 0); SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 12, 64, 406, 160, 101);
				SaveChoices(window, 101, {L"A4 (210x297 mm), Vorlage ggf. einpassen", L"A4 (210x297 mm), Vorlage ggf. einpassen, ggf. Querformat", L"Wie Vorlage (Originalgröße)"}, static_cast<int>(state->options.pdfPageMode));
				SaveControl(window, L"BUTTON", L"automatisch öffnende Seitenübersicht", WS_TABSTOP | BS_AUTOCHECKBOX, 12, 104, 406, 24, 102); SaveCheck(window, 102, state->options.showThumbnails);

			}
			const wchar_t* label = format == Format::Png ? L"Kompression (1-9):" : pdf ? L"Qualität bei 24-Bit-Farb-/8-Bit-Graustufenbildern (0-100):" : L"Qualität erzeugter Dateien (0-99):";
			SaveControl(window, L"STATIC", label, 0, 12, y, 404, 22, 0);
			HWND edit = SaveControl(window, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 12, y + 26, 78, 24, 100);
			HWND spin = SaveControl(window, UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_NOTHOUSANDS, 70, y + 26, 20, 24, 200);
			SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0); SendMessageW(spin, UDM_SETRANGE32, format == Format::Png ? 1 : 0, FormatQualityLimit(format));
			SetDlgItemInt(window, 100, format == Format::Png ? std::clamp(state->options.pngCompression, 1, 9) : pdf ? state->pdfQuality : state->jpegQuality, FALSE);
			const wchar_t* text = format == Format::Png ? L"Bei größerer Kompression wird eine kleinere Datei bei gleicher Bildqualität erzeugt. Der Zeitaufwand erhöht sich." : pdf ? L"Bei höherer Qualität wird eine größere Datei erzeugt. Bei 100 erfolgt die Speicherung verlustfrei." : L"Bei einem höheren Wert wird eine größere Datei mit besserer Bildqualität erzeugt. Eine verlustfreie Speicherung ist beim JPG-Format NICHT möglich.";
			SaveControl(window, L"STATIC", text, 0, 108, y + 26, 308, 72, 0);
		}
		SaveButtons(window, 430, format == Format::Pdf || format == Format::PdfA ? 256 : 182); return 0;
	}
	if (message == WM_COMMAND) {
		try {
			int id = LOWORD(wParam);
			if (id == 1001) { ResetFormatControls(window, format); return 0; }
			if (id == IDHELP) { ShowDialogHelp(window, L"Formatoptionen – Hilfe", FormatHelpSections(format)); return 0; }
			if (id == IDCANCEL) { DestroyWindow(window); return 0; }
			if (id == IDOK) {
				ExportOptions next = state->options; int quality = 0;
				if (format == Format::Tiff) { const TiffCompression color[]{TiffCompression::None, TiffCompression::Lzw, TiffCompression::Zip}; int c = SaveCombo(window, 100), m = SaveCombo(window, 101); if (c < 0 || c > 2 || m < 0 || m > 4) throw std::runtime_error("Ungueltige TIFF-Kompression."); next.tiffColor = color[c]; next.tiffMono = static_cast<TiffCompression>(m); next.invertMono = IsDlgButtonChecked(window, 102) == BST_CHECKED; }
				else {
					quality = Integer(AreaText(window, 100), format == Format::Png ? 1 : 0, FormatQualityLimit(format));
					if (format == Format::Png) next.pngCompression = quality;
					else { next.quality = quality; if (format == Format::Pdf || format == Format::PdfA) { int mode = SaveCombo(window, 101); if (mode < 0 || mode > 2) throw std::runtime_error("Ungueltiges Seitenformat."); next.pdfPageMode = static_cast<PdfPageMode>(mode); next.showThumbnails = IsDlgButtonChecked(window, 102) == BST_CHECKED; } }
				}
				state->options = next; if (format == Format::Jpeg) state->jpegQuality = quality; if (format == Format::Pdf || format == Format::PdfA) state->pdfQuality = quality; state->accepted = true; DestroyWindow(window); return 0;
			}
		} catch (const std::exception& e) { Error(window, e); } return 0;
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) { DeleteObject(state->font); state->font = nullptr; } state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static bool FormatDialog(HWND owner, ExportOptions& options, int& jpegQuality, int& pdfQuality) {
	if (options.format == Format::Bmp) return false;
	FormatDialogState state; state.options = options; state.jpegQuality = jpegQuality; state.pdfQuality = pdfQuality;
	const wchar_t* title = options.format == Format::Jpeg ? L"Jpg-Optionen" : options.format == Format::Png ? L"Png-Optionen" : options.format == Format::Tiff ? L"Tif-Optionen" : L"Pdf-Optionen";
	if (!SaveModal(owner, state, FormatDialogProc, title, 430, options.format == Format::Pdf || options.format == Format::PdfA ? 294 : 220)) return false;
	options = state.options; jpegQuality = state.jpegQuality; pdfQuality = state.pdfQuality; return true;
}
static const std::vector<std::wstring> SaveFormats{L"Bmp - Windows-Bitmap", L"Jpg - Jpeg-Datei", L"Png - Portable Network Graphics", L"Tif - Tagged Image File Format", L"Pdf - Portable Document Format"};
static const wchar_t* SaveExtension(Format format) { static const wchar_t* values[]{L"bmp", L"jpg", L"png", L"tif", L"pdf", L"pdf"}; return values[static_cast<int>(format)]; }
static void ShowSaveHelp(HWND window) {
	int selected = SaveCombo(window, 112); if (selected < 0 || selected > 4) throw std::runtime_error("Ungueltiger Dateityp.");
	auto format = static_cast<Format>(selected);
	std::vector<HelpSection> sections{{L"Seitenauswahl", L"Wählen Sie, ob alle Seiten oder nur markierte Seiten ausgegeben werden."}, {L"Dateiname und Zielordner", L"Dateiname und Zielordner bestimmen den Speicherort. Die Dateiendung wird passend zum gewählten Dateityp gesetzt. Vorhandene Dateien werden nicht überschrieben. Über Automatik legen Sie die Vorgabe für den nächsten Aufruf fest."}};
	if (format == Format::Tiff || format == Format::Pdf) sections.push_back({L"Mehrere Seiten", L"Mit \"Alle Seiten in eine Datei\" werden die Seiten gemeinsam gespeichert. Ohne diese Auswahl entstehen einzelne Dateien mit fortlaufenden Seitennummern."});
	else sections.push_back({L"Eine Datei pro Seite", L"Jede Seite wird als eigene Bilddatei gespeichert. Bei mehreren Seiten werden fortlaufende Seitennummern an den Dateinamen angehängt."});
	if (format == Format::Bmp) sections.push_back({L"Windows-Bitmap (BMP)", L"Speichert das Bild ohne verlustbehaftete Kompression. Die Dateien können groß werden. Für BMP gibt es keine zusätzlichen Formatoptionen."});
	else {
		auto options = FormatHelpSections(format); sections.insert(sections.end(), options.begin(), options.end());
		sections.push_back({L"Optionen", L"Die Schaltfläche Optionen öffnet die Einstellungen für den gewählten Dateityp."});
	}
	if (format == Format::Pdf) {
		sections.push_back({L"PDF/A", L"PDF/A nach ISO 19005-1 ist vorausgewählt und abwählbar."});
		sections.push_back({L"Durchsuchbares PDF (OCR)", L"Mit \"Durchsuchbares PDF erstellen (OCR)\" aktivieren Sie die lokale Texterkennung und wählen die OCR-Sprache. Das Scanbild bleibt erhalten; erkannter Text wird suchbar und kopierbar. Tesseract und die gewählten Sprachdateien müssen verfügbar sein. Textzeilen müssen waagerecht und aufrecht stehen; drehen Sie den Scan gegebenenfalls vorher im Hauptfenster. Das gelbe Warndreieck neben der OCR-Auswahl öffnet die Hinweise zur Ausrichtung. Für gute Ergebnisse etwa 300 dpi verwenden. Erkennungsfehler sind möglich."});
		sections.push_back({L"Metadaten", L"Titel, Thema, Stichworte und Verfasser können Sie bearbeiten."});
	}
	const wchar_t* titles[]{L"Speichern als BMP – Hilfe", L"Speichern als JPG – Hilfe", L"Speichern als PNG – Hilfe", L"Speichern als TIFF – Hilfe", L"Speichern als PDF – Hilfe"};
	ShowDialogHelp(window, titles[selected], std::move(sections));
}
static constexpr int SaveOcrWarning = 141;
static std::vector<HelpSection> OcrOrientationHelp() {
	return {{L"Text waagerecht ausrichten", L"Für eine zuverlässige Texterkennung müssen die Textzeilen im Scan waagerecht und aufrecht lesbar sein. Seitlich gedrehter oder kopfstehender Text wird möglicherweise nicht oder falsch erkannt. Die OCR richtet die Seite nicht automatisch aus."},
		{L"Scan vor dem Speichern drehen", L"Schließen Sie den Speichern-Dialog gegebenenfalls mit Abbrechen. Markieren Sie den Scan im Hauptfenster und drehen Sie ihn über Bearbeiten nach links, nach rechts oder um 180°, bis der Text aufrecht steht. Öffnen Sie anschließend Speichern erneut und aktivieren Sie die OCR."},
		{L"Ergebnis kontrollieren", L"Wählen Sie die passende OCR-Sprache; für gedruckte Vorlagen sind etwa 300 dpi sinnvoll. Prüfen Sie den erkannten Text im fertigen PDF. Auch bei richtiger Ausrichtung sind Erkennungsfehler möglich."}};
}
struct NativeSaveState : SaveDialogBase {
	ExportOptions options;
	int jpegQuality = 80, pdfQuality = 80;
	size_t pages = 0, selectedPages = 0;
	bool selectedOnly = false, settingsOnly = false, mailMode = false, together = true, collisionSafe = false;
	std::filesystem::path path, folder;
	std::wstring filename;
	HBITMAP disk = nullptr;
	HICON fileIcon = nullptr, folderIcon = nullptr;
	std::function<void(HWND)> automation;
	std::function<bool(HWND)> initialize;
};
static std::wstring SaveCurrentUser() {
	std::vector<wchar_t> user(32768); DWORD length = static_cast<DWORD>(user.size());
	if (GetUserNameExW(NameDisplay, user.data(), &length) && user[0]) return user.data();
	length = static_cast<DWORD>(user.size());
	if (!GetUserNameW(user.data(), &length)) throw std::runtime_error("Aktueller Windows-Benutzer konnte nicht ermittelt werden.");
	return user.data();
}
static void ApplySaveDialogDefaults(NativeSaveState& state) {
	state.together = state.options.multipage;
	state.options.quality = state.options.format == Format::Jpeg ? state.jpegQuality : state.pdfQuality;
}
static bool SaveValidName(const std::wstring& name) {
	if (name.empty() || name.size() > 240 || name.back() == L'.' || name.back() == L' ' || name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
	for (wchar_t c : name) if (c < 32) return false;
	auto stem = name.substr(0, name.find(L'.')); while (!stem.empty() && stem.back() == L' ') stem.pop_back();
	std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
	if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" || stem == L"CONIN$" || stem == L"CONOUT$") return false;
	if (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") && ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'¹' || stem[3] == L'²' || stem[3] == L'³')) return false;
	return true;
}
static bool SaveReady(HWND window, const NativeSaveState& state) {
	if (state.settingsOnly) return true;
	if ((IsDlgButtonChecked(window, 101) == BST_CHECKED ? state.selectedPages : state.pages) == 0 || !SaveValidName(AreaText(window, 110))) return false;
	if (state.mailMode) return true;
	std::error_code error; auto folder = std::filesystem::path(AreaText(window, 111));
	return folder.is_absolute() && std::filesystem::is_directory(folder, error);
}
static void SaveReadFields(HWND window, NativeSaveState& state) {
	state.filename = AreaText(window, 110); if (!state.mailMode) state.folder = AreaText(window, 111); state.selectedOnly = IsDlgButtonChecked(window, 101) == BST_CHECKED;
	for (int i = 0; i < 4; ++i) { auto value = AreaText(window, 120 + i); if (i == 0) state.options.title = value; if (i == 1) state.options.subject = value; if (i == 2) state.options.keywords = value; if (i == 3) state.options.author = value; }
	int format = SaveCombo(window, 112); if (format < 0 || format > 4) throw std::runtime_error("Ungueltiger Dateityp.");
	state.options.format = format == 4 && IsDlgButtonChecked(window, 115) == BST_CHECKED ? Format::PdfA : static_cast<Format>(format);
	state.options.multipage = (format == 3 || format == 4) && IsDlgButtonChecked(window, 114) == BST_CHECKED;
	state.options.searchablePdf = IsDlgButtonChecked(window, 118) == BST_CHECKED;
	int language = SaveCombo(window, 119); if (language < 0 || language > 2) throw std::runtime_error("Ungueltige OCR-Sprache.");
	state.options.ocrLanguage = static_cast<OcrLanguage>(language);
	state.options.quality = format == 1 ? state.jpegQuality : state.pdfQuality;
}
static void SaveLayout(HWND window, NativeSaveState& state) {
	int format = SaveCombo(window, 112); bool pdf = format == 4, multi = format == 3 || pdf;
	EnableWindow(GetDlgItem(window, 113), format != 0); EnableWindow(GetDlgItem(window, 114), multi); SaveCheck(window, 114, multi && state.together);
	if (state.mailMode) {
		EnableWindow(GetDlgItem(window, 115), pdf);
		for (int id : {115, 118, 119, 120, 121, 122, 123, 130, 131, 132, 133, 134, SaveOcrWarning}) ShowWindow(GetDlgItem(window, id), pdf ? SW_SHOW : SW_HIDE);
		EnableWindow(GetDlgItem(window, 119), pdf && IsDlgButtonChecked(window, 118) == BST_CHECKED);
		int bottom = pdf ? 468 : 184; for (int id : {IDOK, IDCANCEL, IDHELP}) { RECT bounds{}; GetWindowRect(GetDlgItem(window, id), &bounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2); SetWindowPos(GetDlgItem(window, id), nullptr, bounds.left, UiPx(window, bottom), 0, 0, SWP_NOSIZE | SWP_NOZORDER); }
		RECT bounds{0, 0, UiPx(window, 410), UiPx(window, bottom + 38)}; AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
		SetWindowPos(window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER);
		EnableWindow(GetDlgItem(window, IDOK), SaveReady(window, state)); return;
	}
	EnableWindow(GetDlgItem(window, 115), pdf);
	for (int id : {115, 118, 119, 120, 121, 122, 123, 130, 131, 132, 133, 134, SaveOcrWarning}) ShowWindow(GetDlgItem(window, id), pdf ? SW_SHOW : SW_HIDE);
	EnableWindow(GetDlgItem(window, 119), pdf && IsDlgButtonChecked(window, 118) == BST_CHECKED);
	int bottom = pdf ? 504 : 258;
	for (int id : {IDOK, IDCANCEL, IDHELP}) { RECT bounds{}; GetWindowRect(GetDlgItem(window, id), &bounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2); SetWindowPos(GetDlgItem(window, id), nullptr, bounds.left, UiPx(window, bottom), 0, 0, SWP_NOSIZE | SWP_NOZORDER); }
	RECT bounds{0, 0, UiPx(window, 410), UiPx(window, bottom + 38)}; AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
	SetWindowPos(window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER);
	EnableWindow(GetDlgItem(window, IDOK), SaveReady(window, state));
}
static void SavePickerCheck(HRESULT result) {
	if (FAILED(result)) throw std::runtime_error("Windows-Speicherdialog konnte nicht ausgefuehrt werden (HRESULT " + std::to_string(static_cast<long>(result)) + ").");
}
static bool SavePickFile(HWND window, NativeSaveState& state) {
	SaveReadFields(window, state);
	Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
	SavePickerCheck(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf())));
	FILEOPENDIALOGOPTIONS flags{}; SavePickerCheck(dialog->GetOptions(&flags));
	SavePickerCheck(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR));
	const COMDLG_FILTERSPEC filters[]{
		{SaveFormats[0].c_str(), L"*.bmp"}, {SaveFormats[1].c_str(), L"*.jpg;*.jpeg"},
		{SaveFormats[2].c_str(), L"*.png"}, {SaveFormats[3].c_str(), L"*.tif;*.tiff"}, {SaveFormats[4].c_str(), L"*.pdf"}
	};
	SavePickerCheck(dialog->SetFileTypes(static_cast<UINT>(_countof(filters)), filters));
	SavePickerCheck(dialog->SetFileTypeIndex(static_cast<UINT>(std::min(4, static_cast<int>(state.options.format))) + 1));
	SavePickerCheck(dialog->SetDefaultExtension(SaveExtension(state.options.format)));
	SavePickerCheck(dialog->SetTitle(L"Speichern unter")); SavePickerCheck(dialog->SetOkButtonLabel(L"Weiter"));
	if (!state.filename.empty()) SavePickerCheck(dialog->SetFileName(state.filename.c_str()));
	std::error_code error;
	if (std::filesystem::is_directory(state.folder, error)) {
		Microsoft::WRL::ComPtr<IShellItem> folder;
		SavePickerCheck(SHCreateItemFromParsingName(state.folder.c_str(), nullptr, IID_PPV_ARGS(folder.GetAddressOf())));
		SavePickerCheck(dialog->SetFolder(folder.Get()));
	}
	const HRESULT result = dialog->Show(window);
	if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
	SavePickerCheck(result);
	Microsoft::WRL::ComPtr<IShellItem> item; SavePickerCheck(dialog->GetResult(item.GetAddressOf()));
	PWSTR rawPath = nullptr; SavePickerCheck(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath));
	const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> selectedPath(rawPath, &CoTaskMemFree);
	const std::filesystem::path path(selectedPath.get());
	UINT type = 0; SavePickerCheck(dialog->GetFileTypeIndex(&type));
	if (type < 1 || type > _countof(filters)) throw std::runtime_error("Ungueltiger Dateityp im Windows-Speicherdialog.");
	SetDlgItemTextW(window, 110, path.filename().c_str()); SetDlgItemTextW(window, 111, path.parent_path().c_str());
	SendDlgItemMessageW(window, 112, CB_SETCURSEL, static_cast<WPARAM>(type) - 1, 0); SaveLayout(window, state);
	return true;
}
static int CALLBACK SaveBrowseCallback(HWND window, UINT message, LPARAM, LPARAM data) { if (message == BFFM_INITIALIZED && data) SendMessageW(window, BFFM_SETSELECTIONW, TRUE, data); return 0; }
static bool SavePickFolder(HWND window) {
	auto initial = AreaText(window, 111); BROWSEINFOW browse{}; browse.hwndOwner = window; browse.lpszTitle = L"Zielordner auswählen"; browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE; browse.lpfn = SaveBrowseCallback; browse.lParam = reinterpret_cast<LPARAM>(initial.c_str());
	auto item = SHBrowseForFolderW(&browse); if (!item) return false; wchar_t folder[MAX_PATH]{}; bool ok = SHGetPathFromIDListW(item, folder); CoTaskMemFree(item);
	if (!ok) throw std::runtime_error("Ordnerpfad konnte nicht gelesen werden."); SetDlgItemTextW(window, 111, folder); return true;
}
static bool SaveTargetsAvailable(const std::filesystem::path& path, const ExportOptions& options, size_t pages) {
	if (std::filesystem::exists(path)) return false;
	bool multi = options.multipage && (options.format == Format::Tiff || options.format == Format::Pdf || options.format == Format::PdfA);
	if (!multi && pages > 1) for (size_t i = 1; i <= pages; ++i) { std::wostringstream suffix; suffix << L"_" << std::setw(4) << std::setfill(L'0') << i; if (std::filesystem::exists(path.parent_path() / (path.stem().wstring() + suffix.str() + path.extension().wstring()))) return false; }
	return true;
}
static std::filesystem::path SaveUniquePath(std::filesystem::path path, const ExportOptions& options, size_t pages) {
	const std::filesystem::path base = path; unsigned long long index = 1;
	while (!SaveTargetsAvailable(path, options, pages)) path = base.parent_path() / (base.stem().wstring() + L"_" + std::to_wstring(index++) + base.extension().wstring());
	return path;
}
static std::filesystem::path SaveAutomaticPath(int mode, const std::filesystem::path& last, const std::filesystem::path& folder, const ExportOptions& options, size_t pages) {
	if (mode < 0 || mode > 5) throw std::runtime_error("Ungueltige Dateinamenautomatik.");
	if (mode < 3) return {};
	auto path = last.empty() ? folder / L"Scan" : last; path.replace_extension(SaveExtension(options.format));
	if (mode == 5) { SYSTEMTIME time{}; GetLocalTime(&time); wchar_t name[80]{}; swprintf_s(name, L"Scan_%04u%02u%02u_%02u%02u%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond); path = folder / (std::wstring(name) + L"." + SaveExtension(options.format)); }
	return mode == 4 || mode == 5 ? SaveUniquePath(path, options, pages) : path;
}
static const std::vector<std::wstring> SaveNameModes{L"Keinen Dateinamen vorgeben", L"Dialog zur Dateiauswahl öffnen", L"Dialog zur Ordnerauswahl öffnen", L"Letzten Dateinamen einsetzen", L"Letzten Dateinamen mit Index versehen", L"Dateinamen aus Datum und Zeit erzeugen"};
struct NameAutomationState : SaveDialogBase { int mode = 0; };
static LRESULT CALLBACK NameAutomationProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<NameAutomationState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<NameAutomationState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		SaveDialogFont(window, *state); SaveControl(window, L"BUTTON", L"Zurücksetzen", WS_TABSTOP | BS_FLAT, 183, 6, 102, 24, 1001);
		SaveControl(window, L"STATIC", L"Beim nächsten Aufruf des \"Speichern\"-Dialogs:", 0, 10, 40, 280, 20, 0);
		SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 10, 66, 275, 200, 100); SaveChoices(window, 100, SaveNameModes, state->mode); SaveButtons(window, 295, 119); return 0;
	}
	if (message == WM_COMMAND) {
		int id = LOWORD(wParam);
		if (id == 1001) { SendDlgItemMessageW(window, 100, CB_SETCURSEL, 0, 0); return 0; }
		if (id == IDHELP) { ShowDialogHelp(window, L"Dateinamenautomatik – Hilfe", {{L"Beim nächsten Speichern", L"Die Automatik bestimmt, wie der Speicherdialog beim nächsten Aufruf startet."}, {L"Auswahlmöglichkeiten", L"Sie können keinen Namen vorgeben, eine Datei- oder Ordnerauswahl öffnen, den letzten Namen wiederverwenden, mit einem Index ergänzen oder einen Namen aus Datum und Uhrzeit bilden."}, {L"Vorhandene Dateien", L"Index- und Zeitnamen vermeiden vorhandene Zieldateien; bestehende Dateien werden nicht überschrieben."}}); return 0; }
		if (id == IDOK) { int mode = SaveCombo(window, 100); if (mode >= 0 && mode < 6) { state->mode = mode; state->accepted = true; DestroyWindow(window); } return 0; }
		if (id == IDCANCEL) { DestroyWindow(window); return 0; }
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) { DeleteObject(state->font); state->font = nullptr; } state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static LRESULT CALLBACK NativeSaveProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto state = reinterpret_cast<NativeSaveState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { state = static_cast<NativeSaveState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)); }
	if (!state) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_CREATE) {
		SaveDialogFont(window, *state);
		SaveControl(window, L"STATIC", L"", SS_OWNERDRAW, 16, 14, 32, 32, 140);
		SaveControl(window, L"BUTTON", L"Alle Seiten", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 64, 12, 208, 24, 100);
		SaveControl(window, L"BUTTON", L"Nur markierte Seiten", WS_TABSTOP | BS_AUTORADIOBUTTON, 64, 38, 240, 24, 101);
		SaveCheck(window, 100, true); EnableWindow(GetDlgItem(window, 101), state->selectedPages > 0 && !state->settingsOnly);
		SaveControl(window, WC_LINK, L"<a>Automatik</a>", WS_TABSTOP, 316, 12, 82, 24, 102);
		SaveControl(window, L"STATIC", L"Dateiname:", 0, 12, 80, 90, 22, 0); SaveControl(window, L"EDIT", state->filename, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 104, 78, state->mailMode ? 294 : 258, 24, 110);
		if (!state->mailMode) {
			SaveControl(window, L"BUTTON", L"", WS_TABSTOP | BS_ICON, 370, 78, 28, 24, 116);
			SaveControl(window, L"STATIC", L"In Ordner:", 0, 12, 116, 90, 22, 0); SaveControl(window, L"EDIT", state->folder.wstring(), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 104, 114, 258, 24, 111);
			SaveControl(window, L"BUTTON", L"", WS_TABSTOP | BS_ICON, 370, 114, 28, 24, 117);
			for (int i = 0; i < 2; ++i) { SHSTOCKICONINFO icon{sizeof(icon)}; if (SUCCEEDED(SHGetStockIconInfo(i ? SIID_FOLDER : SIID_DOCASSOC, SHGSI_ICON | SHGSI_SMALLICON, &icon))) { (i ? state->folderIcon : state->fileIcon) = icon.hIcon; SendDlgItemMessageW(window, 116 + i, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon.hIcon)); } else SetDlgItemTextW(window, 116 + i, L"…"); }
		}
		const int formatY = state->mailMode ? 112 : 150;
		SaveControl(window, L"STATIC", L"Dateityp:", 0, 12, formatY + 4, 90, 22, 0); SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 104, formatY, 210, 180, 112); SaveChoices(window, 112, SaveFormats, std::min(4, static_cast<int>(state->options.format)));
		SaveControl(window, L"BUTTON", L"Optionen", WS_TABSTOP, state->mailMode ? 322 : 338, formatY, state->mailMode ? 76 : 60, 26, 113);
		SaveControl(window, L"BUTTON", L"Alle Seiten in eine Datei", WS_TABSTOP | BS_AUTOCHECKBOX, 104, formatY + (state->mailMode ? 30 : 42), 290, 24, 114);
		SaveControl(window, L"BUTTON", L"Pdf/A nach ISO 19005-1", WS_TABSTOP | BS_AUTOCHECKBOX, 104, 226, 290, 24, 115); SaveCheck(window, 115, state->options.format == Format::PdfA);
		SaveControl(window, L"BUTTON", L"Durchsuchbares PDF erstellen (OCR)", WS_TABSTOP | BS_AUTOCHECKBOX, 104, 260, 254, 24, 118); SaveCheck(window, 118, state->options.searchablePdf);
		HWND warning = SaveControl(window, L"BUTTON", L"OCR-Hinweis: Textausrichtung", WS_TABSTOP | BS_ICON, 362, 250, 36, 36, SaveOcrWarning);
		HICON warningIcon = static_cast<HICON>(LoadImageW(nullptr, IDI_WARNING, IMAGE_ICON, UiPx(window, 16), UiPx(window, 16), LR_SHARED));
		if (!warning || !warningIcon) { Error(window, std::runtime_error("OCR-Warnsymbol konnte nicht geladen werden.")); return -1; }
		SendMessageW(warning, BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(warningIcon));
		SaveControl(window, L"STATIC", L"OCR-Sprache:", 0, 104, 296, 90, 22, 134);
		SaveControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 198, 292, 200, 120, 119);
		SaveChoices(window, 119, {L"Deutsch", L"Englisch", L"Deutsch und Englisch"}, static_cast<int>(state->options.ocrLanguage));
		const wchar_t* labels[]{L"Titel:", L"Thema:", L"Stichworte:", L"Verfasser:"}; const std::wstring values[]{state->options.title, state->options.subject, state->options.keywords, state->options.author};
		for (int i = 0; i < 4; ++i) { SaveControl(window, L"STATIC", labels[i], 0, 12, 340 + i * 36, 90, 22, 130 + i); SaveControl(window, L"EDIT", values[i], WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 104, 338 + i * 36, 294, 24, 120 + i); }
		SaveButtons(window, 410, 504, state->settingsOnly ? L"OK" : state->mailMode ? L"Weiter" : L"Fertigstellen");
		if (state->mailMode) {
			ShowWindow(GetDlgItem(window, 102), SW_HIDE);
			for (int id : {115, 118, 119, 120, 121, 122, 123, 130, 131, 132, 133, 134, SaveOcrWarning}) {
				HWND control = GetDlgItem(window, id); RECT bounds{}; GetWindowRect(control, &bounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
				SetWindowPos(control, nullptr, bounds.left, bounds.top - UiPx(window, 36), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
			}
		} else if (state->settingsOnly) for (int id : {100, 101, 102, 110, 111, 116, 117}) EnableWindow(GetDlgItem(window, id), FALSE);
		SaveLayout(window, *state); if (state->initialize) PostMessageW(window, WM_APP + 41, 0, 0); return 0;
	}
	if (message == WM_DRAWITEM && wParam == 140) {
		auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); FillRect(draw->hDC, &draw->rcItem, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
		if (state->disk) { BITMAP bitmap{}; GetObjectW(state->disk, sizeof(bitmap), &bitmap); HDC source = CreateCompatibleDC(draw->hDC); auto old = SelectObject(source, state->disk); BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(draw->hDC, 0, 0, UiPx(window, 32), UiPx(window, 32), source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend); SelectObject(source, old); DeleteDC(source); } return TRUE;
	}
	if (message == WM_NOTIFY) { auto notification = reinterpret_cast<NMHDR*>(lParam); if (notification->idFrom == 102 && (notification->code == NM_CLICK || notification->code == NM_RETURN)) { if (state->automation) state->automation(window); return 0; } }
	if (message == WM_APP + 41) { try { if (!state->initialize(window)) DestroyWindow(window); } catch (const std::exception& e) { Error(window, e); DestroyWindow(window); } return 0; }
	if (message == WM_COMMAND) {
		try {
			int id = LOWORD(wParam);
			if (id == IDCANCEL) { DestroyWindow(window); return 0; }
			if (id == IDHELP) { if (state->mailMode) ShowDialogHelp(window, L"E-Mail – Hilfe", {{L"Seitenauswahl", L"Wählen Sie alle Seiten oder nur die markierten Seiten. Ohne markierte Seiten ist diese Auswahl nicht verfügbar."}, {L"Anhang", L"Geben Sie den Namen des Anhangs ein und wählen Sie den Dateityp. Bei TIFF und PDF können Sie alle Seiten gemeinsam in eine Datei schreiben; andere Formate erzeugen eine Datei je Seite."}, {L"Weiter", L"Der Anhang wird erstellt und als Entwurf an das Standard-E-Mail-Programm übergeben. Der Mailclient öffnet den Entwurf; nichts wird automatisch versendet."}}); else ShowSaveHelp(window); return 0; }
			if (id == SaveOcrWarning) { ShowDialogHelp(window, L"OCR – Textausrichtung", OcrOrientationHelp()); return 0; }
			if (id == 114) state->together = IsDlgButtonChecked(window, 114) == BST_CHECKED;
			if (id == 118) EnableWindow(GetDlgItem(window, 119), SaveCombo(window, 112) == 4 && IsDlgButtonChecked(window, 118) == BST_CHECKED);
			if (id == 112 && HIWORD(wParam) == CBN_SELCHANGE) {
				auto name = std::filesystem::path(AreaText(window, 110)); if (!name.empty() && name.has_extension()) { name.replace_extension(SaveExtension(static_cast<Format>(SaveCombo(window, 112)))); SetDlgItemTextW(window, 110, name.c_str()); } SaveLayout(window, *state);
			}
			if (id == 116) SavePickFile(window, *state);
			if (id == 117) SavePickFolder(window);
			if (id == 113) { SaveReadFields(window, *state); FormatDialog(window, state->options, state->jpegQuality, state->pdfQuality); }
			if (id == IDOK && SaveReady(window, *state)) { SaveReadFields(window, *state); state->path = state->mailMode ? std::filesystem::path(state->filename) : state->folder / state->filename; state->path.replace_extension(SaveExtension(state->options.format)); if (state->collisionSafe && !state->settingsOnly) state->path = SaveUniquePath(state->path, state->options, state->selectedOnly ? state->selectedPages : state->pages); state->accepted = true; DestroyWindow(window); return 0; }
			EnableWindow(GetDlgItem(window, IDOK), SaveReady(window, *state));
		} catch (const std::exception& e) { Error(window, e); } return 0;
	}
	if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message == WM_DESTROY) { if (state->font) { DeleteObject(state->font); state->font = nullptr; } if (state->fileIcon) DestroyIcon(state->fileIcon); if (state->folderIcon) DestroyIcon(state->folderIcon); state->fileIcon = state->folderIcon = nullptr; state->done = true; return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static void RunNativeSaveTests(HWND owner, const std::filesystem::path& directory, const Image& image, const std::function<void(bool, const wchar_t*)>& test) {
	auto create = [&](WNDPROC proc, const wchar_t* name, void* state) {
		WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = instance; cls.lpszClassName = name; cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); RegisterClassW(&cls);
		return CreateWindowExW(0, name, L"", WS_POPUP, 0, 0, 500, 500, owner, nullptr, instance, state);
	};
	{
		NativeSaveState defaults; defaults.options.format = Format::Pdf; defaults.options.multipage = false; defaults.together = false;
		defaults.options.pdfPageMode = PdfPageMode::Original; defaults.options.showThumbnails = true; defaults.pdfQuality = 47; defaults.jpegQuality = 91; defaults.options.author = L"Alter Verfasser";
		ApplySaveDialogDefaults(defaults); defaults.folder = directory; defaults.pages = 1; defaults.filename = L"defaults.pdf";
		test(defaults.options.format == Format::Pdf && !defaults.options.multipage && !defaults.together && defaults.options.pdfPageMode == PdfPageMode::Original && defaults.options.showThumbnails && defaults.pdfQuality == 47 && defaults.options.quality == 47 && defaults.jpegQuality == 91 && defaults.options.author == L"Alter Verfasser", L"Save opening preserves supplied settings");
		HWND dialog = create(NativeSaveProc, L"SaveSmoke", &defaults);
		test(SaveCombo(dialog, 112) == 4 && IsDlgButtonChecked(dialog, 114) == BST_UNCHECKED && IsDlgButtonChecked(dialog, 115) == BST_UNCHECKED && IsWindowEnabled(GetDlgItem(dialog, 115)) && AreaText(dialog, 123) == L"Alter Verfasser", L"Save controls show previously chosen PDF options and author");
		SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
		FormatDialogState pdfDefaults; pdfDefaults.options = defaults.options; pdfDefaults.pdfQuality = defaults.pdfQuality;
		dialog = create(FormatDialogProc, L"FormatSmoke", &pdfDefaults);
		test(SaveCombo(dialog, 101) == static_cast<int>(PdfPageMode::Original) && IsDlgButtonChecked(dialog, 102) == BST_CHECKED && GetDlgItemInt(dialog, 100, nullptr, FALSE) == 47, L"PDF options open with saved page mode, overview and quality");
		SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
	}
	NativeSaveState empty; empty.folder = directory; HWND window = create(NativeSaveProc, L"SaveSmoke", &empty);
	test(IsWindow(window) && IsDlgButtonChecked(window, 100) == BST_CHECKED && !IsWindowEnabled(GetDlgItem(window, 101)) && !IsWindowEnabled(GetDlgItem(window, IDOK)), L"Native Save: all pages default, empty selection and empty document disabled");
	RECT folderBounds{}, typeBounds{}, optionBounds{}, multiBounds{}, nameBounds{}, metadataBounds{}; GetWindowRect(GetDlgItem(window, 111), &folderBounds); GetWindowRect(GetDlgItem(window, 112), &typeBounds); GetWindowRect(GetDlgItem(window, 113), &optionBounds); GetWindowRect(GetDlgItem(window, 114), &multiBounds); GetWindowRect(GetDlgItem(window, 110), &nameBounds); GetWindowRect(GetDlgItem(window, 120), &metadataBounds);
	test(folderBounds.bottom <= typeBounds.top && typeBounds.right <= optionBounds.left && optionBounds.bottom <= multiBounds.top, L"Native Save: folder, file type, options and multipage controls do not overlap");
	test(nameBounds.left == folderBounds.left && folderBounds.left == typeBounds.left && typeBounds.left == multiBounds.left && multiBounds.left == metadataBounds.left && nameBounds.right == folderBounds.right && typeBounds.right + UiPx(window, 24) == optionBounds.left, L"Native Save: filename, folder, format and PDF fields align as in reference");
	DestroyWindow(window);
	NativeSaveState mail; mail.mailMode = true; mail.pages = 3; mail.selectedPages = 1; mail.filename = L"Anlage"; mail.options.format = Format::Pdf; window = create(NativeSaveProc, L"SaveSmoke", &mail);
	RECT mailMulti{}, mailPdf{}, mailWarning{}, mailName{}, mailType{}, mailOption{}, mailMetadata{}; GetWindowRect(GetDlgItem(window, 114), &mailMulti); GetWindowRect(GetDlgItem(window, 115), &mailPdf); GetWindowRect(GetDlgItem(window, SaveOcrWarning), &mailWarning); GetWindowRect(GetDlgItem(window, 110), &mailName); GetWindowRect(GetDlgItem(window, 112), &mailType); GetWindowRect(GetDlgItem(window, 113), &mailOption); GetWindowRect(GetDlgItem(window, 120), &mailMetadata);
	test(mailMulti.bottom <= mailPdf.top && mailPdf.top - mailMulti.bottom <= UiPx(window, 30) && mailWarning.top > mailPdf.top, L"Mail Save: PDF controls follow multipage without overlap or excessive gap");
	test(mailName.left == mailType.left && mailType.left == mailMulti.left && mailMulti.left == mailPdf.left && mailPdf.left == mailMetadata.left && mailName.right == mailMetadata.right && mailType.right + UiPx(window, 8) == mailOption.left, L"Mail Save: filename, format and PDF fields align as in reference");
	test(AreaText(window, 110) == L"Anlage" && AreaText(window, IDOK) == L"Weiter" && SendDlgItemMessageW(window, 112, CB_GETCOUNT, 0, 0) == 5 && !GetDlgItem(window, 111) && (GetWindowLongPtrW(GetDlgItem(window, 115), GWL_STYLE) & WS_VISIBLE) && (GetWindowLongPtrW(GetDlgItem(window, 118), GWL_STYLE) & WS_VISIBLE) && (GetWindowLongPtrW(GetDlgItem(window, 120), GWL_STYLE) & WS_VISIBLE), L"Mail Save: PDF exposes the full save options with attachment name and Continue action");
	CheckRadioButton(window, 100, 101, 101); mail.selectedPages = 0; SendMessageW(window, WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
	test(!SaveReady(window, mail) && !IsWindowEnabled(GetDlgItem(window, IDOK)), L"Mail Save: selected-only without selected pages cannot continue");
	SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
	mail = NativeSaveState{}; mail.mailMode = true; mail.pages = 3; mail.selectedPages = 1; mail.filename = L"Anlage"; mail.options.format = Format::Pdf;
	window = create(NativeSaveProc, L"SaveSmoke", &mail); SaveCheck(window, 115, true); SaveCheck(window, 118, true); SendMessageW(window, WM_COMMAND, 118, 0); SendDlgItemMessageW(window, 119, CB_SETCURSEL, 2, 0); SetDlgItemTextW(window, 120, L"Anhangstitel"); SetDlgItemTextW(window, 123, L"Absender"); SendMessageW(window, WM_COMMAND, IDOK, 0);
	test(mail.accepted && mail.options.format == Format::PdfA && mail.options.searchablePdf && mail.options.ocrLanguage == OcrLanguage::GermanEnglish && mail.options.title == L"Anhangstitel" && mail.options.author == L"Absender", L"Mail Save: PDF/A, OCR language and PDF metadata reach the attachment export options");
	mail = NativeSaveState{}; mail.mailMode = true; mail.pages = 3; mail.selectedPages = 1; mail.filename = L"Anlage"; mail.options.format = Format::Pdf;
	window = create(NativeSaveProc, L"SaveSmoke", &mail); SendDlgItemMessageW(window, 112, CB_SETCURSEL, 1, 0); SendMessageW(window, WM_COMMAND, MAKEWPARAM(112, CBN_SELCHANGE), 0); SendMessageW(window, WM_COMMAND, IDOK, 0);
	test(mail.accepted && mail.path.filename() == L"Anlage.jpg" && mail.options.format == Format::Jpeg, L"Mail Save: Continue commits the selected image format and matching attachment extension");
	mail = NativeSaveState{}; mail.mailMode = true; mail.pages = 3; mail.selectedPages = 1; mail.filename = L"Anlage"; mail.options.format = Format::Pdf; mail.together = true;
	window = create(NativeSaveProc, L"SaveSmoke", &mail); SendDlgItemMessageW(window, 112, CB_SETCURSEL, 4, 0); SendMessageW(window, WM_COMMAND, MAKEWPARAM(112, CBN_SELCHANGE), 0); SendMessageW(window, WM_COMMAND, IDOK, 0);
	test(mail.accepted && mail.path.filename() == L"Anlage.pdf" && mail.options.multipage, L"Mail Save: PDF can keep all pages together in one attachment");
	auto bundledFiles = ExportImages({image, image}, directory / L"mail-bundle.pdf", mail.options); std::ifstream bundledPdf(bundledFiles[0], std::ios::binary); std::string bundledBytes((std::istreambuf_iterator<char>(bundledPdf)), {});
	test(bundledFiles.size() == 1 && bundledFiles[0].filename() == L"mail-bundle.pdf" && bundledBytes.find("/Count 2") != std::string::npos, L"Mail export: multipage PDF produces one two-page attachment");
	ExportOptions separateOptions; separateOptions.format = Format::Jpeg; separateOptions.multipage = false;
	auto separateFiles = ExportImages({image, image}, directory / L"mail-separate.jpg", separateOptions);
	test(separateFiles.size() == 2 && separateFiles[0].filename() == L"mail-separate_0001.jpg" && separateFiles[1].filename() == L"mail-separate_0002.jpg", L"Mail export: per-page format produces two separately named attachments");
	NativeSaveState state; state.pages = 3; state.selectedPages = 1; state.folder = directory; state.filename = L"Prüfung"; window = create(NativeSaveProc, L"SaveSmoke", &state);
	test(IsWindowEnabled(GetDlgItem(window, 101)) && IsDlgButtonChecked(window, 100) == BST_CHECKED && IsWindowEnabled(GetDlgItem(window, IDOK)), L"Native Save: selection enabled but all pages remains default");
	test(SendDlgItemMessageW(window, 112, CB_GETCOUNT, 0, 0) == 5, L"Native Save: exactly five formats, no sixth PDF/A format");
	RECT pdfBounds{}; GetWindowRect(window, &pdfBounds);
	for (int format = 0; format < 5; ++format) {
		SendDlgItemMessageW(window, 112, CB_SETCURSEL, format, 0); SendMessageW(window, WM_COMMAND, MAKEWPARAM(112, CBN_SELCHANGE), 0);
		bool multi = format >= 3, pdf = format == 4; RECT bounds{}; GetWindowRect(window, &bounds);
		test(!!IsWindowEnabled(GetDlgItem(window, 113)) == (format != 0) && !!IsWindowEnabled(GetDlgItem(window, 114)) == multi && (IsDlgButtonChecked(window, 114) == BST_CHECKED) == multi, L"Native Save: dynamic option and multipage availability per format");
		test(!!(GetWindowLongPtrW(GetDlgItem(window, 115), GWL_STYLE) & WS_VISIBLE) == pdf && !!(GetWindowLongPtrW(GetDlgItem(window, 120), GWL_STYLE) & WS_VISIBLE) == pdf && (pdf || bounds.bottom - bounds.top < pdfBounds.bottom - pdfBounds.top), L"Native Save: PDF-only fields and expanded height");
		if (pdf) test((IsDlgButtonChecked(window, 115) == BST_CHECKED) == (state.options.format == Format::PdfA) && IsWindowEnabled(GetDlgItem(window, 115)), L"Native Save: PDF/A choice survives format changes");
	}
	SetDlgItemTextW(window, 110, L"bad?.pdf"); test(!SaveReady(window, state) && !IsWindowEnabled(GetDlgItem(window, IDOK)), L"Native Save: invalid filename disables Finish");
	SetDlgItemTextW(window, 110, L"Prüfung.pdf"); SetDlgItemTextW(window, 111, (directory / L"missing-folder").c_str()); test(!SaveReady(window, state), L"Native Save: missing folder disables Finish"); SetDlgItemTextW(window, 111, directory.c_str());
	state.selectedPages = 0; CheckRadioButton(window, 100, 101, 101); test(!SaveReady(window, state), L"Native Save: selected-only without target pages cannot finish"); CheckRadioButton(window, 100, 101, 100); test(SaveReady(window, state), L"Native Save: all pages works without selection");
	SaveCheck(window, 115, true); SetDlgItemTextW(window, 120, L"Titel ä 中文 📄"); SendMessageW(window, WM_COMMAND, IDOK, 0);
	test(state.accepted && state.options.format == Format::PdfA && state.options.title == L"Titel ä 中文 📄" && !state.selectedOnly && state.options.multipage, L"Native Save: Finish commits PDF/A, Unicode metadata and all-page scope");
	auto output = ExportImages({image, image}, directory / L"native-save.pdf", state.options); std::ifstream pdf(output[0], std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(pdf)), {});
	test(output.size() == 1 && bytes.find("/Count 2") != std::string::npos && bytes.find("pdfaid:part") != std::string::npos, L"Native Save: committed options drive real synthetic PDF/A export");
	NativeSaveState cancel; cancel.options.title = L"Original"; cancel.folder = directory; window = create(NativeSaveProc, L"SaveSmoke", &cancel); SetDlgItemTextW(window, 120, L"Changed"); SendMessageW(window, WM_COMMAND, IDCANCEL, 0); test(!cancel.accepted && cancel.options.title == L"Original" && cancel.path.empty(), L"Native Save: Cancel never commits metadata or export path");
	NativeSaveState selected; selected.pages = 3; selected.selectedPages = 1; selected.folder = directory; selected.filename = L"selected.pdf"; window = create(NativeSaveProc, L"SaveSmoke", &selected); CheckRadioButton(window, 100, 101, 101); SaveCheck(window, 114, false); SaveCheck(window, 115, false); SetDlgItemTextW(window, 121, L"Thema 中文"); SetDlgItemTextW(window, 122, L"eins zwei"); SetDlgItemTextW(window, 123, L"Verfasser ä"); SendMessageW(window, WM_COMMAND, IDOK, 0);
	test(selected.accepted && selected.selectedOnly && !selected.options.multipage && selected.options.format == Format::Pdf && selected.options.subject == L"Thema 中文" && selected.options.keywords == L"eins zwei" && selected.options.author == L"Verfasser ä", L"Native Save: user can disable PDF/A and export plain PDF with edited metadata");
	for (bool settingsOnly : {false, true}) {
		NativeSaveState defaults; defaults.settingsOnly = settingsOnly; defaults.folder = directory; defaults.pages = 1; defaults.filename = L"ocr.pdf";
		HWND dialog = create(NativeSaveProc, L"SaveSmoke", &defaults);
		test(AreaText(dialog, 118) == L"Durchsuchbares PDF erstellen (OCR)" && IsDlgButtonChecked(dialog, 118) == BST_UNCHECKED && !IsWindowEnabled(GetDlgItem(dialog, 119)) && SaveCombo(dialog, 119) == 0, L"OCR: controls reside in Save with off default and German language");
		HWND warning = GetDlgItem(dialog, SaveOcrWarning); RECT checkboxBounds{}, warningBounds{}, client{};
		GetWindowRect(GetDlgItem(dialog, 118), &checkboxBounds); GetWindowRect(warning, &warningBounds); GetClientRect(dialog, &client);
		MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&checkboxBounds), 2); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&warningBounds), 2);
		test(warning && SendMessageW(warning, BM_GETIMAGE, IMAGE_ICON, 0) && IsWindowEnabled(warning) && (GetWindowLongPtrW(warning, GWL_STYLE) & WS_TABSTOP) && warningBounds.left >= checkboxBounds.right && warningBounds.right <= client.right && warningBounds.right == UiPx(dialog, 398) && warningBounds.top == UiPx(dialog, 250) && warningBounds.right - warningBounds.left == UiPx(dialog, 36) && warningBounds.bottom - warningBounds.top == UiPx(dialog, 36), L"OCR: warning fits a raised right-aligned 36-pixel button even with OCR off");
		auto orientationHelp = OcrOrientationHelp();
		test(orientationHelp.size() == 3 && std::wstring(orientationHelp[0].text).find(L"waagerecht") != std::wstring::npos && std::wstring(orientationHelp[0].text).find(L"nicht automatisch") != std::wstring::npos && std::wstring(orientationHelp[1].text).find(L"drehen") != std::wstring::npos, L"OCR: structured warning explains horizontal text, no autorotation and rotating scans");
		for (int format = 0; format < 5; ++format) {
			SendDlgItemMessageW(dialog, 112, CB_SETCURSEL, format, 0); SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(112, CBN_SELCHANGE), 0);
			test(!!(GetWindowLongPtrW(warning, GWL_STYLE) & WS_VISIBLE) == (format == 4), L"OCR: warning appears only for PDF and PDF/A");
		}
		SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
		for (int language = 0; language < 3; ++language) {
			NativeSaveState enabled = defaults; enabled.done = enabled.accepted = false;
			dialog = create(NativeSaveProc, L"SaveSmoke", &enabled);
			SendDlgItemMessageW(dialog, 118, BM_CLICK, 0, 0);
			test(IsDlgButtonChecked(dialog, 118) == BST_CHECKED && IsWindowEnabled(GetDlgItem(dialog, 119)), L"OCR: Save checkbox enables language selection");
			SendDlgItemMessageW(dialog, 119, CB_SETCURSEL, language, 0);
			for (int format : {1, 4}) {
				SendDlgItemMessageW(dialog, 112, CB_SETCURSEL, format, 0); SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(112, CBN_SELCHANGE), 0);
				test(!!(GetWindowLongPtrW(GetDlgItem(dialog, 118), GWL_STYLE) & WS_VISIBLE) == (format == 4) && !!(GetWindowLongPtrW(GetDlgItem(dialog, 119), GWL_STYLE) & WS_VISIBLE) == (format == 4) && !!IsWindowEnabled(GetDlgItem(dialog, 119)) == (format == 4) && SaveCombo(dialog, 119) == language && IsDlgButtonChecked(dialog, 118) == BST_CHECKED, L"OCR: PDF-only visibility preserves selections across format changes");
			}
			SaveCheck(dialog, 115, language != 1);
			SendMessageW(dialog, WM_COMMAND, IDOK, 0);
			test(enabled.accepted && enabled.options.searchablePdf && static_cast<int>(enabled.options.ocrLanguage) == language && enabled.options.format == (language == 1 ? Format::Pdf : Format::PdfA), L"OCR: Save commits languages for PDF and PDF/A");
			FormatDialogState options; options.options = enabled.options;
			HWND optionDialog = create(FormatDialogProc, L"FormatSmoke", &options);
			test(!GetDlgItem(optionDialog, 103) && !GetDlgItem(optionDialog, 104), L"OCR: controls removed from format options");
			SendMessageW(optionDialog, WM_COMMAND, 1001, 0); SendMessageW(optionDialog, WM_COMMAND, IDOK, 0);
			test(options.options.searchablePdf && static_cast<int>(options.options.ocrLanguage) == language, L"OCR: format options Reset and OK preserve Save OCR settings");
			enabled.done = enabled.accepted = false; dialog = create(NativeSaveProc, L"SaveSmoke", &enabled);
			SendDlgItemMessageW(dialog, 118, BM_CLICK, 0, 0); SendDlgItemMessageW(dialog, 119, CB_SETCURSEL, (language + 1) % 3, 0);
			SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
			test(!enabled.accepted && enabled.options.searchablePdf && static_cast<int>(enabled.options.ocrLanguage) == language, L"OCR: Save Cancel discards checkbox and language edits");
			enabled.done = false; dialog = create(NativeSaveProc, L"SaveSmoke", &enabled);
			SendDlgItemMessageW(dialog, 118, BM_CLICK, 0, 0); SendMessageW(dialog, WM_COMMAND, IDOK, 0);
			test(enabled.accepted && !enabled.options.searchablePdf && static_cast<int>(enabled.options.ocrLanguage) == language, L"OCR: Save can disable recognition without losing language");
		}
	}
	for (int mode = 0; mode < 3; ++mode) { FormatDialogState page; page.options.format = Format::Pdf; HWND dialog = create(FormatDialogProc, L"FormatSmoke", &page); SendDlgItemMessageW(dialog, 101, CB_SETCURSEL, mode, 0); SaveCheck(dialog, 102, false); SetDlgItemInt(dialog, 100, 42, FALSE); SendMessageW(dialog, WM_COMMAND, IDOK, 0); test(page.accepted && static_cast<int>(page.options.pdfPageMode) == mode && !page.options.showThumbnails && page.options.quality == 42 && page.pdfQuality == 42 && page.jpegQuality == 80, L"Native PDF: every page mode, overview and independent quality reaches ExportOptions"); }
	for (int color = 0; color < 3; ++color) for (int mono = 0; mono < 5; ++mono) { FormatDialogState tiff; tiff.options.format = Format::Tiff; HWND dialog = create(FormatDialogProc, L"FormatSmoke", &tiff); SendDlgItemMessageW(dialog, 100, CB_SETCURSEL, color, 0); SendDlgItemMessageW(dialog, 101, CB_SETCURSEL, mono, 0); SaveCheck(dialog, 102, true); SendMessageW(dialog, WM_COMMAND, IDOK, 0); const TiffCompression codecs[]{TiffCompression::None, TiffCompression::Lzw, TiffCompression::Zip}; test(tiff.accepted && tiff.options.tiffColor == codecs[color] && static_cast<int>(tiff.options.tiffMono) == mono && tiff.options.invertMono, L"Native TIFF: every color/mono codec pair and inversion reaches ExportOptions"); }
	for (Format format : {Format::Jpeg, Format::Png, Format::Tiff, Format::Pdf, Format::PdfA}) {
		FormatDialogState options; options.options.format = format; options.options.pngCompression = 9; options.jpegQuality = 91; options.pdfQuality = 47; options.options.tiffColor = TiffCompression::Zip; options.options.tiffMono = TiffCompression::G3;
		HWND dialog = create(FormatDialogProc, L"FormatSmoke", &options);
		if (format == Format::Tiff) test(SendDlgItemMessageW(dialog, 100, CB_GETCOUNT, 0, 0) == 3 && SendDlgItemMessageW(dialog, 101, CB_GETCOUNT, 0, 0) == 5, L"Native TIFF: three color codecs and five mono codecs");
		else { int low = -1, high = -1; SendDlgItemMessageW(dialog, 200, UDM_GETRANGE32, reinterpret_cast<WPARAM>(&low), reinterpret_cast<LPARAM>(&high)); test(low == (format == Format::Png ? 1 : 0) && high == FormatQualityLimit(format), L"Native format spinner matches JPG/PNG/PDF bounds"); }
		SendMessageW(dialog, WM_COMMAND, 1001, 0);
		test(format == Format::Tiff ? SaveCombo(dialog, 100) == 1 && SaveCombo(dialog, 101) == 2 && IsDlgButtonChecked(dialog, 102) == BST_UNCHECKED : (format == Format::Pdf || format == Format::PdfA) ? SaveCombo(dialog, 101) == static_cast<int>(PdfPageMode::Original) && GetDlgItemInt(dialog, 100, nullptr, FALSE) == 80 : GetDlgItemInt(dialog, 100, nullptr, FALSE) == (format == Format::Png ? 6u : 80u), L"Native format Reset restores documented defaults");
		SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0); test(!options.accepted && options.jpegQuality == 91 && options.pdfQuality == 47 && options.options.pngCompression == 9 && options.options.tiffColor == TiffCompression::Zip, L"Native format Reset then Cancel discards every change");
		FormatDialogState accepted; accepted.options.format = format; dialog = create(FormatDialogProc, L"FormatSmoke", &accepted); SendMessageW(dialog, WM_COMMAND, 1001, 0); SendMessageW(dialog, WM_COMMAND, IDOK, 0);
		test(accepted.accepted && (format != Format::Tiff || (accepted.options.tiffColor == TiffCompression::Lzw && accepted.options.tiffMono == TiffCompression::G4)) && (format != Format::Png || accepted.options.pngCompression == 6) && (format != Format::Jpeg || accepted.options.quality == 80) && ((format != Format::Pdf && format != Format::PdfA) || (accepted.options.quality == 80 && accepted.options.pdfPageMode == PdfPageMode::Original && !accepted.options.showThumbnails)), L"Native format OK commits reset values to ExportOptions");
		if (format != Format::Tiff) for (int value : {format == Format::Png ? 1 : 0, FormatQualityLimit(format)}) { FormatDialogState boundary; boundary.options.format = format; dialog = create(FormatDialogProc, L"FormatSmoke", &boundary); SetDlgItemInt(dialog, 100, value, FALSE); SendMessageW(dialog, WM_COMMAND, IDOK, 0); test(boundary.accepted && (format == Format::Png ? boundary.options.pngCompression : boundary.options.quality) == value, L"Native format accepts both numeric boundaries"); }
		if (format != Format::Tiff) for (int value : {format == Format::Png ? 0 : -1, FormatQualityLimit(format) + 1}) { bool rejected = false; try { Integer(std::to_wstring(value), format == Format::Png ? 1 : 0, FormatQualityLimit(format)); } catch (...) { rejected = true; } test(rejected, L"Native format validation rejects outside bounds"); }
	}
	for (int mode = 0; mode < 6; ++mode) {
		NameAutomationState automation; automation.mode = mode; HWND dialog = create(NameAutomationProc, L"NameSmoke", &automation);
		test(SendDlgItemMessageW(dialog, 100, CB_GETCOUNT, 0, 0) == 6 && SaveCombo(dialog, 100) == mode, L"Native automation contains six exact selectable modes"); SendMessageW(dialog, WM_COMMAND, IDOK, 0); test(automation.accepted && automation.mode == mode, L"Native automation OK commits selected mode");
		auto path = SaveAutomaticPath(mode, directory / L"last.pdf", directory, state.options, 3); test(mode < 3 ? path.empty() : !path.empty() && path.parent_path() == directory && path.extension() == L".pdf", L"Automation initializes paths only for last-name, index and time modes");
	}
	NameAutomationState reset; reset.mode = 5; window = create(NameAutomationProc, L"NameSmoke", &reset); SendMessageW(window, WM_COMMAND, 1001, 0); test(SaveCombo(window, 100) == 0, L"Automation Reset selects no filename"); SendMessageW(window, WM_COMMAND, IDCANCEL, 0); test(!reset.accepted && reset.mode == 5, L"Automation Cancel preserves original mode");
	for (int mode : {1, 2}) { NativeSaveState aborted; aborted.folder = directory; bool invoked = false; aborted.initialize = [&](HWND) { invoked = true; return false; }; window = create(NativeSaveProc, L"SaveSmoke", &aborted); SendMessageW(window, WM_APP + 41, 0, 0); test(invoked && aborted.done && !aborted.accepted && aborted.path.empty(), mode == 1 ? L"Automatic file picker cancellation closes Save without export" : L"Automatic folder picker cancellation closes Save ohne export"); }
	ExportOptions split; split.format = Format::Png; auto occupied = directory / L"indexed_0001.png"; { std::ofstream file(occupied); file << "unchanged"; }
	auto unique = SaveAutomaticPath(4, directory / L"indexed.png", directory, split, 3); test(unique.filename() == L"indexed_1.png" && SaveTargetsAvailable(unique, split, 3), L"Index automation avoids existing numbered output targets");
	auto dated = SaveAutomaticPath(5, {}, directory, split, 3); { std::ofstream file(dated); file << "unchanged"; } auto next = SaveUniquePath(dated, split, 3); test(next != dated && SaveTargetsAvailable(next, split, 3), L"Time automation resolves same-timestamp collisions");
	test(!SaveValidName(L"CON.pdf") && !SaveValidName(L"LPT1.png") && !SaveValidName(L"name.") && SaveValidName(L"Überprüfung 中文.pdf"), L"Save validates reserved Windows names and accepts Unicode");
}
enum SidebarCommand { SourceArrow = 510, DpiControl, AreaControl, PaperControl, BrightnessControl };
static const int presetDpi[]{100, 150, 200, 300, 400, 600, 1200, 2400, 4800};
struct Connection { bool wia; std::wstring id, name; };
struct SourceCache { std::vector<Connection> connections; std::wstring twainError, wiaError; };
class App {
public:
	HWND window = nullptr, list = nullptr, status = nullptr;
	Document document; std::filesystem::path data, settings, workspace;
	SourceCache sourceCache; std::future<SourceCache> sourceTask; bool sourcesReady = false, wia = false; std::wstring deviceId;
	ScanOptions scan; ScanCapabilities capabilities; ExportOptions exportOptions;
	std::vector<Area> areas; bool filterAreas = false, copyBusy = false, fullScreen = false, openAfter = false, cropDoubleClick = false;
	std::vector<int> sidebarAreas; std::wstring selectedArea; bool areaCapabilitiesKnown = false;
	int nameMode = 0, copyCount = 1, jpegQuality = 80, pdfQuality = 80, closeMode = 2; bool printFit = true, printRotate = true, printCollate = true, printToFile = false;
	std::wstring printPrinter; std::vector<BYTE> printDeviceMode;
	int viewMode = 1;
	COLORREF background = RGB(255, 255, 255); POINT scroll{}, panStart{}, panOrigin{}; bool panning = false, updatingScroll = false; RECT savedRect{}; LONG_PTR savedStyle = 0;
	POINT selectionStart{}; RECT selectionFrame{}; bool selecting = false, pageDrag = false, dragMoved = false, collapseSelection = false; size_t selectionAnchor = 0, pressedPage = 0; std::vector<size_t> selectionBase;
	std::wstring lastName; HWND buttons[9]{}, hoveredToolbarButton = nullptr;
	HWND zoomIn = nullptr, zoomOut = nullptr, panelCombo = nullptr, scanLabel = nullptr, tooltips = nullptr;
	HWND sourceArrow = nullptr, settingLabels[4]{}, settingControls[4]{};
	HFONT uiFont = nullptr; int fontDpi = 0;
	HBITMAP toolbarImages[10]{}, disabledToolbarImages[10]{};
	~App() { for (auto bitmap : toolbarImages) if (bitmap) DeleteObject(bitmap); for (auto bitmap : disabledToolbarImages) if (bitmap) DeleteObject(bitmap); if (uiFont) DeleteObject(uiFont); }
	RECT WorkRect() const { RECT r{}; GetClientRect(window, &r); r.left = UiPx(window, 139); r.right = std::max(r.left, r.right); r.top = UiPx(window, 60); r.bottom = std::max(r.top, r.bottom - UiPx(window, 20)); return r; }
	struct CanvasLayout { std::vector<RECT> pages; SIZE extent{}; };
	CanvasLayout Geometry() const {
		CanvasLayout result; RECT work = WorkRect(); int width = std::max(1L, work.right - work.left), height = std::max(1L, work.bottom - work.top);
		int gap = UiPx(window, 8), margin = viewMode == 0 ? 0 : UiPx(window, 12), caption = UiPx(window, 20);
		int boxWidth = viewMode == 4 ? UiPx(window, 42) : UiPx(window, 152), boxHeight = viewMode == 4 ? UiPx(window, 60) : UiPx(window, 216);
		int pitch = UiPx(window, viewMode == 4 ? 64 : 168), columns = viewMode >= 3 ? std::max(1, (width - 2 * margin + pitch - boxWidth) / pitch) : viewMode == 2 ? 2 : 1;
		int slot = std::max(1, (width - 2 * margin - (columns - 1) * gap) / columns), y = margin, rowHeight = 0;
		for (size_t i = 0; i < document.pages.size(); ++i) {
			int column = static_cast<int>(i % columns); if (!column && i) { y += rowHeight + gap + caption; rowHeight = 0; }
			const auto& image = document.pages[i].image; double scale = viewMode >= 3 ? std::min(double(boxWidth) / std::max(1u, image.width), double(boxHeight) / std::max(1u, image.height)) : double(slot) / std::max(1u, image.width);
			if (viewMode == 1 || viewMode == 2) scale = std::min(scale, double(std::max(1, height - 2 * margin - caption)) / std::max(1u, image.height));
			int w = viewMode == 0 ? slot : std::max(1, static_cast<int>(image.width * scale)), h = std::max(1, static_cast<int>(image.height * scale));
			int x = margin + (viewMode >= 3 ? column * pitch + (boxWidth - w) / 2 : column * (slot + gap) + (slot - w) / 2);
			int top = y + (viewMode >= 3 ? (boxHeight - h) / 2 : 0); RECT r{x, top, x + w, top + h};
			result.extent.cx = std::max<LONG>(result.extent.cx, r.right + margin); rowHeight = std::max(rowHeight, viewMode >= 3 ? boxHeight : h);
			OffsetRect(&r, work.left - scroll.x, work.top - scroll.y); result.pages.push_back(r);
		}
		result.extent.cy = document.pages.empty() ? 0 : y + rowHeight + caption + margin; return result;
	}
	void UpdateScroll() {
		if (updatingScroll || !window) return; updatingScroll = true;
		for (int pass = 0; pass < 3; ++pass) {
			auto layout = Geometry(); RECT work = WorkRect();
			for (int bar : {SB_VERT, SB_HORZ}) { int extent = bar == SB_VERT ? layout.extent.cy : layout.extent.cx, page = std::max(1L, bar == SB_VERT ? work.bottom - work.top : work.right - work.left); LONG& position = bar == SB_VERT ? scroll.y : scroll.x; position = std::clamp<LONG>(position, 0, std::max(0, extent - page)); SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS}; info.nMax = std::max(0, extent - 1); info.nPage = page; info.nPos = position; SetScrollInfo(window, bar, &info, TRUE); }
		}
		updatingScroll = false;
	}
	void ScrollTo(int x, int y) { scroll = {x, y}; UpdateScroll(); InvalidateRect(window, nullptr, TRUE); }
	void ScrollBar(int bar, int code) {
		SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, bar, &info); int position = info.nPos, line = UiPx(window, 48);
		switch (code) { case SB_LINEUP: position -= line; break; case SB_LINEDOWN: position += line; break; case SB_PAGEUP: position -= info.nPage; break; case SB_PAGEDOWN: position += info.nPage; break; case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break; case SB_TOP: position = 0; break; case SB_BOTTOM: position = info.nMax; break; default: return; }
		ScrollTo(bar == SB_HORZ ? position : scroll.x, bar == SB_VERT ? position : scroll.y);
	}
	size_t HitPage(POINT point) const { RECT work = WorkRect(); if (PtInRect(&work, point)) { auto layout = Geometry(); for (size_t i = 0; i < layout.pages.size(); ++i) if (PtInRect(&layout.pages[i], point)) return i; } return document.pages.size(); }
	void SetView(int mode) { viewMode = std::clamp(mode, 0, 4); scroll = {}; Set(L"CanvasMode", std::to_wstring(viewMode)); UpdateScroll(); InvalidateRect(window, nullptr, TRUE); }
	void BeginSelection(POINT point, WPARAM keys) {
		RECT work = WorkRect(); if (!PtInRect(&work, point)) return; SetFocus(window);
		if (GetKeyState(VK_LMENU) & 0x8000) { panning = true; panStart = point; panOrigin = scroll; SetCapture(window); return; }
		size_t hit = HitPage(point); selectionStart = {point.x + scroll.x, point.y + scroll.y}; selectionFrame = {}; dragMoved = false; collapseSelection = false; pageDrag = false;
		if (hit == document.pages.size()) { selecting = true; selectionBase = keys & MK_CONTROL ? document.selection : std::vector<size_t>{}; document.selection = selectionBase; }
		else {
			pressedPage = hit; bool selected = std::find(document.selection.begin(), document.selection.end(), hit) != document.selection.end();
			if (keys & MK_SHIFT) { size_t anchor = std::min(selectionAnchor, document.pages.size() - 1); if (!(keys & MK_CONTROL)) document.selection.clear(); for (size_t i = std::min(anchor, hit); i <= std::max(anchor, hit); ++i) if (std::find(document.selection.begin(), document.selection.end(), i) == document.selection.end()) document.selection.push_back(i); }
			else if (keys & MK_CONTROL) { if (selected) document.selection.erase(std::remove(document.selection.begin(), document.selection.end(), hit), document.selection.end()); else document.selection.push_back(hit); selectionAnchor = hit; }
			else { collapseSelection = selected && document.selection.size() > 1; if (!selected) document.selection = {hit}; selectionAnchor = hit; }
			pageDrag = std::find(document.selection.begin(), document.selection.end(), hit) != document.selection.end();
		}
		std::sort(document.selection.begin(), document.selection.end()); Refresh(); SetCapture(window);
	}
	void MovePointer(POINT point) {
		if (panning) { ScrollTo(panOrigin.x - point.x + panStart.x, panOrigin.y - point.y + panStart.y); return; }
		if (!selecting && !pageDrag) return;
		if (std::abs(point.x + scroll.x - selectionStart.x) > UiPx(window, 6) || std::abs(point.y + scroll.y - selectionStart.y) > UiPx(window, 6)) dragMoved = true;
		RECT work = WorkRect(); if (dragMoved && point.x >= work.left && point.x < work.right) { int delta = point.y < work.top ? -UiPx(window, 32) : point.y >= work.bottom ? UiPx(window, 32) : 0; if (delta) ScrollTo(scroll.x, scroll.y + delta); }
		if (pageDrag) { if (dragMoved) SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return; }
		POINT start{selectionStart.x - scroll.x, selectionStart.y - scroll.y}; selectionFrame = {std::min(start.x, point.x), std::min(start.y, point.y), std::max(start.x, point.x), std::max(start.y, point.y)};
		document.selection = selectionBase; auto layout = Geometry(); for (size_t i = 0; i < layout.pages.size(); ++i) { RECT intersection{}; if (IntersectRect(&intersection, &selectionFrame, &layout.pages[i]) && std::find(document.selection.begin(), document.selection.end(), i) == document.selection.end()) document.selection.push_back(i); }
		std::sort(document.selection.begin(), document.selection.end()); Refresh();
	}
	void EndSelection(POINT point) {
		bool drop = pageDrag && dragMoved, collapse = pageDrag && collapseSelection && !dragMoved; selecting = pageDrag = panning = false; selectionFrame = {}; ReleaseCapture();
		if (collapse) { document.selection = {pressedPage}; Refresh(); }
		if (drop) {
			POINT screen = point; ClientToScreen(window, &screen); for (int i = 2; i <= 4; ++i) { RECT button{}; GetWindowRect(buttons[i], &button); if (PtInRect(&button, screen)) { ExecuteSelection(i == 2 ? Save : i == 3 ? Print : Mail); return; } }
			RECT work = WorkRect(); if (PtInRect(&work, point)) { auto layout = Geometry(); size_t before = layout.pages.size(); for (size_t i = 0; i < layout.pages.size(); ++i) { const auto& r = layout.pages[i]; if (PtInRect(&r, point)) { before = i + (viewMode >= 2 ? point.x > (r.left + r.right) / 2 : point.y > (r.top + r.bottom) / 2); break; } if (point.y < r.top || (point.y < r.bottom && point.x < r.left)) { before = i; break; } } document.MoveSelected(before); if (!document.selection.empty()) selectionAnchor = document.selection.front(); Refresh(true); }
		}
		InvalidateRect(window, nullptr, TRUE);
	}
	bool CommandEnabled(int command) const {
		bool pages = !document.pages.empty(), selected = !document.selection.empty();
		switch (command) {
		case Clear: case Save: case Print: case Mail: case Width: case Fit: case TwoPages: case CombinedView: case SmallSymbols: case ZoomIn: case ZoomOut: case EditMenu: return pages;
		case Cut: case Copy: case Delete: case RotateL: case RotateR: case RotateHalf: case Invert: case MirrorH: case CropPage: return selected;
		case Reverse: case SelectAll: return pages;
		case Paste: return (!document.clipboard.empty() && clipboardSequence == GetClipboardSequenceNumber()) || IsClipboardFormatAvailable(CF_DIB);
		default: return true;
		}
	}
	void UpdateCommandMenu(HMENU menu) const {
		const int commands[]{Clear, Import, Scan, Save, Print, Mail, CopyMode, Exit, Cut, Copy, Paste, Delete, RotateL, RotateHalf, RotateR, Invert, MirrorH, CropPage, Reverse, SelectAll, Width, Fit, TwoPages, CombinedView, SmallSymbols, ZoomIn, ZoomOut};
		for (int command : commands) if (GetMenuState(menu, command, MF_BYCOMMAND) != static_cast<UINT>(-1)) EnableMenuItem(menu, command, MF_BYCOMMAND | (CommandEnabled(command) ? MF_ENABLED : MF_GRAYED));
	}
	void UpdateToolbar() const {
		const int commands[]{Save, Print, Mail, Clear, EditMenu, CombinedView}; for (int i = 2; i <= 7; ++i) EnableWindow(buttons[i], CommandEnabled(commands[i - 2]));
		EnableWindow(zoomIn, CommandEnabled(ZoomIn)); EnableWindow(zoomOut, CommandEnabled(ZoomOut));
		InvalidateRect(buttons[8], nullptr, FALSE);
	}
	std::vector<std::wstring> pendingArguments;
	bool actionSelectionOnly = false;
	explicit App() { data = UserDataDirectory(); settings = data / L"settings.ini"; workspace = data / L"Workspace"; }
	std::wstring Setting(const wchar_t* key, const std::wstring& fallback) const { return GetIni(settings, L"General", key, fallback); }
	void Set(const wchar_t* key, const std::wstring& value) const { SetIni(settings, L"General", key, value); }
	void Load() {
		document = {}; LoadWorkspace(document, workspace);
		wia = Setting(L"Backend", L"TWAIN") == L"WIA"; deviceId = Setting(L"DeviceId", L"");
		viewMode = Integer(Setting(L"CanvasMode", L"1"), 0, 4);
		openAfter = Setting(L"OpenAfter", L"0") == L"1"; cropDoubleClick = Setting(L"CropDoubleClick", L"0") == L"1"; closeMode = Integer(Setting(L"CloseMode", L"2"), 0, 2);
		nameMode = Integer(Setting(L"NameMode", L"0"), 0, 5); auto storedName = Setting(L"LastNameUtf16", L""); lastName = storedName.empty() ? Setting(L"LastName", L"") : DecodeSettingText(storedName); scan.source = Setting(L"Source", L"");
		scan.brightness = Decimal(GetIni(settings, SourceKey().c_str(), L"Brightness", L"0"), -1000, 1000);
		scan.dpi = Decimal(Setting(L"Dpi", L"300"), 25, 9600); scan.mode = Integer(Setting(L"Mode", L"2"), 0, 2); scan.widthMm = Decimal(Setting(L"Width", L"210"), 0.001, 2000); scan.heightMm = Decimal(Setting(L"Height", L"297"), 0.001, 2000); scan.driverDialog = Setting(L"DriverDialog", L"1") == L"1";
		LoadAreas(); LoadExport();
		scan.xMm = Decimal(Setting(L"X", L"0"), 0, 2000); scan.yMm = Decimal(Setting(L"Y", L"0"), 0, 2000); scan.maximumArea = Setting(L"MaximumArea", L"0") == L"1"; selectedArea = Setting(L"SelectedArea", L"");
		scan.feeder = Setting(L"Feeder", L"0") == L"1"; scan.duplex = scan.feeder && Setting(L"Duplex", L"0") == L"1";
		background = static_cast<COLORREF>(Integer(Setting(L"Background", std::to_wstring(RGB(255, 255, 255))), 0, 0xffffff));
		copyCount = Integer(Setting(L"PrintCopies", L"1"), 1, 999); printFit = Setting(L"PrintFit", L"1") == L"1"; printRotate = Setting(L"PrintRotate", L"1") == L"1"; printCollate = Setting(L"PrintCollate", L"1") == L"1"; printToFile = Setting(L"PrintToFile", L"0") == L"1";
		auto printer = Setting(L"PrintPrinterUtf16", L""); printPrinter = printer.empty() ? L"" : DecodeSettingText(printer);
		printDeviceMode.clear(); auto appendMode = [&](const std::wstring& encoded) { auto bytes = DecodeSettingText(encoded); for (auto byte : bytes) { if (byte > 255) throw std::runtime_error("Ungueltige gespeicherte Druckereinstellungen."); printDeviceMode.push_back(static_cast<BYTE>(byte)); } };
		auto chunks = Setting(L"PrintDeviceModeChunks", L"");
		if (chunks.empty()) { auto encoded = Setting(L"PrintDeviceMode", L""); if (!encoded.empty()) appendMode(encoded); }
		else { int count = Integer(chunks, 0, 64); for (int i = 0; i < count; ++i) { auto section = L"PrintDeviceMode-" + std::to_wstring(i); auto encoded = GetIni(settings, section.c_str(), L"Data", L""); if (encoded.empty()) throw std::runtime_error("Gespeicherte Druckereinstellungen sind unvollstaendig."); size_t before = printDeviceMode.size(); appendMode(encoded); size_t length = printDeviceMode.size() - before; if (length == 0 || length > 2048 || (i + 1 < count && length != 2048)) throw std::runtime_error("Gespeicherte Druckereinstellungen sind unvollstaendig."); } }
	}
	void SavePrintSettings() const {
		Set(L"PrintCopies", std::to_wstring(copyCount)); Set(L"PrintFit", printFit ? L"1" : L"0"); Set(L"PrintRotate", printRotate ? L"1" : L"0"); Set(L"PrintCollate", printCollate ? L"1" : L"0"); Set(L"PrintToFile", printToFile ? L"1" : L"0"); Set(L"PrintPrinterUtf16", EncodeSettingText(printPrinter));
		if (printDeviceMode.size() > 64 * 2048) throw std::runtime_error("Druckereinstellungen sind zu gross zum Speichern.");
		size_t count = (printDeviceMode.size() + 2047) / 2048; for (size_t i = 0; i < count; ++i) { std::wstring bytes; size_t end = (std::min)(printDeviceMode.size(), (i + 1) * 2048); for (size_t j = i * 2048; j < end; ++j) bytes.push_back(static_cast<wchar_t>(printDeviceMode[j])); auto section = L"PrintDeviceMode-" + std::to_wstring(i); SetIni(settings, section.c_str(), L"Data", EncodeSettingText(bytes)); }
		Set(L"PrintDeviceModeChunks", std::to_wstring(count));
	}
	HMENU ModeMenu() const {
		HMENU menu = CreatePopupMenu(); const wchar_t* names[]{L"Schwarz/Weiss", L"Graustufen", L"Farbe", L"Scanner-Menü"}; int ids[]{ModeMono, ModeGray, ModeColor, DriverScan};
		for (int i = 0; i < 4; ++i) { AppendMenuW(menu, MF_STRING, ids[i], names[i]); MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_FTYPE | MIIM_STATE; item.fType = MFT_RADIOCHECK; item.fState = ids[i] == (scan.driverDialog ? DriverScan : ModeMono + scan.mode) ? MFS_CHECKED : MFS_UNCHECKED; SetMenuItemInfoW(menu, ids[i], FALSE, &item); }
		return menu;
	}
	void SyncSettings() {
		const wchar_t* modes[]{L"Schwarz/Weiss", L"Graustufen", L"Farbe"}; SetWindowTextW(panelCombo, scan.driverDialog ? L"Scanner-Menü" : modes[scan.mode]);
		SendMessageW(settingControls[0], CB_RESETCONTENT, 0, 0); int selected = -1;
		for (int i = 0; i < static_cast<int>(_countof(presetDpi)); ++i) { auto text = std::to_wstring(presetDpi[i]) + L" dpi"; SendMessageW(settingControls[0], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())); if (scan.dpi == presetDpi[i]) selected = i; }
		if (selected < 0) { auto text = std::to_wstring(static_cast<int>(scan.dpi)) + L" dpi"; selected = static_cast<int>(SendMessageW(settingControls[0], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()))); }
		SendMessageW(settingControls[0], CB_SETCURSEL, selected, 0);
		SendMessageW(settingControls[1], CB_SETDROPPEDWIDTH, UiPx(window, 280), 0); SendMessageW(settingControls[1], CB_RESETCONTENT, 0, 0); sidebarAreas = {-1}; selected = scan.maximumArea ? 0 : -1;
		SendMessageW(settingControls[1], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Maximalgröße"));
		for (size_t i = 0; i < areas.size(); ++i) {
			const auto& a = areas[i]; if (filterAreas && !FitsArea(a, capabilities)) continue;
			auto text = AreaLabel(a); int row = static_cast<int>(SendMessageW(settingControls[1], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()))); sidebarAreas.push_back(static_cast<int>(i));
			if (!scan.maximumArea && a.width == scan.widthMm && a.height == scan.heightMm && a.x == scan.xMm && a.y == scan.yMm && (selected < 0 || a.name == selectedArea)) selected = row;
		}
		if (selected < 0) { selected = static_cast<int>(SendMessageW(settingControls[1], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Benutzerdefiniert"))); sidebarAreas.push_back(-3); }
		SendMessageW(settingControls[1], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Liste bearbeiten")); sidebarAreas.push_back(-2);
		SendMessageW(settingControls[1], CB_SETCURSEL, selected, 0); SendMessageW(settingControls[2], CB_SETCURSEL, scan.feeder ? (scan.duplex ? 2 : 1) : 0, 0);
		SendMessageW(settingControls[3], TBM_SETPOS, TRUE, static_cast<LPARAM>(scan.brightness));
	}
	void SidebarSetting(int id) {
		int selected = static_cast<int>(SendMessageW(settingControls[id - DpiControl], CB_GETCURSEL, 0, 0)); if (selected < 0) return;
		if (id == DpiControl && selected < static_cast<int>(_countof(presetDpi))) { scan.dpi = presetDpi[selected]; Set(L"Dpi", std::to_wstring(scan.dpi)); }
		if (id == AreaControl && static_cast<size_t>(selected) < sidebarAreas.size()) {
			int index = sidebarAreas[selected]; if (index == -2) { SyncSettings(); ManageAreas(); SyncSettings(); return; }
			if (index == -1) { scan.maximumArea = true; selectedArea.clear(); PersistArea(); }
			else if (index >= 0) { const auto& a = areas.at(index); scan.maximumArea = false; scan.widthMm = a.width; scan.heightMm = a.height; scan.xMm = a.x; scan.yMm = a.y; selectedArea = a.name; PersistArea(); }
		}
		if (id == PaperControl) { scan.feeder = selected != 0; scan.duplex = selected == 2; Set(L"Feeder", scan.feeder ? L"1" : L"0"); Set(L"Duplex", scan.duplex ? L"1" : L"0"); capabilities = {}; areaCapabilitiesKnown = false; SyncSettings(); }
	}
	void LoadExport() {
		auto read = [&](const wchar_t* key, const wchar_t* fallback) { return GetIni(settings, L"Export", key, fallback); };
		bool firstFormat = read(L"Format", L"").empty();
		int pageMode = Integer(read(L"PageMode", L"2"), 0, 2);
		if (read(L"PageModeOriginalDefault", L"0") != L"1" && pageMode == static_cast<int>(PdfPageMode::A4)) pageMode = static_cast<int>(PdfPageMode::Original);
		auto& o = exportOptions; o.format = static_cast<Format>(Integer(read(L"Format", L"5"), 0, 5)); o.multipage = read(L"Multipage", L"1") == L"1"; o.quality = Integer(read(L"Quality", L"80"), 0, 100); o.pngCompression = Integer(read(L"PngCompression", L"6"), 0, 9); o.pdfPageMode = static_cast<PdfPageMode>(pageMode); o.showThumbnails = read(L"Thumbnails", firstFormat ? L"0" : L"1") == L"1"; o.tiffColor = static_cast<TiffCompression>(Integer(read(L"TiffColor", L"3"), 0, 4)); o.tiffMono = static_cast<TiffCompression>(Integer(read(L"TiffMono", L"2"), 0, 4)); o.invertMono = read(L"InvertMono", L"0") == L"1";
		o.searchablePdf = read(L"SearchablePdf", L"0") == L"1"; o.ocrLanguage = static_cast<OcrLanguage>(Integer(read(L"OcrLanguage", L"0"), 0, 2));
		auto text = [&](const wchar_t* key) { const auto encoded = read((std::wstring(key) + L"Utf16").c_str(), L""); return encoded.empty() ? read(key, L"") : DecodeSettingText(encoded); };
		o.title = text(L"Title"); o.subject = text(L"Subject"); o.keywords = text(L"Keywords"); o.author = text(L"Author");
		if (firstFormat && o.author.empty()) o.author = SaveCurrentUser();
		jpegQuality = Integer(read(L"JpegQuality", L"80"), 0, 99); pdfQuality = Integer(read(L"PdfQuality", L"80"), 0, 100);
		o.quality = o.format == Format::Jpeg ? jpegQuality : pdfQuality;
	}
	void SaveExport() {
		auto write = [&](const wchar_t* key, const std::wstring& value) { SetIni(settings, L"Export", key, value); }; auto& o = exportOptions;
		write(L"SearchablePdf", o.searchablePdf ? L"1" : L"0"); write(L"OcrLanguage", std::to_wstring(static_cast<int>(o.ocrLanguage)));
		write(L"JpegQuality", std::to_wstring(jpegQuality)); write(L"PdfQuality", std::to_wstring(pdfQuality));
		write(L"Format", std::to_wstring(static_cast<int>(o.format))); write(L"Multipage", o.multipage ? L"1" : L"0"); write(L"Quality", std::to_wstring(o.quality)); write(L"PngCompression", std::to_wstring(o.pngCompression)); write(L"PageMode", std::to_wstring(static_cast<int>(o.pdfPageMode))); write(L"PageModeOriginalDefault", L"1"); write(L"Thumbnails", o.showThumbnails ? L"1" : L"0"); write(L"TiffColor", std::to_wstring(static_cast<int>(o.tiffColor))); write(L"TiffMono", std::to_wstring(static_cast<int>(o.tiffMono))); write(L"InvertMono", o.invertMono ? L"1" : L"0"); write(L"TitleUtf16", EncodeSettingText(o.title)); write(L"SubjectUtf16", EncodeSettingText(o.subject)); write(L"KeywordsUtf16", EncodeSettingText(o.keywords)); write(L"AuthorUtf16", EncodeSettingText(o.author));
	}
	void LoadAreas() {
		auto storedCount = GetIni(settings, L"Areas", L"Count", L""); int count = storedCount.empty() ? 0 : Integer(storedCount, 0, 200); areas.clear();
		for (int i = 0; i < count; ++i) { auto section = L"Area-" + std::to_wstring(i); areas.push_back({GetIni(settings, section.c_str(), L"Name", L"Bereich"), Decimal(GetIni(settings, section.c_str(), L"Width", L"210"), 0.001, 2000), Decimal(GetIni(settings, section.c_str(), L"Height", L"297"), 0.001, 2000), Decimal(GetIni(settings, section.c_str(), L"X", L"0"), 0, 2000), Decimal(GetIni(settings, section.c_str(), L"Y", L"0"), 0, 2000)}); }
		if (storedCount.empty()) DefaultAreas(); filterAreas = GetIni(settings, L"Areas", L"Filter", L"0") == L"1";
	}
	void DefaultAreas() { areas = StandardAreas(); }
	void PersistArea() const { Set(L"Width", std::to_wstring(scan.widthMm)); Set(L"Height", std::to_wstring(scan.heightMm)); Set(L"X", std::to_wstring(scan.xMm)); Set(L"Y", std::to_wstring(scan.yMm)); Set(L"MaximumArea", scan.maximumArea ? L"1" : L"0"); Set(L"SelectedArea", selectedArea); }
	void SaveAreas() { SetIni(settings, L"Areas", L"Count", std::to_wstring(areas.size())); SetIni(settings, L"Areas", L"Filter", filterAreas ? L"1" : L"0"); for (size_t i = 0; i < areas.size(); ++i) { auto section = L"Area-" + std::to_wstring(i); SetIni(settings, section.c_str(), L"Name", areas[i].name); SetIni(settings, section.c_str(), L"Width", std::to_wstring(areas[i].width)); SetIni(settings, section.c_str(), L"Height", std::to_wstring(areas[i].height)); SetIni(settings, section.c_str(), L"X", std::to_wstring(areas[i].x)); SetIni(settings, section.c_str(), L"Y", std::to_wstring(areas[i].y)); } }
	void CaptureSelection() { document.selection.clear(); int count = static_cast<int>(SendMessageW(list, LB_GETSELCOUNT, 0, 0)); if (count > 0) { std::vector<int> indices(count); SendMessageW(list, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(indices.data())); for (int i : indices) document.selection.push_back(i); } }
	size_t Current() const { return document.selection.empty() ? 0 : document.selection.front(); }
	void Refresh(bool = false) {
		document.TrimCache();
		SendMessageW(list, LB_RESETCONTENT, 0, 0);
		for (size_t i = 0; i < document.pages.size(); ++i) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L""));
		for (auto i : document.selection) SendMessageW(list, LB_SETSEL, TRUE, i); InvalidateRect(window, nullptr, TRUE);
		std::wstring text = std::to_wstring(document.pages.size()) + L" Seiten | " + std::to_wstring(document.selection.size()) + L" markiert | Scanmodus | " + scan.source;
		SetWindowTextW(status, document.pages.empty() ? L"" : text.c_str()); UpdateToolbar(); UpdateScroll();
	}
	void AddImages(std::vector<Image> images) { uint64_t total = 0; for (const auto& p : document.pages) total += p.image.pixels.size(); for (const auto& i : images) { i.Validate(); total += i.pixels.size(); } if (total > 9000000000ULL || document.pages.size() + images.size() > 10000) throw std::runtime_error("Dokument ueberschreitet 9 GB aktuelle Bilddaten oder 10000 Seiten."); for (auto& image : images) document.pages.push_back({NewId(), std::move(image)}); document.selection.clear(); selectionAnchor = 0; Refresh(true); }
	void ImportFiles(const std::vector<std::filesystem::path>& files) { for (const auto& file : files) try { AddImages(ReadImages(file)); } catch (const std::exception& e) { std::wstring error = file.wstring() + L"\n\n" + WideError(e.what()); MessageBoxW(window, error.c_str(), L"Bildimport fehlgeschlagen", MB_OK | MB_ICONERROR); } }
	void ImportDialog() { std::vector<wchar_t> buffer(65536); OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window; dialog.lpstrFilter = L"Bilddateien\0*.bmp;*.png;*.jpg;*.jpeg;*.tif;*.tiff;*.gif\0Alle Dateien\0*.*\0\0"; dialog.lpstrFile = buffer.data(); dialog.nMaxFile = static_cast<DWORD>(buffer.size()); dialog.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
		if (!GetOpenFileNameW(&dialog)) { if (CommDlgExtendedError()) throw std::runtime_error("Dateiauswahl fehlgeschlagen."); return; } std::vector<std::filesystem::path> paths; std::filesystem::path first = buffer.data(); auto next = buffer.data() + wcslen(buffer.data()) + 1; if (!*next) paths.push_back(first); else for (; *next; next += wcslen(next) + 1) paths.push_back(first / next); ImportFiles(paths);
	}
	void ChooseSource() {
		HMENU menu = SourceMenu(); RECT r{}; GetWindowRect(buttons[0], &r);
		int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, window, nullptr); DestroyMenu(menu); if (selected) Execute(selected);
	}
	void StartSources() {
		sourceTask = std::async(std::launch::async, [] {
			SourceCache result;
			try { for (const auto& s : TwainSources(nullptr)) if (_wcsnicmp(s.name.c_str(), L"WIA-", 4) != 0) result.connections.push_back({false, s.name, s.name}); }
			catch (const std::exception& e) { result.twainError = L"TWAIN: " + WideError(e.what()); }
			try { for (const auto& s : WiaSources()) result.connections.push_back({true, s.manufacturer, s.name}); }
			catch (const std::exception& e) { result.wiaError = L"WIA: " + WideError(e.what()); }
			return result;
		});
		SetTimer(window, 43, 100, nullptr);
	}
	void PollSources() {
		if (!sourceTask.valid() || sourceTask.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
		sourceCache = sourceTask.get(); sourcesReady = true; KillTimer(window, 43);
		if (scan.source.empty() && deviceId.empty() && !sourceCache.connections.empty()) SelectConnection(0);
	}
	bool Selected(const Connection& c) const { return c.wia == wia && c.id == (deviceId.empty() && !wia ? scan.source : deviceId); }
	void PopulateSourceMenu(HMENU menu) const {
		while (GetMenuItemCount(menu) > 0) if (!DeleteMenu(menu, 0, MF_BYPOSITION)) throw std::runtime_error("Scannerliste konnte nicht aktualisiert werden.");
		for (size_t i = 0; i < sourceCache.connections.size(); ++i) { const auto& c = sourceCache.connections[i]; std::wstring prefix = c.wia ? L"WIA-" : L"TW-"; auto text = c.name.rfind(prefix, 0) == 0 ? c.name : prefix + c.name; AppendMenuW(menu, MF_STRING | (Selected(c) ? MF_CHECKED : MF_UNCHECKED), SourceBase + i, text.c_str()); }
		if (!sourcesReady) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Scanner werden gesucht …");
		else if (sourceCache.connections.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Keine Scanner gefunden");
		if (!sourceCache.twainError.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, sourceCache.twainError.c_str());
		if (!sourceCache.wiaError.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, sourceCache.wiaError.c_str());
	}
	HMENU SourceMenu() const {
		HMENU menu = CreatePopupMenu();
		PopulateSourceMenu(menu);
		return menu;
	}
	std::wstring SourceKey() const { return DeviceKey(wia ? L"WIA:" + deviceId : scan.source); }
	void SelectConnection(size_t index) {
		const auto& c = sourceCache.connections.at(index); wia = c.wia; deviceId = c.id; scan.source = c.name; capabilities = {}; areaCapabilitiesKnown = false;
		Set(L"Backend", wia ? L"WIA" : L"TWAIN"); Set(L"DeviceId", deviceId); Set(L"Source", scan.source);
		scan.brightness = Decimal(GetIni(settings, SourceKey().c_str(), L"Brightness", L"0"), -1000, 1000); Refresh(); SendMessageW(window, WM_SIZE, 0, 0);
	}
	void ScanDialog() {
		if (scan.source.empty()) { ChooseSource(); return; } UpdateAreaCapabilities();
		std::vector<std::wstring> names{L"Benutzerdefiniert", L"Maximalgröße"}; std::wstring current = scan.maximumArea ? L"Maximalgröße" : L"Benutzerdefiniert";
		for (const auto& a : areas) if (!filterAreas || FitsArea(a, capabilities)) { names.push_back(AreaLabel(a)); if (!scan.maximumArea && a.width == scan.widthMm && a.height == scan.heightMm && a.x == scan.xMm && a.y == scan.yMm) current = AreaLabel(a); }
		names.push_back(L"Liste bearbeiten…");
		std::vector<Field> f{{L"Bildmodus", std::vector<std::wstring>{L"Schwarzweiß", L"Graustufen", L"Farbe"}[scan.mode], {L"Schwarzweiß", L"Graustufen", L"Farbe"}}, {L"Auflösung (DPI, 25–9600; Treiber entscheidet)", std::to_wstring(static_cast<int>(scan.dpi))}, {L"Scanbereich (Liste über Scanner → Scanbereiche)", current, names}, {L"Breite bei benutzerdefiniert (mm)", std::to_wstring(scan.widthMm)}, {L"Höhe bei benutzerdefiniert (mm)", std::to_wstring(scan.heightMm)}, {L"Temporäre Helligkeit (−1000 bis 1000)", std::to_wstring(scan.brightness)}, {L"Automatischer Einzug (Quelle: " + Yn(capabilities.feeder) + L")", Yn(scan.feeder), capabilities.feeder ? YesNo : std::vector<std::wstring>{L"Nein"}}, {L"Duplex (Quelle: " + Yn(capabilities.duplex) + L")", Yn(scan.duplex), capabilities.duplex ? YesNo : std::vector<std::wstring>{L"Nein"}}, {L"Herstellertreiberdialog statt eigener Werte", Yn(scan.driverDialog), YesNo}, {L"X bei benutzerdefiniert (mm)", AreaNumber(scan.xMm)}, {L"Y bei benutzerdefiniert (mm)", AreaNumber(scan.yMm)}};
		if (!Form(window, L"Scaneinstellungen – ohne Treiberdialog werden Werte exakt angefordert", f)) return;
		if (f[2].value == L"Liste bearbeiten…") { ManageAreas(); ScanDialog(); return; }
		ScanOptions next = scan; next.mode = f[0].value == L"Farbe" ? 2 : f[0].value == L"Graustufen" ? 1 : 0; next.dpi = Decimal(f[1].value, 25, 9600); next.widthMm = Decimal(f[3].value, 0.001, 2000); next.heightMm = Decimal(f[4].value, 0.001, 2000); next.xMm = Decimal(f[9].value, 0, 2000); next.yMm = Decimal(f[10].value, 0, 2000); next.maximumArea = f[2].value == L"Maximalgröße"; selectedArea.clear();
		for (const auto& a : areas) if (AreaLabel(a) == f[2].value) { next.widthMm = a.width; next.heightMm = a.height; next.xMm = a.x; next.yMm = a.y; selectedArea = a.name; } next.brightness = Decimal(f[5].value, -1000, 1000); next.feeder = IsYes(f[6]); next.duplex = IsYes(f[7]); next.driverDialog = IsYes(f[8]); scan = next; PersistArea();
		Set(L"Mode", std::to_wstring(scan.mode)); Set(L"Dpi", std::to_wstring(scan.dpi)); Set(L"Width", std::to_wstring(scan.widthMm)); Set(L"Height", std::to_wstring(scan.heightMm)); Set(L"DriverDialog", scan.driverDialog ? L"1" : L"0"); Set(L"Feeder", scan.feeder ? L"1" : L"0"); Set(L"Duplex", scan.duplex ? L"1" : L"0"); SetIni(settings, SourceKey().c_str(), L"Brightness", std::to_wstring(scan.brightness));
	}
	void UpdateAreaCapabilities() {
		if (!areaCapabilitiesKnown && !scan.source.empty()) { capabilities = wia ? ScanCapabilities{} : TwainCapabilities(window, scan.source, scan.feeder); areaCapabilitiesKnown = true; }
		SyncSettings();
	}
	void ManageAreas() {
		AreaDialogState draft; draft.areas = areas; draft.filter = filterAreas;
		if (!AreaDialog(window, draft)) return;
		CommitAreas(draft);
	}
	void CommitAreas(const AreaDialogState& draft) {
		if (!draft.accepted) return;
		areas = draft.areas; filterAreas = draft.filter;
		if (!scan.maximumArea && !selectedArea.empty()) {
			auto selected = std::find_if(areas.begin(), areas.end(), [&](const Area& a) { return a.name == selectedArea; });
			if (selected != areas.end()) { scan.xMm = selected->x; scan.yMm = selected->y; scan.widthMm = selected->width; scan.heightMm = selected->height; } else selectedArea.clear();
		}
		SaveAreas(); PersistArea(); SyncSettings();
	}
	void BrightnessDialog() { if (scan.source.empty()) { ChooseSource(); return; } std::vector<Field> f{{L"Dauerhafte Helligkeitsvorwahl für " + scan.source, GetIni(settings, SourceKey().c_str(), L"Brightness", L"0")}}; if (Form(window, L"Gerätespezifische Helligkeit", f)) { auto value = Decimal(f[0].value, -1000, 1000); SetIni(settings, SourceKey().c_str(), L"Brightness", std::to_wstring(value)); scan.brightness = value; SendMessageW(window, WM_SIZE, 0, 0); } }
	void ScanNow(bool driver) {
		if (scan.source.empty()) { ChooseSource(); if (scan.source.empty()) return; } ScanOptions options = scan; if (driver) options.driverDialog = true;
		if (!sourcesReady) { SetWindowTextW(status, L"Scanner werden noch gesucht."); return; }
		if (std::none_of(sourceCache.connections.begin(), sourceCache.connections.end(), [&](const Connection& c) { return Selected(c); })) { ChooseSource(); return; }
		SetWindowTextW(status, wia ? L"WIA arbeitet …" : L"TWAIN arbeitet … Escape bricht ab.");
		auto result = wia ? WiaScan(window, deviceId, options) : TwainScan(window, options);
		AddImages(std::move(result.pages));
		if (!result.completed || result.cancelled) {
			std::wstring message = WideError(result.error.c_str());
			SetWindowTextW(status, L"Scan nicht erfolgreich abgeschlossen...");
			MessageBoxW(window, message.c_str(), L"Scanner – Scan nicht abgeschlossen", MB_OK | MB_ICONWARNING); return;
		}
	}
	struct CopyReceipt { std::wstring printer; size_t pages = 0; int jobId = 0; };
	std::function<std::wstring()> copyPrinter = DefaultPrinter;
	std::function<ScanResult(HWND, const ScanOptions&)> copyTwainScan = TwainScan;
	std::function<bool(HWND, const std::vector<Image>&, const PrintOptions&)> copyPrint = PrintImages;
	CopyReceipt CopyNow() {
		if (copyBusy) throw std::runtime_error("Ein Kopierauftrag wird bereits ausgefuehrt.");
		copyBusy = true; const bool enabled = IsWindowEnabled(window) != FALSE; EnableWindow(window, FALSE);
		struct Reset { App& app; bool enabled; ~Reset() { app.copyBusy = false; EnableWindow(app.window, enabled); app.UpdateToolbar(); } } reset{*this, enabled};
		try {
			if (scan.source.empty()) throw std::runtime_error("Bitte vor dem Kopieren einen Scanner auswaehlen.");
			if (!sourcesReady) throw std::runtime_error("Scanner werden noch gesucht. Bitte den Kopierauftrag danach erneut starten.");
			if (std::none_of(sourceCache.connections.begin(), sourceCache.connections.end(), [&](const Connection& c) { return Selected(c); }))
				throw std::runtime_error("Der ausgewaehlte Scanner ist nicht verfuegbar. Bitte Scannerverbindung und Treiber pruefen.");
			CopyReceipt receipt; receipt.printer = copyPrinter();
			if (receipt.printer.empty()) throw std::runtime_error("Windows meldet keinen Standarddrucker.");
			PrintOptions print{copyCount, printCollate, printFit, printRotate}; print.printer = receipt.printer; print.jobId = &receipt.jobId;
			ScanOptions options = scan; options.driverDialog = false;
			SetWindowTextW(status, L"Kopieren: Scan läuft … Escape bricht ab.");
			auto result = wia ? WiaScan(window, deviceId, options) : copyTwainScan(window, options);
			if (!result.completed || result.cancelled) throw std::runtime_error(result.error.empty() ? "Kopierscan abgebrochen oder unvollstaendig; es wurde nichts gedruckt." : result.error);
			if (result.pages.empty()) throw std::runtime_error("Kopierscan lieferte keine Seiten; es wurde nichts gedruckt.");
			receipt.pages = result.pages.size();
			SetWindowTextW(status, (L"Kopieren: Druckauftrag an " + receipt.printer + L" …").c_str());
			if (!copyPrint(window, result.pages, print)) throw std::runtime_error("Kopierdruck wurde abgebrochen.");
			SetWindowTextW(status, (L"Kopieren: " + std::to_wstring(receipt.pages) + L" Seite(n) an " + receipt.printer + L" übergeben.").c_str());
			return receipt;
		} catch (...) { SetWindowTextW(status, L"Kopieren nicht abgeschlossen."); throw; }
	}
	bool ExportDialog() {
		NativeSaveState state; state.settingsOnly = true; state.options = exportOptions; state.together = exportOptions.multipage; state.jpegQuality = jpegQuality; state.pdfQuality = pdfQuality; state.disk = toolbarImages[1];
		ApplySaveDialogDefaults(state);
		if (!SaveModal(window, state, NativeSaveProc, L"Export-Einstellungen", 410, 472)) return false;
		exportOptions = state.options; jpegQuality = state.jpegQuality; pdfQuality = state.pdfQuality; SaveExport(); return true;
	}
	bool ChooseSubset(const wchar_t* title) const { if (actionSelectionOnly) return true; if (document.selection.empty()) return false; std::vector<Field> fields{{L"Seiten", L"Markierte Seiten", {L"Alle Seiten", L"Markierte Seiten"}}}; if (!Form(window, title, fields)) throw std::runtime_error("Vorgang abgebrochen."); return fields[0].value == L"Markierte Seiten"; }
	void ExecuteSelection(int command) { actionSelectionOnly = true; try { Execute(command); } catch (...) { actionSelectionOnly = false; throw; } actionSelectionOnly = false; }
	void NameAutomation(HWND owner) {
		NameAutomationState state; state.mode = nameMode;
		if (SaveModal(owner, state, NameAutomationProc, L"Dateinamenautomatik", 295, 155)) { nameMode = state.mode; Set(L"NameMode", std::to_wstring(nameMode)); }
	}
	void SaveNow(bool mail) {
		if (!mail) {
			NativeSaveState state; state.options = exportOptions; state.together = exportOptions.multipage; state.jpegQuality = jpegQuality; state.pdfQuality = pdfQuality; state.pages = document.pages.size(); state.selectedPages = document.selection.size(); state.disk = toolbarImages[1];
			ApplySaveDialogDefaults(state);
			state.folder = lastName.empty() ? data : std::filesystem::path(lastName).parent_path();
			const int mode = nameMode; auto initial = SaveAutomaticPath(mode, lastName, state.folder, state.options, state.pages); if (!initial.empty()) { state.folder = initial.parent_path(); state.filename = initial.filename().wstring(); }
			state.collisionSafe = mode == 4 || mode == 5; state.automation = [this](HWND owner) { NameAutomation(owner); };
			if (mode == 1 || mode == 2) state.initialize = [&state, mode](HWND owner) { return mode == 1 ? SavePickFile(owner, state) : SavePickFolder(owner); };
			if (!SaveModal(window, state, NativeSaveProc, L"Speichern", 410, 472)) return;
			exportOptions = state.options; jpegQuality = state.jpegQuality; pdfQuality = state.pdfQuality; SaveExport(); lastName = state.path.wstring(); Set(L"LastNameUtf16", EncodeSettingText(lastName));
			auto images = document.Images(state.selectedOnly); if (images.empty()) throw std::runtime_error("Keine Seiten vorhanden.");
			auto files = ExportImages(images, state.path, state.options);
			if (openAfter && !files.empty() && reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", files[0].c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("Die Dateien wurden gespeichert, aber die erste Datei konnte nicht mit dem zugeordneten Programm geoeffnet werden."); return;
		}
		NativeSaveState state; state.mailMode = true; state.options = exportOptions; state.together = exportOptions.multipage; state.jpegQuality = jpegQuality; state.pdfQuality = pdfQuality; state.pages = document.pages.size(); state.selectedPages = document.selection.size(); state.filename = L"Anlage"; state.disk = toolbarImages[1];
		ApplySaveDialogDefaults(state);
		if (!SaveModal(window, state, NativeSaveProc, L"Als E-Mail versenden", 410, 222)) return;
		exportOptions = state.options; jpegQuality = state.jpegQuality; pdfQuality = state.pdfQuality; SaveExport();
		auto images = document.Images(state.selectedOnly); if (images.empty()) throw std::runtime_error("Keine Seiten fuer den E-Mail-Anhang ausgewaehlt.");
		auto root = data / L"Mail"; std::filesystem::create_directories(root); auto dir = root / NewId();
		if (!std::filesystem::create_directory(dir)) throw std::runtime_error("Ein temporaerer Ordner fuer den E-Mail-Anhang konnte nicht angelegt werden.");
		try { auto files = ExportImages(images, dir / state.path.filename(), state.options); PrepareMail(window, files); }
		catch (...) { std::error_code ec; std::filesystem::remove_all(dir, ec); throw; }
		std::error_code ec; std::filesystem::remove_all(dir, ec);
	}
	void PrintPages(const std::vector<Image>& images, const std::vector<Image>& selected) {
		if (images.empty()) throw std::runtime_error("Keine Seiten zum Drucken.");
		if (actionSelectionOnly && selected.empty()) throw std::runtime_error("Keine markierten Seiten zum Drucken.");
		NativePrintState state; state.options = PrintOptions{copyCount, printCollate, printFit, printRotate}; state.options.printer = printPrinter; state.options.deviceMode = printDeviceMode; state.outputToFile = printToFile; state.printers = PrintPrinters(); state.hasSelection = !selected.empty(); state.selectedOnly = actionSelectionOnly; state.icon = toolbarImages[2];
		if (state.printers.empty()) { MessageBoxW(window, L"Es sind keine Drucker installiert. Bitte zuerst einen Drucker in Windows einrichten.", L"Drucken", MB_OK | MB_ICONWARNING); return; }
		if (!SaveModal(window, state, NativePrintProc, L"Drucken", 385, 294)) return;
		copyCount = state.options.copies; printFit = state.options.fit; printRotate = state.options.rotate; printCollate = state.options.collate; printToFile = state.outputToFile; printPrinter = state.options.printer; printDeviceMode = state.options.deviceMode; SavePrintSettings();
		PrintImages(window, state.selectedOnly ? selected : images, state.options);
	}
	void PrintNow() { PrintPages(document.Images(false), document.selection.empty() ? std::vector<Image>{} : document.Images(true)); }
	void OptionsDialog() {
		OptionsState state; state.openAfter = openAfter; state.cropDoubleClick = cropDoubleClick; state.closeMode = closeMode;
		if (!SaveModal(window, state, OptionsProc, L"Optionen", 340, 240)) return;
		Set(L"OpenAfter", state.openAfter ? L"1" : L"0"); Set(L"CropDoubleClick", state.cropDoubleClick ? L"1" : L"0"); Set(L"CloseMode", std::to_wstring(state.closeMode));
		openAfter = state.openAfter; cropDoubleClick = state.cropDoubleClick; closeMode = state.closeMode;
	}
	void EventSettings(const std::wstring& eventId = L"Default") {
		if (eventId == L"Default") {
			std::vector<wchar_t> sections(65536); DWORD length = GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), settings.c_str());
			if (length >= sections.size() - 2) throw std::runtime_error("Zu viele gespeicherte Ereignisbelegungen.");
			std::vector<std::wstring> ids{L"Neue Belegung"};
			std::vector<DeviceEvent> available; std::wstring availability;
			try { available = EnumerateDeviceEvents(); } catch (const std::exception& e) { availability = WideError(e.what()); }
			for (const auto& event : available) if (std::find(ids.begin(), ids.end(), event.eventId) == ids.end()) ids.push_back(event.eventId);
			for (const wchar_t* name = sections.data(); *name; name += wcslen(name) + 1) if (std::wstring(name).rfind(L"Event-", 0) == 0) { auto id = GetIni(settings, name, L"Id", L""); if (!id.empty()) ids.push_back(id); }
			std::vector<Field> listFields{{L"Belegungen und verfügbare Windows-Ereignisse", ids[0], ids}, {L"Windows-Zuordnung", L"Unverändert", {L"Unverändert", L"Registrieren", L"Entfernen"}}, {L"Treiberstatus (nur Information)", availability.empty() ? L"Ereignisse verfügbar" : availability}};
			if (!Form(window, L"Geräteereignis-Belegungen verwalten", listFields)) return;
			if (listFields[1].value != L"Unverändert") { bool matched = false; for (const auto& event : available) if (event.eventId == listFields[0].value) { RegisterDeviceEvent(event.deviceId, event.eventId, listFields[1].value == L"Entfernen"); matched = true; } if (!matched) throw std::runtime_error("Keine passende Windows-Geraetezuordnung verfuegbar."); }
			EventSettings(listFields[0].value == L"Neue Belegung" ? L"Neue-Ereignis-ID" : listFields[0].value); return;
		}
		std::wstring section = L"Event-" + DeviceKey(eventId); std::vector<std::wstring> actions{L"Ignorieren", L"Starten", L"Scannen", L"Kopieren"};
		std::vector<Field> f{{L"Ereignis-ID (Windows /StiEvent:)", eventId}, {L"Aktion", GetIni(settings, section.c_str(), L"Action", L"Ignorieren"), actions}, {L"Auflösung DPI", GetIni(settings, section.c_str(), L"Dpi", L"300")}, {L"Scanmodus", GetIni(settings, section.c_str(), L"Mode", L"Farbe"), {L"Schwarzweiß", L"Graustufen", L"Farbe"}}, {L"Breite mm", GetIni(settings, section.c_str(), L"Width", L"210")}, {L"Höhe mm", GetIni(settings, section.c_str(), L"Height", L"297")}, {L"Einzug", GetIni(settings, section.c_str(), L"Feeder", L"Nein"), YesNo}, {L"Belegung löschen", L"Nein", YesNo}};
		if (!Form(window, L"Gerätetasten – Windows-Geräteereignis dieser Anwendung zuordnen", f)) return;
		Decimal(f[2].value, 25, 9600); Decimal(f[4].value, 1, 2000); Decimal(f[5].value, 1, 2000); section = L"Event-" + DeviceKey(f[0].value);
		if (f[0].value.empty() || f[0].value == L"Neue-Ereignis-ID") throw std::runtime_error("Eine tatsaechliche Windows-Ereignis-ID ist erforderlich.");
		if (f[0].value != eventId) WritePrivateProfileStringW((L"Event-" + DeviceKey(eventId)).c_str(), nullptr, nullptr, settings.c_str());
		if (IsYes(f[7])) { WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, settings.c_str()); return; }
		const wchar_t* keys[]{L"Id", L"Action", L"Dpi", L"Mode", L"Width", L"Height", L"Feeder"}; for (int i = 0; i < 7; ++i) SetIni(settings, section.c_str(), keys[i], f[i].value);
		MessageBoxW(window, L"Windows muss das Scannerereignis an Scanner übergeben. Neu: Programm beenden, Gerätetaste betätigen und die Windows-Zuordnung wählen. Treiber ohne Still-Image-Ereignisse werden nicht simuliert.", L"Gerätetasten", MB_OK);
	}
	void HandleEvent(const std::wstring& id) {
		std::wstring section = L"Event-" + DeviceKey(id), action = GetIni(settings, section.c_str(), L"Action", L""); if (action.empty()) { EventSettings(id); return; }
		if (action == L"Ignorieren" || action == L"Starten") return;
		scan.dpi = Decimal(GetIni(settings, section.c_str(), L"Dpi", L"300"), 25, 9600); auto mode = GetIni(settings, section.c_str(), L"Mode", L"Farbe"); scan.mode = mode == L"Farbe" ? 2 : mode == L"Graustufen" ? 1 : 0; scan.widthMm = Decimal(GetIni(settings, section.c_str(), L"Width", L"210"), 1, 2000); scan.heightMm = Decimal(GetIni(settings, section.c_str(), L"Height", L"297"), 1, 2000); scan.feeder = GetIni(settings, section.c_str(), L"Feeder", L"Nein") == L"Ja"; scan.driverDialog = false;
		if (action == L"Kopieren") CopyNow(); else ScanNow(false);
	}
	void ShowHelp() const {
		auto topics = ReadHelpTopics(HelpFilePath()); std::vector<HelpSection> sections; sections.reserve(topics.size());
		for (const auto& topic : topics) sections.push_back({topic.heading.c_str(), topic.text.c_str()});
		ShowDialogHelp(window, L"Scanner – Hilfethemen", std::move(sections), true);
	}
	void ToggleFullScreen() { ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE); fullScreen = IsZoomed(window) != FALSE; }
	bool Close(int response = 0) {
		bool clear = closeMode == 2;
		if (closeMode == 1 && !document.pages.empty()) {
			if (!response) response = MessageBoxW(window, L"Soll die Seitenübersicht geleert werden?", L"Programm beenden", MB_YESNOCANCEL | MB_ICONQUESTION);
			if (response == IDCANCEL || response == 0) return false;
			if (response != IDYES && response != IDNO) throw std::runtime_error("Ungueltige Antwort beim Beenden.");
			clear = response == IDYES;
		}
		if (clear || document.pages.empty()) ClearWorkspace(workspace); else SaveWorkspace(document, workspace);
		document = {}; if (window) Refresh(); return true;
	}
	void Execute(int command) {
		if (command >= SourceBase) {
			const size_t sourceIndex = static_cast<size_t>(static_cast<unsigned int>(command) - static_cast<unsigned int>(SourceBase));
			if (sourceIndex < sourceCache.connections.size()) { SelectConnection(sourceIndex); return; }
		}
		CaptureSelection();
		switch (command) {
		case ModeMono: case ModeGray: case ModeColor: case DriverScan: scan.driverDialog = command == DriverScan; if (!scan.driverDialog) scan.mode = command - ModeMono; Set(L"Mode", std::to_wstring(scan.mode)); Set(L"DriverDialog", scan.driverDialog ? L"1" : L"0"); SendMessageW(window, WM_SIZE, 0, 0); Refresh(); break;
		case SmallSymbols: SetView(4); break;
		case Import: ImportDialog(); break; case Save: SaveNow(false); break; case Mail: SaveNow(true); break; case Print: PrintNow(); break; case Scan: ScanNow(false); break; case SourceArrow: case SelectSource: ChooseSource(); break; case ScanSettings: ScanDialog(); SendMessageW(window, WM_SIZE, 0, 0); break; case Areas: ManageAreas(); SendMessageW(window, WM_SIZE, 0, 0); break; case Brightness: BrightnessDialog(); break;
		case ExportSettings: ExportDialog(); break; case ResetExport: exportOptions = {}; jpegQuality = pdfQuality = exportOptions.quality = 80; SaveExport(); break; case Options: OptionsDialog(); break; case CopyMode: CopyNow(); break;
		case DeviceEvents: EventSettings(); break; case DeviceRegister: RegisterDeviceApplication(); MessageBoxW(window, L"Anwendung registriert. Die Zuordnung zu einer Gerätetaste erfolgt in den Windows-Geräteereignissen, sofern der Treiber sie unterstützt.", L"Still Image", MB_OK); break;
		case Cut: case Copy: if (!document.selection.empty()) { auto i = document.selection.front(); if (!CopyWindowsClipboard(window, document.pages[i].image)) throw std::runtime_error("Windows-Zwischenablage konnte nicht aktualisiert werden."); document.CopySelected(command == Cut); clipboardSequence = GetClipboardSequenceNumber(); Refresh(command == Cut); } break;
		case Paste: { size_t before = document.selection.empty() ? document.pages.size() : document.selection.front(); if (document.clipboard.empty() || clipboardSequence != GetClipboardSequenceNumber()) { auto image = ReadWindowsClipboard(window); document.clipboard = {{NewId(), std::move(image)}}; clipboardSequence = GetClipboardSequenceNumber(); } document.Paste(before); Refresh(true); break; }
		case Delete: document.EraseSelected(); Refresh(true); break;
		case RotateL: case RotateR: case RotateHalf: case Invert: case MirrorH: case MirrorV: document.EditSelected(command == RotateL ? Edit::RotateLeft : command == RotateR ? Edit::RotateRight : command == RotateHalf ? Edit::Rotate180 : command == Invert ? Edit::Invert : command == MirrorH ? Edit::MirrorHorizontal : Edit::MirrorVertical); Refresh(true); break;
		case CropPage: if (!document.selection.empty()) { Image edited = document.pages[Current()].image; if (CropDialog(window, edited)) { document.pages[Current()].Replace(std::move(edited)); Refresh(true); } } break;
		case UndoEdit: document.UndoSelected(); Refresh(true); break; case RestoreOriginal: document.RestoreOriginals(); Refresh(true); break;
		case CombinedView: case SymbolsView: SetView(3); break; case PageView: SetView(1); break;
		case Reverse: document.Reverse(); Refresh(true); break;
		case SelectAll: document.SelectAll(); Refresh(); break;
		case ZoomIn: SetView(viewMode - 1); break; case ZoomOut: SetView(viewMode + 1); break; case Fit: SetView(1); break;
		case Width: SetView(0); break; case TwoPages: SetView(2); break; case FullScreen: ToggleFullScreen(); break;
		case BackgroundColor: { static COLORREF colors[16]{}; CHOOSECOLORW chooser{}; chooser.lStructSize = sizeof(chooser); chooser.hwndOwner = window; chooser.rgbResult = background; chooser.lpCustColors = colors; chooser.Flags = CC_FULLOPEN | CC_RGBINIT; if (ChooseColorW(&chooser)) { background = chooser.rgbResult; Set(L"Background", std::to_wstring(background)); InvalidateRect(window, nullptr, TRUE); } break; }
		case Clear: if (MessageBoxW(window, L"Alle Seiten aus dem Arbeitsbereich entfernen?", L"Seitenübersicht leeren", MB_YESNO | MB_ICONQUESTION) == IDYES) { document.pages.clear(); document.selection.clear(); Refresh(true); } break;
		case Help: ShowHelp(); break; case About: if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_ABOUTBOX), window, AboutProc, 0) == -1) throw std::runtime_error("Der Info-Dialog konnte nicht geoeffnet werden."); break; case Exit: SendMessageW(window, WM_CLOSE, 0, 0); break;
		}
	}
	DWORD clipboardSequence = 0;
};
static void MenuItem(HMENU menu, int id, const wchar_t* text) { AppendMenuW(menu, MF_STRING, id, text); }
static HMENU BuildMenu(bool compactEdit = false) {
	HMENU root = CreateMenu(); auto submenu = [&](const wchar_t* title) { HMENU m = CreatePopupMenu(); AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(m), title); return m; };
	auto action = submenu(L"&Aktion"); MenuItem(action, Clear, L"Neu"); MenuItem(action, Import, L"Importieren"); MenuItem(action, Scan, L"Scannen"); MenuItem(action, Save, L"Speichern"); MenuItem(action, Print, L"Drucken"); MenuItem(action, Mail, L"Als E-Mail versenden"); MenuItem(action, CopyMode, L"Kopieren"); MenuItem(action, Exit, L"Schließen");
	auto edit = submenu(L"&Bearbeiten"); MenuItem(edit, Cut, L"Ausschneiden"); MenuItem(edit, Copy, L"Kopieren"); MenuItem(edit, Paste, L"Einfügen"); MenuItem(edit, Delete, L"Löschen"); AppendMenuW(edit, MF_SEPARATOR, 0, nullptr); MenuItem(edit, RotateL, L"Drehen nach links"); MenuItem(edit, RotateHalf, L"Drehen um 180°"); MenuItem(edit, RotateR, L"Drehen nach rechts"); AppendMenuW(edit, MF_SEPARATOR, 0, nullptr); MenuItem(edit, Invert, L"Invertieren"); MenuItem(edit, MirrorH, L"Spiegeln"); MenuItem(edit, CropPage, L"Zuschneiden");
	if (!compactEdit) { AppendMenuW(edit, MF_SEPARATOR, 0, nullptr); MenuItem(edit, Reverse, L"Sortierung umkehren"); MenuItem(edit, SelectAll, L"Alle auswählen"); }
	auto view = submenu(L"&Ansicht"); MenuItem(view, Width, L"Optimale Breite"); MenuItem(view, Fit, L"Ganze Seite"); MenuItem(view, TwoPages, L"Zwei Seiten"); MenuItem(view, CombinedView, L"Symbole"); MenuItem(view, SmallSymbols, L"Kleine Symbole"); MenuItem(view, ZoomIn, L"Vergrößern"); MenuItem(view, ZoomOut, L"Verkleinern");
	auto extra = submenu(L"E&xtras"); MenuItem(extra, Options, L"Optionen"); HMENU scanner = CreatePopupMenu(); AppendMenuW(extra, MF_POPUP, reinterpret_cast<UINT_PTR>(scanner), L"Scanner"); AppendMenuW(scanner, MF_STRING | MF_GRAYED, 0, L"Scanner werden gesucht …"); MenuItem(extra, BackgroundColor, L"Hintergrundfarbe ändern");
	auto help = submenu(L"?"); MenuItem(help, Help, L"Hilfethemen"); MenuItem(help, About, L"Info"); return root;
}
static WNDPROC originalListProc;
static POINT dragStart{}; static bool dragging = false, canDrag = false;
static bool selectingFrame = false;
static std::vector<size_t> frameBase;
static LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam) {
	auto app = reinterpret_cast<App*>(GetWindowLongPtrW(GetParent(list), GWLP_USERDATA));
	if (message == WM_LBUTTONDOWN) { dragStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; canDrag = HIWORD(SendMessageW(list, LB_ITEMFROMPOINT, 0, lParam)) == 0; dragging = false;
		RECT client{}; GetClientRect(list, &client);
		if (!canDrag || dragStart.x < 10 || dragStart.x > client.right - 10) { app->CaptureSelection(); frameBase = wParam & MK_CONTROL ? app->document.selection : std::vector<size_t>{}; selectingFrame = true; canDrag = false; if (!(wParam & MK_CONTROL)) SendMessageW(list, LB_SETSEL, FALSE, -1); SetCapture(list); return 0; }
		auto item = LOWORD(SendMessageW(list, LB_ITEMFROMPOINT, 0, lParam)); if (canDrag && !(GetKeyState(VK_CONTROL) & 0x8000) && SendMessageW(list, LB_GETSELCOUNT, 0, 0) > 1 && SendMessageW(list, LB_GETSEL, item, 0) > 0) { SetCapture(list); return 0; }
	}
	if (message == WM_MOUSEMOVE && selectingFrame) {
		RECT frame{std::min<LONG>(dragStart.x, GET_X_LPARAM(lParam)), std::min<LONG>(dragStart.y, GET_Y_LPARAM(lParam)), std::max<LONG>(dragStart.x, GET_X_LPARAM(lParam)), std::max<LONG>(dragStart.y, GET_Y_LPARAM(lParam))};
		SendMessageW(list, WM_SETREDRAW, FALSE, 0);
		for (size_t i = 0; i < app->document.pages.size(); ++i) { RECT item{}, intersection{}; SendMessageW(list, LB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&item)); bool selected = IntersectRect(&intersection, &frame, &item) || std::find(frameBase.begin(), frameBase.end(), i) != frameBase.end(); SendMessageW(list, LB_SETSEL, selected, i); }
		SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE); UpdateWindow(list); HDC dc = GetDC(list); DrawFocusRect(dc, &frame); ReleaseDC(list, dc); app->CaptureSelection(); InvalidateRect(app->window, nullptr, FALSE); return 0;
	}
	if (message == WM_LBUTTONUP && selectingFrame) { selectingFrame = false; ReleaseCapture(); app->CaptureSelection(); InvalidateRect(list, nullptr, TRUE); return 0; }
	if (message == WM_CAPTURECHANGED) { selectingFrame = false; dragging = false; canDrag = false; }
	if (message == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL)) { app->Execute(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? ZoomIn : ZoomOut); return 0; }
	if (message == WM_MOUSEMOVE && canDrag && (wParam & MK_LBUTTON) && (std::abs(GET_X_LPARAM(lParam) - dragStart.x) > 6 || std::abs(GET_Y_LPARAM(lParam) - dragStart.y) > 6)) { dragging = true; SetCapture(list); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return 0; }
	if (message == WM_LBUTTONUP) {
		canDrag = false; if (dragging) { dragging = false; ReleaseCapture(); app->CaptureSelection(); POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ClientToScreen(list, &screen);
			try { for (int i = 2; i <= 4; ++i) { RECT button{}; GetWindowRect(app->buttons[i], &button); if (PtInRect(&button, screen)) { app->ExecuteSelection(i == 2 ? Save : i == 3 ? Print : Mail); return 0; } }
				POINT target = screen; ScreenToClient(list, &target); RECT client{}; GetClientRect(list, &client); if (PtInRect(&client, target)) { auto item = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(target.x, target.y)); app->document.MoveSelected(HIWORD(item) ? app->document.pages.size() : LOWORD(item)); app->Refresh(true); }
			} catch (const std::exception& e) { Error(app->window, e); } return 0;
		}
	}
	if (message == WM_LBUTTONDBLCLK || message == WM_MBUTTONDBLCLK) { try { app->CaptureSelection(); int command = DoubleClickCommand(app->cropDoubleClick, message == WM_MBUTTONDBLCLK, (wParam & MK_CONTROL) != 0); if (command) app->Execute(command); } catch (const std::exception& e) { Error(app->window, e); } return 0; }
	if (message == WM_KEYDOWN && wParam == VK_DOWN && !(GetKeyState(VK_SHIFT) & 0x8000) && !app->document.pages.empty() && SendMessageW(list, LB_GETCARETINDEX, 0, 0) == static_cast<LRESULT>(app->document.pages.size() - 1)) { SendMessageW(list, LB_SETSEL, FALSE, -1); app->CaptureSelection(); app->Refresh(); return 0; }
	return CallWindowProcW(originalListProc, list, message, wParam, lParam);
}
static HBITMAP ToolbarBitmap(HWND window, const wchar_t* name, int index) {
	using Microsoft::WRL::ComPtr;
	HBITMAP bitmap = nullptr;
	HRSRC resource = FindResourceW(instance, name, RT_RCDATA);
	if (resource) {
		auto loaded = LoadResource(instance, resource); auto bytes = loaded ? static_cast<BYTE*>(LockResource(loaded)) : nullptr; DWORD length = SizeofResource(instance, resource);
		ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
		UINT width = 0, height = 0;
		if (bytes && length && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) &&
			SUCCEEDED(factory->CreateStream(stream.GetAddressOf())) && SUCCEEDED(stream->InitializeFromMemory(bytes, length)) &&
			SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) &&
			SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())) && SUCCEEDED(frame->GetSize(&width, &height)) && width && height && width <= 4096 && height <= 4096 &&
			SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf())) &&
			SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
			BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -static_cast<LONG>(height); info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
			void* pixels = nullptr; bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
			if (bitmap && SUCCEEDED(converter->CopyPixels(nullptr, width * 4, width * height * 4, static_cast<BYTE*>(pixels)))) return bitmap;
			if (bitmap) { DeleteObject(bitmap); bitmap = nullptr; }
		}
	}
	HDC dc = GetDC(window), memory = CreateCompatibleDC(dc); int size = UiPx(window, 32); bitmap = CreateCompatibleBitmap(dc, size, size); auto old = SelectObject(memory, bitmap); RECT r{0, 0, size, size}; FillRect(memory, &r, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
	SetMapMode(memory, MM_ANISOTROPIC); SetWindowExtEx(memory, 32, 32, nullptr); SetViewportExtEx(memory, size, size, nullptr); HPEN pen = CreatePen(PS_SOLID, 2, RGB(42, 81, 135)); auto oldPen = SelectObject(memory, pen); auto oldBrush = SelectObject(memory, GetStockObject(HOLLOW_BRUSH));
	if (index >= 8) { Ellipse(memory, 3, 3, 23, 23); MoveToEx(memory, 21, 21, nullptr); LineTo(memory, 30, 30); MoveToEx(memory, 7, 13, nullptr); LineTo(memory, 19, 13); if (index == 8) { MoveToEx(memory, 13, 7, nullptr); LineTo(memory, 13, 19); } }
	else if (index == 0) { Rectangle(memory, 3, 16, 29, 27); MoveToEx(memory, 3, 16, nullptr); LineTo(memory, 9, 5); LineTo(memory, 27, 5); LineTo(memory, 29, 16); MoveToEx(memory, 8, 22, nullptr); LineTo(memory, 22, 22); }
	else if (index == 1) { Rectangle(memory, 5, 3, 27, 29); Rectangle(memory, 9, 3, 23, 12); Rectangle(memory, 9, 18, 23, 29); }
	else if (index == 2) { Rectangle(memory, 3, 12, 29, 24); Rectangle(memory, 8, 3, 24, 13); Rectangle(memory, 8, 19, 24, 29); }
	else if (index == 3) { Rectangle(memory, 3, 7, 29, 26); MoveToEx(memory, 3, 7, nullptr); LineTo(memory, 16, 18); LineTo(memory, 29, 7); }
	else if (index == 6) { for (int y = 4; y <= 18; y += 14) for (int x = 4; x <= 18; x += 14) Rectangle(memory, x, y, x + 10, y + 10); }
	else { Rectangle(memory, 7, 3, 26, 28); if (index == 4) { MoveToEx(memory, 11, 15, nullptr); LineTo(memory, 22, 15); MoveToEx(memory, 16, 9, nullptr); LineTo(memory, 16, 22); } else if (index == 5) { MoveToEx(memory, 4, 28, nullptr); LineTo(memory, 24, 8); } else Rectangle(memory, 3, 8, 21, 31); }
	SelectObject(memory, oldBrush); SelectObject(memory, oldPen); DeleteObject(pen); SelectObject(memory, old); DeleteDC(memory); ReleaseDC(window, dc); return bitmap;
}
static HBITMAP DisabledToolbarBitmap(HBITMAP source) {
	DIBSECTION section{}; if (!source || GetObjectW(source, sizeof(section), &section) != sizeof(section) || !section.dsBm.bmBits || section.dsBm.bmBitsPixel != 32) return nullptr;
	BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = section.dsBm.bmWidth; info.bmiHeader.biHeight = -section.dsBm.bmHeight; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
	void* pixels = nullptr; HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); if (!bitmap || !pixels) { if (bitmap) DeleteObject(bitmap); return nullptr; }
	const auto* input = static_cast<const BYTE*>(section.dsBm.bmBits); auto* output = static_cast<BYTE*>(pixels); const int stride = section.dsBm.bmWidthBytes;
	for (LONG y = 0; y < section.dsBm.bmHeight; ++y) for (LONG x = 0; x < section.dsBm.bmWidth; ++x) {
		const size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4; const BYTE alpha = input[offset + 3];
		const int gray = (29 * input[offset] + 150 * input[offset + 1] + 77 * input[offset + 2] + 128) >> 8; const BYTE muted = static_cast<BYTE>((3 * gray + alpha + 2) / 4);
		output[offset] = output[offset + 1] = output[offset + 2] = std::min(muted, alpha); output[offset + 3] = alpha;
	}
	return bitmap;
}
static void DrawToolbar(App& app, const DRAWITEMSTRUCT& item) {
	HDC dc = item.hDC; int saved = SaveDC(dc); if (app.uiFont) SelectObject(dc, app.uiFont); RECT r = item.rcItem; bool disabled = (item.itemState & ODS_DISABLED) != 0, hovered = !disabled && app.hoveredToolbarButton == item.hwndItem; ToolbarButtonBackground(dc, r, hovered, (item.itemState & ODS_SELECTED) != 0); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(25, 25, 25));
	if (item.CtlID == SourceArrow) { RECT edge = r; edge.right = edge.left + 1; FillRect(dc, &edge, GetSysColorBrush(COLOR_3DSHADOW)); int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, half = UiPx(app.window, 5), height = UiPx(app.window, 6); POINT triangle[]{{cx - half, cy - height / 2}, {cx + half, cy - height / 2}, {cx, cy + height / 2}}; SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN)); SetDCBrushColor(dc, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(25, 25, 25)); Polygon(dc, triangle, 3); if (item.itemState & (ODS_SELECTED | ODS_FOCUS)) { InflateRect(&r, -2, -2); DrawFocusRect(dc, &r); } RestoreDC(dc, saved); return; }
	if (item.CtlID == PanelMenu) {
		if (item.CtlID == PanelMenu) FillRect(dc, &r, SidebarBrush());
		COLORREF color = disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(0, 70, 180); SetTextColor(dc, color);
		wchar_t text[80]{}; GetWindowTextW(item.hwndItem, text, 80); DrawTextW(dc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | (item.CtlID == PanelMenu ? DT_LEFT : DT_RIGHT));
		if (item.CtlID == PanelMenu) {
			SIZE extent{}; GetTextExtentPoint32W(dc, text, lstrlenW(text), &extent);
			int left = r.left + extent.cx + UiPx(app.window, 4), cy = (r.top + r.bottom) / 2, width = UiPx(app.window, 10), height = UiPx(app.window, 6);
			POINT triangle[]{{left, cy - height / 2}, {left + width, cy - height / 2}, {left + width / 2, cy + height / 2}};
			SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN)); SetDCBrushColor(dc, color); Polygon(dc, triangle, 3);
		}
		RestoreDC(dc, saved); return;
	}
	int index = item.CtlID == Scan ? 0 : item.CtlID == Save ? 1 : item.CtlID == Print ? 2 : item.CtlID == Mail ? 3 : item.CtlID == Clear ? 4 : item.CtlID == EditMenu ? 5 : item.CtlID == ViewMenu ? 6 : item.CtlID == CopyMode ? 7 : item.CtlID == ZoomIn ? 8 : 9;
	bool narrow = index >= 8; int size = UiPx(app.window, narrow ? 24 : 30), x = (r.right - size) / 2, y = narrow ? (r.bottom - size) / 2 : UiPx(app.window, 5);
	if (item.itemState & ODS_SELECTED) { FrameRect(dc, &r, GetSysColorBrush(COLOR_3DSHADOW)); ++x; ++y; }
	if (app.toolbarImages[index]) {
		HBITMAP image = disabled && app.disabledToolbarImages[index] ? app.disabledToolbarImages[index] : app.toolbarImages[index]; bool muted = disabled && app.disabledToolbarImages[index];
		HDC source = CreateCompatibleDC(dc), scaled = CreateCompatibleDC(dc); HBITMAP resized = CreateCompatibleBitmap(dc, size, size); auto oldSource = SelectObject(source, image), oldScaled = SelectObject(scaled, resized); BITMAP b{}; GetObjectW(image, sizeof(b), &b); BitBlt(scaled, 0, 0, size, size, dc, x, y, SRCCOPY); if (b.bmBitsPixel == 32) { BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(scaled, 0, 0, size, size, source, 0, 0, b.bmWidth, b.bmHeight, blend); } else { SetStretchBltMode(scaled, HALFTONE); SetBrushOrgEx(scaled, 0, 0, nullptr); StretchBlt(scaled, 0, 0, size, size, source, 0, 0, b.bmWidth, b.bmHeight, SRCCOPY); } SelectObject(scaled, oldScaled);
		DrawStateW(dc, nullptr, nullptr, reinterpret_cast<LPARAM>(resized), 0, x, y, size, size, DST_BITMAP | (disabled && !muted ? DSS_DISABLED : DSS_NORMAL)); SelectObject(source, oldSource); DeleteObject(resized); DeleteDC(scaled); DeleteDC(source);
	}
	if (!narrow) { wchar_t text[80]{}; GetWindowTextW(item.hwndItem, text, 80); r.top = UiPx(app.window, 38); bool dropdown = item.CtlID == EditMenu || item.CtlID == ViewMenu; if (dropdown) r.right -= UiPx(app.window, 20); DrawTextW(dc, text, -1, &r, DT_CENTER | DT_SINGLELINE); if (dropdown) { int cx = item.rcItem.right - UiPx(app.window, 10), cy = UiPx(app.window, 45), half = UiPx(app.window, 5), height = UiPx(app.window, 6); POINT triangle[]{{cx - half, cy - height / 2}, {cx + half, cy - height / 2}, {cx, cy + height / 2}}; SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN)); SetDCBrushColor(dc, disabled ? GetSysColor(COLOR_GRAYTEXT) : RGB(25, 25, 25)); Polygon(dc, triangle, 3); } }
	if (item.itemState & ODS_FOCUS) { InflateRect(&r, -2, -2); DrawFocusRect(dc, &r); }
	RestoreDC(dc, saved);
}
static void Layout(App& app) {
	RECT r{}; GetClientRect(app.window, &r); auto px = [&](int v) { return UiPx(app.window, v); }; int x = 0;
	if (app.fontDpi != px(96)) { HFONT font = CreateFontW(-px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma"); if (font) { HFONT previous = app.uiFont; app.uiFont = font; app.fontDpi = px(96); for (auto button : app.buttons) if (button) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); for (auto control : {app.list, app.status, app.scanLabel, app.panelCombo, app.zoomIn, app.zoomOut}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); if (previous) DeleteObject(previous); } }
	int buttonWidth = px(96); HDC measure = GetDC(app.window); auto previousFont = SelectObject(measure, app.uiFont); for (int i = 0; i < 9; ++i) if (i != 1) { wchar_t text[80]{}; GetWindowTextW(app.buttons[i], text, 80); SIZE size{}; GetTextExtentPoint32W(measure, text, static_cast<int>(wcslen(text)), &size); buttonWidth = std::max(buttonWidth, static_cast<int>(size.cx) + px(i == 6 || i == 7 ? 28 : 16)); } SelectObject(measure, previousFont); ReleaseDC(app.window, measure);
	for (int i = 0; i < 8; ++i) if (i != 1) { MoveWindow(app.buttons[i], x, 0, buttonWidth, px(60), TRUE); x += buttonWidth; }
	MoveWindow(app.buttons[0], 0, 0, buttonWidth - px(22), px(60), TRUE); MoveWindow(app.sourceArrow, buttonWidth - px(22), 0, px(22), px(60), TRUE);
	MoveWindow(app.zoomIn, x, 0, px(28), px(29), TRUE); MoveWindow(app.zoomOut, x, px(30), px(28), px(29), TRUE);
	MoveWindow(app.buttons[8], std::max<LONG>(x + px(28), r.right - buttonWidth), 0, buttonWidth, px(60), TRUE);
	MoveWindow(app.scanLabel, px(16), px(73), px(115), px(18), TRUE); MoveWindow(app.panelCombo, px(16), px(94), px(120), px(20), TRUE);
	app.SyncSettings(); bool settingsVisible = !app.scan.driverDialog;
	ShowWindow(app.scanLabel, SW_SHOW); ShowWindow(app.panelCombo, SW_SHOW);
	for (int i = 0; i < 4; ++i) { int y = 120 + i * 48; MoveWindow(app.settingLabels[i], px(8), px(y), px(126), px(18), TRUE); MoveWindow(app.settingControls[i], px(8), px(y + 18), px(124), px(i == 3 ? 26 : 240), TRUE); ShowWindow(app.settingLabels[i], settingsVisible ? SW_SHOW : SW_HIDE); ShowWindow(app.settingControls[i], settingsVisible ? SW_SHOW : SW_HIDE); if (app.uiFont) { SendMessageW(app.settingLabels[i], WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE); SendMessageW(app.settingControls[i], WM_SETFONT, reinterpret_cast<WPARAM>(app.uiFont), TRUE); } }
	ShowWindow(app.list, SW_HIDE); MoveWindow(app.list, 0, 0, 0, 0, FALSE);
	MoveWindow(app.status, px(139), std::max<LONG>(px(60), r.bottom - px(20)), std::max<LONG>(1, r.right - px(139)), px(20), TRUE); app.UpdateScroll(); InvalidateRect(app.window, nullptr, TRUE);
}
static HMENU BuildToolbarMenu(App& app, int command) {
	HMENU popup = CreatePopupMenu();
	if (command == PanelMenu) { DestroyMenu(popup); popup = app.ModeMenu(); }
	else { HMENU root = BuildMenu(command == EditMenu); HMENU source = GetSubMenu(root, command == EditMenu ? 1 : 2); for (int i = 0; i < GetMenuItemCount(source); ++i) { wchar_t text[128]{}; GetMenuStringW(source, i, text, 128, MF_BYPOSITION); UINT id = GetMenuItemID(source, i); if (id == 0) AppendMenuW(popup, MF_SEPARATOR, 0, nullptr); else MenuItem(popup, id, text); } DestroyMenu(root); }
	app.UpdateCommandMenu(popup);
	if (command == ViewMenu) CheckMenuRadioItem(popup, 0, 4, app.viewMode, MF_BYPOSITION);
	return popup;
}
static void ToolbarPopup(App& app, HWND anchor, int command) {
	HMENU popup = BuildToolbarMenu(app, command);
	RECT r{}; GetWindowRect(anchor, &r); int selected = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_LEFTALIGN, r.left, r.bottom, 0, app.window, nullptr); DestroyMenu(popup); if (selected) app.Execute(selected);
}
static LRESULT CALLBACK SymbolsProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
	auto app = reinterpret_cast<App*>(data);
	RECT r{}; GetClientRect(button, &r);
	if ((message == WM_LBUTTONUP && GetCapture() == button && (GET_Y_LPARAM(lParam) >= UiPx(button, 35) || GET_X_LPARAM(lParam) >= r.right - UiPx(button, 20)) && PtInRect(&r, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) || (message == WM_KEYDOWN && wParam == VK_DOWN)) {
		ReleaseCapture(); SendMessageW(button, BM_SETSTATE, FALSE, 0); try { ToolbarPopup(*app, button, ViewMenu); } catch (const std::exception& e) { Error(app->window, e); } return 0;
	}
	return DefSubclassProc(button, message, wParam, lParam);
}
static bool SaveUiScreenshot(App& app, const std::filesystem::path& path, bool* emptyWhite = nullptr) {
	RECT r{}; GetClientRect(app.window, &r); HDC dc = GetDC(app.window), memory = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom); auto old = SelectObject(memory, bitmap);
	bool painted = PrintWindow(app.window, memory, PW_CLIENTONLY) != FALSE;
	SelectObject(memory, old); BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = r.right; info.bmiHeader.biHeight = -r.bottom; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB; std::vector<BYTE> pixels(static_cast<size_t>(r.right) * r.bottom * 4);
	bool read = GetDIBits(memory, bitmap, 0, r.bottom, pixels.data(), &info, DIB_RGB_COLORS) == static_cast<UINT>(r.bottom);
	if (emptyWhite) { *emptyWhite = painted && read; RECT work = app.WorkRect(); for (int y = work.top; y < work.bottom && *emptyWhite; ++y) for (int x = work.left; x < work.right; ++x) { size_t offset = (static_cast<size_t>(y) * r.right + x) * 4; if (pixels[offset] != 255 || pixels[offset + 1] != 255 || pixels[offset + 2] != 255) { *emptyWhite = false; break; } } }
	BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER); header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size()); std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(&header), sizeof(header)); out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader)); out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size())); bool written = out.good(); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(app.window, dc); return painted && read && written;
}
static LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE) { app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app)); }
	try {
		switch (message) {
		case WM_CREATE: {
			SetMenu(window, BuildMenu()); const wchar_t* names[]{L"Scannen", L"", L"Speichern", L"Drucken", L"Mailen", L"Neu", L"Bearbeiten", L"Symbole", L"Kopieren"}; int ids[]{Scan, Import, Save, Print, Mail, Clear, EditMenu, ViewMenu, CopyMode};
			for (int i = 0; i < 9; ++i) if (i != 1) app->buttons[i] = Control(window, L"BUTTON", names[i], WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, ids[i]);
			SetWindowSubclass(app->buttons[7], SymbolsProc, 1, reinterpret_cast<DWORD_PTR>(app));
			const wchar_t* resources[]{L"TOOL_SCAN", L"TOOL_SAVE", L"TOOL_PRINT", L"TOOL_MAIL", L"TOOL_NEW", L"TOOL_EDIT", L"TOOL_SYMBOLS", L"TOOL_COPY", L"TOOL_ZOOMIN", L"TOOL_ZOOMOUT"};
			for (int i = 0; i < 10; ++i) { app->toolbarImages[i] = ToolbarBitmap(window, resources[i], i); app->disabledToolbarImages[i] = DisabledToolbarBitmap(app->toolbarImages[i]); }
			app->zoomIn = Control(window, L"BUTTON", L"Vergrößern", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, ZoomIn); app->zoomOut = Control(window, L"BUTTON", L"Verkleinern", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, ZoomOut);
			app->scanLabel = Control(window, L"STATIC", L"Scanmodus:", 0, 0, 0, 1, 1, 502); app->panelCombo = Control(window, L"BUTTON", L"Scanner-Menü", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, PanelMenu);
			app->sourceArrow = Control(window, L"BUTTON", L"Scanner auswählen", WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, SourceArrow);
			for (auto button : app->buttons) if (button) SetWindowSubclass(button, ToolbarHoverProc, 2, reinterpret_cast<DWORD_PTR>(&app->hoveredToolbarButton));
			for (auto button : {app->zoomIn, app->zoomOut, app->panelCombo, app->sourceArrow}) SetWindowSubclass(button, ToolbarHoverProc, 2, reinterpret_cast<DWORD_PTR>(&app->hoveredToolbarButton));
			const wchar_t* labels[]{L"Auflösung:", L"Scanbereich:", L"Papierzufuhr:", L"Helligkeit (normal):"};
			for (int i = 0; i < 4; ++i) { app->settingLabels[i] = Control(window, L"STATIC", labels[i], 0, 0, 0, 1, 1, 520 + i); app->settingControls[i] = Control(window, i == 3 ? TRACKBAR_CLASSW : L"COMBOBOX", L"", WS_TABSTOP | (i == 3 ? TBS_HORZ | TBS_NOTICKS : CBS_DROPDOWNLIST | WS_VSCROLL), 0, 0, 1, 1, DpiControl + i); }
			for (auto text : {L"Flachbett", L"Einzug", L"Einzug+Duplex"}) SendMessageW(app->settingControls[2], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
			SendMessageW(app->settingControls[3], TBM_SETRANGE, FALSE, MAKELPARAM(-1000, 1000)); SendMessageW(app->settingControls[3], TBM_SETTIC, 0, 0);
			app->tooltips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
			auto tip = [&](HWND control, const wchar_t* text) { TOOLINFOW info{sizeof(info)}; info.uFlags = TTF_IDISHWND | TTF_SUBCLASS; info.hwnd = window; info.uId = reinterpret_cast<UINT_PTR>(control); info.lpszText = const_cast<wchar_t*>(text); SendMessageW(app->tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info)); };
			for (int i = 0; i < 9; ++i) if (i != 1) tip(app->buttons[i], names[i]); tip(app->zoomIn, L"Vergrößern"); tip(app->zoomOut, L"Verkleinern"); tip(app->panelCombo, L"Scanmodus und Scanner-Menü");
			app->list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | LBS_EXTENDEDSEL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(500), instance, nullptr); SendMessageW(app->list, LB_SETITEMHEIGHT, 0, UiPx(window, 28));
			originalListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(app->list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc)));
			app->status = Control(window, L"STATIC", L"", SS_LEFT, 139, 590, 1000, 20, 501); DragAcceptFiles(window, TRUE); Layout(*app); app->Refresh(); return 0;
		}
		case WM_INITMENUPOPUP: { HMENU popup = reinterpret_cast<HMENU>(wParam); HMENU main = GetMenu(window); HMENU extra = main ? GetSubMenu(main, 3) : nullptr; if (extra && popup == GetSubMenu(extra, 1)) app->PopulateSourceMenu(popup); else app->UpdateCommandMenu(popup); break; }
		case WM_SIZE: if (app->list) Layout(*app); return 0;
		case WM_DPICHANGED: { auto r = reinterpret_cast<RECT*>(lParam); SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); Layout(*app); return 0; }
		case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {UiPx(window, 860), UiPx(window, 420)}; return 0;
		case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = UiPx(window, 28); return TRUE;
		case WM_CTLCOLORLISTBOX: SetBkColor(reinterpret_cast<HDC>(wParam), RGB(235, 239, 249)); return reinterpret_cast<LRESULT>(SidebarBrush());
		case WM_CTLCOLORSTATIC: { HDC dc = reinterpret_cast<HDC>(wParam); SetBkMode(dc, TRANSPARENT); int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam)); if (reinterpret_cast<HWND>(lParam) == app->scanLabel || (id >= 520 && id <= 523)) { SetBkColor(dc, RGB(235, 239, 249)); return reinterpret_cast<LRESULT>(SidebarBrush()); } SetBkColor(dc, RGB(245, 245, 247)); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_3DFACE)); }
		case WM_DRAWITEM: {
			auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); if (item->CtlType == ODT_BUTTON) { DrawToolbar(*app, *item); return TRUE; } if (item->CtlID != 500 || item->itemID >= app->document.pages.size()) return TRUE; HIGHCONTRASTW contrast{sizeof(contrast)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0); bool highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0; bool selected = (item->itemState & ODS_SELECTED) != 0; HBRUSH bg = CreateSolidBrush(highContrast ? GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW) : selected ? RGB(180, 210, 250) : RGB(235, 239, 249)); FillRect(item->hDC, &item->rcItem, bg); DeleteObject(bg); SetTextColor(item->hDC, GetSysColor(selected && highContrast ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT)); RECT bounds = item->rcItem; bounds.left += UiPx(window, 15); std::wstring label = L"Seite " + std::to_wstring(item->itemID + 1); SetBkMode(item->hDC, TRANSPARENT); DrawTextW(item->hDC, label.c_str(), -1, &bounds, DT_LEFT | DT_VCENTER | DT_SINGLELINE); if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem); return TRUE;
		}
		case WM_HSCROLL: if (reinterpret_cast<HWND>(lParam) == app->settingControls[3]) { app->scan.brightness = static_cast<double>(SendMessageW(app->settingControls[3], TBM_GETPOS, 0, 0)); SetIni(app->settings, app->SourceKey().c_str(), L"Brightness", std::to_wstring(app->scan.brightness)); return 0; } if (!lParam) { app->ScrollBar(SB_HORZ, LOWORD(wParam)); return 0; } break;
		case WM_VSCROLL: if (!lParam) { app->ScrollBar(SB_VERT, LOWORD(wParam)); return 0; } break;
		case WM_CONTEXTMENU: if (reinterpret_cast<HWND>(wParam) == app->buttons[0] || reinterpret_cast<HWND>(wParam) == app->sourceArrow) { app->ChooseSource(); return 0; } if (reinterpret_cast<HWND>(wParam) == app->panelCombo) { ToolbarPopup(*app, app->panelCombo, PanelMenu); return 0; } if (reinterpret_cast<HWND>(wParam) == app->buttons[7]) { ToolbarPopup(*app, app->buttons[7], ViewMenu); return 0; } break;
		case WM_COMMAND: if (HIWORD(wParam) == CBN_DROPDOWN && LOWORD(wParam) == AreaControl) { app->UpdateAreaCapabilities(); return 0; } if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) >= DpiControl && LOWORD(wParam) <= PaperControl) { app->SidebarSetting(LOWORD(wParam)); return 0; } if (LOWORD(wParam) == 500 && HIWORD(wParam) == LBN_SELCHANGE) { app->CaptureSelection(); app->Refresh(); return 0; } if (LOWORD(wParam) == SourceArrow) { app->ChooseSource(); return 0; } if (LOWORD(wParam) == ViewMenu) { app->Execute(CombinedView); return 0; } if (LOWORD(wParam) == PanelMenu || LOWORD(wParam) == EditMenu) { ToolbarPopup(*app, reinterpret_cast<HWND>(lParam), LOWORD(wParam)); return 0; } app->Execute(LOWORD(wParam)); return 0;
		case WM_COPYDATA: {
			auto data = reinterpret_cast<COPYDATASTRUCT*>(lParam);
			if (data->dwData != 1 || !data->lpData || data->cbData < sizeof(wchar_t) || data->cbData > 65536 || data->cbData % sizeof(wchar_t)) return FALSE;
			auto text = static_cast<const wchar_t*>(data->lpData); if (text[data->cbData / sizeof(wchar_t) - 1]) return FALSE; std::wstring argument(text);
			if (!IsWindowEnabled(window)) { if (app->pendingArguments.size() >= 100) return FALSE; app->pendingArguments.push_back(std::move(argument)); SetTimer(window, 42, 250, nullptr); return TRUE; }
			if (argument.rfind(L"/StiEvent:", 0) == 0) app->HandleEvent(argument.substr(10)); else if (argument.rfind(L"/StiDevice:", 0) != 0 && !argument.empty()) app->ImportFiles({std::filesystem::path(argument)});
			return TRUE;
		}
		case WM_TIMER: if (wParam == 43) { app->PollSources(); return 0; } if (wParam == 42 && IsWindowEnabled(window)) { KillTimer(window, 42); std::vector<std::wstring> pending; pending.swap(app->pendingArguments); for (const auto& argument : pending) { if (argument.rfind(L"/StiEvent:", 0) == 0) app->HandleEvent(argument.substr(10)); else if (argument.rfind(L"/StiDevice:", 0) != 0 && !argument.empty()) app->ImportFiles({std::filesystem::path(argument)}); } return 0; } break;
		case WM_DROPFILES: { auto drop = reinterpret_cast<HDROP>(wParam); UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0); std::vector<std::filesystem::path> files; for (UINT n = 0; n < count; ++n) { UINT length = DragQueryFileW(drop, n, nullptr, 0); std::wstring file(size_t(length) + 1, L' '); DragQueryFileW(drop, n, file.data(), length + 1); file.resize(length); files.emplace_back(file); } DragFinish(drop); app->ImportFiles(files); return 0; }
		case WM_PRINTCLIENT:
		case WM_PAINT: {
			PAINTSTRUCT p{}; HDC dc = message == WM_PRINTCLIENT ? reinterpret_cast<HDC>(wParam) : BeginPaint(window, &p); RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))); RECT sidebar{0, UiPx(window, 60), UiPx(window, 139), client.bottom}; FillRect(dc, &sidebar, SidebarBrush()); RECT toolbar{0, 0, client.right, UiPx(window, 60)}; ToolbarBackground(dc, toolbar); RECT strip{UiPx(window, 139), client.bottom - UiPx(window, 20), client.right, client.bottom}; FillRect(dc, &strip, GetSysColorBrush(COLOR_3DFACE)); RECT bounds = app->WorkRect(); HBRUSH bg = CreateSolidBrush(app->document.pages.empty() ? RGB(255, 255, 255) : app->background); FillRect(dc, &bounds, bg); DeleteObject(bg); int saved = SaveDC(dc); IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
			auto layout = app->Geometry(); if (app->uiFont) SelectObject(dc, app->uiFont); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
			for (size_t i = 0; i < layout.pages.size(); ++i) {
				RECT r = layout.pages[i], visible = r, intersection{}; visible.bottom += UiPx(window, 20); if (!IntersectRect(&intersection, &visible, &bounds)) continue;
				DrawImage(dc, app->viewMode >= 3 ? app->document.pages[i].Thumbnail() : app->document.pages[i].image, r);
				bool selected = std::find(app->document.selection.begin(), app->document.selection.end(), i) != app->document.selection.end(); FrameRect(dc, &r, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_3DSHADOW)); if (selected) { InflateRect(&r, -1, -1); FrameRect(dc, &r, GetSysColorBrush(COLOR_HIGHLIGHT)); }
				RECT label{layout.pages[i].left - UiPx(window, 6), layout.pages[i].bottom, layout.pages[i].right + UiPx(window, 6), layout.pages[i].bottom + UiPx(window, 20)}; std::wstring text = std::to_wstring(i + 1); DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
			}
			if (app->selecting) DrawFocusRect(dc, &app->selectionFrame);
			RestoreDC(dc, saved); if (message == WM_PAINT) EndPaint(window, &p); return 0;
		}
		case WM_MOUSEWHEEL: if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) app->Execute(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? ZoomIn : ZoomOut); else { UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0); RECT work = app->WorkRect(); int amount = lines == WHEEL_PAGESCROLL ? work.bottom - work.top : static_cast<int>(lines) * UiPx(window, 16); app->ScrollTo(app->scroll.x, app->scroll.y - MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), amount, WHEEL_DELTA)); } return 0;
		case WM_KEYDOWN: if (wParam == VK_NEXT || wParam == VK_PRIOR) { app->ScrollBar(SB_VERT, wParam == VK_NEXT ? SB_PAGEDOWN : SB_PAGEUP); return 0; } if (wParam == VK_HOME || wParam == VK_END) { app->ScrollBar(SB_VERT, wParam == VK_HOME ? SB_TOP : SB_BOTTOM); return 0; } if (wParam == VK_UP || wParam == VK_DOWN) { app->ScrollBar(SB_VERT, wParam == VK_UP ? SB_LINEUP : SB_LINEDOWN); return 0; } break;
		case WM_LBUTTONDOWN: app->BeginSelection({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam); return 0;
		case WM_MOUSEMOVE: app->MovePointer({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); return 0;
		case WM_LBUTTONUP: app->EndSelection({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); return 0;
		case WM_CAPTURECHANGED: app->panning = app->selecting = app->pageDrag = false; app->selectionFrame = {}; InvalidateRect(window, nullptr, TRUE); return 0;
		case WM_LBUTTONDBLCLK: case WM_MBUTTONDBLCLK: { POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; size_t hit = app->HitPage(point); if (hit < app->document.pages.size()) { app->document.selection = {hit}; app->selectionAnchor = hit; app->Refresh(); int command = DoubleClickCommand(app->cropDoubleClick, message == WM_MBUTTONDBLCLK, (wParam & MK_CONTROL) != 0); if (command) app->Execute(command); } return 0; }
		case WM_CLOSE: if (app->Close()) DestroyWindow(window); return 0;
		case WM_DESTROY: PostQuitMessage(0); return 0;
		}
	} catch (const std::exception& e) { Error(window, e); return 0; }
	return DefWindowProcW(window, message, wParam, lParam);
}
static void RunNativeCopyTests(const std::filesystem::path& directory, const std::function<void(bool, const wchar_t*)>& test) {
	App app; app.data = directory; app.settings = directory / L"settings.ini"; app.DefaultAreas();
	HWND window = CreateWindowExW(0, L"ScannerApplication", L"Copy regression", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 938, 629, nullptr, nullptr, instance, &app);
	if (!window) throw std::runtime_error("Kopiertest-Fenster konnte nicht erstellt werden.");
	struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{window};
	Image image; image.width = 2; image.height = 3; image.dpiX = 200; image.dpiY = 300; image.pixels.resize(24, 255); image.pixels[0] = 17;
	Image back = image; back.pixels[0] = 29;
	app.document.pages.emplace_back(L"existing", image); app.document.selection = {0}; app.document.clipboard = app.document.pages; app.Refresh();
	app.scan.source = L"Test TWAIN"; app.scan.dpi = 300; app.scan.mode = 2; app.scan.driverDialog = true;
	app.scan.xMm = 3; app.scan.yMm = 5; app.scan.widthMm = 100; app.scan.heightMm = 150; app.scan.brightness = -20;
	app.sourcesReady = true; app.sourceCache.connections = {{false, app.scan.source, app.scan.source}};
	app.copyCount = 2; app.printCollate = false; app.printFit = false; app.printRotate = false;
	app.copyPrinter = [] { return L"Test default printer"; };
	int scans = 0, prints = 0; bool exactScan = true, exactPrint = true, guarded = true;
	ScanResult result; result.completed = true; result.pages = {image};
	app.copyTwainScan = [&](HWND owner, const ScanOptions& options) {
		++scans; guarded = guarded && owner == window && app.copyBusy && !IsWindowEnabled(window);
		exactScan = exactScan && options.source == app.scan.source && options.mode == 2 && options.dpi == 300 && options.xMm == 3 && options.yMm == 5 && options.widthMm == 100 && options.heightMm == 150 && options.brightness == -20 && options.feeder == app.scan.feeder && options.duplex == app.scan.duplex && !options.driverDialog;
		return result;
	};
	app.copyPrint = [&](HWND owner, const std::vector<Image>& pages, const PrintOptions& options) {
		++prints; guarded = guarded && owner == window && app.copyBusy && !IsWindowEnabled(window);
		exactPrint = exactPrint && options.printer == L"Test default printer" && options.copies == 2 && !options.fit && !options.rotate && !options.collate && options.output.empty() && pages.size() == result.pages.size();
		for (size_t i = 0; i < pages.size() && i < result.pages.size(); ++i) exactPrint = exactPrint && pages[i].pixels == result.pages[i].pixels && pages[i].dpiX == 200 && pages[i].dpiY == 300;
		if (options.jobId) *options.jobId = 42;
		return true;
	};
	auto unchanged = [&] { return app.document.pages.size() == 1 && app.document.pages[0].id == L"existing" && app.document.pages[0].image.pixels == image.pixels && app.document.selection == std::vector<size_t>{0} && app.document.clipboard.size() == 1 && app.document.clipboard[0].image.pixels == image.pixels; };
	for (int mode = 0; mode < 3; ++mode) {
		app.scan.feeder = mode != 0; app.scan.duplex = mode == 2; result.pages = mode == 2 ? std::vector<Image>{image, back} : std::vector<Image>{image};
		SendMessageW(app.buttons[8], BM_CLICK, 0, 0);
		test(scans == mode + 1 && prints == mode + 1 && exactScan && exactPrint && guarded && !app.copyBusy && IsWindowEnabled(window) && app.scan.driverDialog && unchanged(), L"Copy: button immediately scans flatbed/ADF/duplex and prints once without changing document or scan settings");
	}
	test((GetMenuState(GetMenu(window), CopyMode, MF_BYCOMMAND) & MF_CHECKED) == 0 && !IsWindowVisible(window), L"Copy: no persistent mode and no scan preview");
	Document existing = app.document; app.document = {}; app.Refresh();
	SendMessageW(app.buttons[8], BM_CLICK, 0, 0);
	test(scans == 4 && prints == 4 && exactPrint && app.document.pages.empty() && app.document.selection.empty() && app.document.clipboard.empty() && !app.copyBusy, L"Copy: empty document stays empty after immediate button copy");
	app.document = std::move(existing); app.Refresh();
	auto rejected = [&] { int before = prints; bool failed = false; try { app.CopyNow(); } catch (const std::runtime_error&) { failed = true; } return failed && prints == before && !app.copyBusy && IsWindowEnabled(window) && unchanged(); };
	result.completed = false; result.error = "Incomplete scan";
	test(rejected(), L"Copy: partial failed scan neither prints nor changes document and resets busy state");
	result.completed = true; result.cancelled = true;
	test(rejected(), L"Copy: cancelled scan never prints partial pages");
	result.cancelled = false; result.pages.clear(); result.error.clear();
	test(rejected(), L"Copy: empty successful scan is reported as failure");
	const int before = scans; app.copyPrinter = []() -> std::wstring { throw std::runtime_error("No default printer"); };
	test(rejected() && scans == before, L"Copy: missing default printer fails before scanning without fallback or dialog");
	app.copyPrinter = [] { return L"Test default printer"; }; app.sourcesReady = false;
	test(rejected() && scans == before, L"Copy: pending source enumeration is explicit and starts no scan");
	app.sourcesReady = true; app.sourceCache.connections.clear();
	test(rejected() && scans == before, L"Copy: unavailable scanner starts no scan");
	app.sourceCache.connections = {{false, app.scan.source, app.scan.source}}; result.pages = {image};
	app.copyPrint = [](HWND, const std::vector<Image>&, const PrintOptions&) { return false; };
	test(rejected(), L"Copy: printer cancellation reports failure and preserves document");
	app.copyPrint = [](HWND, const std::vector<Image>&, const PrintOptions&) -> bool { throw std::runtime_error("Printer failed"); };
	test(rejected(), L"Copy: printer exception resets busy state and preserves document");
	app.copyTwainScan = [](HWND, const ScanOptions&) -> ScanResult { throw std::runtime_error("Scanner failed"); };
	test(rejected(), L"Copy: scanner exception resets busy state and preserves document");
}
static void RunNativeCropTests(HWND owner, const std::function<void(bool, const wchar_t*)>& test) {
	Image original; original.width = 64; original.height = 48; original.dpiX = 200; original.dpiY = 300; original.pixels.resize(64 * 48 * 4, 255);
	for (size_t i = 0; i < 64 * 48; ++i) { original.pixels[i * 4] = static_cast<BYTE>(i % 200); original.pixels[i * 4 + 1] = 45; original.pixels[i * 4 + 2] = 90; }
	for (bool buttons : {false, true}) {
		CropState state; state.image = original;
		// Update regions are discarded for hidden windows; keep this test off-screen and nonactivating.
		HWND crop = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"ScannerCrop", L"Crop regression", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, -30000, -30000, 850, 740, owner, nullptr, instance, &state);
		if (!crop) throw std::runtime_error("Zuschneiden-Testfenster konnte nicht erstellt werden.");
		struct Cleanup { HWND window; ~Cleanup() { if (IsWindow(window)) DestroyWindow(window); } } cleanup{crop};
		auto command = [&](int id) { if (buttons && GetDlgItem(crop, id)) SendDlgItemMessageW(crop, id, BM_CLICK, 0, 0); else SendMessageW(crop, WM_COMMAND, id, 0); };
		auto acceptDisabled = [&]() { return !IsWindowEnabled(GetDlgItem(crop, CropAccept)) && (GetMenuState(GetSubMenu(state.menu, 0), CropAccept, MF_BYCOMMAND) & MF_GRAYED); };
		auto select = [&]() {
			POINT start{state.display.left + 8, state.display.top + 8}, end{state.display.left + 40, state.display.top + 32};
			SendMessageW(crop, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
			SendMessageW(crop, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
			SendMessageW(crop, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
		};
		test(acceptDisabled(), L"Crop: Apply initially disabled in button and menu");
		SendMessageW(crop, WM_COMMAND, MAKEWPARAM(CropAccept, 1), 0);
		test(IsWindow(crop) && !state.accepted, L"Crop: Enter cannot accept before Free");
		command(CropOriginal); select();
		RECT selected = CropPixelRect(state);
		test(selected.left == 8 && selected.top == 8 && selected.right == 40 && selected.bottom == 32 && acceptDisabled(), L"Crop: marquee maps to pixels without enabling Apply");
		RECT oldOutline = state.selection; InflateRect(&oldOutline, 2, 2);
		ValidateRect(crop, nullptr);
		POINT next{state.display.left + 45, state.display.top + 35};
		SendMessageW(crop, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(next.x, next.y));
		RECT repaint{}; bool invalidated = GetUpdateRect(crop, &repaint, FALSE) != FALSE;
		test(invalidated && repaint.left <= oldOutline.left && repaint.top <= oldOutline.top && repaint.right >= oldOutline.right && repaint.bottom >= oldOutline.bottom, L"Crop: starting a disjoint rectangle repaints the entire old outline");
		SendMessageW(crop, WM_LBUTTONUP, 0, MAKELPARAM(next.x, next.y));
		select(); command(CropFree);
		ValidateRect(crop, nullptr);
		SendMessageW(crop, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(next.x, next.y));
		RECT work = CropWorkArea(crop); invalidated = GetUpdateRect(crop, &repaint, FALSE) != FALSE;
		test(invalidated && repaint.left <= work.left && repaint.top <= work.top && repaint.right >= work.right && repaint.bottom >= work.bottom && !state.freePreview && acceptDisabled(), L"Crop: replacing a freed rectangle restores the whole preview and disables Apply");
		SendMessageW(crop, WM_LBUTTONUP, 0, MAKELPARAM(next.x, next.y));
		select(); command(CropFree);
		test(state.freePreview && IsWindowEnabled(GetDlgItem(crop, CropAccept)) && !(GetMenuState(GetSubMenu(state.menu, 0), CropAccept, MF_BYCOMMAND) & MF_GRAYED) && state.image.pixels == original.pixels, buttons ? L"Crop: Free button previews without changing pixels" : L"Crop: Free menu previews without changing pixels");
		select(); test(!state.freePreview && acceptDisabled(), L"Crop: new rectangle resets Free and disables Apply");
		command(CropCut);
		Image erased = original; DeleteRectangle(erased, RECT{8, 8, 40, 32});
		test(state.image.width == 64 && state.image.height == 48 && state.image.pixels == erased.pixels && acceptDisabled(), buttons ? L"Crop: Cut button erases only rectangle" : L"Crop: Cut menu erases only rectangle");
		command(CropZoomIn); test(state.zoomPercent == 125., L"Crop: Zoom In changes view");
		command(CropZoomOut); test(state.zoomPercent == 100., L"Crop: Zoom Out restores view");
		select(); command(CropFree);
		command(CropCancel);
		test(state.done && !state.accepted && original.pixels[0] == 0, L"Crop: Cancel does not accept edited draft");
	}
	CropState state; state.image = original;
	HWND crop = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ScannerCrop", L"Crop regression", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 850, 740, owner, nullptr, instance, &state);
	if (!crop) throw std::runtime_error("Zuschneiden-Testfenster konnte nicht erstellt werden.");
	struct Cleanup { HWND window; ~Cleanup() { if (IsWindow(window)) DestroyWindow(window); } } cleanup{crop};
	auto renderButton = [&](UINT flags) {
		BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = 160; info.bmiHeader.biHeight = -36; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
		void* pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr); HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
		if (!dc || !bitmap || !pixels) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); throw std::runtime_error("Button-Testpuffer konnte nicht erstellt werden."); }
		HGDIOBJ previous = SelectObject(dc, bitmap); RECT rect{0, 0, 160, 36}; FillRect(dc, &rect, GetSysColorBrush(COLOR_BTNFACE));
		DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_BUTTON; draw.CtlID = CropCancel; draw.itemAction = ODA_DRAWENTIRE; draw.itemState = flags; draw.hwndItem = GetDlgItem(crop, CropCancel); draw.hDC = dc; draw.rcItem = rect;
		SendMessageW(crop, WM_DRAWITEM, CropCancel, reinterpret_cast<LPARAM>(&draw)); GdiFlush();
		const auto* first = static_cast<const BYTE*>(pixels); std::vector<BYTE> result(first, first + 160 * 36 * 4);
		SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); return result;
	};
	auto enabledPixels = renderButton(0), disabledPixels = renderButton(ODS_DISABLED);
	test(enabledPixels != disabledPixels, L"Crop: disabled toolbar is visibly different from active toolbar");
	SendDlgItemMessageW(crop, CropCancel, WM_MOUSEMOVE, 0, MAKELPARAM(4, 4));
	test(renderButton(0) != enabledPixels, L"Crop: enabled toolbar has Mouse-over feedback");
	SendDlgItemMessageW(crop, CropCancel, WM_MOUSELEAVE, 0, 0);
	test(renderButton(0) == enabledPixels, L"Crop: leaving toolbar clears Mouse-over feedback");
	SendMessageW(crop, WM_COMMAND, CropOriginal, 0);
	state.selection = {state.display.left + 8, state.display.top + 8, state.display.left + 40, state.display.top + 32}; CropUpdateActions(crop, state);
	SendDlgItemMessageW(crop, CropFree, BM_CLICK, 0, 0); SendDlgItemMessageW(crop, CropAccept, BM_CLICK, 0, 0);
	Image expected = Crop(original, RECT{8, 8, 40, 32});
	test(state.done && state.accepted && state.image.width == 32 && state.image.height == 24 && state.image.pixels == expected.pixels && state.image.dpiX == 200 && state.image.dpiY == 300, L"Crop: Free then Apply buttons commit exact rectangle and DPI");
}
struct HelpSmoke { bool opened = false, search = false, topic = false; };
static void CALLBACK HelpSmokeTimer(HWND owner, UINT, UINT_PTR id, DWORD) {
	KillTimer(owner, id); auto result = static_cast<HelpSmoke*>(GetPropW(owner, L"HelpSmoke")); HWND dialog = FindWindowW(L"ScannerDialogHelp", L"Scanner – Hilfethemen"); if (!result || !dialog || GetWindow(dialog, GW_OWNER) != owner) return;
	auto text = [&] { HWND control = GetDlgItem(dialog, 101); int n = GetWindowTextLengthW(control); std::wstring value(n + 1, L' '); GetWindowTextW(control, value.data(), n + 1); value.resize(n); return value; };
	auto initial = text(); result->opened = initial.find(L"Scannen") != std::wstring::npos && initial.find(L"Arbeitsbereich und Datenschutz") != std::wstring::npos && SendDlgItemMessageW(dialog, 102, CB_GETCOUNT, 0, 0) == 11;
	SetDlgItemTextW(dialog, 103, L"Dateinamenautomatik"); auto filtered = text(); result->search = filtered.find(L"Dateinamenautomatik") != std::wstring::npos && filtered.find(L"Arbeitsbereich und Datenschutz") == std::wstring::npos;
	SetDlgItemTextW(dialog, 103, L""); SendDlgItemMessageW(dialog, 102, CB_SETCURSEL, 1, 0); SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(102, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(dialog, 102))); auto chosen = text(); result->topic = chosen.find(L"Scannen") != std::wstring::npos && chosen.find(L"Arbeitsbereich und Datenschutz") == std::wstring::npos;
	SendMessageW(dialog, WM_CLOSE, 0, 0);
}
int RunApplication(HINSTANCE hInstance, int show, const std::vector<std::wstring>& arguments) {
	instance = hInstance;
	INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES}; InitCommonControlsEx(&common);
	WNDCLASSEXW form{}; form.cbSize = sizeof(form); form.hInstance = instance; form.hCursor = LoadCursorW(nullptr, IDC_ARROW); form.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); form.lpfnWndProc = FormProc; form.lpszClassName = L"ScannerForm"; RegisterClassExW(&form);	form.lpfnWndProc = CropProc; form.lpszClassName = L"ScannerCrop"; form.style = CS_HREDRAW | CS_VREDRAW; form.hCursor = LoadCursorW(nullptr, IDC_CROSS); form.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SCANNER)); form.hIconSm = form.hIcon; RegisterClassExW(&form);
	WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; cls.hInstance = instance; cls.lpfnWndProc = MainProc; cls.lpszClassName = L"ScannerApplication"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_SCANNER)); RegisterClassExW(&cls);
	bool cropSmoke = arguments.size() == 2 && arguments[0] == L"--crop-smoke-test";
	bool copySmoke = arguments.size() == 2 && arguments[0] == L"--copy-smoke-test";
	bool smoke = cropSmoke || copySmoke || (arguments.size() == 2 && arguments[0] == L"--ui-smoke-test");
	bool copyHardware = arguments.size() == 2 && arguments[0] == L"--copy-hardware-test";
	HANDLE mutex = nullptr;
	if (!smoke) {
		auto mutexName = L"Local\\Scanner-" + DeviceKey(UserDataDirectory().wstring()); mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
		if (!mutex) return 2;
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			if (copyHardware) { std::wofstream report{std::filesystem::path(arguments[1])}; report << L"ERROR: Scanner laeuft bereits. Vor dem Hardware-Kopiertest schliessen.\n"; CloseHandle(mutex); return 2; }
			HWND existing = nullptr; for (int attempt = 0; attempt < 40 && !existing; ++attempt) { existing = FindWindowW(cls.lpszClassName, nullptr); if (!existing) Sleep(50); }
			if (existing) { for (const auto& argument : arguments) { COPYDATASTRUCT data{1, static_cast<DWORD>((argument.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(argument.c_str())}; DWORD_PTR result = 0; SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000, &result); } ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
			CloseHandle(mutex); return existing ? 0 : 2;
		}
	}
	struct Mutex { HANDLE value; ~Mutex() { if (value) { ReleaseMutex(value); CloseHandle(value); } } } releaseMutex{mutex};
	App app;
	if (smoke) { app.data = arguments[1]; std::filesystem::create_directories(app.data); app.settings = app.data / L"settings.ini"; app.workspace = app.data / L"Workspace"; ClearWorkspace(app.workspace); app.DefaultAreas(); }
	else try { app.Load(); } catch (const std::exception& e) { if (copyHardware) { std::wofstream report{std::filesystem::path(arguments[1])}; report << L"ERROR: " << WideError(e.what()) << L'\n'; } else Error(nullptr, e); return 1; }
	HWND window = CreateWindowExW(0, cls.lpszClassName, L"Scanner-Interface", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 938, 629, nullptr, nullptr, instance, &app); if (!window) return 1;
	if (UiPx(window, 96) != 96) SetWindowPos(window, nullptr, 0, 0, UiPx(window, 938), UiPx(window, 629), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	if (copyHardware) {
		std::wofstream report{std::filesystem::path(arguments[1])};
		if (!report) { DestroyWindow(window); return 2; }
		int exit = 0;
		try {
			report << L"Source=" << app.scan.source << L" Backend=" << (app.wia ? L"WIA" : L"TWAIN") << L"\nDpi=" << app.scan.dpi << L" Feeder=" << app.scan.feeder << L" Duplex=" << app.scan.duplex << L" DriverUI=0\n"; report.flush();
			app.StartSources(); app.sourceTask.wait(); app.PollSources();
			auto receipt = app.CopyNow();
			report << L"Printer=" << receipt.printer << L"\nAcquiredPages=" << receipt.pages << L" JobId=" << receipt.jobId << L" GdiEndDocSucceeded=1\nDocumentPages=" << app.document.pages.size() << L" MainWindowVisible=" << IsWindowVisible(window) << L" CopyBusy=" << app.copyBusy << L"\nPhysicalOutputVisuallyConfirmed=0\n";
			if (receipt.jobId <= 0 || !app.document.pages.empty() || app.copyBusy || IsWindowVisible(window)) throw std::runtime_error("Hardware-Kopiertest hat seine Nachbedingungen verletzt.");
		} catch (const std::exception& e) { report << L"ERROR: " << WideError(e.what()) << L'\n'; exit = 1; }
		report.flush(); if (!report) exit = 2;
		DestroyWindow(window); return exit;
	}
	if (smoke) {
		std::wofstream report(app.data / L"ui-results.txt"); int failures = 0;
		auto test = [&](bool ok, const wchar_t* name) { report << (ok ? L"PASS " : L"FAIL ") << name << L'\n'; if (!ok) ++failures; };
		try {
			if (copySmoke) { RunNativeCopyTests(app.data, test); report << L"TOTAL FAILURES: " << failures << L'\n'; DestroyWindow(window); return failures ? 1 : 0; }
			if (cropSmoke) { RunNativeCropTests(window, test); report << L"TOTAL FAILURES: " << failures << L'\n'; DestroyWindow(window); return failures ? 1 : 0; }
			test(IsWindow(app.list) && IsWindow(app.status) && IsWindow(app.buttons[8]), L"Main window controls");
			{
				App areasApp; areasApp.data = app.data / L"AreaTests"; std::filesystem::create_directories(areasApp.data); areasApp.settings = areasApp.data / L"settings.ini"; areasApp.workspace = areasApp.data / L"Workspace"; areasApp.window = window;
				for (int i = 0; i < 4; ++i) areasApp.settingControls[i] = app.settingControls[i]; areasApp.panelCombo = app.panelCombo;
				areasApp.LoadAreas(); const std::vector<Area> defaults = areasApp.areas;
				test(defaults.size() == 7 && defaults[0].name == L"A2" && defaults[0].width == 420 && defaults[0].height == 594 && defaults[1].name == L"A3" && defaults[2].name == L"A4" && defaults[3].name == L"A5" && defaults[3].width == 148 && defaults[4].name == L"A6" && defaults[4].width == 105 && defaults[5].name == L"US-Letter" && defaults[5].width == 216 && defaults[5].height == 279 && defaults[6].width == 85 && defaults[6].height == 55 && std::all_of(defaults.begin(), defaults.end(), [](const Area& a) { return a.x == 0 && a.y == 0; }), L"Scan areas exact standard list and zero origins");
				ScanCapabilities caps; caps.maxWidthMm = 210; caps.maxHeightMm = 297;
				test(FitsArea({L"edge", 200, 277, 10, 20}, caps) && !FitsArea({L"X outside", 200, 277, 10.01, 20}, caps) && !FitsArea({L"Y outside", 200, 277, 10, 20.01}, caps) && FitsArea(defaults[0], {}), L"Scan area filter checks X+width and Y+height with unknown capability compatibility");
				areasApp.areas = {{L"Hidden", 500, 600}, {L"Offset", 80, 50, 12.5, 23.75}}; areasApp.filterAreas = true; areasApp.capabilities = caps; areasApp.SyncSettings();
				SendMessageW(areasApp.settingControls[1], CB_SETCURSEL, 1, 0); areasApp.SidebarSetting(AreaControl);
				test(areasApp.scan.widthMm == 80 && areasApp.scan.heightMm == 50 && areasApp.scan.xMm == 12.5 && areasApp.scan.yMm == 23.75 && areasApp.sidebarAreas[1] == 1, L"Filtered sidebar maps visible row to original area including X/Y");
				areasApp.SaveAreas(); areasApp.Load(); test(areasApp.scan.xMm == 12.5 && areasApp.scan.yMm == 23.75 && areasApp.selectedArea == L"Offset" && areasApp.areas[1].x == 12.5 && areasApp.areas[1].y == 23.75, L"Scan area geometry and named selection persist and reload");
				SendMessageW(areasApp.settingControls[1], CB_SETCURSEL, 0, 0); areasApp.SidebarSetting(AreaControl); areasApp.Load();
				test(areasApp.scan.maximumArea && areasApp.Setting(L"MaximumArea", L"") == L"1", L"Maximum area selection persists without substituting guessed dimensions");
				AreaDialogState draft; draft.areas = StandardAreas(); draft.filter = false; areasApp.CommitAreas(draft);
				test(areasApp.areas.size() == 2 && GetIni(areasApp.settings, L"Areas", L"Count", L"") == L"2" && areasApp.filterAreas, L"Cancelled area management persists neither list nor filter");
				draft.accepted = true; areasApp.CommitAreas(draft); areasApp.LoadAreas(); test(areasApp.areas.size() == 7 && !areasApp.filterAreas, L"Accepted area management persists reset list and filter");
				areasApp.areas.clear(); areasApp.SaveAreas(); areasApp.LoadAreas(); test(areasApp.areas.empty(), L"Explicitly empty scan area list remains empty after reload");
				SetIni(areasApp.settings, L"Areas", L"Count", L"1"); SetIni(areasApp.settings, L"Area-0", L"Name", L"Legacy"); SetIni(areasApp.settings, L"Area-0", L"X", L""); SetIni(areasApp.settings, L"Area-0", L"Y", L"");
				WritePrivateProfileStringW(L"Area-0", L"X", nullptr, areasApp.settings.c_str()); WritePrivateProfileStringW(L"Area-0", L"Y", nullptr, areasApp.settings.c_str()); areasApp.LoadAreas();
				test(areasApp.areas[0].x == 0 && areasApp.areas[0].y == 0, L"Legacy area profiles without X/Y load at zero origin");
				WNDCLASSW dialogClass{}; dialogClass.lpfnWndProc = AreaDialogProc; dialogClass.hInstance = instance; dialogClass.lpszClassName = L"ScannerAreaTest"; RegisterClassW(&dialogClass);
				AreaDialogState editor; editor.editor = true; HWND edit = CreateWindowExW(0, dialogClass.lpszClassName, L"Scanbereich bearbeiten", WS_POPUP, 0, 0, 600, 400, window, nullptr, instance, &editor);
				test(IsWindow(edit) && AreaText(edit, 114).empty() && AreaText(edit, 110) == L"0" && AreaText(edit, 111) == L"0" && AreaText(edit, 112) == L"0" && AreaText(edit, 113) == L"0" && IsWindow(GetDlgItem(edit, 210)) && IsWindow(GetDlgItem(edit, 213)), L"Native new area editor starts blank with four zero fields and spinners");
				auto invalidEditor = [&]() { try { ReadAreaEditor(edit); return false; } catch (const std::exception&) { return true; } }; test(invalidEditor(), L"Area editor rejects zero dimensions");
				SetDlgItemTextW(edit, 114, L"Offset"); SetDlgItemTextW(edit, 110, L"12,5"); SetDlgItemTextW(edit, 111, L"23.75"); SetDlgItemTextW(edit, 112, L"80"); SetDlgItemTextW(edit, 113, L"50");
				SetDlgItemTextW(edit, 110, L"-1"); test(invalidEditor(), L"Area editor rejects negative origin"); SetDlgItemTextW(edit, 110, L"12,5");
				SendMessageW(edit, WM_COMMAND, IDOK, 0); test(editor.accepted && editor.done && editor.area.x == 12.5 && editor.area.y == 23.75 && editor.area.width == 80 && editor.area.height == 50, L"Native area editor OK accepts decimal X/Y and dimensions");
				AreaDialogState cancelEditor; cancelEditor.editor = true; cancelEditor.area = editor.area; edit = CreateWindowExW(0, dialogClass.lpszClassName, L"", WS_POPUP, 0, 0, 600, 400, window, nullptr, instance, &cancelEditor); SetDlgItemTextW(edit, 110, L"99"); SendMessageW(edit, WM_COMMAND, IDCANCEL, 0);
				test(cancelEditor.done && !cancelEditor.accepted && cancelEditor.area.x == 12.5, L"Native area editor Cancel preserves original geometry");
				AreaDialogState manager; manager.areas = {editor.area}; HWND management = CreateWindowExW(0, dialogClass.lpszClassName, L"Scanbereiche bearbeiten", WS_POPUP, 0, 0, 700, 500, window, nullptr, instance, &manager);
				test(ListView_GetItemCount(GetDlgItem(management, 100)) == 1 && Header_GetItemCount(ListView_GetHeader(GetDlgItem(management, 100))) == 6 && IsWindow(GetDlgItem(management, 101)) && IsWindow(GetDlgItem(management, 105)), L"Native area manager has table, commands and full containment checkbox");
				SendMessageW(management, WM_COMMAND, 103, 0); test(manager.areas.empty(), L"Native area manager deletes selected row"); SendMessageW(management, WM_COMMAND, 104, 0); test(manager.areas.size() == 7, L"Native area manager reset restores seven standards"); SendMessageW(management, WM_CLOSE, 0, 0); test(manager.done && !manager.accepted, L"Native area manager close discards working copy");
				app.SyncSettings();
			}
			test(!IsWindowVisible(window), L"Smoke main window never visible");
			test(IsWindow(app.zoomIn) && IsWindow(app.zoomOut) && IsWindow(app.panelCombo) && !GetDlgItem(window, FullScreen), L"Zoom and sidebar controls without fullscreen button");
			bool icons = true; for (auto bitmap : app.toolbarImages) icons = icons && bitmap != nullptr; test(icons && cls.hIcon != nullptr, L"Toolbar bitmaps and application icon nonnull");
			wchar_t title[80]{}; GetWindowTextW(window, title, 80); test(std::wstring(title) == L"Scanner-Interface", L"Main window title");
			HMENU menu = GetMenu(window); test(GetMenuItemCount(menu) == 5, L"Five original main menus");
			const wchar_t* menuNames[]{L"&Aktion", L"&Bearbeiten", L"&Ansicht", L"E&xtras", L"?"}; const int menuCounts[]{8, 15, 7, 3, 2};
			for (int i = 0; i < 5; ++i) { wchar_t name[80]{}; GetMenuStringW(menu, i, name, 80, MF_BYPOSITION); test(std::wstring(name) == menuNames[i] && GetMenuItemCount(GetSubMenu(menu, i)) == menuCounts[i], L"Main menu name and item count"); }
			HMENU helpMenu = GetSubMenu(menu, 4); test(GetMenuState(helpMenu, Help, MF_BYCOMMAND) != static_cast<UINT>(-1) && GetMenuState(helpMenu, About, MF_BYCOMMAND) != static_cast<UINT>(-1) && GetMenuState(helpMenu, Support, MF_BYCOMMAND) == static_cast<UINT>(-1), L"Help menu contains help topics and info without support");
			auto helpPath = HelpFilePath(); std::ifstream helpFile(helpPath, std::ios::binary); std::string helpContents((std::istreambuf_iterator<char>(helpFile)), std::istreambuf_iterator<char>());
			test(helpFile.is_open() && helpPath.filename() == L"Hilfe.html" && helpContents.find("id=\"ausschneiden\"") != std::string::npos && helpContents.find("id=\"optionen\"") != std::string::npos && helpContents.find("OCR") != std::string::npos, L"Bundled help beside executable contains dialog topics");
			auto helpTopics = ReadHelpTopics(helpPath); test(helpTopics.size() == 10 && helpTopics.front().heading == L"Scannen" && helpTopics.back().heading == L"Probleme und Produktinfo", L"All bundled help sections decode correctly");
			HelpSmoke helpSmoke; SetPropW(window, L"HelpSmoke", &helpSmoke);
			UINT_PTR timer = SetTimer(window, 901, 100, HelpSmokeTimer); if (timer) { SendMessageW(window, WM_COMMAND, Help, 0); KillTimer(window, timer); } RemovePropW(window, L"HelpSmoke");
			test(timer && helpSmoke.opened && helpSmoke.search && helpSmoke.topic, L"Help menu opens internal viewer with working search and topic selection");
			HWND about = CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_ABOUTBOX), window, AboutProc, 0); wchar_t aboutText[1024]{}; if (about) GetDlgItemTextW(about, 1001, aboutText, 1024); std::wstring thanks = aboutText;
			const std::wstring expectedThanks = L"Herzlichen Dank an Grewe Computertechnik für die ursprüngliche Idee des Scanner-Interface.\r\n\r\nNach Grewes Ausscheiden wurde die Anwendung von MASU mit Unterstützung von GitHub Copilot Chat neu umgesetzt.\r\n\r\nDie Anwendung ist frei verfügbar; der Quellcode ist auf GitHub als Open Source bereitgestellt.";
			test(about && IsWindowEnabled(GetDlgItem(about, IDOK)) && !IsWindowEnabled(GetDlgItem(about, 1001)) && thanks == expectedThanks, L"Info dialog has exact requested text disabled and OK enabled"); if (about) DestroyWindow(about);
			HMENU editMenu = GetSubMenu(menu, 1); test(GetMenuState(editMenu, Backs, MF_BYCOMMAND) == static_cast<UINT>(-1) && GetMenuState(editMenu, Reverse, MF_BYCOMMAND) != static_cast<UINT>(-1) && GetMenuState(editMenu, SelectAll, MF_BYCOMMAND) != static_cast<UINT>(-1), L"Edit menu omits back-page sorting but retains reverse and select all");
			HMENU extra = GetSubMenu(menu, 3), scannerMenu = GetSubMenu(extra, 1); SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(scannerMenu), 0);
			bool noCopyOptions = true; for (int i = 0; i < GetMenuItemCount(extra); ++i) { wchar_t text[128]{}; GetMenuStringW(extra, i, text, 128, MF_BYPOSITION); noCopyOptions = noCopyOptions && std::wstring(text) != L"Kopieroptionen"; }
			test(noCopyOptions && scannerMenu && GetMenuItemCount(scannerMenu) == 1 && (GetMenuState(scannerMenu, 0, MF_BYPOSITION) & MF_GRAYED) && GetMenuState(extra, ScanSettings, MF_BYCOMMAND) == static_cast<UINT>(-1) && GetMenuState(extra, DeviceEvents, MF_BYCOMMAND) == static_cast<UINT>(-1) && GetMenuState(extra, Options, MF_BYCOMMAND) != static_cast<UINT>(-1) && GetMenuState(extra, BackgroundColor, MF_BYCOMMAND) != static_cast<UINT>(-1), L"Extras omits removed commands and retains options, scanner selection and background color");
			bool disabled = true; for (int i = 2; i <= 7; ++i) disabled = disabled && !IsWindowEnabled(app.buttons[i]); test(disabled && IsWindowEnabled(app.buttons[8]) && !IsWindowEnabled(app.zoomIn) && !IsWindowEnabled(app.zoomOut) && IsWindowEnabled(app.buttons[0]), L"Empty document enables copy mode but disables page actions");
			HMENU actionMenu = GetSubMenu(menu, 0), viewMenu = GetSubMenu(menu, 2);
			auto menuEnabled = [](HMENU m, int id) { UINT state = GetMenuState(m, id, MF_BYCOMMAND); return state != static_cast<UINT>(-1) && !(state & (MF_DISABLED | MF_GRAYED)); };
			auto refreshMenus = [&] { for (HMENU m : {actionMenu, editMenu, viewMenu}) SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(m), 0); };
			refreshMenus(); HMENU compact = BuildToolbarMenu(app, EditMenu), viewPopup = BuildToolbarMenu(app, ViewMenu);
			test(!menuEnabled(actionMenu, Save) && !menuEnabled(actionMenu, Clear) && menuEnabled(actionMenu, Scan) && menuEnabled(actionMenu, CopyMode) && !menuEnabled(editMenu, Cut) && !menuEnabled(editMenu, SelectAll) && !menuEnabled(viewMenu, ZoomIn) && !menuEnabled(compact, Cut) && !menuEnabled(viewPopup, ZoomOut) && menuEnabled(editMenu, Paste) == (IsClipboardFormatAvailable(CF_DIB) != FALSE), L"Empty document menu states match buttons, selection and clipboard"); DestroyMenu(compact); DestroyMenu(viewPopup);
			RunNativeCopyTests(app.data, test);
			RECT work = app.WorkRect(); test(work.left == UiPx(window, 139) && work.top == UiPx(window, 60), L"Original workspace origin");
			RECT listBounds{}; GetClientRect(app.list, &listBounds); test(!(GetWindowLongPtrW(app.list, GWL_STYLE) & (WS_VISIBLE | WS_TABSTOP)) && IsRectEmpty(&listBounds), L"Page list is a hidden non-tabstop selection mirror");
			bool white = false; test(SaveUiScreenshot(app, app.data / L"ui-empty.bmp", &white), L"Hidden initial PrintWindow BMP saved"); test(white && GetWindowTextLengthW(app.status) == 0, L"Empty workspace entirely white without hints");
			test(!app.sourceTask.valid() && !app.sourcesReady, L"Hidden smoke test never starts source enumeration");
			RECT arrowBounds{}; GetWindowRect(app.sourceArrow, &arrowBounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&arrowBounds), 2);
			test(ChildWindowFromPointEx(window, POINT{(arrowBounds.left + arrowBounds.right) / 2, UiPx(window, 30)}, CWP_SKIPINVISIBLE) == app.sourceArrow && ChildWindowFromPointEx(window, POINT{UiPx(window, 30), UiPx(window, 30)}, CWP_SKIPINVISIBLE) == app.buttons[0], L"Scan main button and source arrow have separate hit areas");
			RECT editBounds{}, symbolBounds{}, saveBounds{}; GetWindowRect(app.buttons[6], &editBounds); GetWindowRect(app.buttons[7], &symbolBounds); GetWindowRect(app.buttons[2], &saveBounds);
			test(editBounds.right - editBounds.left == symbolBounds.right - symbolBounds.left && editBounds.right - editBounds.left == saveBounds.right - saveBounds.left, L"Toolbar text buttons share equal width");
			for (int command : {ModeMono, ModeGray, ModeColor, DriverScan}) {
				app.Execute(command); HMENU modes = app.ModeMenu(); int checked = 0; bool radio = true;
				for (int i = 0; i < 4; ++i) { MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_FTYPE | MIIM_STATE; GetMenuItemInfoW(modes, i, TRUE, &item); radio = radio && (item.fType & MFT_RADIOCHECK); if (item.fState & MFS_CHECKED) ++checked; }
				test(GetMenuItemCount(modes) == 4 && checked == 1 && radio && (GetMenuState(modes, command, MF_BYCOMMAND) & MF_CHECKED), L"Four radio modes with exactly the selected mode checked"); DestroyMenu(modes);
				bool shown = command != DriverScan, visible = true; for (int i = 0; i < 4; ++i) visible = visible && (((GetWindowLongPtrW(app.settingControls[i], GWL_STYLE) & WS_VISIBLE) != 0) == shown) && (((GetWindowLongPtrW(app.settingLabels[i], GWL_STYLE) & WS_VISIBLE) != 0) == shown);
				RECT bounds{}; GetWindowRect(app.list, &bounds); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
				test(visible && !(GetWindowLongPtrW(app.list, GWL_STYLE) & WS_VISIBLE) && app.scan.driverDialog == !shown && app.document.pages.empty() && app.Setting(L"DriverDialog", L"") == (shown ? L"0" : L"1"), L"Mode selection persists, lays out settings, keeps list hidden, and never scans");
			}
			app.Execute(ModeGray); SendMessageW(app.settingControls[0], CB_SETCURSEL, 5, 0); SendMessageW(window, WM_COMMAND, MAKEWPARAM(DpiControl, CBN_SELCHANGE), reinterpret_cast<LPARAM>(app.settingControls[0]));
			SendMessageW(app.settingControls[2], CB_SETCURSEL, 2, 0); SendMessageW(window, WM_COMMAND, MAKEWPARAM(PaperControl, CBN_SELCHANGE), reinterpret_cast<LPARAM>(app.settingControls[2]));
			test(app.scan.dpi == 600 && app.scan.feeder && app.scan.duplex, L"Sidebar preset controls update scan options without querying hardware");
			app.Execute(DriverScan); app.sourceCache.connections = {{false, L"Synthetic", L"Same name"}, {true, L"wia-device-id", L"Same name"}}; app.sourcesReady = true;
			SetIni(app.settings, DeviceKey(L"WIA:wia-device-id").c_str(), L"Brightness", L"123");
			SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(scannerMenu), 0); SendMessageW(window, WM_COMMAND, SourceBase + 1, 0); SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(scannerMenu), 0); HMENU sources = app.SourceMenu();
			bool sameSources = GetMenuItemCount(scannerMenu) == GetMenuItemCount(sources); for (int i = 0; i < GetMenuItemCount(sources); ++i) { wchar_t a[256]{}, b[256]{}; GetMenuStringW(scannerMenu, i, a, 256, MF_BYPOSITION); GetMenuStringW(sources, i, b, 256, MF_BYPOSITION); sameSources = sameSources && std::wstring(a) == b && GetMenuItemID(scannerMenu, i) == GetMenuItemID(sources, i) && GetMenuState(scannerMenu, i, MF_BYPOSITION) == GetMenuState(sources, i, MF_BYPOSITION); }
			test(sameSources && app.wia && app.deviceId == L"wia-device-id" && app.scan.brightness == 123 && (GetMenuState(sources, SourceBase + 1, MF_BYCOMMAND) & MF_CHECKED) && !(GetMenuState(sources, SourceBase, MF_BYCOMMAND) & MF_CHECKED) && app.Setting(L"Backend", L"") == L"WIA" && app.Setting(L"DeviceId", L"") == L"wia-device-id", L"Extras and scan arrow have identical sources; menu selection persists exact backend"); DestroyMenu(sources);
			app.Execute(SourceBase); app.sourceCache.wiaError = L"WIA: synthetic error"; sources = app.SourceMenu(); SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(scannerMenu), 0);
			test(!app.wia && (GetMenuState(sources, SourceBase, MF_BYCOMMAND) & MF_CHECKED) && (GetMenuState(scannerMenu, SourceBase, MF_BYCOMMAND) & MF_CHECKED) && GetMenuItemCount(scannerMenu) == 3 && (GetMenuState(scannerMenu, 2, MF_BYPOSITION) & MF_GRAYED) && !(GetMenuState(sources, SourceBase, MF_BYCOMMAND) & MF_GRAYED), L"Extras updates TWAIN selection and shows WIA enumeration error"); DestroyMenu(sources);
			app.sourceCache = {}; SendMessageW(window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(scannerMenu), 0); wchar_t noScanner[80]{}; GetMenuStringW(scannerMenu, 0, noScanner, 80, MF_BYPOSITION);
			test(GetMenuItemCount(scannerMenu) == 1 && std::wstring(noScanner) == L"Keine Scanner gefunden" && (GetMenuState(scannerMenu, 0, MF_BYPOSITION) & MF_GRAYED), L"Extras updates empty scanner list after enumeration");
			app.sourcesReady = false; app.scan.source.clear(); app.deviceId.clear(); app.Refresh();
			Image image; image.width = 32; image.height = 24; image.pixels.resize(32 * 24 * 4, 255); image.pixels[0] = 17;
			app.AddImages({image, image}); test(SendMessageW(app.list, LB_GETCOUNT, 0, 0) == 2 && !std::filesystem::exists(app.workspace), L"Document updates hidden selection mirror without saving workspace");
			test(app.document.selection.empty() && SendMessageW(app.list, LB_GETSELCOUNT, 0, 0) == 0, L"New scans are unselected until clicked");
			bool enabled = true; for (int i = 2; i <= 7; ++i) enabled = enabled && IsWindowEnabled(app.buttons[i]); test(enabled && IsWindowEnabled(app.zoomIn) && IsWindowEnabled(app.zoomOut), L"Populated document toolbar enable states");
			refreshMenus(); compact = BuildToolbarMenu(app, EditMenu); viewPopup = BuildToolbarMenu(app, ViewMenu);
			test(menuEnabled(actionMenu, Save) && menuEnabled(actionMenu, Print) && menuEnabled(actionMenu, Mail) && menuEnabled(actionMenu, Clear) && menuEnabled(viewMenu, ZoomIn) && menuEnabled(viewPopup, ZoomOut) && menuEnabled(editMenu, SelectAll) && !menuEnabled(editMenu, Cut) && !menuEnabled(editMenu, CropPage) && !menuEnabled(compact, RotateR), L"Unselected pages enable document actions but disable selection edits in both menus"); DestroyMenu(compact); DestroyMenu(viewPopup);
			app.document.clipboard = app.document.pages; app.clipboardSequence = GetClipboardSequenceNumber(); refreshMenus(); test(menuEnabled(editMenu, Paste), L"Internal clipboard enables paste"); app.document.clipboard.clear();
			std::wstring queuedArgument = L"/StiDevice:synthetic"; COPYDATASTRUCT queuedData{1, static_cast<DWORD>((queuedArgument.size() + 1) * sizeof(wchar_t)), queuedArgument.data()}; EnableWindow(window, FALSE); SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&queuedData)); test(app.pendingArguments.size() == 1 && app.document.pages.size() == 2, L"Busy window defers forwarded arguments"); EnableWindow(window, TRUE); SendMessageW(window, WM_TIMER, 42, 0); test(app.pendingArguments.empty() && app.document.pages.size() == 2, L"Deferred argument drains without document reentrancy");
			app.Execute(SymbolsView); auto center = [](RECT r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
			auto click = [&](POINT point, WPARAM keys) { SendMessageW(window, WM_LBUTTONDOWN, keys | MK_LBUTTON, MAKELPARAM(point.x, point.y)); SendMessageW(window, WM_LBUTTONUP, keys, MAKELPARAM(point.x, point.y)); };
			auto selectionLayout = app.Geometry(); POINT firstPoint = center(selectionLayout.pages[0]), secondPoint = center(selectionLayout.pages[1]);
			click(firstPoint, 0); test(app.document.selection == std::vector<size_t>{0} && SendMessageW(app.list, LB_GETSELCOUNT, 0, 0) == 1, L"Central click selects page and updates hidden mirror");
			refreshMenus(); compact = BuildToolbarMenu(app, EditMenu); test(menuEnabled(editMenu, Cut) && menuEnabled(editMenu, Copy) && menuEnabled(editMenu, Delete) && menuEnabled(editMenu, CropPage) && menuEnabled(compact, RotateR) && menuEnabled(compact, Cut), L"Page selection enables editing in main and toolbar menus"); DestroyMenu(compact);
			click(secondPoint, MK_CONTROL); test(app.document.selection.size() == 2 && SendMessageW(app.list, LB_GETSELCOUNT, 0, 0) == 2, L"Central Ctrl click adds selection");
			click(firstPoint, 0); click(secondPoint, MK_SHIFT); test(app.document.selection == std::vector<size_t>({0, 1}), L"Central Shift click selects anchored range");
			click(firstPoint, MK_CONTROL); test(app.document.selection == std::vector<size_t>{1}, L"Central Ctrl click toggles selected page off");
			work = app.WorkRect(); POINT frameStart{work.left + 1, work.top + 1}, frameEnd{selectionLayout.pages[1].right + 1, std::max(selectionLayout.pages[0].bottom, selectionLayout.pages[1].bottom) + 1};
			SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(frameStart.x, frameStart.y)); SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(frameEnd.x, frameEnd.y)); SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(frameEnd.x, frameEnd.y)); test(app.document.selection.size() == 2 && SendMessageW(app.list, LB_GETSELCOUNT, 0, 0) == 2, L"Central rubber-band multi-selection updates mirror");
			click(firstPoint, 0); const std::wstring firstId = app.document.pages[0].id; POINT dropPoint{selectionLayout.pages[1].right - 2, secondPoint.y};
			SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(firstPoint.x, firstPoint.y)); SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(dropPoint.x, dropPoint.y)); SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(dropPoint.x, dropPoint.y)); test(app.document.pages[1].id == firstId && app.document.selection == std::vector<size_t>{1}, L"Central drag reorders selected page and keeps selection"); app.Execute(SelectAll);
			app.Execute(RotateR); test(app.document.pages[0].image.width == 24 && app.document.pages[1].image.height == 32, L"UI command applies multi-selection edit");
			app.Execute(Reverse); app.Execute(Width); bool cycling = true; for (int mode = 0; mode < 5; ++mode) { cycling = cycling && app.viewMode == mode; app.Execute(ZoomOut); } cycling = cycling && app.viewMode == 4; for (int mode = 4; mode >= 0; --mode) { cycling = cycling && app.viewMode == mode; app.Execute(ZoomIn); } test(cycling && app.viewMode == 0, L"Plus and minus cycle adjacent five modes and clamp at endpoints");
			SendMessageW(window, WM_COMMAND, MAKEWPARAM(ViewMenu, BN_CLICKED), reinterpret_cast<LPARAM>(app.buttons[7])); test(app.viewMode == 3, L"Symbols toolbar main action selects large grid");
			Document twoPageDocument = app.document; app.AddImages(std::vector<Image>(30, image)); const int viewCommands[]{Width, Fit, TwoPages, SymbolsView, SmallSymbols};
			for (int mode = 0; mode < 5; ++mode) {
				app.Execute(viewCommands[mode]); auto geometry = app.Geometry(); work = app.WorkRect(); bool all = geometry.pages.size() == app.document.pages.size() && geometry.pages.size() == 32;
				for (const auto& r : geometry.pages) all = all && r.right > r.left && r.bottom > r.top && r.bottom - work.top <= geometry.extent.cy;
				test(all && app.viewMode == mode && !(GetWindowLongPtrW(app.list, GWL_STYLE) & WS_VISIBLE) && (GetWindowLongPtrW(app.panelCombo, GWL_STYLE) & WS_VISIBLE), L"Each view lays out every page and retains settings without visible list");
				if (mode == 0) test(geometry.pages[0].left == work.left && geometry.pages[0].right == work.right && geometry.pages[1].top > geometry.pages[0].bottom && !(GetWindowLongPtrW(window, GWL_STYLE) & WS_HSCROLL), L"Width uses exact viewport width and vertical sequence without horizontal bar");
				if (mode == 1) test(geometry.pages[0].top >= work.top && geometry.pages[0].bottom <= work.bottom && geometry.pages[0].left >= work.left && geometry.pages[0].right <= work.right && geometry.pages[1].top > geometry.pages[0].bottom, L"Fit contains whole page in viewport and lays out later pages below");
				if (mode == 2) test(geometry.pages[0].top == geometry.pages[1].top && geometry.pages[1].left > geometry.pages[0].right && geometry.pages[2].top > std::max(geometry.pages[0].bottom, geometry.pages[1].bottom), L"Two pages per row includes later rows");
				if (mode >= 3) test(geometry.pages[1].left > geometry.pages[0].right && geometry.pages.back().top > geometry.pages[0].top, L"Symbol grids include all later rows");
			}
			app.Execute(Width); SendMessageW(window, WM_KEYDOWN, VK_NEXT, 0); test(app.scroll.y > 0, L"Page Down scrolls central canvas"); SendMessageW(window, WM_KEYDOWN, VK_PRIOR, 0); test(app.scroll.y == 0, L"Page Up returns to previous viewport");
			SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0); test(app.scroll.y > 0 && app.viewMode == 0, L"Ordinary wheel scrolls vertically without changing view");
			app.ScrollTo(1000000000, 1000000000); auto endGeometry = app.Geometry(); work = app.WorkRect(); test(app.scroll.x == 0 && app.scroll.y == endGeometry.extent.cy - (work.bottom - work.top) && app.HitPage(center(endGeometry.pages.back())) == app.document.pages.size() - 1, L"Scroll clamps to content extent and hit testing follows scrolled rectangles");
			app.ScrollTo(-100, -100); test(app.scroll.x == 0 && app.scroll.y == 0, L"Negative scroll clamps to zero"); app.document = twoPageDocument; app.Refresh(); app.Execute(Fit); app.ScrollTo(0, 1000000000); app.document.pages.resize(1); app.document.selection = {0}; app.Refresh(); test(app.scroll.y == 0, L"Document shrink clamps scroll"); app.document = std::move(twoPageDocument); app.Refresh(); app.Execute(SymbolsView);
			app.Execute(UndoEdit); test(app.document.pages[0].image.width == 32, L"UI undo restores original dimensions"); app.Execute(RotateR); app.Execute(RestoreOriginal); test(app.document.pages[0].image.pixels == image.pixels, L"UI original restoration preserves exact pixels");
			test(SaveUiScreenshot(app, app.data / L"ui-preview.bmp"), L"Hidden preview and thumbnail painting BMP saved");
			FormState formState; std::vector<Field> fields{{L"Value", L"42"}, {L"Choice", L"Ja", YesNo}}; formState.fields = &fields;
			HWND form = CreateWindowExW(0, L"ScannerForm", L"Test", WS_POPUP, 0, 0, 845, 220, window, nullptr, instance, &formState); SendMessageW(form, WM_COMMAND, IDOK, 0); test(formState.done && formState.accepted && fields[0].value == L"42" && fields[1].value == L"Ja", L"Options form accepts values");
			RunNativeCropTests(window, test);
			WNDCLASSW optionsClass{}; optionsClass.lpfnWndProc = OptionsProc; optionsClass.hInstance = instance; optionsClass.lpszClassName = L"OptionsSmoke"; RegisterClassW(&optionsClass);
			OptionsState options; HWND optionsWindow = CreateWindowExW(0, L"OptionsSmoke", L"Optionen", WS_POPUP, 0, 0, 340, 240, window, nullptr, instance, &options);
			test(AreaText(optionsWindow, 100) == L"Nach dem Speichern erste Seite öffnen" && SaveCombo(optionsWindow, 101) == 2 && SendDlgItemMessageW(optionsWindow, 101, CB_GETCOUNT, 0, 0) == 3 && SendDlgItemMessageW(optionsWindow, 102, CB_GETCOUNT, 0, 0) == 2, L"Options: reference controls and default clear mode");
			SaveCheck(optionsWindow, 100, true); SendDlgItemMessageW(optionsWindow, 101, CB_SETCURSEL, 0, 0); SendDlgItemMessageW(optionsWindow, 102, CB_SETCURSEL, 1, 0); SendMessageW(optionsWindow, WM_COMMAND, IDOK, 0);
			test(options.accepted && options.openAfter && options.closeMode == 0 && options.cropDoubleClick, L"Options: OK accepts all three choices");
			options = OptionsState{}; optionsWindow = CreateWindowExW(0, L"OptionsSmoke", L"Optionen", WS_POPUP, 0, 0, 340, 240, window, nullptr, instance, &options); SendDlgItemMessageW(optionsWindow, 101, CB_SETCURSEL, 0, 0); SendMessageW(optionsWindow, WM_COMMAND, IDCANCEL, 0); test(!options.accepted && options.closeMode == 2, L"Options: Cancel discards changes");
			test(DoubleClickCommand(false, false, false) == ZoomIn && DoubleClickCommand(false, true, false) == ZoomOut && DoubleClickCommand(true, false, false) == CropPage && DoubleClickCommand(true, false, true) == ZoomIn && DoubleClickCommand(true, true, true) == ZoomOut && DoubleClickCommand(true, true, false) == 0, L"Options: double-click actions respect crop and Control choices");
			App session; session.data = app.data / L"session-lifecycle"; std::filesystem::create_directories(session.data); session.settings = session.data / L"settings.ini"; session.workspace = session.data / L"Workspace"; session.Set(L"Dpi", L"600"); session.Set(L"CloseMode", L"0"); session.Set(L"OpenAfter", L"1"); session.Set(L"CropDoubleClick", L"1"); session.document = app.document; session.closeMode = 0;
			test(session.Close() && std::filesystem::exists(session.workspace / L"current"), L"Close keep saves workspace"); session.Load(); test(session.document.pages.size() == app.document.pages.size() && session.document.pages[0].image.pixels == app.document.pages[0].image.pixels && session.closeMode == 0 && session.openAfter && session.cropDoubleClick && session.scan.dpi == 600, L"Startup restores saved pages and options");
			session.closeMode = 1; test(!session.Close(IDCANCEL) && !session.document.pages.empty(), L"Close prompt Cancel preserves open document"); test(session.Close(IDNO), L"Close prompt No retains pages"); session.Load(); test(!session.document.pages.empty(), L"Close prompt No restores pages on startup"); session.closeMode = 1; test(session.Close(IDYES) && !std::filesystem::exists(session.workspace), L"Close prompt Yes deletes saved pages");
			session.document = app.document; session.closeMode = 2; test(session.Close() && session.document.pages.empty() && !std::filesystem::exists(session.workspace), L"Close without prompt clears pages"); session.Load(); test(session.document.pages.empty(), L"Cleared pages do not return on startup");
			session.document = app.document; session.closeMode = 0; auto workspace = session.workspace; session.workspace = session.settings / L"invalid"; bool saveFailed = false; try { session.Close(); } catch (const std::exception&) { saveFailed = true; } session.workspace = workspace;
			test(saveFailed && session.document.pages.size() == app.document.pages.size(), L"Close save failure preserves the open document");
			app.document.clipboard = app.document.pages; test(app.Close() && app.document.pages.empty() && app.document.selection.empty() && app.document.clipboard.empty() && SendMessageW(app.list, LB_GETCOUNT, 0, 0) == 0 && !std::filesystem::exists(app.workspace), L"UI Close clears in-memory session and hidden selection mirror");
			app.pdfQuality = app.exportOptions.quality = 73; app.jpegQuality = 29; app.exportOptions.author = L"Integration"; app.SaveExport(); App reloaded; reloaded.data = app.data; reloaded.settings = app.settings; reloaded.workspace = app.workspace; reloaded.LoadExport(); test(reloaded.exportOptions.quality == 73 && reloaded.pdfQuality == 73 && reloaded.jpegQuality == 29 && reloaded.exportOptions.author == L"Integration", L"Separate JPEG/PDF qualities and export settings persist");
			auto sameOptions = [](const ExportOptions& a, const ExportOptions& b) { return a.format == b.format && a.multipage == b.multipage && a.quality == b.quality && a.pngCompression == b.pngCompression && a.pdfPageMode == b.pdfPageMode && a.showThumbnails == b.showThumbnails && a.searchablePdf == b.searchablePdf && a.ocrLanguage == b.ocrLanguage && a.tiffColor == b.tiffColor && a.tiffMono == b.tiffMono && a.invertMono == b.invertMono && a.title == b.title && a.subject == b.subject && a.keywords == b.keywords && a.author == b.author; };
			ExportOptions custom; custom.format = Format::PdfA; custom.multipage = false; custom.quality = 82; custom.pngCompression = 9; custom.pdfPageMode = PdfPageMode::A4LandscapeIfNeeded; custom.showThumbnails = false; custom.searchablePdf = true; custom.ocrLanguage = OcrLanguage::GermanEnglish; custom.tiffColor = TiffCompression::Zip; custom.tiffMono = TiffCompression::G3; custom.invertMono = true; custom.title = L"  \"\u00e4 & <Archiv>\"  "; custom.subject = L"Zeile1\r\nZeile2"; custom.keywords = std::wstring(2000, L'\u00fc'); custom.author = L"Autor \U0001F4C4";
			app.exportOptions = custom; app.pdfQuality = custom.quality; app.SaveExport(); reloaded.LoadExport(); test(sameOptions(custom, reloaded.exportOptions), L"All export options and long Unicode metadata persist losslessly");
			NativeSaveState reopened; reopened.options = reloaded.exportOptions; reopened.pdfQuality = reloaded.pdfQuality; reopened.jpegQuality = reloaded.jpegQuality; ApplySaveDialogDefaults(reopened); test(sameOptions(custom, reopened.options) && !reopened.together, L"Reloaded export settings survive save and mail dialog initialization");
			App printSession; printSession.data = app.data / L"print-settings"; std::filesystem::create_directories(printSession.data); printSession.settings = printSession.data / L"settings.ini"; printSession.workspace = printSession.data / L"Workspace"; printSession.copyCount = 7; printSession.printFit = false; printSession.printRotate = false; printSession.printCollate = false; printSession.printToFile = true; printSession.printPrinter = L"Drucker 中文"; printSession.printDeviceMode.resize(16384); for (size_t i = 0; i < printSession.printDeviceMode.size(); ++i) printSession.printDeviceMode[i] = static_cast<BYTE>(i % 256); printSession.SavePrintSettings();
			App printReloaded; printReloaded.data = printSession.data; printReloaded.settings = printSession.settings; printReloaded.workspace = printSession.workspace; printReloaded.Load(); test(printReloaded.copyCount == 7 && !printReloaded.printFit && !printReloaded.printRotate && !printReloaded.printCollate && printReloaded.printToFile && printReloaded.printPrinter == printSession.printPrinter && printReloaded.printDeviceMode == printSession.printDeviceMode, L"Print settings including Unicode printer and large binary driver data survive restart");
			App legacyExport; legacyExport.data = app.data / L"legacy-export"; std::filesystem::create_directories(legacyExport.data); legacyExport.settings = legacyExport.data / L"settings.ini"; SetIni(legacyExport.settings, L"Export", L"Author", L"Existing author"); SetIni(legacyExport.settings, L"Export", L"PageMode", L"2"); SetIni(legacyExport.settings, L"Export", L"Thumbnails", L"1"); legacyExport.LoadExport(); test(legacyExport.exportOptions.format == Format::PdfA && legacyExport.exportOptions.author == L"Existing author" && legacyExport.exportOptions.pdfPageMode == PdfPageMode::Original && legacyExport.exportOptions.showThumbnails, L"Missing export format does not overwrite existing metadata and options");
			App migratedExport; migratedExport.data = app.data / L"migrated-export"; std::filesystem::create_directories(migratedExport.data); migratedExport.settings = migratedExport.data / L"settings.ini"; SetIni(migratedExport.settings, L"Export", L"PageMode", L"0"); migratedExport.LoadExport(); test(migratedExport.exportOptions.pdfPageMode == PdfPageMode::Original, L"Legacy default A4 migrates to original scan size");
			migratedExport.SaveExport(); migratedExport.exportOptions.pdfPageMode = PdfPageMode::A4; migratedExport.SaveExport(); App selectedA4; selectedA4.data = migratedExport.data; selectedA4.settings = migratedExport.settings; selectedA4.LoadExport(); test(selectedA4.exportOptions.pdfPageMode == PdfPageMode::A4, L"Explicit A4 selection survives the one-time default migration");
			App firstExport; firstExport.data = app.data / L"first-export"; std::filesystem::create_directories(firstExport.data); firstExport.settings = firstExport.data / L"settings.ini"; firstExport.LoadExport(); test(firstExport.exportOptions.pdfPageMode == PdfPageMode::Original, L"First PDF export defaults to original scan size");
			ExportOptions roundtrip = custom; roundtrip.multipage = true; roundtrip.pngCompression = 0; app.exportOptions = roundtrip; app.SaveExport(); reloaded.LoadExport(); test(reloaded.exportOptions.multipage && reloaded.exportOptions.pngCompression == 0, L"Export format roundtrips with lossless compressor and multipage option");
			RunNativeSaveTests(window, app.data, image, test);
			RunNativePrintTests(window, test);
			for (int mode = 0; mode < 6; ++mode) { app.Set(L"NameMode", std::to_wstring(mode)); test(Integer(app.Setting(L"NameMode", L"0"), 0, 5) == mode, L"All six automation modes persist"); }
			const std::wstring unicodeName = (app.data / L"Überprüfung 中文 📄.pdf").wstring(); app.Set(L"LastNameUtf16", EncodeSettingText(unicodeName)); test(DecodeSettingText(app.Setting(L"LastNameUtf16", L"")) == unicodeName, L"Last filename persists as exact Unicode");
			app.Execute(ResetExport); reloaded.LoadExport(); ExportOptions resetDefaults; resetDefaults.quality = 80; test(sameOptions(reloaded.exportOptions, resetDefaults) && reloaded.jpegQuality == 80 && reloaded.pdfQuality == 80, L"Internal export reset persists UI defaults and clears metadata");
			SetIni(app.settings, DeviceKey(L"Synthetic A").c_str(), L"Brightness", L"-100"); SetIni(app.settings, DeviceKey(L"Synthetic B").c_str(), L"Brightness", L"200"); test(GetIni(app.settings, DeviceKey(L"Synthetic A").c_str(), L"Brightness", L"0") == L"-100" && GetIni(app.settings, DeviceKey(L"Synthetic B").c_str(), L"Brightness", L"0") == L"200", L"Independent per-device brightness persistence");
			test(app.Close() && !std::filesystem::exists(app.workspace), L"Repeated session close remains empty without saving");
		} catch (const std::exception& e) { ++failures; report << L"FAIL exception " << WideError(e.what()) << L'\n'; }
		report << L"TOTAL FAILURES: " << failures << L'\n'; DestroyWindow(window); return failures ? 1 : 0;
	}
	app.StartSources();
	ShowWindow(window, show); UpdateWindow(window);
	try { std::vector<std::filesystem::path> files; for (const auto& argument : arguments) { if (argument.rfind(L"/StiEvent:", 0) == 0) app.HandleEvent(argument.substr(10)); else if (argument.rfind(L"/StiDevice:", 0) != 0) files.emplace_back(argument); } if (!files.empty()) app.ImportFiles(files); } catch (const std::exception& e) { Error(window, e); }
	ACCEL entries[]{{FCONTROL | FVIRTKEY, 'O', Import}, {FCONTROL | FVIRTKEY, 'S', Save}, {FCONTROL | FVIRTKEY, 'P', Print}, {FCONTROL | FVIRTKEY, 'X', Cut}, {FCONTROL | FVIRTKEY, 'C', Copy}, {FCONTROL | FVIRTKEY, 'V', Paste}, {FCONTROL | FVIRTKEY, 'A', SelectAll}, {FVIRTKEY, VK_DELETE, Delete}, {FVIRTKEY, VK_F1, Help}, {FVIRTKEY, VK_F2, Scan}, {FVIRTKEY, VK_F11, FullScreen}, {FVIRTKEY, VK_ADD, ZoomIn}, {FVIRTKEY, VK_SUBTRACT, ZoomOut}, {FCONTROL | FVIRTKEY, VK_ADD, ZoomIn}, {FCONTROL | FVIRTKEY, VK_SUBTRACT, ZoomOut}, {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ZoomIn}, {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ZoomOut}, {0, '+', ZoomIn}, {0, '-', ZoomOut}};
	HACCEL accelerators = CreateAcceleratorTableW(entries, static_cast<int>(std::size(entries))); MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) if (!TranslateAcceleratorW(window, accelerators, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } DestroyAcceleratorTable(accelerators); return static_cast<int>(msg.wParam);
}
}
