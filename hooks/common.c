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
enum speedcontrolmode the_speedcontrolmode = speedcontrol_off;

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
  if (the_speedcontrolmode == speedcontrol_on) {
    speedcontroller_set_defaults();
  }
}

void speedcontroller_set_defaults() {
  the_speedcontroller->last_day = -1;
  the_speedcontroller->oldest_ping_day = -1;
  the_speedcontroller->oldest_ping_day_player = -1;
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
    } else if (strncmp(_my_argv[i], "-fastmp", 7) == 0
               || strncmp(_my_argv[i], "--fastmp", 8) == 0) {
      the_patcher_config.patchmode = always;
    } else if (strncmp(_my_argv[i], "-patchmode=", 11) == 0) {
    // whether to apply checksum patch and whether to run it
    } else if (strncmp(_my_argv[i], "-patchmode=", 11) == 0) {
      // hopefully deal with the extra space eu4 tags on
      char* the_arg = _my_argv[i]+11;
      long n = strcspn(the_arg, " ");
      the_arg[n] = '\0';
      the_patcher_config.patchmode = parse_patchmode(_my_argv[i]+11);
    } else if (strncmp(_my_argv[i], "--patchmode=", 12) == 0) {
      char* the_arg = _my_argv[i]+12;
      long n = strcspn(the_arg, " ");
      the_arg[n] = '\0';
      the_patcher_config.patchmode = parse_patchmode(_my_argv[i]+12);
    // whether to intercept default args being passed to createClient
    // and replace with our own defaults
    // only respected if the nakama_host is the default one.
    } else if (strncmp(_my_argv[i], "-useunofficialmpserver", 22) == 0
               || strncmp(_my_argv[i], "--useunofficialmpserver", 23) == 0) {
      the_patcher_config.clientmode = unofficial;
    }
    // :todo
    // else if (strncmp(_my_argv[i], "-speedcontrol-throttle-start=", 29) == 0
    //            || strncmp(_my_argv[i], "--speedcontrol-throttle-start=", 30) == 0) {
    //   char* the_arg = _my_argv[i];
    //   long n = strcspn(the_arg, " ");
    //   the_arg[n] = '\0';
    //   char* the_number = strchr(the_arg, '=')+1;
    //   long value = strtol(the_number, 0, 10);
    //   log("throttle start: %ld\n", value);
    // }
    // else if (strncmp(_my_argv[i], "-speedcontrol-throttle-peak=", 28) == 0
    //            || strncmp(_my_argv[i], "--speedcontrol-throttle-peak=", 29) == 0) {
    //   char* the_arg = _my_argv[i];
    //   long n = strcspn(the_arg, " ");
    //   the_arg[n] = '\0';
    //   char* the_number = strchr(the_arg, '=')+1;
    //   long value = strtol(the_number, 0, 10);
    //   log("throttle peak: %ld\n", value);
    // }
    // else if (strncmp(_my_argv[i], "-speedcontrol-throttle-amount=", 30) == 0
    //            || strncmp(_my_argv[i], "--speedcontrol-throttle-amount=", 31) == 0) {
    //   char* the_arg = _my_argv[i];
    //   long n = strcspn(the_arg, " ");
    //   the_arg[n] = '\0';
    //   char* the_number = strchr(the_arg, '=')+1;
    //   long value = strtol(the_number, 0, 10);
    //   log("throttle peak: %ld\n", value);
    // }
    else if (strncmp(_my_argv[i], "-speedcontrol", 13) == 0
               || strncmp(_my_argv[i], "--speedcontrol", 14) == 0) {
      the_speedcontrolmode = speedcontrol_on;
    }
  }

  if (the_patcher_config.patchmode == disabled && the_speedcontrolmode == speedcontrol_on) {
    log("speed control requested, but patch is disabled.\n");
    log("  -> setting patch mode to patched_but_disabled\n");
    the_patcher_config.patchmode = patched_but_disabled;
  }

  log("patch mode:          %s\n", unparse_patchmode(the_patcher_config.patchmode));
  log("client mode:         %s\n", the_patcher_config.clientmode == official ? "official" : "unofficial");
  log("speed control mode:  %s\n", the_speedcontrolmode == speedcontrol_on ? "on" : "off");
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

