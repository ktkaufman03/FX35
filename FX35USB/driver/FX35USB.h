#pragma once

#include <cstddef>

#include "driver.h"
#include "ringtail.h"

NTSTATUS __stdcall AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo);
void __stdcall RemoveDevice(PDEVICE_OBJECT fdo);
NTSTATUS __stdcall StartDevice(PDEVICE_OBJECT fdo, PCM_PARTIAL_RESOURCE_LIST raw, PCM_PARTIAL_RESOURCE_LIST translated);
void __stdcall StartIo(PDEVICE_OBJECT fdo, PIRP Irp);
void __stdcall StopDevice(PDEVICE_OBJECT fdo, BOOLEAN oktouch);
NTSTATUS __stdcall GetStringDescriptor(PDEVICE_OBJECT fdo, UCHAR Index, PUNICODE_STRING String);
