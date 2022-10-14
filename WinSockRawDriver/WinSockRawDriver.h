#pragma once

#include "WinSockCommon.h"

#define PACKET_QUEUE_MAX_SIZE 1024
#define TAG '4R4W'

// Function declarations.
void WinSockRawDriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS WinSockRawDriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS WinSockRawDriverClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS WinSockRawDriverDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);


// Struct representing a single packet.
typedef struct WinSockRawPacket{
	// List entry to be queued.
	LIST_ENTRY entry;
	// Packet length.
	UINT32 packetLen;
	// Packet data.
	UINT8 packetData;
} WinSockRawPacket;


// Struct carrying some global context.
// Note: at some point, all of this should be configurable via registry/user mode calls.
typedef struct WinSockRawContext {
	// Index of the bound interface.
	UINT32 interfaceIndex = 0;
	// Recv packet queue current size.
	UINT16 packetQueueSize = 0;
	// Recv packet queue max size.
	UINT16 packetQueueMaxSize = PACKET_QUEUE_MAX_SIZE;
	// Recv packet queue spin lock.
	KSPIN_LOCK packetQueueLock;
	// Event signaling if the queue contains data or not.
	KEVENT packetQueueEvent;
	// Recv packet queue head.
	LIST_ENTRY packetQueueHead;
	// FWPS Callout id (to unregister).
	UINT32 sCalloutId = 0;
	// FWPM Callout id (to unregister).
	UINT32 mCalloutId = 0;
	// Handle to filter engine (to unregister).
	HANDLE engineHandle = nullptr;
} WinSockRawContext;
