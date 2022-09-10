// Declarations for Generic driver
// Copyright (C) 1999 by Walter Oney
// All rights reserved

#ifndef DRIVER_H
#define DRIVER_H

#define GENERIC_INTERNAL
#include "Generic.h"

#ifndef _BYTE_DEFINED
#define _BYTE_DEFINED
typedef unsigned char BYTE;
#endif // !_BYTE_DEFINED

#ifndef _WORD_DEFINED
#define _WORD_DEFINED
typedef unsigned short WORD;
#endif // !_WORD_DEFINED

// Remove the comment from the following line in order to build a version of
// GENERIC that works correctly on the first release of Win98. (There is a bug
// in this release such that the first call to a DllInitialize routine results
// in an error that prevents the driver from loading.) You will also need to modify
// the .DEF file to remove the export declarations for DllInitialize and DllUnload

//#define WIN98SAFE

#if defined F135
#define DRIVERNAME "F135USB3"				// for use in messages
#define LDRIVERNAME L"F135USB3"				// for use in messages
#elif defined F235
#define DRIVERNAME "F235USB3"				// for use in messages
#define LDRIVERNAME L"F235USB3"				// for use in messages
#elif defined F335
#define DRIVERNAME "F335USB3"				// for use in messages
#define LDRIVERNAME L"F335USB3"				// for use in messages
#else
#error "Not sure what scanner we're compiling for"
#endif

#define S(x) #x
#define S_(x) S(x)
#define S__LINE__ S_(__LINE__)

#ifdef DBG
#define _DRIVER_LOG(level, _x_) do { \
	DbgPrint((DRIVERNAME " - " __FUNCTION__ ":" S__LINE__ " "));\
	DbgPrint(("[" level "]: "));\
	DbgPrint _x_ ;\
	DbgPrint(("\n"));\
	} while (0);
#else
#define _DRIVER_LOG(level, _x_) do {} while (0);
#endif

#define DRIVER_LOG_INFO(_x_) _DRIVER_LOG("INFO", _x_)
#define DRIVER_LOG_ERROR(_x_) _DRIVER_LOG("ERROR", _x_)

///////////////////////////////////////////////////////////////////////////////
// Device extension structure

enum DEVSTATE {
	STOPPED,								// device stopped
	WORKING,								// started and working
	PENDINGSTOP,							// stop pending
	PENDINGREMOVE,							// remove pending
	SURPRISEREMOVED,						// removed by surprise
	REMOVED,								// removed
	};

