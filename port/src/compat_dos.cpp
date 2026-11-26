// compat_dos.cpp - the POSIX functions that the layer calls and that the
// C library of 32-bit DOS (picolibc in dos-sdk's i386-msdos sysroot) does
// not have. port/compat/dos declares them.
//
// Some functions work as on Unix: pread and pwrite, fsync, fcntl for the
// usual requests, poll on files, mmap over the heap, shared memory over
// files in the temporary folder, uname, ftime, and the address text
// functions.
// DOS has no network, no other users, no dynamic loader and no child
// processes started this way: those functions fail with the usual error.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(__MSDOS__) && defined(__i386__)

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <i86.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/utsname.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>

namespace {

// One mapping that mmap made: a block of the heap.
struct Mapping {
  size_t len;
  int fd;          // the file to write back (MAP_SHARED with PROT_WRITE), or -1
  off_t offset;    // where the mapping starts in the file
  std::string shm; // the shared memory object it shows, or empty
};

// A shared memory object: one block for all mappings of the name.
struct Shared {
  void* data = nullptr;
  size_t len = 0;
  int maps = 0;
};

std::mutex g_lock;
// Returns the table of the mmap regions, by address.
std::map<void*, Mapping>& Mappings() {
  static std::map<void*, Mapping> m;
  return m;
}
// The descriptors that shm_open gave, by descriptor.
std::map<int, std::string>& ShmFds() {
  static std::map<int, std::string> m;
  return m;
}
// Returns the table of the shared memory objects, by name.
std::map<std::string, Shared>& ShmObjects() {
  static std::map<std::string, Shared> m;
  return m;
}

// Gives the file of a shared memory object: a name of 8 characters (a
// hash of the object's name) in the TEMP folder, or in the current
// folder when TEMP is not set.
std::string ShmPath(const char* name) {
  unsigned long h = 5381;
  for (const char* p = name; *p; p++) h = h * 33 + (unsigned char)*p;
  char file[16];
  snprintf(file, sizeof file, "%08lX.SHM", h & 0xFFFFFFFFul);
  const char* dir = getenv("TEMP");
  if (!dir || !*dir) dir = getenv("TMP");
  std::string path = dir && *dir ? dir : ".";
  if (path.back() != '\\' && path.back() != '/') path += '\\';
  return path + file;
}

// Calls DOS (INT 21h) with the registers r.
int Int21(union REGS* r) { return int386(0x21, r, r); }

// Makes binary the default mode of open and fopen, before other static
// constructors run. The port reads and writes files as on Unix, where no
// file has a text mode; in text mode DOS changes CR LF and stops at ^Z.
__attribute__((constructor(101))) void BinaryFileMode() { _fmode = O_BINARY; }

}  // namespace

