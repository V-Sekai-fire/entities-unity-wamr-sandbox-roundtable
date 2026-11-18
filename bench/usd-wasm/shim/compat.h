// Declarations WASI libc lacks and OpenUSD's Linux-shaped arch code names. Definitions: compat.c.
#pragma once
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif
int readdir_r(DIR *, struct dirent *, struct dirent **);
mode_t umask(mode_t);
int mkstemp(char *);
char *mkdtemp(char *);
uid_t geteuid(void);
gid_t getegid(void);
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
int dladdr(const void *, Dl_info *);
#ifdef __cplusplus
}
#endif

#include <signal.h>
typedef int pid_t_compat;
#ifdef __cplusplus
extern "C" {
#endif
#ifndef SA_SIGINFO
#define SA_SIGINFO 4
typedef unsigned long sigset_compat_t;
typedef struct { int si_signo; } siginfo_t;
#ifndef SIGBUS
#define SIGBUS 7
#endif
struct sigaction { void (*sa_handler)(int); void (*sa_sigaction)(int, siginfo_t *, void *); unsigned long sa_mask; int sa_flags; };
int sigaction(int, const struct sigaction *, struct sigaction *);
int sigemptyset(unsigned long *);
int sigaddset(unsigned long *, int);
#endif
typedef int pid_compat_t;
int fork(void);
int execv(const char *, char *const[]);
unsigned alarm(unsigned);
int waitpid(int, int *, int);
int kill(int, int);
int setsid(void);
#define WIFEXITED(s) (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s) ((s) & 0x7f)
#ifdef __cplusplus
}
#endif
