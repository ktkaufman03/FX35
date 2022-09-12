#pragma once

#include <wdm.h>
#include <usb.h>
#include "ringtail.h"

#pragma warning(disable: 4200)
typedef struct _RWCONTEXT
{
	PIRP mainirp;
	NTSTATUS status;
	ULONG_PTR info;
	ULONG numirps;
	LONG numpending;
	LONG refcnt;
	PRING_TAIL pRingTail;
	PMDL RingPacketMdl;
	USBD_PIPE_HANDLE hpipe;
	PDEVICE_OBJECT fdo;
	bool cancel;
	KPROCESSOR_MODE RequestorMode;
	PKEVENT EventScanPacketReady;

	_Field_size_(numirps)
	RING_PACKET RingPackets[];
} RWCONTEXT, * PRWCONTEXT;

#define RW_CONTEXT_SIZE(numirps) (offsetof(RWCONTEXT, RingPackets) + sizeof(RING_PACKET)*(numirps))

NTSTATUS __stdcall AllDone(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRWCONTEXT Context);
void __stdcall OnCancelReadWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp);
void __stdcall ReleaseContextResources(PRWCONTEXT ctx);
ULONG __stdcall ReadRingPacket(PMDL SourceMdl, PRWCONTEXT ctx);