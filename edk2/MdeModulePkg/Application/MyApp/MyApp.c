#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>


EFI_STATUS
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
	UINTN Index;

//
// Send a message to the ConsoleOut device.
//
SystemTable->ConOut->OutputString (
				SystemTable->ConOut, 
				L"Hello application started\n\r"
			);
//
// Wait for the user to press a key.
//
SystemTable->ConOut->OutputString (
				SystemTable->ConOut, 
				L"\n\r\n\r\n\rHit any key to exit\n\r"
			);
SystemTable->BootServices->WaitForEvent (
				1, 
				&(SystemTable->ConIn->WaitForKey),
				&Index
			);
SystemTable->ConOut->OutputString (
				SystemTable->ConOut,
				L"\n\r\n\r"
			);
return EFI_SUCCESS;
//
// Exit the application.
//
}