extern "C" {

// Reads at an offset; the file position does not change.
ssize_t pread(int fd, void* buf, size_t n, off_t offset) {
  off_t old = lseek(fd, 0, SEEK_CUR);
  if (old < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
  ssize_t r = read(fd, buf, n);
  int e = errno;
  lseek(fd, old, SEEK_SET);
  errno = e;
  return r;
}

// Writes at an offset; the file position does not change.
ssize_t pwrite(int fd, const void* buf, size_t n, off_t offset) {
  off_t old = lseek(fd, 0, SEEK_CUR);
  if (old < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
  ssize_t r = write(fd, buf, n);
  int e = errno;
  lseek(fd, old, SEEK_SET);
  errno = e;
  return r;
}

// Writes the buffers of DOS for the file to the disk (INT 21h, AH=68h).
int fsync(int fd) {
  union REGS r;
  r.w.ax = 0x6800;
  r.w.bx = (unsigned short)fd;
  Int21(&r);
  return 0;
}

// Does the fcntl requests that the layer uses. DOS keeps no descriptor
// flags, so F_GETFL gives O_RDWR and F_SETFL keeps nothing.
int fcntl(int fd, int cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  int arg = va_arg(ap, int);
  va_end(ap);
  switch (cmd) {
    case F_DUPFD: {
      int fds[64], n = 0, r = -1;
      while (n < 64) {
        int d = dup(fd);
        if (d < 0) break;
        if (d >= arg) {
          r = d;
          break;
        }
        fds[n++] = d;
      }
      while (n > 0) close(fds[--n]);
      return r;
    }
    case F_GETFD:
    case F_SETFD:
    case F_SETFL:
      return 0;
    case F_GETFL:
      return O_RDWR;
  }
  errno = EINVAL;
  return -1;
}

// No other program uses the file: the lock always succeeds.
int flock(int, int) { return 0; }

// The I/O control requests: none is available on DOS.
int ioctl(int, unsigned long, void*) {
  errno = ENOTTY;
  return -1;
}

// Files and the console are always ready. When no descriptor is given,
// waits for the time-out while the other threads run.
int poll(struct pollfd* fds, nfds_t n, int timeout) {
  int ready = 0;
  for (nfds_t i = 0; i < n; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0) continue;
    fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
    if (fds[i].revents) ready++;
  }
  if (ready == 0 && timeout > 0) usleep((useconds_t)timeout * 1000);
  return ready;
}

// The time and the time zone, for old programs. The C library does not
// have ftime.
int ftime(struct timeb* tb) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  tb->time = tv.tv_sec;
  tb->millitm = (unsigned short)(tv.tv_usec / 1000);
  tb->timezone = 0;
  tb->dstflag = 0;
  return 0;
}

// One user owns everything.
int chown(const char*, uid_t, gid_t) { return 0; }
// Does nothing: DOS files have no owner.
int fchown(int, uid_t, gid_t) { return 0; }
// Returns 0: DOS has one user.
uid_t geteuid(void) { return 0; }
// Returns 0: DOS has one group.
gid_t getegid(void) { return 0; }
// Returns 0: DOS has no extra groups.
int getgroups(int, gid_t*) { return 0; }
// Returns the process ID: DOS has no sessions.
pid_t getsid(pid_t) { return getpid(); }

// The computer's name: COMPUTERNAME from the environment, else "PC".
int gethostname(char* name, size_t len) {
  const char* n = getenv("COMPUTERNAME");
  if (!n || !*n) n = "PC";
  if (strlen(n) + 1 > len) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(name, n);
  return 0;
}

// MS-DOS and its version (INT 21h, AH=30h).
int uname(struct utsname* u) {
  union REGS r;
  r.w.ax = 0x3000;
  Int21(&r);
  memset(u, 0, sizeof *u);
  strcpy(u->sysname, "MS-DOS");
  gethostname(u->nodename, sizeof u->nodename);
  snprintf(u->release, sizeof u->release, "%u.%02u", r.h.al, r.h.ah);
  strcpy(u->version, u->release);
  strcpy(u->machine, "i386");
  return 0;
}

// No signal comes from outside the program: waits forever while the
// other threads run.
int pause(void) {
  for (;;) sched_yield();
}

// Uses signal: only sa_handler counts.
int sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
  void (*prev)(int) = SIG_DFL;
  if (act) {
    prev = signal(sig, act->sa_handler);
    if (prev == SIG_ERR) return -1;
  } else {
    prev = signal(sig, SIG_IGN);
    signal(sig, prev);
  }
  if (old) {
    memset(old, 0, sizeof *old);
    old->sa_handler = prev;
  }
  return 0;
}

