#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/crash.h"
#ifndef DEDICATED
#include "../client/client.h"
#endif
#include "win_local.h"
#include "glw_win.h"
#include <DbgHelp.h>
#include <strsafe.h>


#if !id386 && !idx64
#	error "This architecture is not supported."
#endif


typedef void (WINAPI *FptrGeneric)();
typedef BOOL (WINAPI *FptrSymInitialize)(HANDLE, PCSTR, BOOL);
typedef PVOID (WINAPI *FptrSymFunctionTableAccess64)(HANDLE, DWORD64);
typedef DWORD64 (WINAPI *FptrSymGetModuleBase64)(HANDLE, DWORD64);
typedef BOOL (WINAPI *FptrStackWalk64)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
typedef BOOL (WINAPI *FptrSymGetSymFromAddr64)(HANDLE, DWORD64, PDWORD64, PIMAGEHLP_SYMBOL64);
typedef BOOL (WINAPI *FptrMiniDumpWriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, CONST PMINIDUMP_EXCEPTION_INFORMATION, CONST PMINIDUMP_USER_STREAM_INFORMATION, CONST PMINIDUMP_CALLBACK_INFORMATION);


typedef struct {
	HMODULE libraryHandle;
	FptrSymInitialize SymInitialize;
	FptrSymFunctionTableAccess64 SymFunctionTableAccess64;
	FptrSymGetModuleBase64 SymGetModuleBase64;
	FptrStackWalk64 StackWalk64;
	FptrSymGetSymFromAddr64 SymGetSymFromAddr64;
	FptrMiniDumpWriteDump MiniDumpWriteDump;
} debug_help_t;


static void WIN_CloseDebugHelp( debug_help_t* debugHelp )
{
	if (debugHelp->libraryHandle == NULL)
		return;

	FreeLibrary(debugHelp->libraryHandle);
	debugHelp->libraryHandle = NULL;
}

