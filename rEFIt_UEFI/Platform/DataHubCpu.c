//
/// @file rEFIt_UEFI/Platform/DataHubCpu.c
///
/// VirtualBox CPU descriptors
///
/// VirtualBox CPU descriptors also used to set OS X-used NVRAM variables and DataHub data
///

// Copyright(C) 2009-2010 Oracle Corporation
//
// This file is part of VirtualBox Open Source Edition(OSE), as
// available from http://www.virtualbox.org. This file is free software;
// you can redistribute it and/or modify it under the terms of the GNU
// General Public License(GPL) as published by the Free Software
// Foundation, in version 2 as it comes in the "COPYING" file of the
// VirtualBox OSE distribution. VirtualBox OSE is distributed in the
// hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.

//
// CHANGELOG:
//
// 2014/11/20
// Download-Fritz
// Removed commented out code in rev 2963 and 2965
//

#ifndef DEBUG_ALL
#define DEBUG_DH 1
#else
#define DEBUG_DH DEBUG_ALL
#endif

#if DEBUG_DH == 0
#define DBG(...)
#else
#define DBG(...) DebugLog(DEBUG_DH, __VA_ARGS__)
#endif


#include "Platform.h"
//#include "Version.h"

#include <Guid/AppleSystemInfo.h>

#include <Protocol/DataHub.h>

#define EFI_CPU_DATA_MAXIMUM_LENGTH 0x100

// gDataHub
/// A pointer to the DataHubProtocol
EFI_DATA_HUB_PROTOCOL     *gDataHub;

EFI_SUBCLASS_TYPE1_HEADER mCpuDataRecordHeader = {
  EFI_PROCESSOR_SUBCLASS_VERSION,       // Version
  sizeof(EFI_SUBCLASS_TYPE1_HEADER),    // Header Size
  0,                                    // Instance (initialize later)
  EFI_SUBCLASS_INSTANCE_NON_APPLICABLE, // SubInstance
  0                                     // RecordType (initialize later)
};

extern APPLE_SMC_IO_PROTOCOL *gAppleSmc;

typedef union {
  EFI_CPU_DATA_RECORD *DataRecord;
  UINT8               *Raw;
} EFI_CPU_DATA_RECORD_BUFFER;

// PLATFORM_DATA
/// The struct passed to "LogDataHub" holing key and value to be added
#pragma pack(1)
typedef struct {
  EFI_SUBCLASS_TYPE1_HEADER Hdr;     /// 0x48
  UINT32                    NameLen; /// 0x58 (in bytes)
  UINT32                    ValLen;  /// 0x5c
  UINT8                     Data[1]; /// 0x60 Name Value
} PLATFORM_DATA;
#pragma pack()

// CopyRecord
/// Copy the data provided in arguments into a PLATFORM_DATA buffer
///
/// @param Rec    The buffer the data should be copied into
/// @param Name   The value for the member "name"
/// @param Val    The data the object should have
/// @param ValLen The length of the parameter "Val"
///
/// @return The size of the new PLATFORM_DATA object is returned
UINT32 EFIAPI
CopyRecord(IN        PLATFORM_DATA *Rec,
           IN  CONST CHAR16        *Name,
           IN        VOID          *Val,
           IN        UINT32        ValLen)
{
  CopyMem(&Rec->Hdr, &mCpuDataRecordHeader, sizeof(EFI_SUBCLASS_TYPE1_HEADER));
  Rec->NameLen = (UINT32)StrLen(Name) * sizeof(CHAR16);
  Rec->ValLen  = ValLen;
  CopyMem(Rec->Data,                Name, Rec->NameLen);
  CopyMem(Rec->Data + Rec->NameLen, Val,  ValLen);

  return (sizeof(EFI_SUBCLASS_TYPE1_HEADER) + 8 + Rec->NameLen + Rec->ValLen);
}

