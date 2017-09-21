/** @file
  SMM Memory pool management functions.

  Copyright (c) 2009 - 2016, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials are licensed and made available 
  under the terms and conditions of the BSD License which accompanies this 
  distribution.  The full text of the license may be found at        
  http://opensource.org/licenses/bsd-license.php                                            

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

**/

#include "PiSmmCore.h"

LIST_ENTRY  mSmmPoolLists[SmmPoolTypeMax][MAX_POOL_INDEX];
//
// To cache the SMRAM base since when Loading modules At fixed address feature is enabled, 
// all module is assigned an offset relative the SMRAM base in build time.
//
GLOBAL_REMOVE_IF_UNREFERENCED  EFI_PHYSICAL_ADDRESS       gLoadModuleAtFixAddressSmramBase = 0;

/**
  Convert a UEFI memory type to SMM pool type.

  @param[in]  MemoryType              Type of pool to allocate.

  @return SMM pool type
**/
SMM_POOL_TYPE
UefiMemoryTypeToSmmPoolType (
  IN  EFI_MEMORY_TYPE   MemoryType
  )
{
  ASSERT ((MemoryType == EfiRuntimeServicesCode) || (MemoryType == EfiRuntimeServicesData));
  switch (MemoryType) {
  case EfiRuntimeServicesCode:
    return SmmPoolTypeCode;
  case EfiRuntimeServicesData:
    return SmmPoolTypeData;
  default:
    return SmmPoolTypeMax;
  }
}


/**
  Called to initialize the memory service.

  @param   SmramRangeCount       Number of SMRAM Regions
  @param   SmramRanges           Pointer to SMRAM Descriptors

**/
VOID
SmmInitializeMemoryServices (
  IN UINTN                 SmramRangeCount,
  IN EFI_SMRAM_DESCRIPTOR  *SmramRanges
  )
{
  UINTN                  Index;
  EFI_STATUS             Status;
  UINTN                  SmmPoolTypeIndex;
  EFI_LOAD_FIXED_ADDRESS_CONFIGURATION_TABLE *LMFAConfigurationTable;

  //
  // Initialize Pool list
  //
  for (SmmPoolTypeIndex = 0; SmmPoolTypeIndex < SmmPoolTypeMax; SmmPoolTypeIndex++) {
    for (Index = 0; Index < ARRAY_SIZE (mSmmPoolLists[SmmPoolTypeIndex]); Index++) {
      InitializeListHead (&mSmmPoolLists[SmmPoolTypeIndex][Index]);
    }
  }

  Status = EfiGetSystemConfigurationTable (
            &gLoadFixedAddressConfigurationTableGuid,
           (VOID **) &LMFAConfigurationTable
           );
  if (!EFI_ERROR (Status) && LMFAConfigurationTable != NULL) {
    gLoadModuleAtFixAddressSmramBase = LMFAConfigurationTable->SmramBase;
  }

  //
  // Add Free SMRAM regions
  // Need add Free memory at first, to let gSmmMemoryMap record data
  //
  for (Index = 0; Index < SmramRangeCount; Index++) {
    if ((SmramRanges[Index].RegionState & (EFI_ALLOCATED | EFI_NEEDS_TESTING | EFI_NEEDS_ECC_INITIALIZATION)) != 0) {
      continue;
    }
    SmmAddMemoryRegion (
      SmramRanges[Index].CpuStart,
      SmramRanges[Index].PhysicalSize,
      EfiConventionalMemory,
      SmramRanges[Index].RegionState
      );
  }

  //
  // Add the allocated SMRAM regions
  //
  for (Index = 0; Index < SmramRangeCount; Index++) {
    if ((SmramRanges[Index].RegionState & (EFI_ALLOCATED | EFI_NEEDS_TESTING | EFI_NEEDS_ECC_INITIALIZATION)) == 0) {
      continue;
    }
    SmmAddMemoryRegion (
      SmramRanges[Index].CpuStart,
      SmramRanges[Index].PhysicalSize,
      EfiConventionalMemory,
      SmramRanges[Index].RegionState
      );
  }

}

