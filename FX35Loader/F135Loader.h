#ifndef F135_LOADER_H
#define F135_LOADER_H
#include <stddef.h>
#include <wdm.h>
#include <usb.h>

#ifndef _BYTE_DEFINED
#define _BYTE_DEFINED
typedef unsigned char BYTE;
#endif // !_BYTE_DEFINED

#ifndef _WORD_DEFINED
#define _WORD_DEFINED
typedef unsigned short WORD;
#endif // !_WORD_DEFINED

#pragma pack(push, 1)
typedef struct _DEVICE_PERSONALITY
{
	BYTE id;
	WORD wVendorId;
	WORD wProductId;
	WORD wRevision;
	BYTE unk_7;
} DEVICE_PERSONALITY, *PDEVICE_PERSONALITY;
#pragma pack(pop)

static_assert(sizeof(DEVICE_PERSONALITY) == 8, "DEVICE_PERSONALITY isn't the right size");

// based on EZ-USB

typedef struct _DEVICE_EXTENSION
{
	PDEVICE_OBJECT PhysicalDeviceObject;
	PDEVICE_OBJECT StackDeviceObject;
	LONG usage;
	KEVENT evRemove;
	BOOLEAN removing;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#define MAX_INTEL_HEX_RECORD_LENGTH 16

typedef struct _INTEL_HEX_RECORD
{
	BYTE  Length;
	WORD  Address;
	BYTE  Type;
	BYTE  Data[MAX_INTEL_HEX_RECORD_LENGTH];
} INTEL_HEX_RECORD, * PINTEL_HEX_RECORD;

//
// Vendor specific request code for Anchor Upload/Download
//
// This one is implemented in the core
//
#define ANCHOR_LOAD_INTERNAL  0xA0

//
// This command is not implemented in the core.  Requires firmware
//
#define ANCHOR_LOAD_EXTERNAL  0xA3

//
// This is the highest internal RAM address for the AN2131Q
//
#define MAX_INTERNAL_ADDRESS  0x1B3F

#define INTERNAL_RAM(address) (((address) <= MAX_INTERNAL_ADDRESS) ? 1 : 0)
//
// EZ-USB Control and Status Register.  Bit 0 controls 8051 reset
//
#define CPUCS_REG_EZUSB    0x7F92
#define CPUCS_REG_FX2      0xE600

#define ALLOC_PAGED_TAG(size, tag) ExAllocatePool2(POOL_FLAG_PAGED, (size), (tag))
#define ALLOC_PAGED(size) ALLOC_PAGED_TAG(size, ' mdW')
#define ALLOC_NON_PAGED_TAG(size, tag) ExAllocatePool2(POOL_FLAG_NON_PAGED, (size), (tag))
#define ALLOC_NON_PAGED(size) ALLOC_NON_PAGED_TAG(size, ' mdW')
 
#define PAKON_GET_PERSONALITY 0xA9
#define MAX_FIRMWARE_RECORDS 0x1000

#ifdef _DEBUG

#define Ezusb_KdPrint(_x)
//#define Ezusb_KdPrint(_x_) DbgPrint("Ezusb.SYS: "); \
//                             DbgPrint _x_ ;
#define TRAP() DbgBreakPoint()
#else
#define Ezusb_KdPrint(_x_)
#define TRAP()
#endif
#endif