// Power request handler for Generic driver
// Copyright (C) 1999 by Walter Oney
// All rights reserved

#include "stddcls.h"
#include "driver.h"
#include "GenericPower.h"

NTSTATUS DefaultPowerHandler(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS WaitWakeCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PGENERIC_EXTENSION pdx);
VOID WaitWakeCallback(PDEVICE_OBJECT junk, UCHAR MinorFunction, POWER_STATE state,
	PGENERIC_EXTENSION pdx, PIO_STATUS_BLOCK pstatus);

enum POWSTATE {
	InitialState = 0,				// initial state of FSM
	SysPowerUpPending,				// system power-up IRP forwarded
	SubPowerUpPending,				// waiting for nested device power up to finish
	SubPowerDownPending,			// waiting from device to power down before forwarding system power-down IRP
	SysPowerDownPending,			// waiting for system power-down IRP to finish
	DevPowerUpPending,				// waiting for device power-up IRP
	DevPowerDownPending,			// waiting for device power-down IRP
	ContextSavePending,				// context save is underway
	ContextRestorePending,			// context restore is underway
	DevQueryUpPending,				// device query for power-up pending
	DevQueryDownPending,			// device query for power-down pending
	QueueStallPending,				// waiting for device to be idle
	SaveSeqPending,					// waiting to get sequence numbers after context save
	RestoreSeqPending,				// waiting to get sequence numbers before context restore
	PassiveCompletePending,			// waiting for PASSIVE_LEVEL callback to complete IRP
	FinalState,						// final state of FSM
	NUMPOWSTATES,
	};

enum POWEVENT {
	NewIrp = 0,						// new query/set IRP
	MainIrpComplete,				// the main IRP has finished
	AsyncNotify,					// some other event has occurred
	NUMPOWEVENTS,
	};

typedef struct _POWCONTEXT {
	LONG id;						// unique sequence number for this IRP
	LONG eventcount;				// number of events generated for this IRP
	PGENERIC_EXTENSION pdx;			// our own device extension
	PIRP irp;						// the IRP we're processing
	enum POWSTATE state;			// current state of FSM
	NTSTATUS status;				// completion status for main IRP
	DEVICE_POWER_STATE devstate;	// device power state to use
	POWER_SEQUENCE sequence;		// sequence numbers retrieved by ditto
	DEVICE_POWER_STATE oldpower;	// previous device power state
	UCHAR MinorFunction;			// minor function to use in requested power IRP
	BOOLEAN UnstallQueue;			// unstall queue when main IRP finishes
	} POWCONTEXT, *PPOWCONTEXT;

NTSTATUS HandlePowerEvent(PPOWCONTEXT ctx, enum POWEVENT event);

static LONG ctxcount = 0;			// counter for POWCONTEXT structures

#define VERBOSETRACE				// for debugging HandlePowerEvent

#if DBG

	#ifdef VERBOSETRACE
		#define POWTRACE(x) DbgPrint x
	#else
		#define POWTRACE(x)
	#endif

	static char* fcnname[] = {
		"IRP_MN_WAIT_WAKE",
		"IRP_MN_POWER_SEQUENCE",
		"IRP_MN_SET_POWER",
		"IRP_MN_QUERY_POWER",
		};

	static char* sysstate_names[] = {
		"PowerSystemUnspecified",
		"PowerSystemWorking",
		"PowerSystemSleeping1",
		"PowerSystemSleeping2",
		"PowerSystemSleeping3",
		"PowerSystemHibernate",
		"PowerSystemShutdown",
		"PowerSystemMaximum",
		};

	static char* devstate[] = {
		"PowerDeviceUnspecified",
		"PowerDeviceD0",
		"PowerDeviceD1",
		"PowerDeviceD2",
		"PowerDeviceD3",
		"PowerDeviceMaximum",
		};

#else
	#define POWTRACE(x)
#endif // DBG
									  
///////////////////////////////////////////////////////////////////////////////

GENERICAPI NTSTATUS GENERIC_EXPORT GenericDispatchPower(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// DispatchPower
	NTSTATUS status = STATUS_SUCCESS;
	if (pdx->RemoveLock)
		{
		status = IoAcquireRemoveLock(pdx->RemoveLock, Irp);
		if (!NT_SUCCESS(status))
			{
			PoStartNextPowerIrp(Irp);
			return CompleteRequest(Irp, status);
			}
		}

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(stack->MajorFunction == IRP_MJ_POWER);

	ULONG fcn = stack->MinorFunction;

	if (fcn == IRP_MN_SET_POWER || fcn == IRP_MN_QUERY_POWER)
		{						// handle set/query

//#if DBG
//		if (stack->Parameters.Power.Type == SystemPowerState)
//			KdPrint(("%s - POWER Request (%s), S-state = %s\n", pdx->DebugName, fcnname[fcn], sysstate_names[stack->Parameters.Power.State.SystemState]));
//		else
//			KdPrint(("%s - POWER Request (%s), D-state = %s\n", pdx->DebugName, fcnname[fcn], devstate[stack->Parameters.Power.State.DeviceState]));
//#endif // DBG

		// Create a context structure and launch the finite state machine that will process
		// this IRP asynchronously. The initial call to HandlePowerEvent should return
		// STATUS_PENDING. The FSM will eventually complete the IRP.

		PPOWCONTEXT ctx = (PPOWCONTEXT) ALLOC_NON_PAGED(sizeof(POWCONTEXT));
		if (!ctx)
			{
			DRIVER_LOG_ERROR(("%s - Can't allocate power context structure\n", pdx->DebugName));
			PoStartNextPowerIrp(Irp);
			status = CompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES);
			}
		else
			{				// process this IRP
			//RtlZeroMemory(ctx, sizeof(POWCONTEXT));
			ctx->pdx = pdx;
			ctx->irp = Irp;
			ctx->id = InterlockedIncrement(&ctxcount);
			status = HandlePowerEvent(ctx, NewIrp);
			}				// process this IRP
		}						// handle set/query

	else
		{						// handle other power request
	//#if DBG
	//	if (fcn < arraysize(fcnname))
	//		KdPrint(("%s - POWER Request (%s)\n", pdx->DebugName, fcnname[fcn]));
	//	else
	//		KdPrint(("%s - POWER Request ?? (0x%X)\n", pdx->DebugName, fcn));
	//#endif

		// Install a completion routine for a WAIT_WAKE so we're sure to nullify our
		// cached pointer before the IRP disappears.

		if (fcn == IRP_MN_WAIT_WAKE)
			{					// wait-wake IRP
			PoStartNextPowerIrp(Irp);
			IoCopyCurrentIrpStackLocationToNext(Irp);
			IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE) WaitWakeCompletionRoutine, pdx, TRUE, TRUE, TRUE);
			status = PoCallDriver(pdx->LowerDeviceObject, Irp);
			}					// wait-wake IRP

		// Simply forward any other power IRP. At the present time, the only
		// kind it could be is IRP_MN_POWER_SEQUENCE, which probably no-one
		// actually uses.

		else
			status = DefaultPowerHandler(pdx, Irp);
		}						// handle other power request
	
	if (pdx->RemoveLock)
		IoReleaseRemoveLock(pdx->RemoveLock, Irp);

	return status;
	}							// DispatchPower

///////////////////////////////////////////////////////////////////////////////

