/*
 * Customize the container
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <dirent.h>

#include "accelerator.h"

#define NS_MOUNT_PROC_PATH      "/proc/%d/ns/mnt"
#define SYSFS_CGROUP_DEV_PATH   SYSFS_CGROUP_PATH "/devices"
#define SYSFS_CGROUP_DEV_ALLOW  "devices.allow"


// Enter mount namespace of container PID
int enterNamespace(pid_t pid)
{
   char path[FS_PATH_MAX];
   int fdnsDefault;
   int fdnsProc;

   // Get default namespace
   snprintf(path, FS_PATH_MAX, NS_MOUNT_PROC_PATH, getpid());
   fdnsDefault = open(path, O_RDONLY|O_CLOEXEC);
   if (fdnsDefault < 0)
   {
     log_error("Failed to open %s: %s", path, strerror(errno));
     return -1;
   }

   // Switch to container process namespace
   snprintf(path, FS_PATH_MAX, NS_MOUNT_PROC_PATH, pid);
   fdnsProc = open(path, O_RDONLY|O_CLOEXEC);
   if (fdnsProc < 0)
   {
     log_error("Failed to open %s (wrong pid?) : %s", path, strerror(errno));
     close(fdnsDefault);
     return -1;
   }

   if (setns(fdnsProc, CLONE_NEWNS) < 0)
   {
     log_error("Failed to set mount namespace of pid %d: %s", pid, strerror(errno));
     close(fdnsDefault);
     close(fdnsProc);
     return -1;
   }
   log_info("Switched to mount namespace of pid %d", pid);

   close(fdnsProc);
   return(fdnsDefault);
}

// Leave mount namespace of container PID
int leaveNamespace(int fdnsDefault)
{
   if (fdnsDefault < 0)
      return(-1);

   if (setns(fdnsDefault, CLONE_NEWNS) < 0)
   {
     log_error("Failed to set back default mount namespace: %s", strerror(errno));
     close(fdnsDefault);
     return -1;
   }
   log_info("Switched back to default mount namespace");

   close(fdnsDefault);

   return 0;
}


// Add devices nodes to dest FS /sys/fs/cgroup/devices/devices.allow
int allowDevices(char *rootfs, t_acceldev **acceldevList, int nbAcceldev)
{
   char sysCgroupPath[2*FS_PATH_MAX]; // *2 for nested FS
   char pathallow[2*FS_PATH_MAX];
   char *devpath;
   char devallow[32];
   struct stat stats;
   FILE *pfd=NULL;
   int idev;
   int idevpath;
   int ret = -1;

   snprintf(sysCgroupPath, sizeof(sysCgroupPath), "%s/%s", rootfs, SYSFS_CGROUP_DEV_PATH);
   if (mount (NULL, sysCgroupPath, "cgroup", MS_BIND | MS_REMOUNT, NULL) < 0)
   {
      log_error("Failed to remount sysfs cgroup devices read/write (path %s): %s", sysCgroupPath, strerror(errno));
      return(-1);
   }
   log_debug("sysfs cgroup devices remounted read/write (path %s)", sysCgroupPath);

   snprintf(pathallow, sizeof(pathallow), "%s/%s", sysCgroupPath, SYSFS_CGROUP_DEV_ALLOW);
   pfd = fopen ( pathallow, "w" );
   if ( pfd == NULL )
   {
      log_error("Failed to open %s in read/write mode", SYSFS_CGROUP_DEV_ALLOW);
      goto out;
   }

   for (idev = 0; idev < nbAcceldev; idev++)
   {
      for (idevpath = 0; idevpath < NB_DEVPATH_MAX; idevpath++)
      {
         devpath = acceldevList[idev]->devpath[idevpath];
         if (strlen(devpath) == 0)
            continue;

         if (stat(devpath, &stats) != 0)
         {
            log_error("Device node %s: stat failed: %s", devpath, strerror(errno));
            goto out;
         }
         snprintf(devallow, sizeof(devallow), "c %d:%d rwm", major(stats.st_rdev), minor(stats.st_rdev));
         log_debug("Device %s: devallow %s", acceldevList[idev]->bdf.str, devallow);
         if (fputs(devallow, pfd ) <= 0)
         {
            log_error("Failed to write [%s] to devices.allow: %s", devallow, strerror(errno));
            goto out;
         }
         fflush(pfd);

         // Attach host /dev/<devnode> to dest FS /dev/<devnode>
         // Note: runc uses mknod by default or mount bind if (RunningInUserNS() || config.Namespaces.Contains(configs.NEWUSER))
         //       libnvidia always mounts bind devnodes
         //   => use mount bind as it works in all situations
         if (mountFile(rootfs, devpath, NULL, true, false, true) < 0)
            goto out;

         log_info("Device %s: device node %u:%u whitelisted",
               acceldevList[idev]->bdf.str, major(stats.st_rdev), minor(stats.st_rdev));
      }

      // Mount accel and/or engine sysfs path if not empty
      if (strlen(acceldevList[idev]->syspathAccel) > 0)
      {
         if (mountFile(rootfs, acceldevList[idev]->syspathAccel, NULL, false, false, true) < 0)
            goto out;
      }
      if (strlen(acceldevList[idev]->syspathEngine) > 0)
      {
         if (mountFile(rootfs, acceldevList[idev]->syspathEngine, NULL, false, false, true) < 0)
            goto out;
      }
   }

   ret = 0;

out:
   if (pfd != NULL)
      fclose(pfd);
   mount (NULL, sysCgroupPath, "cgroup", MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
   log_debug("sysfs cgroup devices remounted read only");
   return ret;
}


// Set up container to be ready for accelerators access
int containerSetup(pid_t pid, char *rootfs, t_acceldev **acceldevList, int nbAcceldev)
{
   const uint64_t MB = (1024 *1024);
   const uint64_t GB = MB * 1024;
   bool attachEngine[ACCEL_ENGINE_MAX] = { false };
   int fdnsDefault = -1;  // file descriptor of default namespace
   int iengine;
   int idev;
   int  totHugepage2M = 0;
   int  totHugepage1G = 0;
   rlim_t memHugepage;
   int ret = -1;

   fdnsDefault = enterNamespace(pid);
   if (fdnsDefault < 0)
      return(-1);

   // Compute nb hugepages required for all attached devices
   for (idev = 0; idev < nbAcceldev; idev++)
   {
      if (acceldevList[idev]->enginetype < ACCEL_ENGINE_MAX)
      {
         attachEngine[acceldevList[idev]->enginetype] = true;

         totHugepage2M += acceleratorHugepage2M(acceldevList[idev]);
         totHugepage1G += acceleratorHugepage1G(acceldevList[idev]);
      }
   }

   for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
   {
      if (attachEngine[iengine])
      {
         // mount binds engine generic paths
         if (accelengineMountPaths(rootfs, iengine) < 0)
            goto out;

         // mount bind engine driver libraries
         if (accelengineAttachLibs(rootfs, iengine) < 0)
            goto out;
      }
   }
   ldconfigCacheUpdate(rootfs);

   // Configure device access inside container
   if (allowDevices(rootfs, acceldevList, nbAcceldev) < 0)
      goto out;

   // Configure memory resources of container
   memHugepage = (totHugepage2M * MB * 2) + (totHugepage1G * GB);
   if ( (rlimitConfig(pid, RLIMIT_MEMLOCK, memHugepage, memHugepage) != 0)
     || (limitHugetlb(pid, totHugepage2M, totHugepage1G) != 0))
   {
      log_error("Container pid %d: failed to set memory limits", pid);
      goto out;
   }
   log_info("Container pid %d: memlock %llu, hugepages 2MB %d, hugepages 1GB %d", pid, memHugepage, totHugepage2M, totHugepage1G);

   ret = 0;

out:
   if (fdnsDefault != -1)
       leaveNamespace(fdnsDefault);

   return ret;
}

