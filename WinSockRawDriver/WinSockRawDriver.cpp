#include <ntddk.h>

#include "WinSockRawDriver.h"

#include <fwpsk.h>
#include <initguid.h>
#include <fwpmk.h>




namespace {
// Context.
WinSockRawContext context{};

// Pop a packet from the queue.
WinSockRawPacket* WinSockRawQueuePop() {
	PLIST_ENTRY entry = RemoveHeadList(&context.packetQueueHead);
	WinSockRawPacket* packet = CONTAINING_RECORD(entry, WinSockRawPacket, entry);
	return packet;
}

// Cleanup Context.
void WinSockRawContextCleanup() {
	// Unregister callout.
	if (context.mCalloutId && context.engineHandle) {
		FwpmCalloutDeleteById(context.engineHandle, context.mCalloutId);
		context.mCalloutId = 0;
	}

	if (context.engineHandle) {
		FwpmEngineClose(context.engineHandle);
		context.engineHandle = nullptr;
	}

	if (context.sCalloutId) {
		FwpsCalloutUnregisterById(context.sCalloutId);
		context.sCalloutId = 0;
	}

	if (context.netPoolHandle) {
		NdisFreeNetBufferListPool(context.netPoolHandle);
		context.netPoolHandle = nullptr;
	}

	if (context.injectionHandle) {
		FwpsInjectionHandleDestroy(context.injectionHandle);
		context.injectionHandle = nullptr;
	}

	// Cleanup packet queue.
	KLOCK_QUEUE_HANDLE lockHandle;
	KeAcquireInStackQueuedSpinLock(&context.packetQueueLock, &lockHandle);
	while (!IsListEmpty(&context.packetQueueHead)) {
		WinSockRawPacket* packet = WinSockRawQueuePop();
		if (packet) {
			ExFreePoolWithTag(packet, TAG);
		}
	}
	KeClearEvent(&context.packetQueueEvent);
	KeReleaseInStackQueuedSpinLock(&lockHandle);

	context.interfaceIndex = 0;
	context.packetQueueSize = 0;
}

// Callout notify callback (unused).
NTSTATUS WinSockRawNotify(FWPS_CALLOUT_NOTIFY_TYPE notifyType, const GUID* filterKey,
	FWPS_FILTER3* filter) {
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(filterKey);
	UNREFERENCED_PARAMETER(filter);

	return STATUS_SUCCESS;
}

// Callout classify callback (read packets and store them in the queue).
void WinSockRawCalloutClassify(const FWPS_INCOMING_VALUES0* inFixedValues, 
							   const FWPS_INCOMING_METADATA_VALUES0* inMetaValues, void* layerData,
							   const void* classifyContext, const FWPS_FILTER3* filter,
	UINT64 flowContext, FWPS_CLASSIFY_OUT0* classifyOut) {
	UNREFERENCED_PARAMETER(inFixedValues);
	UNREFERENCED_PARAMETER(inMetaValues);
	UNREFERENCED_PARAMETER(classifyContext);
	UNREFERENCED_PARAMETER(filter);
	UNREFERENCED_PARAMETER(flowContext);
	UNREFERENCED_PARAMETER(classifyOut);
	PNET_BUFFER_LIST rawData = (PNET_BUFFER_LIST)layerData;
	PNET_BUFFER netBuffer = NET_BUFFER_LIST_FIRST_NB(rawData);

	// Loop over the NET_BUFFER_LIST and copy over the packets in the queue.
	while (netBuffer) {
		// Skip packet if we reached the maximum size of the queue.
		if (context.packetQueueSize == context.packetQueueMaxSize) {
			KdPrint(("Packet queue is full, skipping packet\n"));
			netBuffer = netBuffer->Next;
			continue;
		}

		// Allocate packet.
		UINT32 packetLen = NET_BUFFER_DATA_LENGTH(netBuffer);
		PVOID data;
		WinSockRawPacket *packet = (WinSockRawPacket*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(WinSockRawPacket) + packetLen, TAG);
		if (!packet) {
			KdPrint(("Failed to allocate packet in callout Classify, probably not enough resources\n"));
			return;
		}
		packet->packetLen = packetLen;
		
		// Retrieve data.
		data = NdisGetDataBuffer(netBuffer, packetLen, &packet->packetData, 1, 0);
		if (!data) {
			KdPrint(("Failed to retrieve packet data in callout Classify, probably not enough resources\n"));
			ExFreePoolWithTag(packet, TAG);
			return;
		}

		// If the pointer is part of the NET_BUFFER Mdl we need to copy over the data.
		if (data != &packet->packetData) {
			RtlCopyMemory(&packet->packetData, data, packetLen);
		}

		// Insert in the queue (and transfer ownership).
		KLOCK_QUEUE_HANDLE lockHandle;
		KeAcquireInStackQueuedSpinLockAtDpcLevel(&context.packetQueueLock, &lockHandle);
		InsertTailList(&context.packetQueueHead, &packet->entry);
		
		// If the queue was empty, signal the event.
		if (context.packetQueueSize == 0) {
			KeSetEvent(&context.packetQueueEvent, 0, FALSE);
		}
		
		++context.packetQueueSize;
		KeReleaseInStackQueuedSpinLockFromDpcLevel(&lockHandle);

		netBuffer = netBuffer->Next;
	}
	
}

// Register callouts with the filter engine.
bool WinSockRawBindInterface(PDEVICE_OBJECT DeviceObject, UINT32* interfaceIndex) {
	bool res = false;
	FWPS_CALLOUT sCallout; 
	FWPM_CALLOUT mCallout;

	if (*interfaceIndex == 0) {
		return false;
	}

	context.interfaceIndex = *interfaceIndex;

	// Initialize callouts.
	RtlZeroMemory(&sCallout, sizeof(sCallout));
	ExUuidCreate(&sCallout.calloutKey);
	sCallout.notifyFn = WinSockRawNotify;
	sCallout.classifyFn = WinSockRawCalloutClassify;

	RtlZeroMemory(&mCallout, sizeof(mCallout));
	mCallout.calloutKey = sCallout.calloutKey;
	mCallout.displayData.name = L"WinSockRawMCallout";
	mCallout.applicableLayer = FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE;

	do {
		// Register callout.
		NTSTATUS status = FwpsCalloutRegister(DeviceObject, &sCallout, &context.sCalloutId);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to register callout (0x%08X)\n", status));
			return false;
		}

		// Add mCallout and filter.
		FWPM_FILTER filter;
		status = FwpmEngineOpen(nullptr, RPC_C_AUTHN_DEFAULT, nullptr, nullptr, &context.engineHandle);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to open filter engine: (0x%08X)\n", status));
			break;
		}

