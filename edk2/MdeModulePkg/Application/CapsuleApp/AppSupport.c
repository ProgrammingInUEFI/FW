/** @file
  A shell application that triggers capsule update process.

  Copyright (c) 2016 - 2017, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/ShellParameters.h>
#include <Guid/FileInfo.h>
#include <Guid/Gpt.h>

#define IS_HYPHEN(a)               ((a) == L'-')
#define IS_NULL(a)                 ((a) == L'\0')

#define MAX_ARG_NUM     11

UINTN  Argc;
CHAR16 **Argv;

/**

  This function parse application ARG.

  @return Status
**/
EFI_STATUS
GetArg (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID**)&ShellParameters
                  );
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Argc = ShellParameters->Argc;
  Argv = ShellParameters->Argv;
  return EFI_SUCCESS;
}

/**
  Return File System Volume containing this shell application.

  @return File System Volume containing this shell application.
**/
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *
GetMyVol (
  VOID
  )
{
  EFI_STATUS                        Status;
  EFI_LOADED_IMAGE_PROTOCOL         *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Vol;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Vol
                  );
  if (!EFI_ERROR (Status)) {
    return Vol;
  }

  return NULL;
}

/**
  Read a file from this volume.

  @param[in]  Vol             File System Volume
  @param[in]  FileName        The file to be read.
  @param[out] BufferSize      The file buffer size
  @param[out] Buffer          The file buffer

  @retval EFI_SUCCESS    Read file successfully
  @retval EFI_NOT_FOUND  File not found
**/
EFI_STATUS
ReadFileFromVol (
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Vol,
  IN  CHAR16                            *FileName,
  OUT UINTN                             *BufferSize,
  OUT VOID                              **Buffer
  )
{
  EFI_STATUS                        Status;
  EFI_FILE_HANDLE                   RootDir;
  EFI_FILE_HANDLE                   Handle;
  UINTN                             FileInfoSize;
  EFI_FILE_INFO                     *FileInfo;
  UINTN                             TempBufferSize;
  VOID                              *TempBuffer;

  //
  // Open the root directory
  //
  Status = Vol->OpenVolume (Vol, &RootDir);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Open the file
  //
  Status = RootDir->Open (
                      RootDir,
                      &Handle,
                      FileName,
                      EFI_FILE_MODE_READ,
                      0
                      );
  if (EFI_ERROR (Status)) {
    RootDir->Close (RootDir);
    return Status;
  }

  RootDir->Close (RootDir);

  //
  // Get the file information
  //
  FileInfoSize = sizeof(EFI_FILE_INFO) + 1024;

  FileInfo = AllocateZeroPool (FileInfoSize);
  if (FileInfo == NULL) {
    Handle->Close (Handle);
    return Status;
  }

  Status = Handle->GetInfo (
                     Handle,
                     &gEfiFileInfoGuid,
                     &FileInfoSize,
                     FileInfo
                     );
  if (EFI_ERROR (Status)) {
    Handle->Close (Handle);
    gBS->FreePool (FileInfo);
    return Status;
  }

  //
  // Allocate buffer for the file data. The last CHAR16 is for L'\0'
  //
  TempBufferSize = (UINTN) FileInfo->FileSize + sizeof(CHAR16);
  TempBuffer = AllocateZeroPool (TempBufferSize);
  if (TempBuffer == NULL) {
    Handle->Close (Handle);
    gBS->FreePool (FileInfo);
    return Status;
  }

  gBS->FreePool (FileInfo);

  //
  // Read the file data to the buffer
  //
  Status = Handle->Read (
                     Handle,
                     &TempBufferSize,
                     TempBuffer
                     );
  if (EFI_ERROR (Status)) {
    Handle->Close (Handle);
    gBS->FreePool (TempBuffer);
    return Status;
  }

  Handle->Close (Handle);

  *BufferSize = TempBufferSize;
  *Buffer     = TempBuffer;
  return EFI_SUCCESS;
}

