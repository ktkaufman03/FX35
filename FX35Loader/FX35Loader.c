#include "F135Loader.h"

#include <limits.h>
#include <stdio.h>
#include <usbioctl.h>

extern INTEL_HEX_RECORD loader[];

UNICODE_STRING DriverRegistryPath;

NTSTATUS
CompleteRequest(
	IN PIRP Irp,
	IN NTSTATUS status,
	IN ULONG_PTR info
)
/*++
Routine Description:
   Mark I/O request complete

Arguments:
   Irp - I/O request in question
   status - Standard status code
   info Additional information related to status code

Return Value:
   STATUS_SUCCESS if successful,
   STATUS_UNSUCCESSFUL otherwise
--*/
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

ULONG __stdcall ReadLine(PCHAR input, PCHAR output, int inputLength)
{
	int writtenBytes;
	PCHAR readCursor;
	CHAR readChar;

	writtenBytes = 0;
	if (inputLength <= 0)
		return ULONG_MAX;
	for (readCursor = input; ; ++readCursor)
	{
		readChar = *readCursor;
		if (readChar == 0 || readChar == '\n' || readChar == '\r')
			break;
		output[writtenBytes++] = readChar;
		if (writtenBytes >= inputLength)
			return ULONG_MAX;
	}
	output[writtenBytes] = 0;
	return writtenBytes;
}

NTSTATUS
Ezusb_DefaultPnpHandler(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp
)
{
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(pdx->StackDeviceObject, Irp);
}

NTSTATUS
Ezusb_CallUSBD(
	IN PDEVICE_OBJECT fdo,
	IN PURB Urb
)
/*++

Routine Description:
   Passes a Usb Request Block (URB) to the USB class driver (USBD)

Arguments:
   fdo - pointer to the device object for this instance of an Ezusb Device
   Urb          - pointer to Urb request block

Return Value:
   STATUS_SUCCESS if successful,
   STATUS_UNSUCCESSFUL otherwise

--*/
{
	NTSTATUS ntStatus, status = STATUS_SUCCESS;
	PDEVICE_EXTENSION pdx;
	PIRP irp;
	KEVENT event;
	IO_STATUS_BLOCK ioStatus;
	PIO_STACK_LOCATION nextStack;

	Ezusb_KdPrint(("enter Ezusb_CallUSBD\n"));

	pdx = fdo->DeviceExtension;

	// issue a synchronous request (see notes above)
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(
		IOCTL_INTERNAL_USB_SUBMIT_URB,
		pdx->StackDeviceObject,
		NULL,
		0,
		NULL,
		0,
		TRUE, /* INTERNAL */
		&event,
		&ioStatus);

	// Prepare for calling the USB driver stack
	nextStack = IoGetNextIrpStackLocation(irp);
	ASSERT(nextStack != NULL);

	// Set up the URB ptr to pass to the USB driver stack
	nextStack->Parameters.Others.Argument1 = Urb;

	Ezusb_KdPrint(("Calling USB Driver Stack\n"));

	//
	// Call the USB class driver to perform the operation.  If the returned status
	// is PENDING, wait for the request to complete.
	//
	ntStatus = IoCallDriver(pdx->StackDeviceObject,
		irp);

	Ezusb_KdPrint(("return from IoCallDriver USBD %x\n", ntStatus));

	if (ntStatus == STATUS_PENDING)
	{
		Ezusb_KdPrint(("Wait for single object\n"));

		status = KeWaitForSingleObject(
			&event,
			Suspended,
			KernelMode,
			FALSE,
			NULL);

		Ezusb_KdPrint(("Wait for single object, returned %x\n", status));
	}
	else
	{
		ioStatus.Status = ntStatus;
	}

	Ezusb_KdPrint(("URB status = %x status = %x irp status %x\n",
		Urb->UrbHeader.Status, status, ioStatus.Status));

	ntStatus = ioStatus.Status;


	Ezusb_KdPrint(("exit Ezusb_CallUSBD (%x)\n", ntStatus));

	return ntStatus;
}

NTSTATUS __stdcall USBSendVendorDeviceRequest(
	PDEVICE_OBJECT fdo,
	UCHAR Request,
	BOOLEAN Read,
	int BufferLength,
	PVOID Buffer,
	USHORT Value,
	USHORT Index)
{
	PURB urb;
	NTSTATUS ntStatus;

	urb = (PURB)ALLOC_NON_PAGED(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));
	if (urb)
	{
		urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
		urb->UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;
		urb->UrbControlVendorClassRequest.TransferFlags = USBD_SHORT_TRANSFER_OK;
		if (Read)
			urb->UrbControlVendorClassRequest.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
		urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
		urb->UrbControlVendorClassRequest.TransferBufferLength = BufferLength;
		urb->UrbControlVendorClassRequest.TransferBuffer = Buffer;
		urb->UrbControlVendorClassRequest.Request = Request;
		urb->UrbControlVendorClassRequest.Value = Value;
		urb->UrbControlVendorClassRequest.Index = Index;
		ntStatus = Ezusb_CallUSBD(fdo, urb);
	}
	else
	{
		ntStatus = STATUS_NO_MEMORY;
	}
	if (urb)
		ExFreePool(urb);
	return ntStatus;
}