GENERICAPI NTSTATUS GENERIC_EXPORT GenericHandlePowerIoctl(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// GenericHandlePowerIoctl
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	ULONG info = 0;
	NTSTATUS status = STATUS_SUCCESS;

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	if (stack->MajorFunction != IRP_MJ_DEVICE_CONTROL)
		return STATUS_INVALID_DEVICE_REQUEST;

	ULONG cbin = stack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG cbout = stack->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;

	switch (code)
		{						// process request

	case IOCTL_GET_POWERINFO:				// code == 0xFF0
		{						// IOCTL_GET_POWERINFO
		if (cbout < sizeof(POWERINFO))
			{
			status = STATUS_INVALID_PARAMETER;
			break;
			}
		
		PPOWERINFO pip = (PPOWERINFO) Irp->AssociatedIrp.SystemBuffer;

		// Get timeout constants from the registry

		status = GetPowerInfoFromRegistry(pdx, pip);
		if (!NT_SUCCESS(status))
			{					// initialize parameters
			pip->ulConservation = 0;
			pip->ulPerformance = 0;
			status = STATUS_SUCCESS;
			}					// initialize parameters

		// Determine current device state

		pip->bCanIdle = (pdx->Flags & GENERIC_IDLE_DETECT) != 0;
		pip->bCanWake = pdx->devcaps.SystemWake != PowerSystemUnspecified && pdx->devcaps.DeviceWake != PowerDeviceUnspecified;
		pip->bWakeup = pdx->WakeupEnabled;
		pip->bIdle = pdx->devpower > PowerDeviceD0;
		
		info = sizeof(POWERINFO);
		break;
		}						// IOCTL_GET_POWERINFO

	case IOCTL_SET_POWERINFO:				// code == 0xFF1
		{						// IOCTL_SET_POWERINFO
		if (cbin < sizeof(POWERINFO))
			{
			status = STATUS_INVALID_PARAMETER;
			break;
			}

		// If we're currently idle, restore power. This is in case we've just been told to
		// never idle in the power mode the machine is currently in (which we can't easily
		// determine).

		GenericWakeupFromIdle(pdx, TRUE);

		PPOWERINFO pip = (PPOWERINFO) Irp->AssociatedIrp.SystemBuffer;
		(void)pip;
			status = WritePowerInfoToRegistry(pdx, (PPOWERINFO) Irp->AssociatedIrp.SystemBuffer);
		if (NT_SUCCESS(status))
			status = ImplementPowerPolicy(pdx, (PPOWERINFO) Irp->AssociatedIrp.SystemBuffer);
		break;
		}						// IOCTL_SET_POWERINFO

	case IOCTL_IDLENOW:						// code == 0xFF2
		{						// IOCTL_IDLENOW
		status = GenericIdleDevice(pdx, PowerDeviceD3);
		break;
		}						// IOCTL_IDLENOW

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;

		}						// process request

	Irp->IoStatus.Information = info;
	Irp->IoStatus.Status = status;
	return status;
	}							// GenericHandlePowerIoctl

///////////////////////////////////////////////////////////////////////////////

GENERICAPI NTSTATUS GENERIC_EXPORT GenericIdleDevice(PGENERIC_EXTENSION pdx, DEVICE_POWER_STATE state, BOOLEAN wait /* = FALSE */)
	{							// GenericIdleDevice
	if (pdx->state != WORKING && pdx->state != STOPPED)
		return STATUS_SUCCESS;
	NTSTATUS status = SendDeviceSetPower(pdx, state, wait);
	if (status == STATUS_PENDING)
		status = STATUS_SUCCESS;
	return status;
	}							// GenericIdleDevice

///////////////////////////////////////////////////////////////////////////////

GENERICAPI VOID GENERIC_EXPORT GenericMarkDeviceBusy(PGENERIC_EXTENSION pdx)
	{							// GenericMarkDeviceBusy
	if (pdx->idlecount)
		PoSetDeviceBusy(pdx->idlecount);
	}							// GenericMarkDeviceBusy

///////////////////////////////////////////////////////////////////////////////

GENERICAPI VOID GENERIC_EXPORT GenericRegisterForIdleDetection(PGENERIC_EXTENSION pdx, ULONG ConservationTimeout,
	ULONG PerformanceTimeout, DEVICE_POWER_STATE state)
	{							// GenericRegisterForIdleDetection
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);	// required by PoRegisterDeviceForIdleDetection
	pdx->cto = ConservationTimeout;
	pdx->pto = PerformanceTimeout;
	pdx->idlestate = state;
	pdx->idlecount = PoRegisterDeviceForIdleDetection(pdx->Pdo, ConservationTimeout, PerformanceTimeout, state);
	}							// GenericRegisterForIdleDetection

///////////////////////////////////////////////////////////////////////////////

GENERICAPI VOID GENERIC_EXPORT GenericSaveRestoreComplete(PVOID context)
	{							// GenericSaveRestoreComplete
	HandlePowerEvent((PPOWCONTEXT) context, AsyncNotify);
	}							// GenericSaveRestoreComplete

///////////////////////////////////////////////////////////////////////////////

GENERICAPI NTSTATUS GENERIC_EXPORT GenericWakeupControl(PGENERIC_EXTENSION pdx, enum WAKEFUNCTION wf)
	{							// GenericWakeupControl

	NTSTATUS status = STATUS_SUCCESS;

	switch (wf)
		{						// perform requested function

	case EnableWakeup:
		pdx->WakeupEnabled = TRUE;
		goto manage;

	case DisableWakeup:
		pdx->WakeupEnabled = FALSE;
		goto manage;

	case ManageWaitWake:
	manage:
		{						// ManageWaitWake
		if (pdx->WakeupEnabled)
			{					// wakeup is enabled

			// If capabilities don't provide for wakeup, don't do anything. This often happens the
			// first time we get called because the capabilities query hasn't happened yet. Also,
			// we mustn't issue a WAIT_WAKE if the device is not in D0.

			if (pdx->devpower > pdx->devcaps.DeviceWake 
				|| pdx->syspower > pdx->devcaps.SystemWake
				|| pdx->devpower != PowerDeviceD0)
				{
				//KdPrint(("%s - Deferring WAIT_WAKE due to state or capabilities\n", pdx->DebugName));
				return STATUS_INVALID_DEVICE_REQUEST;
				}

			if (InterlockedExchange(&pdx->wwoutstanding, 1))
				break;			// IRP already pending

			pdx->wwcancelled = 0;

			POWER_STATE junk;
			junk.SystemState = pdx->devcaps.SystemWake; // lowest state from which this IRP will wake system

			// I have seen systems where SystemState has a ridiculous value, such that we have to
			// ask for S0 or else it doesn't work...

			if (junk.SystemState > PowerSystemHibernate)
				{				// correct bogus value
				//KdPrint(("%s - Requesting WAIT_WAKE with S0 instead of bogus S%d from capabilities\n", pdx->DebugName, junk.SystemState - 1));
				junk.SystemState = PowerSystemWorking;
				}				// correct bogus value
			
			//KdPrint(("%s - Issuing WAIT_WAKE\n", pdx->DebugName));
			
			status = PoRequestPowerIrp(pdx->Pdo, IRP_MN_WAIT_WAKE, junk, (PREQUEST_POWER_COMPLETE) WaitWakeCallback, pdx, &pdx->WaitWakeIrp);
			if (!NT_SUCCESS(status))
				{					// error starting wakeup IRP
				DRIVER_LOG_ERROR(("%s - PoRequestPowerIrp(IRP_MN_WAIT_WAIT) failed - %X\n", pdx->DebugName, status));
				pdx->WakeupEnabled = FALSE;
				pdx->wwoutstanding = 0;
				break;
				}					// error starting wakeup IRP

			status = STATUS_SUCCESS;	// especially *not* STATUS_PENDING!

			break;
			}					// wakeup is enabled

		// Note fall-through to CanceWaitWake here!!

		}						// ManageWaitWake

	case CancelWaitWake:
		{						// CancelWaitWake

		// If the following exchange operation returns NULL, WaitWakeComplete has run and
		// the IRP may no longer exist.

		PIRP Irp = (PIRP) InterlockedExchangePointer((PVOID volatile *) &pdx->WaitWakeIrp, NULL);
		if (Irp)
			{				// cancel pending IRP

			// We need to be sure that IoCancelIrp and any cancel routine it may
			// call finish before the Power Manager calls IoFreeIrp to get rid
			// of the WAIT_WAKE IRP. The call to IoFreeIrp can't happen sooner
			// than the IRP gets all the way through IoCompleteRequest, however, so we establish
			// an interlock such that whichever of us and our completion routine
			// sees the IRP last allows it to complete.

			IoCancelIrp(Irp);
			//KdPrint(("%s - IRP_MN_WAIT_WAKE cancelled\n", pdx->DebugName));

			if (InterlockedExchange(&pdx->wwcancelled, 1))
				IoCompleteRequest(Irp, IO_NO_INCREMENT); // pickup with aborted completion
			}				// cancel pending IRP
		break;
		}						// CancelWaitWake

	default:
		ASSERT(FALSE);
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
		}						// perform requested function

	return status;
	}							// GenericWakeupControl

///////////////////////////////////////////////////////////////////////////////

GENERICAPI NTSTATUS GENERIC_EXPORT GenericWakeupFromIdle(PGENERIC_EXTENSION pdx, BOOLEAN wait /* = FALSE */)
	{							// GenericWakeupFromIdle
	if (pdx->devpower == PowerDeviceD0)
		return STATUS_SUCCESS;
	NTSTATUS status = SendDeviceSetPower(pdx, PowerDeviceD0, wait);
	if (status == STATUS_PENDING)
		status = STATUS_SUCCESS;
	return status;
	}							// GenericWakeupFromIdle

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

