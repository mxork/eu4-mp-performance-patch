#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>

#include "patcher.h"

__attribute__((constructor))
void on_load() {
  libpatcher_init();
}

enum patcher_mode _mode = normal;
void libpatcher_init() {
  info("libpatcher_init\n");
  alloc_enabled();

  char* envvar = getenv("PATCHER_MODE");
  if (envvar && *envvar) {
    if (strcmp(envvar, "normal") == 0) {
      info("mode: normal\n");
      _mode = normal;
    } else if (strcmp(envvar, "disabled") == 0) {
      info("mode: disabled\n");
      _mode = normal;
      _mode = disabled;
      return;
    } else if (strcmp(envvar, "force_enabled") == 0) {
      info("mode: force enabled\n");
      _mode = force_enabled;
    } else if (strcmp(envvar, "patched_but_disabled") == 0) {
      info("mode: patched but disabled\n");
      _mode = patched_but_disabled;
    } else {
      err("PATCHER_MODE: expected mode. got %s\n", envvar);
      exit(1);
    }
  }

  dopatch();
}

void on_nakama_client_join_match() {
  if (_mode == normal) {
    info("patcher normal mode: on nakama client join match, so enabling patch\n");
    set_enabled(true);
  }
}

// conveniences for hooks
extern void* _ZN17CCurrentGameState11GetInstanceEv();
const int currentDayOfMonthOffset = 0x1db4;

int get_current_day_of_month() {
  void* state = _ZN17CCurrentGameState11GetInstanceEv();
  int32_t current_day = *((int32_t*)(state+currentDayOfMonthOffset));
  return current_day;
}

// get_enabled is probably unsafe to modify mid-game (not in lobby).
// I could cache the results of get_enabled after first run.
bool dynamic_should_run_hook() {
  switch (_mode) {
  case normal:
    int day = 1+get_current_day_of_month();
    bool ret = get_enabled() && (day % 7 != 0);
    return ret;
  case force_enabled:
    return true;
  case disabled:
    assert(false && "dynamic_should_run_hook should never be called if patch wasn't applied");
    return false;
  case patched_but_disabled:
    return false;
  }
}

// base address we let the linker tell us about
extern void* _Z13CalcChecksumsI14CBasicChecksumEvPK10CGameStateR6CArrayIT_E(void*, void*);

// dynamic
atomic_bool* enabled;

void hook1();
void* hook1return;

void hook2();
void* hook2return;

void hook3();
void* hook3return;

void unprotect(void* addr) {
  size_t pagesize = sysconf(_SC_PAGESIZE);
  void* page_start = (void*)((uintptr_t)addr & ~(pagesize - 1));
  debug("  unprotect\n");
  debug("    addr: %p\n", addr);
  debug("    page: %p\n", page_start);
  if (mprotect(page_start, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      perror("unprotect failed");
      fflush(stderr);
      exit(1);
  }
}

void protect(void* addr) {
  size_t pagesize = sysconf(_SC_PAGESIZE);
  void* page_start = (void*)((uintptr_t)addr & ~(pagesize - 1));
  debug("  protect\n");
  debug("    addr: %p\n", addr);
  debug("    page: %p\n", page_start);
  if (mprotect(page_start, pagesize, PROT_READ | PROT_EXEC) != 0) {
      perror("protect failed");
      fflush(stderr);
      exit(1);
  }
}

// ensure nstolen falls on instruction boundary
// #define nstolen 17
// should just use sizeof, here
#define jumplen 13
void jump_inject(void* target, void* addr) {
  uint8_t jump[jumplen] = {
        0x48, 0xB8,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0,
        0x58, // pop rax
  };

  *(uintptr_t*)(jump + 2) = (uintptr_t)addr;
  memcpy(target, jump, jumplen);
}

void dumphex(uint8_t* start, size_t len) {
    debug("  ");
    for (int i=0; i<len; i++)
      debug("%02x ", *(start+i));
    debug("\n");
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
  do1patch(base+(((void*)0x13c96cd)-base0), 18, hook1, &hook1return);
  do1patch(base+(((void*)0x13c9ef0)-base0), 18, hook2, &hook2return);
  do1patch(base+(((void*)0x13cbab5)-base0), 18, hook3, &hook3return);
}

void alloc_enabled() {
  enabled = mmap(NULL, sizeof(atomic_bool),
      PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_ANON, -1, 0);
  if (enabled == MAP_FAILED) {
    err("mmap");
    _exit(1);
  }
  atomic_init(enabled, true);
}

void set_enabled(bool value) {
  atomic_store(enabled, value);
}

bool get_enabled() {
  return atomic_load(enabled);
}

// this patch clobbers the area [target, target+patchsize)
// and installs jump to the hook in its place.
//
// the hooks are responsible for running any clobbered
// instructions if necessary.
//
// the hooks unconditionally push rax and jump back to target+jumplen-1
// when they are done.
void do1patch(
    void* target,
    size_t patchsize,
    void* hook,
    void** hook_return
) {
  debug("patching %p.\n size: %ld\n hook: %p\n hook_return (addr): %p\n", target, patchsize, hook, hook_return);

  // tell the hook where to return to
  *hook_return = target+jumplen-1;
  debug("hook return (value): %p\n", *hook_return);

  // unprotect functions
  unprotect(target);

  // clobber target and install jump
  assert((patchsize >= jumplen) && "not enough room for jump");
  debug("target (pre) ");
  dumphex(target, patchsize);

  memset(target, 0x90, patchsize);
  jump_inject(target, hook);
  debug("target (post): ");
  dumphex(target, patchsize);

  // reprotect functions
  protect(target);
}












