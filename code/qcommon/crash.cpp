#include "crash.h"
#include "git.h"
#include "vm_local.h"
#include <sys/stat.h>


typedef struct {
	char gitHeadHash[24]; // SHA-1 -> 160 bits -> 20 hex chars
	vm_t* vm;
	unsigned int crc32; // CRC32
} vmCrashInfo_t;

static vmCrashInfo_t crash_vm[VM_COUNT];
static char crash_modName[64];
static char crash_modVersion[64];

#if defined(_WIN32)
#	define NEWLINE	"\n"
#else
#	define NEWLINE	"\r\n"
#endif


static qbool IsVMIndexValid(vmIndex_t vmIndex)
{
	return vmIndex >= 0 && vmIndex < VM_COUNT;
}

static const char* GetVMName(vmIndex_t vmIndex)
{
	switch (vmIndex) {
		case VM_CGAME: return "cgame";
		case VM_GAME: return "game";
		case VM_UI: return "ui";
		default: return "unknown";
	}
}

void Crash_SaveQVMPointer(vmIndex_t vmIndex, vm_t* vm)
{
	if (IsVMIndexValid(vmIndex))
		crash_vm[vmIndex].vm = vm;
}

void Crash_SaveQVMChecksum(vmIndex_t vmIndex, unsigned int crc32)
{
	if (IsVMIndexValid(vmIndex))
		crash_vm[vmIndex].crc32 = crc32;
}

void Crash_SaveQVMGitString(const char* varName, const char* varValue)
{
	if (strstr(varName, "gitHeadHash") == NULL)
		return;

	vmIndex_t index;
	if (strstr(varName, "cg_") == varName)
		index = VM_CGAME;
	else if (strstr(varName, "g_") == varName)
		index = VM_GAME;
	else if (strstr(varName, "ui_") == varName)
		index = VM_UI;
	else
		return;

	Q_strncpyz(crash_vm[index].gitHeadHash, varValue, sizeof(crash_vm[index].gitHeadHash));
}

void Crash_SaveModName(const char* modName)
{
	if (modName && *modName != '\0')
		Q_strncpyz(crash_modName, modName, sizeof(crash_modName));
}

void Crash_SaveModVersion(const char* modVersion)
{
	if (modVersion && *modVersion != '\0')
		Q_strncpyz(crash_modVersion, modVersion, sizeof(crash_modVersion));
}

static void PrintQVMInfo(vmIndex_t vmIndex)
{
	static char callStack[MAX_VM_CALL_STACK_DEPTH * 12];

	vmCrashInfo_t* vm = &crash_vm[vmIndex];
	if (vm->crc32 == 0) {
		return;
	}

	JSONW_BeginObject();

	JSONW_IntegerValue("index", vmIndex);
	JSONW_StringValue("name", GetVMName(vmIndex));
	JSONW_BooleanValue("loaded", vm->vm != NULL);
	if (vm->crc32)
		JSONW_HexValue("crc32", vm->crc32);
	JSONW_StringValue("git_head_hash", vm->gitHeadHash);

	if (vm->vm != NULL) {
		vm_t* const vmp = vm->vm;
		JSONW_IntegerValue("call_stack_depth_current", vmp->callStackDepth);
		JSONW_IntegerValue("call_stack_depth_previous", vmp->lastCallStackDepth);

		int d = vmp->callStackDepth;
		qbool current = qtrue;
		if (d <= 0 || d > MAX_VM_CALL_STACK_DEPTH) {
			d = vmp->lastCallStackDepth;
			current = qfalse;
		}

		if (d <= 0 || d > MAX_VM_CALL_STACK_DEPTH)
			d = 0;

		if (d > 0) {
			JSONW_BooleanValue("call_stack_current", current);
			JSONW_BooleanValue("call_stack_limit_reached", d == MAX_VM_CALL_STACK_DEPTH);

			callStack[0] = '\0';
			for (int i = 0; i < d; i++) {
				Q_strcat(callStack, sizeof(callStack), Q_itohex(vmp->callStack[i], qtrue, qtrue));
				if (i - 1 < d) 
					Q_strcat(callStack, sizeof(callStack), " ");
			}
			JSONW_StringValue("call_stack", callStack);
		}
	}

	JSONW_EndObject();
}

static qbool IsAnyVMLoaded()
{
	for (int i = 0; i < VM_COUNT; i++) {
		if (crash_vm[i].crc32 != 0)
			return qtrue;
	}

	return qfalse;
}

static unsigned int CRC32_HashFile(const char* filePath)
{
	enum { BUFFER_SIZE = 16 << 10 }; // 16 KB
	static byte buffer[BUFFER_SIZE];

	struct stat st;
	if (stat(filePath, &st) != 0 || st.st_size == 0)
		return 0;

	FILE* const file = fopen(filePath, "rb");
	if (file == NULL)
		return 0;

	const unsigned int fileSize = (unsigned int)st.st_size;
	const unsigned int fullBlocks = fileSize / (unsigned int)BUFFER_SIZE;
	const unsigned int lastBlockSize = fileSize - fullBlocks * (unsigned int)BUFFER_SIZE;

	unsigned int crc32 = 0;
	crc32_init(&crc32);

	for(unsigned int i = 0; i < fullBlocks; ++i) {
		if (fread(buffer, BUFFER_SIZE, 1, file) != 1) {
			fclose(file);
			return 0;
		}
		crc32_update(&crc32, buffer, BUFFER_SIZE);
	}

	if(lastBlockSize > 0) {
		if (fread(buffer, lastBlockSize, 1, file) != 1) {
			fclose(file);
			return 0;
		}
		crc32_update(&crc32, buffer, lastBlockSize);
	}

	crc32_final(&crc32);

	fclose(file);

	return crc32;
}

void Crash_PrintToFile(const char* engineFilePath)
{
	JSONW_StringValue("engine_version", Q3_VERSION);
	JSONW_StringValue("engine_build_date", __DATE__);
	JSONW_StringValue("engine_arch", ARCH_STRING);
#ifdef DEDICATED
	JSONW_BooleanValue("engine_ded_server", qtrue);
#else
	JSONW_BooleanValue("engine_ded_server", qfalse);
#endif
#ifdef DEBUG
	JSONW_BooleanValue("engine_debug", qtrue);
#else
	JSONW_BooleanValue("engine_debug", qfalse);
#endif
#ifdef CNQ3_DEV
	JSONW_BooleanValue("engine_dev_build", qtrue);
#else
	JSONW_BooleanValue("engine_dev_build", qfalse);
#endif
	JSONW_StringValue("engine_git_branch", GIT_BRANCH);
	JSONW_StringValue("engine_git_commit", GIT_COMMIT);
	const unsigned int crc32 = CRC32_HashFile(engineFilePath);
	if (crc32)
		JSONW_HexValue("engine_crc32", crc32);
	JSONW_StringValue("mod_name", crash_modName);
	JSONW_StringValue("mod_version", crash_modVersion);

	if (IsAnyVMLoaded()) {
		JSONW_BeginNamedArray("vms");
		for (int i = 0; i < VM_COUNT; i++) {
			PrintQVMInfo((vmIndex_t)i);
		}
		JSONW_EndArray();
	}
}
