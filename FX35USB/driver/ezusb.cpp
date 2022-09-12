#include "ezusb.h"

#include <usbdlib.h>

#include "driver.h"
#include <usbioctl.h>


NTSTATUS __stdcall Ezusb_ProcessIOCTL(PDEVICE_OBJECT fdo, PIRP Irp)
{
	ULONG_PTR length;
	PDEVICE_EXTENSION pdx;
	PIO_STACK_LOCATION irpStack;

	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation(Irp);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	PVOID ioBuffer = Irp->AssociatedIrp.SystemBuffer;
	ULONG inputBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

	switch (ioControlCode)
	{
	case IOCTL_Ezusb_VENDOR_REQUEST:
		length = Ezusb_VendorRequest(fdo, (PVENDOR_REQUEST_IN)ioBuffer);

		if (length)
		{
			Irp->IoStatus.Information = length;
			Irp->IoStatus.Status = STATUS_SUCCESS;
		}
		else
		{
			Irp->IoStatus.Status = STATUS_SUCCESS;
		}

		break;
	case IOCTL_Ezusb_GET_CURRENT_CONFIG:
		Irp->IoStatus.Status = STATUS_SUCCESS;
		Irp->IoStatus.Information = 0;
		break;
	case IOCTL_Ezusb_ANCHOR_DOWNLOAD:
	{
		PANCHOR_DOWNLOAD_CONTROL downloadControl = (PANCHOR_DOWNLOAD_CONTROL)ioBuffer;
		if (inputBufferLength == 2 && outputBufferLength)
		{
			Irp->IoStatus.Status = Ezusb_AnchorDownload(fdo,
				downloadControl->Offset,
				(PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, HighPagePriority),
				outputBufferLength,
				0x200u);
		}
		else
		{
			DRIVER_LOG_ERROR(("Invalid input (%u) or output (%u) buffer size", inputBufferLength, outputBufferLength));
			Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case IOCTL_Ezusb_RESET:
		Ezusb_ResetParentPort(fdo);
		break;
	case IOCTL_EZUSB_GET_CURRENT_FRAME_NUMBER:
	{
		ULONG frameNumber = 0;

		//
		// make sure the output buffer is valid
		//
		if (outputBufferLength < 4)
		{
			DRIVER_LOG_ERROR(("Insufficient buffer size: %u", outputBufferLength));
			Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
			break;
		}

		frameNumber = Ezusb_GetCurrentFrameNumber(fdo);
		if (frameNumber)
		{
			*((PULONG)ioBuffer) = frameNumber;
			Irp->IoStatus.Information = sizeof(ULONG);
			Irp->IoStatus.Status = STATUS_SUCCESS;
		}
		else
			Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		break;
	}
	case IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST:
		Irp->IoStatus.Status = Ezusb_VendorRequest2(fdo, Irp);
		break;
	case IOCTL_EZUSB_GET_LAST_ERROR:

		//
		// make sure the output buffer is ok, and then copy the most recent
		// URB status from the device extension to it
		//
		if (outputBufferLength >= sizeof(ULONG))
		{
			*((PULONG)ioBuffer) = pdx->LastFailedUrbStatus;
			Irp->IoStatus.Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = sizeof(ULONG);
		}
		else
		{
			DRIVER_LOG_ERROR(("Insufficient buffer size: %u", outputBufferLength));
			Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		}
		break;
	case IOCTL_EZUSB_ANCHOR_DOWNLOAD:
	{
		PANCHOR_DOWNLOAD_CONTROL downloadControl = (PANCHOR_DOWNLOAD_CONTROL)ioBuffer;
		if (inputBufferLength == sizeof(ANCHOR_DOWNLOAD_CONTROL) && outputBufferLength != 0)
		{
			Irp->IoStatus.Status = Ezusb_AnchorDownload(fdo,
				downloadControl->Offset,
				(PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, HighPagePriority),
				outputBufferLength,
				0x40u);
		}
		else
		{
			DRIVER_LOG_ERROR(("Invalid input (%u) or output (%u) buffer size", inputBufferLength, outputBufferLength));
			Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case IOCTL_EZUSB_GET_DRIVER_VERSION:
	{
		PEZUSB_DRIVER_VERSION version = (PEZUSB_DRIVER_VERSION)ioBuffer;

		if (outputBufferLength >= sizeof(EZUSB_DRIVER_VERSION))
		{
			version->MajorVersion = EZUSB_MAJOR_VERSION;
			version->MinorVersion = EZUSB_MINOR_VERSION;
			version->BuildVersion = EZUSB_BUILD_VERSION;

			Irp->IoStatus.Status = STATUS_SUCCESS;
			Irp->IoStatus.Information = sizeof(EZUSB_DRIVER_VERSION);
		}
		else
		{
			DRIVER_LOG_ERROR(("Insufficient buffer size: %u", outputBufferLength));
			Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		}
		break;
	}
	case IOCTL_EZUSB_SET_FEATURE:
		if (inputBufferLength == sizeof(SET_FEATURE_CONTROL))
		{
			Ezusb_SetFeature(fdo, (PSET_FEATURE_CONTROL)ioBuffer);
			Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
		}
		else
		{
			Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		}
		break;
	case IOCTL_PAKON_READ_DIRECT:
		Irp->IoStatus.Status = Ezusb_Read_Write_Direct(fdo, Irp, TRUE);
		break;
	case IOCTL_PAKON_WRITE_DIRECT:
		Irp->IoStatus.Status = Ezusb_Read_Write_Direct(fdo, Irp, FALSE);
		break;
	case IOCTL_PAKON_SEND_AND_RECEIVE_PACKET:
		Irp->IoStatus.Status = Ezusb_Read_Write_Direct(fdo, Irp, FALSE);
		if (!NT_SUCCESS(Irp->IoStatus.Status))
		{
			DRIVER_LOG_ERROR(("Writing packet to scanner failed with status %08X", Irp->IoStatus.Status));
			break;
		}
		Irp->IoStatus.Status = Ezusb_Read_Write_Direct(fdo, Irp, TRUE);
		if (!NT_SUCCESS(Irp->IoStatus.Status))
		{
			DRIVER_LOG_ERROR(("Reading response from scanner failed with status %08X", Irp->IoStatus.Status));
			break;
		}
		break;
	default:
		DRIVER_LOG_ERROR(("Unexpected IOCTL - code: 0x%X; InLen: %u; OutLen: %u", ioControlCode, inputBufferLength, outputBufferLength));
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (!NT_SUCCESS(Irp->IoStatus.Status))
	{
		DRIVER_LOG_ERROR(("Failed to handle IOCTL - code: 0x%X; InLen: %u; OutLen: %u", ioControlCode, inputBufferLength, outputBufferLength));
	}

	return Irp->IoStatus.Status;
}


NTSTATUS __stdcall Ezusb_Read_Write_Direct(PDEVICE_OBJECT DeviceObject, PIRP Irp, BOOLEAN Read)
{
	PDEVICE_EXTENSION pdx;
	PIO_STACK_LOCATION irpStack;
	void* PipeHandle;
	void* TransferBuffer;
	ULONG TransferBufferLength;
	PURB urb;
	NTSTATUS status;
	ULONG TransferFlags;

	pdx = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
	irpStack = Irp->Tail.Overlay.CurrentStackLocation;
	TransferFlags = USBD_SHORT_TRANSFER_OK;

	if (Read)
	{
		PipeHandle = pdx->DirectReadHandle;
		TransferBuffer = Irp->UserBuffer;
		TransferBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
		TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
	}
	else
	{
		PipeHandle = pdx->DirectWriteHandle;
		TransferBuffer = Irp->AssociatedIrp.SystemBuffer;
		TransferBufferLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
	}
	if (TransferBufferLength > 0x200)
	{
		DRIVER_LOG_ERROR(("Transfer buffer is too large (%u bytes)", TransferBufferLength));
		return STATUS_INVALID_PARAMETER;
	}
	if (!PipeHandle)
	{
		DRIVER_LOG_ERROR(("Could not obtain USB pipe handle"));
		return STATUS_INVALID_PARAMETER;
	}

	size_t urbSize = sizeof(_URB_BULK_OR_INTERRUPT_TRANSFER);
	urb = (PURB)ALLOC_NON_PAGED(urbSize);
	if (!urb)
	{
		DRIVER_LOG_ERROR(("URB allocation failed"));
		return STATUS_NO_MEMORY;
	}

	if (Read)
	{
		// This is a weird hack but it makes things work properly with the USB 3.0 driver stack.
		// USBXHCI.SYS was causing a page fault when trying to write to the user-supplied buffer,
		// so we make our own buffer in non-paged memory that just gets copied to the user-supplied
		// buffer. For some reason, the original code worked perfectly fine with the USB 2.0 stack.

		PVOID tmpReadBuffer = ALLOC_NON_PAGED(TransferBufferLength);

		if (!tmpReadBuffer)
		{
			DRIVER_LOG_ERROR(("Temporary buffer allocation failed"));
			ExFreePool(urb);
			return STATUS_NO_MEMORY;
		}

		UsbBuildInterruptOrBulkTransferRequest(urb, (USHORT)urbSize, PipeHandle, tmpReadBuffer, NULL, TransferBufferLength, TransferFlags, NULL);

		status = Ezusb_CallUSBD(DeviceObject, urb);
		if (NT_SUCCESS(status))
		{
			Irp->IoStatus.Information = urb->UrbControlTransfer.TransferBufferLength;
			memcpy_s(TransferBuffer, TransferBufferLength, tmpReadBuffer, urb->UrbControlTransfer.TransferBufferLength);
		}
		else
		{
			DRIVER_LOG_ERROR(("Read failed with status %08X", status));
		}

		ExFreePool(tmpReadBuffer);
	}
	else
	{
		UsbBuildInterruptOrBulkTransferRequest(urb, (USHORT)urbSize, PipeHandle, TransferBuffer, NULL, TransferBufferLength, TransferFlags, NULL);

		status = Ezusb_CallUSBD(DeviceObject, urb);
		if (NT_SUCCESS(status))
		{
			Irp->IoStatus.Information = urb->UrbControlTransfer.TransferBufferLength;
		}
		else
		{
			DRIVER_LOG_ERROR(("Write failed with status %08X", status));
		}
	}

	ExFreePool(urb);
	return status;
}

NTSTATUS
Ezusb_VendorRequest2(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp
)
{
	NTSTATUS                   ntStatus;
	PIO_STACK_LOCATION         irpStack = IoGetCurrentIrpStackLocation(Irp);
	PVENDOR_OR_CLASS_REQUEST_CONTROL  requestControl =
		(PVENDOR_OR_CLASS_REQUEST_CONTROL)Irp->AssociatedIrp.SystemBuffer;
	ULONG                      bufferLength =
		irpStack->Parameters.DeviceIoControl.OutputBufferLength;
	PURB                       urb = NULL;
	ULONG                      urbSize = 0;

	ULONG                      transferFlags = 0;
	USHORT                     urbFunction = 0;

	//
	// verify that the input parameter is correct (or at least that it's
	// the right size
	//
	if (irpStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(VENDOR_OR_CLASS_REQUEST_CONTROL))
	{
		ntStatus = STATUS_INVALID_PARAMETER;
		return ntStatus;
	}

	//
	// allocate and fill in the Usb request (URB)
	//
	urbSize = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);

	urb = (PURB)ALLOC_NON_PAGED(urbSize);

	if (!urb)
	{
		return STATUS_NO_MEMORY;
	}

	RtlZeroMemory(urb, urbSize);

	transferFlags = USBD_SHORT_TRANSFER_OK;

	//
	// get direction info from the input parms
	//
	if (requestControl->direction)
		transferFlags |= USBD_TRANSFER_DIRECTION_IN;

	//
	// the type of request (class or vendor) and the recepient
	// (device, interface, endpoint, other) combine to determine the 
	// URB function.  The following ugly code transforms fields in
	// the input param into an URB function
	//
	switch ((requestControl->requestType << 2) | requestControl->recepient)
	{
	case 0x04:
		urbFunction = URB_FUNCTION_CLASS_DEVICE;
		break;
	case 0x05:
		urbFunction = URB_FUNCTION_CLASS_INTERFACE;
		break;
	case 0x06:
		urbFunction = URB_FUNCTION_CLASS_ENDPOINT;
		break;
	case 0x07:
		urbFunction = URB_FUNCTION_CLASS_OTHER;
		break;
	case 0x08:
		urbFunction = URB_FUNCTION_VENDOR_DEVICE;
		break;
	case 0x09:
		urbFunction = URB_FUNCTION_VENDOR_INTERFACE;
		break;
	case 0x0A:
		urbFunction = URB_FUNCTION_VENDOR_ENDPOINT;
		break;
	case 0x0B:
		urbFunction = URB_FUNCTION_VENDOR_OTHER;
		break;
	default:
		ExFreePool(urb);
		return STATUS_INVALID_PARAMETER;
	}

	urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
	urb->UrbHeader.Function = urbFunction;

	urb->UrbControlVendorClassRequest.TransferFlags = transferFlags;
	urb->UrbControlVendorClassRequest.TransferBufferLength = bufferLength;
	urb->UrbControlVendorClassRequest.TransferBufferMDL = Irp->MdlAddress;
	urb->UrbControlVendorClassRequest.Request = requestControl->request;
	urb->UrbControlVendorClassRequest.Value = requestControl->value;
	urb->UrbControlVendorClassRequest.Index = requestControl->index;

	//
	// Call the USB Stack.
	//
	ntStatus = Ezusb_CallUSBD(fdo, urb);

	//
	// If the transfer was successful, report the length of the transfer to the
	// caller by setting IoStatus.Information
	//
	if (NT_SUCCESS(ntStatus))
	{
		Irp->IoStatus.Information = urb->UrbControlVendorClassRequest.TransferBufferLength;
	}

	ExFreePool(urb);

	return ntStatus;
}

NTSTATUS
Ezusb_ResetParentPort(
	IN IN PDEVICE_OBJECT fdo
)
/*++

Routine Description:

	Reset the our parent port

Arguments:

Return Value:

	STATUS_SUCCESS if successful,
	STATUS_UNSUCCESSFUL otherwise

--*/
{
	NTSTATUS ntStatus;
	PIRP irp;
	KEVENT event;
	IO_STATUS_BLOCK ioStatus;
	PIO_STACK_LOCATION nextStack;
	PDEVICE_EXTENSION pdx;

	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	//
	// issue a synchronous request
	//

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(
		IOCTL_INTERNAL_USB_RESET_PORT,
		pdx->LowerDeviceObject,
		//                pdx->TopOfStackDeviceObject,
		NULL,
		0,
		NULL,
		0,
		TRUE, /* INTERNAL */
		&event,
		&ioStatus);

	//
	// Call the class driver to perform the operation.  If the returned status
	// is PENDING, wait for the request to complete.
	//

	nextStack = IoGetNextIrpStackLocation(irp);
	ASSERT(nextStack != NULL);

	ntStatus = IoCallDriver(pdx->LowerDeviceObject, irp);

	if (ntStatus == STATUS_PENDING) {

		KeWaitForSingleObject(
			&event,
			Suspended,
			KernelMode,
			FALSE,
			NULL);
	}
	else {
		ioStatus.Status = ntStatus;
	}

	//
	// USBD maps the error code for us
	//
	ntStatus = ioStatus.Status;

	return ntStatus;
}

NTSTATUS Ezusb_SetFeature(
	IN PDEVICE_OBJECT fdo,
	IN PSET_FEATURE_CONTROL setFeatureControl
)
/*
   Routine Description:
   This routine performs a Set Feature control transfer
   Arguments:
   fdo - our device object
   setFeatureControl - a data structure that contains the arguments for the
   set featire command
   Return Value:
   NTSTATUS
*/
{
	NTSTATUS            ntStatus = STATUS_SUCCESS;
	PURB                urb = NULL;

	urb = (PURB) ALLOC_NON_PAGED(sizeof(struct _URB_CONTROL_FEATURE_REQUEST));

	if (urb)
	{
		urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_FEATURE_REQUEST);
		urb->UrbHeader.Function = URB_FUNCTION_SET_FEATURE_TO_DEVICE;

		urb->UrbControlFeatureRequest.FeatureSelector = setFeatureControl->FeatureSelector;
		urb->UrbControlFeatureRequest.Index = setFeatureControl->Index;

		ntStatus = Ezusb_CallUSBD(fdo, urb);

		ExFreePool(urb);
	}
	else
	{
		ntStatus = STATUS_NO_MEMORY;
	}
	
	return ntStatus;
}

ULONG
Ezusb_VendorRequest(
	IN PDEVICE_OBJECT fdo,
	IN PVENDOR_REQUEST_IN pVendorRequest
)
{
	NTSTATUS            ntStatus = STATUS_SUCCESS;
	PURB                urb = NULL;
	ULONG               length = 0;
	PUCHAR buffer = NULL;
	
	urb = (PURB) ALLOC_NON_PAGED(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

	if (urb)
	{
		//
		// fill in the URB
		//
		urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
		urb->UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;

		urb->UrbControlVendorClassRequest.TransferBufferLength = pVendorRequest->wLength;
		urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
		urb->UrbControlVendorClassRequest.Request = pVendorRequest->bRequest;
		urb->UrbControlVendorClassRequest.Value = pVendorRequest->wValue;
		urb->UrbControlVendorClassRequest.Index = pVendorRequest->wIndex;


		//
		// very kludgey.  The idea is: if its an IN then a buffer has been passed
		// in from user mode.  So, use the pointer to the system buffer as the transfer
		// buffer.  If the transfer is an out, then we need to allocate a transfer 
		// buffer.  If the length of the transfer is 1, then put pVendorRequest->bData
		// in the buffer.  Otherwise, fill the buffer with an incrementing byte pattern.
		// yuch
		//
		if (pVendorRequest->direction)
		{
			urb->UrbControlVendorClassRequest.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
			urb->UrbControlVendorClassRequest.TransferBuffer = pVendorRequest;

		}
		else
		{
			urb->UrbControlVendorClassRequest.TransferFlags = 0;
			buffer = (PUCHAR) ALLOC_NON_PAGED(pVendorRequest->wLength);
			NT_VERIFY(buffer);
			urb->UrbControlVendorClassRequest.TransferBuffer = buffer;

			if (pVendorRequest->wLength == 1)
			{
				buffer[0] = pVendorRequest->bData;
			}
			else
			{
				int i;
				PUCHAR ptr = buffer;

				for (i = 0; i < pVendorRequest->wLength; i++)
				{
					*ptr = (UCHAR)i;
					ptr++;
				}
			}
		}

		ntStatus = Ezusb_CallUSBD(fdo, urb);

		//
		// only return a length if this was an IN transaction
		//
		if (pVendorRequest->direction)
		{
			length = urb->UrbControlVendorClassRequest.TransferBufferLength;
		}
		else
		{
			length = 0;
		}

		ExFreePool(urb);
		if (buffer)
			ExFreePool(buffer);
	}
	return length;
}

NTSTATUS __stdcall Ezusb_AnchorDownload(
	PDEVICE_OBJECT fdo,
	WORD offset,
	PUCHAR downloadBuffer,
	ULONG downloadSize,
	ULONG chunkSize)
{
	NTSTATUS ntStatus;
	PURB urb;
	int i;
	int chunkCount;
	PUCHAR ptr = downloadBuffer;

	ntStatus = STATUS_SUCCESS;
	urb = (PURB)ALLOC_NON_PAGED(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

	if (urb)
	{
		chunkCount = ((downloadSize + chunkSize - 1) / chunkSize);
		//
		// The download will be split into CHUNK_SIZE pieces and
		// downloaded with multiple setup transfers.  For the Rev B parts
		// CHUNK_SIZE should not exceed 64 bytes, as larger transfers can
		// result in data corruption when other USB devices are present.
		//
		for (i = 0; i < chunkCount; i++)
		{
			RtlZeroMemory(urb, sizeof(struct  _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

			urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
			urb->UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;

			urb->UrbControlVendorClassRequest.TransferBufferLength =
				((i == (chunkCount - 1)) && (downloadSize % chunkSize)) ?
				(downloadSize % chunkSize) :
				chunkSize;

			urb->UrbControlVendorClassRequest.TransferBuffer = ptr;
			urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
			urb->UrbControlVendorClassRequest.Request = ANCHOR_LOAD_INTERNAL;
			urb->UrbControlVendorClassRequest.Value = (USHORT)((i * chunkSize) + offset);
			urb->UrbControlVendorClassRequest.Index = 0;

			ntStatus = Ezusb_CallUSBD(fdo, urb);

			if (!NT_SUCCESS(ntStatus))
				break;

			ptr += chunkSize;
		}
	}
	else
	{
		ntStatus = STATUS_NO_MEMORY;
	}

	if (urb)
		ExFreePool(urb);

	return ntStatus;
}

NTSTATUS __stdcall Ezusb_CallUSBD(PDEVICE_OBJECT fdo, PURB urb)
{
	PDEVICE_EXTENSION pdx;
	PIRP irp;
	NTSTATUS ntStatus;
	KEVENT event;
	LARGE_INTEGER timeout;
	IO_STATUS_BLOCK ioStatus;
	PIO_STACK_LOCATION nextStack;
	USBD_PIPE_HANDLE pipeHandle;

	pipeHandle = urb->UrbPipeRequest.PipeHandle;
	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	irp = IoBuildDeviceIoControlRequest(
		IOCTL_INTERNAL_USB_SUBMIT_URB,
		pdx->LowerDeviceObject,
		NULL,
		0,
		NULL,
		0,
		TRUE, /* INTERNAL */
		&event,
		&ioStatus);
	if (!irp)
		return STATUS_UNSUCCESSFUL;

	// Prepare for calling the USB driver stack
	nextStack = IoGetNextIrpStackLocation(irp);
	ASSERT(nextStack != NULL);

	// Set up the URB ptr to pass to the USB driver stack
	nextStack->Parameters.Others.Argument1 = urb;

	ntStatus = IoCallDriver(pdx->LowerDeviceObject, irp);
	if (ntStatus == STATUS_PENDING)
	{
		timeout.HighPart = -1;
		timeout.LowPart = 3974967296;

		ntStatus = KeWaitForSingleObject(&event, Suspended, 0, 0, &timeout);
		if (ntStatus)
		{
			DRIVER_LOG_ERROR(("Timed out waiting for USBD to complete pending request"));
			ioStatus.Status = ntStatus | STATUS_UNSUCCESSFUL;
			if (pipeHandle)
				AbortPipe(fdo, pipeHandle);
		}
	}
	else
	{
		ioStatus.Status = ntStatus;
	}

	if (!USBD_SUCCESS(urb->UrbHeader.Status))
	{
		DRIVER_LOG_ERROR(("URB failed with status %08X", urb->UrbHeader.Status));
		pdx->LastFailedUrbStatus = urb->UrbHeader.Status;
		if (NT_SUCCESS(ioStatus.Status))
		{
			DRIVER_LOG_ERROR(("However, IOSB reports succcess with status %08X", ioStatus.Status));
			return STATUS_UNSUCCESSFUL;
		}
	}
	return ioStatus.Status;
}

ULONG
Ezusb_GetCurrentFrameNumber(
	IN PDEVICE_OBJECT fdo
)
{
	PURB     urb = NULL;
	PDEVICE_EXTENSION   pdx = NULL;
	NTSTATUS            ntStatus = STATUS_SUCCESS;
	ULONG               frameNumber = 0;

	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	urb = (PURB)ALLOC_NON_PAGED(sizeof(struct _URB_GET_CURRENT_FRAME_NUMBER));

	if (urb == NULL)
		return 0;

	RtlZeroMemory(urb, sizeof(struct _URB_GET_CURRENT_FRAME_NUMBER));

	urb->UrbHeader.Length = sizeof(struct _URB_GET_CURRENT_FRAME_NUMBER);
	urb->UrbHeader.Function = URB_FUNCTION_GET_CURRENT_FRAME_NUMBER;

	ntStatus = Ezusb_CallUSBD(fdo, urb);

	if (NT_SUCCESS(ntStatus))
	{
		frameNumber = urb->UrbGetCurrentFrameNumber.FrameNumber;
	}

	ExFreePool(urb);

	return frameNumber;
}
