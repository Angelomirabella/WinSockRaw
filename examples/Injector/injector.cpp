// Injector.cpp : Sample client for WinSockRaw that reads packets from a PCAP and injects them in 
// the specified interface.
// Notice: the PCAP parsing is very lightweight and not robust, no error checks is performed. 

#include <windows.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "winsockraw.h"


void dumpBufasHex(char* buf, UINT32 len) {
    for (UINT32 i = 0; i < len; ++i) {
        printf("%02X ", (UINT8)buf[i]);
    }
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: Injector.exe <interface_idx> <filename>" << std::endl;
        return 1;
    }

    UINT32 interfaceIndex = atoi(argv[1]);
    std::ifstream pcapFile(argv[2], std::ios::binary);

    if (!pcapFile.is_open()) {
        std::wcerr << "Failed to open file: " << argv[2] << std::endl;
        return 1;
    }

    // Skip the file header.
    pcapFile.ignore(24);
    
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
    while (pcapFile.good()) {
        // Read pcap header (ignore everything but the captured length).
        pcapFile.ignore(8);
        UINT32 packetLen;
        pcapFile.read(reinterpret_cast<char*>(&packetLen), 4);
        pcapFile.ignore(4);

        std::vector<char> packetData(packetLen);
        pcapFile.read(packetData.data(), packetLen);
        if (pcapFile.eof()) {
            break;
        }

        std::cout << "Injecting Packet: " << i++ << std::endl;
        dumpBufasHex(packetData.data(), packetLen);
        int sent = SocketRawSend(hSocket, packetData.data(), packetLen);
        if (sent != packetLen) {
            std::cerr << "Error injecting packet: sent " << sent << " bytes out of " << packetLen << std::endl;
        }
    }

    SocketRawClose(hSocket);
    return 0;
}