// The serial port settings stay in the structure.
void cfmakeraw(struct termios* t) {
  t->c_iflag = 0;
  t->c_oflag = 0;
  t->c_lflag = 0;
  t->c_cflag = (t->c_cflag & ~(tcflag_t)(CSIZE | PARENB)) | CS8;
}
// Returns the input speed in T.
speed_t cfgetispeed(const struct termios* t) { return t->c_ispeed; }
// Returns the output speed in T.
speed_t cfgetospeed(const struct termios* t) { return t->c_ospeed; }
// Sets the input speed in T.
int cfsetispeed(struct termios* t, speed_t s) {
  t->c_ispeed = s;
  return 0;
}
// Sets the output speed in T.
int cfsetospeed(struct termios* t, speed_t s) {
  t->c_ospeed = s;
  return 0;
}
// Fails: DOS has no terminal devices.
int tcdrain(int) {
  errno = ENOTTY;
  return -1;
}
// Fails: DOS has no terminal devices.
int tcflow(int, int) {
  errno = ENOTTY;
  return -1;
}
// Fails: DOS has no terminal devices.
int tcflush(int, int) {
  errno = ENOTTY;
  return -1;
}

// Maps memory: a block of the heap. An anonymous mapping is zeros. A
// file mapping holds a copy of the file; with MAP_SHARED and PROT_WRITE,
// msync and munmap write it back. All mappings of one shared memory
// object (shm_open) share one block.
void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
  (void)addr;
  if (len == 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }
  std::lock_guard<std::mutex> g(g_lock);
  if (!(flags & MAP_ANONYMOUS) && ShmFds().count(fd)) {
    const std::string& name = ShmFds()[fd];
    Shared& s = ShmObjects()[name];
    if (!s.data) {
      s.len = (size_t)offset + len;
      s.data = calloc(1, s.len);
      if (!s.data) {
        errno = ENOMEM;
        return MAP_FAILED;
      }
      pread(fd, s.data, s.len, 0);
    } else if ((size_t)offset + len > s.len) {
      errno = EINVAL;
      return MAP_FAILED;
    }
    s.maps++;
    char* p = (char*)s.data + offset;
    Mappings()[p] = Mapping{len, -1, offset, name};
    return p;
  }
  void* p = calloc(1, len);
  if (!p) {
    errno = ENOMEM;
    return MAP_FAILED;
  }
  int back = -1;
  if (!(flags & MAP_ANONYMOUS)) {
    if (pread(fd, p, len, offset) < 0) {
      free(p);
      return MAP_FAILED;
    }
    if ((flags & MAP_SHARED) && (prot & PROT_WRITE)) back = dup(fd);
  }
  Mappings()[p] = Mapping{len, back, offset, std::string()};
  return p;
}

// Writes a shared file mapping back to its file.
int msync(void* addr, size_t, int) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Mappings().find(addr);
  if (it == Mappings().end()) {
    errno = EINVAL;
    return -1;
  }
  if (it->second.fd >= 0) pwrite(it->second.fd, addr, it->second.len, it->second.offset);
  return 0;
}

// Removes a mapping, after msync.
int munmap(void* addr, size_t len) {
  msync(addr, len, 0);
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Mappings().find(addr);
  if (it == Mappings().end()) {
    errno = EINVAL;
    return -1;
  }
  Mapping m = it->second;
  Mappings().erase(it);
  if (!m.shm.empty()) {
    Shared& s = ShmObjects()[m.shm];
    if (--s.maps == 0) {
      free(s.data);
      ShmObjects().erase(m.shm);
    }
    return 0;
  }
  if (m.fd >= 0) close(m.fd);
  free(addr);
  return 0;
}

// The memory has no protection on DOS.
int mprotect(void*, size_t, int) { return 0; }

// Opens a shared memory object: a file in the TEMP folder.
int shm_open(const char* name, int oflag, mode_t mode) {
  int fd = open(ShmPath(name).c_str(), oflag | O_BINARY, mode);
  if (fd >= 0) {
    std::lock_guard<std::mutex> g(g_lock);
    ShmFds()[fd] = name;
  }
  return fd;
}

// Removes the file of a shared memory object.
int shm_unlink(const char* name) { return unlink(ShmPath(name).c_str()); }

