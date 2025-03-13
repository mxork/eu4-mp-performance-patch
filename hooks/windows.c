#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <windows.h>
#include <errno.h>
#include <memoryapi.h>
#include <processthreadsapi.h>
#include <libloaderapi.h>

#include "common.h"

// initialization
void* module_base = NULL;
char* _my_argv0[32];

#pragma comment(linker, "/SECTION:.shared,RWS")
#pragma data_seg(".shared")
atomic_bool enabled0 = false;
#pragma data_seg()
atomic_bool* enabled = &enabled0;


// init
void libpatcher_init() {
  info("libpatcher_init\n");
}

// config
void cmdline_init() {
  _my_argv = _my_argv0;
  log("cmdline init\n");
  // _my_argc = __argc;
  // _my_argv = __argv;
  LPWSTR cmdline = GetCommandLineW();
  LPWSTR* _argv = CommandLineToArgvW(cmdline, &_my_argc);
  if (_argv == NULL) {
    log("CommandLineToArgvW failed\n");
    exit(1);
  }

  for (int i=0; i<_my_argc; i++) {
    // errno_t wcstombs_s(
    //    size_t *pReturnValue,
    //    char *mbstr,
    //    size_t sizeInBytes,
    //    const wchar_t *wcstr,
    //    size_t count
    // );
    size_t len;
    char* converted = malloc(128);
    _my_argv0[i] = converted;
    errno_t e = wcstombs_s(&len, converted, 128, _argv[i], 127);
    if (e) {
      log("failed to convert arg %d\n", i);
      exit(1);
    }
  }

  for (int i=0; i<_my_argc; i++) {
    info("arg %d: %s\n", i, _my_argv[i]);
  }
}


void on_nakama_client_join_match() {
  // :stub
}
//
// this is a little unsafe, but man am I salty.
// trying to write to the console is just not happening.
// bool log_output_open_once = false;
FILE* log_output = NULL;
void _log(const char *format, ...) {
  if (!log_output) {
    log_output = fopen("patcher.log", "w");
  }

  if (!log_output) {
    return;
  }

  va_list args;
  va_start(args, format);
  vfprintf(log_output, format, args);
  va_end(args);
  fflush(log_output);
  // fclose(log_output);
}

// dynamic
int get_current_day_of_month() {
  const void* state = *(void**)(module_base+0x233fe78);
  const int currentDayOfMonthOffset = 0x1de4;
  int32_t current_day = *((int32_t*)(state+currentDayOfMonthOffset));
  return current_day;
}

void hook1();
void* hook1return;

void hook2();
void* hook2return;

void hook3();
void* hook3return;
void* hook3returnalt;