VOID AdjustSpecialFileCounter(PGENERIC_EXTENSION pdx, BOOLEAN inpath, PLONG counter)
	{							// AdjustSpecialFileCounter

	// Increment or decrement the appopriate special-file usage counter. Note
	// that IoAdjustPagingPathCount (recommended for this use in the DDK doc) is
	// just a macro that does exactly this...

	if (inpath)
		InterlockedIncrement(counter);
	else
		InterlockedDecrement(counter);

	// Enable or disable idle detection, as appropriate. Alter the not-disableable
	// status flag as well (SP-7, ver 1.6)

	LONG totcount = pdx->ndumpfiles + pdx->nhibernatefiles + pdx->npagefiles;
	if (totcount == 0)
		{						// resume idle detection
		if (pdx->cto && pdx->pto)
			pdx->idlecount = PoRegisterDeviceForIdleDetection(pdx->Pdo, pdx->cto, pdx->pto, pdx->idlestate);
		GenericSetDeviceState(pdx, 0);
		}						// resume idle detection
	else if (inpath && totcount == 1)
		{						// disable idle detection
		pdx->idlecount = PoRegisterDeviceForIdleDetection(pdx->Pdo, 0, 0, PowerDeviceD0);
		GenericSetDeviceState(pdx, PNP_DEVICE_NOT_DISABLEABLE);
		}						// disable idle detection
	}							// AdjustSpecialFileCounter

///////////////////////////////////////////////////////////////////////////////

NTSTATUS DefaultPowerHandler(PGENERIC_EXTENSION pdx, PIRP Irp)
	{							// DefaultPowerHandler
	PoStartNextPowerIrp(Irp);	// must be done while we own the IRP
	IoSkipCurrentIrpStackLocation(Irp);
	return PoCallDriver(pdx->LowerDeviceObject, Irp);
	}							// DefaultPowerHandler

///////////////////////////////////////////////////////////////////////////////
// GetLowestDevicePowerState returns the lowest device power state that is
// consistent with a given system power state and the current wakeup state of
// the device.

DEVICE_POWER_STATE GetLowestDevicePowerState(PGENERIC_EXTENSION pdx, SYSTEM_POWER_STATE sysstate)
	{							// GetLowestDevicePowerState
	DEVICE_POWER_STATE maxstate = pdx->devcaps.DeviceState[sysstate];
	DEVICE_POWER_STATE minstate = PowerDeviceD3;

	if (pdx->WakeupEnabled && sysstate <= pdx->devcaps.SystemWake)
		minstate = pdx->devcaps.DeviceWake;	// select state that will preserve wakeup ability

	DEVICE_POWER_STATE dstate = minstate > maxstate ? minstate : maxstate;

	// Allow minidriver to pick the new state, subject to the minimum and maximum
	// already established.

	if (pdx->GetDevicePowerState)
		{						// let minidriver pick state
		dstate = (*pdx->GetDevicePowerState)(pdx->DeviceObject, sysstate, dstate);
		if (dstate < maxstate)
			dstate = maxstate;
		else if (dstate > minstate)
			dstate = minstate;
		}						// let minidriver pick state

	return dstate;
	}							// GetLowestDevicePowerState

///////////////////////////////////////////////////////////////////////////////

NTSTATUS GetPowerInfoFromRegistry(PGENERIC_EXTENSION pdx, PPOWERINFO pip)
	{							// GetPowerInfoFromRegistry
	HANDLE hkey;
	NTSTATUS status = IoOpenDeviceRegistryKey(pdx->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_ALL_ACCESS, &hkey);
	if (!NT_SUCCESS(status))
		{
		DRIVER_LOG_ERROR(("Error %X trying to open registry key", status));
		return status;
		}

	UNICODE_STRING valname;
	RtlInitUnicodeString(&valname, L"PowerSettings");
	ULONG size = 0;
	UCHAR junk;

	status = ZwQueryValueKey(hkey, &valname, KeyValuePartialInformation, &junk, 0, &size);
	if (status != STATUS_OBJECT_NAME_NOT_FOUND && size)
		{						// value exists
		PKEY_VALUE_PARTIAL_INFORMATION vpip = (PKEY_VALUE_PARTIAL_INFORMATION) ALLOC_PAGED(size);
		status = ZwQueryValueKey(hkey, &valname, KeyValuePartialInformation, vpip, size, &size);
		if (NT_SUCCESS(status))
			{					// copy value
			if (vpip->DataLength < 2 * sizeof(ULONG))
				{
				DRIVER_LOG_ERROR(("%s - Ignoring PowerSettings value with strange size %d", pdx->DebugName, vpip->DataLength));
				status = STATUS_UNSUCCESSFUL;
				}
			else
				RtlCopyBytes(pip, vpip->Data, 2 * sizeof(ULONG));
			}					// copy value
		else
			DRIVER_LOG_ERROR(("ZwQueryValueKey failed - %X", status));

		ExFreePool(vpip);
		}						// value exists

	ZwClose(hkey);
	return status;
	}							// GetPowerInfoFromRegistry

///////////////////////////////////////////////////////////////////////////////

NTSTATUS ImplementPowerPolicy(PGENERIC_EXTENSION pdx, PPOWERINFO pip)
	{							// ImplementPowerPolicy
	if (pdx->Flags & GENERIC_IDLE_DETECT)
		GenericRegisterForIdleDetection(pdx,
		pip->ulConservation, pip->ulPerformance, PowerDeviceD3);
	GenericWakeupControl(pdx, pip->bWakeup ? EnableWakeup : DisableWakeup);
	return STATUS_SUCCESS;
	}							// ImplementPowerPolicy

///////////////////////////////////////////////////////////////////////////////

BOOLEAN OkayToRemove(PGENERIC_EXTENSION pdx)
	{							// OkayToRemove
	if (pdx->ndumpfiles || pdx->nhibernatefiles || pdx->npagefiles)
		return FALSE;
	if (!pdx->OkayToRemove)
		return TRUE;
	return (*pdx->OkayToRemove)(pdx->DeviceObject);
	}							// OkayToRemove

///////////////////////////////////////////////////////////////////////////////

BOOLEAN OkayToStop(PGENERIC_EXTENSION pdx)
	{							// OkayToStop
	if (pdx->ndumpfiles || pdx->nhibernatefiles || pdx->npagefiles)
		return FALSE;
	if (!pdx->OkayToStop)
		return TRUE;
	return (*pdx->OkayToStop)(pdx->DeviceObject);
	}							// OkayToStop

///////////////////////////////////////////////////////////////////////////////

struct SDSP_CONTEXT {
	PKEVENT pev;				// event to signal when request complete
	NTSTATUS status;			// ending status
	};

#pragma LOCKEDCODE

VOID SendDeviceSetPowerComplete(PDEVICE_OBJECT junk, UCHAR fcn, POWER_STATE state, SDSP_CONTEXT* context, PIO_STATUS_BLOCK pstatus)
	{							// SendDeviceSetPowerComplete
	(void)junk;
	(void)fcn;
	(void)state;
	context->status = pstatus->Status;
	KeSetEvent(context->pev, EVENT_INCREMENT, FALSE);
	}							// SendDeviceSetPowerComplete

#pragma PAGEDCODE

NTSTATUS SendDeviceSetPower(PGENERIC_EXTENSION pdx, DEVICE_POWER_STATE devpower, BOOLEAN wait /* = FALSE */)
	{							// SendDeviceSetPower
	POWER_STATE state;
	state.DeviceState = devpower;
	NTSTATUS status;

	if (wait)
		{						// synchronous operation
		KEVENT event;
		KeInitializeEvent(&event, NotificationEvent, FALSE);
		SDSP_CONTEXT context = {&event};
		status = PoRequestPowerIrp(pdx->Pdo, IRP_MN_SET_POWER, state,
			(PREQUEST_POWER_COMPLETE) SendDeviceSetPowerComplete, &context, NULL);
		if (status == STATUS_PENDING)
			{
			KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
			status = context.status;
			}
		}						// synchronous operation
	else
		status = PoRequestPowerIrp(pdx->Pdo, IRP_MN_SET_POWER, state, NULL, NULL, NULL);
	
	return status;
	}							// SendDeviceSetPower

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

VOID WaitWakeCallback(PDEVICE_OBJECT junk, UCHAR MinorFunction, POWER_STATE state,
	PGENERIC_EXTENSION pdx, PIO_STATUS_BLOCK pstatus)
	{							// WaitWakeCallback
	(void)junk;
	(void)MinorFunction;
	(void)state;
	// Indicate that wait-wake is no longer outstanding

	pdx->wwoutstanding = 0;

	if (!NT_SUCCESS(pstatus->Status))
		{						// IRP failed
		DRIVER_LOG_ERROR(("%s - IRP_MN_WAIT_WAKE failed - %X\n", pdx->DebugName, pstatus->Status));
		return;
		}						// IRP failed

	// Wakeup our own device. This is necessary if the device signalled a wakeup while
	// the system was already in S0 because, in this case, we won't otherwise get
	// an IRP to power ourselves on.
	
	SendDeviceSetPower(pdx, PowerDeviceD0, FALSE);
	}							// WaitWakeCallback

