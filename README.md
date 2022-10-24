# WinSockRaw

WDM based WFP callout driver and corresponding user mode dll that allow to receive/inject L2 frames from Windows network interfaces.

This repo contains a .sln file that builds the driver the dll and two examples.

## Usage
Link your program to WinSockRawDll. The dll will take care of installing and removing the driver automatically.

WinSockRawDll exposes the following API:

```
/** \brief Open an handle to a raw socket.
  *
  * \return HANDLE          The handle to the raw socket on success or nullptr on error.
  */
HANDLE SocketRawOpen();

/** \brief Bind the raw socket to an interface.
  *        Notice this method needs to be invoked also when binding to the `any`
  *        interface by providing `WINSOCKRAW_INTERACE_ANY_INDEX` as `interfaceIndex`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param interfaceIndex   The network interface index.
  * \return BOOL            TRUE on success, FALSE on failure (access error with `GetLastError`).
  */
BOOL SocketRawBind(HANDLE hSocket, UINT32 interfaceIndex);

/** \brief Read a frame from a raw socket.
  *        Notice that if the input buffer is too small to contain the frame, this will be 
  *        truncated and `GetLastError` will return `WSAEMSGSIZE`.
  *
  * \param hSocket          The handle to the raw socket.
  * \param buf              The input buffer.
  * \param len              The length of the input buffer.
  * \return int             The number of received bytes or 0 on failure (access error with `GetLastError`).
  */
int SocketRawRecv(HANDLE hSocket, char* buf, UINT32 len);

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
int SocketRawSend(HANDLE hSocket, char* buf, UINT32 len);

/** \brief Close an handle to a raw socket.
  *
  * \param hSocket          The handle to the raw socket.
  */
VOID SocketRawClose(HANDLE hSocket);
```

The usual workflow of a client application should be:
 - Get an handle to a raw socket with `SocketRawOpen`
 - Bind to a specific interface or to every interface with `SocketRawBind`.
   Notice that you can retrieve a network interface index by running:
   ```
   netsh interface ipv4 show interfaces
   
   Idx     Met         MTU          State                Name
   ---  ----------  ----------  ------------  ---------------------------
     1          75  4294967295  connected     Loopback Pseudo-Interface 1
     5          25        1500  connected     Ethernet (Kernel Debugger)
   ```
	The index is shown in the first column (Idx).
 - Receive/Inject frames with `SocketRawRecv`/`SocketRawSend`.
 - Close the socket handle with `SocketRawClose`.

## Examples

The `examples` directory contains two examples: Receiver and Injector. Refer to the README of those directories for more info.

## Future Work
Currently planning to add (at some point):
 - Allow `SocketRawRecv` to specify a timeout instead of waiting indefinitely.
 - Add `SocketSetOption` method to allow to configure various options (at the moment eveything is hard coded).

## Disclaimer
WinSockRaw is a project I developed for fun to learn more about Windows internals and drivers.

It is still in a very early development phase. It has been tested (x64 version) on a Windows 10 and Windows 11 machine and seems to be stable (no BSODs)
but I would recommend to install it and try it out in a virtual machine.