// BOOL GetModuleHandleExA(
//   [in]           DWORD   dwFlags,
//   [in, optional] LPCSTR  lpModuleName,
//   [out]          HMODULE *phModule
// );
//
// DWORD GetModuleFileNameA(
//   [in, optional] HMODULE hModule,
//   [out]          LPSTR   lpFilename,
//   [in]           DWORD   nSize
// );
//
// FARPROC GetProcAddress(
//   [in] HMODULE hModule,
//   [in] LPCSTR  lpProcName
// );
void dopatch() {
  HMODULE module;
  GetModuleHandleExA(0, NULL, &module);
  // technically, we could just pass null to get the name
  char name[256];
  GetModuleFileNameA(module, name, 256);
  debug("dopatch. main exe name: %s\n", name);
  module_base = (void*) module;
  debug("  module base: %p\n", module_base);

  // from https://stackoverflow.com/questions/26572459/c-get-module-base-address-for-64bit-application
  // I'm 90% sure that the module handle is, itself, a pointer to the base address of the module.
  // gonna confirm with GetProcAddress
  //
  // using this as base
  // Function
  //   Ordinal:                         658
  //   Address:                         0x17361a0
  //   Name:                            SDL_vsnprintf
  //
  // god bless:
  //
  // module handle:              0000000140000000
  // SDL_vsnprintf addr:         00000001417361A0
  // SDL_vsnprintf addr (orig):  0x17361a0
  FARPROC thaddr = GetProcAddress(module, "SDL_vsnprintf");
  debug("module handle:              %p\n", module);
  debug("SDL_vsnprintf addr:         %p\n", thaddr);
  debug("SDL_vsnprintf addr (orig):  %s\n", "0x17361a0");
  if ((((void*)thaddr)-((void*)module_base)) != 0x17361a0) {
    log("address of SDL_vsnprintf didn't match expected\n");
    log("refusing to apply patch\n");
    the_patcher_config.patchmode = disabled;
    return;
  }

  // now dump some bytes and check they match
  // calc checksums 0x140743aa0
  // huh. I didn't realize this shit was static.
  // 0x488954241048894c is expected, but little-endian man
  // void* calcchecksums_addr = (void*)0x140743aa0;
  void* calcchecksums_addr = module_base + 0x743aa0;
  debug("calcchecksums_addr: %p\n", calcchecksums_addr);
  dumphex(calcchecksums_addr, 8);
  if (*((uint64_t*)calcchecksums_addr) != 0x4c89481024548948) {
    log("bytes at calcchecksums_addr did not match expected\n");
    log("refusing to apply patch\n");
    the_patcher_config.patchmode = disabled;
    return;
  }

  char* gitchecksum = module_base + 0x1d2ffd8;
  char* gitchecksumexpected = "  f2de20fd7cc5d735418b384c759d491d\n";
  if (strncmp(gitchecksum, gitchecksumexpected, 64) != 0) {
    log("git checksum does not match expected\n");
    log("refusing to apply patch\n");
    the_patcher_config.patchmode = disabled;
    return;
  }

  // constant patch
  // province loop count start 0x140743e48
  // unit loop countstart    0x140744461 to 0x14074446f
  //    0x140744473 - 0x140744461
  //    18 btw
  //
  // original
  // mov rcx, qword [rdx+0x1cf0]
  // sub rcx, qword [rdx+0x1ce8]
  // sar rcx, 0x3
  //
  // patch
  // -> rasm2 'mov rcx, 0'
  // 48c7c100 00 00 00
  //void* calcchecksums_unit_loop_target = (void*)0x140744461;
  //size_t patchsize = 18;
  //uint8_t patch[] = {0x48,0xc7,0xc1,0,0,0,0};

  //unprotect(calcchecksums_unit_loop_target, patchsize);

  //debug("unit loop pre: %p\n", calcchecksums_unit_loop_target);
  //dumphex(calcchecksums_unit_loop_target, patchsize);

  //memset(calcchecksums_unit_loop_target, 0x90, patchsize);
  //memcpy(calcchecksums_unit_loop_target, patch, 7); // sizeof(patch));
  //                                                  //
  //debug("unit loop post: %p\n", calcchecksums_unit_loop_target);
  //dumphex(calcchecksums_unit_loop_target, patchsize);

  //protect(calcchecksums_unit_loop_target, patchsize);
  // redo above using do1patch

  // :todo assert prepatch contents

  // 0x140743e4c - 0x140743e3a  = 18
  //
  // mov rax, qword [rdi+0x1c98]
  // sub rax, qword [rdi+0x1c90]
  // sar rax, 0x3
  do1patch(module_base + 0x743e3a, 18, hook1, &hook1return);
  do1patch(module_base + 0x744461, 18, hook2, &hook2return);



  // >>> 0x140745892 - 0x140745884
  // 14
  //
  // mov edi, dword [r14+0x21bc]
  // mov ebx, r15d
  // test edi, edi
  // jle 0x1407458f5
  //
  // skip loop addr 0x1407458f5
  do1patch(module_base + 0x745884, 14, hook3, &hook3return);
  hook3returnalt = module_base + 0x7458f5;
}

// BOOL VirtualProtect(
//   [in]  LPVOID lpAddress,
//   [in]  SIZE_T dwSize,
//   [in]  DWORD  flNewProtect,
//   [out] PDWORD lpflOldProtect
// );
//
// BOOL FlushInstructionCache(
// [in] HANDLE  hProcess,
// [in] LPCVOID lpBaseAddress,
// [in] SIZE_T  dwSize
// );
void unprotect(void* addr, size_t size) {
  debug("  unprotect\n");
  debug("    addr: %p\n", addr);
  debug("    size: %u\n", size);
  DWORD old_protect;
  DWORD new_protect = PAGE_EXECUTE_READWRITE;

  if (VirtualProtect(addr, size, new_protect, &old_protect) == 0) {
      err("unprotect failed");
      exit(1);
  }
}

void protect(void* addr, size_t size) {
  debug("  unprotect\n");
  debug("    addr: %p\n", addr);
  debug("    size: %u\n", size);
  DWORD old_protect;
  DWORD new_protect = PAGE_EXECUTE_READ;

  if (VirtualProtect(addr, size, new_protect, &old_protect) == 0) {
      err("protect failed");
      exit(1);
  }

  HANDLE handle = GetCurrentProcess();
  if (FlushInstructionCache(handle, addr, size) == 0) {
      err("flush instruction cache failed");
      exit(1);
  }
}