///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

NTSTATUS WaitWakeCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PGENERIC_EXTENSION pdx)
	{							// WaitWakeCompletionRoutine
	(void)junk;
	if (Irp->PendingReturned)
		IoMarkIrpPending(Irp);

	// Nullify our WaitWakeIrp pointer to prevent anyone from dereferncing
	// the pointer once the IRP gets released as part of the completion process.
	// If it's already NULL, someone has gotten into CancelWaitWake in GenericWakeupControl,
	// meaning we have to avoid a race with IoCancelIrp.

	if (InterlockedExchangePointer((PVOID volatile *) &pdx->WaitWakeIrp, NULL))
		return STATUS_SUCCESS;	// we got here before anyone tried to cancel the IRP

	// CancelWaitWake has run. If wwcancelled is nonzero, so has the call to
	// IoCancelIrp and we can allow the IRP to complete. If wwcancelled is still zero
	// (left over from when we launched the IRP), IoCancelIrp hasn't been called yet
	// and we want to interrupt the completion process.

	if (InterlockedExchange(&pdx->wwcancelled, 1))
		return STATUS_SUCCESS;	// allow it to complete

	return STATUS_MORE_PROCESSING_REQUIRED; // let CancelWakeWait complete it
	}							// WaitWakeCompletionRoutine

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

NTSTATUS WritePowerInfoToRegistry(PGENERIC_EXTENSION pdx, PPOWERINFO pip)
	{							// WritePowerInfoToRegistry
	HANDLE hkey;
	NTSTATUS status = IoOpenDeviceRegistryKey(pdx->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_ALL_ACCESS, &hkey);
	if (!NT_SUCCESS(status))
		{
		DRIVER_LOG_ERROR(("Error %X trying to open registry key", status));
		return status;
		}

	UNICODE_STRING valname;
	RtlInitUnicodeString(&valname, L"PowerSettings");

	status = ZwSetValueKey(hkey, &valname, 0, REG_BINARY, pip, 2 * sizeof(ULONG));
	if (!NT_SUCCESS(status))
		DRIVER_LOG_ERROR(("ZwSetValueKey failed - %X", status));

	ZwClose(hkey);
	return status;
	}							// WritePowerInfoToRegistry

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#pragma LOCKEDCODE

NTSTATUS MainCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PPOWCONTEXT ctx);
NTSTATUS SequenceCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PPOWCONTEXT ctx);
VOID PoCompletionRoutine(PDEVICE_OBJECT junk, UCHAR fcn, POWER_STATE state, PPOWCONTEXT ctx, PIO_STATUS_BLOCK pstatus);
NTSTATUS SafePoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp);
VOID PassivePowerComplete(PPOWCONTEXT ctx) {HandlePowerEvent(ctx, AsyncNotify);}

