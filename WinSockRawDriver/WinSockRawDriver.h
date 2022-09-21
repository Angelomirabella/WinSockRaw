#pragma once

#define LUID_INTERFACE_ANY    0xFFFFFFFFFFFFFFFF
#define PACKET_QUEUE_MAX_SIZE 1024

#include "WinSockCommon.h"


// Struct carrying some global context.
// Note: at some point, all of this should be configurable via registry/user mode calls.
typedef struct WinSockRawContext {
	// LUID for the 'any' interface (default).
	ULONG64 interfaceLuid = LUID_INTERFACE_ANY;
	// Flags (read|write access).
	UINT8 flags = 0;
	// Recv packet queue max size.
	UINT16 packetQueueMaxSize = PACKET_QUEUE_MAX_SIZE;
} WinSockRawContext;