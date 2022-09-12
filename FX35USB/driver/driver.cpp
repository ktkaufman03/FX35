
#include "driver.h"

extern "C" {
#include <usbioctl.h>
#include <usbdlib.h>
}

#if DBG
#define MSGUSBSTRING(d,s,i) { \
		UNICODE_STRING sd; \
		if (i && NT_SUCCESS(GetStringDescriptor(d,i,&sd))) { \
			DRIVER_LOG_INFO((s, sd.Buffer)); \
			RtlFreeUnicodeString(&sd); \
		}}
#else
#define MSGUSBSTRING(d,i,s)
#endif


///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

VOID StopDevice(IN PDEVICE_OBJECT fdo, BOOLEAN oktouch /* = FALSE */)
{							// StopDevice
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	// If it's okay to touch our hardware (i.e., we're processing an IRP_MN_STOP_DEVICE),
	// deconfigure the device.

	if (oktouch)
	{						// deconfigure device
		URB urb;
		UsbBuildSelectConfigurationRequest(&urb, sizeof(_URB_SELECT_CONFIGURATION), NULL);
		NTSTATUS status = SendAwaitUrb(fdo, &urb);
		if (!NT_SUCCESS(status))
			DRIVER_LOG_ERROR(("Error %X trying to deconfigure device", status));
	}						// deconfigure device

	if (pdx->pcd)
		ExFreePool(pdx->pcd);
	pdx->pcd = NULL;
}							// StopDevice

///////////////////////////////////////////////////////////////////////////////
// AbortPipe is called as part of an attempt to recover after an I/O error to
// abort pending requests for a given pipe.

#pragma PAGEDCODE

VOID AbortPipe(PDEVICE_OBJECT fdo, USBD_PIPE_HANDLE hpipe)
{							// AbortPipe
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	(void)pdx;
	URB urb;

	urb.UrbHeader.Length = (USHORT)sizeof(_URB_PIPE_REQUEST);
	urb.UrbHeader.Function = URB_FUNCTION_ABORT_PIPE;
	urb.UrbPipeRequest.PipeHandle = hpipe;

	NTSTATUS status = SendAwaitUrb(fdo, &urb);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Error %X in AbortPipe", status));
	}
}							// AbortPipe

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

NTSTATUS GetStringDescriptor(PDEVICE_OBJECT fdo, UCHAR istring, PUNICODE_STRING s)
{							// GetStringDescriptor
	NTSTATUS status;
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	URB urb;

	UCHAR data[256];			// maximum-length buffer

	// If this is the first time here, read string descriptor zero and arbitrarily select
	// the first language identifer as the one to use in subsequent get-descriptor calls.

	if (!pdx->langid)
	{						// determine default language id
		UsbBuildGetDescriptorRequest(&urb, sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST), USB_STRING_DESCRIPTOR_TYPE,
			0, 0, data, NULL, sizeof(data), NULL);
		status = SendAwaitUrb(fdo, &urb);
		if (!NT_SUCCESS(status))
			return status;
		pdx->langid = *(LANGID*)(data + 2);
	}						// determine default language id

// Fetch the designated string descriptor.

	UsbBuildGetDescriptorRequest(&urb, sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST), USB_STRING_DESCRIPTOR_TYPE,
		istring, pdx->langid, data, NULL, sizeof(data), NULL);
	status = SendAwaitUrb(fdo, &urb);
	if (!NT_SUCCESS(status))
		return status;

	ULONG nchars = (data[0] - sizeof(WCHAR)) / sizeof(WCHAR);
	if (nchars > 127)
		nchars = 127;
	PWSTR p = (PWSTR)ALLOC_PAGED(((SIZE_T)nchars + 1) * sizeof(WCHAR));
	if (!p)
		return STATUS_INSUFFICIENT_RESOURCES;

	memcpy(p, data + 2, nchars * sizeof(WCHAR));
	p[nchars] = 0;

	s->Length = (USHORT)(sizeof(WCHAR) * nchars);
	s->MaximumLength = (USHORT)((sizeof(WCHAR) * nchars) + sizeof(WCHAR));
	s->Buffer = p;

	return STATUS_SUCCESS;
}							// GetStringDescriptor

///////////////////////////////////////////////////////////////////////////////
// ResetDevice is called during an attempt to recover after an error in order to
// reset the pipe that had the error

#pragma PAGEDCODE

NTSTATUS ResetPipe(PDEVICE_OBJECT fdo, USBD_PIPE_HANDLE hpipe)
{							// ResetPipe
	PAGED_CODE();
	URB urb;

	urb.UrbHeader.Length = (USHORT)sizeof(_URB_PIPE_REQUEST);
	urb.UrbHeader.Function = URB_FUNCTION_RESET_PIPE;
	urb.UrbPipeRequest.PipeHandle = hpipe;

	NTSTATUS status = SendAwaitUrb(fdo, &urb);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Error %X in ResetPipe", status));
	}
	return status;
}							// ResetPipe

