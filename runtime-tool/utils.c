/*
 * Misc utils
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/fsuid.h>
#include <libgen.h>

#include "utils.h"

static FILE *logFd;
static int logLevel = LOG_INFO;
#define LOG_MAX_LEN 512
static char logPriority[LOG_DEBUG+1][10]={"","","","error","warn","","info","debug"};
static char logStr[LOG_MAX_LEN];

#define SYSFS_CGROUP_HUGETLB_PATH     SYSFS_CGROUP_PATH "/hugetlb"
#define SYSFS_CGROUP_HUGETLB_2MB_LIMIT "hugetlb.2MB.limit_in_bytes"
#define SYSFS_CGROUP_HUGETLB_1GB_LIMIT "hugetlb.1GB.limit_in_bytes"


void logOpen(const char *path, int level)
{
   if (path != NULL)
   {
      logFd = fopen(path, "ae");
      logLevel = level;
   }
}

void logClose()
{
   if (logFd != NULL)
   {
      fclose(logFd);
      logFd = NULL;
   }
}

void logSetLevel(int level)
{
   logLevel = level;
}

void logWrite(int level, const char *fmt, ...)
{
   if ((logFd != NULL) && (level <= logLevel))
   {
      time_t    rawtime;
      struct tm tm;
      va_list   args;

      time(&rawtime);
      localtime_r(&rawtime, &tm);

      va_start (args, fmt);
      vsnprintf(logStr, LOG_MAX_LEN, fmt, args);
      va_end (args);

      fprintf(logFd, "%02d-%02d-%02d %02d:%02d:%02d [%s] %s\n",
            tm.tm_mday, tm.tm_mon+1, tm.tm_year%100, tm.tm_hour, tm.tm_min, tm.tm_sec,
            logPriority[level], logStr);
      fflush(logFd);
   }
}


int sysfsReadString(char *syspath, char *value, int valuelen)
{
   FILE *pFd;

   pFd = fopen(syspath, "r");
   if (! pFd)
   {
      log_error("Failed to open file %s: %s", syspath, strerror(errno));
      return -1;
   }
   if (fread((void*)value, 1, valuelen, pFd) == 0)
   {
      log_error("%s: failed to read string", syspath);
      fclose(pFd);
      return -1;
   }
   fclose(pFd);

   value[strcspn(value, "\n")] = '\0';
   return 0;
}
int sysfsWriteString(char *syspath, char *value)
{
   FILE *pFd;

   pFd = fopen(syspath, "w");
   if (! pFd)
   {
      log_error("Failed to open file %s: %s", syspath, strerror(errno));
      return -1;
   }
   if (fputs(value, pFd) <= 0)
   {
      log_error("%s: failed to write string", syspath);
      fclose(pFd);
      return -1;
   }
   fclose(pFd);
   return 0;
}

uint64_t sysfsReadUint64(char *syspath)
{
   char valuestr[16];

   if (sysfsReadString(syspath, valuestr, 16) == 0)
   {
      return strtoull(valuestr, NULL, 0);
   }
   else
      return 0;
}
int sysfsWriteUint64(char *syspath, uint64_t value)
{
   char valuestr[64];

   snprintf(valuestr, sizeof valuestr, "%llu", (unsigned long long) value);
   return sysfsWriteString(syspath, valuestr);
}

// Set system ulimit limits on container process
// (note: container ignores /etc/security/limits.* files)
int rlimitConfig(pid_t pid, int resource, rlim_t rlim_soft, rlim_t rlim_hard)
{
   struct rlimit limits = {rlim_soft, rlim_hard};

   if (prlimit(pid, resource, & limits, NULL) < 0)
   {
      log_warn("Dest FS set ulimits %d failed: %s", resource, strerror(errno));
      return -1;
   }

   log_debug("Dest FS ulimits %d set to %llu, %llu", resource, rlim_soft, rlim_hard);
   return 0;
}


// Find sysfs path of a cgroup
int findCgroupPath(pid_t pid, char *cgroupname, char *path, int pathlen)
{
   char procpath[FS_PATH_MAX];
   char line[FS_PATH_MAX];
   FILE *pFd;
   char *ptr;
   char *cgpath, *cgname;

   snprintf(procpath, FS_PATH_MAX, "/proc/%d/cgroup", pid);
   pFd = fopen(procpath, "r");
   if (pFd != NULL)
   {
      while ( fgets ( line, sizeof line, pFd ) != NULL )
      {
         if ((ptr = strrchr(line, ':')))
         {
            ptr[strcspn(ptr, "\n")] = '\0';
            cgpath = ptr + 1;
            *ptr = 0;
            if ((ptr = strrchr(line, ':')))
            {
               cgname = ptr + 1;
               if (! strcmp(cgname, cgroupname))
               {
                  snprintf(path, pathlen, "%s/%s/%s/", SYSFS_CGROUP_PATH, cgroupname, cgpath);
                  log_debug("cgroup %s sysfs path %s", cgname, path);
                  return 0;
               }
            }
         }
      }
      log_error("Failed to find cgroup %s for pid %d", cgroupname, pid);
      fclose(pFd);
      return -1;
   }
   else
   {
      log_error("Failed to open file %s: %s", procpath, strerror(errno));
      return -1;
   }
}

// Configure huge TLB memory limits
int limitHugetlb(pid_t pid, int  nbHugepage2M, int  nbHugepage1G)
{
   char syspath[FS_PATH_MAX];
   char cgLimitPath[FS_PATH_MAX];
   char limitvalue[32];
   int ret;

   if (findCgroupPath(pid, "hugetlb", syspath, sizeof syspath) == 0)
   {
      if (mount (NULL, SYSFS_CGROUP_HUGETLB_PATH, "cgroup", MS_BIND | MS_REMOUNT, NULL) < 0)
      {
         log_error("Failed to remount sysfs cgroup hugetlb read/write (path %s)", SYSFS_CGROUP_HUGETLB_PATH);
         return(-1);
      }

      snprintf(cgLimitPath, FS_PATH_MAX, "%s/%s", syspath, SYSFS_CGROUP_HUGETLB_2MB_LIMIT);
      snprintf(limitvalue, sizeof limitvalue, "%dM", nbHugepage2M * 2);
      ret = sysfsWriteString(cgLimitPath, limitvalue);
      if (ret == 0)
      {
         snprintf(cgLimitPath, FS_PATH_MAX, "%s/%s", syspath, SYSFS_CGROUP_HUGETLB_1GB_LIMIT);
         snprintf(limitvalue, sizeof limitvalue, "%dG", nbHugepage1G);
         ret = sysfsWriteString(cgLimitPath, limitvalue);
      }

      mount(NULL, SYSFS_CGROUP_HUGETLB_PATH, "cgroup", MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
      return ret;
   }
   else
   {
      return -1;
   }
}



static mode_t get_umask(void)
{
        mode_t mask;

        mask = umask(0);
        umask(mask);
        return (mask);
}

static int set_fsugid(uid_t uid, gid_t gid)
{
#if 0
        cap_t state = NULL;
        cap_value_t cap = CAP_DAC_OVERRIDE;
        cap_flag_value_t flag;
#endif
        int rv = -1;

        setfsgid(gid);
        if ((gid_t)setfsgid((gid_t)-1) != gid) {
                errno = EPERM;
                return (-1);
        }
        setfsuid(uid);
        if ((uid_t)setfsuid((uid_t)-1) != uid) {
                errno = EPERM;
                return (-1);
        }

#if 0
        /*
         * Changing the filesystem user ID potentially affects effective capabilities.
         * If allowed, restore CAP_DAC_OVERRIDE because some distributions rely on it
         * (e.g. https://bugzilla.redhat.com/show_bug.cgi?id=517575).
         */
        if ((state = cap_get_proc()) == NULL)
                goto fail;
        if (cap_get_flag(state, cap, CAP_PERMITTED, &flag) < 0)
                goto fail;
        if (flag == CAP_SET) {
                if (cap_set_flag(state, CAP_EFFECTIVE, 1, &cap, CAP_SET) < 0)
                        goto fail;
                if (cap_set_proc(state) < 0)
                        goto fail;
        }