// The program is one executable: dlopen(NULL) gives it, and its symbols
// are not available by name.
static const char* g_dlerror;
// Fails: DOS has no dynamic loader.
void* dlopen(const char* path, int) {
  if (!path) return (void*)&g_dlerror;
  g_dlerror = "DOS has no shared libraries";
  return NULL;
}
// Fails: DOS has no dynamic loader.
void* dlsym(void*, const char*) {
  g_dlerror = "symbol not found";
  return NULL;
}
// Does nothing: dlopen gives no handles.
int dlclose(void*) { return 0; }
// Returns the text of the last dynamic loader error.
char* dlerror(void) {
  const char* e = g_dlerror;
  g_dlerror = NULL;
  return (char*)e;
}
// Returns 0: there is no information about the address.
int dladdr(const void*, Dl_info*) { return 0; }

// DOS starts no programs this way.
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* a) {
  *a = NULL;
  return 0;
}
// Does nothing: the actions have no data.
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t*) { return 0; }
// Does nothing: posix_spawn always fails.
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t*, int, const char*, int, mode_t) { return 0; }
// Does nothing: posix_spawn always fails.
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t*, int, int) { return 0; }
// Does nothing: posix_spawn always fails.
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t*, int) { return 0; }
int posix_spawn(pid_t*, const char*, const posix_spawn_file_actions_t*, const posix_spawnattr_t*,
                char* const[], char* const[]) {
  return ENOSYS;
}
int posix_spawnp(pid_t*, const char*, const posix_spawn_file_actions_t*, const posix_spawnattr_t*,
                 char* const[], char* const[]) {
  return ENOSYS;
}

