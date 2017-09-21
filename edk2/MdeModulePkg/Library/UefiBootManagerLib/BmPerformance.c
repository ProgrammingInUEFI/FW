/** @file
  This file include the file which can help to get the system
  performance, all the function will only include if the performance
  switch is set.

Copyright (c) 2004 - 2015, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "InternalBm.h"

PERF_HEADER               mBmPerfHeader;
PERF_DATA                 mBmPerfData;
EFI_PHYSICAL_ADDRESS      mBmAcpiLowMemoryBase = 0x0FFFFFFFFULL;

/**
  Get the short verion of PDB file name to be
  used in performance data logging.

  @param PdbFileName     The long PDB file name.
  @param GaugeString     The output string to be logged by performance logger.
  @param StringSize      The buffer size of GaugeString in bytes.

**/
VOID
BmGetShortPdbFileName (
  IN  CONST CHAR8  *PdbFileName,
  OUT       CHAR8  *GaugeString,
  IN        UINTN   StringSize
  )
{
  UINTN Index;
  UINTN Index1;
  UINTN StartIndex;
  UINTN EndIndex;

  if (PdbFileName == NULL) {
    AsciiStrCpyS (GaugeString, StringSize, " ");
  } else {
    StartIndex = 0;
    for (EndIndex = 0; PdbFileName[EndIndex] != 0; EndIndex++)
      ;

    for (Index = 0; PdbFileName[Index] != 0; Index++) {
      if (PdbFileName[Index] == '\\') {
        StartIndex = Index + 1;
      }

      if (PdbFileName[Index] == '.') {
        EndIndex = Index;
      }
    }

    Index1 = 0;
    for (Index = StartIndex; Index < EndIndex; Index++) {
      GaugeString[Index1] = PdbFileName[Index];
      Index1++;
      if (Index1 == StringSize - 1) {
        break;
      }
    }

    GaugeString[Index1] = 0;
  }

  return ;
}

/**
  Get the name from the Driver handle, which can be a handle with
  EFI_LOADED_IMAGE_PROTOCOL or EFI_DRIVER_BINDING_PROTOCOL installed.
  This name can be used in performance data logging.

  @param Handle          Driver handle.
  @param GaugeString     The output string to be logged by performance logger.
  @param StringSize      The buffer size of GaugeString in bytes.

**/
VOID
BmGetNameFromHandle (
  IN  EFI_HANDLE     Handle,
  OUT CHAR8          *GaugeString,
  IN  UINTN          StringSize
  )
{
  EFI_STATUS                  Status;
  EFI_LOADED_IMAGE_PROTOCOL   *Image;
  CHAR8                       *PdbFileName;
  EFI_DRIVER_BINDING_PROTOCOL *DriverBinding;

  AsciiStrCpyS (GaugeString, StringSize, " ");

  //
  // Get handle name from image protocol
  //
  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **) &Image
                  );

  if (EFI_ERROR (Status)) {
    Status = gBS->OpenProtocol (
                    Handle,
                    &gEfiDriverBindingProtocolGuid,
                    (VOID **) &DriverBinding,
                    NULL,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      return ;
    }
    //
    // Get handle name from image protocol
    //
    Status = gBS->HandleProtocol (
                    DriverBinding->ImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **) &Image
                    );
  }

  PdbFileName = PeCoffLoaderGetPdbPointer (Image->ImageBase);

  if (PdbFileName != NULL) {
    BmGetShortPdbFileName (PdbFileName, GaugeString, StringSize);
  }

  return ;
}

