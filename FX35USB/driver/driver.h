#pragma once

#include <wdm.h>
#include <usb.h>
#include "../../generic/driver.h"

typedef struct _DEVICE_EXTENSION
{
	PDEVICE_OBJECT DeviceObject;
	PDEVICE_OBJECT LowerDeviceObject;
	PDEVICE_OBJECT Pdo;
	GENERIC_REMOVE_LOCK RemoveLock;
	UNICODE_STRING devname;
	DEVQUEUE dqReadWrite;
	PGENERIC_EXTENSION pgx;
	LONG handles;
	USB_DEVICE_DESCRIPTOR dd;
	USBD_CONFIGURATION_HANDLE ConfigurationHandle;
	PUSB_CONFIGURATION_DESCRIPTOR pcd;
	LANGID langid;
	void* DirectReadHandle;
	void* DirectWriteHandle;
	USBD_PIPE_HANDLE RingReadPipe;
	USBD_PIPE_HANDLE RingWritePipe;
	LONG numfailed;
	ULONG maxtransfer;
	ULONG LastFailedUrbStatus;
	// GENERIC_EXTENSION follows
} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

NTSTATUS __stdcall SendAwaitUrb(PDEVICE_OBJECT fdo, PURB urb);
VOID __stdcall AbortPipe(PDEVICE_OBJECT fdo, USBD_PIPE_HANDLE PipeHandle);
NTSTATUS __stdcall ResetPipe(PDEVICE_OBJECT fdo, USBD_PIPE_HANDLE PipeHandle);
NTSTATUS __stdcall ResetDevice(PDEVICE_OBJECT fdo);