NTSTATUS Ezusb_8051Reset(
	PDEVICE_OBJECT fdo,
	UCHAR resetBit
)
/*++
Routine Description:
   Uses the ANCHOR LOAD vendor specific command to either set or release the
   8051 reset bit in the EZ-USB chip.
Arguments:
   fdo - pointer to the device object for this instance of an Ezusb Device
   resetBit - 1 sets the 8051 reset bit (holds the 8051 in reset)
			  0 clears the 8051 reset bit (8051 starts running)

Return Value:
   STATUS_SUCCESS if successful,
   STATUS_UNSUCCESSFUL otherwise
--*/
{
	NTSTATUS ntStatus;

	ntStatus = USBSendVendorDeviceRequest(fdo, 0xA0, 0, 1, &resetBit, CPUCS_REG_EZUSB, 0);
	if (NT_SUCCESS(ntStatus))
		ntStatus = USBSendVendorDeviceRequest(fdo, 0xA0, 0, 1, &resetBit, CPUCS_REG_FX2, 0);
	return ntStatus;
}

///////////////////////////////////////////////////////////////////////////////
// @func Lock a SIMPLE device object
// @parm Address of our device extension
// @rdesc TRUE if it was possible to lock the device, FALSE otherwise.
// @comm A FALSE return value indicates that we're in the process of deleting
// the device object, so all new requests should be failed

BOOLEAN LockDevice(
	IN PDEVICE_OBJECT fdo
)
{
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	// Increment use count on our device object
	LONG usage = InterlockedIncrement(&pdx->usage);

	// AddDevice initialized the use count to 1, so it ought to be bigger than
	// one now. HandleRemoveDevice sets the "removing" flag and decrements the
	// use count, possibly to zero. So if we find a use count of "1" now, we
	// should also find the "removing" flag set.

	ASSERT(usage > 1 || pdx->removing);

	(void)usage;

	// If device is about to be removed, restore the use count and return FALSE.
	// If we're in a race with HandleRemoveDevice (maybe running on another CPU),
	// the sequence we've followed is guaranteed to avoid a mistaken deletion of
	// the device object. If we test "removing" after HandleRemoveDevice sets it,
	// we'll restore the use count and return FALSE. In the meantime, if
	// HandleRemoveDevice decremented the count to 0 before we did our increment,
	// its thread will have set the remove event. Otherwise, we'll decrement to 0
	// and set the event. Either way, HandleRemoveDevice will wake up to finish
	// removing the device, and we'll return FALSE to our caller.
	// 
	// If, on the other hand, we test "removing" before HandleRemoveDevice sets it,
	// we'll have already incremented the use count past 1 and will return TRUE.
	// Our caller will eventually call UnlockDevice, which will decrement the use
	// count and might set the event HandleRemoveDevice is waiting on at that point.

	if (pdx->removing)
	{
		if (InterlockedDecrement(&pdx->usage) == 0)
			KeSetEvent(&pdx->evRemove, 0, FALSE);
		return FALSE;
	}

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// @func Unlock a SIMPLE device object
// @parm Address of our device extension
// @comm If the use count drops to zero, set the evRemove event because we're
// about to remove this device object.

void UnlockDevice(
	PDEVICE_OBJECT fdo
)
{
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	LONG usage = InterlockedDecrement(&pdx->usage);

	ASSERT(usage >= 0);

	if (usage == 0)
	{						// removing device
		ASSERT(pdx->removing);	// HandleRemoveDevice should already have set this
		KeSetEvent(&pdx->evRemove, 0, FALSE);
	}						// removing device
}

NTSTATUS
Ezusb_RemoveDevice(
	IN  PDEVICE_OBJECT fdo
)
/*++

Routine Description:
	Removes a given instance of a Ezusb Device device on the USB.

Arguments:
	fdo - pointer to the device object for this instance of a Ezusb Device

Return Value:
	NT status code

--*/
{
	PDEVICE_EXTENSION pdx;
	NTSTATUS ntStatus = STATUS_SUCCESS;

	Ezusb_KdPrint(("enter Ezusb_RemoveDevice\n"));

	pdx = fdo->DeviceExtension;

	IoDetachDevice(pdx->StackDeviceObject);

	IoDeleteDevice(fdo);

	Ezusb_KdPrint(("exit Ezusb_RemoveDevice (%x)\n", ntStatus));

	return ntStatus;
}

NTSTATUS
Ezusb_HandleRemoveDevice(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp
)
{
	NTSTATUS ntStatus;
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	pdx->removing = TRUE;
	UnlockDevice(fdo);			// once for LockDevice at start of dispatch
	UnlockDevice(fdo);			// once for initialization during AddDevice
	KeWaitForSingleObject(&pdx->evRemove, Executive, KernelMode, FALSE, NULL);

	// Let lower-level drivers handle this request. Ignore whatever
	// result eventuates.

//   ntStatus = Ezusb_DefaultPnpHandler(fdo, Irp);

//   Ezusb_Cleanup(fdo);

   // Remove the device object

	Ezusb_RemoveDevice(fdo);

	ntStatus = Ezusb_DefaultPnpHandler(fdo, Irp);

	return ntStatus;				// lower-level completed IoStatus already

}

///////////////////////////////////////////////////////////////////////////////
// @func Handle completion of a request by a lower-level driver
// @parm Functional device object
// @parm I/O request which has completed
// @parm Context argument supplied to IoSetCompletionRoutine, namely address of
// KEVENT object on which ForwardAndWait is waiting
// @comm This is the completion routine used for requests forwarded by ForwardAndWait. It
// sets the event object and thereby awakens ForwardAndWait.
// @comm Note that it's *not* necessary for this particular completion routine to test
// the PendingReturned flag in the IRP and then call IoMarkIrpPending. You do that in many
// completion routines because the dispatch routine can't know soon enough that the
// lower layer has returned STATUS_PENDING. In our case, we're never going to pass a
// STATUS_PENDING back up the driver chain, so we don't need to worry about this.

NTSTATUS
OnRequestComplete(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp,
	IN PKEVENT pev
)
/*++

Routine Description:
   Handle completion of a request by a lower-level driver

Arguments:
   DriverObject -  Functional device object
   Irp - I/O request which has completed
   pev - Context argument supplied to IoSetCompletionRoutine, namely address of
		 KEVENT object on which ForwardAndWait is waiting

Return Value:
   STATUS_MORE_PROCESSING_REQUIRED
--*/
{
	(void)fdo;
	(void)Irp;
	KeSetEvent(pev, 0, FALSE);
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ForwardAndWait(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp
)
/*++
Routine Description:
   Forward request to lower level and await completion

   The only purpose of this routine in this particular driver is to pass down
   IRP_MN_START_DEVICE requests and wait for the PDO to handle them.

   The processor must be at PASSIVE IRQL because this function initializes
   and waits for non-zero time on a kernel event object.

Arguments:
   fdo - pointer to a device object
   Irp          - pointer to an I/O Request Packet

Return Value:
   STATUS_SUCCESS if successful,
   STATUS_UNSUCCESSFUL otherwise
--*/
{
	KEVENT event;
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	NTSTATUS ntStatus;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	//
   // Initialize a kernel event object to use in waiting for the lower-level
	// driver to finish processing the object. 
   //
	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)OnRequestComplete,
		(PVOID) & event, TRUE, TRUE, TRUE);

	ntStatus = IoCallDriver(pdx->StackDeviceObject, Irp);

	if (ntStatus == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		ntStatus = Irp->IoStatus.Status;
	}

	return ntStatus;
}

