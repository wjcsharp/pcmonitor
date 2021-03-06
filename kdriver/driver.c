#include <h/drvioctl.h>
#include <inc/drvmain.h>
#include <inc/klogger.h>
#include <inc/mwsk.h>
#include <inc/monitor.h>
#include <inc/keybrd.h>


NTSTATUS DriverEntry(PDRIVER_OBJECT  DriverObject,
                 PUNICODE_STRING RegistryPath);

#define __SUBCOMPONENT__ "drventry"

typedef struct _MDEVICE_EXTENSION
{
    PDEVICE_OBJECT	DeviceObject;
    UNICODE_STRING	SymLinkName;
} MDEVICE_EXTENSION, *PMDEVICE_EXTENSION;

NTSTATUS 
    CompleteIrp( PIRP Irp, NTSTATUS status, ULONG info)
{
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp,IO_NO_INCREMENT);
    return status;
}

VOID UnloadRoutine(IN PDRIVER_OBJECT pDriverObject)
{
    PDEVICE_OBJECT	pNextDevObj;
    int i;

    KLog(LInfo, "UnloadRoutine");

	MonitorStop(NULL);

	pNextDevObj = pDriverObject->DeviceObject;

    for(i = 0; pNextDevObj != NULL; i++)
    {
        PMDEVICE_EXTENSION DevExt = (PMDEVICE_EXTENSION)pNextDevObj->DeviceExtension;
        PDEVICE_OBJECT DevObj = pNextDevObj;
        pNextDevObj = pNextDevObj->NextDevice;
        if (DevExt != NULL) {
            KLog(LInfo, "Deleted device ext %p device %p", DevExt, DevExt->DeviceObject);
            KLog(LInfo, "Deleted symlink = %wZ", &DevExt->SymLinkName);
            IoDeleteSymbolicLink(&DevExt->SymLinkName);
        }

        IoDeleteDevice(DevObj);
    }

	KLog(LInfo, "UnloadRoutine completed");
    KLoggingRelease();
}


NTSTATUS DriverDeviceControlHandler( IN PDEVICE_OBJECT fdo, IN PIRP Irp )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG ControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode;
    ULONG method = ControlCode & 0x3;
    ULONG ResultLength = 0;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;

	KeEnterGuardedRegion();

    KLOG(LInfo, "IoControl fdo %p, ioctl %x", fdo, ControlCode);
	if (OutputLength < InputLength) {
		KLog(LError, "invalid outputlen=%x vs inputlen=%x", OutputLength, InputLength);
		Status = STATUS_INVALID_PARAMETER;
		goto complete;
	}
	
	ResultLength = InputLength;

    switch( ControlCode) {
        case IOCTL_KMON_INIT:
            {   
				PKMON_INIT initData = (PKMON_INIT)Buffer;
				if (InputLength < sizeof(KMON_INIT)) {
					Status = STATUS_BUFFER_TOO_SMALL;
					goto complete;
				}

                KLog(LInfo, "IOCTL_KMON_INIT");
				Status = MonitorStart(initData);
                break;
            }
        case IOCTL_KMON_RELEASE:
            {
				PKMON_RELEASE releaseData = (PKMON_RELEASE)Buffer;
				if (InputLength < sizeof(KMON_RELEASE)) {
					Status = STATUS_BUFFER_TOO_SMALL;
					goto complete;
				}
                KLog(LInfo, "IOCTL_KMON_RELEASE");
				Status = MonitorStop(releaseData);
                break;
            }
		case IOCTL_KMON_OPEN_WINSTA:
			{
				POPEN_WINSTA openWinsta = (POPEN_WINSTA)Buffer;
				if (InputLength < sizeof(OPEN_WINSTA)) {
					Status = STATUS_BUFFER_TOO_SMALL;
					goto complete;
				}
				Status = MonitorOpenWinsta(openWinsta);
				break;
			}
		case IOCTL_KMON_OPEN_DESKTOP:
		{
				POPEN_DESKTOP openDeskop = (POPEN_DESKTOP)Buffer;
				if (InputLength < sizeof(OPEN_DESKTOP)) {
					Status = STATUS_BUFFER_TOO_SMALL;
					goto complete;
				}
				Status = MonitorOpenDesktop(openDeskop);
				break;
		}
		case IOCTL_KMON_SCREENSHOT:
		{
				PKMON_SCREENSHOT screenShot = (PKMON_SCREENSHOT)Buffer;
				if (InputLength < sizeof(KMON_SCREENSHOT)) {
					Status = STATUS_BUFFER_TOO_SMALL;
					goto complete;
				}
				Status = MonitorScreenshot(screenShot);
				break;
		}
        default: Status = STATUS_INVALID_DEVICE_REQUEST;
    }


