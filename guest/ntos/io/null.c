#include "../ntos.h"

static NTSTATUS NTAPI null_request(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    switch (stack->MajorFunction) {
    case IRP_MJ_CREATE:
        if (stack->Parameters.Create.Options & FILE_DIRECTORY_FILE)
            status = STATUS_NOT_A_DIRECTORY;
        else if (stack->FileObject->FileName.Length)
            status = STATUS_OBJECT_NAME_NOT_FOUND;
        else
            information = FILE_OPENED;
        break;
    case IRP_MJ_READ:
        if (stack->Parameters.Read.Length)
            status = STATUS_END_OF_FILE;
        break;
    case IRP_MJ_WRITE:
        information = stack->Parameters.Write.Length;
        break;
    case IRP_MJ_CLEANUP:
    case IRP_MJ_CLOSE:
    case IRP_MJ_FLUSH_BUFFERS:
        break;
    case IRP_MJ_QUERY_INFORMATION:
        if (stack->Parameters.QueryFile.FileInformationClass == FileStandardInformation) {
            FILE_STANDARD_INFORMATION *info = irp->AssociatedIrp.SystemBuffer;
            if (stack->Parameters.QueryFile.Length < sizeof(*info)) {
                status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            memset(info, 0, sizeof(*info));
            info->NumberOfLinks = 1;
            information = sizeof(*info);
        } else {
            status = STATUS_INVALID_PARAMETER;
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS NTAPI NullDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = null_request;
    driver->MajorFunction[IRP_MJ_READ] = null_request;
    driver->MajorFunction[IRP_MJ_WRITE] = null_request;
    driver->MajorFunction[IRP_MJ_CLEANUP] = null_request;
    driver->MajorFunction[IRP_MJ_CLOSE] = null_request;
    driver->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = null_request;
    driver->MajorFunction[IRP_MJ_QUERY_INFORMATION] = null_request;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\Null");
    PDEVICE_OBJECT device;
    NTSTATUS status = IoCreateDevice(driver, 0, &name, FILE_DEVICE_NULL, 0, FALSE, &device);
    if (NT_SUCCESS(status))
        device->Flags &= ~DO_DEVICE_INITIALIZING;
    return status;
}

void IopInitializeNull(void)
{
    NTSTATUS status = IoCreateDriver(L"\\Driver\\Null", NullDriverEntry, nullptr);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("null device failed to start: %08x", status);
        return;
    }
    ObCreateSymbolicLink(L"\\GLOBAL??\\NUL", L"\\Device\\Null");
}
