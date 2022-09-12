// GenericPower.h -- Declarations for power management interfaces supported by GENERIC.SYS
// Copyright (C) 1999 by Walter Oney
// All rights reserved
#ifndef GENERICPOWER_H
#define GENERICPOWER_H

#ifndef CTL_CODE
	#pragma message("CTL_CODE undefined. Include winioctl.h or wdm.h")
#endif

// IOCTL interface for generic power management

#define IOCTL_GET_POWERINFO CTL_CODE(FILE_DEVICE_UNKNOWN, 0xFF0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_POWERINFO CTL_CODE(FILE_DEVICE_UNKNOWN, 0xFF1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IDLENOW		CTL_CODE(FILE_DEVICE_UNKNOWN, 0xFF2, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _POWERINFO {
	unsigned long ulConservation;	// idle timeout in conservation mode
	unsigned long ulPerformance;	// idle timeout in performance mode
	BYTE bWakeup;			// TRUE if wakeup feature should be armed
	BYTE bCanWake;			// device capable of waking system
	BYTE bCanIdle;			// device can be idled
	BYTE bIdle;			// device is idle right now
	} POWERINFO, *PPOWERINFO;

// Devices that use GENERIC.SYS for power managment register an interface with the following GUID:

// {894A7461-A033-11d2-821E-444553540000}
DEFINE_GUID(GUID_GENERIC_POWER, 0x894a7461, 0xa033, 0x11d2, 0x82, 0x1e, 0x44, 0x45, 0x53, 0x54, 0x0, 0x0);

#endif
