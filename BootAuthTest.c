
#if _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "BootAuth.h"
#include "cfg.h"
#include <Base.h>
#include <Library/BaseMemoryLib.h>
#include <Efil.h>
#include <Nvme.h>
#include <NvmExpressPassthru.h>
#include <Efiltrace.h>

extern CrlStatus CRL_API OnStartup(int argc,
                                   char* argv[]);

extern void CRL_API OnShutdown(int exitCode);
extern int CRL_API OnLoadImage(int exitCode);



static struct
{    
    CrlStringBuffer MainScriptFileLocation;
    CrlLogListenerHandle LogListener;
    ScriptVirtualMachine* Vm;
    SrcContext* Context;
    SrcSource* Som;
    SrcSource* DMSource;
    SrcSource* UISource;
    SrcLoop* Loop;   
    char** Argv;
    int Argc;    
} s_runtimeData;

static void CRL_API Cleanup()
{
    if (s_runtimeData.DMSource != NULL)
    {
        DmRemoveDmSource(s_runtimeData.DMSource);
        SrcDestroySource(s_runtimeData.DMSource);
        s_runtimeData.DMSource = NULL;
    }

    if (s_runtimeData.UISource != NULL)
    {
        UIRemoveUISource(s_runtimeData.UISource);
        SrcDestroySource(s_runtimeData.UISource);
        s_runtimeData.UISource = NULL;
    }

    if (s_runtimeData.Loop != NULL)
    {
        SrcDestroyLoop(s_runtimeData.Loop);
        s_runtimeData.Loop = NULL;
    }

    if (s_runtimeData.Context != NULL)
    {
        SrcDestroyContext(s_runtimeData.Context);
        s_runtimeData.Context = NULL;
    }

    if (s_runtimeData.Vm != NULL)
    {
        ScriptDestroyVirtualMachine(s_runtimeData.Vm);
        s_runtimeData.Vm = NULL;
    }

    if (s_runtimeData.LogListener != NULL)
    {
        CrlUnregisterErrorLogListener(s_runtimeData.LogListener);
        s_runtimeData.LogListener = NULL;
    }

    if (s_runtimeData.Som != NULL)
    {
        SrcDestroySource(s_runtimeData.Som);
        s_runtimeData.Som = NULL;
    }

    CfgUninitialize();

    SbDestroy(s_runtimeData.MainScriptFileLocation);
    s_runtimeData.MainScriptFileLocation = NULL;
}

static int PrintMessageCleanupAndWait(char const* format,
                                      ...)
{
    if (format != NULL && *format != 0)
    {
        va_list ap;
        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);
    }

    Cleanup();

    return 1;
}

static void CRL_API PanicCallback(ScriptVirtualMachine* vm,
                                  CrlUserData userData)
{
    CRL_UNUSED_ARGUMENT(userData);
    CRL_UNUSED_ARGUMENT(vm);

    CRL_SET_UNDOCUMENTED_LOG_EVENT("Panic");
    CrlLogEvent(CrlLogSeverityFatal);

    exit(PrintMessageCleanupAndWait(NULL));
}

static void CRL_API StartupFuse(CrlUserData userData)
{
    CrlStatus status = OnStartup(s_runtimeData.Argc, s_runtimeData.Argv);

    CRL_UNUSED_ARGUMENT(userData);

    if (CRL_FAILED(status))
    {
        CRL_ADD_LOG_BACKTRACE(NULL);
        CrlLogEvent(CrlLogSeverityFatal);
        PostQuit(1);
    } 

    if (CRL_FAILED(ScriptExecuteFile(s_runtimeData.Vm, SbToAscii(s_runtimeData.MainScriptFileLocation))))
    {
        CRL_ADD_LOG_BACKTRACE(NULL);
        CrlLogEvent(CrlLogSeverityFatal);
        PostQuit(1);
    } 
}

static int CRL_API ApplicationQuit(SrcSource* som)
{    
    lua_State* luaState = SomGetLuaState(som);
    SrcQuitLoop(s_runtimeData.Loop, luaL_optint(luaState, 1, 0));
    return 0;
}

/*--- Public internal -------------------------------------------------------*/

ScriptVirtualMachine* CRL_API GetVirtualMachine()
{
    return s_runtimeData.Vm;
}

SrcSource* CRL_API GetSomSource()
{
    return UIGetSomSource(s_runtimeData.UISource);
}

