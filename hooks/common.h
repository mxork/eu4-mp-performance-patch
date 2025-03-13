#ifdef __cplusplus
#pragma once
extern "C" {
#endif

#include <stdint.h>
#include "patcher.h"

// command line parsing
enum patchmode parse_patchmode(char* var);
char* unparse_patchmode(enum patchmode mode);

extern int _my_argc;
extern char** _my_argv;
void cmdline_init();
void the_patcher_config_init();

// internal
bool dynamic_should_run_hook();
extern void* hook1return;
extern void* hook2return;
extern void* hook3return;

void dopatch();
void alloc_enabled();
void do1patch( void* target, size_t patchsize, void* hook, void** hook_return);
void unprotect(void* addr, size_t size);
void protect(void* addr, size_t size);
int get_current_day_of_month();
bool dynamic_should_run_hook();
void jump_inject(void* target, void* addr);
void dumphex(uint8_t* start, size_t len);


#ifdef __cplusplus
}
#endif
