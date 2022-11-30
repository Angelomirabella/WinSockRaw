#pragma once

#ifdef WINSOCKRAWDLL_EXPORTS
#define WINSOCKRAW_API __declspec(dllexport)
#else
#define WINSOCKRAW_API __declspec(dllimport)
#endif

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers.
#include <windows.h>

// Interface index any.
#define WINSOCKRAW_INTERACE_ANY_INDEX   0xFFFFFFFF


#ifdef __cplusplus
extern "C" {
#endif

/** \brief Open an handle to a raw socket.
  *
  * \return HANDLE          The handle to the raw socket on success or nullptr on error.
  */
WINSOCKRAW_API HANDLE SocketRawOpen();

/** \brief Bind the raw socket to an interface.
  *        Notice this method needs to be invoked also when binding to the `any`
  *        interface by providing `WINSOCKRAW_INTERACE_ANY_INDEX` as `interfaceIndex`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param interfaceIndex   The network interface index.
  * \return BOOL            TRUE on success, FALSE on failure (access error with `GetLastError`).
  */
WINSOCKRAW_API BOOL SocketRawBind(HANDLE hSocket, UINT32 interfaceIndex);

/** \brief Read a frame from a raw socket.
  *        Notice that if the input buffer is too small to contain the frame, this will be 
  *        truncated and `GetLastError` will return `WSAEMSGSIZE`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param buf              The input buffer.
  * \param len              The length of the input buffer.
  * \return int             The number of received bytes or 0 on failure (access error with `GetLastError`).
  */
WINSOCKRAW_API int SocketRawRecv(HANDLE hSocket, char* buf, UINT32 len);

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
WINSOCKRAW_API int SocketRawSend(HANDLE hSocket, char* buf, UINT32 len);

/** \brief Close an handle to a raw socket.
  *
  * \param hSocket          The handle to the raw socket.
  */
WINSOCKRAW_API VOID SocketRawClose(HANDLE hSocket);

#ifdef __cplusplus
}
#endif
