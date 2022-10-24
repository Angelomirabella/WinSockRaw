#include "pch.h"
#include "WinIoctl.h"
#include "WinSockCommon.h"

#include <ws2def.h>
#include <ws2ipdef.h>

#include <string>

#define WINSOCKRAW_DRIVER_NAME L"WinSockRawDriver.sys"
#define WINSOCKRAW_INTERFACE_ANY_NAME  L"any"


namespace {

// Handle to the DLL module.
HMODULE hModule = nullptr;

// Retrieve the driver path.
BOOL WinSockRawGetDriverPath(PWSTR driverPath) {
    DWORD length = GetModuleFileName(hModule, driverPath, MAX_PATH);

    if (!length) {
        return FALSE;
    }

    std::wstring tmp(driverPath);
    size_t idx = tmp.find_last_of(L'\\');
    if (idx == std::wstring::npos || idx + wcslen(WINSOCKRAW_DRIVER_NAME) > MAX_PATH) {
        return FALSE;
    }
    wcscpy_s(driverPath + idx + 1, MAX_PATH - idx, WINSOCKRAW_DRIVER_NAME);

    return TRUE;
}

// Install the driver.
BOOL WinSockRawInstall() {
    BOOL res = FALSE;
    SC_HANDLE hManager = nullptr;
    SC_HANDLE hService = nullptr;
    wchar_t driverPath[MAX_PATH + 1];

    // Prevent multiple process to try to install the driver at the same time.
    HANDLE hMutex = CreateMutex(nullptr, FALSE, L"WinSockRawMutex");
    if (hMutex == nullptr) {
        return FALSE;
    }

    DWORD result = WaitForSingleObject(hMutex, INFINITE);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
        // Something is off, fail.
        return FALSE;
    }

    do {
        // Open the service manager.
        hManager = OpenSCManager(nullptr, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
        if (hManager == nullptr) {
            break;
        }

        // Check if the service exist, and try to create it if it does not.
        hService = OpenService(hManager, WINSOCKRAW_DEVICE_NAME, SERVICE_ALL_ACCESS);
        if (hService != nullptr) {
            break;
        }

        // Get driver path.
        if (!WinSockRawGetDriverPath(driverPath)) {
            break;
        }

        // Create the service.
        hService = CreateService(hManager, WINSOCKRAW_DEVICE_NAME, WINSOCKRAW_DEVICE_NAME, SERVICE_ALL_ACCESS, 
                                 SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, 
                                 driverPath, nullptr, nullptr, nullptr, nullptr, nullptr);
        
        if (hService == nullptr && GetLastError() == ERROR_SERVICE_EXISTS) {
            hService = OpenService(hManager, WINSOCKRAW_DEVICE_NAME, SERVICE_ALL_ACCESS);
        }

        res = TRUE;
    } while (FALSE);

    if (hService != nullptr) {
        // Try to start the service. 
        if (!StartService(hService, 0, nullptr) &&
            GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            res = FALSE;
        }

        // Mark service for deletion so the driver is unloaded when the handle is closed.
        DeleteService(hService);
        CloseServiceHandle(hService);
    }
    if (hManager != nullptr) {
        CloseServiceHandle(hManager);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    
    return res;
}

}  //  namespace

// DLL entrypoint.
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);

            // Just store the module handle.
            hModule = module;
            break;
        default:
            break;
    }

    return TRUE;
}

/** \brief Open an handle to a raw socket.
  *
  * \return HANDLE          The handle to the raw socket on success or nullptr on error.
  */