// DOS has no network: the sockets fail.
int h_errno;
// Fails with EAFNOSUPPORT: DOS has no network.
int socket(int, int, int) {
  errno = EAFNOSUPPORT;
  return -1;
}
// Fails with EAFNOSUPPORT: DOS has no network.
int socketpair(int, int, int, int*) {
  errno = EAFNOSUPPORT;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int bind(int, const struct sockaddr*, socklen_t) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int listen(int, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int accept(int, struct sockaddr*, socklen_t*) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int accept4(int, struct sockaddr*, socklen_t*, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int connect(int, const struct sockaddr*, socklen_t) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t send(int, const void*, size_t, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t recv(int, void*, size_t, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t sendto(int, const void*, size_t, int, const struct sockaddr*, socklen_t) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t recvfrom(int, void*, size_t, int, struct sockaddr*, socklen_t*) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t sendmsg(int, const struct msghdr*, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
ssize_t recvmsg(int, struct msghdr*, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int shutdown(int, int) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int getsockopt(int, int, int, void*, socklen_t*) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int setsockopt(int, int, int, const void*, socklen_t) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int getsockname(int, struct sockaddr*, socklen_t*) {
  errno = ENOTSOCK;
  return -1;
}
// Fails with ENOTSOCK: DOS has no network.
int getpeername(int, struct sockaddr*, socklen_t*) {
  errno = ENOTSOCK;
  return -1;
}

// Reads an IPv4 address in dotted form.
int inet_aton(const char* text, struct in_addr* addr) {
  unsigned long part[4];
  int n = 0;
  const char* p = text;
  while (n < 4) {
    char* end;
    if (*p < '0' || *p > '9') return 0;
    part[n++] = strtoul(p, &end, 10);
    p = end;
    if (*p != '.') break;
    p++;
  }
  if (*p || n != 4) return 0;
  for (int i = 0; i < 4; i++)
    if (part[i] > 255) return 0;
  addr->s_addr = htonl((in_addr_t)(part[0] << 24 | part[1] << 16 | part[2] << 8 | part[3]));
  return 1;
}

// Reads an IPv4 address, or gives INADDR_NONE.
in_addr_t inet_addr(const char* text) {
  struct in_addr a;
  return inet_aton(text, &a) ? a.s_addr : INADDR_NONE;
}

// Writes an IPv4 address in dotted form, in a static buffer.
char* inet_ntoa(struct in_addr addr) {
  static char buf[16];
  in_addr_t a = ntohl(addr.s_addr);
  snprintf(buf, sizeof buf, "%u.%u.%u.%u", (unsigned)(a >> 24), (unsigned)(a >> 16) & 255,
           (unsigned)(a >> 8) & 255, (unsigned)a & 255);
  return buf;
}

// Writes an IPv4 address in dotted form. IPv6 is not available.
const char* inet_ntop(int family, const void* addr, char* buf, socklen_t size) {
  if (family != AF_INET) {
    errno = EAFNOSUPPORT;
    return NULL;
  }
  struct in_addr a;
  memcpy(&a, addr, sizeof a);
  const char* t = inet_ntoa(a);
  if (strlen(t) + 1 > size) {
    errno = ENOSPC;
    return NULL;
  }
  strcpy(buf, t);
  return buf;
}

// Reads an IPv4 address in dotted form. IPv6 is not available.
int inet_pton(int family, const char* text, void* addr) {
  if (family != AF_INET) {
    errno = EAFNOSUPPORT;
    return -1;
  }
  struct in_addr a;
  if (!inet_aton(text, &a)) return 0;
  memcpy(addr, &a, sizeof a);
  return 1;
}

// Finds only addresses in dotted form: there is no resolver.
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                struct addrinfo** res) {
  struct in_addr a;
  a.s_addr = htonl(INADDR_LOOPBACK);
  if (node && !inet_aton(node, &a)) return EAI_NONAME;
  if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET) return EAI_FAMILY;
  unsigned long port = service ? strtoul(service, NULL, 10) : 0;
  struct addrinfo* ai = (struct addrinfo*)calloc(1, sizeof *ai + sizeof(struct sockaddr_in));
  if (!ai) return EAI_MEMORY;
  struct sockaddr_in* sin = (struct sockaddr_in*)(ai + 1);
  sin->sin_family = AF_INET;
  sin->sin_port = htons((uint16_t)port);
  sin->sin_addr = a;
  ai->ai_family = AF_INET;
  ai->ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
  ai->ai_protocol = hints ? hints->ai_protocol : 0;
  ai->ai_addr = (struct sockaddr*)sin;
  ai->ai_addrlen = sizeof *sin;
  *res = ai;
  return 0;
}

// Releases the list of getaddrinfo.
void freeaddrinfo(struct addrinfo* ai) {
  while (ai) {
    struct addrinfo* next = ai->ai_next;
    free(ai);
    ai = next;
  }
}

// Gives the text of an error of getaddrinfo.
const char* gai_strerror(int err) { return err == EAI_NONAME ? "Name not known" : "Name lookup failed"; }

// Writes the address in dotted form and the port as a number.
int getnameinfo(const struct sockaddr* addr, socklen_t, char* host, socklen_t hostlen, char* serv,
                socklen_t servlen, int) {
  if (addr->sa_family != AF_INET) return EAI_FAMILY;
  const struct sockaddr_in* sin = (const struct sockaddr_in*)addr;
  if (host && !inet_ntop(AF_INET, &sin->sin_addr, host, hostlen)) return EAI_OVERFLOW;
  if (serv) snprintf(serv, servlen, "%u", (unsigned)ntohs(sin->sin_port));
  return 0;
}

// There is no host, service or protocol database.
struct hostent* gethostbyname(const char*) {
  h_errno = 1;  // HOST_NOT_FOUND
  return NULL;
}
// Fails: DOS has no network.
struct hostent* gethostbyaddr(const void*, socklen_t, int) {
  h_errno = 1;
  return NULL;
}
// Returns NULL: there is no services database.
struct servent* getservbyname(const char*, const char*) { return NULL; }
// Returns NULL: there is no services database.
struct servent* getservbyport(int, const char*) { return NULL; }
// Returns NULL: there is no protocols database.
struct protoent* getprotobyname(const char*) { return NULL; }
// Returns NULL: there is no protocols database.
struct protoent* getprotobynumber(int) { return NULL; }
// Returns the text of a host lookup error.
const char* hstrerror(int) { return "Host not found"; }

}  // extern "C"

#endif
