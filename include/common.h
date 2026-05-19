#pragma once

#include <windows.h>

namespace pract2 {

inline constexpr wchar_t kServiceName[] = L"Pract2Service";
inline constexpr wchar_t kServiceDisplayName[] = L"Pract2 Service";
inline constexpr wchar_t kTrayAppExeName[] = L"TrayWin32App.exe";
inline constexpr wchar_t kServiceExeName[] = L"Pract2Service.exe";
inline constexpr wchar_t kRpcEndpoint[] = L"Pract2ServiceRpc";
inline constexpr wchar_t kLogFolderName[] = L"Pract2Service";
inline constexpr DWORD kRpcErrUnauthorized = 0x2001;
inline constexpr DWORD kRpcErrNoLicense = 0x2002;
inline constexpr DWORD kRpcErrInvalidArgument = 0x2003;
inline constexpr DWORD kRpcErrStopCancelled = 0x2004;
inline constexpr wchar_t kValidActivationCode[] = L"PRACT2-DEMO-KEY";

} // namespace pract2