#define max(a,b) ((a) > (b) ? (a) : (b))

// this is called from hook4 (clientpingexecute), and tracks the
// oldest (most behind) player, and how far behind they are.
//
// this feature is more intertwined with the binary, in particular the
// need for get_current_total_days and offset wrangling to get the player
// id into this function.
//
// I tried simpler approaches, but, in the end, this was the minimal
// mechanism that just feels *good* and automagical.
//
// clientpingexecute is only ever called on the host, so we can use the
// "-1" fields to guard against speedcontrolling on clients.
//
// :todo I'm almost 100% sure that everything is safely reinitialized on game
//       reset, because eu4 restarts. worth checking.
void handle_clientping(int days_behind, int game_day, int ping_day, int player_id) {
  speedcontroller* c = the_speedcontroller;
  if (player_id != 1) {
    debug("handle_clientping:\n");
    debug("  current day:   %d\n", get_current_total_days());
    debug("  game day:      %d\n", game_day);
    debug("  ping day:      %d\n", ping_day);
    debug("  days behind:   %d\n", days_behind);
    debug("  player id:     %d\n", player_id);
  }

  if (c->oldest_ping_day_player == -1) {
    c->oldest_ping_day = ping_day;
    c->oldest_ping_day_player = player_id;
    return;
  }

  if (ping_day < c->oldest_ping_day) {
    c->oldest_ping_day = ping_day;
    c->oldest_ping_day_player = player_id;
    return;
  }

  if (player_id == c->oldest_ping_day_player && ping_day > c->oldest_ping_day) {
    c->oldest_ping_day = ping_day;
    c->oldest_ping_day_player = player_id;
    return;
  }
}

void speed_control() {
  // :todo check am host?
  if (the_speedcontrolmode == speedcontrol_on) {
    speedcontroller* c = the_speedcontroller;
    // :note this guards against running before any pings
    //       received, but also prevents speedcontrol on
    //       clients.
    if (c->oldest_ping_day_player == -1) {
      return;
    }

    int day = get_current_day_of_month();
    if (c->last_day == -1) {
      c->last_day = day;
    } else {
      // only run on first call of day
      if (day != c->last_day) {
        c->last_day = day;

        int days_behind_lower_speed = get_days_behind_lower_speed_setting();
        double days_behind_penalty = 0.;
        int days_behind = get_current_total_days() - c->oldest_ping_day;

        // :hack this branch is responsible for ignoring players on disconnect
        //       will sort itself out on next clientping, and we've already
        //       triggered (or are going to trigger) the main lag handler.
        if (days_behind > get_days_behind_lower_speed_setting()) {
          debug("exceeded days behind limit, probably due to client disconnect\n");
          debug("resetting speedcontroller\n");
          days_behind = 0;
          c->oldest_ping_day = -1;
          c->oldest_ping_day_player = -1;
        }

        // :note magic constants
        double max_penalty = 1200.;
        int day_buffer = days_behind_lower_speed/2;
        int threshold = days_behind_lower_speed-day_buffer;
        if (days_behind >= threshold) {
          double k = ((double)(days_behind - threshold))/((double)day_buffer);
          // just, being careful
          if (k > 1.0) {
            k = 1.0;
          }
          // :todo consider other powers of k
          days_behind_penalty = max_penalty*k*k;
        }

        double tosleep = days_behind_penalty;
        debug("speed control:\n");
        debug("  oldest player: %d\n", c->oldest_ping_day_player);
        debug("  oldest ping:   %d\n", c->oldest_ping_day);
        debug("  current day:   %d\n", get_current_total_days());
        debug("  tosleep:       %ld\n", (long) tosleep);
        if (tosleep > 5) {
          sleepms(tosleep);
        }
      }
    }
  }
}

bool dynamic_should_run_hook() {
  speed_control();
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

