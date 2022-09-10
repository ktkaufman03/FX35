#include "rw.h"

#include <cstddef>

#include "driver.h"
#include <usbioctl.h>
#include <usbdlib.h>

void __stdcall ReleaseContextResources(PRWCONTEXT ctx)
{
	if (ctx->RingPacketMdl)
	{
		ObDereferenceObject(ctx->EventScanPacketReady);
		ctx->pRingTail->m_bTransferInProgress = false;
		IoFreeMdl(ctx->RingPacketMdl);
		ctx->RingPacketMdl = nullptr;
		ctx->pRingTail = nullptr;
	}
}

bool __stdcall DestroyContextStructure(PRWCONTEXT P)
{
	if (InterlockedDecrement(&P->refcnt))
		return false;
	for (ULONG i = 0; i < P->numirps; ++i)
	{
		PIRP irp = P->RingPackets[i].Irp;
		if (irp)
			IoFreeIrp(irp);
	}
	ReleaseContextResources(P);
	ExFreePool(P);
	return true;
}

NTSTATUS __stdcall AllDone(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRWCONTEXT Context)
{
	if (Context)
	{
		PDEVICE_EXTENSION pdx = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;
		if (!NT_SUCCESS(Irp->IoStatus.Status))
		{
			InterlockedIncrement(&pdx->numfailed);
		}
		else
		{
			Irp->IoStatus.Information = Context->info;
			Irp->IoStatus.Status = Context->status;
		}
		if (DestroyContextStructure(Context))
		{
			Irp->Tail.Overlay.DriverContext[0] = nullptr;
			StartNextPacket(&pdx->dqReadWrite, DeviceObject);
			IoReleaseRemoveLock(&pdx->RemoveLock, Irp);
		}
		else
		{
			return STATUS_MORE_PROCESSING_REQUIRED;
		}
	}
	return STATUS_SUCCESS;
}