typedef struct _GENERIC_EXTENSION {
	PDEVICE_OBJECT DeviceObject;			// device object this extension belongs to
	PDEVICE_OBJECT LowerDeviceObject;		// next lower driver in same stack
	PDEVICE_OBJECT Pdo;						// the PDO
	PGENERIC_REMOVE_LOCK RemoveLock;		// removal control locking structure
	DEVSTATE state;							// current state of device
	DEVSTATE prevstate;						// state prior to removal query
	DEVICE_POWER_STATE devpower;			// current device power state
	SYSTEM_POWER_STATE syspower;			// current system power state
	PULONG idlecount;						// address of idle counter from idle detection registration
	ULONG cto;								// conservation timeout
	ULONG pto;								// performance timeout
	DEVICE_POWER_STATE idlestate;			// idle state requested by minidriver
	DEVICE_CAPABILITIES devcaps;			// copy of most recent device capabilities
	ULONG nqueues;							// number of queues
	PDEVQUEUE* queues;						// IRP queues
	PSTART_DEVICE StartDevice;				// routine to initialize device configuration
	PSTOP_DEVICE StopDevice;				// routine to release device configuration
	PREMOVE_DEVICE RemoveDevice;			// routine to remove device object
	PQUERYFUNCTION OkayToStop;				// okay to stop device now?
	PQUERYFUNCTION OkayToRemove;			// okay to remove device now?
	PFLUSHIOFUNCTION FlushPendingIo;		// encourage pending I/O to finish
	PGETDSTATEFUNCTION GetDevicePowerState;	// get D-state for given S-state
	CHAR DebugName[64];						// name for debugging messages
	ULONG Flags;							// flags from initialization call
	LONG npagefiles;						// # page files about which we've been notified
	LONG ndumpfiles;						// # dump ditto
	LONG nhibernatefiles;					// # hibernate ditto
	LIST_ENTRY PendingIoctlList;			// list of asynchronous IOCTLs
	KSPIN_LOCK IoctlListLock;				// lock for pending IOCTLs
	NTSTATUS IoctlAbortStatus;				// abort incoming IOCTLs if nonzero
	PQUERYPOWERFUNCTION QueryPower;			// okay to change device power?
	PCONTEXTFUNCTION SaveDeviceContext;		// routine to save device context
	PCONTEXTFUNCTION RestoreDeviceContext;	// routine to restore device context
	PNP_DEVICE_STATE pnpstate;				// new PnP state bits
	PNP_DEVICE_STATE pnpstatemask;			// mask of changed state bits
	DEVICE_POWER_STATE PerfBoundary;		// context restore very expensive from >= this power state
	PIRP WaitWakeIrp;						// outstanding WAIT_WAKE request (if any)
	LONG wwcancelled;						// non-zero when WAIT_WAKE has been cancelled
	LONG wwoutstanding;						// non-zero between PoRequestPowerIrp and WaitWaitCallback
	POWER_SEQUENCE oldseq;					// old power sequence numbers
	KEVENT iflock;							// for interlocking use of interface list
	LIST_ENTRY iflist;						// list of registered device interfaces
	KEVENT evPagingPath;					// synch event used as a mutex for paging path notifications
	BOOLEAN HardwareWorking;				// we should touch hardware at stop-device time
	BOOLEAN WakeupEnabled;					// device wakeup feature is enabled
	BOOLEAN StalledForPower;				// power management has stalled IRP queue
	BOOLEAN GotCapabilities;				// capabilities have been gotten already
	} GENERIC_EXTENSION;

///////////////////////////////////////////////////////////////////////////////

typedef struct _INTERFACE_RECORD {
	LIST_ENTRY list;						// linking fields
	GUID guid;								// guid for registered interface
	UNICODE_STRING linkname;				// symbolic link name
	BOOLEAN enabled;						// true if interface currently enabled
	} INTERFACE_RECORD, *PINTERFACE_RECORD;

///////////////////////////////////////////////////////////////////////////////
// Global functions

struct _POWERINFO;

VOID AbortPendingIoctls(PGENERIC_EXTENSION pdx, NTSTATUS status);
VOID AdjustSpecialFileCounter(PGENERIC_EXTENSION pdx, BOOLEAN inpath, PLONG counter);
NTSTATUS CompleteRequest(PIRP Irp, NTSTATUS status, ULONG_PTR info);
NTSTATUS CompleteRequest(PIRP Irp, NTSTATUS status);
NTSTATUS ForwardAndWait(PGENERIC_EXTENSION pdx, PIRP Irp);
NTSTATUS GetPowerInfoFromRegistry(PGENERIC_EXTENSION pdx, _POWERINFO* pip);
NTSTATUS ImplementPowerPolicy(PGENERIC_EXTENSION pdx, _POWERINFO* pip);
BOOLEAN OkayToRemove(PGENERIC_EXTENSION pdx);
BOOLEAN OkayToStop(PGENERIC_EXTENSION pdx);
NTSTATUS SendDeviceSetPower(PGENERIC_EXTENSION pdx, DEVICE_POWER_STATE state, BOOLEAN wait = FALSE);
NTSTATUS WritePowerInfoToRegistry(PGENERIC_EXTENSION pdx, _POWERINFO* pip);

extern BOOLEAN win98;						// true if running under Win98

#define ALLOC_PAGED_TAG(size, tag) ExAllocatePool2(POOL_FLAG_PAGED, (size), (tag))
#define ALLOC_PAGED(size) ALLOC_PAGED_TAG(size, ' mdW')
#define ALLOC_NON_PAGED_TAG(size, tag) ExAllocatePool2(POOL_FLAG_NON_PAGED, (size), (tag))
#define ALLOC_NON_PAGED(size) ALLOC_NON_PAGED_TAG(size, ' mdW')

#endif // DRIVER_H
