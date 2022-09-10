// Main program for Generic driver
// Copyright (C) 1999 by Walter Oney
// All rights reserved

#include "stddcls.h"
#include "driver.h"
#include "version.h"

#include <initguid.h>
DEFINE_GUID(GUID_AUTOLAUNCH_NOTIFY, 0xba4ec740, 0xda8b, 0x11d2, 0x81, 0xb5, 0x0, 0xc0, 0x4f, 0xa3, 0x30, 0xa6);
#include "GenericPower.h"

NTSTATUS OnRequestComplete(PDEVICE_OBJECT junk, PIRP Irp, PKEVENT pev);

BOOLEAN win98 = FALSE;

///////////////////////////////////////////////////////////////////////////////
// DriverEntry is not used in a kernel-mode DLL such as this one. It's only here
// to satisfy the build script

//#pragma PAGEDCODE
//
//extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
//	PUNICODE_STRING RegistryPath)
//	{							// DriverEntry
//	(void)DriverObject;
//	(void)RegistryPath;
//	return STATUS_SUCCESS;		// never entered, actually
//	}							// DriverEntry

///////////////////////////////////////////////////////////////////////////////
// DllInitialize is called when this module is first loaded. This function
// was added in version 1.7, part of SP-9. Note that RegistryPath just names
// a bogus service key that doesn't necessarily exist -- HKLM\...\Services\Generic.

#ifndef WIN98SAFE				// see comment in driver.h

#pragma PAGEDCODE

extern "C" NTSTATUS DllInitialize(PUNICODE_STRING RegistryPath)
	{							// DllInitialize
	(void)RegistryPath;
	KdPrint(("GENERIC - DllInitialize\n"));

	// Determine addresses of version-dependent routines

	//#define GETADDRESS(n) \
	//	RtlInitUnicodeString(&name, L#n); \
	//	p##n = (t##n) MmGetSystemRoutineAddress(&name); 

	//UNICODE_STRING name;

	return STATUS_SUCCESS;
	}							// DllInitialize

///////////////////////////////////////////////////////////////////////////////
// DllUnload is called when all references to this module disappear. This
// function was added in version 1.7, part of SP-9

#pragma PAGEDCODE

extern "C" NTSTATUS DllUnload()
	{							// DllUnload
	KdPrint(("GENERIC - DllUnload\n"));
	return STATUS_SUCCESS;
	}							// DllUnload

#endif // WIN98SAFE

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE	

GENERICAPI VOID GENERIC_EXPORT CleanupGenericExtension(PGENERIC_EXTENSION pdx)
	{							// CleanupGenericExtension
	if (pdx->queues)
		ExFreePool(pdx->queues);
	pdx->queues = NULL;
	pdx->nqueues = 0;
	}							// CleanupGenericExtension

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

NTSTATUS CompleteRequest(PIRP Irp, NTSTATUS status, ULONG_PTR info)
	{							// CompleteRequest
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
	}							// CompleteRequest

// Overloaded variant that doesn't change IoStatus.Information -- use
// this for most PnP requests.

NTSTATUS CompleteRequest(PIRP Irp, NTSTATUS status)
	{							// CompleteRequest
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
	}							// CompleteRequest

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

NTSTATUS ForwardAndWait(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// ForwardAndWait
	PAGED_CODE();
	
	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE) OnRequestComplete,
		(PVOID) &event, TRUE, TRUE, TRUE);

	IoCallDriver(pdx->LowerDeviceObject, Irp);
	KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
	return Irp->IoStatus.Status;
	}							// ForwardAndWait

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

GENERICAPI ULONG GENERIC_EXPORT GenericGetVersion()
	{							// GenericGetVersion
	return (VERMAJOR << 16) | VERMINOR;
	}							// GenericGetVersion

///////////////////////////////////////////////////////////////////////////////
// Portable implementation of MmGetSystemAddressForMdlSafe...

#pragma LOCKEDCODE

