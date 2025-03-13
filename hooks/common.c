#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "common.h"

// initialization
patcher_config the_patcher_config = {
  official,
  disabled,
};
client_settings default_unofficial_client_settings = {
  "34.152.37.181",
  443,
  "default_key",
  true,
  false,
};

patcher_config get_patcher_config() { return the_patcher_config; }
client_settings get_unofficial_client_settings() { return default_unofficial_client_settings; }

int _my_argc;
char** _my_argv;

// init
__attribute__((constructor))
void on_load() {
  log("on load\n");

  cmdline_init();
  the_patcher_config_init();
  libpatcher_init();
  if (the_patcher_config.patchmode != disabled) {
    dopatch();
  }
}

// misc
void dumphex(uint8_t* start, size_t len) {
    debug("  ");
    for (int i=0; i<len; i++)
      debug("%02x ", *(start+i));
    debug("\n");
}

// cmdline
char* unparse_patchmode(enum patchmode mode) {
  char* names[] = {"disabled", "dynamic", "always", "patched_but_disabled"};
  return names[mode];
}

enum patchmode parse_patchmode(char* var) {
  if (strcmp(var, "dynamic") == 0) {
    return dynamic;
  } else if (strcmp(var, "disabled") == 0) {
    return disabled;
  } else if (strcmp(var, "always") == 0) {
    return always;
  } else if (strcmp(var, "patched_but_disabled") == 0) {
    return patched_but_disabled;
  } else {
    err("patchmode: expected mode. got %s\n", var);
    exit(1);
  }
}

void the_patcher_config_init() {
  // getopt doesn't support ignoring variables
  for (int i=0; i<_my_argc; i++) {
    // :note this parsing is adhoc and a little brittle,
    //       but its necessary to survive the argument mangling eu4
    //       does when it restarts from in-game.

    // convenience, omnibus flag
    if (strncmp(_my_argv[i], "-unofficialmp", 13) == 0
        || strncmp(_my_argv[i], "--unofficialmp", 14) == 0) {
      the_patcher_config.patchmode = always;
      the_patcher_config.clientmode = unofficial;
    // whether to apply checksum patch and whether to run it
    } else if (strncmp(_my_argv[i], "-patchmode=", 11) == 0) {
      the_patcher_config.patchmode = parse_patchmode(_my_argv[i]+11);
    } else if (strncmp(_my_argv[i], "--patchmode=", 12) == 0) {
      the_patcher_config.patchmode = parse_patchmode(_my_argv[i]+12);
    // whether to intercept default args being passed to createClient
    // and replace with our own defaults
    // only respected if the nakama_host is the default one.
    } else if (strncmp(_my_argv[i], "-useunofficialmpserver", 22) == 0
               || strncmp(_my_argv[i], "--useunofficialmpserver", 23) == 0) {
      the_patcher_config.clientmode = unofficial;
    }
  }

  log("patch mode:  %s\n", unparse_patchmode(the_patcher_config.patchmode));
  log("client mode: %s\n", the_patcher_config.clientmode == official ? "official" : "unofficial");
  if (the_patcher_config.clientmode == unofficial) {
    log("unofficial server settings:\n");
    log("  host:   %s\n", default_unofficial_client_settings.host);
    log("  port:   %d\n", default_unofficial_client_settings.port);
    log("  key:    %s\n", default_unofficial_client_settings.key);
    log("  ssl:    %d\n", default_unofficial_client_settings.ssl);
    log("  verify: %d\n", default_unofficial_client_settings.verify_tls);
  }
}

// patching
#define jumplen 14
void jump_inject(void* target, void* addr) {
  uint8_t jump[jumplen] = {
        0x50, // push rax
        0x48, 0xB8,
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0,
        0x58, // pop rax
  };

  memcpy(((void*)jump)+3,&addr,sizeof(void*));
  dumphex(jump, jumplen);
  memcpy(target, jump, jumplen);
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
  unprotect(target, patchsize);

  // clobber target and install jump
  assert((patchsize >= jumplen) && "not enough room for jump");
  debug("target (pre) ");
  dumphex(target, patchsize);

  memset(target, 0x90, patchsize);
  jump_inject(target, hook);
  debug("target (post): ");
  dumphex(target, patchsize);

  // reprotect functions
  protect(target, patchsize);
}

// dynamic checks
// get_enabled is probably unsafe to modify mid-game (not in lobby).
// I could cache the results of get_enabled after first run.
void set_enabled(bool value) {
  atomic_store(enabled, value);
}

bool get_enabled() {
  return atomic_load(enabled);
}

bool dynamic_should_run_hook() {
  switch (the_patcher_config.patchmode) {
  case dynamic:
    int day = 1+get_current_day_of_month();
    bool ret = get_enabled() && (day % 7 != 0);
    return ret;
  case always:
    return true;
  case disabled:
    assert(false && "dynamic_should_run_hook should never be called if patch wasn't applied");
    return false;
  case patched_but_disabled:
    return false;
  }
}