// LogDataHub
/// Adds a key-value-pair to the DataHubProtocol
EFI_STATUS EFIAPI
LogDataHub(IN  EFI_GUID *TypeGuid,
           IN  CHAR16   *Name,
           IN  VOID     *Data,
           IN  UINT32   DataSize)
{
  UINT32        RecordSize;
  EFI_STATUS    Status;
  PLATFORM_DATA *PlatformData;

  PlatformData = (PLATFORM_DATA*)AllocatePool(sizeof(PLATFORM_DATA) + DataSize + EFI_CPU_DATA_MAXIMUM_LENGTH);
  if (PlatformData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  RecordSize = CopyRecord(PlatformData, Name, Data, DataSize);
  Status     = gDataHub->LogData(gDataHub,
                                 TypeGuid,                          // DataRecordGuid
                                 &gAppleSystemInfoProducerNameGuid, // ProducerName (always)
                                 EFI_DATA_RECORD_CLASS_DATA,
                                 PlatformData,
                                 RecordSize);

  FreePool(PlatformData);
  return Status;
}

// SetVariablesForOSX
/// Sets the volatile and non-volatile variables used by OS X
EFI_STATUS EFIAPI
SetVariablesForOSX(LOADER_ENTRY *Entry)
{
  // The variable names used should be made global constants to prevent them being allocated multiple times

  UINT32  Attributes;
  UINT32  Color;
  CHAR8   *None;
  CHAR8   *NvidiaWebValue;

  CHAR16  *KbdPrevLang;
  UINTN   LangLen;
  CHAR8   *VariablePtr;
  VOID    *OldData;
  UINT64  os_version = AsciiOSVersionToUint64(Entry->OSVersion);

  //
  // firmware Variables
  //

  // As found on a real Mac, the system-id variable solely has the BS flag
  SetNvramVariable(L"system-id",
                   &gAppleVendorVariableGuid,
                   EFI_VARIABLE_BOOTSERVICE_ACCESS,
                   sizeof(gUuid),
                   &gUuid);

  Attributes     = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

  if (gSettings.RtMLB != NULL) {
    if (AsciiStrLen(gSettings.RtMLB) != 17) {
      DBG("** Warning: Your MLB is not suitable for iMessage(must be 17 chars long) !\n");
    }

    SetNvramVariable(L"MLB",
                     &gAppleVendorVariableGuid,
                     Attributes,
                     AsciiStrLen(gSettings.RtMLB),
                     gSettings.RtMLB);
  }

  if (gSettings.RtROM != NULL) {
    SetNvramVariable(L"ROM",
                     &gAppleVendorVariableGuid,
                     Attributes,
                     gSettings.RtROMLen,
                     gSettings.RtROM);
  }

  SetNvramVariable(L"FirmwareFeatures",
                   &gAppleVendorVariableGuid,
                   Attributes,
                   sizeof(gFwFeatures),
                   &gFwFeatures);

  // Download-Fritz: Should be added to SMBIOS or at least to some other config section
  AddNvramVariable(L"FirmwareFeaturesMask",
                   &gAppleVendorVariableGuid,
                   Attributes,
                   sizeof(gFwFeaturesMask),
                   &gFwFeaturesMask);

  // HW_MLB and HW_ROM are also around on some Macs with the same values as MLB and ROM
  AddNvramVariable(L"HW_BID", &gAppleVendorVariableGuid, Attributes, AsciiStrLen(gSettings.BoardNumber), gSettings.BoardNumber);


  //
  // OS X non-volatile Variables
  //

  // note: some gAppleBootVariableGuid vars present in nvram.plist are already set by PutNvramPlistToRtVars()
  // we should think how to handle those vars from nvram.plist and ones set here from gSettings

  if (!gFirmwareClover && !gDriversFlags.EmuVariableLoaded) {
    Attributes |= EFI_VARIABLE_NON_VOLATILE;
  } else {
    // using AddNvramVariable content instead of calling the function to do LangLen calculation only when necessary
    // Do not mess with prev-lang:kbd on UEFI systems without NVRAM emulation; it's OS X's business
    KbdPrevLang = L"prev-lang:kbd";
    OldData     = GetNvramVariable(KbdPrevLang, &gAppleBootVariableGuid, NULL, NULL);
    if (OldData == NULL) {
      LangLen     = 16;
      VariablePtr = &gSettings.Language[15];
      for (LangLen = 16; ((*VariablePtr == ' ') || (*VariablePtr == 0)) && (LangLen != 0); --LangLen) {
        --VariablePtr;
      }

      gRT->SetVariable(KbdPrevLang, &gAppleBootVariableGuid, Attributes, LangLen, &gSettings.Language);
    } else {
      FreePool(OldData);
    }
  }

  None           = "none";
  AddNvramVariable(L"security-mode", &gAppleBootVariableGuid, Attributes, 5, (VOID*)None);

  // we should have two UUID: platform and system
  // NO! Only Platform is the best solution
  if (!gSettings.InjectSystemID) {
    if (gSettings.SmUUIDConfig) {
      SetNvramVariable(L"platform-uuid", &gAppleBootVariableGuid, Attributes, 16, &gUuid);
    } else {
      AddNvramVariable(L"platform-uuid", &gAppleBootVariableGuid, Attributes, 16, &gUuid);
    }
  }

  // Download-Fritz: Do not mess with BacklightLevel; it's OS X's business
  if (gMobile) {
    if (gSettings.BacklightLevelConfig) {
      SetNvramVariable(L"backlight-level", &gAppleBootVariableGuid, Attributes, sizeof(gSettings.BacklightLevel), &gSettings.BacklightLevel);
    } else {
      AddNvramVariable(L"backlight-level", &gAppleBootVariableGuid, Attributes, sizeof(gSettings.BacklightLevel), &gSettings.BacklightLevel);
    }
  }

  if (gSettings.DefaultBackgroundColor == 0x80000000) {
    DeleteNvramVariable(L"DefaultBackgroundColor", &gAppleVendorVariableGuid);
  } else {
    UINT16 ActualDensity = 0xE1;
    UINT16 DensityThreshold = 0x96;
    UINT64 ConfigStatus = 0;
    Color = gSettings.DefaultBackgroundColor;
    AddNvramVariable(L"DefaultBackgroundColor", &gAppleVendorVariableGuid, Attributes, 4, &Color);
    // add some UI variables
    AddNvramVariable(L"ActualDensity", &gAppleBootVariableGuid, Attributes, 2, &ActualDensity);
    AddNvramVariable(L"DensityThreshold", &gAppleBootVariableGuid, Attributes, 2, &DensityThreshold);
    AddNvramVariable(L"gfx-saved-config-restore-status", &gAppleVendorVariableGuid, Attributes, 8, &ConfigStatus);
  }

  if (gSettings.UIScale == 0x80000000) {
    DeleteNvramVariable(L"UIScale", &gAppleVendorVariableGuid);
  } else {
    SetNvramVariable(L"UIScale", &gAppleVendorVariableGuid, Attributes, 1, &gSettings.UIScale);
  }

  if (gSettings.EFILoginHiDPI == 0x80000000) {
    DeleteNvramVariable(L"EFILoginHiDPI", &gAppleBootVariableGuid);
  } else {
    SetNvramVariable(L"EFILoginHiDPI", &gAppleBootVariableGuid, Attributes, 4, &gSettings.EFILoginHiDPI);
  }

  // ->GetVariable(flagstate, gAppleBootVariableGuid, 0/0, 20, 10FE110) = Not Found
  if (gSettings.flagstate[3] == 0x80) {
    DeleteNvramVariable(L"flagstate", &gAppleBootVariableGuid);
  } else {
    SetNvramVariable(L"flagstate", &gAppleBootVariableGuid, Attributes, 32, &gSettings.flagstate);
  }

  // Hack for recovery by Asgorath
  if (gSettings.CsrActiveConfig != 0xFFFF) {
    SetNvramVariable(L"csr-active-config", &gAppleBootVariableGuid, Attributes, sizeof(gSettings.CsrActiveConfig), &gSettings.CsrActiveConfig);
  }

  if (gSettings.BooterConfig != 0) {
    SetNvramVariable(L"bootercfg", &gAppleBootVariableGuid, Attributes, sizeof(gSettings.BooterConfig), &gSettings.BooterConfig);
  }

  if (gSettings.NvidiaWeb) {
    NvidiaWebValue = "1";
    SetNvramVariable(L"nvda_drv", &gAppleBootVariableGuid, Attributes, 2, (VOID*)NvidiaWebValue);
  }

   DeleteNvramVariable(L"recovery-boot-mode", &gAppleBootVariableGuid);

  // Check for AptioFix2Drv loaded to store efi-boot-device for special boot
    if (gDriversFlags.AptioFix2Loaded || gDriversFlags.AptioFixLoaded ||
        gDriversFlags.AptioFix3Loaded || gDriversFlags.AptioMemFixLoaded)  {
      EFI_STATUS          Status;
      REFIT_VOLUME *Volume = Entry->Volume;
      EFI_DEVICE_PATH_PROTOCOL    *DevicePath = Volume->DevicePath;
      // We need to remember from which device we boot, to make silence boot while special recovery boot
      Status = gRT->SetVariable(L"specialbootdevice", &gAppleBootVariableGuid,
                                EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                                GetDevicePathSize(DevicePath), (UINT8 *)DevicePath);
      if (EFI_ERROR(Status)) {
        DBG("can't set  specialbootdevice!\n");
      }
    }

  // Sherlocks: to fix "OSInstall.mpkg appears to be missing or damaged" in 10.13, we should remove this variables.
  if (Entry->LoaderType == OSTYPE_OSX_INSTALLER) {
    if (os_version > AsciiOSVersionToUint64("10.12")) {
      DeleteNvramVariable(L"install-product-url",  &gAppleBootVariableGuid);
      DeleteNvramVariable(L"previous-system-uuid", &gAppleBootVariableGuid);
    }
  }

  // SMC helper
  SetNvramVariable (
    L"fakesmc-key-RPlt-ch8*",
    &gAppleBootVariableGuid,
    Attributes,
    8,
    &gSettings.RPlt
    );

  SetNvramVariable (
    L"fakesmc-key-RBr-ch8*",
    &gAppleBootVariableGuid,
    Attributes,
    8,
    &gSettings.RBr
    );

  SetNvramVariable (
    L"fakesmc-key-EPCI-ui32",
    &gAppleBootVariableGuid,
    Attributes,
    4,
    &gSettings.EPCI
    );

  SetNvramVariable (
    L"fakesmc-key-REV-{rev",
    &gAppleBootVariableGuid,
    Attributes,
    6,
    &gSettings.REV
    );

  SetNvramVariable (
    L"fakesmc-key-BEMB-flag",
    &gAppleBootVariableGuid,
    Attributes,
    1,
    &gSettings.Mobile
    );

  return EFI_SUCCESS;
}

VOID
AddSMCkey(SMC_KEY Key, SMC_DATA_SIZE Size, SMC_KEY_TYPE Type, SMC_DATA *Data)
{
  if (gAppleSmc && (gAppleSmc->Revision == SMC_HELPER_SIGNATURE)) {
    gAppleSmc->SmcAddKey(gAppleSmc,     Key, Size, Type, 0xC0);
    gAppleSmc->SmcWriteValue(gAppleSmc, Key, Size, Data);
  }
}

// SetupDataForOSX
/// Sets the DataHub data used by OS X
VOID EFIAPI
SetupDataForOSX(BOOLEAN Hibernate)
{
  EFI_STATUS Status;

  UINT32     DevPathSupportedVal;
  UINT64     FrontSideBus;
  UINT64     CpuSpeed;
  UINT64     TscFrequency;
  UINT64     ARTFrequency;
  CHAR16*    ProductName;
  CHAR16*    SerialNumber;
  UINTN      Revision;
  UINT16     Zero = 0;

  Revision = StrDecimalToUintn(gFirmwareRevision);

  // fool proof
  FrontSideBus = gCPUStructure.FSBFrequency;
  if ((FrontSideBus < (50 * Mega)) || (FrontSideBus > (1000 * Mega))) {
    DBG("Wrong FrontSideBus=%d, set to 100MHz\n", FrontSideBus);
    FrontSideBus = 100 * Mega;
  }

  if (gSettings.QEMU) {
    FrontSideBus = gCPUStructure.TSCFrequency;
    switch (gCPUStructure.Model) {
      case CPU_MODEL_DOTHAN:
      case CPU_MODEL_YONAH:
      case CPU_MODEL_MEROM:
//      case CPU_MODEL_PENRYN:
        FrontSideBus = DivU64x32(FrontSideBus, 4);
        break;
      default:
        break;
    }
    DBG("Using QEMU FrontSideBus=%ull\n", FrontSideBus);
  }

  // Save values into gSettings for the genconfig aim
  gSettings.BusSpeed   = (UINT32)DivU64x32(FrontSideBus, kilo);

  CpuSpeed = gCPUStructure.CPUFrequency;
  gSettings.CpuFreqMHz = (UINT32)DivU64x32(CpuSpeed,     Mega);

  // Locate DataHub Protocol
  Status = gBS->LocateProtocol(&gEfiDataHubProtocolGuid, NULL, (VOID**)&gDataHub);
  if (!EFI_ERROR(Status)) {
    ProductName         = AllocateZeroPool(128);
    AsciiStrToUnicodeStrS(gSettings.ProductName, ProductName, 64);

    SerialNumber        = AllocateZeroPool(128);
    AsciiStrToUnicodeStrS(gSettings.SerialNr,    SerialNumber, 64);

    LogDataHub(&gEfiProcessorSubClassGuid, L"FSBFrequency",     &FrontSideBus,        sizeof(UINT64));

    if (gCPUStructure.ARTFrequency && gSettings.UseARTFreq) {
      ARTFrequency = gCPUStructure.ARTFrequency;
      LogDataHub(&gEfiProcessorSubClassGuid, L"ARTFrequency",   &ARTFrequency,        sizeof(UINT64));
    }

    TscFrequency        = gCPUStructure.TSCFrequency;
    LogDataHub(&gEfiProcessorSubClassGuid, L"InitialTSC",         &TscFrequency,        sizeof(UINT64));
    LogDataHub(&gEfiProcessorSubClassGuid, L"CPUFrequency",         &CpuSpeed,            sizeof(UINT64));

    DevPathSupportedVal = 1;
    LogDataHub(&gEfiMiscSubClassGuid,      L"DevicePathsSupported", &DevPathSupportedVal, sizeof(UINT32));
    LogDataHub(&gEfiMiscSubClassGuid,      L"Model",                ProductName,         (UINT32)StrSize(ProductName));
    LogDataHub(&gEfiMiscSubClassGuid,      L"SystemSerialNumber",   SerialNumber,        (UINT32)StrSize(SerialNumber));

    if (gSettings.InjectSystemID) {
      LogDataHub(&gEfiMiscSubClassGuid, L"system-id", &gUuid, sizeof(EFI_GUID));
    }

    LogDataHub(&gEfiProcessorSubClassGuid, L"clovergui-revision", &Revision, sizeof(UINT32));

    // collect info about real hardware
    LogDataHub(&gEfiMiscSubClassGuid, L"OEMVendor",  &gSettings.OEMVendor,  (UINT32)iStrLen(gSettings.OEMVendor,  64) + 1);
    LogDataHub(&gEfiMiscSubClassGuid, L"OEMProduct", &gSettings.OEMProduct, (UINT32)iStrLen(gSettings.OEMProduct, 64) + 1);
    LogDataHub(&gEfiMiscSubClassGuid, L"OEMBoard",   &gSettings.OEMBoard,   (UINT32)iStrLen(gSettings.OEMBoard,   64) + 1);

    // all current settings
    LogDataHub(&gEfiMiscSubClassGuid, L"Settings", &gSettings, sizeof(gSettings));
  }
  if (!gAppleSmc) {
    return;
  }
  AddSMCkey(SMC_MAKE_KEY('R','P','l','t'), 8, SmcKeyTypeCh8, (SMC_DATA *)&gSettings.RPlt);
  AddSMCkey(SMC_MAKE_KEY('R','B','r',' '), 8, SmcKeyTypeCh8, (SMC_DATA *)&gSettings.RBr);
  AddSMCkey(SMC_MAKE_KEY('E','P','C','I'), 4, SmcKeyTypeUint32, (SMC_DATA *)&gSettings.EPCI);
  AddSMCkey(SMC_MAKE_KEY('R','E','V',' '), 6, SmcKeyTypeCh8, (SMC_DATA *)&gSettings.REV);
  AddSMCkey(SMC_MAKE_KEY('B','E','M','B'), 1, SmcKeyTypeFlag, (SMC_DATA *)&gSettings.Mobile);
  //laptop battery keys will be better to import from nvram.plist or read from ACPI(?)
  //they are needed for FileVault2 who want to draw battery status
  AddSMCkey(SMC_MAKE_KEY('B','A','T','P'), 1, SmcKeyTypeFlag, (SMC_DATA *)&Zero); //isBatteryPowered
  AddSMCkey(SMC_MAKE_KEY('B','N','u','m'), 1, SmcKeyTypeUint8, (SMC_DATA *)&gSettings.Mobile); // Num Batteries
  if (gSettings.Mobile) {
    AddSMCkey(SMC_MAKE_KEY('B','B','I','N'), 1, SmcKeyTypeFlag, (SMC_DATA *)&gSettings.Mobile); //Battery inserted
  }
  AddSMCkey(SMC_MAKE_KEY('M','S','T','c'), 1, SmcKeyTypeUint8, (SMC_DATA *)&Zero); // CPU Plimit
  AddSMCkey(SMC_MAKE_KEY('M','S','A','c'), 2, SmcKeyTypeUint16, (SMC_DATA *)&Zero);// GPU Plimit
//  AddSMCkey(SMC_MAKE_KEY('M','S','L','D'), 1, SmcKeyTypeUint8, (SMC_DATA *)&Zero);   //isLidClosed
  Zero = Hibernate?((ResumeFromCoreStorage||GlobalConfig.HibernationFixup)?25:29):0;

  AddSMCkey(SMC_MAKE_KEY('M','S','W','r'), 1, SmcKeyTypeUint8, (SMC_DATA *)&Zero);
  Zero = 1;
  AddSMCkey(SMC_MAKE_KEY('M','S','F','W'), 2, SmcKeyTypeFlag, (SMC_DATA *)&Zero);
  Zero = 0x300;
  AddSMCkey(SMC_MAKE_KEY('M','S','P','S'), 2, SmcKeyTypeUint16, (SMC_DATA *)&Zero);
}
