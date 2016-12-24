/** @file
  Replaces EFI_SIMPLE_FILE_SYSTEM_PROTOCOL on target volume and
  injects content of specified source folder on source (injection)
  volume into target folder in target volume.

  Copyright (C) 2012 Damir Mazar. All rights reserved.<BR>
**/

#ifndef FS_INJECT_DRV_H_
#define FS_INJECT_DRV_H_

EFI_STATUS
EFIAPI
FSInjectEntrypoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  );

#endif // FS_INJECT_DRV_H_
