#include "ringtail.h"

int __stdcall ConsumeRingTailPacket(PRING_TAIL pRingTail, PUCHAR* ppDestBuf, ULONG consumePackets)
{
	ULONG availablePackets = GetAvailableRingTailPackets(pRingTail, 21);
	if (availablePackets < consumePackets)
	{
		DRIVER_LOG_ERROR(("Wanted to consume %d packets, but only %d are available", consumePackets, availablePackets));
		*ppDestBuf = nullptr;
		return -1;
	}

	*ppDestBuf = &pRingTail->m_pRingData[(size_t)(pRingTail->m_iNumFinished * pRingTail->m_iPacketSize)];
	pRingTail->m_iNumFinished = (pRingTail->m_iNumFinished + consumePackets) % pRingTail->m_iNumPackets;
	return 1;
}

int __stdcall ConsumeRingTailWritingPacket(PRING_TAIL pRingTail, PUCHAR pDestBuf, ULONG consumePackets)
{
	ULONG availablePackets = GetAvailableRingTailPackets(pRingTail, 32);
	if (availablePackets < consumePackets)
	{
		DRIVER_LOG_ERROR(("Wanted to consume %d packets, but only %d are available", consumePackets, availablePackets));
		return -1;
	}

	NT_VERIFY(pDestBuf == &pRingTail->m_pRingData[(size_t)(pRingTail->m_iWriting * pRingTail->m_iPacketSize)]);
	pRingTail->m_iWriting = (pRingTail->m_iWriting + consumePackets) % pRingTail->m_iNumPackets;
	return 1;
}

ULONG __stdcall GetAvailableRingTailPackets(PRING_TAIL pRingTail, BYTE type)
{
	NT_ASSERT(type == 21 || type == 32 || type == 43);

	switch (type)
	{
	case 21:
		return (pRingTail->m_iNumPackets + pRingTail->m_iReading - pRingTail->m_iNumFinished - 1) % pRingTail->m_iNumPackets;
	case 32:
		return (pRingTail->m_iNumPackets + pRingTail->m_iNumFinished - pRingTail->m_iWriting) % pRingTail->m_iNumPackets;
	case 43:
		return (pRingTail->m_iNumPackets + pRingTail->m_iWriting - pRingTail->m_iToRead) % pRingTail->m_iNumPackets;
	default:
		// The assertion at the beginning of the function will catch misuse in debug builds.
		// We know for a fact that this function will *only* be called with any one of the
		// expected values of `type`, since it's private to the driver. If this assumption is
		// violated, we might as well allow things to go off the rails, since there are obviously
		// bigger problems.
		__assume(false);
	}
}