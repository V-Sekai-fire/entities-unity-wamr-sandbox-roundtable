#include "compat.h"
#include <errno.h>
int readdir_r(DIR *d, struct dirent *e, struct dirent **r) { (void)e; errno = 0; *r = readdir(d); return 0; }
mode_t umask(mode_t m) { return m; }
int mkstemp(char *t) { (void)t; errno = ENOSYS; return -1; }
char *mkdtemp(char *t) { (void)t; errno = ENOSYS; return 0; }
uid_t geteuid(void) { return 0; }
gid_t getegid(void) { return 0; }
int dladdr(const void *a, Dl_info *i) { (void)a; (void)i; return 0; }
int sigaction(int s, const struct sigaction *a, struct sigaction *o) { (void)s; (void)a; (void)o; return -1; }
int sigemptyset(unsigned long *m) { *m = 0; return 0; }
int sigaddset(unsigned long *m, int s) { *m |= 1ul << (s & 31); return 0; }
int fork(void) { errno = ENOSYS; return -1; }
int execv(const char *p, char *const a[]) { (void)p; (void)a; errno = ENOSYS; return -1; }
unsigned alarm(unsigned s) { (void)s; return 0; }
int waitpid(int p, int *st, int o) { (void)p; (void)st; (void)o; errno = ECHILD; return -1; }
int kill(int p, int s) { (void)p; (void)s; errno = ENOSYS; return -1; }
int setsid(void) { errno = ENOSYS; return -1; }
/* Exceptions are compiled out (-fignore-exceptions): a throw is fatal, as in a -fno-exceptions build. */
#include <stdio.h>
void *__cxa_allocate_exception(unsigned long n) { return malloc(n ? n : 1); }
void __cxa_throw(void *p, void *t, void (*d)(void *)) { (void)p; (void)t; (void)d; fprintf(stderr, "usd.wasm: C++ exception thrown, type %s\n", t ? ((const char *const *)t)[1] : "?"); abort(); }
void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
int dlclose(void *h) { (void)h; return 0; }
char *dlerror(void) { return (char *)"dynamic loading is not available in wasm"; }
int posix_madvise(void *a, unsigned long l, int adv) { (void)a; (void)l; (void)adv; return 0; }
