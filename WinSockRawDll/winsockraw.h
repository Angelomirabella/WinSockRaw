#pragma once

#ifdef WINSOCKRAWDLL_EXPORTS
#define WINSOCKRAW_API __declspec(dllexport)
#else
#define WINSOCKRAW_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Open a raw socket.
WINSOCKRAW_API HANDLE SocketRawOpen(UINT8 flags);

// Bind a socket to a network interface.
// WINSOCKRAW_API BOOL SocketRawBind(HANDLE hSocket, const WCHAR* interface);

// Close a socket.
WINSOCKRAW_API VOID SocketRawClose(HANDLE hSocket);

#ifdef __cplusplus
}
#endif
