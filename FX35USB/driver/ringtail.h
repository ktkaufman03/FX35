#pragma once

#include <wdm.h>
#include <usb.h>
#include <cstddef>
#include "../../generic/driver.h"

typedef struct __declspec(align(4)) _RING_TAIL
{
	ULONG m_iHeaderSize; // @0x0
	ULONG m_iTotalSize; // @0x4
	int UNUSED_mdl; // @0x8
	ULONG m_iNumPackets; // @0xC
	ULONG m_iNumSimultaneousPackets; // @0x10
	ULONG m_iReading; // @0x14
	ULONG m_iToRead; // @0x18
	ULONG m_iWriting; // @0x1C
	ULONG m_iNumFinished; // @0x20
	ULONG m_iPacketSize; // @0x24
	ULONG m_iMinimumPacketsForReady; // @0x28
	int HANDLE_EventScanPacketReady; // @0x2C
	bool m_bStopTransfer; // @0x30
	bool m_bTransferInProgress; // @0x31
	bool m_bOverFlow; // @0x32
	UCHAR* POINTER_32 m_pRingData; // @0x34;
} RING_TAIL, * PRING_TAIL;

static_assert(offsetof(RING_TAIL, m_pRingData) == 0x34, "_RING_TAIL::m_pRingData should be at offset 0x34");
static_assert(sizeof(RING_TAIL) == 0x38, "_RING_TAIL size should be 0x38");

typedef struct _RING_PACKET
{
	PIRP Irp;
	PURB Urb;
	ULONG Length;
	PMDL Mdl;
	PVOID Data;
	bool cancel;
} RING_PACKET, * PRING_PACKET;

ULONG __stdcall GetAvailableRingTailPackets(PRING_TAIL pRingTail, BYTE type);
int __stdcall ConsumeRingTailPacket(PRING_TAIL pRingTail, PUCHAR* ppDestBuf, ULONG consumePackets);
int __stdcall ConsumeRingTailWritingPacket(PRING_TAIL pRingTail, PUCHAR pDestBuf, ULONG consumePackets);