NTSTATUS HandlePowerEvent(PPOWCONTEXT ctx, enum POWEVENT event)
	{							// HandlePowerEvent
	NTSTATUS status = -1;		// an invalid value
	ASSERT(ctx);
	ASSERT((ULONG) event < NUMPOWEVENTS);

	PIRP Irp = ctx->irp;		// could be null if we did an early S0 completion
	PIO_STACK_LOCATION stack = Irp ? IoGetCurrentIrpStackLocation(Irp) : NULL;

	PGENERIC_EXTENSION pdx = ctx->pdx;

	enum POWACTION {
		InvalidAction,			// code for invalid state/event combinations
		TriageNewIrp,			// decide what to do with new IRP
		QueueStallComplete,		// device queue has been stalled
		ForwardMainIrp,			// begin system or device IRP for more power
		SysPowerUpComplete,		// system power-up IRP completed
		SysPowerDownComplete,	// system power-down IRP completed
		SelectDState,			// choose D-state corresponding to main IRP's S-state
		SendDeviceIrp,			// send device IRP
		CompleteMainIrp,		// complete the main IRP
		DestroyContext,			// terminate FSM
		SubPowerUpComplete,		// nested power-up IRP finished or failed
		SubPowerDownComplete,	// nested power-down IRP finished or failed
		DevPowerUpComplete,		// device power-up IRP has completed
		SaveContext,			// save context in preparation for powering down
		ContextSaveComplete,	// device context has been saved
		ContextRestoreComplete,	// device context has been restored
		DevQueryUpComplete,		// device query for power-up complete
		DevQueryDown,			// see if device can power down
		DevQueryDownComplete,	// device query for power-down complete
		SendPowerSequence,		// create and send POWER_SEQUENCE request
		SaveSeqComplete,		// sequence fetch after context save complete
		ForwardDevDown,			// forward device set-power down IRP
		RestoreSeqComplete,		// sequence fetch before context save complete
		RestoreContext,			// restore device context
		};

#ifdef VERBOSETRACE
	static char* powstatenames[] = {
		"InitialState",
		"SysPowerUpPending",
		"SubPowerUpPending",
		"SubPowerDownPending",
		"SysPowerDownPending",
		"DevPowerUpPending",
		"DevPowerDownPending",
		"ContextSavePending",
		"ContextRestorePending",
		"DevQueryUpPending",
		"DevQueryDownPending",
		"QueueStallPending",
		"SaveSeqPending",
		"RestoreSeqPending",
		"PassiveCompletePending",
		"FinalState",
		};

	static char* eventnames[] = {
		"NewIrp",
		"MainIrpComplete",
		"AsyncNotify",
		};

	static char* actionnames[] = {
		"InvalidAction",
		"TriageNewIrp",
		"QueueStallComplete",
		"ForwardMainIrp",
		"SysPowerUpComplete",
		"SysPowerDownComplete",
		"SelectDState",
		"SendDeviceIrp",
		"CompleteMainIrp",
		"DestroyContext",
		"SubPowerUpComplete",
		"SubPowerDownComplete",
		"DevPowerUpComplete",
		"SaveContext",
		"ContextSaveComplete",
		"ContextRestoreComplete",
		"DevQueryUpComplete",
		"DevQueryDown",
		"DevQueryDownComplete",
		"SendPowerSequence",
		"SaveSeqComplete",
		"ForwardDevDown",
		"RestoreSeqComplete",
		"RestoreContext",
		};
#endif // VERBOSETRACE

	static enum POWACTION actiontable[NUMPOWSTATES][NUMPOWEVENTS] = {
/*							NewIrp				MainIrpComplete				AsyncNotify	*/
/* InitialState */			{TriageNewIrp,		InvalidAction,				InvalidAction},
/* SysPowerUpPending */		{InvalidAction,		SysPowerUpComplete,			InvalidAction},
/* SubPowerUpPending */		{InvalidAction,		InvalidAction,				SubPowerUpComplete},
/* SubPowerDownPending */	{InvalidAction,		InvalidAction,				SubPowerDownComplete},
/* SysPowerDownPending */	{InvalidAction,		SysPowerDownComplete,		InvalidAction},
/* DevPowerUpPending */		{InvalidAction,		DevPowerUpComplete,			InvalidAction},
/* DevPowerDownPending */	{InvalidAction,		CompleteMainIrp,			InvalidAction},
/* ContextSavePending */	{InvalidAction,		InvalidAction,				ContextSaveComplete},
/* ContextRestorePending */	{InvalidAction,		InvalidAction,				ContextRestoreComplete},
/* DevQueryUpPending */		{InvalidAction,		DevQueryUpComplete,			InvalidAction},
/* DevQueryDownPending */	{InvalidAction,		DevQueryDownComplete,		InvalidAction},
/* QueueStallPending */		{InvalidAction,		InvalidAction,				QueueStallComplete},
/* SaveSeqPending */		{InvalidAction,		InvalidAction,				SaveSeqComplete},
/* RestoreSeqPending */		{InvalidAction,		InvalidAction,				RestoreSeqComplete},
/* PassiveCompletePending */{InvalidAction,		InvalidAction,				CompleteMainIrp},
/* FinalState */			{InvalidAction,		InvalidAction,				InvalidAction},
		};

	// Determine the first action to take based on the current state of the FSM and the event that occurred.
	// Note that this isn't as complicated as the use of 2-D array might suggest: all states except
	// the initial state lead to a single action for the one-and-only event that's possible to get in
	// that state.

	enum POWACTION action = actiontable[ctx->state][event];

	// Structurally, the following code is a switch on "action" imbedded within an
	// infinite loop. A case that does a "break" from the switch executes a "break"
	// from the loop, whereupon we return whatever value is left in "status". A case
	// that does a "continue" from the switch repeats the loop -- this is how actions
	// can be strung together during one call to this routine. I coded it this way to
	// avoid return statements in the middle that make it harder to prove that the
	// routine behaves in a predictable way. Note that any "break" should be preceded
	// by a change to the state recorded in the context structure and to the initially
	// invalid valid of "status". There are ASSERTs at the end to check this.

	// Concerning the required change to "ctx->state": there are many cases where we
	// call PoRequestPowerIrp or PoCallDriver, whereupon the context structure gets
	// released before those routines return. We use a SETSTATE macro so we don't
	// have to dereference a possibly invalid "ctx" pointer at the end of the loop. Any
	// action that calls a routine that might result in completing the current IRP
	// should also take care not to touch "ctx" afterwards. (These are always cases that
	// "break" from the switch, so you can just verify that the break always immediately
	// follows the PoXxx call.)

	// Concerning the required change to "status": only TriageNewIrp
	// will arrange to return STATUS_PENDING. Many of the other initial actions are entered
	// from a standard I/O completion routine and will need to return STATUS_MORE_PROCESSING_REQUIRED
	// to hold off final completion. Any action for MainIrpComplete that goes out through
	// CompleteMainIrp will end up returning ctx->status, which gets set in MainCompletionRoutine
	// to whatever's in the IRP -- this allows the IRP to complete normally. Any action off of
	// AsyncNotify should be changing "status" explicitly (and they do -- I checked).

	// Note: avoid doing ASSERTs in this code because they're basically impossible to debug.
	// (Usually, the display is turned off before anything else, and then the serial port.
	// So the debugger is usually deaf and dumb when something goes wrong here.)

//#if DBG
//	enum POWSTATE originalstate = ctx->state;
//	enum POWSTATE nextstate = originalstate;
//	#define SETSTATE(s) ctx->state = nextstate = s
//	LONG eventid = InterlockedIncrement(&ctx->eventcount);
//	LONG ctxid = ctx->id;
//
//	(void)ctxid;
//	(void)eventid;
//#else
//	#define SETSTATE(s) ctx->state = s
//#endif
#define SETSTATE(s) ctx->state = s

	//POWTRACE(("%s - %d.%d is %s in state %s\n", pdx->DebugName, ctxid, eventid, eventnames[event], powstatenames[originalstate]));

	while (TRUE)
		{						// handle this event
		//POWTRACE(("%s -  %d.%d %s\n", pdx->DebugName, ctxid, eventid, actionnames[action]));
		switch (action)
			{					// perform next action

		///////////////////////////////////////////////////////////////////////
		// TriageNewIrp is the first action for a newly receive query or set IRP

		case TriageNewIrp:
			{					// TriageNewIrp

			// We want the power dispatch routine to return STATUS_PENDING unless
			// something goes wrong right away. If we do return STATUS_PENDING, we
			// need to be sure we mark the IRP pending, 

			status = STATUS_PENDING;
			IoMarkIrpPending(Irp);

			// Acquire remove lock an extra time. We'll release it when we eventually
			// complete this IRP.

			if (pdx->RemoveLock)
				IoAcquireRemoveLock(pdx->RemoveLock, Irp);

			// For a system IRP, we'll request the corresponding device IRP. If system power is
			// being restored, we wait until the lower level drivers finish the system IRP. If
			// system power is being removed, we do it now and forward the system IRP when the
			// device IRP finishes.

			if (stack->Parameters.Power.Type == SystemPowerState)
				{				// system IRP
				if (stack->Parameters.Power.State.SystemState < pdx->syspower)
					{
					action = ForwardMainIrp;
					SETSTATE(SysPowerUpPending);
					}
				else
					{
					action = SelectDState;
					SETSTATE(SubPowerDownPending);
					}
				}				// system IRP

			// For a device set-power IRP, we have a variety of tasks to carry out. If device
			// power is being restored, we do those tasks when the lower level drivers complete
			// the IRP. If device power is being removed or staying the same, we do those tasks
			// before passing this IRP down. In either case, we ensure that the device isn't busy
			// with any substantive IRPs first.

			else
				{				// device IRP
				SETSTATE(QueueStallPending);

				DEVICE_POWER_STATE newstate = stack->Parameters.Power.State.DeviceState;

				// Conceivably, we could cancel any outstanding wait-wake when going to a D-state
				// that doesn't support wakeup. In practice, the bus driver will do this for us.

				// According to the DDK, we're supposed to call PoSetPowerState any time we
				// change the power state, *before* calling PoStartNextPowerIrp and at IRQL
				// < DISPATCH_LEVEL (except = DISPATCH_LEVEL is okay for D0). In fact, it seems
				// to make no difference whether we do this or not, and bus drivers seem to make
				// all the necessary calls. Therefore, don't bother.

				// If the device is currently sleeping or off, the queue should already be
				// stalled. If the device is in D0, however, it might be servicing a
				// request right now, and there might be requests queued up behind the
				// active one. StallRequestsAndNotify will stall the queue and establish
				// a callback for when the client driver calls StartNextPacket.

				if (pdx->nqueues && !pdx->StalledForPower)
					{			// stall request queue

					// We mustn't touch the context structure after calling a function that
					// might recursively invoke this routine...

					ctx->UnstallQueue = TRUE;
					pdx->StalledForPower = TRUE;

					UCHAR minor = stack->MinorFunction;
					DEVICE_POWER_STATE oldstate = pdx->devpower;

					NTSTATUS qstatus = StallAllRequestsAndNotify(pdx->queues, pdx->nqueues, GenericSaveRestoreComplete, ctx);
					if (!NT_SUCCESS(qstatus))
						{		// can't stall queue
						ctx->status = qstatus;
						action = CompleteMainIrp;
						ctx->UnstallQueue = FALSE;
						pdx->StalledForPower = FALSE;
						continue;
						}		// can't stall queue

					// The UnstallQueue flag tells CompleteMainIrp to unstall the queue as part
					// of the cleanup process. We may clear this flag somewhere along the way
					// in order to keep the queue stalled during a period of low power.

					//POWTRACE(("%s -  %d.%d stalling queue\n", pdx->DebugName, ctxid, eventid));

					if (qstatus == STATUS_PENDING)
						{
						if (pdx->FlushPendingIo)
							(*pdx->FlushPendingIo)(pdx->DeviceObject, IRP_MJ_POWER, minor, oldstate, newstate);
						break;	// wait for notification that device is idle
						}
					}			// stall request queue

				action = QueueStallComplete;
				}				// device IRP

			continue;
			}					// TriageNewIrp
			
		///////////////////////////////////////////////////////////////////////
		// QueueStallComplete is the action for an AsyncNotify event in the
		// QueueStallPending state. It's reached when StartNextPacket calls
		// GenericSaveRestoreComplete, which we specified as the current-irp
		// complete notification routine in our earlier call to StallRequestsAndNotify.
		// This action can also be reached directly from TriageNewIrp if the
		// device was idle to begin with or if we were already in a low-power
		// state (so that the queue should have been stalled)

		case QueueStallComplete:
			{					// QueueStallComplete
			if (stack->MinorFunction == IRP_MN_SET_POWER)
				{				// device set-power IRP
				if (stack->Parameters.Power.State.DeviceState < pdx->devpower)
					{
					action = ForwardMainIrp;
					SETSTATE(DevPowerUpPending);
					}
				else
					action = SaveContext;
				}				// device set-power IRP
			else
				{				// device query-power IRP
				if (stack->Parameters.Power.State.DeviceState < pdx->devpower)
					{
					action = ForwardMainIrp;
					SETSTATE(DevQueryUpPending);
					}
				else
					action = DevQueryDown;
				}				// device query-power IRP
			continue;
			}					// QueueStallComplete
			
		///////////////////////////////////////////////////////////////////////
		// ForwardMainIrp sends the current power IRP to the next driver in the
		// stack. We regain control in MainCompletionRoutine.

		case ForwardMainIrp:
			{					// ForwardMainIrp
			IoCopyCurrentIrpStackLocationToNext(Irp);
			IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE) MainCompletionRoutine, (PVOID) ctx, TRUE, TRUE, TRUE);
			SafePoCallDriver(pdx->LowerDeviceObject, Irp); // avoid Win98 problem later on
			break;
			}					// ForwardMainIrp

		///////////////////////////////////////////////////////////////////////
		// SysPowerUpComplete is the action for a MainIrpComplete event in the
		// SysPowerUpPending state. If the IRP succeeded, request the corresponding
		// D-state IRP. When the subsidiary IRP finishes, we'll complete this
		// S-state IRP as well.
		//
		// The DDK doesn't explicitly say you need to send a D-state query when you
		// get an S-state query. It simplifies our own logic a good deal to do this,
		// however.

		case SysPowerUpComplete:
			{					// SysPowerUpComplete
			if (!NT_SUCCESS(ctx->status))
				action = CompleteMainIrp;
			else
				{				// S-irp succeeded
				action = SelectDState;
				SETSTATE(SubPowerUpPending);
				status = STATUS_MORE_PROCESSING_REQUIRED;	// defer completion of S-IRP

				if (stack->MinorFunction == IRP_MN_SET_POWER)
					{			// S-set
					pdx->syspower = stack->Parameters.Power.State.SystemState;
					
					// Except in 98/Me, complete an S0 IRP right away to allow
					// faster restart after suspend.

					if (pdx->syspower == PowerSystemWorking && !win98)
						{		// S0
						//POWTRACE(("%s - %d.%d Completing S0 IRP early\n", pdx->DebugName, ctxid, eventid));
						PoStartNextPowerIrp(Irp);
						status = STATUS_SUCCESS;	// allows S-IRP to complete
						if (pdx->RemoveLock)
							IoReleaseRemoveLock(pdx->RemoveLock, Irp);
						ctx->irp = NULL;				// flag for eventual CompleteMainIrp step
						}		// S0
					}			// S-set
				}				// S-irp succeeded

			continue;
			}					// SysPowerUpComplete

		///////////////////////////////////////////////////////////////////////
		// SysPowerDownComplete is the action for a MainIrpComplete event in the
		// SysPowerDownPending state.

		case SysPowerDownComplete:
			{					// SysPowerDownComplete
			if (!NT_SUCCESS(ctx->status))
				DRIVER_LOG_ERROR(("%s - System power-down IRP failed - %X\n", pdx->DebugName, ctx->status));

			if (stack->MinorFunction == IRP_MN_SET_POWER)
				pdx->syspower = stack->Parameters.Power.State.SystemState;	// (note that this IRP could not legally have been failed)

			action = CompleteMainIrp;
			continue;
			}					// SysPowerDownComplete

		///////////////////////////////////////////////////////////////////////
		// SelectDState is used to establish the power state and minor function
		// code for a D-state IRP that corresponds to the S-state IRP we're
		// processing. After doing that, we do the SendDeviceIrp action.

		case SelectDState:
			{					// SelectDState
			SYSTEM_POWER_STATE sstate = stack->Parameters.Power.State.SystemState;

			ctx->devstate = GetLowestDevicePowerState(pdx, sstate);
			ctx->MinorFunction = stack->MinorFunction;
			action = SendDeviceIrp;
			continue;
			}					// SelectDState

		///////////////////////////////////////////////////////////////////////
		// SendDeviceIrp requests a device set- or query-power IRP using the power
		// state and minor function code currently in the context block. SelectDState
		// put them there.

		case SendDeviceIrp:
			{					// SendDeviceIrp

			// If we want the device in the same state it's already in, bypass sending
			// the D-state IRP. This is necessary in Win98 due to a bug that causes
			// PoRequestPowerIrp to report success in a situation where CONFIGMG won't
			// generate the configuration event. This has also turned out to be necessary
			// when bringing Win2K/Xp out of standby or hibernate, although I don't
			// exactly understand why.

			if (ctx->devstate == pdx->devpower)
				{				// pretend success
				//POWTRACE(("%s -  %d.%d pretending to succeed D-state IRP for same state\n", pdx->DebugName, ctxid, eventid));
				ctx->status = STATUS_SUCCESS;
				action = actiontable[ctx->state][AsyncNotify];
				continue;
				}				// pretend success

			// Ask the power manager to send us an IRP. In Win98, we need to supply the
			// PDO as the device object address because NTKERN needs to go directly from
			// there to the devnode address.

			POWER_STATE powstate;
			powstate.DeviceState = ctx->devstate;

			NTSTATUS postatus = PoRequestPowerIrp(pdx->Pdo, ctx->MinorFunction, powstate,
				(PREQUEST_POWER_COMPLETE) PoCompletionRoutine, ctx, NULL);

			// If PoRequestPowerIrp fails, it never actually sent an IRP down the stack,
			// so we can certain that PoCompletionRoutine never ran

			if (NT_SUCCESS(postatus))
				break;			// started device IRP okay

			DRIVER_LOG_ERROR(("%s - PoRequestPowerIrp failed - %X\n", pdx->DebugName, postatus));
			action = CompleteMainIrp;
			ctx->status = postatus;
			continue;
			}					// SendDeviceIrp

		///////////////////////////////////////////////////////////////////////
		// CompleteMainIrp is the penultimate action of the finite state machine.
		// This is where we actually complete the power IRP we've been handling.

		case CompleteMainIrp:
			{					// CompleteMainIrp

			// In Win98, we must not complete a power IRP at DISPATCH_LEVEL, so
			// queue a work item in that case.

			if (win98 && KeGetCurrentIrql() > PASSIVE_LEVEL)
				{				// defer completion to passive level
				#pragma warning(disable:4995)
				#pragma warning(disable:4996)
				C_ASSERT(sizeof(Irp->Tail.Overlay.DriverContext) >= sizeof(WORK_QUEUE_ITEM));

				PWORK_QUEUE_ITEM item = (PWORK_QUEUE_ITEM) Irp->Tail.Overlay.DriverContext;
				ExInitializeWorkItem(item, (PWORKER_THREAD_ROUTINE) PassivePowerComplete, (PVOID) ctx);
				ExQueueWorkItem(item, CriticalWorkQueue);

				#pragma warning(default:4995)
				#pragma warning(default:4996)

				if (event == MainIrpComplete)
					status = STATUS_MORE_PROCESSING_REQUIRED;
				SETSTATE(PassiveCompletePending);
				break;
				}				// defer completion to passive level

			// Skip completion processing if we've already completed an S0 SET_POWER.
			// Otherwise, complete (or allow to complete) the main IRP

			if (Irp)
				{				// not previously completed

				// Tell the power manager to release the next IRP of the same flavor
				// as we're currently processing.

				PoStartNextPowerIrp(Irp);

				// If called from MainCompletionRoutine, just allow the completion process
				// to take its course. Otherwise, explicitly complete the main IRP.

				if (event == MainIrpComplete)
					status = ctx->status;	// might have been STATUS_MORE_PROCESSING_REQUIRED until now
				else
					{
					Irp->IoStatus.Status = ctx->status;
					IoCompleteRequest(Irp, IO_NO_INCREMENT);
					
					if (status == -1)
						status = STATUS_SUCCESS;
					}

				// Release the remove lock to balance the extra acquisition in TriageNewIrp
				
				if (pdx->RemoveLock)
					IoReleaseRemoveLock(pdx->RemoveLock, Irp);
				}				// not previously completed

			// If we stalled the request queue for the pendency of this IRP, unstall it now.
			// Sometimes, we will have cleared the flag in order to leave the queue stalled
			// after completing a power-down.

			if (ctx->UnstallQueue)
				{
				pdx->StalledForPower = FALSE;
				//POWTRACE(("%s -  %d.%d unstalling queue\n", pdx->DebugName, ctxid, eventid));
				RestartAllRequests(pdx->queues, pdx->nqueues, pdx->DeviceObject);
				}
			
			action = DestroyContext;
			continue;
			}					// CompleteMainIrp

		///////////////////////////////////////////////////////////////////////
		// DestroyContext is the last action for an IRP.

		case DestroyContext:
			{					// DestroyContext
			SETSTATE(FinalState);
			ExFreePool(ctx);
			break;
			}					// DestroyContext

		///////////////////////////////////////////////////////////////////////
		// SubPowerUpComplete is the action for a AsyncNotify event in the
		// SubPowerUpPending state. This should be called from PoCompletionRoutine.
		// We can also get here from SendDeviceIrp to avoid the Win98 no-D-IRP bug,
		// in which case we don't want to alter "status" from its current value.

		case SubPowerUpComplete:
			{					// SubPowerUpComplete
			if (status == -1)
				status = STATUS_SUCCESS; // don't actually care, since called from PoCompletionRoutine
			action = CompleteMainIrp;
			continue;
			}					// SubPowerUpComplete

		///////////////////////////////////////////////////////////////////////
		// SubPowerDownComplete is the action for a AsyncNotify event in the
		// SubPowerDownPending state. This should be called from PoCompletionRoutine.
		// We can also get here from SendDeviceIrp to avoid the Win98 no-D-IRP bug,
		// in which case we don't want to alter "status" from its current value.

		case SubPowerDownComplete:
			{					// SubPowerDownComplete
			if (status == -1)
				status = STATUS_SUCCESS; // don't actually care, since called from PoCompletionRoutine

			if (NT_SUCCESS(ctx->status))
				{
				SETSTATE(SysPowerDownPending);
				action = ForwardMainIrp;
				}
			else
				action = CompleteMainIrp; // D-state IRP failed, so fail S-state IRP too
			
			continue;
			}					// SubPowerDownComplete

		///////////////////////////////////////////////////////////////////////
		// DevPowerUpComplete is the action for a MainIrpComplete event in the
		// DevPowerUpPending state. This should be called from MainCompletionRoutine
		// when a device power-up IRP finishes in the lower layers.

		case DevPowerUpComplete:
			{					// DevPowerUpComplete

			// If this IRP failed, or if we're just dealing with a query, we're done.

			if (!NT_SUCCESS(ctx->status) || stack->MinorFunction != IRP_MN_SET_POWER)
				{
				action = CompleteMainIrp;
				continue;
				}

			status = STATUS_MORE_PROCESSING_REQUIRED; // defer completion of the main IRP while we restore context

			ctx->oldpower = pdx->devpower;
			pdx->devpower = stack->Parameters.Power.State.DeviceState;

			//KdPrint(("%s - Now in %s\n", pdx->DebugName, devstate[pdx->devpower]));

			// Since no bus driver currently implements IRP_MN_POWER_SEQUENCE, you
			// don't really need to bother with it. But doing it correctly is enough of a pain
			// in the neck that it's worthwhile showing you how to do it just in case it ever
			// becomes relevant. I thought this was marginal enough that I didn't discuss
			// the extra states in the book, though. In the book, I showed this action as
			// including the stuff that's in RestoreContext, too. [SP-10: don't do this unless
			// PerfBoundary is nonzero. The comment in generic.h says that leaving it zero
			// will suppress the IRP, and a reader reported a problem with PCMCIA.SYS barfing
			// on one of them.]

			if (pdx->PerfBoundary && ctx->oldpower >= pdx->PerfBoundary)
				{				// context restore will be expensive
				SETSTATE(RestoreSeqPending);
				action = SendPowerSequence;
				continue;
				}				// context restore will be expensive

			action = RestoreContext;
			continue;
			}					// DevPowerUpComplete

		///////////////////////////////////////////////////////////////////////
		// ContextRestoreComplete is the last action for a device set power up
		// operation. It's ordinarily reached when GenericSaveRestoreComplete
		// signals a MainIrpComplete event from the ContextRestorePending state.
		// It can also be reached directly from DevPowerUpComplete when there is
		// no context restore function.

		case ContextRestoreComplete:
			{					// ContextRestoreComplete
			if (event == AsyncNotify)
				status = STATUS_SUCCESS; // doesn't actually matter
			action = CompleteMainIrp;

			// If the device IRP failed, just go ahead and let it complete. If we've
			// successfully resumed to a sleeping state (> D0), skip restarting the
			// substantive IRP queue and complete the IRP as well.

			if (!NT_SUCCESS(ctx->status) || pdx->devpower != PowerDeviceD0)
				continue;

			// We've just gone to D0, unstall the request queue by setting
			// the flag that will cause CompleteMainIrp to do so. It's very
			// important in our scheme that we stall the queue for any device
			// IRP that arrives while we're in D0 and omit to unstall it when
			// we complete a SET for a lower power state. We thereby arrange
			// to stall the queue just once across an entire period of low
			// power.

			if (pdx->nqueues)
				ctx->UnstallQueue = TRUE;

			continue;
			}					// ContextRestoreComplete

		///////////////////////////////////////////////////////////////////////
		// SaveContext initiates a context save operation if necessary. This will
		// be the second action for a new device set-power IRP.

		case SaveContext:
			{					// SaveContext
			DEVICE_POWER_STATE devpower = stack->Parameters.Power.State.DeviceState;

			// If we're actually changing to a lower power state, give the client driver
			// a chance to save any device context information asynchronously. It will
			// call GenericSaveRestoreComplete when it finishes.

			if (pdx->SaveDeviceContext && devpower > pdx->devpower)
				{				// save context
				SETSTATE(ContextSavePending);
				(*pdx->SaveDeviceContext)(pdx->DeviceObject, pdx->devpower, devpower, ctx);
				break;
				}				// save context

			action = ContextSaveComplete;
			continue;
			}					// SaveContext

		///////////////////////////////////////////////////////////////////////
		// ContextSaveComplete is the action for an AsyncNotify event in the
		// ContextSavePending state. It should be entered from GenericSaveRestoreComplete,
		// which in turn should have been called by the client driver when its
		// context save operation finished. It can also be entered directly from
		// SaveContext when there is no context save routine.

		case ContextSaveComplete:
			{					// ContextSaveComplete
			if (event == AsyncNotify)
				status = STATUS_SUCCESS;	// doesn't actually matter in this case

			// Obtain a set of power-state sequence numbers to optimize state restoration later.
			// In the book, I didn't include this bit of nonsense. The code in ForwardDevDown
			// appears right here in the book. [SP-10: see note elsewhere]

			if (pdx->PerfBoundary && stack->Parameters.Power.State.DeviceState >= pdx->PerfBoundary)
				{				// context restore will be expensive
				SETSTATE(SaveSeqPending);
				action = SendPowerSequence;
				continue;
				}				// context restore will be expensive/

			action = ForwardDevDown;
			continue;
			}					// ContextSaveComplete

		///////////////////////////////////////////////////////////////////////
		// DevQueryUpComplete is the action for a MainIrpComplete event in the
		// DevQueryUpPending state. This should be called by MainCompletionRoutine
		// when a device query-power-up IRP completes. We don't expect to ever get this
		// kind of a query, by the way, but we should handle it nontheless.

		case DevQueryUpComplete:
			{					// DevQueryUpComplete
			if (NT_SUCCESS(ctx->status) && pdx->QueryPower)
				{				// ask client if change okay

				// Everybody belows us has acquiesced in restoring power. Ask the
				// client driver if it's cool by this too.

				if (!(*pdx->QueryPower)(pdx->DeviceObject, pdx->devpower, stack->Parameters.Power.State.DeviceState))
					ctx->status = STATUS_UNSUCCESSFUL; // fail the query
				}				// ask client if change okay

			action = CompleteMainIrp;
			continue;
			}					// DevQueryUpComplete

		///////////////////////////////////////////////////////////////////////
		// DevQueryDown is the second action (after TriageNewIrp) for a device
		// query-power that specifies less than or equal to the current device
		// power state.

		case DevQueryDown:
			{					// DevQueryDown
			DEVICE_POWER_STATE devpower = stack->Parameters.Power.State.DeviceState;

			// If the proposed power state is less than the current state,
			// ask the client driver if it's okay to change.

			if (devpower > pdx->devpower && pdx->QueryPower && !(*pdx->QueryPower)(pdx->DeviceObject, pdx->devpower, devpower))
				{				// fail the query
				ctx->status = STATUS_UNSUCCESSFUL;
				action = DevQueryDownComplete;
				continue;
				}				// fail the query

			SETSTATE(DevQueryDownPending);
			action = ForwardMainIrp;
			continue;
			}					// DevQueryDown

		///////////////////////////////////////////////////////////////////////
		// DevQueryDownComplete is the action for a MainIrpComplete event in the
		// DevQueryDownPending state. It can be reached from MainCompletionRoutine
		// or directly from DevQueryDown.

		case DevQueryDownComplete:
			{					// DevQueryDownComplete

			// The Windows 98 version of the USB hub driver fails device power
			// query operations, which is incorrect...

			if (ctx->status == STATUS_INVALID_PARAMETER)
				ctx->status = STATUS_SUCCESS;

			// If the query is succeeding, leave the request queue stalled

			if (NT_SUCCESS(ctx->status))
				ctx->UnstallQueue = FALSE;

			action = CompleteMainIrp;
			continue;
			}					// DevQueryDownComplete

		///////////////////////////////////////////////////////////////////////
		// SendPowerSequence creates an IRP_MN_POWER_SEQUENCE request. It can be
		// entered from DevPowerUpComplete or ContextSaveComplete

		case SendPowerSequence:
			{					// SendPowerSequence
			PIRP seqirp = IoAllocateIrp(pdx->LowerDeviceObject->StackSize, FALSE);
			if (seqirp)
				{				// sequence IRP created
				PIO_STACK_LOCATION seqstack = IoGetNextIrpStackLocation(seqirp);
				seqstack->MajorFunction = IRP_MJ_POWER;
				seqstack->MinorFunction = IRP_MN_POWER_SEQUENCE;
				seqstack->Parameters.PowerSequence.PowerSequence = &ctx->sequence;

				IoSetCompletionRoutine(seqirp, (PIO_COMPLETION_ROUTINE) SequenceCompletionRoutine, (PVOID) ctx, TRUE, TRUE, TRUE);

				PoCallDriver(pdx->LowerDeviceObject, seqirp);
				break;
				}				// sequence IRP created
			else
				{				// couldn't create IRP
				if (ctx->state == SaveSeqPending)
					action = ForwardDevDown;
				else
					action = RestoreContext;

				continue;
				}				// couldn't create IRP
			}					// SendPowerSequence

		///////////////////////////////////////////////////////////////////////
		// SaveSeqComplete is the action for an AsyncNotify event in the
		// SaveSeqPending state. It should be entered from SequenceCompletionRoutine
		// when an IRP_MN_POWER_SEQUENCE request completes.

		case SaveSeqComplete:
			{					// SaveSeqComplete
			if (NT_SUCCESS(ctx->status))
				{
				//KdPrint(("%s - Someone actually succeeded a POWER_SEQUENCE request!\n", pdx->DebugName));
				pdx->oldseq = ctx->sequence; // save sequence numbers for power-up
				}
			else
				RtlZeroMemory(&pdx->oldseq, sizeof(pdx->oldseq));

			status = STATUS_MORE_PROCESSING_REQUIRED; // won't actually be used
			ctx->status = STATUS_SUCCESS;	// status from POWER_SEQUENCE not actually relevant

			action = ForwardDevDown;
			continue;
			}					// SaveSeqComplete

		///////////////////////////////////////////////////////////////////////
		// ForwardDevDown forwards a device set-power down IRP. It can be reached
		// from SaveSeqComplete or from ContextSaveComplete.

		case ForwardDevDown:
			{					// ForwardDevDown
			SETSTATE(DevPowerDownPending);
			action = ForwardMainIrp;

			DEVICE_POWER_STATE devpower = stack->Parameters.Power.State.DeviceState;
			if (devpower <= pdx->devpower)
				continue;		// no actual change in device power

			pdx->devpower = devpower;

			//KdPrint(("%s - Now in %s\n", pdx->DebugName, devstate[pdx->devpower]));

			// If power has now been removed, leave the queue stalled

			if (devpower > PowerDeviceD0)
				ctx->UnstallQueue = FALSE;

			continue;
			}					// ForwardDevDown

		///////////////////////////////////////////////////////////////////////
		// RestoreSeqComplete is the action for an AsyncNotify event in the
		// RestoreSeqPending state. It should be entered from SequenceCompletionRoutine
		// when an IRP_MN_POWER_SEQUENCE request completes

		case RestoreSeqComplete:
			{					// RestoreSeqComplete

			// Now decide if we really need to restore the device context by comparing
			// the sequence numbers for the performance boundary state

			action = RestoreContext;	// assume we'll want to restore context

			if (NT_SUCCESS(ctx->status))
				switch (pdx->PerfBoundary)
					{			// check relevant sequence numbers
					
				case PowerDeviceD1:
					if (ctx->sequence.SequenceD1 <= pdx->oldseq.SequenceD1)
						action = ContextRestoreComplete;
					break;
					
				case PowerDeviceD2:
					if (ctx->sequence.SequenceD2 <= pdx->oldseq.SequenceD2)
						action = ContextRestoreComplete;
					break;
					
				case PowerDeviceD3:
					if (ctx->sequence.SequenceD3 <= pdx->oldseq.SequenceD3)
						action = ContextRestoreComplete;
					break;

					}			// check relevant sequence numbers

		//#if DBG
		//	if (action != RestoreContext)
		//		KdPrint(("%s - Bypassing context restore due to sequence number optimization\n", pdx->DebugName));
		//#endif

			ctx->status = STATUS_SUCCESS; // ignore status from POWER_SEQUENCE
			status = STATUS_MORE_PROCESSING_REQUIRED;

			continue;
			}					// RestoreSeqComplete

		///////////////////////////////////////////////////////////////////////
		// RestoreContext restores device context if necessary. It can be entered
		// from RestoreSeqComplete or from DevPowerUpComplete

		case RestoreContext:
			{					// RestoreContext

			// Allow the client driver to restore any device context. This occurs
			// asynchronously. The client notifies us when the the restore operation
			// finishes by calling GenericSaveRestoreComplete.

			if (pdx->RestoreDeviceContext)
				{				// restore context
				SETSTATE(ContextRestorePending);
				(*pdx->RestoreDeviceContext)(pdx->DeviceObject, ctx->oldpower, pdx->devpower, ctx);
				break;
				}				// restore context

			action = ContextRestoreComplete;
			continue;
			}					// RestoreContext

		///////////////////////////////////////////////////////////////////////
		// InvalidAction is the action for any unexpected event. It should never occur.

		case InvalidAction:
		default:
			status = STATUS_UNSUCCESSFUL;
			break;
			}					// perform next action

		break;					// for cases that "break" from the switch
		}						// handle this event

