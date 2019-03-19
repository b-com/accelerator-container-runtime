#ifndef __INCLUDE_UTILS_H__
#define __INCLUDE_UTILS_H__

#include <stdint.h>
#include <stdbool.h>
#include <sys/syslog.h>
#include <sys/resource.h>
#include <sys/types.h>


#define FS_PATH_MAX  256
#define FILE_NAME_MAX 64

#define SYSFS_CGROUP_PATH "/sys/fs/cgroup/"

#define nitems(x) (sizeof(x) / sizeof(*x))

#define log_fatal(fmt, args...) { logWrite(LOG_ERR, fmt, ##args ) ; fprintf(stderr, fmt "\n", ##args );}
#define log_error(fmt, args...) logWrite(LOG_ERR, fmt, ##args )
#define log_warn(fmt, args...)  logWrite(LOG_WARNING, fmt, ##args )
#define log_info(fmt, args...)  logWrite(LOG_INFO, fmt, ##args )
#define log_debug(fmt, args...) logWrite(LOG_DEBUG, fmt, ##args )

void logOpen(const char *path, int level);
void logClose();
void logSetLevel(int level);
void logWrite(int level, const char *fmt, ...);

int sysfsReadString(char *syspath, char *value, int valuelen);
int sysfsWriteString(char *syspath, char *value);
uint64_t sysfsReadUint64(char *syspath);
int sysfsWriteUint64(char *syspath, uint64_t value);

int rlimitConfig(pid_t pid, int resource, rlim_t rlim_soft, rlim_t rlim_hard);
int limitHugetlb(pid_t pid, int  nbHugepage2M, int  nbHugepage1G);

int file_create(const char *path, const char *data, uid_t uid, gid_t gid, mode_t mode);
int mountFile(char *rootfs, char *srcpath, char *dstpath, bool device, bool rdonly, bool noexec);
int ldconfigCacheUpdate(char *rootfs);

int fspathGetEntries(char *fspathPattern, char entriesList[][FS_PATH_MAX], int maxEntries);

#endif // __INCLUDE_UTILS_H__
