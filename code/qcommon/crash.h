#include "q_shared.h"
#include "qcommon.h"

#include <stdio.h>


// json.cpp
void JSONW_BeginFile(FILE* file);
void JSONW_EndFile();
void JSONW_BeginObject();
void JSONW_BeginNamedObject(const char* name);
void JSONW_EndObject();
void JSONW_BeginArray();
void JSONW_BeginNamedArray(const char* name);
void JSONW_EndArray();
void JSONW_IntegerValue(const char* name, int number);
void JSONW_HexValue(const char* name, uint64_t number);
void JSONW_BooleanValue(const char* name, qbool value);
void JSONW_StringValue(const char* name, const char* format, ...);
void JSONW_UnnamedHex(uint64_t number);
void JSONW_UnnamedString(const char* format, ...);

// crash.cpp
void Crash_SaveQVMPointer(vmIndex_t vmIndex, vm_t* vm);
void Crash_SaveQVMChecksum(vmIndex_t vmIndex, unsigned int crc32);
void Crash_SaveQVMGitString(const char* varName, const char* varValue);
void Crash_SaveModName(const char* modName);
void Crash_SaveModVersion(const char* modVersion);
void Crash_PrintToFile(const char* engineFilePath);
#if defined(__linux__)
void Crash_PrintVMStackTracesASS(int fd); // async-signal-safe
#endif