/**
  Internal Function. Allocate a pool by specified PoolIndex.

  @param  PoolType              Type of pool to allocate.
  @param  PoolIndex             Index which indicate the Pool size.
  @param  FreePoolHdr           The returned Free pool.

  @retval EFI_OUT_OF_RESOURCES   Allocation failed.
  @retval EFI_SUCCESS            Pool successfully allocated.

**/
EFI_STATUS
InternalAllocPoolByIndex (
  IN  EFI_MEMORY_TYPE   PoolType,
  IN  UINTN             PoolIndex,
  OUT FREE_POOL_HEADER  **FreePoolHdr
  )
{
  EFI_STATUS            Status;
  FREE_POOL_HEADER      *Hdr;
  EFI_PHYSICAL_ADDRESS  Address;
  SMM_POOL_TYPE         SmmPoolType;

  SmmPoolType = UefiMemoryTypeToSmmPoolType(PoolType);

  ASSERT (PoolIndex <= MAX_POOL_INDEX);
  Status = EFI_SUCCESS;
  Hdr = NULL;
  if (PoolIndex == MAX_POOL_INDEX) {
    Status = SmmInternalAllocatePages (AllocateAnyPages, PoolType, EFI_SIZE_TO_PAGES (MAX_POOL_SIZE << 1), &Address);
    if (EFI_ERROR (Status)) {
      return EFI_OUT_OF_RESOURCES;
    }
    Hdr = (FREE_POOL_HEADER *) (UINTN) Address;
  } else if (!IsListEmpty (&mSmmPoolLists[SmmPoolType][PoolIndex])) {
    Hdr = BASE_CR (GetFirstNode (&mSmmPoolLists[SmmPoolType][PoolIndex]), FREE_POOL_HEADER, Link);
    RemoveEntryList (&Hdr->Link);
  } else {
    Status = InternalAllocPoolByIndex (PoolType, PoolIndex + 1, &Hdr);
    if (!EFI_ERROR (Status)) {
      Hdr->Header.Size >>= 1;
      Hdr->Header.Available = TRUE;
      Hdr->Header.Type = PoolType;
      InsertHeadList (&mSmmPoolLists[SmmPoolType][PoolIndex], &Hdr->Link);
      Hdr = (FREE_POOL_HEADER*)((UINT8*)Hdr + Hdr->Header.Size);
    }
  }

  if (!EFI_ERROR (Status)) {
    Hdr->Header.Size = MIN_POOL_SIZE << PoolIndex;
    Hdr->Header.Available = FALSE;
    Hdr->Header.Type = PoolType;
  }

  *FreePoolHdr = Hdr;
  return Status;
}

/**
  Internal Function. Free a pool by specified PoolIndex.

  @param  FreePoolHdr           The pool to free.

  @retval EFI_SUCCESS           Pool successfully freed.

**/
EFI_STATUS
InternalFreePoolByIndex (
  IN FREE_POOL_HEADER  *FreePoolHdr
  )
{
  UINTN                 PoolIndex;
  SMM_POOL_TYPE         SmmPoolType;

  ASSERT ((FreePoolHdr->Header.Size & (FreePoolHdr->Header.Size - 1)) == 0);
  ASSERT (((UINTN)FreePoolHdr & (FreePoolHdr->Header.Size - 1)) == 0);
  ASSERT (FreePoolHdr->Header.Size >= MIN_POOL_SIZE);

  SmmPoolType = UefiMemoryTypeToSmmPoolType(FreePoolHdr->Header.Type);

  PoolIndex = (UINTN) (HighBitSet32 ((UINT32)FreePoolHdr->Header.Size) - MIN_POOL_SHIFT);
  FreePoolHdr->Header.Available = TRUE;
  ASSERT (PoolIndex < MAX_POOL_INDEX);
  InsertHeadList (&mSmmPoolLists[SmmPoolType][PoolIndex], &FreePoolHdr->Link);
  return EFI_SUCCESS;
}

