#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

// #include <sys/mman.h>
#include <memoryapi.h>
#include <processthreadsapi.h>
#include <libloaderapi.h>

#include "patcher.h"

void unprotect(void* addr, size_t size);
void protect(void* addr, size_t size);

// #pragma comment(linker, "/SECTION:.shared,RWS")
#pragma data_seg(".shared")
atomic_bool enabled0 = false;
#pragma data_seg()
atomic_bool* enabled = &enabled0;
void set_enabled(bool value) {
  atomic_store(enabled, value);
}
bool get_enabled() {
  return atomic_load(enabled);
}

#define log(...) fprintf (stderr, __VA_ARGS__)
void dumphex(uint8_t* start, size_t len) {
    log("  ");
    for (int i=0; i<len; i++)
      log("%02x", *(start+i));
    log("\n");
}

__attribute__((constructor))
void on_load() {
  fprintf(stderr, "onload\n");
  fflush(stderr);

  char* disabled_envvar = getenv("DISABLED");
  if (disabled_envvar && *disabled_envvar) {
    log("DISABLED is present; refusing to patch\n");
    return;
  }

  char* patched_disabled_envvar = getenv("PATCHED_BUT_DISABLED");
  if (patched_disabled_envvar && *patched_disabled_envvar) {
    log("PATCHED_BUT_DISABLED is present; disabling\n");
    disabled_from_env = true;
  }

  dopatch();
  set_enabled(true);
}

void dummy() {
  fprintf(stderr, "dummy called\n");
  fflush(stderr);
}

const void** currentGameState = (const void**)0x14233fe78;
int get_current_day_of_month() {
  const void* state = *currentGameState;
  // log("current game state: %p\n", state);
  const int currentDayOfMonthOffset = 0x1de4;
  // log("  +%02x  : %d\n", 0x1dd0, *((int32_t*)(state+0x1dd0)));
  // log("  +%02x  : %d\n", 0x1dd4, *((int32_t*)(state+0x1dd4)));
  // log("  +%02x  : %d\n", 0x1ddc, *((int32_t*)(state+0x1ddc)));
  // log("  +%02x  : %d\n", 0x1de4, *((int32_t*)(state+0x1de4)));
  int32_t current_day = *((int32_t*)(state+currentDayOfMonthOffset));
  return current_day;
}

bool disabled_from_env = false;
// :todo I need to double check if I got the boolean condition backwards
bool dynamic_should_run_hook() {
  // log("dynamic should run running\n");

  // actually day-1
  int day = 1+get_current_day_of_month();

  // bool should_run = get_enabled() && ((day % 7) != 0);
  // bool should_run = false && ((day % 5) != 0);
  // bool should_run = false;
  // log("should run (%d): %s\n", day, should_run ? "yes" : "no");
  // return true;
  bool ret = !disabled_from_env && get_enabled() && (day % 7 != 0);
  if (!ret) {
    log("checksum run on day: %d\n", day);
  }
  return ret;
}


void hook1();
void* hook1return;

void hook2();
void* hook2return;

void hook3();
void* hook3return;

#define jumplen 14
void jump_inject(void* target, void* addr) {
  uint8_t jump[jumplen] = {
        0x50, // push rax
        0x48, 0xB8,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0,
        0x58, // pop rax
  };

  // :note, I'm not sure the original was actually a problem
  // nooooooooooooooooooooooooooo.
  // alignment, you fool.
  // *(void**)(jump + 3) = addr;
  memcpy(((void*)jump)+3,&addr,sizeof(void*));
  dumphex(jump, jumplen);
  memcpy(target, jump, jumplen);
}

void do1patch(
    void* target,
    size_t patchsize,
    void* hook,
    void** hook_return
) {
  log("patching %p.\n size: %u\n hook: %p\n hook_return (addr): %p\n", target, patchsize, hook, hook_return);

  // tell the hook where to return to
  *hook_return = target+jumplen-1; // -1 is the pop rax
  log("hook return (value): %p\n", *hook_return);

  // unprotect functions
  unprotect(target, patchsize);

  // clobber target and install jump
  assert((patchsize >= jumplen) && "not enough room for jump");
  log("target (pre) ");
  dumphex(target, patchsize);

  memset(target, 0x90, patchsize);
  jump_inject(target, hook);
  log("target (post): ");
  // dumphex(target, patchsize);
  dumphex(target, patchsize);

  // reprotect functions
  protect(target, patchsize);
}


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
  log("dopatch. main exe name: %s\n", name);

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
  log("module handle:              %p\n", module);
  log("SDL_vsnprintf addr:         %p\n", thaddr);
  log("SDL_vsnprintf addr (orig):  %s\n", "0x17361a0");
  assert(((((void*)thaddr)-((void*)module)) == 0x17361a0) && "address of SDL_vsnprintf didn't match expected");

  // now dump some bytes and check they match
  // calc checksums 0x140743aa0
  // huh. I didn't realize this shit was static.
  // 0x488954241048894c is expected, but little-endian man
  void* calcchecksums_addr = (void*)0x140743aa0;
  log("calcchecksums_addr: %p\n", calcchecksums_addr);
  dumphex(calcchecksums_addr, 10);
  assert((*((uint64_t*)calcchecksums_addr) == 0x4c89481024548948) && "bytes at calcchecksums_addr did not match expected");

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

  //log("unit loop pre: %p\n", calcchecksums_unit_loop_target);
  //dumphex(calcchecksums_unit_loop_target, patchsize);

  //memset(calcchecksums_unit_loop_target, 0x90, patchsize);
  //memcpy(calcchecksums_unit_loop_target, patch, 7); // sizeof(patch));
  //                                                  //
  //log("unit loop post: %p\n", calcchecksums_unit_loop_target);
  //dumphex(calcchecksums_unit_loop_target, patchsize);

  //protect(calcchecksums_unit_loop_target, patchsize);
  // redo above using do1patch

  // :todo assert prepatch contents

  // 0x140743e4c - 0x140743e3a  = 18
  //
  // mov rax, qword [rdi+0x1c98]
  // sub rax, qword [rdi+0x1c90]
  // sar rax, 0x3
  do1patch((void*)0x140743e3a, 18, hook1, &hook1return);
  do1patch((void*)0x140744461, 18, hook2, &hook2return);



  // >>> 0x140745892 - 0x140745884
  // 14
  //
  // mov edi, dword [r14+0x21bc]
  // mov ebx, r15d
  // test edi, edi
  // jle 0x1407458f5
  //
  // skip loop addr 0x1407458f5
  do1patch((void*)0x140745884, 14, hook3, &hook3return);
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
  log("  unprotect\n");
  log("    addr: %p\n", addr);
  log("    size: %u\n", size);
  DWORD old_protect;
  DWORD new_protect = PAGE_EXECUTE_READWRITE;

  if (VirtualProtect(addr, size, new_protect, &old_protect) == 0) {
      perror("unprotect failed");
      fflush(stderr);
      exit(1);
  }
}

void protect(void* addr, size_t size) {
  log("  unprotect\n");
  log("    addr: %p\n", addr);
  log("    size: %u\n", size);
  DWORD old_protect;
  DWORD new_protect = PAGE_EXECUTE_READ;

  if (VirtualProtect(addr, size, new_protect, &old_protect) == 0) {
      perror("protect failed");
      fflush(stderr);
      exit(1);
  }

  HANDLE handle = GetCurrentProcess();
  if (FlushInstructionCache(handle, addr, size) == 0) {
      perror("flush instruction cache failed");
      fflush(stderr);
      exit(1);
  }
}
