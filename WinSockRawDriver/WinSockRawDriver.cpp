#include <ntddk.h>


// Function declarations.
void WinSockRawDriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS WinSockRawDriverCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS WinSockRawDriverRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS WinSockRawDriverWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// Driver entrypoint.
extern "C" NTSTATUS  DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\winsockraw");
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\winsockraw");

	do {
		// Create a device object.	
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &deviceObject);
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
	} while (false);

	// Set routines.
	DriverObject->DriverUnload = WinSockRawDriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = WinSockRawDriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = WinSockRawDriverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = WinSockRawDriverRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = WinSockRawDriverWrite;

    return status;
}

// Driver unload routine.
void WinSockRawDriverUnload(PDRIVER_OBJECT DriverObject) {
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\winsockraw");

	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}

// Creation/Closing routine (unused).
NTSTATUS WinSockRawDriverCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	return STATUS_SUCCESS;
}

// Frame reading routine.
NTSTATUS WinSockRawDriverRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	return STATUS_SUCCESS;
}

// Frame injection routine.
NTSTATUS WinSockRawDriverWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Irp);

	return STATUS_SUCCESS;
}
