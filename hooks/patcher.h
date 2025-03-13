#ifdef __cplusplus
#pragma once
extern "C" {
#else
// c++ explodes on atomics; it's internal, anyways
#include <stdatomic.h>
extern atomic_bool* enabled; // changes visible to threads, need to alloc
#endif

#include <stdbool.h>
#include <stddef.h>

// public
void libpatcher_init();
void on_nakama_client_join_match();
void set_enabled(bool value);
bool get_enabled();

// config
enum clientmode {official, unofficial};
enum patchmode {disabled, dynamic, always, patched_but_disabled};
typedef struct patcher_config {
  enum clientmode clientmode;
  enum patchmode patchmode;
} patcher_config;
extern patcher_config the_patcher_config;
void the_patcher_config_init();

typedef struct client_settings {
  char* host;
  int port;
  char* key;
  bool ssl;
  bool verify_tls;
} client_settings;
extern client_settings default_unofficial_client_settings;

patcher_config get_patcher_config();
client_settings get_unofficial_client_settings();

// logging
#define log(...) _log(__VA_ARGS__)
#define info(...) _log(__VA_ARGS__)
#define debug(...)
#define err(...) _log(__VA_ARGS__)
void _log(const char* fmt, ...);
#ifdef __cplusplus
}
#endif