SrcSource* CRL_API GetUISource()
{
    return s_runtimeData.UISource;
}

SrcContext* CRL_API GetContext()
{
    return s_runtimeData.Context;
}

void PostQuit(int exitCode)
{
    if (s_runtimeData.Loop != NULL)
    {
        SrcQuitLoop(s_runtimeData.Loop, exitCode);
    }
    else
    {
        exit(exitCode);
    }
}

static DmDeviceDriverDescriptor s_dmDrivers[] = 
{   
    {&SedAddSedDmDriver, &SedRemoveSedDmDriver, CRL_MKUD(NULL)},
    {&ScrdAddScrdDmDriver, &ScrdRemoveScrdDmDriver, CRL_MKUD(NULL)},
};

//#define EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID \
//  { \
//    0x52c78312, 0x8edc, 0x4233, { 0x98, 0xf2, 0x1a, 0x1a, 0xa5, 0xe3, 0x88, 0xa5 } \
//  }

EFI_STATUS get_nvme_passthru();

int main(int argc,
         char* argv[])
{    
	int exitCode;
	int i = 0;
	


	//    CrlStatus status;
	//CrlVariant mainScript = VAR_STATIC_INITIALIZE(CRL_VT_NIL, 0);
	ScriptPackage systemInfoPackage = {"SystemInfo",&SystemInfoRegisterPackage, 0 };

#if _WIN32
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	s_runtimeData.Argc = argc;
	s_runtimeData.Argv = argv;

	s_runtimeData.LogListener = CrlRegisterLogListener(&CrlConsoleLogListener, 0);
	PrintMessageCleanupAndWait("Hello World!\n");
	get_nvme_passthru();
	scanf("%d",&i);
	if (s_runtimeData.LogListener == NULL)
	{
		return PrintMessageCleanupAndWait("Failed to register console log listener\nPress any key to continue");
	}

	if (CRL_FAILED(CfgInitialize(APP_CONFIG_NAMESPACE, &systemInfoPackage, 1)))
	{
		CRL_ADD_LOG_BACKTRACE(NULL);
		CrlLogEvent(CrlLogSeverityFatal);
		return PrintMessageCleanupAndWait(NULL);
	}

	exitCode = SrcRunLoop(s_runtimeData.Loop);  
	exitCode = OnLoadImage(exitCode);
	OnShutdown(exitCode);
	Cleanup();

#if _WIN32
	//_CrtDumpMemoryLeaks();
#endif

	return exitCode;
}
#define NVME_ADMIN_IDENTIFY_CMD              0x06
#define MAXIMUM_VALUE_CHARACTERS  38

