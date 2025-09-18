
#pragma once
#include <windows.h>
#include <string>
#include <string_view>

// Header-only logger & UI message helper to avoid link issues.
static inline unsigned int GetAutoSwapUiMsg()
{
	static UINT s_msg = RegisterWindowMessageW(L"MVR_HDR_AUTOSWAP_UI");
	return s_msg;
}

static inline void AutoSwapLog(const wchar_t* fmt, ...)
{
	static CRITICAL_SECTION s_cs;
	static bool s_init = false;
	static HANDLE s_hFile = nullptr;
	if (!s_init) {
		InitializeCriticalSection(&s_cs);
		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::wstring full(exePath);
		size_t p = full.find_last_of(L"\\/");
		std::wstring dir = (p != std::wstring::npos) ? full.substr(0, p) : L".";
		std::wstring logPath = dir + L"\\MVR_AutoSwap.log";
		s_hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (s_hFile && GetFileSize(s_hFile, nullptr) == 0) {
			const char bom[] = "\xEF\xBB\xBF";
			DWORD w; WriteFile(s_hFile, bom, 3, &w, nullptr);
		}
		s_init = true;
	}
	if (!s_hFile) return;
	EnterCriticalSection(&s_cs);
	wchar_t msg[2048] = {};
	SYSTEMTIME st; GetLocalTime(&st);
	int wrote = swprintf(msg, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap; va_start(ap, fmt);
	if (wrote < 0) wrote = 0;
	_vsnwprintf(msg + wrote, _countof(msg) - wrote - 2, fmt, ap);
	va_end(ap);
	std::wstring line(msg);
	line.append(L"\r\n");
	int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr) - 1;
	if (len > 0) {
		std::string utf8(len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), len + 1, nullptr, nullptr);
		DWORD written = 0; WriteFile(s_hFile, utf8.data(), len, &written, nullptr);
	}
	LeaveCriticalSection(&s_cs);
}
