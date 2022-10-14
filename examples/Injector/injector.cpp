// Injector.cpp : Sample client for WinSockRaw that sniffes from ine interface and injects them in anothe one.
//

#include <windows.h>
#include "winsockraw.h"


int main() 
{
    HANDLE hSocket = SocketRawOpen();
    return 0;
}