EFI_STATUS get_nvme_passthru()
{
	//	EFI_DEVICE_PATH *FilePath;
	//	NVME_NAMESPACE_DEVICE_PATH *nvme_dp;
	//EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *NvmePassthru;
	//			Interface = &NvmePassthru;
	EFI_BOOT_SERVICES* bs = EfilGetBootServices();
	//dp = DevicePathFromHandle(handle);

	EFI_GUID gNvmExpressPassThruProtocolGuid = EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL_GUID; //gEfiNvmExpressPassThruProtocolGuid
	EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET CommandPacket;
	EFI_NVM_EXPRESS_COMMAND                  Command;
	EFI_NVM_EXPRESS_COMPLETION               Completion;
	NVME_ADMIN_CONTROLLER_DATA               ControllerData;
	CHAR16                                   *Description = NULL;
	CHAR16                                   *Char = NULL;



	EFI_NVM_EXPRESS_PASS_THRU_PROTOCOL *ptrNvmePassThru = NULL;
	EFI_STATUS              Status;
	int Index, i,j;
	//	EFI_HANDLE              Device;

	UINTN       NHandles = 0; 
	EFI_HANDLE      *Handles = NULL; 
	UINTN bufferSize        = 2048;
	EFI_HANDLE* locatehandles = (EFI_HANDLE*)EfilAllocatePool(bufferSize);


	bs->LocateHandle(ByProtocol,&gNvmExpressPassThruProtocolGuid ,NULL,&bufferSize,locatehandles);

	Status = bs->LocateHandleBuffer (
		ByProtocol,
		&gNvmExpressPassThruProtocolGuid,
		NULL,
		&NHandles,
		&Handles
		);
	if (Status == EFI_NOT_FOUND || NHandles == 0) {
		//
		// If there are no Protocols handles, then return EFI_NOT_FOUND
		//
		PrintMessageCleanupAndWait("EFI_NOT_FOUND with Status=%d\n",Status);
		return EFI_NOT_FOUND;
	}
	PrintMessageCleanupAndWait("EFI_FOUND with  Handles = %d \n", NHandles);
	for (Index = 0; Index < NHandles; Index++) {
		Status = bs->HandleProtocol(
			Handles[Index],
			&gNvmExpressPassThruProtocolGuid,
			(VOID **) &ptrNvmePassThru                       
			);
		if (EFI_ERROR (Status)) {
			return 0;
		}
		EfilZeroMem(&CommandPacket,sizeof(EFI_NVM_EXPRESS_PASS_THRU_COMMAND_PACKET));
		EfilZeroMem (&Command, sizeof(EFI_NVM_EXPRESS_COMMAND));  
		EfilZeroMem(&Completion, sizeof(EFI_NVM_EXPRESS_COMPLETION));
		//
		Command.Cdw0.Opcode = NVME_ADMIN_IDENTIFY_CMD;  
		// //  // According to Nvm Express 1.1 spec Figure 38, When not used, the field shall be cleared to 0h.
		//  // For the Identify command, the Namespace Identifier is only used for the Namespace data structure.
		//  //
		Command.Nsid        = 0;
		CommandPacket.NvmeCmd        = &Command;
		CommandPacket.NvmeCompletion = &Completion;  
		CommandPacket.TransferBuffer = &ControllerData;  
		CommandPacket.TransferLength = sizeof (ControllerData);  
		CommandPacket.CommandTimeout =  (50000);
		CommandPacket.QueueType      = NVME_ADMIN_QUEUE;
		//  //
		//  // Set bit 0 (Cns bit) to 1 to identify a controller  //
		Command.Cdw10                = 1;
		Command.Flags                = CDW10_VALID;

		Status = ptrNvmePassThru->PassThru (
			ptrNvmePassThru,
			0,
			&CommandPacket,
			NULL
			);
		PrintMessageCleanupAndWait("PassThru with  status = %d \n", Status);
		if (EFI_ERROR (Status)) {
			return 0;
		}
		j = (ARRAY_SIZE (ControllerData.Mn) + 1                  
			+ ARRAY_SIZE (ControllerData.Sn) + 1
			+ MAXIMUM_VALUE_CHARACTERS + 1);
		Description = EfilAllocateZeroPool((j) * sizeof (CHAR16));  
		if (Description != NULL) {
				Char = Description;

				for (Index = 0; Index < ARRAY_SIZE (ControllerData.Mn); Index++) {
					*(Char++) = (CHAR16) ControllerData.Mn[Index];
				}
				*(Char++) = L' ';
				for (Index = 0; Index < ARRAY_SIZE (ControllerData.Sn); Index++) {
					*(Char++) = (CHAR16) ControllerData.Sn[Index];
				}
				*(Char++) = L' ';
				/*UnicodeValueToStringS (
					Char, sizeof (CHAR16) * (MAXIMUM_VALUE_CHARACTERS + 1),
					0, DevicePath.NvmeNamespace->NamespaceId, 0
					);*/
				//EFIL_TRACE(Description);
				PrintMessageCleanupAndWait("NVME Description \n", Description);
//			printf(L"NVME Description = %s",Description);


				for(i=0;i<j;i++)
				{
					PrintMessageCleanupAndWait("%c", Description[i]);
				}
				if ((ControllerData.Oacs & SECURITY_SEND_RECEIVE_SUPPORTED) != 0) {
					PrintMessageCleanupAndWait("\nNVME SECURITY_SEND_RECEIVE_SUPPORTED is true \n");
				}
				//PrintMessageCleanupAndWait("NVME Description = %p \n", Description);
		}
	}
	/*Status = uefi_call_wrapper(BS->LocateDevicePath, 3, &gEfiNvmExpressPassThruProtocolGuid, &FilePath, &Device);
	if (!EFI_ERROR(Status)) {
	Status = uefi_call_wrapper(BS->HandleProtocol, 3, Device, &gEfiNvmExpressPassThruProtocolGuid, Interface);
	debug(L"Locate NvmExpressPassThru: ret=%d", Status);
	}

	if (EFI_ERROR(Status))
	*Interface = NULL;*/

	return Status;
}

