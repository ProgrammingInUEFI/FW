
#include <Uefi.h>
#include <PiPei.h>

//#include <Library/HandleParsingLib.h>
//#include <Library/ShellCommandLib.h>
#include "DumpHex1.h"

#include <Library/SortLib.h>
#include <Library/BaseLib.h>
#include <Library/FileHandleLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h> 
#include <Library/DebugLib.h>
#include <Library/GenericBdsLib.h> 
//#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#include <Protocol/LoadedImage.h>
#include <Library/MemoryAllocationLib.h>
/*
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Protocol/ShellParameters.h>
#include <Guid/FileInfo.h>
#include <Guid/Gpt.h>

*/
//#include <Library/ShellLib.h>
//#include <Library/ShellCEntryLib.h>
//#include <Library/BcfgCommandLib.h>
EFI_GUID LoadedImageProtocol      = LOADED_IMAGE_PROTOCOL;
EFI_GUID DevicePathProtocol       = DEVICE_PATH_PROTOCOL;
EFI_GUID FileSystemProtocol	      = SIMPLE_FILE_SYSTEM_PROTOCOL;
EFI_GUID BlockIoProtocol 		  = BLOCK_IO_PROTOCOL;

EFI_STATUS
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{

EFI_DEVICE_PATH		    	*DevicePath;
EFI_LOADED_IMAGE   			*LoadedImage;
EFI_FILE_IO_INTERFACE 		*Vol;
EFI_FILE_HANDLE 			RootFs;
EFI_FILE_HANDLE 			CurDir;
EFI_FILE_HANDLE 			FileHandle;
EFI_HANDLE 					HandleBuffer;
EFI_BLOCK_IO 				*BlkIo;
EFI_BLOCK_IO_MEDIA 			*Media;

CHAR16 FileName[100];
UINTN i;
UINTN Size;

VOID *OsKernelBuffer;
UINTN NumberOfHandles;

UINT8 *Block;
UINT32 MediaId;

gBS->HandleProtocol(
		ImageHandle,
		&LoadedImageProtocol,
		&LoadedImage );


/*
if(DevicePathProtocol > 0)		
	Print (L"DevicePathProtocol : %X\n", &DevicePathProtocol);
else
{
	Print (L"DevicePathProtocol is null\n");
	return EFI_OUT_OF_RESOURCES;
}

if(DevicePath)		
	Print (L"DevicePath : %X\n", &DevicePath);
else
{
	Print (L"DevicePath is null\n");
	return EFI_OUT_OF_RESOURCES;
}
	*/	
gBS->HandleProtocol(
		LoadedImage->DeviceHandle,
		&DevicePathProtocol,
		&DevicePath
);
Print (	L"Image device : %s\n", DevicePathToStr (DevicePath));
Print (L"Image file : %s\n", DevicePathToStr (LoadedImage->FilePath));
Print (L"Image Base : %X\n", LoadedImage->ImageBase);
Print (L"Image Size : %X\n", LoadedImage->ImageSize);
gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
				  &FileSystemProtocol,
                  (VOID **)&Vol
                  );	 

Vol->OpenVolume (
		Vol,
		&RootFs
);

CurDir = RootFs;

StrCpy(FileName,DevicePathToStr(LoadedImage->FilePath));
for(i=StrLen(FileName);i>=0 && FileName[i]!='\\';i--);
FileName[i] = 0;
StrCat(FileName,L"\\OSKERNEL.BIN");
CurDir->Open (
					CurDir, 
					&FileHandle, 
					FileName,
					EFI_FILE_MODE_READ, 
					0
					);
Size = 0x00100000;
gBS->AllocatePool(
					EfiLoaderData, 
					Size,
					&OsKernelBuffer
);
FileHandle->Read(
					FileHandle, 
					&Size,
					OsKernelBuffer
);
FileHandle->Close(FileHandle);

NumberOfHandles = 0;
HandleBuffer = NULL;
gBS->LocateHandle(
				ByProtocol, 
				&BlockIoProtocol, 
				NULL,
				&NumberOfHandles, 
				&HandleBuffer
		);
for(i=0;i<NumberOfHandles;i++) {
	Print (L"Iteration %d of %d \n", i, NumberOfHandles);	
	if(LoadedImage != NULL)		
		Print (L"LoadedImage->DeviceHandle address : %X\n", LoadedImage->DeviceHandle);
	else
	{
		Print (L"LoadedImage->DeviceHandle is null\n");
		return EFI_OUT_OF_RESOURCES;
	}
	
	if(HandleBuffer != NULL)		
		Print (L"HandleBuffer address : %X\n", &HandleBuffer);
	else
	{
		Print (L"HandleBuffer address is null\n");
		return EFI_OUT_OF_RESOURCES;
	}	
	Print (L"Getting DevicePath...\n");
	if(LoadedImage != NULL)		
		Print (L"LoadedImage->DeviceHandle address : %X\n", LoadedImage->DeviceHandle);
else
{ 
	Print (L"LoadedImage->DeviceHandle is null\n");
	return EFI_OUT_OF_RESOURCES;
}
	gBS->HandleProtocol (
				HandleBuffer[i], 
				&DevicePathProtocol, 
				&DevicePath
			);
	
	Print (L"Getting BlkIO...\n");
	gBS->HandleProtocol (
				&HandleBuffer[i], 
				&BlockIoProtocol, 
				&BlkIo
				); 
    Media = BlkIo->Media;
	Block = AllocatePool(Media->BlockSize);
	MediaId = Media->MediaId;
	BlkIo->ReadBlocks(
				BlkIo, 
				MediaId, 
				(EFI_LBA)0, 
				Media->BlockSize, 
				Block
			);

	Print(L"\nBlock #0 of device %s\n",DevicePathToStr(DevicePath));
	
	//DumpHex(0, 0, Media->BlockSize, Block);
}
return EFI_SUCCESS;
//
// Exit the application.
//
}