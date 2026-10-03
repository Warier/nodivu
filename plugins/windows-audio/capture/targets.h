#pragma once
#include "process_capture.h"
#include <string>
#include <vector>
namespace nodivu::capture {
struct Application { Target identity; std::wstring executable; };
std::wstring executable_path(DWORD pid);
bool same_path(const std::wstring& a, const std::wstring& b) noexcept;
// Active/inactive audio sessions on active outputs; exact executable root identity.
HRESULT applications(std::vector<Application>& result) noexcept;
std::string utf8(const std::wstring& value);
std::wstring wide(const char* value);
}