		status = FwpmTransactionBegin(context.engineHandle, 0);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to start transaction in the filter engine: (0x%08X)\n", status));
			break;
		}

		status = FwpmCalloutAdd(context.engineHandle, &mCallout, NULL, &context.mCalloutId);
		if (!NT_SUCCESS(status)) {
			FwpmTransactionAbort(context.engineHandle);
			KdPrint(("Failed to add mCallout: (0x%08X)\n", status));
			break;
		}

		// Setup filter.
		RtlZeroMemory(&filter, sizeof(FWPM_FILTER));
		filter.layerKey = FWPM_LAYER_INBOUND_MAC_FRAME_NATIVE;
		filter.displayData.name = L"WinSockRawFilter";
		filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
		filter.action.calloutKey = mCallout.calloutKey;
		filter.weight.type = FWP_EMPTY;
		filter.rawContext = (UINT64)&context;
		filter.numFilterConditions = 0;

		FWPM_FILTER_CONDITION filterCondition;
		if (context.interfaceIndex != WINSOCKRAW_INTERACE_ANY_INDEX) {
			// We are sniffing on a single interface, add filter condition.
			RtlZeroMemory(&filterCondition, sizeof(FWPM_FILTER_CONDITION));
			filterCondition.fieldKey = FWPM_CONDITION_INTERFACE_INDEX;
			filterCondition.matchType = FWP_MATCH_EQUAL;
			filterCondition.conditionValue.type = FWP_UINT32;
			filterCondition.conditionValue.uint32 = context.interfaceIndex;

			filter.filterCondition = &filterCondition;
			filter.filterCondition = &filterCondition;
			filter.numFilterConditions = 1;
		}

		// Add filter.
		status = FwpmFilterAdd(context.engineHandle, &filter, NULL, NULL);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to register filter in the filter engine (0x%08X)\n", status));
			FwpmTransactionAbort(context.engineHandle);
			break;
		}

		// Commit transaction.
		status = FwpmTransactionCommit(context.engineHandle);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to commit transaction (0x%08X)\n", status));
			FwpmTransactionAbort(context.engineHandle);
			break;
		}

		KdPrint(("Filter registered successfully\n"));
		res = true;
	} while (false);

	// Cleanup in case of failure.
	if (!res) {
		FwpsCalloutUnregisterById(context.sCalloutId);
		context.sCalloutId = 0;
		context.interfaceIndex = 0;
		
		if (context.mCalloutId && context.engineHandle) {
			FwpmCalloutDeleteById(context.engineHandle, context.mCalloutId);
			context.mCalloutId = 0;
		}

		if (context.engineHandle) {
			FwpmEngineClose(context.engineHandle);
			context.engineHandle = nullptr;
		}
	}

	return res;
}

