// Receiver.cpp : Sample client for WinSockRaw that sniffes on the given interface and prints out the packet in hex format.
//

#include <windows.h>

#include <iostream>
#include <string>

#include "winsockraw.h"

// Buffer len (8KB)
#define BUFLEN 8192


void dumpBufasHex(char* buf, UINT32 len) {
    for (UINT32 i = 0; i < len; ++i) {
        printf("%02X ", (UINT8)buf[i]);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: Receiver.exe <n_packets> [interface]" << std::endl;
        return 1;
    }

    int n_packets = atoi(argv[1]);
    UINT32 interfaceIndex = WINSOCKRAW_INTERACE_ANY_INDEX;
    if (argc > 2) {
        interfaceIndex = atoi(argv[2]);
    }

    HANDLE hSocket = SocketRawOpen();
    if (hSocket == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed opening raw socket due to error: " << GetLastError() << std::endl;
        return 1;
    }

    if (!SocketRawBind(hSocket, interfaceIndex)) {
        std::cerr << "Failed binding raw socket due to error: " << GetLastError() << std::endl;
        SocketRawClose(hSocket);
        return 1;
    }

    int i = 0;
    // Allocate buffer of 16KB;
    UINT32 len = BUFLEN;
    char buf[BUFLEN];
    while (i < n_packets) {
        int readBytes = SocketRawRecv(hSocket, buf, len);

        // If we `readBytes` == `len` check if the packet is truncated.
        if (readBytes == len && GetLastError() == WSAEMSGSIZE) {
            std::cerr << "Packet: " << i << " is truncated becuase input buffer was too small" << std::endl;
        }

        // Display hex data.
        std::cout << "Packet: " << i << " len: " << readBytes << std::endl;
        std::cout << "Packet: " << i << " content: " << std::endl;
        dumpBufasHex(buf, readBytes);

        ++i;
    }
    

    SocketRawClose(hSocket);
    return 0;
}
