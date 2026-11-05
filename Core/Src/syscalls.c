/*
 * Minimal newlib system call stubs. Console output goes through con_puts(),
 * not stdio, so none of these do real I/O.
 */
#include <errno.h>
#include <sys/stat.h>

int _close(int fd)
{
  return -1;
}

int _fstat(int fd, struct stat *st)
{
  st->st_mode = S_IFCHR;
  return 0;
}

int _getpid(void)
{
  return 1;
}

int _isatty(int fd)
{
  return 1;
}

int _kill(int pid, int sig)
{
  errno = EINVAL;
  return -1;
}

int _lseek(int fd, int ptr, int dir)
{
  return 0;
}

int _read(int fd, char *ptr, int len)
{
  return 0;
}

int _write(int fd, char *ptr, int len)
{
  return len;
}
