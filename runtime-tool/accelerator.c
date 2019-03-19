/*
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <inttypes.h>
#include <errno.h>

#include "accelerator.h"

static t_accelEngine * accelEngineList[ACCEL_ENGINE_MAX];

static t_acceldev acceldevList[ACCEL_DEVICE_MAX];
static int nbAcceldev = 0;

#define CMD_LDCACHE_PRINT "ldconfig -p"


// foreach registered accelerator engine, look for its driver libraries
static int findInstalledEngines()
{
   char line[1024];
   char *libpath;
   char *libname;
   char *ptr;
   FILE *pfd;
   int iengine, ilib;

   for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
   {
      if ((accelEngineList[iengine] != NULL) && (accelEngineList[iengine]->nblibs > 0))
      {
         accelEngineList[iengine]->libspaths = (char **) calloc(accelEngineList[iengine]->nblibs, sizeof(char*));
      }
   }

   if (( pfd = popen(CMD_LDCACHE_PRINT, "r")) == NULL)
   {
      log_error("popen \"%s\" failed: %s", CMD_LDCACHE_PRINT, strerror(errno));
      return (-1);
   }

   while (fgets(line, sizeof line, pfd))
   {
      line[strcspn(line, "\n")] = '\0';
      libname = line;
      while (isspace((unsigned char)*libname)) libname++;
      if ((ptr = strchr(libname, ' ')) != NULL)
      {
         *ptr = 0;
         libpath = strrchr(ptr+1, ' ');
         if (libpath != NULL)
         {
            libpath++;
            for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
            {
               if (accelEngineList[iengine] != NULL)
               {
                  for (ilib = 0; ilib < accelEngineList[iengine]->nblibs; ilib++)
                  {
                     if ((accelEngineList[iengine]->libspaths[ilib] == NULL)
                      && (! strcmp(libname, accelEngineList[iengine]->libsnames[ilib])))
                     {
                        accelEngineList[iengine]->libspaths[ilib] = strdup(libpath);
                        log_debug("Lib [%s] found in LD cache: %s", libname, libpath);
                        //TODO check lib version matches driver version
                     }
                  }
               }
            }
         }
      }
   }
   pclose(pfd);

   // Accelerator engine is installed if all its libraries have been resolved
   for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
   {
      if (accelEngineList[iengine] != NULL)
      {
         accelEngineList[iengine]->installed = true;
         for (ilib = 0; ilib < accelEngineList[iengine]->nblibs; ilib++)
         {
            if (accelEngineList[iengine]->libspaths[ilib] == NULL)
            {
               accelEngineList[iengine]->installed = false;
               log_info("Engine %s not installed (library %s missing)",
                     accelEngineList[iengine]->name, accelEngineList[iengine]->libsnames[ilib]);
               break;
            }
         }
      }
   }

   return 0;
}


int acceleratorReadConf(char *conffile)
{
   accelEngineList[ACCEL_ENGINE_INTEL] = intelOpaeRegister();
   accelEngineList[ACCEL_ENGINE_XILINX]= xilinxAwsRegister();

   if (accelSettingsReadConf(conffile, accelEngineList) < 0)
      return (-1);

   return findInstalledEngines();
}


// Enumerate all accelerators of all installed engines
int acceleratorEnumerate()
{
   int iengine;

   for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
   {
      if ((accelEngineList[iengine] != NULL) && (accelEngineList[iengine]->installed))
      {
         if (accelEngineList[iengine]->accelops->enumerate(acceldevList, &nbAcceldev) < 0)
         {
            return -1;
         }
      }
   }

   return 0;
}


// Return function config for a given engine
static t_accelfuncConf *acceleratorFuncConf(e_accelengine enginetype, int accelfunc)
{
   int ifct;

   if ((enginetype < ACCEL_ENGINE_MAX) && (accelEngineList[enginetype] != NULL))
   {
      for (ifct = 0; ifct < accelEngineList[enginetype]->nbfunc; ifct++)
      {
         if (accelEngineList[enginetype]->funclist[ifct].funcID == accelfunc)
         {
            return & accelEngineList[enginetype]->funclist[ifct];
         }
      }
   }
   return NULL;
}

// Return function index based on accelerator engine function hardware ID
int acceleratorFuncHwidToIndex(e_accelengine enginetype, char *hwid)
{
   int ifct;

   if ((enginetype < ACCEL_ENGINE_MAX) && (accelEngineList[enginetype] != NULL))
   {
      for (ifct = 0; ifct < accelEngineList[enginetype]->nbfunc; ifct++)
      {
         if (! strcasecmp(accelEngineList[enginetype]->funclist[ifct].accelID, hwid))
         {
            return accelEngineList[enginetype]->funclist[ifct].funcID;
         }
      }
   }
   return ACCELFUNC_UNKNOWN;
}

// Return all available accelerator devices
int acceleratorAddAlldev(t_acceldev **attachdevList, int *nbAttachdev)
{
   int idev;

   for (idev = 0; idev < nbAcceldev; idev++ )
   {
      attachdevList[(*nbAttachdev)++] = & acceldevList[idev];

      log_info("Device %s: engine %s, devpath %s, syspath %s", acceldevList[idev].bdf.str,
            accelEngineList[acceldevList[idev].enginetype]->name,
            acceldevList[idev].devpath[0], acceldevList[idev].syspathAccel);
   }

   log_info("all devices: %d device(s) found", *nbAttachdev);
   return 0;
}

// Try to find (bus:dev:fn) or (slot index) in accelerator devices list
int acceleratorAddDev(char *device, t_acceldev **attachdevList, int *nbAttachdev)
{
   int bus, dev, fn;
   int idev, slotid;
   char *ptr;
   bool found = false;

   if (sscanf(device, "%d:%d.%d", &bus, &dev, &fn) == 3)
   {
      for (idev = 0; idev < nbAcceldev; idev++ )
      {
         if (strcasecmp(device, acceldevList[idev].bdf.str) == 0)
         {
            found = true;
            break;
         }
      }
   }
   else
   {
      slotid = strtoumax(device, &ptr, 10);
      if ((*ptr == '\0') && (slotid < UINTMAX_MAX))
      {
         for (idev = 0; idev < nbAcceldev; idev++ )
         {
            if (acceldevList[idev].slotId == slotid)
            {
               found = true;
               break;
            }
         }
      }
   }

   if (found)
   {
      attachdevList[(*nbAttachdev)++] = & acceldevList[idev];

      log_info("Device %s: engine %s, devpath %s, syspath %s", acceldevList[idev].bdf.str,
            accelEngineList[acceldevList[idev].enginetype]->name,
            acceldevList[idev].devpath[0], acceldevList[idev].syspathAccel);
      return 0;
   }
   else
      return -1;
}

// Check if bitstream reconfig is supported for either physical or virtual PCIe function
bool acceleratorReconfigSupport(t_acceldev *acceldev, e_pciFunction pcifnType)
{
   if ((acceldev->enginetype < ACCEL_ENGINE_MAX) && (accelEngineList[acceldev->enginetype] != NULL))
   {
      if ((pcifnType == PCIFUNC_PHYSICAL) && (accelEngineList[acceldev->enginetype]->reconfigPhysfn))
         return true;

      if ((pcifnType == PCIFUNC_VIRTUAL) && (accelEngineList[acceldev->enginetype]->reconfigVirtfn))
         return true;

   }
   return false;
}

// Load a new bitstream to an accelerator
int acceleratorLoadBitstream(t_acceldev *acceldev, int accelfunc)
{
   t_accelfuncConf *accelfuncConf;

   accelfuncConf = acceleratorFuncConf(acceldev->enginetype, accelfunc);
   if (accelfuncConf != NULL)
   {
      return accelEngineList[acceldev->enginetype]->accelops->loadBitstream(acceldev, accelfuncConf);
   }
   else
   {
      log_error("Device %s: function %s not supported", acceldev->bdf.str, accelfuncIndexToName(accelfunc));
      return -1;
   }
}

// Get number of hugepages 2MB required by the current accelerator function
int acceleratorHugepage2M(t_acceldev *acceldev)
{
   t_accelfuncConf *accelfuncConf;

   accelfuncConf = acceleratorFuncConf(acceldev->enginetype, acceldev->accelfunc);
   if (accelfuncConf)
      return accelfuncConf->nbHugepage2M;
   else
      return 0;
}
// Get number of hugepages 1GB required by the current accelerator function
int acceleratorHugepage1G(t_acceldev *acceldev)
{
   t_accelfuncConf *accelfuncConf;

   accelfuncConf = acceleratorFuncConf(acceldev->enginetype, acceldev->accelfunc);
   if (accelfuncConf)
      return accelfuncConf->nbHugepage1G;
   else
      return 0;
}


// Set user read/write access to some host sysfs device entries
int accelengineHostDeviceSetup(e_accelengine enginetype, t_acceldev *acceldev)
{
   char  syspath[FS_PATH_MAX];
   char *syspathdev;
   int ientry;
   int i;

   if ((enginetype >= ACCEL_ENGINE_MAX) || (accelEngineList[enginetype] == NULL)
         || (acceldev == NULL))
      return -1;

   for (i = 0; i <= 1; i++)
   {
      if (i == 0)
         syspathdev = acceldev->syspathAccel;
      else
         syspathdev = acceldev->syspathEngine;
      if (strlen(syspathdev) == 0)
         continue;

      for (ientry = 0; ientry < accelEngineList[enginetype]->nbsysentries ; ientry++)
      {
         snprintf(syspath, FS_PATH_MAX, "%s/%s", syspathdev, accelEngineList[enginetype]->sysentriesRW[ientry]);
         if (chmod(syspath, 0666) < 0)
         {
            log_error("Device %s: failed to chmod %s: %s", acceldev->bdf.str, syspath, strerror(errno));
            return -1;
         }
         log_debug("Device %s: chmod %s done", acceldev->bdf.str, syspath);
      }
   }
   return 0;
}

// Attach engine mount paths to container
int accelengineMountPaths(char *rootfs, e_accelengine enginetype)
{
   int imount;

   if ((enginetype >= ACCEL_ENGINE_MAX) || (accelEngineList[enginetype] == NULL))
      return -1;

   for (imount = 0; imount < accelEngineList[enginetype]->nbmount; imount ++)
   {
      if (mountFile(rootfs, accelEngineList[enginetype]->mountlist[imount].src,
            accelEngineList[enginetype]->mountlist[imount].dst, false,
            accelEngineList[enginetype]->mountlist[imount].rdonly, false) < 0)
      {
         log_error("Mount path %s: failed to mount: %s", accelEngineList[enginetype]->mountlist[imount].src, strerror(errno));
         return -1;
      }
   }
   if (accelEngineList[enginetype]->nbmount > 0)
      log_info("Engine %s: mount paths attached to container", accelEngineList[enginetype]->name);

   return 0;
}

// Attach engine driver libraries to container
int accelengineAttachLibs(char *rootfs, e_accelengine enginetype)
{
   char srcpath[FS_PATH_MAX];
   char lnkpath[FS_PATH_MAX];
   char dstpath[2*FS_PATH_MAX];
   struct stat stats;
   int ilib;

   if ((enginetype >= ACCEL_ENGINE_MAX) || (accelEngineList[enginetype] == NULL))
      return -1;

   for (ilib = 0; ilib < accelEngineList[enginetype]->nblibs; ilib++)
   {
      if (accelEngineList[enginetype]->libspaths[ilib] != NULL)
      {
         // follow symlink
         strncpy(srcpath, accelEngineList[enginetype]->libspaths[ilib], FS_PATH_MAX);
         while ((lstat(srcpath, &stats) == 0) && (S_ISLNK(stats.st_mode)))
         {
            memset(lnkpath, 0, FS_PATH_MAX);
            if (readlink(srcpath, lnkpath, FS_PATH_MAX) < 0)
            {
               log_error("Library %s: failed to read symlink: %s", srcpath, strerror(errno));
               return -1;
            }
            dirname(srcpath);
            strcat(srcpath, "/");
            strncat(srcpath, basename(lnkpath), FS_PATH_MAX-strlen(srcpath));
         }

         if (mountFile(rootfs, srcpath, NULL, false, true, false) < 0)
         {
            log_error("Library %s: failed to mount: %s", srcpath, strerror(errno));
            return -1;
         }
         if (strcmp(srcpath, accelEngineList[enginetype]->libspaths[ilib]))
         {
            snprintf(dstpath, sizeof(dstpath), "%s/%s", rootfs, accelEngineList[enginetype]->libspaths[ilib]);
            if (file_create(dstpath, basename(srcpath), 0/*uid*/, 0/*gid*/, 0777 | S_IFLNK) < 0)
            {
               log_error("Library %s: failed to create symlink: %s", dstpath, strerror(errno));
               return -1;
            }
            log_debug("Library %s: symlink created", accelEngineList[enginetype]->libspaths[ilib]);
         }
      }
   }
   if (accelEngineList[enginetype]->nblibs > 0)
      log_info("Engine %s: driver libraries attached to container", accelEngineList[enginetype]->name);

   return 0 ;
}


// Free all engine and accelerator resources
void acceleratorEnd()
{
   int iengine, ilib;

   for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
   {
      if (accelEngineList[iengine] != NULL)
      {
         if (accelEngineList[iengine]->nbfunc > 0)
         {
            free(accelEngineList[iengine]->funclist);
         }
         if (accelEngineList[iengine]->nbmount > 0)
         {
            free(accelEngineList[iengine]->mountlist);
         }
         if (accelEngineList[iengine]->libspaths != NULL)
         {
            for (ilib = 0; ilib < accelEngineList[iengine]->nblibs; ilib++)
            {
               if (accelEngineList[iengine]->libspaths[ilib] != NULL)
               {
                  free(accelEngineList[iengine]->libspaths[ilib]);
               }
            }
            free(accelEngineList[iengine]->libspaths);
         }
      }
   }
}