void NTAPI WinSockRawInjectCompleteCallback(PVOID callbackContext, PNET_BUFFER_LIST netBuffers, 
											BOOLEAN dispatchLevel) {
	UNREFERENCED_PARAMETER(callbackContext);
	UNREFERENCED_PARAMETER(dispatchLevel);

	PNET_BUFFER netBuffer = NET_BUFFER_LIST_FIRST_NB(netBuffers);
	PMDL mdl = NET_BUFFER_FIRST_MDL(netBuffer);
	IoFreeMdl(mdl);
	FwpsFreeNetBufferList(netBuffers);
}

NTSTATUS WinSockRawInjectFrame(UINT8* buffer, UINT32 bufLen) {
	NTSTATUS status = STATUS_SUCCESS;
	PMDL mdl = nullptr;
	PNET_BUFFER_LIST netBuffers = nullptr;

	do {
		mdl = IoAllocateMdl(buffer, bufLen, FALSE, FALSE, nullptr);
		if (!mdl) {
			// Probably not enough resources.
			KdPrint(("Failed to allocate MDL to inject frame of length %u\n", bufLen));
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}
		MmBuildMdlForNonPagedPool(mdl);

		status = FwpsAllocateNetBufferAndNetBufferList(context.netPoolHandle, 0, 0, mdl, 0, bufLen, &netBuffers);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to allocate NET_BUFFER_LIST to inject frame of length %u\n", bufLen));
			break;
		}

		// Inject frame.
		status = FwpsInjectMacSendAsync(context.injectionHandle, nullptr, 0, FWPS_LAYER_INBOUND_MAC_FRAME_NATIVE,
									    context.interfaceIndex, 0, netBuffers, WinSockRawInjectCompleteCallback, nullptr);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to inject frame of length %u\n", bufLen));
			break;
		}
	} while (FALSE);

	if (!NT_SUCCESS(status)) {
		// Cleanup.
		if (mdl) {
			IoFreeMdl(mdl);
		}

		if (netBuffers) {
			FwpsFreeNetBufferList(netBuffers);
		}
	}

	return status;
}

}  // namespace.

// Driver entrypoint.
extern "C" NTSTATUS  DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\" WINSOCKRAW_DEVICE_NAME);
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\" WINSOCKRAW_DEVICE_NAME);

	do {
		// Create a device object.	
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, TRUE, &deviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to create device (0x%08X)\n", status));
			break;
		}

		deviceObject->Flags |= DO_DIRECT_IO;

		// Create symbolic link.
		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to create sym link (0x%08X)\n", status));
			break;
		}

		// Inititalize context.
		KeInitializeEvent(&context.packetQueueEvent, NotificationEvent, FALSE);
		KeInitializeSpinLock(&context.packetQueueLock);
		InitializeListHead(&context.packetQueueHead);

		NET_BUFFER_LIST_POOL_PARAMETERS netPoolParameters;

		RtlZeroMemory(&netPoolParameters, sizeof(netPoolParameters));
		netPoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
		netPoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
		netPoolParameters.Header.Size = sizeof(netPoolParameters);
		netPoolParameters.fAllocateNetBuffer = TRUE;
		netPoolParameters.PoolTag = TAG;
		netPoolParameters.DataSize = 0;

		context.netPoolHandle = NdisAllocateNetBufferListPool(nullptr, &netPoolParameters);
		if (!context.netPoolHandle) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			KdPrint(("Failed to allocate NET_BUFFER_LIST pool"));
			break;
		}

		// Injection handle (might move this later).
		status = FwpsInjectionHandleCreate(AF_UNSPEC, FWPS_INJECTION_TYPE_L2, &context.injectionHandle);
		if (!NT_SUCCESS(status)) {
			KdPrint(("Failed to allocate injection handle"));
			break;
		}
	} while (false);

	if (!NT_SUCCESS(status)) {
		WinSockRawDriverUnload(DriverObject);
		return status;
	}

	// Set routines.
	DriverObject->DriverUnload = WinSockRawDriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = WinSockRawDriverCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = WinSockRawDriverClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WinSockRawDriverDeviceControl;

	return status;
}

