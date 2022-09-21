#include "pch.h"

// Open a raw socket.
HANDLE SocketRawOpen(UINT8 flags) {
    return INVALID_HANDLE_VALUE;
}

// Bind a socket to a network interface.
//BOOL SocketRawBind(HANDLE hSocket, const WCHAR* interface) {
//    return TRUE;
//}

// Close a socket.
VOID SocketRawClose(HANDLE hSocket) {
    return;
}