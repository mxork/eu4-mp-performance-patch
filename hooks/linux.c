#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "common.h"

// init
void libpatcher_init() {
  info("libpatcher_init\n");
  alloc_enabled();
  alloc_speedcontroller();
}
speedcontroller* the_speedcontroller = NULL;

char* _my_argv0[32];
void cmdline_init() {
  log("cmdline init\n");
  _my_argv = _my_argv0;
  char _my_cmdline[4096];
  FILE *fp = fopen("/proc/self/cmdline", "r");
  if (!fp) return;

  size_t len = fread(_my_cmdline, 1, sizeof(_my_cmdline) - 1, fp);
  fclose(fp);

  int i=0;
  char* ptr0 = _my_cmdline;
  while (i < 32 && ptr0 < _my_cmdline+len) {
    char* ptr = ptr0;
    while (*ptr) {
      ptr++;
    }
    info("arg %d %s\n", i, ptr0);
    _my_argv[i] = ptr0;
    ptr0 = ptr+1;
    i++;
  }
  _my_argc = i;
}

void on_nakama_client_join_match() {
  // :stub
}

// logging
void _log(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

// conveniences for hooks
extern void* _ZN17CCurrentGameState11GetInstanceEv();
extern int32_t _ZNK14CGregorianDate12GetTotalDaysEv(void* date);
const int currentDateOffset = 0x1da0;
extern int32_t _ZN8NDefines5NGame23DAYS_BEHIND_LOWER_SPEEDE;
const int currentDayOfMonthOffset = 0x1db4;

int get_current_day_of_month() {
  void* state = _ZN17CCurrentGameState11GetInstanceEv();
  int32_t current_day = *((int32_t*)(state+currentDayOfMonthOffset));
  return current_day;
}

int get_current_total_days() {
  void* state = _ZN17CCurrentGameState11GetInstanceEv();
  void* current_date = (void*)(state+currentDateOffset);
  int32_t total_days = _ZNK14CGregorianDate12GetTotalDaysEv(current_date);
  return total_days;
}

int get_days_behind_lower_speed_setting() {
  return _ZN8NDefines5NGame23DAYS_BEHIND_LOWER_SPEEDE;
}

// base address we let the linker tell us about
extern void* _Z13CalcChecksumsI14CBasicChecksumEvPK10CGameStateR6CArrayIT_E(void*, void*);

// dynamic
atomic_bool* enabled;
void alloc_enabled() {
  enabled = mmap(NULL, sizeof(atomic_bool),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANON, -1, 0);
  if (enabled == MAP_FAILED) {
    err("mmap");
    exit(1);
  }
  atomic_init(enabled, true);
}

void alloc_speedcontroller() {
  the_speedcontroller = mmap(NULL, sizeof(speedcontroller),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANON, -1, 0);
  if (enabled == MAP_FAILED) {
    err("mmap");
    exit(1);
  }
}

#include <errno.h>
void sleepms(double ms) {
  usleep(ms*1000);
}
double nowms() {
  // return 0.0;
  struct timespec ts;
  int err = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (err) {
    log("now err: %d", errno);
    return 0.0;
  }
  return (((double)ts.tv_sec) * 1000.0L) + (((double)ts.tv_nsec) / 1000000.0L);
}

//
void hook1();
void* hook1return;

void hook2();
void* hook2return;

void hook3();
void* hook3return;

void hook4();
void* hook4return;
void* hook4returnalt;

void unprotect(void* addr, size_t size) {
  size_t pagesize = sysconf(_SC_PAGESIZE);
  void* page_start = (void*)((uintptr_t)addr & ~(pagesize - 1));
  size_t region_size = size + (addr-page_start);
  debug("  unprotect\n");
  debug("    addr: %p\n", addr);
  debug("    size: %ld\n", size);
  if (mprotect(page_start, region_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      perror("unprotect failed");
      fflush(stderr);
      exit(1);
  }
}

void protect(void* addr, size_t size) {
  size_t pagesize = sysconf(_SC_PAGESIZE);
  void* page_start = (void*)((uintptr_t)addr & ~(pagesize - 1));
  size_t region_size = size + (addr-page_start);
  debug("  protect\n");
  debug("    addr: %p\n", addr);
  debug("    size: %ld\n", size);
  if (mprotect(page_start, region_size, PROT_READ | PROT_EXEC) != 0) {
      perror("protect failed");
      fflush(stderr);
      exit(1);
  }
}

// CalcChecksums<CBasicChecksum> is at:
//   0x13c964e
//
// >>> 0x13c96df - 0x13c96cd
// 18
// >>> 0x13c9f02 - 0x13c9ef0
// 18
// >>> 0x13cbac1 - 0x13cbab5
// 12 # alternate splice end; do not use
// >>> 0x13cbac7 - 0x13cbab5
// 18
void dopatch() {
  log("applying patch\n");
  void* base0 = (void*) 0x13c964e;
  void* base = _Z13CalcChecksumsI14CBasicChecksumEvPK10CGameStateR6CArrayIT_E;

  char* gitchecksum = base+(((void*)0x271ed8b)-(void*)base0);
  char* gitchecksumexpected = "  f2de20fd7cc5d735418b384c759d491d\n";
  if (strncmp(gitchecksum, gitchecksumexpected, 64) != 0) {
    log("git checksum does not match expected.. ");
    log("refusing to apply patch\n");
    the_patcher_config.patchmode = disabled;
    return;
  }

  // :todo use this
  // void* module_base = (void*)(base - base0);
  do1patch(base+(((void*)0x13c96cd)-base0), 18, hook1, &hook1return);
  do1patch(base+(((void*)0x13c9ef0)-base0), 18, hook2, &hook2return);
  do1patch(base+(((void*)0x13cbab5)-base0), 18, hook3, &hook3return);
  do1patch(base+(((void*)0x1780c32)-base0), 17, hook4, &hook4return);
  hook4returnalt = (void*)0x1780d79+(base-base0);
}