/**
  Allocate pool of a particular type.

  @param  PoolType               Type of pool to allocate.
  @param  Size                   The amount of pool to allocate.
  @param  Buffer                 The address to return a pointer to the allocated
                                 pool.

  @retval EFI_INVALID_PARAMETER  PoolType not valid.
  @retval EFI_OUT_OF_RESOURCES   Size exceeds max pool size or allocation failed.
  @retval EFI_SUCCESS            Pool successfully allocated.

**/
EFI_STATUS
EFIAPI
SmmInternalAllocatePool (
  IN   EFI_MEMORY_TYPE  PoolType,
  IN   UINTN            Size,
  OUT  VOID             **Buffer
  )
{
  POOL_HEADER           *PoolHdr;
  FREE_POOL_HEADER      *FreePoolHdr;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Address;
  UINTN                 PoolIndex;

  if (PoolType != EfiRuntimeServicesCode &&
      PoolType != EfiRuntimeServicesData) {
    return EFI_INVALID_PARAMETER;
  }

  Size += sizeof (*PoolHdr);
  if (Size > MAX_POOL_SIZE) {
    Size = EFI_SIZE_TO_PAGES (Size);
    Status = SmmInternalAllocatePages (AllocateAnyPages, PoolType, Size, &Address);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    PoolHdr = (POOL_HEADER*)(UINTN)Address;
    PoolHdr->Size = EFI_PAGES_TO_SIZE (Size);
    PoolHdr->Available = FALSE;
    PoolHdr->Type = PoolType;
    *Buffer = PoolHdr + 1;
    return Status;
  }

  Size = (Size + MIN_POOL_SIZE - 1) >> MIN_POOL_SHIFT;
  PoolIndex = (UINTN) HighBitSet32 ((UINT32)Size);
  if ((Size & (Size - 1)) != 0) {
    PoolIndex++;
  }

  Status = InternalAllocPoolByIndex (PoolType, PoolIndex, &FreePoolHdr);
  if (!EFI_ERROR(Status)) {
    *Buffer = &FreePoolHdr->Header + 1;
  }
  return Status;
}

/**
  Allocate pool of a particular type.

  @param  PoolType               Type of pool to allocate.
  @param  Size                   The amount of pool to allocate.
  @param  Buffer                 The address to return a pointer to the allocated
                                 pool.

  @retval EFI_INVALID_PARAMETER  PoolType not valid.
  @retval EFI_OUT_OF_RESOURCES   Size exceeds max pool size or allocation failed.
  @retval EFI_SUCCESS            Pool successfully allocated.

**/
EFI_STATUS
EFIAPI
SmmAllocatePool (
  IN   EFI_MEMORY_TYPE  PoolType,
  IN   UINTN            Size,
  OUT  VOID             **Buffer
  )
{
  EFI_STATUS  Status;

  Status = SmmInternalAllocatePool (PoolType, Size, Buffer);
  if (!EFI_ERROR (Status)) {
    SmmCoreUpdateProfile (
      (EFI_PHYSICAL_ADDRESS) (UINTN) RETURN_ADDRESS (0),
      MemoryProfileActionAllocatePool,
      PoolType,
      Size,
      *Buffer,
      NULL
      );
  }
  return Status;
}

/**
  Frees pool.

  @param  Buffer                 The allocated pool entry to free.

  @retval EFI_INVALID_PARAMETER  Buffer is not a valid value.
  @retval EFI_SUCCESS            Pool successfully freed.

**/
EFI_STATUS
EFIAPI
SmmInternalFreePool (
  IN VOID  *Buffer
  )
{
  FREE_POOL_HEADER  *FreePoolHdr;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FreePoolHdr = (FREE_POOL_HEADER*)((POOL_HEADER*)Buffer - 1);
  ASSERT (!FreePoolHdr->Header.Available);

  if (FreePoolHdr->Header.Size > MAX_POOL_SIZE) {
    ASSERT (((UINTN)FreePoolHdr & EFI_PAGE_MASK) == 0);
    ASSERT ((FreePoolHdr->Header.Size & EFI_PAGE_MASK) == 0);
    return SmmInternalFreePages (
             (EFI_PHYSICAL_ADDRESS)(UINTN)FreePoolHdr,
             EFI_SIZE_TO_PAGES (FreePoolHdr->Header.Size)
             );
  }
  return InternalFreePoolByIndex (FreePoolHdr);
}

/**
  Frees pool.

  @param  Buffer                 The allocated pool entry to free.

  @retval EFI_INVALID_PARAMETER  Buffer is not a valid value.
  @retval EFI_SUCCESS            Pool successfully freed.

**/
EFI_STATUS
EFIAPI
SmmFreePool (
  IN VOID  *Buffer
  )
{
  EFI_STATUS  Status;

  Status = SmmInternalFreePool (Buffer);
  if (!EFI_ERROR (Status)) {
    SmmCoreUpdateProfile (
      (EFI_PHYSICAL_ADDRESS) (UINTN) RETURN_ADDRESS (0),
      MemoryProfileActionFreePool,
      EfiMaxMemoryType,
      0,
      Buffer,
      NULL
      );
  }
  return Status;
}
