// Plug and Play handlers for Generic driver
// Copyright (C) 1999 by Walter Oney
// All rights reserved

#include "stddcls.h"
#include "driver.h"
#include "GenericPower.h"

NTSTATUS DefaultPnpHandler(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleCancelRemove(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleCancelStop(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleQueryCapabilities(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleQueryRemove(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleQueryState(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleQueryStop(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleRemoveDevice(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleStartDevice(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleStopDevice(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleSurpriseRemoval(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS HandleUsageNotification(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS OnNotificationComplete(PDEVICE_OBJECT fdo, PIRP Irp, PGENERIC_EXTENSION pdx);

VOID AdjustDeviceCapabilities(PDEVICE_CAPABILITIES pdc);
VOID AdjustWakeCapabilities(PGENERIC_EXTENSION pdx, PDEVICE_CAPABILITIES pdc, DEVICE_POWER_STATE dstate);
VOID DeregisterInterface(PGENERIC_EXTENSION pdx, PINTERFACE_RECORD ifp, BOOLEAN NeedRemoveList);
VOID DeregisterAllInterfaces(PGENERIC_EXTENSION pdx);
VOID EnableAllInterfaces(PGENERIC_EXTENSION pdx, BOOLEAN enable);
PINTERFACE_RECORD FindInterfaceRecord(PGENERIC_EXTENSION pdx, const GUID* guid);

#if DBG
VOID ShowResources(IN PCM_PARTIAL_RESOURCE_LIST list);

static char* statenames[] = {
	"STOPPED",
	"WORKING",
	"PENDINGSTOP",
	"PENDINGREMOVE",
	"SURPRISEREMOVED",
	"REMOVED",
	};

static char* sname[] = {
	"PowerSystemUnspecified",
	"PowerSystemWorking",
	"PowerSystemSleeping1",
	"PowerSystemSleeping2",
	"PowerSystemSleeping3",
	"PowerSystemHibernate",
	"PowerSystemShutdown",
	};

static char* dname[] = {
	"PowerDeviceUnspecified",
	"PowerDeviceD0",
	"PowerDeviceD1",
	"PowerDeviceD2",
	"PowerDeviceD3",
	};

#endif

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI NTSTATUS GENERIC_EXPORT GenericDispatchPnp(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// GenericDispatchPnp
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;
	if (pdx->RemoveLock && !NT_SUCCESS(status = IoAcquireRemoveLock(pdx->RemoveLock, Irp)))
		return CompleteRequest(Irp, status);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(stack->MajorFunction == IRP_MJ_PNP);

	static NTSTATUS (*fcntab[])(PGENERIC_EXTENSION pdx, PIRP Irp) = {
		HandleStartDevice,		// IRP_MN_START_DEVICE
		HandleQueryRemove,		// IRP_MN_QUERY_REMOVE_DEVICE
		HandleRemoveDevice,		// IRP_MN_REMOVE_DEVICE
		HandleCancelRemove,		// IRP_MN_CANCEL_REMOVE_DEVICE
		HandleStopDevice,		// IRP_MN_STOP_DEVICE
		HandleQueryStop,		// IRP_MN_QUERY_STOP_DEVICE
		HandleCancelStop,		// IRP_MN_CANCEL_STOP_DEVICE
		DefaultPnpHandler,		// IRP_MN_QUERY_DEVICE_RELATIONS
		DefaultPnpHandler,		// IRP_MN_QUERY_INTERFACE
		HandleQueryCapabilities,// IRP_MN_QUERY_CAPABILITIES
		DefaultPnpHandler,		// IRP_MN_QUERY_RESOURCES
		DefaultPnpHandler,		// IRP_MN_QUERY_RESOURCE_REQUIREMENTS
		DefaultPnpHandler,		// IRP_MN_QUERY_DEVICE_TEXT
		DefaultPnpHandler,		// IRP_MN_FILTER_RESOURCE_REQUIREMENTS
		DefaultPnpHandler,		// 
		DefaultPnpHandler,		// IRP_MN_READ_CONFIG
		DefaultPnpHandler,		// IRP_MN_WRITE_CONFIG
		DefaultPnpHandler,		// IRP_MN_EJECT
		DefaultPnpHandler,		// IRP_MN_SET_LOCK
		DefaultPnpHandler,		// IRP_MN_QUERY_ID
		HandleQueryState,		// IRP_MN_QUERY_PNP_DEVICE_STATE
		DefaultPnpHandler,		// IRP_MN_QUERY_BUS_INFORMATION
		HandleUsageNotification,// IRP_MN_DEVICE_USAGE_NOTIFICATION
		HandleSurpriseRemoval,	// IRP_MN_SURPRISE_REMOVAL
		};

	ULONG fcn = stack->MinorFunction;

	if (fcn >= arraysize(fcntab))
		{						// unknown function
		status = DefaultPnpHandler(pdx, Irp); // some function we don't know about
		if (pdx->RemoveLock)
			IoReleaseRemoveLock(pdx->RemoveLock, Irp);
		return status;
		}						// unknown function

//#if DBG
//	static char* fcnname[] = {
//		"IRP_MN_START_DEVICE",
//		"IRP_MN_QUERY_REMOVE_DEVICE",
//		"IRP_MN_REMOVE_DEVICE",
//		"IRP_MN_CANCEL_REMOVE_DEVICE",
//		"IRP_MN_STOP_DEVICE",
//		"IRP_MN_QUERY_STOP_DEVICE",
//		"IRP_MN_CANCEL_STOP_DEVICE",
//		"IRP_MN_QUERY_DEVICE_RELATIONS",
//		"IRP_MN_QUERY_INTERFACE",
//		"IRP_MN_QUERY_CAPABILITIES",
//		"IRP_MN_QUERY_RESOURCES",
//		"IRP_MN_QUERY_RESOURCE_REQUIREMENTS",
//		"IRP_MN_QUERY_DEVICE_TEXT",
//		"IRP_MN_FILTER_RESOURCE_REQUIREMENTS",
//		"",
//		"IRP_MN_READ_CONFIG",
//		"IRP_MN_WRITE_CONFIG",
//		"IRP_MN_EJECT",
//		"IRP_MN_SET_LOCK",
//		"IRP_MN_QUERY_ID",
//		"IRP_MN_QUERY_PNP_DEVICE_STATE",
//		"IRP_MN_QUERY_BUS_INFORMATION",
//		"IRP_MN_DEVICE_USAGE_NOTIFICATION",
//		"IRP_MN_SURPRISE_REMOVAL",
//		};
//
//	KdPrint(("%s - PNP Request (%s)\n", pdx->DebugName, fcnname[fcn]));
//#endif // DBG

	status = (*fcntab[fcn])(pdx, Irp);
	if (fcn != IRP_MN_REMOVE_DEVICE && pdx->RemoveLock)
		IoReleaseRemoveLock(pdx->RemoveLock, Irp);
	return status;
	}							// GenericDispatchPnp

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI NTSTATUS GENERIC_EXPORT GenericDeregisterInterface(PGENERIC_EXTENSION pdx, const GUID* guid)
	{							// GenericDeregisterInterface
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	NTSTATUS status;
	
	KeEnterCriticalRegion();
	KeWaitForSingleObject(&pdx->iflock, Executive, KernelMode, FALSE, NULL);
	PINTERFACE_RECORD ifp = FindInterfaceRecord(pdx, guid);
	if (ifp)
		{
		DeregisterInterface(pdx, ifp, TRUE);
		status = STATUS_SUCCESS;
		}
	else
		status = STATUS_INVALID_PARAMETER;

	KeSetEvent(&pdx->iflock, EVENT_INCREMENT, FALSE);
	KeLeaveCriticalRegion();
	return status;
	}							// GenericDeregisterInterface

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI NTSTATUS GENERIC_EXPORT GenericEnableInterface(PGENERIC_EXTENSION pdx, const GUID* guid, BOOLEAN enable)
	{							// GenericEnableInterface
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	NTSTATUS status;
	
	KeEnterCriticalRegion();
	KeWaitForSingleObject(&pdx->iflock, Executive, KernelMode, FALSE, NULL);
	PINTERFACE_RECORD ifp = FindInterfaceRecord(pdx, guid);
	if (ifp)
		{						// enable/disable this interface
		if (ifp->enabled != enable)
			IoSetDeviceInterfaceState(&ifp->linkname, enable);
		ifp->enabled = enable;
		status = STATUS_SUCCESS;
		}						// enable/disable this interface
	else
		status = STATUS_INVALID_PARAMETER;

	KeSetEvent(&pdx->iflock, EVENT_INCREMENT, FALSE);
	KeLeaveCriticalRegion();
	return status;
	}							// GenericEnableInterface

///////////////////////////////////////////////////////////////////////////////
// GenericGetDeviceCapabilities (added 2.0) returns the device capabilties.
// If necessary, it retrieves them.

#pragma LOCKEDCODE

GENERICAPI PDEVICE_CAPABILITIES GENERIC_EXPORT GenericGetDeviceCapabilities(PGENERIC_EXTENSION pdx)
	{							// GenericGetDeviceCapabilities
	
	// If necessary, ask the bus driver for this device's capabilities. We must be
	// at PASSIVE_LEVEL to do this (requirement for sending the IRP).

	if (!pdx->GotCapabilities)
		{						// fetch capabilities
		if (KeGetCurrentIrql() != PASSIVE_LEVEL)
			return NULL;

		PDEVICE_CAPABILITIES pdc = (PDEVICE_CAPABILITIES) ALLOC_PAGED(sizeof(DEVICE_CAPABILITIES));
		if (!pdc)
			return NULL;
		//RtlZeroMemory(pdc, sizeof(*pdc));
		pdc->Size = sizeof(*pdc);
		pdc->Version = 1;
		pdc->Address = (ULONG) -1;
		pdc->UINumber = (ULONG) - 1;

		KEVENT event;
		KeInitializeEvent(&event, NotificationEvent, FALSE);

		IO_STATUS_BLOCK iosb;

		PDEVICE_OBJECT tdo = IoGetAttachedDeviceReference(pdx->Pdo);
		PIRP Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, tdo, NULL, 0, NULL, &event, &iosb);
		if (!Irp)
		{
			ExFreePool(pdc);
			ObDereferenceObject(tdo);
			return NULL;
		}
		PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(Irp);
		stack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
		stack->Parameters.DeviceCapabilities.Capabilities = pdc;

		NTSTATUS status = IoCallDriver(tdo, Irp);
		if (status == STATUS_PENDING)
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);

		ExFreePool(pdc);
		ObDereferenceObject(tdo);
		}						// fetch capabilities

	return &pdx->devcaps;
	}							// GenericGetDeviceCapabilities

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

GENERICAPI NTSTATUS GENERIC_EXPORT GenericRegisterInterface(PGENERIC_EXTENSION pdx, const GUID* guid)
	{							// GenericRegisterInterface
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	NTSTATUS status;
	
	KeEnterCriticalRegion();
	KeWaitForSingleObject(&pdx->iflock, Executive, KernelMode, FALSE, NULL);
	PINTERFACE_RECORD ifp = FindInterfaceRecord(pdx, guid);
	if (ifp)
		status = STATUS_INVALID_PARAMETER;
	else
		{						// register new interface
		ifp = (PINTERFACE_RECORD) ALLOC_NON_PAGED(sizeof(INTERFACE_RECORD));
		if (ifp)
			{					// initialize new interface record
			status = IoRegisterDeviceInterface(pdx->Pdo, guid, NULL, &ifp->linkname);
			if (NT_SUCCESS(status))
				{				// interface registered
				ifp->guid = *guid;
				ifp->enabled = FALSE;
				InsertHeadList(&pdx->iflist, &ifp->list);
				}				// interface registered
			else
				ExFreePool(ifp);
			}					// initialize new interface record
		else
			status = STATUS_INSUFFICIENT_RESOURCES;
		}						// register new interface

	KeSetEvent(&pdx->iflock, EVENT_INCREMENT, FALSE);
	KeLeaveCriticalRegion();
	return status;
	}							// GenericRegisterInterface

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

GENERICAPI VOID GENERIC_EXPORT GenericSetDeviceState(PGENERIC_EXTENSION pdx, PNP_DEVICE_STATE pnpstate)
	{							// GenericSetDeviceState
	pdx->pnpstatemask = (pnpstate ^ pdx->pnpstate); // mask for things that changed
	pdx->pnpstate |= pnpstate;

	// Inform PnP manager that our state has changed.

	IoInvalidateDeviceState(pdx->Pdo);
	}							// GenericSetDeviceState

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// AdjustDeviceCapabilities corrects the reported device capabilities to
// workaround the fact that many back-level bus drivers simply don't report
// them correctly. Cf. toaster.sys sample in the DDK and talk given by
// Adrian Oney (no relation) at WinHEC 2002.

#pragma PAGEDCODE

__inline SYSTEM_POWER_STATE& operator++(SYSTEM_POWER_STATE& ss) {ss = (SYSTEM_POWER_STATE) (ss + 1); return ss;}
__inline SYSTEM_POWER_STATE& operator--(SYSTEM_POWER_STATE& ss) {ss = (SYSTEM_POWER_STATE) (ss - 1); return ss;}

VOID AdjustDeviceCapabilities(PGENERIC_EXTENSION pdx, PDEVICE_CAPABILITIES pdc)
	{							// AdjustDeviceCapabilities

	// Make sure that the device state for S0 is D0

	if (pdc->DeviceState[PowerSystemWorking] != PowerDeviceD0)
		{
		DRIVER_LOG_ERROR(("%s - DeviceState[PowerSystemWorking] is D%d instead of D0!\n", pdx->DebugName, pdc->DeviceState[PowerSystemWorking] - 1));
		pdc->DeviceState[PowerSystemWorking] = PowerDeviceD0;
		}

	// Check for silly value of SystemWake

	if (pdc->SystemWake > PowerSystemHibernate)
		{
		DRIVER_LOG_ERROR(("%s - SystemWake has the silly value %s\n", pdx->DebugName, sname[pdc->SystemWake]));
		}

	// Adjust power capabilities to compensate for bus driver written to
	// WDM 1.0 spec. First infer D1 and D2 capabilities from S->D state map.

	for (SYSTEM_POWER_STATE sstate = PowerSystemSleeping1; sstate <= PowerSystemHibernate; ++sstate)
		{					// for each S-state
		if (pdc->DeviceState[sstate] == PowerDeviceD1 && !pdc->DeviceD1)
			{
			//KdPrint(("%s - Inferring DeviceD1 from DeviceState[%s]\n", pdx->DebugName, sname[sstate]));
			pdc->DeviceD1 = TRUE;
			}
		if (pdc->DeviceState[sstate] == PowerDeviceD2 && !pdc->DeviceD2)
			{
			//KdPrint(("%s - Inferring DeviceD2 from DeviceState[%s]\n", pdx->DebugName, sname[sstate]));
			pdc->DeviceD2 = TRUE;
			}
		}					// for each S-state

	// Set the WakeFromDx flags based on the reported DeviceWake state and
	// on the D-state corresponding to the reported SystemWake state

	AdjustWakeCapabilities(pdx, pdc, pdc->DeviceWake);
	if (pdc->SystemWake != PowerSystemUnspecified)
		AdjustWakeCapabilities(pdx, pdc, pdc->DeviceState[pdc->SystemWake]);

	// Make sure the device state for S5 is D3. Do this after adjusting wakeup capabilities
	// because USBHUB uncritically assumes that a device can be in D2 from S1 through SystemWakeup.
	// So if SystemWakeup happens to be S5 because of some ACPI mistake, changing the device
	// state entry too soon would cause us to pick D3 as the DeviceWake value, which is more wrong
	// than what we were starting with...

	if (pdc->DeviceState[PowerSystemShutdown] != PowerDeviceD3)
		{
		DRIVER_LOG_ERROR(("%s - DeviceState[PowerSystemShutdown] is D%d instead of D3!\n", pdx->DebugName, pdc->DeviceState[PowerSystemShutdown] - 1));
		pdc->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
		}

	// Find the deepest D-state from which this device can wake the system

	DEVICE_POWER_STATE wakestate = PowerDeviceUnspecified;	// assume no wakeup capability

	if (pdc->WakeFromD3)
		wakestate = PowerDeviceD3;
	else if (pdc->WakeFromD2)
		wakestate = PowerDeviceD2;
	else if (pdc->WakeFromD1)
		wakestate = PowerDeviceD1;
	else if (pdc->WakeFromD0)
		wakestate = PowerDeviceD0;

	if (pdc->DeviceWake != wakestate)
		{
		DRIVER_LOG_ERROR(("%s - DeviceWake is %s, but should be %s\n", pdx->DebugName, dname[pdc->DeviceWake], dname[wakestate]));
		pdc->DeviceWake = wakestate;
		}

	// If SystemWake is specified, the corresponding D-state had better be at
	// least as powered as the state we just discovered, or else there's a bug
	// in our bus driver. It may just be that we got a bogus SystemWake value,
	// so don't do anything about it.

	if (pdc->SystemWake != PowerSystemUnspecified)
		{
		if (pdc->DeviceState[pdc->SystemWake] > wakestate)
			{
			DRIVER_LOG_ERROR(("%s - DeviceState[SystemWake] is less powered than minimum wake state (%s)!\n", pdx->DebugName, dname[wakestate]));
			}
		}

	// If SystemWake wasn't specified, infer it from the S->D state map by
	// finding the lowest S-state whose D-state is at least as powered as the
	// lowest D-state from which wakeup is possible (I think I got that right...)

	else if (wakestate != PowerDeviceD0 && wakestate != PowerDeviceUnspecified)
		{						// infer system wake state
		for (SYSTEM_POWER_STATE sstate = PowerSystemSleeping3; sstate >= PowerSystemWorking; --sstate)
			{					// for each S-state
			if (pdc->DeviceState[sstate] != PowerDeviceUnspecified
				&& pdc->DeviceState[sstate] <= wakestate)
				{				// found the S-state
				//KdPrint(("%s - Inferring that wakeup from S%d state possible\n", pdx->DebugName, sstate - 1));
				pdc->SystemWake = sstate;
				break;
				}				// found the S-state
			}					// for each S-state
		}						// infer system wake state
	}							// AdjustDeviceCapabilities

///////////////////////////////////////////////////////////////////////////////
// AdjustWakeCapabilities adjusts the wakeup capabilities for a device.	

VOID AdjustWakeCapabilities(PGENERIC_EXTENSION pdx, PDEVICE_CAPABILITIES pdc, DEVICE_POWER_STATE dstate)
	{							// AdjustWakeCapabilities
	(void)pdx;
	switch (dstate)
		{						// select on D-state
	case PowerDeviceUnspecified:
		break;
	case PowerDeviceD0:
		//if (!pdc->WakeFromD0)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring WakeFromD0\n", pdx->DebugName));
		pdc->WakeFromD0 = TRUE;
		break;
	case PowerDeviceD1:
		//if (!pdc->WakeFromD1)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring WakeFromD1\n", pdx->DebugName));
		//if (!pdc->DeviceD1)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring DeviceD1\n", pdx->DebugName));
		pdc->DeviceD1 = TRUE;
		pdc->WakeFromD1 = TRUE;
		break;
	case PowerDeviceD2:
		//if (!pdc->WakeFromD2)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring WakeFromD2\n", pdx->DebugName));
		//if (!pdc->DeviceD2)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring DeviceD2\n", pdx->DebugName));
		pdc->DeviceD2 = TRUE;
		pdc->WakeFromD2 = TRUE;
		break;
	case PowerDeviceD3:
		//if (!pdc->WakeFromD3)
		//	KdPrint(("%s - AdjustWakeCapabilities Inferring WakeFromD3\n", pdx->DebugName));
		pdc->WakeFromD3 = TRUE;
		break;
	default:
		ASSERT(FALSE);
		}						// select on D-state
	}							// AdjustWakeCapabilities

///////////////////////////////////////////////////////////////////////////////

VOID CallStopDevice(PGENERIC_EXTENSION pdx)
	{							// CallStopDevice
	GenericWakeupControl(pdx, CancelWaitWake);
	BOOLEAN oktouch = pdx->HardwareWorking;
	pdx->HardwareWorking = FALSE;
	(*pdx->StopDevice)(pdx->DeviceObject, oktouch);
	}							// CallStopDevice

///////////////////////////////////////////////////////////////////////////////

VOID CallRemoveDevice(PGENERIC_EXTENSION pdx)
	{							// CallRemoveDevice
	(*pdx->RemoveDevice)(pdx->DeviceObject);
	}							// CallRemoveDevice

///////////////////////////////////////////////////////////////////////////////

NTSTATUS DefaultPnpHandler(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// DefaultPnpHandler
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(pdx->LowerDeviceObject, Irp);
	}							// DefaultPnpHandler

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

VOID DeregisterAllInterfaces(PGENERIC_EXTENSION pdx)
	{							// DeregisterAllInterfaces
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	//KdPrint(("%s - Deregistering all interfaces\n", pdx->DebugName));
	
	KeEnterCriticalRegion();
	KeWaitForSingleObject(&pdx->iflock, Executive, KernelMode, FALSE, NULL);

	while (!IsListEmpty(&pdx->iflist))
		{						// for each interface
		PLIST_ENTRY list = RemoveHeadList(&pdx->iflist);
		PINTERFACE_RECORD ifp = CONTAINING_RECORD(list, INTERFACE_RECORD, list);
		DeregisterInterface(pdx, ifp, FALSE);
		}						// for each interface

	KeSetEvent(&pdx->iflock, EVENT_INCREMENT, FALSE);
	KeLeaveCriticalRegion();
	}							// DeregisterAllInterfaces

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

VOID DeregisterInterface(PGENERIC_EXTENSION pdx, PINTERFACE_RECORD ifp, BOOLEAN NeedRemoveList)
	{							// DeregisterInterface
	(void)pdx;
	//KdPrint(("%s - Deregistering interface %wZ\n", pdx->DebugName, &ifp->linkname));
	if (ifp->enabled)
	{
		//KdPrint(("-> Disabling interface\n"));
		IoSetDeviceInterfaceState(&ifp->linkname, FALSE);
	}
	RtlFreeUnicodeString(&ifp->linkname);
	if (NeedRemoveList)
	{
		//KdPrint(("-> Removing interface list entry\n"));
		RemoveEntryList(&ifp->list);
	}
	//else
	//{
	//	KdPrint(("-> Not removing interface list entry\n"));
	//}
	//KdPrint(("%s - Deregistered interface\n", pdx->DebugName));
	ExFreePool(ifp);
	}							// DeregisterInterface

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

VOID EnableAllInterfaces(PGENERIC_EXTENSION pdx, BOOLEAN enable)
	{							// EnableAllInterfaces
	for (PLIST_ENTRY list = pdx->iflist.Flink; list != &pdx->iflist; list = list->Flink)
		{						// search for specified interface record
		PINTERFACE_RECORD ifp = CONTAINING_RECORD(list, INTERFACE_RECORD, list);
		if (ifp->enabled != enable)
			IoSetDeviceInterfaceState(&ifp->linkname, enable);
		ifp->enabled = enable;
		}
	}							// EnableAllInterfaces

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

PINTERFACE_RECORD FindInterfaceRecord(PGENERIC_EXTENSION pdx, const GUID* guid)
	{							// FindInterfaceRecord
	PAGED_CODE();

	for (PLIST_ENTRY list = pdx->iflist.Flink; list != &pdx->iflist; list = list->Flink)
		{						// search for specified interface record
		PINTERFACE_RECORD ifp = CONTAINING_RECORD(list, INTERFACE_RECORD, list);
		if (ifp->guid == *guid)
			return ifp;
		}
	return NULL;
	}							// FindInterfaceRecord

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleCancelRemove(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleCancelRemove
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_CANCEL_REMOVE_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	if (pdx->state == PENDINGREMOVE)
		{						// we succeeded earlier query

		// Lower-level drivers are presumably in the pending-remove state as
		// well, so we need to tell them that the remove has been cancelled
		// before we start sending IRPs down to them.

		NTSTATUS status = ForwardAndWait(pdx, Irp); // wait for lower layers
		if (NT_SUCCESS(status))
			{					// completed successfully
			//KdPrint(("%s - To %s from PENDINGREMOVE\n", pdx->DebugName, statenames[pdx->prevstate]));
			RestartAllRequests(pdx->queues, pdx->nqueues, pdx->DeviceObject);
			GenericWakeupControl(pdx, ManageWaitWake);	// reissue wait-wake if necessary
			}					// completed successfully
		else
			DRIVER_LOG_ERROR(("%s - Status %8.8lX returned by PDO for IRP_MN_CANCEL_REMOVE_DEVICE", pdx->DebugName, status));

		return CompleteRequest(Irp, status);
		}						// we succeeded earlier query
	
	return DefaultPnpHandler(pdx, Irp); // unexpected cancel
	}							// HandleCancelRemove

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleCancelStop(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleCancelStop
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_CANCEL_STOP_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	if (pdx->state == PENDINGSTOP)
		{						// we succeeded earlier query

		// Lower level drivers are presumably in the pending-stop state as
		// well, so we need to tell them that the stop has been cancelled
		// before we start sending IRPs down to them.

		NTSTATUS status = ForwardAndWait(pdx, Irp); // wait for lower layers
		if (NT_SUCCESS(status))
			{					// completed successfully
			//KdPrint(("%s - To WORKING from PENDINGSTOP\n", pdx->DebugName));
			pdx->state = WORKING;
			RestartAllRequests(pdx->queues, pdx->nqueues, pdx->DeviceObject);
			GenericWakeupControl(pdx, ManageWaitWake);	// reissue wait-wake if necessary
			}					// completed successfully
		else
			DRIVER_LOG_ERROR(("%s - Status %8.8lX returned by PDO for IRP_MN_CANCEL_STOP_DEVICE", pdx->DebugName, status));
		
		return CompleteRequest(Irp, status);
		}						// we succeeded earlier query

	return DefaultPnpHandler(pdx, Irp); // unexpected cancel
	}							// HandleCancelStop

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleQueryCapabilities(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleQueryCapabilities
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_CAPABILITIES);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	PDEVICE_CAPABILITIES pdc = stack->Parameters.DeviceCapabilities.Capabilities;

	// Check to be sure we know how to handle this version of the capabilities structure

	if (pdc->Version < 1)
		return DefaultPnpHandler(pdx, Irp);

	NTSTATUS status = ForwardAndWait(pdx, Irp);
	if (NT_SUCCESS(status))
		{						// IRP succeeded
		stack = IoGetCurrentIrpStackLocation(Irp);
		pdc = stack->Parameters.DeviceCapabilities.Capabilities;
		if (pdx->Flags & GENERIC_SURPRISE_REMOVAL_OK)
			pdc->SurpriseRemovalOK = TRUE;
		AdjustDeviceCapabilities(pdx, pdc);	// compensate for bus driver shortcoming
		pdx->devcaps = *pdc;	// save capabilities for whoever needs to see them
		pdx->GotCapabilities = TRUE;	// flag for GenericGetDeviceCapabilities
		GenericWakeupControl(pdx, ManageWaitWake);	// in case capabilities now allow WAIT_WAKE
		}						// IRP succeeded

	return CompleteRequest(Irp, status);
	}							// HandleQueryCapabilities

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleQueryRemove(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleQueryRemove
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_REMOVE_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	if (pdx->state == WORKING)
		{						// currently working

	#ifdef _X86_

		// Win98 doesn't check for open handles before allowing a remove to proceed,
		// and it may deadlock in IoReleaseRemoveLockAndWait if handles are still
		// open.

		if (win98 && pdx->DeviceObject->ReferenceCount)
			{
			KdPrint(("%s - Failing removal query due to open handles\n", pdx->DebugName));
			return CompleteRequest(Irp, STATUS_DEVICE_BUSY);
			}

	#endif

		// See if it's okay to remove this device. The test includes asking the client
		// driver. Then stall the queue for the duration of the query. The TOASTER sample
		// in the DDK drains the queue here instead of stalling it, by the way.

		if (!OkayToRemove(pdx))
			return CompleteRequest(Irp, STATUS_UNSUCCESSFUL); // can't remove because busy

		StallAllRequests(pdx->queues, pdx->nqueues);
		if (pdx->FlushPendingIo)
			(*pdx->FlushPendingIo)(pdx->DeviceObject, IRP_MJ_PNP, IRP_MN_QUERY_REMOVE_DEVICE, PowerDeviceUnspecified, PowerDeviceUnspecified);
		WaitForCurrentIrps(pdx->queues, pdx->nqueues);
		GenericWakeupControl(pdx, CancelWaitWake);	// cancel any pending wait-wake

		//KdPrint(("%s - To PENDINGREMOVE from %s\n", pdx->DebugName, statenames[pdx->state]));
		}						// currently working

	// Save current state for restoration if the query gets cancelled.
	// (We can now be stopped or working)

	pdx->prevstate = pdx->state;
	pdx->state = PENDINGREMOVE;
	return DefaultPnpHandler(pdx, Irp);
	}							// HandleQueryRemove

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleQueryState(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleQueryState
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_PNP_DEVICE_STATE);

	// Note that you can't use sizeof in a #if expression, so it's not possible to use
	// conditional compilation to choose between 32- and 64-bit exchanges in the
	// following statement. If the size of a PNP_DEVICE_STATE changes to be bigger than
	// 32 bits, therefore, you have to change this by hand. The ASSERT will alert you to
	// the problem...

	// DIVERGENCE: kk 08/10/2022 - Change NT assertion to compile-time assertion
	static_assert(sizeof(PNP_DEVICE_STATE) == sizeof(LONG), "PNP_DEVICE_STATE is larger than 32 bits.");

	PNP_DEVICE_STATE mask = InterlockedExchange((PLONG) &pdx->pnpstatemask, 0);

	if (mask)
		{						// some status flags have changed
		Irp->IoStatus.Status = STATUS_SUCCESS; // indicate we've handled this IRP
		Irp->IoStatus.Information &= ~(ULONG_PTR) mask; // clear all changed flags
		Irp->IoStatus.Information |= pdx->pnpstate & mask; // set all flags that were changed to "1"
		}						// some status flags have changed

	// DDK doc says you handle this IRP on the way down, but TOASTER handles
	// it on the way up. It shouldn't matter, because you're not supposed to
	// overstore the result you find already in IoStatus.Information.

	return DefaultPnpHandler(pdx, Irp);
	}							// HandleQueryState

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleQueryStop(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleQueryStop
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_QUERY_STOP_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP
	
	// Boot devices may get this query before they even start, so check to see
	// if we're in the WORKING state before doing anything.

	if (pdx->state != WORKING)
		return DefaultPnpHandler(pdx, Irp);

	// See if it will be okay to stop the device right now. This test includes
	// asking the client driver via a callback routine.

	if (!OkayToStop(pdx))
		return CompleteRequest(Irp, STATUS_UNSUCCESSFUL);

	// Stall the request queue and wait for the current IRP (if any) to finish

	StallAllRequests(pdx->queues, pdx->nqueues);
	if (pdx->FlushPendingIo)
		(*pdx->FlushPendingIo)(pdx->DeviceObject, IRP_MJ_PNP, IRP_MN_QUERY_STOP_DEVICE, PowerDeviceUnspecified, PowerDeviceUnspecified);
	WaitForCurrentIrps(pdx->queues, pdx->nqueues);
	GenericWakeupControl(pdx, DisableWakeup);
 	
	pdx->state = PENDINGSTOP;
	return DefaultPnpHandler(pdx, Irp);
	}							// HandleQueryStop

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleRemoveDevice(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleRemoveDevice
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_REMOVE_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	// Cancel any queued requests and start rejecting new ones

	AbortAllRequests(pdx->queues, pdx->nqueues, STATUS_DELETE_PENDING);
	AbortPendingIoctls(pdx, STATUS_DELETE_PENDING);
	GenericWakeupControl(pdx, DisableWakeup);

	// Disable all device interfaces that were registered through us. This
	// triggers PnP notifications that will allow apps to close their handles.

	DeregisterAllInterfaces(pdx);

	// Release I/O resources

	CallStopDevice(pdx);

	//KdPrint(("%s - To REMOVED from %s\n", pdx->DebugName, statenames[pdx->state]));
	pdx->state = REMOVED;

	// Let lower-level drivers handle this request. Ignore whatever
	// result eventuates.

	NTSTATUS status = DefaultPnpHandler(pdx, Irp);

	// Wait for all claims against this device to vanish before removing
	// the device object
	
	if (pdx->RemoveLock)
		IoReleaseRemoveLockAndWait(pdx->RemoveLock, Irp);

	// Remove the device object

	CallRemoveDevice(pdx);

	// Cleanup our device extension

	CleanupGenericExtension(pdx);

	return status;				// lower-level completed IoStatus already
	}							// HandleRemoveDevice

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleStartDevice(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleStartDevice
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_START_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	NTSTATUS status = ForwardAndWait(pdx, Irp);
	if (!NT_SUCCESS(status))
		return CompleteRequest(Irp, status);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

	PCM_PARTIAL_RESOURCE_LIST raw;
	if (stack->Parameters.StartDevice.AllocatedResources)
		raw = &stack->Parameters.StartDevice.AllocatedResources->List[0].PartialResourceList;
	else
		raw = NULL;

	PCM_PARTIAL_RESOURCE_LIST translated;
	if (stack->Parameters.StartDevice.AllocatedResourcesTranslated)
		translated = &stack->Parameters.StartDevice.AllocatedResourcesTranslated->List[0].PartialResourceList;
	else
		translated = NULL;

#if DBG
	if (raw)
		{
		//KdPrint(("%s - Resources:\n", pdx->DebugName));
		//ShowResources(raw);
		}
	if (translated)
		{
		//KdPrint(("%s - Translated Resources:\n", pdx->DebugName));
		//ShowResources(translated);
		}
#endif // DBG

	// Assume we are initially in the D0 state

	pdx->devpower = PowerDeviceD0;
	pdx->syspower = PowerSystemWorking;
	POWER_STATE foo;
	foo.DeviceState = PowerDeviceD0;
	PoSetPowerState(pdx->Pdo, DevicePowerState, foo);

	// Call client driver's StartDevice function to initialize the device for I/O

	status = (*pdx->StartDevice)(pdx->DeviceObject, raw, translated); 

	if (NT_SUCCESS(status))
		{						// started okay
		//KdPrint(("%s - To WORKING from %s\n", pdx->DebugName, statenames[pdx->state]));

		pdx->HardwareWorking = TRUE;

		// If idle detection constants were saved in the registry, register
		// this device for idle detection

		POWERINFO pi;
		if (NT_SUCCESS(GetPowerInfoFromRegistry(pdx, &pi)))
			ImplementPowerPolicy(pdx, &pi);

		// Enable all registered device interfaces

		EnableAllInterfaces(pdx, TRUE);

		// Release any stalled IRPs

		pdx->state = WORKING;
		AllowAllRequests(pdx->queues, pdx->nqueues); // in case we got a bogus STOP
		RestartAllRequests(pdx->queues, pdx->nqueues, pdx->DeviceObject);

		// If system wakeup is enabled, start a wait-wake IRP

		GenericWakeupControl(pdx, ManageWaitWake);
		}						// started okay

	return CompleteRequest(Irp, status);
	}							// HandleStartDevice

///////////////////////////////////////////////////////////////////////////////	

NTSTATUS HandleStopDevice(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleStopDevice
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_STOP_DEVICE);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	// We're supposed to always get a query before we're stopped, so
	// we should already be in the PENDINGSTOP state. There's a Win98 bug that
	// can sometimes cause us to get a STOP instead of a REMOVE, in which case
	// we should start rejecting IRPs

	if (pdx->state != PENDINGSTOP)
		{						// no previous query
		DRIVER_LOG_ERROR(("%s - STOP with no previous QUERY_STOP!\n", pdx->DebugName));
		AbortAllRequests(pdx->queues, pdx->nqueues, STATUS_DELETE_PENDING);
		AbortPendingIoctls(pdx, STATUS_DELETE_PENDING);
		}						// no previous query

	GenericWakeupControl(pdx, CancelWaitWake);	// cancel any outstanding wait-wake IRP
	CallStopDevice(pdx);
	//KdPrint(("%s - To STOPPED from %s\n", pdx->DebugName, statenames[pdx->state]));
	pdx->state = STOPPED;
	return DefaultPnpHandler(pdx, Irp);
	}							// HandleStopDevice

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleSurpriseRemoval(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleSurpriseRemoval
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_SURPRISE_REMOVAL);
	Irp->IoStatus.Status = STATUS_SUCCESS;	// flag that we handled this IRP

	AbortAllRequests(pdx->queues, pdx->nqueues, STATUS_DELETE_PENDING);
	AbortPendingIoctls(pdx, STATUS_DELETE_PENDING);
	GenericWakeupControl(pdx, DisableWakeup);

	//KdPrint(("%s - To SURPRISEREMOVED from %s\n", pdx->DebugName, statenames[pdx->state]));
	pdx->state = SURPRISEREMOVED;

	EnableAllInterfaces(pdx, FALSE);	// this triggers notifications so apps can close their handles
	CallStopDevice(pdx);
	return DefaultPnpHandler(pdx, Irp);
	}							// HandleSurpriseRemoval

///////////////////////////////////////////////////////////////////////////////

NTSTATUS HandleUsageNotification(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// HandleUsageNotification
	ASSERT(IoGetCurrentIrpStackLocation(Irp)->MinorFunction == IRP_MN_DEVICE_USAGE_NOTIFICATION);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	DEVICE_USAGE_NOTIFICATION_TYPE type = stack->Parameters.UsageNotification.Type;
	BOOLEAN inpath = stack->Parameters.UsageNotification.InPath;

	switch (type)
		{						// switch on usage type

	case DeviceUsageTypeHibernation:
		if (!(pdx->Flags & GENERIC_USAGE_HIBERNATE))
			return CompleteRequest(Irp, STATUS_UNSUCCESSFUL);

		AdjustSpecialFileCounter(pdx, inpath, &pdx->nhibernatefiles);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;

	case DeviceUsageTypePaging:
		{						// DeviceUsageTypePaging
		if (!(pdx->Flags & GENERIC_USAGE_PAGING))
			return CompleteRequest(Irp, STATUS_UNSUCCESSFUL);

		if (inpath && pdx->state != WORKING)
			return CompleteRequest(Irp, STATUS_DEVICE_NOT_READY); // SP-7, per DDK doc

		// Guard against reentrance by waiting on evPagingPath, which is a
		// synchronization event that was initialized to the signalled state.
		// Therefore, it will be reset once we get past the wait (SP-7, ver 1.6)

		KeWaitForSingleObject(&pdx->evPagingPath, Executive, KernelMode, FALSE, NULL);

		// Add or substract one from the paging path counter. AdjustSpecialFileCounter
		// will also take care of enabling or disabling idle detection.

		AdjustSpecialFileCounter(pdx, inpath, &pdx->npagefiles);

		// Adjust the power-pagable flag too. (Per AdrianO) we must be careful not to
		// have a non-paged power handler sitting on top of a paged handler, so we
		// only clear DO_POWER_PAGABLE in our completion routine.
		
		PDEVICE_OBJECT fdo = pdx->DeviceObject;
		if (pdx->npagefiles == 0)
			{					// resume paging
			if (!(fdo->Flags & DO_POWER_INRUSH))
				fdo->Flags |= DO_POWER_PAGABLE;
			}					// resume paging

		// TODO Deal with paging of Read, Write, Control requests in minidriver

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
		}						// DeviceUsageTypePaging

	case DeviceUsageTypeDumpFile:
		if (!(pdx->Flags & GENERIC_USAGE_DUMP))
			return CompleteRequest(Irp, STATUS_UNSUCCESSFUL);

		AdjustSpecialFileCounter(pdx, inpath, &pdx->ndumpfiles);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
		}						// switch on usage type

	// Install completion routine so we can recover from a failure and so we can
	// clear DO_POWER_PAGABLE at the right time.
	
	IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE) OnNotificationComplete, pdx, TRUE, TRUE, TRUE);
	IoCopyCurrentIrpStackLocationToNext(Irp);
	return IoCallDriver(pdx->LowerDeviceObject, Irp);
	}							// HandleUsageNotification

///////////////////////////////////////////////////////////////////////////////	

NTSTATUS OnNotificationComplete(PDEVICE_OBJECT fdo, PIRP Irp, PGENERIC_EXTENSION pdx)
	{							// OnNotificationComplete

	if (Irp->PendingReturned)
		IoMarkIrpPending(Irp);

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	DEVICE_USAGE_NOTIFICATION_TYPE type = stack->Parameters.UsageNotification.Type;
	BOOLEAN inpath = stack->Parameters.UsageNotification.InPath;

	// If the notification succeeded, and if we're on the path for paging files,
	// be sure we start getting power IRPs at DISPATCH_LEVEL.

	if (NT_SUCCESS(Irp->IoStatus.Status))
		{						// notification succeeded
		if (pdx->npagefiles)
			fdo->Flags &= ~DO_POWER_PAGABLE;
		if (type == DeviceUsageTypePaging)
			KeSetEvent(&pdx->evPagingPath, IO_NO_INCREMENT, FALSE); // SP-7
		return STATUS_SUCCESS;
		}						// notification succeeded

	// Somebody underneath us failed this usage notification, so undo the steps we
	// took in the dispatch routine. Notice how we reverse the sense of "inpath" in
	// the following calls to AdjustSpecialFileCounter.

	switch (type)
		{						// switch on usage type

	case DeviceUsageTypeHibernation:
		AdjustSpecialFileCounter(pdx, !inpath, &pdx->nhibernatefiles);
		break;

	case DeviceUsageTypePaging:
		{						// DeviceUsageTypePaging
		AdjustSpecialFileCounter(pdx, !inpath, &pdx->npagefiles);
		if (pdx->npagefiles == 0)
			{					// resume paging
			if (!(fdo->Flags & DO_POWER_INRUSH))
				fdo->Flags |= DO_POWER_PAGABLE;
			}					// resume paging
		else
			fdo->Flags &= ~DO_POWER_PAGABLE;

		KeSetEvent(&pdx->evPagingPath, IO_NO_INCREMENT, FALSE); // SP-7
		break;
		}						// DeviceUsageTypePaging

	case DeviceUsageTypeDumpFile:
		AdjustSpecialFileCounter(pdx, !inpath, &pdx->ndumpfiles);
		break;
		}						// switch on usage type

	return STATUS_SUCCESS;
	}							// OnNotificationComplete

///////////////////////////////////////////////////////////////////////////////	

#if DBG

VOID ShowResources(IN PCM_PARTIAL_RESOURCE_LIST list)
	{							// ShowResources
	PCM_PARTIAL_RESOURCE_DESCRIPTOR resource = list->PartialDescriptors;
	ULONG nres = list->Count;
	ULONG i;

	for (i = 0; i < nres; ++i, ++resource)
		{						// for each resource
		ULONG type = resource->Type;

		static char* name[] = {
			"CmResourceTypeNull",
			"CmResourceTypePort",
			"CmResourceTypeInterrupt",
			"CmResourceTypeMemory",
			"CmResourceTypeDma",
			"CmResourceTypeDeviceSpecific",
			"CmResourceTypeBusNumber",
			"CmResourceTypeDevicePrivate",
			"CmResourceTypeAssignedResource",
			"CmResourceTypeSubAllocateFrom",
			};

		KdPrint(("    type %s", type < arraysize(name) ? name[type] : "unknown"));

		switch (type)
			{					// select on resource type
		case CmResourceTypePort:
		case CmResourceTypeMemory:
			KdPrint((" start %8X%8.8lX length %X\n",
				resource->u.Port.Start.HighPart, resource->u.Port.Start.LowPart,
				resource->u.Port.Length));
			break;
		
		case CmResourceTypeInterrupt:
			KdPrint(("  level %X, vector %X, affinity %zX\n",
				resource->u.Interrupt.Level, resource->u.Interrupt.Vector,
				resource->u.Interrupt.Affinity));
			break;
	
		case CmResourceTypeDma:
			KdPrint(("  channel %d, port %X\n",
				resource->u.Dma.Channel, resource->u.Dma.Port));
			}					// select on resource type
		}						// for each resource
	}							// ShowResources

#endif // DBG