#if DBG

	// Check to make sure the state got changed before we exit

	//if (nextstate == originalstate)
	//	KdPrint(("%s - State didn't change from %d in HandlePowerEvent!\n", pdx->DebugName, originalstate));

	// Check to make sure a valid status will be returned

	//if (status == -1)
	//	KdPrint(("%s - returned status didn't change in HandlePowerEvent!\n", pdx->DebugName));

#endif // DBG

	//POWTRACE(("%s - %d.%d returning %X, state is %s\n", pdx->DebugName, ctxid, eventid, status, powstatenames[nextstate]));

	return status;
	}							// HandlePowerEvent

///////////////////////////////////////////////////////////////////////////////

NTSTATUS MainCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PPOWCONTEXT ctx)
	{							// MainCompletionRoutine
	(void)junk;
	ctx->status = Irp->IoStatus.Status;
	return HandlePowerEvent(ctx, MainIrpComplete);
	}							// MainCompletionRoutine

NTSTATUS SequenceCompletionRoutine(PDEVICE_OBJECT junk, PIRP Irp, PPOWCONTEXT ctx)
	{							// SequenceCompletionRoutine
	(void)junk;
	ctx->status = Irp->IoStatus.Status;
	HandlePowerEvent(ctx, AsyncNotify);
	IoFreeIrp(Irp);
	return STATUS_MORE_PROCESSING_REQUIRED;
	}							// SequenceCompletionRoutine

