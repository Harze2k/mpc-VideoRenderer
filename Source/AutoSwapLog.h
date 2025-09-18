#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <cstdarg>

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
	static std::wstring s_lastMessage;
	static DWORD s_lastMessageCount = 0;
	static DWORD s_lastTime = 0;
	
	if (!s_init) {
		InitializeCriticalSection(&s_cs);
		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		std::wstring full(exePath);
		size_t p = full.find_last_of(L"\\/");
		std::wstring dir = (p != std::wstring::npos) ? full.substr(0, p) : L".";
		std::wstring logPath = dir + L"\\MVR_AutoSwap.log";
		
		// Use CREATE_ALWAYS to overwrite the file each time
		s_hFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 
			nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (s_hFile && s_hFile != INVALID_HANDLE_VALUE) {
			const char bom[] = "\xEF\xBB\xBF";
			DWORD w; WriteFile(s_hFile, bom, 3, &w, nullptr);
		}
		s_init = true;
	}
	if (!s_hFile || s_hFile == INVALID_HANDLE_VALUE) return;
	
	EnterCriticalSection(&s_cs);
	
	wchar_t msg[2048] = {};
	va_list ap; va_start(ap, fmt);
	_vsnwprintf_s(msg, _countof(msg) - 2, _TRUNCATE, fmt, ap);
	va_end(ap);
	
	std::wstring currentMessage(msg);
	DWORD currentTime = GetTickCount();
	
	// Check if this is a repeated message within 1 second
	if (currentMessage == s_lastMessage && (currentTime - s_lastTime) < 1000) {
		s_lastMessageCount++;
		LeaveCriticalSection(&s_cs);
		return; // Skip logging repeated messages
	}
	
	// If we had repeated messages, log the count
	if (s_lastMessageCount > 0) {
		SYSTEMTIME st; GetLocalTime(&st);
		wchar_t countMsg[256];
		swprintf_s(countMsg, L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] (Previous message repeated %u times)\r\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, s_lastMessageCount);
		
		int len = WideCharToMultiByte(CP_UTF8, 0, countMsg, -1, nullptr, 0, nullptr, nullptr) - 1;
		if (len > 0) {
			std::string utf8(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, countMsg, -1, utf8.data(), len + 1, nullptr, nullptr);
			DWORD written = 0; WriteFile(s_hFile, utf8.data(), len, &written, nullptr);
		}
		s_lastMessageCount = 0;
	}
	
	// Log the current message
	SYSTEMTIME st; GetLocalTime(&st);
	wchar_t fullMsg[2048];
	int wrote = swprintf_s(fullMsg, _countof(fullMsg), L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
	
	if (wrote > 0) {
		int len = WideCharToMultiByte(CP_UTF8, 0, fullMsg, -1, nullptr, 0, nullptr, nullptr) - 1;
		if (len > 0) {
			std::string utf8(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, fullMsg, -1, utf8.data(), len + 1, nullptr, nullptr);
			DWORD written = 0; WriteFile(s_hFile, utf8.data(), len, &written, nullptr);
			FlushFileBuffers(s_hFile); // Ensure it's written immediately
		}
	}
	
	s_lastMessage = currentMessage;
	s_lastTime = currentTime;
	
	LeaveCriticalSection(&s_cs);
}