#endif
        rv = 0;
// fail:
//        cap_free(state);
        return (rv);
}

static int make_ancestors(char *path, mode_t perm)
{
   struct stat s;
   char *p;

   if (*path == '\0' || *path == '.')
      return (0);

   if (stat(path, &s) == 0) {
      if (S_ISDIR(s.st_mode))
         return (0);
      errno = ENOTDIR;
   }
   if (errno != ENOENT)
      return (-1);

   if ((p = strrchr(path, '/')) != NULL) {
      *p = '\0';
      if (make_ancestors(path, perm) < 0)
         return (-1);
      *p = '/';
   }
   return (mkdir(path, perm));
}

int file_create(const char *path, const char *data, uid_t uid, gid_t gid, mode_t mode)
{
   char *p;
   uid_t euid;
   gid_t egid;
   mode_t perm;
   int fd;
   size_t size;
   int flags = O_NOFOLLOW|O_CREAT;
   int rv = -1;

   if ((p = strdup(path)) == NULL)
      return (-1);

   /*
    * Change the filesystem UID/GID before creating the file to support user namespaces.
    * This is required since Linux 4.8 because the inode needs to be created with a UID/GID known to the VFS.
    */
   euid = geteuid();
   egid = getegid();
   if (set_fsugid(uid, gid) < 0)
   {
      log_error("set_fsugid failed: %s", strerror(errno));
      goto fail;
   }

   perm = (0777 & ~get_umask()) | S_IWUSR | S_IXUSR;
   if (make_ancestors(dirname(p), perm) < 0)
   {
      log_error("make_ancestors failed: %s", strerror(errno));
      goto fail;
   }
   perm = 0777 & ~get_umask() & mode;

   if (S_ISDIR(mode)) {
      if (mkdir(path, perm) < 0 && errno != EEXIST)
      {
         log_error("mkdir %s failed: %s", path, strerror(errno));
         goto fail;
      }
   } else if (S_ISLNK(mode)) {
      if (data == NULL) {
         log_error("S_ISLNK %s failed: data");
         errno = EINVAL;
         goto fail;
      }
      if (symlink(data, path) < 0 && errno != EEXIST)
      {
         log_error("symlink %s failed: %s", path, strerror(errno));
         goto fail;
      }
   } else {
      if (data != NULL) {
         size = strlen(data);
         flags |= O_WRONLY|O_TRUNC;
      }
      if ((fd = open(path, flags, perm)) < 0) {
         if (errno == ELOOP)
            errno = EEXIST; /* XXX Better error message if the file exists and is a symlink. */
         log_warn("open %s failed: %s", path, strerror(errno));
         goto fail;
      }
      if (data != NULL && write(fd, data, size) < (ssize_t)size) {
         log_error("write %s failed: %s", path, strerror(errno));
         close(fd);
         goto fail;
      }
      close(fd);
   }
   rv = 0;

fail:
   set_fsugid(euid, egid);
   free(p);
   return (rv);
}


