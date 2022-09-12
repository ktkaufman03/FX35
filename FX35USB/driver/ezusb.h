#pragma once

#include <wdm.h>
#include <windef.h>
#include <usb.h>

#define EZUSB_MAJOR_VERSION		0
#define EZUSB_MINOR_VERSION		0
#define EZUSB_BUILD_VERSION		0

#define Ezusb_IOCTL_INDEX  0x0800

#define IOCTL_Ezusb_VENDOR_REQUEST					CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+5,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_Ezusb_GET_CURRENT_CONFIG				CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+6,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_Ezusb_ANCHOR_DOWNLOAD					CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+7,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_Ezusb_RESET							CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+12,\
	                                                   METHOD_IN_DIRECT,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_GET_CURRENT_FRAME_NUMBER		CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+21,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST			CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+22,\
	                                                   METHOD_IN_DIRECT,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_GET_LAST_ERROR					CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+23,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_ANCHOR_DOWNLOAD					CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+27,\
	                                                   METHOD_IN_DIRECT,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_GET_DRIVER_VERSION				CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+29,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_EZUSB_SET_FEATURE						CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+33,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_PAKON_READ_DIRECT						CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+34,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_PAKON_WRITE_DIRECT					CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+35,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
#define IOCTL_PAKON_SEND_AND_RECEIVE_PACKET         CTL_CODE(FILE_DEVICE_UNKNOWN,  \
	                                                   Ezusb_IOCTL_INDEX+36,\
	                                                   METHOD_BUFFERED,  \
	                                                   FILE_ANY_ACCESS)
static_assert(IOCTL_Ezusb_VENDOR_REQUEST == 0x222014, "IOCTL_Ezusb_VENDOR_REQUEST is wrong!");
static_assert(IOCTL_Ezusb_GET_CURRENT_CONFIG == 0x222018, "IOCTL_Ezusb_GET_CURRENT_CONFIG is wrong!");
static_assert(IOCTL_Ezusb_ANCHOR_DOWNLOAD == 0x22201C, "IOCTL_Ezusb_ANCHOR_DOWNLOAD is wrong!");
static_assert(IOCTL_Ezusb_RESET == 0x222031, "IOCTL_Ezusb_RESET is wrong!");
static_assert(IOCTL_EZUSB_GET_CURRENT_FRAME_NUMBER == 0x222054, "IOCTL_EZUSB_GET_CURRENT_FRAME_NUMBER is wrong!");
static_assert(IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST == 0x222059, "IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST is wrong!");
static_assert(IOCTL_EZUSB_GET_LAST_ERROR == 0x22205C, "IOCTL_EZUSB_GET_LAST_ERROR is wrong!");
static_assert(IOCTL_EZUSB_ANCHOR_DOWNLOAD == 0x22206D, "IOCTL_EZUSB_ANCHOR_DOWNLOAD is wrong!");
static_assert(IOCTL_EZUSB_GET_DRIVER_VERSION == 0x222074, "IOCTL_EZUSB_GET_DRIVER_VERSION is wrong!");
static_assert(IOCTL_EZUSB_SET_FEATURE == 0x222084, "IOCTL_EZUSB_SET_FEATURE is wrong!");
static_assert(IOCTL_PAKON_READ_DIRECT == 0x222088, "IOCTL_PAKON_READ_DIRECT is wrong!");
static_assert(IOCTL_PAKON_WRITE_DIRECT == 0x22208C, "IOCTL_PAKON_WRITE_DIRECT is wrong!");
static_assert(IOCTL_PAKON_SEND_AND_RECEIVE_PACKET == 0x222090, "IOCTL_PAKON_SEND_AND_RECEIVE_PACKET is wrong!");

//
// Vendor specific request code for Anchor Upload/Download
//
// This one is implemented in the core
//
#define ANCHOR_LOAD_INTERNAL  0xA0

typedef struct _SET_FEATURE_CONTROL
{
	USHORT FeatureSelector;
	USHORT Index;
} SET_FEATURE_CONTROL, * PSET_FEATURE_CONTROL;

static_assert(sizeof(SET_FEATURE_CONTROL) == 4, "sizeof(SET_FEATURE_CONTROL) should be 4");

typedef struct _VENDOR_REQUEST_IN
{
	BYTE bRequest;
	WORD wValue;
	WORD wIndex;
	WORD wLength;
	BYTE direction;
	BYTE bData;
} VENDOR_REQUEST_IN, * PVENDOR_REQUEST_IN;

static_assert(sizeof(VENDOR_REQUEST_IN) == 10, "sizeof(VENDOR_REQUEST_IN) should be 10");

typedef struct _VENDOR_OR_CLASS_REQUEST_CONTROL
{
	UCHAR direction;
	UCHAR requestType;
	UCHAR recepient;
	UCHAR requestTypeReservedBits;
	UCHAR request;
	USHORT value;
	USHORT index;
} VENDOR_OR_CLASS_REQUEST_CONTROL, * PVENDOR_OR_CLASS_REQUEST_CONTROL;

static_assert(sizeof(VENDOR_OR_CLASS_REQUEST_CONTROL) == 10, "sizeof(VENDOR_OR_CLASS_REQUEST_CONTROL) should be 10");

typedef struct _EZUSB_DRIVER_VERSION
{
	WORD     MajorVersion;
	WORD     MinorVersion;
	WORD     BuildVersion;
} EZUSB_DRIVER_VERSION, * PEZUSB_DRIVER_VERSION;

static_assert(sizeof(EZUSB_DRIVER_VERSION) == 6, "sizeof(EZUSB_DRIVER_VERSION) should be 6");

typedef struct _ANCHOR_DOWNLOAD_CONTROL
{
	WORD Offset;
} ANCHOR_DOWNLOAD_CONTROL, * PANCHOR_DOWNLOAD_CONTROL;

NTSTATUS __stdcall Ezusb_AnchorDownload(
	PDEVICE_OBJECT fdo,
	WORD offset,
	PUCHAR downloadBuffer,
	ULONG downloadSize,
	ULONG chunkSize);
NTSTATUS __stdcall Ezusb_CallUSBD(PDEVICE_OBJECT fdo, PURB urb);
ULONG __stdcall Ezusb_GetCurrentFrameNumber(PDEVICE_OBJECT fdo);
NTSTATUS __stdcall Ezusb_ProcessIOCTL(PDEVICE_OBJECT fdo, PIRP Irp);
NTSTATUS __stdcall Ezusb_Read_Write_Direct(PDEVICE_OBJECT DeviceObject, PIRP Irp, BOOLEAN Read);
NTSTATUS __stdcall Ezusb_ResetParentPort(PDEVICE_OBJECT pdo);
NTSTATUS __stdcall Ezusb_SetFeature(PDEVICE_OBJECT fdo, PSET_FEATURE_CONTROL feature);
ULONG __stdcall Ezusb_VendorRequest(PDEVICE_OBJECT fdo, PVENDOR_REQUEST_IN pVendorRequest);
NTSTATUS __stdcall Ezusb_VendorRequest2(PDEVICE_OBJECT fdo, PIRP Irp);