// Driver unload routine.
void WinSockRawDriverUnload(PDRIVER_OBJECT DriverObject) {
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\" WINSOCKRAW_DEVICE_NAME);

	WinSockRawContextCleanup();
	IoDeleteSymbolicLink(&symLink);

	if (DriverObject->DeviceObject) {
		IoDeleteDevice(DriverObject->DeviceObject);
		DriverObject->DeviceObject = nullptr;
	}
}

// Creation routine (unused).
NTSTATUS WinSockRawDriverCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

// Closing routine.
NTSTATUS WinSockRawDriverClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);

	WinSockRawContextCleanup();

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

// Main routine (binding/reading/writing).
NTSTATUS WinSockRawDriverDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	auto irpSp = IoGetCurrentIrpStackLocation(Irp);
	auto& deviceIoControl = irpSp->Parameters.DeviceIoControl;
	auto status = STATUS_SUCCESS;
	auto count = 0;

	switch (deviceIoControl.IoControlCode) {
		case IOCTL_WINSOCKRAW_BIND: {
			// If we are already bound to an interface, we fail.
			if (context.interfaceIndex) {
				status = STATUS_DEVICE_ALREADY_ATTACHED;
				break;
			}

			auto index = (UINT32*)Irp->AssociatedIrp.SystemBuffer;
			if (!index) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			if (!WinSockRawBindInterface(DeviceObject, index)) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			count = deviceIoControl.InputBufferLength;
			break;
		}
		case IOCTL_WINSOCKRAW_READ: {
			// If we are not bound to an interface yet, we fail.
			if (!context.interfaceIndex) {
				status = STATUS_REQUEST_OUT_OF_SEQUENCE;
				break;
			}

			auto bufLen = deviceIoControl.OutputBufferLength;
			if (!bufLen) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			// Retrieve buffer.
			PVOID buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
			if (!buffer) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			// Wait until a packet is available and retrieve it.
			auto ret = KeWaitForSingleObject(&context.packetQueueEvent, Executive, KernelMode, FALSE, nullptr);
			if (!NT_SUCCESS(ret)) {
				status = STATUS_ABANDONED_WAIT_0;
				break;
			}

			KLOCK_QUEUE_HANDLE lockHandle;
			KeAcquireInStackQueuedSpinLock(&context.packetQueueLock, &lockHandle);
			WinSockRawPacket* packet = WinSockRawQueuePop();
			--context.packetQueueSize;

			if (context.packetQueueSize == 0) {
				// Reset the event.
				KeClearEvent(&context.packetQueueEvent);
			}

			KeReleaseInStackQueuedSpinLock(&lockHandle);

			if (!packet) {
				status = STATUS_DATA_ERROR;
				break;
			}

			bool truncated = packet->packetLen > bufLen;
			UINT32 readLen = packet->packetLen;

			if (truncated) {
				status = STATUS_INVALID_BUFFER_SIZE;
				readLen = bufLen;
			}

			RtlCopyMemory(buffer, &packet->packetData, readLen);
			ExFreePoolWithTag(packet, TAG);
			count = truncated ? 0 : readLen;
			break;
		}
		case IOCTL_WINSOCKRAW_WRITE: {
			// If we are not bound to an interface, we fail.
			if (!context.interfaceIndex) {
				status = STATUS_REQUEST_OUT_OF_SEQUENCE;
				break;
			}

			// If we are bound to `any` we cannot inject frames.
			if (context.interfaceIndex == WINSOCKRAW_INTERACE_ANY_INDEX) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			auto bufLen = deviceIoControl.OutputBufferLength;
			if (!bufLen) {
				// Complete the request successfully if the buffer has length 0.
				break;
			}

			// Retrieve buffer.
			PVOID buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
			if (!buffer) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			status = WinSockRawInjectFrame((UINT8*)buffer, bufLen);
			if (NT_SUCCESS(status)) {
				// If successfull, we sent the all frame.
				count = bufLen;
			}
			break;
		}
		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}


	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}