GENERICAPI PVOID GENERIC_EXPORT GenericGetSystemAddressForMdl(PMDL mdl)
	{							// GenericGetSystemAddressForMdl
	if (!mdl)
		return NULL;

	CSHORT oldfail = mdl->MdlFlags & MDL_MAPPING_CAN_FAIL;
	mdl->MdlFlags |= MDL_MAPPING_CAN_FAIL;

	PVOID address = MmMapLockedPages(mdl, KernelMode);

	if (!oldfail)
		mdl->MdlFlags &= ~MDL_MAPPING_CAN_FAIL;

	return address;
	}							// GenericGetSystemAddressForMdl

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI ULONG GENERIC_EXPORT GetSizeofGenericExtension()
	{							// GetSizeofGenericExtension
	PAGED_CODE();
	ULONG size = (sizeof(GENERIC_EXTENSION) + 7) & ~7;
	return size;
	}							// GetSizeofGenericExtension

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI NTSTATUS GENERIC_EXPORT InitializeGenericExtension(PGENERIC_EXTENSION pdx, PGENERIC_INIT_STRUCT isp)
	{							// InitializeGenericExtension
	if (isp->Size < FIELD_OFFSET(GENERIC_INIT_STRUCT, Flags)
		|| !isp->DeviceObject
		|| !isp->Ldo
		|| !isp->Pdo
		|| !isp->StartDevice
		|| !isp->StopDevice
		|| !isp->RemoveDevice
		|| isp->DeviceQueue && !isp->StartIo)

		return STATUS_INVALID_PARAMETER;

	RtlZeroMemory(pdx, sizeof(GENERIC_EXTENSION));

	pdx->DeviceObject = isp->DeviceObject;
	pdx->LowerDeviceObject = isp->Ldo;
	pdx->Pdo = isp->Pdo;
	pdx->StartDevice = isp->StartDevice;
	pdx->StopDevice = isp->StopDevice;
	pdx->RemoveDevice = isp->RemoveDevice;

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, OkayToRemove) + sizeof(PQUERYFUNCTION))
		{						// set OkayToStop & OkayToRemove pointers
		pdx->OkayToStop = isp->OkayToStop;
		pdx->OkayToRemove = isp->OkayToRemove;
		}						// set OkayToStop & OkayToRemove pointers

	if (/*(pdx->RemoveLock = isp->RemoveLock)*/isp->RemoveLock)
	{
		pdx->RemoveLock = isp->RemoveLock;
		IoInitializeRemoveLock(pdx->RemoveLock, 0, 0, 0);
	}

	pdx->state = STOPPED;
	
	pdx->devpower = PowerDeviceD0;
	pdx->syspower = PowerSystemWorking;
	POWER_STATE state;
	state.DeviceState = PowerDeviceD0;
	PoSetPowerState(pdx->DeviceObject, DevicePowerState, state);

	// In version 1.3, I added support for multiple IRP queues

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, NumberOfQueues) + sizeof(ULONG)
		&& isp->NumberOfQueues)
		{						// multiple queues
		if (isp->DeviceQueue || isp->StartIo)
			return STATUS_INVALID_PARAMETER;	// can't mix new and old ways of identifying queues
		if (isp->Size < FIELD_OFFSET(GENERIC_INIT_STRUCT, Queues) + isp->NumberOfQueues * 2 * sizeof(PVOID))
			return STATUS_INVALID_PARAMETER;	// init structure not big enough
		for (ULONG i = 0; i < isp->NumberOfQueues; ++i)
			if (!isp->Queues[i].DeviceQueue || !isp->Queues[i].StartIo)
				return STATUS_INVALID_PARAMETER;	// none of the entries can be NULL

		pdx->nqueues = isp->NumberOfQueues;
		pdx->queues = (PDEVQUEUE*) ALLOC_NON_PAGED(isp->NumberOfQueues * sizeof(PDEVQUEUE));
		if (!pdx->queues)
			return STATUS_INSUFFICIENT_RESOURCES;

		for (ULONG i = 0; i < isp->NumberOfQueues; ++i)
			{					// for each queue
			pdx->queues[i] = isp->Queues[i].DeviceQueue;
			InitializeQueue(pdx->queues[i], isp->Queues[i].StartIo);
			}					// for each queue
		}						// multiple queues

	else if (isp->DeviceQueue)
		{						// single queue
		pdx->nqueues = 1;
		pdx->queues = (PDEVQUEUE*) ALLOC_NON_PAGED(sizeof(PDEVQUEUE));
		if (!pdx->queues)
			return STATUS_INSUFFICIENT_RESOURCES;
		pdx->queues[0] = isp->DeviceQueue;
		InitializeQueue(pdx->queues[0], isp->StartIo);
		}						// single queue

	// In version 1.9, I added support for FlushPendingIo.
	// In version 1.10, GetDevicePowerState

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, Queues))
		{						// additional reserved fields
		pdx->FlushPendingIo = isp->FlushPendingIo;
		pdx->GetDevicePowerState = isp->GetDevicePowerState;
		}						// additional reserved fields

	// Capture the mini-driver name for messages. This needs to be in ANSI because
	// unicode conversions at or above DISPATCH_LEVEL are not allowed. In retrospect, I
	// should have made the field in the INIT struct be in ANSI to start with...

	if (!isp->DebugName.Length)
		strcpy(pdx->DebugName, "GENERIC");
	else
		{						// convert debug name
		ANSI_STRING asname = {0, sizeof(pdx->DebugName) - 1, pdx->DebugName};
		RtlUnicodeStringToAnsiString(&asname, &isp->DebugName, FALSE);
		pdx->DebugName[asname.Length] = 0;
		}						// convert debug name

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, Flags) + sizeof(ULONG))
		pdx->Flags = isp->Flags & GENERIC_CLIENT_FLAGS;

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, RestoreDeviceContext) + sizeof(PCONTEXTFUNCTION))
		{						// get power helper functions
		pdx->QueryPower = isp->QueryPower;
		pdx->SaveDeviceContext = isp->SaveDeviceContext;
		pdx->RestoreDeviceContext = isp->RestoreDeviceContext;
		}						// get power helper functions

	if (isp->Size >= FIELD_OFFSET(GENERIC_INIT_STRUCT, PerfBoundary) + sizeof(DEVICE_POWER_STATE))
		pdx->PerfBoundary = isp->PerfBoundary;
	else
		pdx->PerfBoundary = PowerDeviceUnspecified;

	if (pdx->PerfBoundary == PowerDeviceUnspecified)
		pdx->PerfBoundary = PowerDeviceMaximum; // inhibit POWER_SEQUENCE optimization

	// Initialize variables related to asynchrounous IOCTL management. In version 2.0, this
	// is now always done rather than depending on a flag in the init struct.

	InitializeListHead(&pdx->PendingIoctlList);
	pdx->IoctlAbortStatus = 0;
	KeInitializeSpinLock(&pdx->IoctlListLock);

	// Initialize to manage registered device interfaces

	KeInitializeEvent(&pdx->iflock, SynchronizationEvent, TRUE);
	InitializeListHead(&pdx->iflist);

	// Indicate we handle power IRPs at PASSIVE_LEVEL

	pdx->DeviceObject->Flags |= DO_POWER_PAGABLE;

	// If device honors paging-path notifications, initialize a synchronization
	// event in the signalled state to act as a simple mutex (SP-7)

	if (pdx->Flags & GENERIC_USAGE_PAGING)
		KeInitializeEvent(&pdx->evPagingPath, SynchronizationEvent, TRUE);

	// If requested to do so, register an AutoLaunch interface

	if (pdx->Flags & GENERIC_AUTOLAUNCH)
		GenericRegisterInterface(pdx, &GUID_AUTOLAUNCH_NOTIFY);

	// Register a power management interface

	GenericRegisterInterface(pdx, &GUID_GENERIC_POWER);

#ifdef _X86_
	win98 = IsWin98();
#endif

	return STATUS_SUCCESS;
	}							// InitializeGenericExtension

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

NTSTATUS OnRequestComplete(PDEVICE_OBJECT junk, PIRP Irp, PKEVENT pev)
	{							// OnRequestComplete
	(void)junk;
	(void)Irp;
	KeSetEvent(pev, 0, FALSE);
	return STATUS_MORE_PROCESSING_REQUIRED;
	}							// OnRequestComplete

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI BOOLEAN GENERIC_EXPORT IsWin98()
	{							// IsWin98
#ifdef _X86_
	return !IoIsWdmVersionAvailable(1, 0x10);
#else
	return FALSE;
#endif // _X86_
	}							// IsWin98


#pragma LOCKEDCODE				// force inline functions into locked code