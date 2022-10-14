#pragma once

#ifdef WINSOCKRAWDLL_EXPORTS
#define WINSOCKRAW_API __declspec(dllexport)
#else
#define WINSOCKRAW_API __declspec(dllimport)
#endif

// Interface index any.
#define WINSOCKRAW_INTERACE_ANY_INDEX   0xFFFFFFFF


#ifdef __cplusplus
extern "C" {
#endif

// Open a raw socket.
WINSOCKRAW_API HANDLE SocketRawOpen();

// Bind a socket to a network interface.
WINSOCKRAW_API BOOL SocketRawBind(HANDLE hSocket, UINT32 interfaceIndex);

// Read from a socket.
WINSOCKRAW_API int SocketRawRecv(HANDLE hSocket, char *buf, UINT32 len);

// Close a socket.
WINSOCKRAW_API VOID SocketRawClose(HANDLE hSocket);

#ifdef __cplusplus
}
#endif
