#include <wdm.h>

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
	PUNICODE_STRING RegistryPath)
{							// DriverEntry
	(void)DriverObject;
	(void)RegistryPath;
	DbgPrint("Hello from F135Test!\n");

	return STATUS_SUCCESS;
}							// DriverEntry