// Mount bind a file or directory to container FS
int mountFile(char *rootfs, char *srcpath, char *dstpath, bool device, bool rdonly, bool noexec)
{
   char dstpathfull[2*FS_PATH_MAX]; // *2 for nested FS
   struct stat stats;
   unsigned long int  options;

   if (stat(srcpath, &stats) != 0)
   {
      log_error("mountFile src %s not found", srcpath);
      return -1;
   }
   if (dstpath != NULL)
      snprintf(dstpathfull, sizeof(dstpathfull), "%s/%s", rootfs, dstpath);
   else
      snprintf(dstpathfull, sizeof(dstpathfull), "%s/%s", rootfs, srcpath);

   if (file_create(dstpathfull, NULL, 0 /*uid*/, 0 /*gid*/, stats.st_mode) < 0)
   {
      log_error("mountFile src path %s: failed to create dest", srcpath);
      return -1;
   }

   if (mount(srcpath, dstpathfull, NULL, MS_BIND, NULL) < 0)
   {
      log_error("mountFile src path %s: mount bind failed: %s", srcpath, strerror(errno));
      return -1;
   }

   options = MS_BIND | MS_REMOUNT;
   if (! device)
      options |= MS_NODEV;
   if (rdonly)
      options |= MS_RDONLY;
   if (noexec)
      options |= MS_NOEXEC | MS_NOSUID;
   if (mount(NULL, dstpathfull, NULL, options, NULL) < 0)
   {
      log_error("mountFile src path %s: remount failed: %s", srcpath, strerror(errno));
      return -1;
   }

   log_debug("srcpath %s mounted to dstpath %s (opt %X)", srcpath, dstpathfull, options);
   return 0;
}


// Update dynamic linker cache
int ldconfigCacheUpdate(char *rootfs)
{
   char cmd[FS_PATH_MAX];

   snprintf(cmd, FS_PATH_MAX, "ldconfig -r %s", rootfs);
   if (system(cmd) == 0)
   {
      log_debug("Dest root FS LD config cache updated");
      return 0;
   }
   else
   {
      log_error("Dest root FS: failed to update LD config cache");
      return -1;
   }
}

// Get all file/dir entries of FS path pattern
int fspathGetEntries(char *fspathPattern, char entriesList[][FS_PATH_MAX], int maxEntries)
{
   FILE *pfd=NULL;
   char cmd[FS_PATH_MAX];
   char entry[FS_PATH_MAX];
   int nbentries = 0;

   snprintf(cmd, sizeof cmd, "ls %s", fspathPattern);
   if (( pfd = popen(cmd, "r")) == NULL)
   {
      log_error("popen \"%s\" failed: %s", cmd, strerror(errno));
      return -1;
   }

   while ((nbentries < maxEntries) && (fgets(entry, sizeof entry, pfd)))
   {
      entry[strcspn(entry, "\n")] = '\0';
      if (strlen(entry) > 0)
      {
         strncpy(entriesList[nbentries++], entry, FS_PATH_MAX-1);
      }
   }
   fclose(pfd);
   return 0;
}