/**
  Read a file.
  If ScanFs is FLASE, it will use this Vol as default Fs.
  If ScanFs is TRUE, it will scan all FS and check the file.
    If there is only one file match the name, it will be read.
    If there is more than one file match the name, it will return Error.

  @param[in,out]  ThisVol         File System Volume
  @param[in]      FileName        The file to be read.
  @param[out]     BufferSize      The file buffer size
  @param[out]     Buffer          The file buffer
  @param[in]      ScanFs          Need Scan all FS

  @retval EFI_SUCCESS    Read file successfully
  @retval EFI_NOT_FOUND  File not found
  @retval EFI_NO_MAPPING There is duplicated files found
**/
EFI_STATUS
ReadFileToBufferEx (
  IN OUT EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   **ThisVol,
  IN  CHAR16                               *FileName,
  OUT UINTN                                *BufferSize,
  OUT VOID                                 **Buffer,
  IN  BOOLEAN                              ScanFs
  )
{
  EFI_STATUS                        Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Vol;
  UINTN                             TempBufferSize;
  VOID                              *TempBuffer;
  UINTN                             NoHandles;
  EFI_HANDLE                        *HandleBuffer;
  UINTN                             Index;

  //
  // Check parameters
  //
  if ((FileName == NULL) || (Buffer == NULL) || (ThisVol == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // not scan fs
  //
  if (!ScanFs) {
    if (*ThisVol == NULL) {
      *ThisVol = GetMyVol ();
      if (*ThisVol == NULL) {
        return EFI_INVALID_PARAMETER;
      }
    }
    //
    // Read file directly from Vol
    //
    return ReadFileFromVol (*ThisVol, FileName, BufferSize, Buffer);
  }

  //
  // need scan fs
  //

  //
  // Get all Vol handle
  //
  Status = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &NoHandles,
                   &HandleBuffer
                   );
  if (EFI_ERROR (Status) && (NoHandles == 0)) {
    return EFI_NOT_FOUND;
  }

  //
  // Walk through each Vol
  //
  *ThisVol = NULL;
  *BufferSize = 0;
  *Buffer     = NULL;
  for (Index = 0; Index < NoHandles; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&Vol
                    );
    if (EFI_ERROR(Status)) {
      continue;
    }

    Status = ReadFileFromVol (Vol, FileName, &TempBufferSize, &TempBuffer);
    if (!EFI_ERROR (Status)) {
      //
      // Read file OK, check duplication
      //
      if (*ThisVol != NULL) {
        //
        // Find the duplicated file
        //
        gBS->FreePool (TempBuffer);
        gBS->FreePool (*Buffer);
        Print (L"Duplicated FileName found!\n");
        return EFI_NO_MAPPING;
      } else {
        //
        // Record value
        //
        *ThisVol = Vol;
        *BufferSize = TempBufferSize;
        *Buffer     = TempBuffer;
      }
    }
  }

  //
  // Scan Fs done
  //
  if (*ThisVol == NULL) {
    return EFI_NOT_FOUND;
  }

  //
  // Done
  //
  return EFI_SUCCESS;
}

/**
  Read a file.

  @param[in]  FileName        The file to be read.
  @param[out] BufferSize      The file buffer size
  @param[out] Buffer          The file buffer

  @retval EFI_SUCCESS    Read file successfully
  @retval EFI_NOT_FOUND  File not found
**/
EFI_STATUS
ReadFileToBuffer (
  IN  CHAR16                               *FileName,
  OUT UINTN                                *BufferSize,
  OUT VOID                                 **Buffer
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Vol;
  Vol = NULL;
  return ReadFileToBufferEx(&Vol, FileName, BufferSize, Buffer, FALSE);
}

/**
  Write a file.

  @param[in] FileName        The file to be written.
  @param[in] BufferSize      The file buffer size
  @param[in] Buffer          The file buffer

  @retval EFI_SUCCESS    Write file successfully
**/
EFI_STATUS
WriteFileFromBuffer (
  IN  CHAR16                               *FileName,
  IN  UINTN                                BufferSize,
  IN  VOID                                 *Buffer
  )
{
  EFI_STATUS                        Status;
  EFI_FILE_HANDLE                   RootDir;
  EFI_FILE_HANDLE                   Handle;
  UINTN                             TempBufferSize;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Vol;

  Vol = GetMyVol();
  if (Vol == NULL) {
    return EFI_NOT_FOUND;
  }

  //
  // Open the root directory
  //
  Status = Vol->OpenVolume (Vol, &RootDir);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Open the file
  //
  Status = RootDir->Open (
                      RootDir,
                      &Handle,
                      FileName,
                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE| EFI_FILE_MODE_CREATE,
                      0
                      );
  if (EFI_ERROR (Status)) {
    RootDir->Close (RootDir);
    return Status;
  }

  //
  // Delete file
  //
  Status = Handle->Delete(Handle);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  //
  // Open the file again
  //
  Status = RootDir->Open (
                      RootDir,
                      &Handle,
                      FileName,
                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE| EFI_FILE_MODE_CREATE,
                      0
                      );
  if (EFI_ERROR (Status)) {
    RootDir->Close (RootDir);
    return Status;
  }

  RootDir->Close (RootDir);

  //
  // Write the file data from the buffer
  //
  TempBufferSize = BufferSize;
  Status = Handle->Write (
                     Handle,
                     &TempBufferSize,
                     Buffer
                     );
  if (EFI_ERROR (Status)) {
    Handle->Close (Handle);
    return Status;
  }

  Handle->Close (Handle);

  return EFI_SUCCESS;
}

