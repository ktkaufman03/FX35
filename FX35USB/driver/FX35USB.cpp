#include "FX35USB.h"

#include <usbiodef.h>

#include "rw.h"
#include "ezusb.h"
#include "../build_ts.h"

VOID DriverUnload(IN PDRIVER_OBJECT fdo);
NTSTATUS DispatchCreate(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchControl(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchInternalControl(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchPnp(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchPower(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchClose(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchReadWrite(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS DispatchWmi(PDEVICE_OBJECT fdo, PIRP Irp);

UNICODE_STRING servkey;

#pragma PAGEDCODE

static const char build_string[] = DRIVERNAME " - " DRIVER_BUILD_TS;

extern "C" NTSTATUS __stdcall DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{							// DriverEntry
	// Insist that OS support at least the WDM level of the DDK we use
	if (!IoIsWdmVersionAvailable(WDM_MAJORVERSION, WDM_MINORVERSION))
	{
		DRIVER_LOG_ERROR(("Expected version of WDM (%d.%2.2d) not available", WDM_MAJORVERSION, WDM_MINORVERSION));
		return STATUS_UNSUCCESSFUL;
	}
	
	// Save the name of the service key
	servkey.Buffer = (PWSTR)ALLOC_PAGED(RegistryPath->Length + sizeof(WCHAR));
	if (!servkey.Buffer)
	{
		DRIVER_LOG_ERROR(("Unable to allocate %zu bytes for copy of service key name", RegistryPath->Length + sizeof(WCHAR)));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DRIVER_LOG_INFO(("%s", build_string));

	servkey.MaximumLength = RegistryPath->Length + sizeof(WCHAR);
	RtlCopyUnicodeString(&servkey, RegistryPath);
	servkey.Buffer[RegistryPath->Length / 2] = 0;

	DriverObject->DriverUnload = DriverUnload;
	DriverObject->DriverExtension->AddDevice = AddDevice;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = DispatchReadWrite;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = DispatchReadWrite;
	DriverObject->MajorFunction[IRP_MJ_POWER] = DispatchPower;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = DispatchInternalControl;
	DriverObject->MajorFunction[IRP_MJ_PNP] = DispatchPnp;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DispatchWmi;
	return 0;
}

#pragma PAGEDCODE

NTSTATUS DispatchWmi(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchWmi
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(pdx->LowerDeviceObject, Irp);
}							// DispatchWmi

NTSTATUS DispatchCreate(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchCreate
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	NTSTATUS status = STATUS_SUCCESS;
	InterlockedIncrement(&pdx->handles);
	return CompleteRequest(Irp, status, 0);
}							// DispatchCreate

NTSTATUS __stdcall DispatchControl(PDEVICE_OBJECT fdo, PIRP Irp)
{
	PAGED_CODE();
	PDEVICE_EXTENSION DeviceExtension = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	NTSTATUS status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Failed to acquire device removal lock - %08X", status));
		return CompleteRequest(Irp, status, 0);
	}
	status = Ezusb_ProcessIOCTL(fdo, Irp);
	if (status == STATUS_INVALID_PARAMETER)
	{
		DRIVER_LOG_ERROR(("Failed to process control message through Ezusb"));
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

		if (!NT_SUCCESS(status))
		{
			DRIVER_LOG_ERROR(("Lower driver returned failing status %08X", status));
		}
		return status;
	}

	IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
	return CompleteRequest(Irp, status, 0);
}

#pragma LOCKEDCODE

NTSTATUS DispatchInternalControl(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchInternalControl
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	NTSTATUS status = IoAcquireRemoveLock(&pdx->RemoveLock, Irp);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Failed to acquire device removal lock - %08X", status));
		return CompleteRequest(Irp, status, 0);
	}
	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(pdx->LowerDeviceObject, Irp);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Lower driver returned failing status %08X", status));
	}
	IoReleaseRemoveLock(&pdx->RemoveLock, Irp);
	return status;
}							// DispatchInternalControl

#pragma PAGEDCODE

NTSTATUS DispatchPnp(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchPnp
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	return GenericDispatchPnp(pdx->pgx, Irp);
}							// DispatchPnp

NTSTATUS DispatchPower(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchPower
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	return GenericDispatchPower(pdx->pgx, Irp);
}							// DispatchPower

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

VOID DriverUnload(IN PDRIVER_OBJECT DriverObject)
{							// DriverUnload
	(void)DriverObject;
	PAGED_CODE();
	RtlFreeUnicodeString(&servkey);
}							// DriverUnload

///////////////////////////////////////////////////////////////////////////////

#if defined F135
#define DEVICENAME "PAKON135"
#elif defined F235
#define DEVICENAME "LOOPBACK"
#elif defined F335
#define DEVICENAME "PAKONX35"
#else
#error "Not sure what scanner we're compiling for"
#endif

NTSTATUS AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	PAGED_CODE();
	NTSTATUS status;

	// Create a function device object to represent the hardware we're managing.

	PDEVICE_OBJECT fdo;

	ULONG dxsize = (sizeof(DEVICE_EXTENSION) + 7 & ~7);
	ULONG xsize = dxsize + GetSizeofGenericExtension();

	UNICODE_STRING devname;
	RtlInitUnicodeString(&devname, L"\\DosDevices\\" DEVICENAME);

	status = IoCreateDevice(
		DriverObject,
		xsize,
		&devname,
		FILE_DEVICE_USB,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&fdo);
	if (!NT_SUCCESS(status))
	{						// can't create device object
		DRIVER_LOG_ERROR(("IoCreateDevice failed - %X", status));
		return status;
	}						// can't create device object

	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	// From this point forward, any error will have side effects that need to
	// be cleaned up. Using a do-once allows us to modify the program
	// easily without losing track of the side effects.

	do
	{
		pdx->DeviceObject = fdo;
		pdx->Pdo = pdo;

		// Make a copy of the device name

		pdx->devname.Buffer = (WCHAR*)ALLOC_NON_PAGED(devname.MaximumLength);
		if (!pdx->devname.Buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			DRIVER_LOG_ERROR(("Unable to allocate %d bytes for copy of name", devname.MaximumLength));
			break;
		}
		pdx->devname.MaximumLength = devname.MaximumLength;
		RtlCopyUnicodeString(&pdx->devname, &devname);
		
		// Declare the buffering method we'll use for read/write requests
		fdo->Flags |= DO_DIRECT_IO;

		// Link our device object into the stack leading to the PDO
		pdx->LowerDeviceObject = IoAttachDeviceToDeviceStack(fdo, pdo);
		if (!pdx->LowerDeviceObject)
		{					// can't attach								 
			DRIVER_LOG_ERROR(("IoAttachDeviceToDeviceStack failed"));
			status = STATUS_DEVICE_REMOVED;
			break;
		}					// can't attach

		// Set power management flags in the device object

		fdo->Flags |= DO_POWER_PAGABLE;

		// Initialize to use the GENERIC.SYS library

		pdx->pgx = (PGENERIC_EXTENSION)((uintptr_t)pdx + dxsize);

		GENERIC_INIT_STRUCT gis = { sizeof(GENERIC_INIT_STRUCT) };
		gis.Size = sizeof(GENERIC_INIT_STRUCT);
		gis.DeviceObject = fdo;
		gis.Pdo = pdo;
		gis.Ldo = pdx->LowerDeviceObject;
		gis.RemoveLock = &pdx->RemoveLock;
		gis.StartDevice = StartDevice;
		gis.StopDevice = StopDevice;
		gis.RemoveDevice = RemoveDevice;
		gis.StartIo = StartIo;
		gis.DeviceQueue = &pdx->dqReadWrite;
		RtlInitUnicodeString(&gis.DebugName, LDRIVERNAME);
		gis.Flags = GENERIC_SURPRISE_REMOVAL_OK;

		status = InitializeGenericExtension(pdx->pgx, &gis);
		if (!NT_SUCCESS(status))
		{
			DRIVER_LOG_ERROR(("InitializeGenericExtension failed - %X", status));
			break;
		}

		// Clear the "initializing" flag so that we can get IRPs

		fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	}						// finish initialization
	while (FALSE);

	if (!NT_SUCCESS(status))
	{					// need to cleanup
		if (pdx->devname.Buffer)
			RtlFreeUnicodeString(&pdx->devname);
		if (pdx->LowerDeviceObject)
			IoDetachDevice(pdx->LowerDeviceObject);
		IoDeleteDevice(fdo);
	}					// need to cleanup

	return status;
}							// AddDevice

#pragma PAGEDCODE

NTSTATUS DispatchClose(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchClose
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	(void)stack;
	InterlockedDecrement(&pdx->handles);
	return CompleteRequest(Irp, STATUS_SUCCESS, 0);
}							// DispatchClose

#pragma PAGEDCODE

NTSTATUS DispatchReadWrite(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchReadWrite
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	IoMarkIrpPending(Irp);
	StartPacket(&pdx->dqReadWrite, fdo, Irp, OnCancelReadWrite);
	return STATUS_PENDING;
}							// DispatchReadWrite