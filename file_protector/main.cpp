#include <fltKernel.h>

PFLT_FILTER g_filter_handle;
UNICODE_STRING g_target_file_name = RTL_CONSTANT_STRING(L"c:\\test.txt");
const char* g_trust_proc = "notepad.exe";

extern "C" UCHAR* PsGetProcessImageFileName(IN PEPROCESS Process);

NTSTATUS
DriverUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags) {
  UNREFERENCED_PARAMETER(Flags);
  FltUnregisterFilter(g_filter_handle);
  KdPrint(("MiniFilter Driver Unloaded\n"));
  return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS
PreCreateCallback(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, _Flt_CompletionContext_Outptr_ PVOID* CompletionContext) {
  UNREFERENCED_PARAMETER(CompletionContext);
  UNREFERENCED_PARAMETER(FltObjects);

  PFLT_FILE_NAME_INFORMATION file_name_info{nullptr};
  PWCHAR dos_full_path_buffer{nullptr};
  UNICODE_STRING volume_dev_name{0};

  bool need_block{false};

  do {
    if (!FltObjects->Volume) {
      break;
    }

    if (Data->RequestorMode == KernelMode) {
      break;
    }

    NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, &file_name_info);
    if (!NT_SUCCESS(status)) {
      break;
    }

    if (status = FltParseFileNameInformation(file_name_info); !NT_SUCCESS(status)) {
      break;
    }

    if (!NT_SUCCESS(IoVolumeDeviceToDosName(FltObjects->FileObject->DeviceObject, &volume_dev_name))) {
      break;
    }

    UNICODE_STRING dos_full_path;
    dos_full_path_buffer = static_cast<PWCHAR>(ExAllocatePool(NonPagedPool, 1024));
    if (!dos_full_path_buffer) {
      break;
    }
    RtlInitEmptyUnicodeString(&dos_full_path, dos_full_path_buffer, 1024);

    if (!NT_SUCCESS(RtlAppendUnicodeStringToString(&dos_full_path, &volume_dev_name)) ||
        !NT_SUCCESS(RtlAppendUnicodeStringToString(&dos_full_path, &file_name_info->ParentDir)) ||
        !NT_SUCCESS(RtlAppendUnicodeStringToString(&dos_full_path, &file_name_info->FinalComponent))) {
      break;
    }

    if (RtlCompareUnicodeString(&dos_full_path, &g_target_file_name, TRUE) == 0) {
      const char* proc_name = (const char*)PsGetProcessImageFileName(PsGetCurrentProcess());
      if (!proc_name) {
        break;
      }

      if (_stricmp(proc_name, g_trust_proc) != 0) {
        need_block = true;
      }
    }

  } while (false);

  if (volume_dev_name.Buffer) {
    ExFreePool(volume_dev_name.Buffer);
  }

  if (file_name_info) {
    FltReleaseFileNameInformation(file_name_info);
  }

  if (dos_full_path_buffer) {
    ExFreePool(dos_full_path_buffer);
  }

  if (need_block) {
    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
    Data->IoStatus.Information = 0;
    return FLT_PREOP_COMPLETE;
  }

  return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

const FLT_OPERATION_REGISTRATION Callbacks[] = {{IRP_MJ_CREATE, 0, PreCreateCallback, NULL}, {IRP_MJ_OPERATION_END}};

const FLT_REGISTRATION FilterRegistration = {
    sizeof(FLT_REGISTRATION),  // Size
    FLT_REGISTRATION_VERSION,  // Version
    0,                         // Flags
    NULL,                      // Context
    Callbacks,                 // Operation callbacks
    DriverUnload,              // MiniFilterUnload
    NULL,                      // InstanceSetup
    NULL,                      // InstanceQueryTeardown
    NULL,                      // InstanceTeardownStart
    NULL,                      // InstanceTeardownComplete
    NULL,                      // GenerateFileName
    NULL,                      // NormalizeNameComponent
    NULL                       // NormalizeContextCleanup
};

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
  UNREFERENCED_PARAMETER(RegistryPath);
  NTSTATUS status = FltRegisterFilter(DriverObject, &FilterRegistration, &g_filter_handle);
  if (NT_SUCCESS(status)) {
    status = FltStartFiltering(g_filter_handle);
    if (!NT_SUCCESS(status)) {
      FltUnregisterFilter(g_filter_handle);
    }
  }
  return status;
}