/**

  Writes performance data of booting into the allocated memory.
  OS can process these records.

  @param  Event                 The triggered event.
  @param  Context               Context for this event.

**/
VOID
EFIAPI
BmWriteBootToOsPerformanceData (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                Status;
  UINT32                    LimitCount;
  EFI_HANDLE                *Handles;
  UINTN                     NoHandles;
  CHAR8                     GaugeString[PERF_TOKEN_SIZE];
  UINT8                     *Ptr;
  UINT32                    Index;
  UINT64                    Ticker;
  UINT64                    Freq;
  UINT32                    Duration;
  UINTN                     LogEntryKey;
  CONST VOID                *Handle;
  CONST CHAR8               *Token;
  CONST CHAR8               *Module;
  UINT64                    StartTicker;
  UINT64                    EndTicker;
  UINT64                    StartValue;
  UINT64                    EndValue;
  BOOLEAN                   CountUp;
  UINTN                     VarSize;
  BOOLEAN                   Found;

  //
  // Record the performance data for End of BDS
  //
  PERF_END(NULL, "BDS", NULL, 0);

  //
  // Retrieve time stamp count as early as possible
  //
  Ticker  = GetPerformanceCounter ();

  Freq    = GetPerformanceCounterProperties (&StartValue, &EndValue);
  
  Freq    = DivU64x32 (Freq, 1000);

  mBmPerfHeader.CpuFreq = Freq;

  //
  // Record BDS raw performance data
  //
  if (EndValue >= StartValue) {
    mBmPerfHeader.BDSRaw = Ticker - StartValue;
    CountUp            = TRUE;
  } else {
    mBmPerfHeader.BDSRaw = StartValue - Ticker;
    CountUp            = FALSE;
  }

  //
  // Reset the entry count
  //
  mBmPerfHeader.Count = 0;

  if (mBmAcpiLowMemoryBase == 0x0FFFFFFFF) {
    VarSize = sizeof (EFI_PHYSICAL_ADDRESS);
    Status = gRT->GetVariable (
                    L"PerfDataMemAddr",
                    &gPerformanceProtocolGuid,
                    NULL,
                    &VarSize,
                    &mBmAcpiLowMemoryBase
                    );
    if (EFI_ERROR (Status)) {
      //
      // Fail to get the variable, return.
      //
      return;
    }
  }

  //
  // Put Detailed performance data into memory
  //
  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  AllHandles,
                  NULL,
                  NULL,
                  &NoHandles,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return ;
  }

  Ptr        = (UINT8 *) ((UINT32) mBmAcpiLowMemoryBase + sizeof (PERF_HEADER));
  LimitCount = (UINT32) (PERF_DATA_MAX_LENGTH - sizeof (PERF_HEADER)) / sizeof (PERF_DATA);

  //
  // Get performance data
  //
  LogEntryKey = 0;
  while ((LogEntryKey = GetPerformanceMeasurement (
                          LogEntryKey,
                          &Handle,
                          &Token,
                          &Module,
                          &StartTicker,
                          &EndTicker)) != 0) {
    if (EndTicker != 0) {
      if (StartTicker == 1) {
        StartTicker = StartValue;
      }
      if (EndTicker == 1) {
        EndTicker = StartValue;
      }
      Ticker = CountUp ? (EndTicker - StartTicker) : (StartTicker - EndTicker);

      Duration = (UINT32) DivU64x32 (Ticker, (UINT32) Freq);
      if (Duration == 0) {
        continue;
      }

      ZeroMem (&mBmPerfData, sizeof (PERF_DATA));

      mBmPerfData.Duration = Duration;

      //
      // See if the Handle is in the handle buffer
      //
      Found = FALSE;
      for (Index = 0; Index < NoHandles; Index++) {
        if (Handle == Handles[Index]) {
          BmGetNameFromHandle (Handles[Index], GaugeString, PERF_TOKEN_SIZE);
          AsciiStrCpyS (mBmPerfData.Token, PERF_TOKEN_SIZE, GaugeString);
          Found = TRUE;
          break;
        }
      }

      if (!Found) {
        AsciiStrnCpyS (mBmPerfData.Token, PERF_TOKEN_SIZE, Token, PERF_TOKEN_LENGTH);
      }

      CopyMem (Ptr, &mBmPerfData, sizeof (PERF_DATA));
      Ptr += sizeof (PERF_DATA);

      mBmPerfHeader.Count++;
      if (mBmPerfHeader.Count == LimitCount) {
        goto Done;
      }
    }
  }

Done:

  FreePool (Handles);

  mBmPerfHeader.Signiture = PERFORMANCE_SIGNATURE;

  //
  // Put performance data to Reserved memory
  //
  CopyMem (
    (UINTN *) (UINTN) mBmAcpiLowMemoryBase,
    &mBmPerfHeader,
    sizeof (PERF_HEADER)
    );

  return ;
}
