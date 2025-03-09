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
void dopatch();
bool dynamic_should_run_hook();
void dummy();

extern void* hook1return;
extern void* hook2return;
extern void* hook3return;

// internal
extern bool disabled_from_env;
void set_enabled(bool value);
bool get_enabled();
void alloc_enabled();
void do1patch( void* target, size_t patchsize, void* hook, void** hook_return);

#ifdef __cplusplus
}
#endif