NTSTATUS Ezusb_DownloadIntelHex(
	PDEVICE_OBJECT fdo,
	PINTEL_HEX_RECORD hexRecord
)
/*++

Routine Description:
   This function downloads Intel Hex Records to the EZ-USB device.  If any of the hex records
   are destined for external RAM, then the caller must have previously downloaded firmware
   to the device that knows how to download to external RAM (ie. firmware that implements
   the ANCHOR_LOAD_EXTERNAL vendor specific command).

Arguments:
   fdo - pointer to the device object for this instance of an Ezusb Device
   hexRecord - pointer to an array of INTEL_HEX_RECORD structures.  This array
			   is terminated by an Intel Hex End record (Type = 1).

Return Value:
   STATUS_SUCCESS if successful,
   STATUS_UNSUCCESSFUL otherwise

--*/
{
	NTSTATUS ntStatus = STATUS_SUCCESS;
	PURB urb = NULL;
	PINTEL_HEX_RECORD ptr = hexRecord;

	urb = ALLOC_NON_PAGED(sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

	if (urb)
	{
		//
		// The download must be performed in two passes.  The first pass loads all of the
		// external addresses, and the 2nd pass loads to all of the internal addresses.
		// why?  because downloading to the internal addresses will probably wipe out the firmware
		// running on the device that knows how to receive external ram downloads.
		//
		//
		// First download all the records that go in external ram
		//
		while (ptr->Type == 0)
		{
			if (!INTERNAL_RAM(ptr->Address))
			{
				RtlZeroMemory(urb, sizeof(struct  _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

				urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
				urb->UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;
				urb->UrbControlVendorClassRequest.TransferBufferLength = ptr->Length;
				urb->UrbControlVendorClassRequest.TransferBuffer = ptr->Data;
				urb->UrbControlVendorClassRequest.Request = ANCHOR_LOAD_EXTERNAL;
				urb->UrbControlVendorClassRequest.Value = ptr->Address;
				urb->UrbControlVendorClassRequest.Index = 0;

				Ezusb_KdPrint(("Downloading %d bytes to 0x%x\n", ptr->Length, ptr->Address));

				ntStatus = Ezusb_CallUSBD(fdo, urb);

				if (!NT_SUCCESS(ntStatus))
					break;
			}
			ptr++;
		}

		//
		// Now download all of the records that are in internal RAM.  Before starting
		// the download, stop the 8051.
		//
		Ezusb_8051Reset(fdo, 1);
		ptr = hexRecord;
		while (ptr->Type == 0)
		{
			if (INTERNAL_RAM(ptr->Address))
			{
				RtlZeroMemory(urb, sizeof(struct  _URB_CONTROL_VENDOR_OR_CLASS_REQUEST));

				urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
				urb->UrbHeader.Function = URB_FUNCTION_VENDOR_DEVICE;
				urb->UrbControlVendorClassRequest.TransferBufferLength = ptr->Length;
				urb->UrbControlVendorClassRequest.TransferBuffer = ptr->Data;
				urb->UrbControlVendorClassRequest.Request = ANCHOR_LOAD_INTERNAL;
				urb->UrbControlVendorClassRequest.Value = ptr->Address;
				urb->UrbControlVendorClassRequest.Index = 0;

				Ezusb_KdPrint(("Downloading %d bytes to 0x%x\n", ptr->Length, ptr->Address));

				ntStatus = Ezusb_CallUSBD(fdo, urb);

				if (!NT_SUCCESS(ntStatus))
					break;
			}
			ptr++;
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

NTSTATUS __stdcall ReadStringValueFromRegistryKey(
	HANDLE KeyHandle,
	PUNICODE_STRING ValueName,
	PUNICODE_STRING DestinationString)
{
	NTSTATUS ntStatus;
	PKEY_VALUE_PARTIAL_INFORMATION keyValueInfo;
	ULONG keyValueInfoLength;
	ULONG keyType;
	UNICODE_STRING valueString;

	keyValueInfoLength = 0;
	ntStatus = ZwQueryValueKey(KeyHandle, ValueName, KeyValuePartialInformation, 0, 0, &keyValueInfoLength);
	if ((ntStatus == STATUS_BUFFER_TOO_SMALL || NT_SUCCESS(ntStatus)) && keyValueInfoLength)
	{
		keyValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ALLOC_PAGED(keyValueInfoLength);
		if (keyValueInfo)
		{
			ntStatus = ZwQueryValueKey(
				KeyHandle,
				ValueName,
				KeyValuePartialInformation,
				keyValueInfo,
				keyValueInfoLength,
				&keyValueInfoLength);
			if (NT_SUCCESS(ntStatus))
			{
				keyType = keyValueInfo->Type;
				if (keyType != REG_NONE)
				{
					if (keyType == REG_SZ || keyType == REG_EXPAND_SZ)
					{
						valueString.Length = (USHORT)keyValueInfo->DataLength;
						valueString.MaximumLength = valueString.Length;
						valueString.Buffer = (PWSTR)keyValueInfo->Data;
						DestinationString->MaximumLength = (USHORT)keyValueInfo->DataLength;
						DestinationString->Buffer = (WCHAR*)ALLOC_PAGED(keyValueInfo->DataLength);
						if (DestinationString->Buffer)
							RtlCopyUnicodeString(DestinationString, &valueString);
						else
							ntStatus = STATUS_INSUFFICIENT_RESOURCES;
					}
					else if (keyType == REG_BINARY || keyType == REG_DWORD || keyType == REG_DWORD_BIG_ENDIAN || keyType == REG_MULTI_SZ)
					{
						ntStatus = STATUS_UNKNOWN_REVISION;
					}
				}
			}
			ExFreePool(keyValueInfo);
		}
		else
		{
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	return ntStatus;
}

// The algorithm used in this function is questionable, but it seems to work for this driver's use case.
// In the future, replacing it might be wise. The intent is obvious, but the implementation is a bit odd.
void __stdcall CutAndAppend(PUNICODE_STRING ModifyString, PCWSTR Append, PCWSTR Remove, ULONG RemoveLen)
{
	int modifyIdx;
	UNICODE_STRING AppendString;
	BOOLEAN removedAnyCharacter;
	ULONG removeIdx;

	// Do an initial pass to remove bad characters from ModifyString
	modifyIdx = 0;
	removedAnyCharacter = TRUE;
	while (ModifyString->MaximumLength >= ModifyString->Length && modifyIdx >= 0 && removedAnyCharacter)
	{
		removedAnyCharacter = FALSE;
		modifyIdx = (ModifyString->Length / 2) - 1;
		for (removeIdx = 0; removeIdx < RemoveLen; removeIdx++)
		{
			while (Remove[removeIdx] == ModifyString->Buffer[modifyIdx])
			{
				if (modifyIdx < 0)
					break;
				ModifyString->Length -= 2;
				modifyIdx = (ModifyString->Length / 2) - 1;
				removedAnyCharacter = TRUE;
			}
		}
	}

	// Append to ModifyString
	RtlInitUnicodeString(&AppendString, Append);
	if (ModifyString->MaximumLength >= AppendString.Length + ModifyString->Length)
	{
		RtlAppendUnicodeStringToString(ModifyString, &AppendString);
		if (!ModifyString->Buffer[(ModifyString->Length / 2) - 1])
			ModifyString->Length -= 2;
	}

	// Do a final pass to remove bad characters that were introduced by the append
	modifyIdx = 0;
	removedAnyCharacter = TRUE;
	while (ModifyString->MaximumLength >= ModifyString->Length && modifyIdx >= 0 && removedAnyCharacter)
	{
		removedAnyCharacter = FALSE;
		modifyIdx = (ModifyString->Length / 2) - 1;

		for (removeIdx = 0; removeIdx < RemoveLen; removeIdx++)
		{
			while (Remove[removeIdx] == ModifyString->Buffer[modifyIdx])
			{
				if (modifyIdx < 0)
					break;
				ModifyString->Length -= 2;
				modifyIdx = (ModifyString->Length / 2) - 1;
				removedAnyCharacter = TRUE;
			}
		}
	}
}

LONG __stdcall ParseHexValue(PCHAR hexString, ULONG numHexChars)
{
	ULONG iHexChar;
	int result;
	char hexChar;
	BYTE nibble;

	if (numHexChars > 4)
		return -1;

	result = 0;
	for (iHexChar = 0; iHexChar < numHexChars; iHexChar++)
	{
		hexChar = hexString[iHexChar];

		if (hexChar >= '0' && hexChar <= '9')
		{
			nibble = (BYTE)(hexChar - '0');
		}
		else if (hexChar >= 'A' && hexChar <= 'F')
		{
			nibble = (BYTE)(hexChar - '7');
		}
		else if (hexChar >= 'a' && hexChar <= 'f')
		{
			nibble = (BYTE)(hexChar - 'W');
		}
		else
		{
			return -1;
		}
		
		result = (result << 4) | nibble;
	}

	return result;
}

NTSTATUS __stdcall ReadHexRecordsFromFile(
	HANDLE FileHandle,
	PINTEL_HEX_RECORD pHexRecord,
	ULONG maxRecords,
	ULONG* pNumRecords)
{
	NTSTATUS ntStatus;
	PCHAR fileData;
	ULONG readLineLength;
	int recordType;
	int recordByteCountParseResult;
	ULONG recordByteCount;
	int recordAddressParseResult;
	WORD recordAddress;
	ULONG recordBytesWritten;
	int iDataByte;
	int dataByte;
	CHAR hexString[256];
	FILE_STANDARD_INFORMATION fileInformation;
	IO_STATUS_BLOCK ioStatusBlock;
	int typeOffset;
	int addressOffset;
	int byteCountOffset;
	PCHAR fileDataCursor;
	int dataOffset;
	PCHAR dataSlice;
	ULONG fileLength;

	byteCountOffset = 1;
	addressOffset = 3;
	typeOffset = 7;
	dataOffset = 9;
	if (!FileHandle)
		return STATUS_UNSUCCESSFUL;
	ntStatus = ZwQueryInformationFile(FileHandle, &ioStatusBlock, &fileInformation, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	if (fileInformation.EndOfFile.HighPart > 0)
		return STATUS_INSUFFICIENT_RESOURCES;
	fileLength = fileInformation.EndOfFile.LowPart;
	fileData = (PCHAR)ALLOC_PAGED(fileLength + 1);
	if (!fileData)
		return STATUS_INSUFFICIENT_RESOURCES;
	ntStatus = ZwReadFile(FileHandle, NULL, NULL, NULL, &ioStatusBlock, fileData, fileInformation.EndOfFile.LowPart, NULL, NULL);

	__try
	{
		if (NT_SUCCESS(ntStatus))
		{
			fileData[fileInformation.EndOfFile.LowPart] = '\0';
			*pNumRecords = 0;
			for (fileDataCursor = fileData, readLineLength = ReadLine(fileData, hexString, 256);
				readLineLength != ULONG_MAX && readLineLength != 0;
				readLineLength = ReadLine(fileDataCursor, hexString, 256))
			{
				if (readLineLength > 256 || readLineLength > fileInformation.EndOfFile.LowPart)
					return STATUS_UNSUCCESSFUL;
				for (fileDataCursor += readLineLength; *fileDataCursor == '\r' || *fileDataCursor == '\n'; ++fileDataCursor)
					;
				if (hexString[0] != ':')
					return STATUS_UNSUCCESSFUL;
				if (hexString[1] == ' ')
				{
					byteCountOffset = 2;
					addressOffset = 5;
					typeOffset = 10;
					dataOffset = 13;
				}
				recordType = ParseHexValue(&hexString[typeOffset], 2);
				if (recordType < 0)
					return STATUS_UNSUCCESSFUL;
				
				if (recordType == 0)
				{
					// Data record

					recordByteCountParseResult = ParseHexValue(&hexString[byteCountOffset], 2);
					if (recordByteCountParseResult < 0)
						return STATUS_UNSUCCESSFUL;
					recordByteCount = (BYTE)recordByteCountParseResult;
					recordAddressParseResult = ParseHexValue(&hexString[addressOffset], 4);
					if (recordAddressParseResult < 0)
						return STATUS_UNSUCCESSFUL;
					recordAddress = (WORD)recordAddressParseResult;
					if (recordByteCount > (readLineLength - dataOffset) / 2)
						return STATUS_UNSUCCESSFUL;

					recordBytesWritten = 0;

					// We have to split large (>16 byte) records into smaller (<=16 byte) ones.
					while (recordBytesWritten < recordByteCount && *pNumRecords <= maxRecords)
					{
						++* pNumRecords;
						pHexRecord->Type = 0;
						pHexRecord->Length = (BYTE)min(16, recordByteCount - recordBytesWritten);
						pHexRecord->Address = (WORD)(recordAddress + recordBytesWritten);
						dataSlice = &hexString[dataOffset];

						for (iDataByte = 0; iDataByte < pHexRecord->Length; iDataByte++)
						{
							dataByte = ParseHexValue(dataSlice, 2);
							if (dataByte < 0)
								return STATUS_UNSUCCESSFUL;
							dataSlice += 2;
							pHexRecord->Data[iDataByte] = (BYTE)dataByte;
						}

						recordBytesWritten += pHexRecord->Length;
						++pHexRecord;
					}

					// This is only possible if we exceeded the record limit
					// during the splitting process. Since we won't be able to
					// process any more records, we give up here.
					if (recordBytesWritten < recordByteCount)
					{
						return STATUS_UNSUCCESSFUL;
					}
				}
				else if (recordType == 1)
				{
					// End Of File record

					pHexRecord->Length = 0;
					pHexRecord->Data[0] = 0;
					pHexRecord->Type = 1;
					pHexRecord->Address = 0;

					return STATUS_SUCCESS;
				}
				else if (recordType == 2)
				{
					// Extended Segment Address record - we just ignore these

					if (ParseHexValue(&hexString[dataOffset], 4) < 0)
						return STATUS_UNSUCCESSFUL;
				}
			}

			// If we made it here, we were probably asked to read a hex file with record types we can't handle.
			// Other possibilities include: not reading *any* records; never seeing an "end of data" record
			// In any case, something went horribly wrong, so we give up here.
			return STATUS_UNSUCCESSFUL;
		}
	}
	__finally
	{
		ExFreePool(fileData);
	}

	return ntStatus;
}

NTSTATUS __stdcall Ezusb_StartDevice(PDEVICE_OBJECT fdo)
{
	NTSTATUS ntStatus;
	PINTEL_HEX_RECORD hexRecords;
	wchar_t Dest[100];
	IO_STATUS_BLOCK ioStatus;
	OBJECT_ATTRIBUTES objectAttributes;
	UNICODE_STRING fwDirValueName;
	DEVICE_PERSONALITY devicePersonality = { 0 };
	UNICODE_STRING firmwareDirectory;
	void* FileHandle;
	void* KeyHandle;
	UNICODE_STRING Source;
	UNICODE_STRING FullFirmwarePathUnicode;
	ULONG discard;

	ntStatus = Ezusb_8051Reset(fdo, 1u);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	ntStatus = Ezusb_DownloadIntelHex(fdo, loader);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	ntStatus = Ezusb_8051Reset(fdo, 0);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	ntStatus = USBSendVendorDeviceRequest(fdo, 164, FALSE, 0, NULL, 161, 0);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	ntStatus = USBSendVendorDeviceRequest(fdo, PAKON_GET_PERSONALITY, TRUE, sizeof(DEVICE_PERSONALITY), &devicePersonality, 0, 0);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	InitializeObjectAttributes(&objectAttributes, &DriverRegistryPath, 0, NULL, NULL);
	ntStatus = ZwOpenKey(&KeyHandle, KEY_READ, &objectAttributes);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	RtlInitUnicodeString(&fwDirValueName, L"FirmwareDirectory");
	ntStatus = ReadStringValueFromRegistryKey(KeyHandle, &fwDirValueName, &firmwareDirectory);
	if (!NT_SUCCESS(ntStatus))
	{
		ZwClose(KeyHandle);
		return ntStatus;
	}
	RtlInitUnicodeString(&FullFirmwarePathUnicode, L"");
	ntStatus = STATUS_UNKNOWN_REVISION;
	if (devicePersonality.id != 0xC0)
	{
		if (devicePersonality.id != 0xC2)
		{
			RtlInitUnicodeString(&Source, L"PknInit.hex");
			FullFirmwarePathUnicode.MaximumLength = Source.MaximumLength + firmwareDirectory.MaximumLength + 4;
			FullFirmwarePathUnicode.Buffer = (PWSTR)ALLOC_PAGED(FullFirmwarePathUnicode.MaximumLength);
			if (FullFirmwarePathUnicode.Buffer)
			{
				RtlCopyUnicodeString(&FullFirmwarePathUnicode, &firmwareDirectory);
				FullFirmwarePathUnicode.MaximumLength = Source.MaximumLength + firmwareDirectory.MaximumLength + 4;
				CutAndAppend(&FullFirmwarePathUnicode, L"", L"\t\n\r\\\x00\x20", 7);
				CutAndAppend(&FullFirmwarePathUnicode, L"\\", L"\t\n\r\x00\x20", 6);
				RtlAppendUnicodeStringToString(&FullFirmwarePathUnicode, &Source);
				CutAndAppend(&FullFirmwarePathUnicode, L"", L"\t\n\r\x00\x20", 6);
				ntStatus = STATUS_SUCCESS;
			}
		}
		else
		{
			ntStatus = STATUS_SUCCESS;
		}
	}
	else if (devicePersonality.wVendorId == 0xF05)
	{
		_snwprintf_s(Dest, 0x64u, 0x64, L"%4.4X_%4.4X", devicePersonality.wProductId, devicePersonality.wRevision);
		RtlInitUnicodeString(&fwDirValueName, Dest);
		ntStatus = ReadStringValueFromRegistryKey(KeyHandle, &fwDirValueName, &Source);

		if (NT_SUCCESS(ntStatus))
		{
			FullFirmwarePathUnicode.MaximumLength = Source.MaximumLength + firmwareDirectory.MaximumLength + 4;
			FullFirmwarePathUnicode.Buffer = (PWSTR)ALLOC_PAGED(FullFirmwarePathUnicode.MaximumLength);
			if (FullFirmwarePathUnicode.Buffer)
			{
				RtlCopyUnicodeString(&FullFirmwarePathUnicode, &firmwareDirectory);
				FullFirmwarePathUnicode.MaximumLength = Source.MaximumLength + firmwareDirectory.MaximumLength + 4;
				CutAndAppend(&FullFirmwarePathUnicode, L"", L"\t\n\r\\", 7);
				CutAndAppend(&FullFirmwarePathUnicode, L"\\", L"\t\n\r", 6);
				RtlAppendUnicodeStringToString(&FullFirmwarePathUnicode, &Source);
				CutAndAppend(&FullFirmwarePathUnicode, L"", L"\t\n\r", 6);
				ntStatus = STATUS_SUCCESS;
			}
			else
			{
				ntStatus = STATUS_INSUFFICIENT_RESOURCES;
			}
			ExFreePool(Source.Buffer);
		}
	}
	ExFreePool(firmwareDirectory.Buffer);
	ZwClose(KeyHandle);
	if (!NT_SUCCESS(ntStatus))
		return ntStatus;
	if (FullFirmwarePathUnicode.Length > 1u)
	{
		InitializeObjectAttributes(&objectAttributes, &FullFirmwarePathUnicode, OBJ_CASE_INSENSITIVE, NULL, NULL);
		ntStatus = ZwCreateFile(
			&FileHandle,
			GENERIC_READ,
			&objectAttributes,
			&ioStatus,
			NULL,
			0,
			FILE_SHARE_READ,
			FILE_OPEN,
			FILE_SYNCHRONOUS_IO_NONALERT,
			NULL,
			0);
		FullFirmwarePathUnicode.MaximumLength = 0;
		FullFirmwarePathUnicode.Length = 0;
		ExFreePool(FullFirmwarePathUnicode.Buffer);
		if (!NT_SUCCESS(ntStatus))
			return ntStatus;
		hexRecords = (PINTEL_HEX_RECORD)ALLOC_PAGED(MAX_FIRMWARE_RECORDS * sizeof(INTEL_HEX_RECORD));
		if (!hexRecords)
		{
			ZwClose(FileHandle);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		ntStatus = ReadHexRecordsFromFile(FileHandle, hexRecords, MAX_FIRMWARE_RECORDS, &discard);
		ZwClose(FileHandle);
		if (!NT_SUCCESS(ntStatus))
		{
			ExFreePool(hexRecords);
			return ntStatus;
		}
		ntStatus = Ezusb_DownloadIntelHex(fdo, hexRecords);
		ExFreePool(hexRecords);
		if (!NT_SUCCESS(ntStatus))
		{
			return ntStatus;
		}
	}
	ntStatus = Ezusb_8051Reset(fdo, 1u);
	if (NT_SUCCESS(ntStatus))
	{
		ntStatus = Ezusb_8051Reset(fdo, 0);
		if (NT_SUCCESS(ntStatus))
			return STATUS_SUCCESS;
	}
	return ntStatus;
}

NTSTATUS
Ezusb_HandleStartDevice(
	IN PDEVICE_OBJECT fdo,
	IN PIRP Irp
)
{
	NTSTATUS ntStatus;

	//
	// First let all lower-level drivers handle this request.
	//
	ntStatus = ForwardAndWait(fdo, Irp);
	if (!NT_SUCCESS(ntStatus))
		return CompleteRequest(Irp, ntStatus, Irp->IoStatus.Information);

	//
	// now do whatever we need to do to start the device
	//
	ntStatus = Ezusb_StartDevice(fdo);

	return CompleteRequest(Irp, ntStatus, 0);
}

NTSTATUS
Ezusb_DispatchPnp(
	IN PDEVICE_OBJECT fdo,
	IN PIRP           Irp
)
/*++
Routine Description:
   Process Plug and Play IRPs sent to this device.

Arguments:
   fdo - pointer to a device object
   Irp          - pointer to an I/O Request Packet

Return Value:
   NTSTATUS
--*/
{
	PIO_STACK_LOCATION irpStack;
	PDEVICE_EXTENSION pdx = fdo->DeviceExtension;
	ULONG fcn;
	NTSTATUS ntStatus;

	(void)pdx;
	Ezusb_KdPrint(("Enter Ezusb_DispatchPnp\n"));

	if (!LockDevice(fdo))
		return CompleteRequest(Irp, STATUS_DELETE_PENDING, 0);

	//
	// Get a pointer to the current location in the Irp. This is where
	//     the function codes and parameters are located.
	//
	irpStack = IoGetCurrentIrpStackLocation(Irp);

	//ASSERT(irpStack->MajorFunction == IRP_MJ_PNP);

	fcn = irpStack->MinorFunction;

	switch (fcn)
	{
	case IRP_MN_START_DEVICE:

		Ezusb_KdPrint(("IRP_MN_START_DEVICE\n"));

		ntStatus = Ezusb_HandleStartDevice(fdo, Irp);

		break; //IRP_MN_START_DEVICE

	case IRP_MN_REMOVE_DEVICE:

		Ezusb_KdPrint(("IRP_MN_REMOVE_DEVICE\n"));

		ntStatus = Ezusb_HandleRemoveDevice(fdo, Irp);

		break; //IRP_MN_REMOVE_DEVICE

	case IRP_MN_QUERY_CAPABILITIES:
	{
		//
		// This code swiped from Walter Oney.  Please buy his book!!
		//

		PDEVICE_CAPABILITIES pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;

		Ezusb_KdPrint(("IRP_MN_QUERY_CAPABILITIES\n"));

		// Check to besure we know how to handle this version of the capabilities structure

		if (pdc->Version < 1)
		{
			ntStatus = Ezusb_DefaultPnpHandler(fdo, Irp);
			break;
		}

		ntStatus = ForwardAndWait(fdo, Irp);
		if (NT_SUCCESS(ntStatus))
		{						// IRP succeeded
			pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;
			// setting this field prevents NT5 from notifying the user when the
			// device is removed.
			pdc->SurpriseRemovalOK = TRUE;
		}						// IRP succeeded

		ntStatus = CompleteRequest(Irp, ntStatus, Irp->IoStatus.Information);
	}
	break; //IRP_MN_QUERY_CAPABILITIES


	//
	// All other PNP IRP's are just passed down the stack by the default handler
	//
	default:
		Ezusb_KdPrint(("Passing down unhandled PnP IOCTL MJ=0x%x MN=0x%x\n",
			irpStack->MajorFunction, irpStack->MinorFunction));
		ntStatus = Ezusb_DefaultPnpHandler(fdo, Irp);

	} // switch MinorFunction

	if (fcn != IRP_MN_REMOVE_DEVICE)
		UnlockDevice(fdo);

	Ezusb_KdPrint(("Exit Ezusb_DispatchPnp %x\n", ntStatus));
	return ntStatus;

}//Ezusb_Dispatch

void __stdcall Ezusb_Unload(PDRIVER_OBJECT DriverObject)
{
	(void)DriverObject;
	RtlFreeUnicodeString(&DriverRegistryPath);
}

NTSTATUS
Ezusb_PnPAddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject
)
/*++
Routine Description:
	This routine is called to create a new instance of the device

Arguments:
	DriverObject - pointer to the driver object for this instance of Ezusb
	PhysicalDeviceObject - pointer to a device object created by the bus

Return Value:
	STATUS_SUCCESS if successful,
	STATUS_UNSUCCESSFUL otherwise

--*/
{
	NTSTATUS                ntStatus = STATUS_SUCCESS;
	PDEVICE_OBJECT          fdo = NULL;
	PDEVICE_EXTENSION       pdx;

	Ezusb_KdPrint(("enter Ezusb_PnPAddDevice\n"));

	ntStatus = IoCreateDevice(DriverObject,
		sizeof(DEVICE_EXTENSION),
		NULL,
		FILE_DEVICE_UNKNOWN,
		0,
		FALSE,
		&fdo);

	if (NT_SUCCESS(ntStatus))
	{
		pdx = fdo->DeviceExtension;

		//
		// Non plug and play drivers usually create the device object in
		// driver entry, and the I/O manager autimatically clears this flag.
		// Since we are creating the device object ourselves in response to 
		// a PnP START_DEVICE IRP, we need to clear this flag ourselves.
		//
		fdo->Flags &= ~DO_DEVICE_INITIALIZING;

		//
		// This driver uses direct I/O for read/write requests
		//
		fdo->Flags |= DO_DIRECT_IO;
		//
		//
		// store away the Physical device Object
		//
		pdx->PhysicalDeviceObject = PhysicalDeviceObject;

		//
		// Attach to the StackDeviceObject.  This is the device object that what we 
		// use to send Irps and Urbs down the USB software stack
		//
		pdx->StackDeviceObject =
			IoAttachDeviceToDeviceStack(fdo, PhysicalDeviceObject);

		ASSERT(pdx->StackDeviceObject != NULL);

		pdx->usage = 1;				// locked until RemoveDevice
		KeInitializeEvent(&pdx->evRemove,
			NotificationEvent,
			FALSE);              // set when use count drops to zero
	}

	Ezusb_KdPrint(("exit Ezusb_PnPAddDevice (%x)\n", ntStatus));

	return ntStatus;
}

NTSTATUS __stdcall DispatchWmi(PDEVICE_OBJECT fdo, PIRP Irp)
{							// DispatchWmi
	PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(pdx->StackDeviceObject, Irp);
}							// DispatchWmi

NTSTATUS __stdcall DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	USHORT numberOfBytes;
	Ezusb_KdPrint(("DriverEntry called with DO %p and RP %ls\n", DriverObject, RegistryPath->Buffer));

	numberOfBytes = RegistryPath->Length + 2;
	DriverRegistryPath.Buffer = (PWSTR)ALLOC_PAGED(numberOfBytes);
	if (!DriverRegistryPath.Buffer)
	{
		Ezusb_KdPrint(("ERROR: Couldn't allocate %d bytes for registry path!\n", numberOfBytes));
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	DriverRegistryPath.MaximumLength = numberOfBytes;
	RtlCopyUnicodeString(&DriverRegistryPath, RegistryPath);
	DriverRegistryPath.Buffer[RegistryPath->Length >> 1] = 0;
	DriverObject->DriverUnload = Ezusb_Unload;
	DriverObject->MajorFunction[IRP_MJ_POWER] = Ezusb_DispatchPnp;
	DriverObject->MajorFunction[IRP_MJ_PNP] = Ezusb_DispatchPnp;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = DispatchWmi;
	DriverObject->DriverExtension->AddDevice = Ezusb_PnPAddDevice;

	Ezusb_KdPrint(("Setup done! Let's roll\n"));

	return 0;
}