HANDLE SocketRawOpen() {
    // Try to open the device driver.
    HANDLE hSocket = ::CreateFile(L"\\\\.\\" WINSOCKRAW_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (hSocket == INVALID_HANDLE_VALUE) {
        // Check if the call failed because the device is not installed yet, if so try to 
        // install it.
        DWORD error = GetLastError();

        if (error != ERROR_PATH_NOT_FOUND && error != ERROR_FILE_NOT_FOUND) {
            // Open failed not because the driver wasn't found.
            return INVALID_HANDLE_VALUE;
        }

        if (!WinSockRawInstall()) {
            return INVALID_HANDLE_VALUE;
        }

        // Try to reopen the device driver.
        hSocket = ::CreateFile(L"\\\\.\\" WINSOCKRAW_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }

    return hSocket;
}

/** \brief Bind the raw socket to an interface.
  *        Notice this method needs to be invoked also when binding to the `any`
  *        interface by providing `WINSOCKRAW_INTERACE_ANY_INDEX` as `interfaceIndex`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param interfaceIndex   The network interface index.
  * \return BOOL            TRUE on success, FALSE on failure (access error with `GetLastError`).
  */
BOOL SocketRawBind(HANDLE hSocket, UINT32 interfaceIndex) {
    if (hSocket == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    // Send `BIND` command to driver.
    if (!DeviceIoControl(hSocket, IOCTL_WINSOCKRAW_BIND, &interfaceIndex, sizeof(UINT32), nullptr, 0, nullptr, nullptr)) {
        return FALSE;
    }

    return TRUE;
}


/** \brief Read a frame from a raw socket.
  *        Notice that if the input buffer is too small to contain the frame, this will be
  *        truncated and `GetLastError` will return `WSAEMSGSIZE`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param buf              The input buffer.
  * \param len              The length of the input buffer.
  * \return int             The number of received bytes or 0 on failure (access error with `GetLastError`).
  */
int SocketRawRecv(HANDLE hSocket, char* buf, UINT32 len) {
    if (hSocket == INVALID_HANDLE_VALUE || buf == nullptr || len == 0) {
        return 0;
    }

    DWORD receivedBytes;
    if (!DeviceIoControl(hSocket, IOCTL_WINSOCKRAW_READ, nullptr, 0, buf, len, &receivedBytes, nullptr)) {
        if (GetLastError() == ERROR_INVALID_USER_BUFFER) {
            // Message is truncated.
            SetLastError(WSAEMSGSIZE);
            return len;
        }
    }

    return receivedBytes;
}

/** \brief Inject a frame in a raw socket.
  *        Notice that if the input buffer is too small to contain the frame, this will be
  *        truncated and `GetLastError` will return `WSAEMSGSIZE`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param buf              The input buffer.
  * \param len              The length of the input buffer.
  * \return int             The number of injected bytes. If this number differes from the buffer length
  *						    the method failed (access error with `GetLastError`).
  */
int SocketRawSend(HANDLE hSocket, char* buf, UINT32 len) {
    if (hSocket == INVALID_HANDLE_VALUE || buf == nullptr || len == 0) {
        return 0;
    }

    DWORD sentBytes;
    if (!DeviceIoControl(hSocket, IOCTL_WINSOCKRAW_WRITE, nullptr, 0, buf, len, &sentBytes, nullptr)) {
        printf("Failed send %08X", GetLastError());
    }

    return sentBytes;
}

/** \brief Close an handle to a raw socket.
  *
  * \param hSocket          The handle to the raw socket.
  */
VOID SocketRawClose(HANDLE hSocket) {
    hModule = nullptr;

    if (hSocket == INVALID_HANDLE_VALUE) {
        return;
    }

    CloseHandle(hSocket);

    // Stop the service.
    SC_HANDLE hManager = nullptr;
    SC_HANDLE hService = nullptr;
    BOOL res = FALSE;

    do {
        hManager = OpenSCManager(nullptr, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
        if (hManager == nullptr) {
            break;
        }

        hService = OpenService(hManager, WINSOCKRAW_DEVICE_NAME, SERVICE_ALL_ACCESS);
        if (hService == nullptr) {
            break;
        }

        SERVICE_STATUS_PROCESS ssp;
        DWORD bytesNeeded;
        if (!QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp,
                                  sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
            break;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED || ssp.dwCurrentState == SERVICE_STOP_PENDING) {
            // Nothing to do.
            break;
        }

        // Stop the service.
        if (!ControlService(hService, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp)) {
            break;
        }

        res = TRUE;
    } while (FALSE);
    
    if (res == FALSE) {
        printf("Failed stopping service, please stop manually running `sc stop winsockraw`\n");
    }

    if (hManager) {
        CloseServiceHandle(hManager);
    }
    if (hService) {
        CloseServiceHandle(hService);
    }
    return;
}