NTSTATUS __stdcall RingPacketComplete(PDEVICE_OBJECT fdo, PIRP Irp, PRWCONTEXT Context)
{
	size_t packetIndex;
	PRING_PACKET ringPacket;
	PDEVICE_EXTENSION pdx;

	packetIndex = (size_t)Irp->Tail.Overlay.CurrentStackLocation->Parameters.Others.Argument2;
	ringPacket = &Context->RingPackets[packetIndex];
	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;

	if (ringPacket->cancel)
	{
		PUCHAR ringPacketData;
		NTSTATUS lastUrbStatus;

		ringPacketData = (PUCHAR)MmGetMdlVirtualAddress(ringPacket->Mdl);
		lastUrbStatus = ringPacket->Urb->UrbHeader.Status;

		if (!NT_SUCCESS(lastUrbStatus))
		{
			DRIVER_LOG_ERROR(("Ring URB has failing status: %08X", lastUrbStatus));
			Context->status = Irp->IoStatus.Status;
			Context->cancel = true;
		}
		if (!Irp->Cancel)
		{
			if (!NT_SUCCESS(Irp->IoStatus.Status))
			{
				Context->cancel = true;
				Context->status = Irp->IoStatus.Status;
			}
			else
			{
				ringPacket->Data = nullptr;
				if (GetAvailableRingTailPackets(Context->pRingTail, 21) >= 1)
				{
					if (ConsumeRingTailWritingPacket(Context->pRingTail, ringPacketData, 1) >= 0)
					{
						++Context->info;
					}
					else
					{
						Context->status = STATUS_INVALID_VARIANT;
						Context->cancel = true;
					}
				}
				else
				{
					DRIVER_LOG_ERROR(("GetRemainingRingTailPackets reported overflow!"));
					Context->pRingTail->m_bOverFlow = true;
					Context->status = STATUS_MARSHALL_OVERFLOW;
					Context->cancel = true;
				}
			}
		}
		else
		{
			Context->cancel = true;
			Context->status = Irp->IoStatus.Status;
		}
		if (Context->pRingTail->m_bStopTransfer)
		{
			Context->cancel = true;
		}
		else if (!Context->cancel && NT_SUCCESS(Irp->IoStatus.Status))
		{
			if (ConsumeRingTailPacket(Context->pRingTail, &ringPacketData, 1) < 0)
			{
				Context->pRingTail->m_bOverFlow = true;
				Context->status = STATUS_MARSHALL_OVERFLOW;
				DRIVER_LOG_ERROR(("Overflow when trying to consume ringtail packet!"));
				Context->cancel = true;
			}
			else
			{
				MmPrepareMdlForReuse(ringPacket->Mdl);
				IoBuildPartialMdl(Context->mainirp->MdlAddress, ringPacket->Mdl, ringPacketData, ringPacket->Length);

				if (!MmGetSystemAddressForMdlSafe(ringPacket->Mdl, NormalPagePriority))
				{
					DRIVER_LOG_ERROR(("Mapping pages for packet %u failed!", packetIndex));
					Context->status = STATUS_INSUFFICIENT_RESOURCES;
					Context->cancel = true;
				}
				else
				{
					ringPacket->Data = ringPacketData;
					ringPacket->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength = ringPacket->Length;
				}

				PIO_STACK_LOCATION irpStack = IoGetNextIrpStackLocation(Irp);
				irpStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
				irpStack->Parameters.Others.Argument1 = ringPacket->Urb;
				irpStack->Parameters.Others.Argument2 = (PVOID)packetIndex;
				irpStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
				IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)RingPacketComplete, Context, TRUE, TRUE, TRUE);
				IoCallDriver(pdx->LowerDeviceObject, Irp);
				if (GetAvailableRingTailPackets(Context->pRingTail, 43) >= Context->pRingTail->m_iMinimumPacketsForReady)
					KeSetEvent(Context->EventScanPacketReady, LOW_PRIORITY, FALSE);
			}
		}
	}

	if (!NT_SUCCESS(Context->status))
	{
		DRIVER_LOG_ERROR(("Potentially bad status: %08X", Context->status));
	}

	if (Context->cancel)
	{
		if (ringPacket->Urb)
			ExFreePool(ringPacket->Urb);
		if (ringPacket->Mdl)
			IoFreeMdl(ringPacket->Mdl);
		Context->RingPackets[packetIndex].cancel = false;
		if (!InterlockedDecrement(&Context->numpending))
		{
			if (InterlockedExchangePointer((volatile PVOID*)&Context->mainirp->CancelRoutine, 0))
				InterlockedDecrement(&Context->refcnt);
			Context->mainirp->IoStatus.Status = Context->status;
			IoCompleteRequest(Context->mainirp, 0);
		}
	}

	return STATUS_MORE_PROCESSING_REQUIRED;
}

ULONG __stdcall ReadRingPacket(PMDL SourceMdl, PRWCONTEXT ctx)
{
	NT_VERIFY(SourceMdl);
	if (SourceMdl->ByteCount < sizeof(RING_TAIL))
	{
		DRIVER_LOG_ERROR(("SourceMdl has fewer bytes (%u) than needed (%u)", SourceMdl->ByteCount, sizeof(RING_TAIL)));
		return 0;
	}

	PVOID virtualAddress = MmGetMdlVirtualAddress(SourceMdl);
	NT_VERIFY(virtualAddress);

	ctx->RingPacketMdl = IoAllocateMdl(virtualAddress, sizeof(RING_TAIL), 0, 0, nullptr);
	if (!ctx->RingPacketMdl)
	{
		DRIVER_LOG_ERROR(("IoAllocateMdl(%p, %u, ...) failed", virtualAddress, sizeof(RING_TAIL)));
		return 0;
	}

	IoBuildPartialMdl(SourceMdl, ctx->RingPacketMdl, virtualAddress, sizeof(RING_TAIL));

	ctx->pRingTail = (PRING_TAIL)MmGetSystemAddressForMdlSafe(ctx->RingPacketMdl, NormalPagePriority);
	if (ctx->pRingTail)
	{
		ULONG headerSize = ctx->pRingTail->m_iHeaderSize;
		if (headerSize == sizeof(RING_TAIL))
		{
			KIRQL oldIrql = KeGetCurrentIrql();
			KeLowerIrql(PASSIVE_LEVEL);
			NTSTATUS status = ObReferenceObjectByHandle(
				reinterpret_cast<HANDLE>(ctx->pRingTail->HANDLE_EventScanPacketReady),
				EVENT_MODIFY_STATE,
				*ExEventObjectType,
				ctx->RequestorMode,
				(PVOID*)&ctx->EventScanPacketReady,
				nullptr);
			KfRaiseIrql(oldIrql);
			if (NT_SUCCESS(status))
			{
				ctx->pRingTail->m_bTransferInProgress = true;
				return ctx->pRingTail->m_iPacketSize;
			}

			DRIVER_LOG_ERROR(("ObReferenceObjectByHandle failed with status %08X", status));
		}
		else
		{
			DRIVER_LOG_ERROR(("Ringtail has weird headerSize (%u) - we expected it to be %u", ctx->pRingTail->m_iHeaderSize, sizeof(RING_TAIL)));
		}
	}

	IoFreeMdl(ctx->RingPacketMdl);
	ctx->RingPacketMdl = nullptr;
	return 0;
}