#pragma PAGEDCODE

NTSTATUS ResetDevice(PDEVICE_OBJECT fdo)
{							// ResetDevice
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	IO_STATUS_BLOCK iostatus;

	PIRP Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_RESET_PORT,
		pdx->LowerDeviceObject, NULL, 0, NULL, 0, TRUE, &event, &iostatus);
	if (!Irp)
		return STATUS_INSUFFICIENT_RESOURCES;

	NTSTATUS status = IoCallDriver(pdx->LowerDeviceObject, Irp);
	if (status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = iostatus.Status;
	}

	if (!NT_SUCCESS(status))
		DRIVER_LOG_ERROR(("Error %X trying to reset device", status));
	return status;
}							// ResetDevice

#pragma PAGEDCODE

NTSTATUS SendAwaitUrb(PDEVICE_OBJECT fdo, PURB urb)
{							// SendAwaitUrb
	PAGED_CODE();
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	KEVENT event;
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IO_STATUS_BLOCK iostatus;
	PIRP Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
		pdx->LowerDeviceObject, NULL, 0, NULL, 0, TRUE, &event, &iostatus);

	if (!Irp)
	{
		DRIVER_LOG_ERROR(("Unable to allocate IRP for sending URB"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(Irp);
	stack->Parameters.Others.Argument1 = (PVOID)urb;
	NTSTATUS status = IoCallDriver(pdx->LowerDeviceObject, Irp);
	if (status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = iostatus.Status;
	}
	return status;
}							// SendAwaitUrb

#pragma PAGEDCODE

VOID RemoveDevice(IN PDEVICE_OBJECT fdo)
{							// RemoveDevice
	PAGED_CODE();
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	RtlFreeUnicodeString(&pdx->devname);

	if (pdx->LowerDeviceObject)
		IoDetachDevice(pdx->LowerDeviceObject);

	IoDeleteDevice(fdo);
}							// RemoveDevice

///////////////////////////////////////////////////////////////////////////////

#pragma PAGEDCODE

NTSTATUS StartDevice(PDEVICE_OBJECT fdo, PCM_PARTIAL_RESOURCE_LIST raw, PCM_PARTIAL_RESOURCE_LIST translated)
{
	PAGED_CODE();

	(void)raw;
	(void)translated;

	NTSTATUS status;
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	URB urb;					// URB for use in this subroutine

	// Read our device descriptor. The only real purpose to this would be to find out how many
	// configurations there are so we can read their descriptors.

	UsbBuildGetDescriptorRequest(&urb, sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST), USB_DEVICE_DESCRIPTOR_TYPE,
		0, 0, &pdx->dd, NULL, sizeof(pdx->dd), NULL);

	status = SendAwaitUrb(fdo, &urb);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Error %X trying to read device descriptor", status));
		return status;
	}

	//MSGUSBSTRING(fdo, "Configuring device from %ws", pdx->dd.iManufacturer);
	//MSGUSBSTRING(fdo, "Product is %ws", pdx->dd.iProduct);
	//MSGUSBSTRING(fdo, "Serial number is %ws", pdx->dd.iSerialNumber);

	// Read the descriptor of the first configuration. This requires two steps. The first step
	// reads the fixed-size configuration descriptor alone. The second step reads the
	// configuration descriptor plus all imbedded interface and endpoint descriptors.

	USB_CONFIGURATION_DESCRIPTOR tcd;
	UsbBuildGetDescriptorRequest(&urb, sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST), USB_CONFIGURATION_DESCRIPTOR_TYPE,
		0, 0, &tcd, NULL, sizeof(tcd), NULL);
	status = SendAwaitUrb(fdo, &urb);
	if (!NT_SUCCESS(status))
	{
		DRIVER_LOG_ERROR(("Error %X trying to read configuration descriptor 1", status));
		return status;
	}

	ULONG size = tcd.wTotalLength;
	PUSB_CONFIGURATION_DESCRIPTOR pcd = (PUSB_CONFIGURATION_DESCRIPTOR)ALLOC_NON_PAGED(size);
	if (!pcd)
	{
		DRIVER_LOG_ERROR(("Unable to allocate %X bytes for configuration descriptor", size));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	__try
	{
		UsbBuildGetDescriptorRequest(&urb, sizeof(_URB_CONTROL_DESCRIPTOR_REQUEST), USB_CONFIGURATION_DESCRIPTOR_TYPE,
			0, 0, pcd, NULL, size, NULL);
		status = SendAwaitUrb(fdo, &urb);
		if (!NT_SUCCESS(status))
		{
			DRIVER_LOG_ERROR(("Error %X trying to read configuration descriptor 1", status));
			return status;
		}
		//MSGUSBSTRING(fdo, "Selecting configuration named %ws", pcd->iConfiguration);
		PUSB_INTERFACE_DESCRIPTOR pid = USBD_ParseConfigurationDescriptorEx(
			pcd,
			pcd,
			-1,
			-1,
			-1,
			-1,
			-1);
		ASSERT(pid);

		//MSGUSBSTRING(fdo, "Selecting interface named %ws", pid->iInterface);

		// Create a URB to use in selecting a configuration.

		USBD_INTERFACE_LIST_ENTRY interfaces[2] = {
			{pid, NULL},
			{NULL, NULL},		// fence to terminate the array
		};

		PURB selurb = USBD_CreateConfigurationRequestEx(pcd, interfaces);
		if (!selurb)
		{
			DRIVER_LOG_ERROR(("Unable to create configuration request"));
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		__try
		{
			PUSB_ENDPOINT_DESCRIPTOR ped = (PUSB_ENDPOINT_DESCRIPTOR)pid;
			ped = (PUSB_ENDPOINT_DESCRIPTOR)USBD_ParseDescriptors(pcd, tcd.wTotalLength, ped, USB_ENDPOINT_DESCRIPTOR_TYPE);
			if (pid->bNumEndpoints == 2)
			{
				if (!ped || ped->bEndpointAddress != 1 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 64)
				{
					DRIVER_LOG_ERROR(("Endpoint 0 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x81 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 64)
				{
					DRIVER_LOG_ERROR(("Endpoint 1 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
			}
			else if (pid->bNumEndpoints == 3)
			{
				if (!ped || ped->bEndpointAddress != 1 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 0 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x81 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 1 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x86 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 2 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
			}
			else if (pid->bNumEndpoints == 4)
			{
				if (!ped || ped->bEndpointAddress != 1 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 0 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x81 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 1 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x86 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 2 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
				ped++;
				if (!ped || ped->bEndpointAddress != 0x88 || ped->bmAttributes != 2 || ped->wMaxPacketSize != 512)
				{
					DRIVER_LOG_ERROR(("Endpoint 3 has wrong attributes"));
					return STATUS_DEVICE_CONFIGURATION_ERROR;
				}
			}
			else
			{
				DRIVER_LOG_ERROR(("I can't deal with this set of endpoints. Either too many or too few..."));
				return STATUS_DEVICE_CONFIGURATION_ERROR;
			}
			PUSBD_INTERFACE_INFORMATION pii = interfaces[0].Interface;
			ASSERT(pii);
			if (pii->NumberOfPipes != pid->bNumEndpoints)
			{
				DRIVER_LOG_ERROR(("Uh oh! We have %d endpoints but %d pipes.", pid->bNumEndpoints, pii->NumberOfPipes));
				return STATUS_DEVICE_CONFIGURATION_ERROR;
			}
			if (pid->bNumEndpoints >= 3)
			{
				if (pid->bNumEndpoints == 4)
				{
					pii->Pipes[3].MaximumTransferSize = 4096;
				}
				pii->Pipes[2].MaximumTransferSize = 4096;
			}
			pii->Pipes[0].MaximumTransferSize = 4096;
			pii->Pipes[1].MaximumTransferSize = 4096;
			pdx->maxtransfer = 4096;
			status = SendAwaitUrb(fdo, selurb);
			if (!NT_SUCCESS(status))
			{
				DRIVER_LOG_ERROR(("Error %X trying to select configuration", status));
				return status;
			}
			pdx->ConfigurationHandle = selurb->UrbSelectConfiguration.ConfigurationHandle;
			pdx->RingWritePipe = nullptr;
			pdx->RingReadPipe = nullptr;
			pdx->DirectWriteHandle = nullptr;
			pdx->DirectReadHandle = nullptr;

			if (pid->bNumEndpoints == 3 || pid->bNumEndpoints == 4)
			{
				pdx->RingReadPipe = pii->Pipes[(pid->bNumEndpoints == 4 ? 3 : 2)].PipeHandle;
				pdx->DirectWriteHandle = pii->Pipes[0].PipeHandle;
				pdx->DirectReadHandle = pii->Pipes[1].PipeHandle;
			}
			pdx->pcd = pcd;
			pcd = nullptr;
		}
		__finally
		{
			ExFreePool(selurb);
		}
	}
	__finally
	{
		if (pcd)
			ExFreePool(pcd);
	}

	return STATUS_SUCCESS;
}							// StartDevice