static qboolean WIN_OpenDebugHelp( debug_help_t* debugHelp )
{
	debugHelp->libraryHandle = LoadLibraryA("dbghelp.dll");
	if (debugHelp->libraryHandle == NULL)
		return qfalse;

#define GET_FUNCTION(func) \
	debugHelp->func = (Fptr##func)GetProcAddress(debugHelp->libraryHandle, #func); \
	if (debugHelp->func == NULL) { \
		WIN_CloseDebugHelp(debugHelp); \
		return qfalse; \
	}

	GET_FUNCTION(SymInitialize)
	GET_FUNCTION(SymFunctionTableAccess64)
	GET_FUNCTION(SymGetModuleBase64)
	GET_FUNCTION(StackWalk64)
	GET_FUNCTION(SymGetSymFromAddr64)
	GET_FUNCTION(MiniDumpWriteDump)

#undef GET_FUNCTION

	return qtrue;
}

static void WIN_DumpStackTrace( debug_help_t* debugHelp )
{
	enum {
		BUFFER_SIZE = 1024,
		MAX_LEVELS = 256
	};

	if (!debugHelp->SymInitialize(GetCurrentProcess(), NULL, TRUE))
		return;

	CONTEXT context;
#if id386
	ZeroMemory(&context, sizeof(CONTEXT));
	context.ContextFlags = CONTEXT_CONTROL;
	__asm {
	Label:
		mov[context.Ebp], ebp;
		mov[context.Esp], esp;
		mov eax, [Label];
		mov[context.Eip], eax;
	}
#else // idx64
	RtlCaptureContext(&context);
#endif

	// Init the stack frame for this function
	STACKFRAME64 stackFrame;
	ZeroMemory(&stackFrame, sizeof(stackFrame));
#if id386
	const DWORD machineType = IMAGE_FILE_MACHINE_I386;
	stackFrame.AddrPC.Offset = context.Eip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Ebp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Esp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#else // idx64
	const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
	stackFrame.AddrPC.Offset = context.Rip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Rsp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Rsp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
#endif

	JSONW_BeginNamedArray("stack_trace");
	
	unsigned char buffer[sizeof(IMAGEHLP_SYMBOL64) + BUFFER_SIZE];
	IMAGEHLP_SYMBOL64* const symbol = (IMAGEHLP_SYMBOL64*)buffer;

	int level = 1;
	while (level++ < (MAX_LEVELS + 1)) {
		BOOL result = debugHelp->StackWalk64(
			machineType, GetCurrentProcess(), GetCurrentThread(), &stackFrame, &context, 
			NULL, debugHelp->SymFunctionTableAccess64, debugHelp->SymGetModuleBase64, NULL);
		if (!result || stackFrame.AddrPC.Offset == 0)
			break;

		ZeroMemory(symbol, sizeof(IMAGEHLP_SYMBOL64) + BUFFER_SIZE);
		symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		symbol->MaxNameLength = BUFFER_SIZE;

		JSONW_BeginObject();
		JSONW_HexValue("program_counter", stackFrame.AddrPC.Offset);
		JSONW_HexValue("stack_pointer", stackFrame.AddrStack.Offset);
		JSONW_HexValue("frame_pointer", stackFrame.AddrFrame.Offset);
		JSONW_HexValue("return_address", stackFrame.AddrReturn.Offset);

		DWORD64 displacement;
		result = debugHelp->SymGetSymFromAddr64(GetCurrentProcess(), stackFrame.AddrPC.Offset, &displacement, symbol);
		if (result)
			JSONW_StringValue("name", symbol->Name);

		JSONW_EndObject();
	}

	JSONW_EndArray();
}

static BOOL WINAPI WIN_MiniDumpCallback(
	IN PVOID CallbackParam,
	IN CONST PMINIDUMP_CALLBACK_INPUT CallbackInput,
	IN OUT PMINIDUMP_CALLBACK_OUTPUT CallbackOutput )
{
	// Keep everything except...
	if (CallbackInput->CallbackType != ModuleCallback)
		return TRUE;

	// ...modules unreferenced by memory.
	if ((CallbackOutput->ModuleWriteFlags & ModuleReferencedByMemory) == 0) {
		CallbackOutput->ModuleWriteFlags &= ~ModuleWriteModule;
		return TRUE;
	}

	return TRUE;
}

static qboolean WIN_CreateDirectoryIfNeeded( const char* path )
{
	const BOOL success = CreateDirectoryA(path, NULL);
	if (success)
		return qtrue;

	return GetLastError() == ERROR_ALREADY_EXISTS;
}

static char exc_reportFolderPath[MAX_PATH];

static void WIN_CreateDumpFilePath( char* buffer, const char* fileName, SYSTEMTIME* time )
{
	char* const temp = getenv("TEMP");
	if (temp == NULL || !WIN_CreateDirectoryIfNeeded(va("%s\\cnq3_crash", temp)))
		return;

	Q_strncpyz(exc_reportFolderPath, va("%s\\cnq3_crash\\", temp), sizeof(exc_reportFolderPath));
	StringCchPrintfA(
		buffer, MAX_PATH, "%s%s_%04d.%02d.%02d_%02d.%02d.%02d",
		exc_reportFolderPath, fileName, time->wYear, time->wMonth, time->wDay,
		time->wHour, time->wMinute, time->wSecond);
}

static qboolean WIN_GetOSVersion( int* major, int* minor, int* revision )
{
	enum { FILE_INFO_SIZE = 4096 };
	const DWORD fileInfoSize = min(FILE_INFO_SIZE, GetFileVersionInfoSizeA("kernel32.dll", NULL));
	if (fileInfoSize == 0)
		return qfalse;
		
	char fileInfo[FILE_INFO_SIZE];
	if (!GetFileVersionInfoA("kernel32.dll", 0, fileInfoSize, fileInfo))
		return qfalse;

	LPVOID osInfo = NULL;
	UINT osInfoSize = 0;
	if (!VerQueryValueA(&fileInfo[0], "\\", &osInfo, &osInfoSize) ||
		osInfoSize < sizeof(VS_FIXEDFILEINFO))
		return qfalse;

	const VS_FIXEDFILEINFO* const versionInfo = (const VS_FIXEDFILEINFO*)osInfo;
	*major = HIWORD(versionInfo->dwProductVersionMS);
	*minor = LOWORD(versionInfo->dwProductVersionMS);
	*revision = HIWORD(versionInfo->dwProductVersionLS);

	return qtrue;
}

static const char* WIN_GetExceptionCodeString( DWORD exceptionCode )
{
	switch (exceptionCode) {
		case EXCEPTION_ACCESS_VIOLATION: return "The thread tried to read from or write to a virtual address for which it does not have the appropriate access.";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "The thread tried to access an array element that is out of bounds and the underlying hardware supports bounds checking.";
		case EXCEPTION_BREAKPOINT: return "A breakpoint was encountered.";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "The thread tried to read or write data that is misaligned on hardware that does not provide alignment.";
		case EXCEPTION_FLT_DENORMAL_OPERAND: return "One of the operands in a floating-point operation is denormal.";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "The thread tried to divide a floating-point value by a floating-point divisor of zero.";
		case EXCEPTION_FLT_INEXACT_RESULT: return "The result of a floating-point operation cannot be represented exactly as a decimal fraction.";
		case EXCEPTION_FLT_INVALID_OPERATION: return "This exception represents any floating-point exception not described by other codes.";
		case EXCEPTION_FLT_OVERFLOW: return "The exponent of a floating-point operation is greater than the magnitude allowed by the corresponding type.";
		case EXCEPTION_FLT_STACK_CHECK: return "The stack overflowed or underflowed as the result of a floating-point operation.";
		case EXCEPTION_FLT_UNDERFLOW: return "The exponent of a floating-point operation is less than the magnitude allowed by the corresponding type.";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "The thread tried to execute an invalid instruction.";
		case EXCEPTION_IN_PAGE_ERROR: return "The thread tried to access a page that was not present and the system was unable to load the page.";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "The thread tried to divide an integer value by an integer divisor of zero.";
		case EXCEPTION_INT_OVERFLOW: return "The result of an integer operation caused a carry out of the most significant bit of the result.";
		case EXCEPTION_INVALID_DISPOSITION: return "An exception handler returned an invalid disposition to the exception dispatcher.";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "The thread tried to continue execution after a noncontinuable exception occurred.";
		case EXCEPTION_PRIV_INSTRUCTION: return "The thread tried to execute an instruction whose operation is not allowed in the current machine mode.";
		case EXCEPTION_SINGLE_STEP: return "A trace trap or other single-instruction mechanism signaled that one instruction has been executed.";
		case EXCEPTION_STACK_OVERFLOW: return "The thread used up its stack.";
		default: return "Unknown exception code";
	}
}

static const char* WIN_GetAccessViolationCodeString( DWORD avCode )
{
	switch (avCode) {
		case 0: return "Read access violation";
		case 1: return "Write access violation";
		case 8: return "User-mode data execution prevention (DEP) violation";
		default: return "Unknown violation";
	}
}

#ifndef DEDICATED
// We save those separately because the handler might change related state before writing the report.
static qbool wasDevModeValid = qfalse;
static qbool wasMinimized = qfalse;
#endif

static void WIN_WriteTextData( const char* filePath, debug_help_t* debugHelp, EXCEPTION_RECORD* pExceptionRecord )
{
	FILE* const file = fopen(filePath, "w");
	if (file == NULL)
		return;

	JSONW_BeginFile(file);
	
	WIN_DumpStackTrace(debugHelp);
	JSONW_HexValue("exception_code", pExceptionRecord->ExceptionCode);
	JSONW_StringValue("exception_description", WIN_GetExceptionCodeString(pExceptionRecord->ExceptionCode));

	if (pExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && 
		pExceptionRecord->NumberParameters >= 2) {
		JSONW_StringValue("exception_details", "%s at address %s",
			WIN_GetAccessViolationCodeString(pExceptionRecord->ExceptionInformation[0]),
			Q_itohex(pExceptionRecord->ExceptionInformation[1], qtrue, qtrue));
	}

	int osVersion[3];
	if (WIN_GetOSVersion(osVersion, osVersion + 1, osVersion + 2)) {
		JSONW_StringValue("windows_version", "%d.%d.%d", osVersion[0], osVersion[1], osVersion[2]);
	}

#ifndef DEDICATED
	JSONW_BooleanValue("device_mode_changed", wasDevModeValid);
	JSONW_BooleanValue("minimized", wasMinimized);
#endif

	Crash_PrintToFile(__argv[0]);

	JSONW_EndFile();

	fclose(file);
}

static void WIN_WriteMiniDump( const char* filePath, debug_help_t* debugHelp, EXCEPTION_POINTERS* pExceptionPointers )
{
	const HANDLE dumpFile = CreateFileA(
		filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
		0, CREATE_ALWAYS, 0, 0);

	if (dumpFile == INVALID_HANDLE_VALUE)
		return;

	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
	ZeroMemory(&exceptionInfo, sizeof(exceptionInfo));
	exceptionInfo.ThreadId = GetCurrentThreadId();
	exceptionInfo.ExceptionPointers = pExceptionPointers;
	exceptionInfo.ClientPointers = TRUE;

	MINIDUMP_CALLBACK_INFORMATION callbackInfo;
	ZeroMemory(&callbackInfo, sizeof(callbackInfo));
	callbackInfo.CallbackRoutine = &WIN_MiniDumpCallback;
	callbackInfo.CallbackParam = NULL;

	debugHelp->MiniDumpWriteDump(
		GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
		(MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
		&exceptionInfo, NULL, &callbackInfo);

	CloseHandle(dumpFile);
}

static const char* WIN_GetFileName( const char* path )
{
	const char* name = strrchr(path, '\\');
	if (name != path)
		return name + 1;

	name = strrchr(path, '/');
	if (name != path)
		return name + 1;

	return path;
}

static qbool exc_reportWritten = qfalse;

static void WIN_WriteExceptionFilesImpl( EXCEPTION_POINTERS* pExceptionPointers )
{
	debug_help_t debugHelp;
	if (!WIN_OpenDebugHelp(&debugHelp))
		return;

	SYSTEMTIME time;
	GetSystemTime(&time);

	char modulePath[MAX_PATH];
	GetModuleFileNameA(GetModuleHandle(NULL), modulePath, sizeof(modulePath));

	char dumpFilePath[MAX_PATH];
	WIN_CreateDumpFilePath(dumpFilePath, WIN_GetFileName(modulePath), &time);

	WIN_WriteTextData(va("%s.json", dumpFilePath), &debugHelp, pExceptionPointers->ExceptionRecord);
	WIN_WriteMiniDump(va("%s.dmp", dumpFilePath), &debugHelp, pExceptionPointers);
	exc_reportWritten = qtrue;

	WIN_CloseDebugHelp(&debugHelp);
}

static int WINAPI WIN_WriteExceptionFiles( EXCEPTION_POINTERS* pExceptionPointers )
{
	// No exception info?
	if (!pExceptionPointers) {
		__try {
			// Generate an exception to get a proper context.
			RaiseException(EXCEPTION_BREAKPOINT, 0, 0, NULL);
		} __except (WIN_WriteExceptionFiles(GetExceptionInformation()), EXCEPTION_CONTINUE_EXECUTION) {}

		return EXCEPTION_EXECUTE_HANDLER;
	}

	// We have exception information now, so let's proceed.
	WIN_WriteExceptionFilesImpl(pExceptionPointers);

	return EXCEPTION_EXECUTE_HANDLER;
}

static qbool WIN_ShouldContinueSearch( DWORD exceptionCode )
{
	// Obviously, this piece of code must be *very* careful with what exception codes are handled.
	// Invoking our handler on a non-crash will shut down the application when it shouldn't.
	// Not invoking our handler on a crash means the app shuts down immediately with no crash report.
	// As you can see, neither scenario is desirable...

	switch (exceptionCode) {
		// The following should always invoke our handler.
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_INVALID_OPERATION:
		case EXCEPTION_FLT_STACK_CHECK:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		// The debugger has first-chance access.
		// Therefore, if we get these, we should stop too.
		case EXCEPTION_BREAKPOINT:
		case EXCEPTION_SINGLE_STEP:
			return qfalse;

		// We don't handle the rest.
		// Please leave the commented lines so we know what's being allowed.
		//case DBG_PRINTEXCEPTION_C:			// used by OutputDebugStringA/W
		//case EXCEPTION_FLT_INEXACT_RESULT:
		//case EXCEPTION_FLT_DENORMAL_OPERAND:
		//case EXCEPTION_FLT_OVERFLOW:
		//case EXCEPTION_FLT_UNDERFLOW:
		//case EXCEPTION_INT_OVERFLOW:
		//case EXCEPTION_INVALID_DISPOSITION:	// should not happen
		//case EXCEPTION_POSSIBLE_DEADLOCK:		// STATUS_POSSIBLE_DEADLOCK is not defined
		//case EXCEPTION_GUARD_PAGE:			// we hit the stack guard page (used for growing the stack)
		//case EXCEPTION_INVALID_HANDLE:		// invalid kernel object (may have been closed)
		default:
			return qtrue;
	}
}

// Debugging a full-screen app with a single screen is a horrendous experience.
// With our custom handler, we can handle crashes by restoring the desktop settings
// and hiding the main window before letting the debugger take over.
// This will only help for crashes though, breakpoints will still be fucked up.
// Work-around for breakpoints: use __debugbreak(); and don't launch the app through the debugger.
static qbool WIN_WouldDebuggingBeOkay()
{
	if (g_wv.monitorCount >= 2 || g_wv.hWnd == NULL)
		return qtrue;

	const qbool fullScreen = (GetWindowLongA(g_wv.hWnd, GWL_STYLE) & WS_POPUP) != 0;
	if (!fullScreen)
		return qtrue;

	return qfalse;
}

//
// The exception handler's job is to reset system settings that won't get reset
// as part of the normal process clean-up by the OS.
// It can't do any memory allocation or use any synchronization objects.
// Ideally, we want it to be called before every abrupt application exit
// and right after any legitimate crash.
//
// There are 2 cases where the function won't be called:
//
// 1. Termination through the debugger.
//    Our atexit handler never gets called.
//
//    Work-around: Quit normally.
//
// 2. Breakpoints. The debugger has first-chance access and handles them.
//    Our exception handler doesn't get called.
//
//    Work-around: None for debugging. Quit normally.
//

static qbool exc_exitCalled = qfalse;

LONG CALLBACK WIN_HandleException( EXCEPTION_POINTERS* ep )
{
	if (ep != NULL && ep->ExceptionRecord != NULL) {
		qbool contSearch = WIN_ShouldContinueSearch(ep->ExceptionRecord->ExceptionCode);
		if (contSearch && (ep->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE) != 0)
			contSearch = qfalse;
		if (contSearch)
			return EXCEPTION_CONTINUE_SEARCH;
	}

	__try {
		WIN_EndTimePeriod(); // system timer resolution
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	if (IsDebuggerPresent() && WIN_WouldDebuggingBeOkay())
		return EXCEPTION_CONTINUE_SEARCH;

#ifndef DEDICATED
	__try {
		wasDevModeValid = glw_state.cdsDevModeValid;
		if (glw_state.cdsDevModeValid)
			WIN_SetDesktopDisplaySettings();
	} __except (EXCEPTION_EXECUTE_HANDLER) {}

	if (g_wv.hWnd != NULL) {
		__try {
			wasMinimized = (qbool)!!IsIconic(g_wv.hWnd);
		} __except(EXCEPTION_EXECUTE_HANDLER) {}

		__try {
			ShowWindow(g_wv.hWnd, SW_MINIMIZE);
		} __except(EXCEPTION_EXECUTE_HANDLER) {}
	}
#endif

#ifndef DEDICATED
	__try {
		CL_MapDownload_CrashCleanUp();
	} __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif

	if (exc_exitCalled || IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	static const char* mbTitle = "CNQ3 Crash";
	static const char* mbMsg = "CNQ3 crashed!\n\nYes to generate a crash report\nNo to continue after attaching a debugger\nCancel to quit";
	const int result = MessageBoxA(NULL, mbMsg, mbTitle, MB_YESNOCANCEL | MB_ICONERROR | MB_TOPMOST);
	if (result == IDYES) {
		WIN_WriteExceptionFiles(ep);
		if (exc_reportWritten)
			ShellExecute(NULL, "open", exc_reportFolderPath, NULL, NULL, SW_SHOW);
		else
			MessageBoxA(NULL, "CNQ3's crash report generation failed!\nExiting now", mbTitle, MB_OK | MB_ICONERROR);
	} else if (result == IDNO && IsDebuggerPresent()) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	ExitProcess(666);
}

void WIN_HandleExit( void )
{
	exc_exitCalled = qtrue;
	WIN_HandleException(NULL);
}