VOID PoCompletionRoutine(PDEVICE_OBJECT junk, UCHAR fcn, POWER_STATE state, PPOWCONTEXT ctx, PIO_STATUS_BLOCK pstatus)
	{							// PoCompletionRoutine
	(void)junk;
	(void)fcn;
	(void)state;
	ctx->status = pstatus->Status;
	HandlePowerEvent(ctx, AsyncNotify);
	}							// PoCompletionRoutine

VOID PassivePowerCall(PIRP Irp)
	{							// PassivePowerCall
	PoCallDriver(IoGetNextIrpStackLocation(Irp)->DeviceObject, Irp);
	}							// PassivePowerCall

NTSTATUS SafePoCallDriver(PDEVICE_OBJECT DeviceObject, PIRP Irp)
	{							// SafePoCallDriver

	// If running in Win2K, or if Win98 and already at PASSIVE_LEVEL, just call
	// PoCallDriver.

	if (!win98 || KeGetCurrentIrql() == PASSIVE_LEVEL)
		return PoCallDriver(DeviceObject, Irp);
	
	// Win98's PoCallDriver is the same as IoCallDriver, and it won't do anything to
	// present the IRP at passive level if we're currently above. Build a work queue
	// item in the DriverContext field of the IRP and queue the work item so we
	// can present the IRP properly. Boy, is this something we shouldn't have to
	// worry about!

	IoMarkIrpPending(Irp);		// be sure it's marked pending
	IoGetNextIrpStackLocation(Irp)->DeviceObject = DeviceObject; // so PassivePowerCall can find it

	// Note: The ExXxxWorkItem functions are deprecated for Win2K and WinXp but
	// okay for win98. This code only executes in Win98, so these should be okay

	#pragma warning(disable:4995)
	#pragma warning(disable:4996)
	C_ASSERT(sizeof(Irp->Tail.Overlay.DriverContext) >= sizeof(WORK_QUEUE_ITEM));

	PWORK_QUEUE_ITEM item = (PWORK_QUEUE_ITEM) Irp->Tail.Overlay.DriverContext;
	ExInitializeWorkItem(item, (PWORKER_THREAD_ROUTINE) PassivePowerCall, (PVOID) Irp);
	ExQueueWorkItem(item, CriticalWorkQueue);

	#pragma warning(default:4995)
	#pragma warning(default:4996)

	return STATUS_PENDING;
	}							// SafePoCallDriver