complete:
	KeLeaveGuardedRegion();
	KLog(LInfo, "dev=%p IoControl: %x bytes: %x, Status=%x", fdo, ControlCode, ResultLength, Status);

    return CompleteIrp(Irp, Status, ResultLength);
}


NTSTATUS
    DriverIrpHandler(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    NTSTATUS Status;
    BOOLEAN bHandled = FALSE;
    PIO_STACK_LOCATION currentIrpStack = IoGetCurrentIrpStackLocation(Irp);
    
    KLOG(LInfo, "DevObj %p Major %x Minor %x", DeviceObject, currentIrpStack->MajorFunction, currentIrpStack->MinorFunction);
	Status = KbdDispatchGeneral(&MonitorGetInstance()->Kbd, DeviceObject, Irp, &bHandled);
    if (bHandled)
        return Status;

    if (currentIrpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL) {
        return DriverDeviceControlHandler(DeviceObject, Irp);
    } else {
        return CompleteIrp(Irp, STATUS_SUCCESS, 0); 
    }
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
					 IN PUNICODE_STRING RegistryPath
					)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PDEVICE_OBJECT  DeviceObject = NULL;
    UNICODE_STRING  DeviceName;

    DPRINT("KMON In DriverEntry\n");
    DPRINT("KMON RegistryPath = %ws\n", RegistryPath->Buffer);

#ifdef USE_DRIVER_UNLOAD
    DriverObject->DriverUnload = UnloadRoutine;
#else
	DriverObject->DriverUnload = NULL;
#endif

    DriverObject->MajorFunction[IRP_MJ_CREATE]= DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_READ]  = DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]= DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]  = DriverIrpHandler;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DriverIrpHandler;
	DriverObject->MajorFunction[IRP_MJ_POWER] = DriverIrpHandler;

    RtlInitUnicodeString( &DeviceName, KMON_NT_DEVICE_NAME_W);

    Status = KLoggingInit();
    if (!NT_SUCCESS(Status)) {
        DPRINT("KLoggingInit failure\n");
        return Status;
    }

	KLog(LInfo, "DriverEntry drvObj=%p, regPath=%wZ", DriverObject, RegistryPath);

    Status = IoCreateDevice(DriverObject,
        sizeof(MDEVICE_EXTENSION),
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE, 
        &DeviceObject);

    if(!NT_SUCCESS(Status)) {
        KLoggingRelease();
        return Status;
    }

    PMDEVICE_EXTENSION DevExt = (PMDEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DevExt->DeviceObject = DeviceObject;  // Сохраняем обратный указатель

    KLog(LInfo, "created Device %p, DevExt=%p", DeviceObject, DevExt);

    RtlInitUnicodeString(&DevExt->SymLinkName, KMON_DOS_DEVICE_NAME_W );

    Status = IoCreateSymbolicLink( &DevExt->SymLinkName, &DeviceName );
    if (!NT_SUCCESS(Status)) { 
        IoDeleteDevice( DeviceObject );
        KLoggingRelease();
        return Status;
    } 

	MonitorInit(DriverObject);

    KLog(LInfo, "DriverEntry successfully completed");

    return Status;
}
