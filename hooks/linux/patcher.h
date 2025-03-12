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

#define log(...) fprintf (stderr, __VA_ARGS__)
#define info(...) fprintf (stderr, __VA_ARGS__)
// #define debug(...) log_to_file (__VA_ARGS__)
#define debug(...)
#define err(...) fprintf(stderr, __VA_ARGS__)

// internal
bool dynamic_should_run_hook();
extern void* hook1return;
extern void* hook2return;
extern void* hook3return;

void dopatch();
enum patcher_mode {normal, disabled, force_enabled, patched_but_disabled};
extern enum patcher_mode _mode;
// extern bool force_disabled;
// extern bool force_patched_but_disabled;
// extern bool force_enabled;
void set_enabled(bool value);
bool get_enabled();
void alloc_enabled();
void do1patch( void* target, size_t patchsize, void* hook, void** hook_return);
void unprotect(void* addr);
void protect(void* addr);

#ifdef __cplusplus
}
#endif