void __stdcall StartIo(PDEVICE_OBJECT fdo, PIRP Irp)
{
	PUCHAR packetVirtualAddress;
	PDEVICE_EXTENSION DeviceExtension;
	NTSTATUS status;

	DeviceExtension = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)AllDone, nullptr, TRUE, TRUE, TRUE);

	status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
	if (!NT_SUCCESS(status))
	{
		CompleteRequest(Irp, status, 0);
		return;
	}

	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
	BOOLEAN read = irpStack->MajorFunction == IRP_MJ_READ;
	USBD_PIPE_HANDLE hpipe = read ? DeviceExtension->RingReadPipe : DeviceExtension->RingWritePipe;
	if (!hpipe)
	{
		DRIVER_LOG_ERROR(("No usable pipe handle for %s request", irpStack->MajorFunction == IRP_MJ_READ ? "READ" : "WRITE"));
		status = STATUS_NO_SUCH_DEVICE;
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}
	if (InterlockedExchange(&DeviceExtension->numfailed, 0))
	{
		KIRQL NewIrql = KeGetCurrentIrql();
		KeLowerIrql(PASSIVE_LEVEL);
		status = ResetPipe(fdo, hpipe);
		if (!NT_SUCCESS(status))
		{
			DRIVER_LOG_ERROR(("Resetting pipe failed with status %08X", status));
			status = ResetDevice(fdo);
			if (!NT_SUCCESS(status))
			{
				DRIVER_LOG_ERROR(("Resetting port failed with status %08X. Aborting!", status));
				AbortPipe(fdo, hpipe);
				KfRaiseIrql(NewIrql);
				StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
				IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
				CompleteRequest(Irp, status, 0);
				return;
			}
		}
		KfRaiseIrql(NewIrql);
	}
	size_t tmpctxsize = RW_CONTEXT_SIZE(0);
	PRWCONTEXT tmpctx = (PRWCONTEXT)ALLOC_NON_PAGED(tmpctxsize);
	if (!tmpctx)
	{
		DRIVER_LOG_ERROR(("Failed to allocate temporary context!"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}

	tmpctx->info = 0;
	tmpctx->cancel = false;
	tmpctx->status = 0;
	tmpctx->fdo = fdo;
	tmpctx->hpipe = hpipe;
	tmpctx->mainirp = Irp;
	tmpctx->refcnt = 2;
	tmpctx->RequestorMode = Irp->RequestorMode;
	if (!ReadRingPacket(Irp->MdlAddress, tmpctx))
	{
		DRIVER_LOG_ERROR(("Failed to read ring buffer packet!"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		ExFreePool(tmpctx);
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}

	ULONG numSimultaneousPackets = tmpctx->pRingTail->m_iNumSimultaneousPackets;
	SIZE_T ctxsize = RW_CONTEXT_SIZE(numSimultaneousPackets);

	if (tmpctxsize > ctxsize)
	{
		DRIVER_LOG_ERROR(("Temporary context size %zu exceeds permanent context size %zu", tmpctxsize, ctxsize));
		status = STATUS_INSUFFICIENT_RESOURCES;
		ReleaseContextResources(tmpctx);
		ExFreePool(tmpctx);
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}

	tmpctx->numpending = tmpctx->numirps = numSimultaneousPackets;
	PRWCONTEXT ctx = (PRWCONTEXT)ALLOC_NON_PAGED(ctxsize);
	if (!ctx)
	{
		DRIVER_LOG_ERROR(("Failed to allocate context!"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		ReleaseContextResources(tmpctx);
		ExFreePool(tmpctx);
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}

	IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)AllDone, ctx, TRUE, TRUE, TRUE);

	memcpy(ctx, tmpctx, tmpctxsize);
	Irp->Tail.Overlay.DriverContext[0] = ctx;
	if (ctx->numirps)
	{
		ULONG packetIndex;

		for (packetIndex = 0; packetIndex < ctx->numirps; packetIndex++)
		{
			if (ConsumeRingTailPacket(ctx->pRingTail, &packetVirtualAddress, 1) < 0)
			{
				DRIVER_LOG_ERROR(("Consuming packet %u from ringtail failed", packetIndex));
				break;
			}
			if (!packetVirtualAddress)
			{
				DRIVER_LOG_ERROR(("NULL VA for packet %u", packetIndex));
				break;
			}
			PIRP pPacketIrp = IoAllocateIrp((CCHAR)(DeviceExtension->LowerDeviceObject->StackSize + 1), FALSE);
			if (!pPacketIrp)
			{
				DRIVER_LOG_ERROR(("Allocating IRP for packet %u failed", packetIndex));
				break;
			}
			
			ULONG iPacketSize = ctx->pRingTail->m_iPacketSize;
			if (iPacketSize == 0 || iPacketSize > 0x5000)
			{
				DRIVER_LOG_ERROR(("Ringtail packet size is invalid: %d", iPacketSize));
				break;
			}

			PURB urb = (PURB)ALLOC_NON_PAGED(sizeof(_URB_BULK_OR_INTERRUPT_TRANSFER));
			if (!urb)
			{
				break;
			}

			PMDL mdl = IoAllocateMdl(packetVirtualAddress, iPacketSize, 0, 0, 0);
			if (!mdl)
			{
				break;
			}

			IoBuildPartialMdl(Irp->MdlAddress, mdl, packetVirtualAddress, iPacketSize);

			if (!MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority))
			{
				DRIVER_LOG_ERROR(("MmGetSystemAddressForMdlSafe failed for packet %u", packetIndex));
				break;
			}

			ULONG urbFlags = USBD_SHORT_TRANSFER_OK;

			if (read)
			{
				urbFlags |= USBD_TRANSFER_DIRECTION_IN;
			}

			UsbBuildInterruptOrBulkTransferRequest(urb,
				sizeof(_URB_BULK_OR_INTERRUPT_TRANSFER),
				hpipe,
				NULL,
				mdl,
				iPacketSize,
				urbFlags,
				NULL);

			ctx->RingPackets[packetIndex].Irp = pPacketIrp;
			ctx->RingPackets[packetIndex].Data = packetVirtualAddress;
			ctx->RingPackets[packetIndex].Length = iPacketSize;
			ctx->RingPackets[packetIndex].Mdl = mdl;
			ctx->RingPackets[packetIndex].Urb = urb;

			IoSetNextIrpStackLocation(pPacketIrp);
			PIO_STACK_LOCATION packetIrpStack = IoGetCurrentIrpStackLocation(pPacketIrp);
			pPacketIrp->Tail.Overlay.DriverContext[0] = ctx;
			packetIrpStack->DeviceObject = fdo;
			packetIrpStack->Parameters.Others.Argument1 = ctx->RingPackets[packetIndex].Urb;
			packetIrpStack->Parameters.Others.Argument2 = (PVOID)packetIndex;
			PIO_STACK_LOCATION packetIrpNextStack = IoGetNextIrpStackLocation(pPacketIrp);
			packetIrpNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
			packetIrpNextStack->Parameters.Others.Argument1 = ctx->RingPackets[packetIndex].Urb;
			packetIrpNextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
			IoSetCompletionRoutine(pPacketIrp, (PIO_COMPLETION_ROUTINE)RingPacketComplete, ctx, TRUE, TRUE, TRUE);
		}

		if (packetIndex < ctx->numirps)
		{
			DRIVER_LOG_ERROR(("Failed to process all packets - made it to index %u out of %u packets", packetIndex, ctx->numirps));

			status = STATUS_INSUFFICIENT_RESOURCES;

			// Run clean-up on every packet up to the one we failed on
			for (ULONG i = 0; i <= packetIndex; i++)
			{
				PRING_PACKET prp = &ctx->RingPackets[i];
				if (prp->Urb)
				{
					ExFreePool(prp->Urb);
				}
				if (prp->Mdl)
				{
					IoFreeMdl(prp->Mdl);
				}
				if (prp->Irp)
				{
					IoFreeIrp(prp->Irp);
				}
			}

			ExFreePool(ctx);
			ReleaseContextResources(tmpctx);
			ExFreePool(tmpctx);
			StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
			IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
			CompleteRequest(Irp, status, 0);
			return;
		}
	}
	InterlockedExchangePointer((volatile PVOID*)&Irp->CancelRoutine, (PVOID)OnCancelReadWrite);
	if (Irp->Cancel)
	{
		status = STATUS_CANCELLED;
		if (InterlockedExchangePointer((volatile PVOID*)&Irp->CancelRoutine, nullptr))
			--ctx->refcnt;
	}
	else
	{
		status = STATUS_SUCCESS;
	}
	IoSetNextIrpStackLocation(Irp);
	if (!NT_SUCCESS(status))
	{
		for (ULONG i = 0; i < ctx->numirps; i++)
		{
			PRING_PACKET prp = &ctx->RingPackets[i];
			if (prp->Urb)
			{
				ExFreePool(prp->Urb);
			}
			if (prp->Mdl)
			{
				IoFreeMdl(prp->Mdl);
			}
			if (prp->Irp)
			{
				IoFreeIrp(prp->Irp);
			}
		}

		ExFreePool(ctx);
		ReleaseContextResources(tmpctx);
		ExFreePool(tmpctx);
		StartNextPacket(&DeviceExtension->dqReadWrite, fdo);
		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
		CompleteRequest(Irp, status, 0);
		return;
	}
	for (ULONG i = 0; i < numSimultaneousPackets; i++)
	{
		ctx->RingPackets[i].cancel = true;
		IoCallDriver(DeviceExtension->LowerDeviceObject, ctx->RingPackets[i].Irp);
	}
	ctx->status = status;
	ExFreePool(tmpctx);
}

void __stdcall OnCancelReadWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PRWCONTEXT ctx;
	PDEVICE_EXTENSION pdx;
	ULONG i;
	KIRQL oldIrql;
	PDEVICE_OBJECT fdo;

	(void)DeviceObject;
	IoReleaseCancelSpinLock(Irp->CancelIrql);
	ctx = (PRWCONTEXT)Irp->Tail.Overlay.DriverContext[0];
	if (!ctx)
	{
		CompleteRequest(Irp, STATUS_CANCELLED, 0);
		return;
	}

	fdo = ctx->fdo;
	pdx = (PDEVICE_EXTENSION)fdo->DeviceExtension;
	ctx->cancel = true;
	for (i = 0; i < ctx->numirps; ++i)
	{
		if (ctx->RingPackets[i].cancel)
			IoCancelIrp(ctx->RingPackets[i].Irp);
	}
	oldIrql = KeGetCurrentIrql();
	KeLowerIrql(PASSIVE_LEVEL);
	AbortPipe(fdo, ctx->hpipe);
	KfRaiseIrql(oldIrql);
	if (DestroyContextStructure(ctx))
	{
		if (Irp->Tail.Overlay.ListEntry.Flink && Irp->Tail.Overlay.ListEntry.Blink)
		{
			KeAcquireSpinLock(&pdx->dqReadWrite.lock, &oldIrql);
			RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
			KeReleaseSpinLock(&pdx->dqReadWrite.lock, oldIrql);
		}
		IoReleaseRemoveLock(&pdx->RemoveLock, Irp);
		Irp->Tail.Overlay.DriverContext[0] = nullptr;
		StartNextPacket(&pdx->dqReadWrite, fdo);
		CompleteRequest(Irp, STATUS_CANCELLED, 0);
	}
}
