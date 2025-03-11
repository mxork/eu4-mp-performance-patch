#ifdef __cplusplus
#pragma once
extern "C" {
#else
#include <stdatomic.h>
extern atomic_bool* enabled; // changes visible to threads, need to alloc
#endif

#include <stdbool.h>
#include <stddef.h>

// public
void libpatcher_init();
void on_nakama_client_join_match();
void _log(const char* format, ...);

#define log(...) _log (__VA_ARGS__)
#define info(...) _log (__VA_ARGS__)
#define debug(...) _log (__VA_ARGS__)
// #define debug(...)
#define err(...) _log (__VA_ARGS__)

// internal
bool dynamic_should_run_hook();
extern void* module_base;
extern void* hook1return;
extern void* hook2return;
extern void* hook3return;
extern void* hook3returnalt;

void dopatch();
enum patcher_mode {normal, disabled, force_enabled, patched_but_disabled};
extern enum patcher_mode _mode;
void set_enabled(bool value);
bool get_enabled();
void do1patch( void* target, size_t patchsize, void* hook, void** hook_return);
void unprotect(void* addr, size_t size);
void protect(void* addr, size_t size);

#ifdef __cplusplus
}
#endif
