/*
 * Intel FPGA engine and AFUs accelerators
 *
 * NOTE: libopae does not export sysfs layout, so parse directly sysfs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#include "accelerator.h"

#define SYS_FPGA_CLASS_PATH "/sys/class/fpga"
#define SYS_PORT_NAME_FMT   "intel-fpga-port.%d"
#define SYS_FME_NAME_FMT    "intel-fpga-fme.%d"

#define UUID_LEN_MAX 32

static t_accelEngine intelOpaeEngine = {
   .name = "IntelOPAE",
   .bistreamPath = "/usr/lib/bitstream/intel",
   .reconfigPhysfn = true,
   .reconfigVirtfn = false,
   .sriovMode = false
};

static char *logtag = intelOpaeEngine.name;

// Intel OPAE libraries depending on Intel FPGA driver
static const char * const intelAccelLibs[] = {
   "libopae-c.so",          /* Intel OPAE C library */
   "libopae-c++.so",        /* Intel OPAE C++ library */
};

// Accelerator device sysfs entries needing user read/write access on host
static const char * const intelSysentriesRW[] = {
   "userclk_freqcmd",
   "userclk_freqcntrcmd",
   "errors/clear"
};


static t_acceldev fmeDevice[ACCEL_DEVICE_ENGINE_MAX];
static int nbFmeDevices = 0;

//static inline uint64_t intelObjectId(t_acceldev *acceldev) {
//   return ((acceldev->devmajor & 0xFFF) << 20) | (acceldev->devminor & 0xFFFFF);
//}


// Read bus:device.function from link name finishing eg with .../0000:06:00.0
static int busdevfnFromSymlink(char *symlink, t_pcibdf *pcibdf)
{
   char syspath[FS_PATH_MAX];
   char *ptr;

   memset(syspath, 0, FS_PATH_MAX);
   if (readlink(symlink, syspath, FS_PATH_MAX) >= 0)
   {
      if ((ptr = strrchr(syspath, '.')))
      {
         pcibdf->function = atoi(ptr+1);
         *ptr = 0;
         if ((ptr = strrchr(syspath, ':')))
         {
            pcibdf->device = atoi(ptr+1);
            *ptr = 0;
            if ((ptr = strrchr(syspath, ':')))
            {
               pcibdf->bus = atoi(ptr+1);
               snprintf(pcibdf->str, PCI_BDF_LEN, PCI_BDF_FMT, pcibdf->bus, pcibdf->device, pcibdf->function);
               return 0;
            }
         }
      }
      log_error("%s: readlink %s: failed to extract (bus,device,function)", logtag, symlink);
   }
   else
      log_error("%s: readlink %s: %s", logtag, symlink, strerror(errno));

   return -1;
}

// Read AFU UUID from sysfs entry
static int readAfuId(t_acceldev *acceldev)
{
   char syspath[FS_PATH_MAX];
   char afuId[UUID_LEN_MAX+1] = { 0 };

   snprintf(syspath, FS_PATH_MAX, "%s/%s", acceldev->syspathAccel, "afu_id");
   if (sysfsReadString(syspath, afuId, UUID_LEN_MAX+1) < 0)
      return -1;

   if (strlen(afuId) != UUID_LEN_MAX)
   {
      log_error("%s: Device %s: malformed AFU Id", logtag, acceldev->bdf.str);
      return -1;
   }

   memset (acceldev->funcHwid, 0, sizeof acceldev->funcHwid);
   strncat(acceldev->funcHwid, afuId   , 8);
   strcat (acceldev->funcHwid, "-");
   strncat(acceldev->funcHwid, afuId+ 8, 4);
   strcat (acceldev->funcHwid, "-");
   strncat(acceldev->funcHwid, afuId+12, 4);
   strcat (acceldev->funcHwid, "-");
   strncat(acceldev->funcHwid, afuId+16, 4);
   strcat (acceldev->funcHwid, "-");
   strncat(acceldev->funcHwid, afuId+20,12);

   // Find corresponding acceleration function
   acceldev->accelfunc = acceleratorFuncHwidToIndex(ACCEL_ENGINE_INTEL, acceldev->funcHwid);

   return 0;
}

// Read AFU port info: afu id, attached fme
static int readPortInfo(t_acceldev *acceldev, char *sysentry)
{
   char syspath[FS_PATH_MAX];
   t_pcibdf  bdf;
   int ifme;

   if (readAfuId(acceldev) < 0)
      return -1;

   acceldev->pcifnType = PCIFUNC_PHYSICAL;  // default

   // If virtual function, get attached physical FME device
   if (acceldev->privdata == NULL)
   {
      snprintf(syspath, FS_PATH_MAX, "%s/%s/%s", sysentry, "device", "physfn");
      if (busdevfnFromSymlink(syspath, & bdf) < 0)
      {
         log_error("%s: Entry %s: failed to get physfn", logtag, sysentry);
         return -1;
      }

      acceldev->pcifnType = PCIFUNC_VIRTUAL;

      for (ifme = 0; ifme < nbFmeDevices; ifme++)
      {
         if (! strcmp(fmeDevice[ifme].bdf.str, bdf.str))
         {
            acceldev->privdata = & fmeDevice[ifme];
            break;
         }
      }
   }

   if (acceldev->privdata == NULL)
   {
      log_error("%s: Port %s: failed to get attached FME", logtag, acceldev->bdf.str);
      return -1;
   }
   return 0;
}


// Enumerate all FPGA engines and AFU ports
static int enumerate(t_acceldev acceldevList[], int *nbAcceldev)
{
   DIR *sysdir = NULL;
   struct dirent *dirent = NULL;
   char   sysentry[FS_PATH_MAX];
   char   syspath[FS_PATH_MAX];
   char   devname[FILE_NAME_MAX];
   struct stat stats;
   t_acceldev acceldev;
   t_acceldev *fmeptr;
   char *ptr;
   int ifme, iport;
   int ret = -1;

   sysdir = opendir(SYS_FPGA_CLASS_PATH);
   if (sysdir == NULL)
   {
      log_warn("%s: sysfs FPGA class not found: check FPGA driver inserted", logtag);
      return 0; // not an error, only xilinx fpga may be present
   }

   while ( ((*nbAcceldev) < ACCEL_DEVICE_MAX)
        && ((dirent = readdir(sysdir)) != NULL) )
   {
      if (!strcmp(dirent->d_name, ".") || !strcmp(dirent->d_name, ".."))
         continue;

      memset(&acceldev, 0, sizeof(t_acceldev));
      fmeptr = NULL;

      // Get instance ID from entry name "intel-fpga-dev.<instance id>"
      ptr = strchr(dirent->d_name, '.');
      if (ptr == NULL)
      {
         log_error("%s: Entry %s: failed to get instance id", logtag, dirent->d_name);
         continue;
      }
      acceldev.slotId = atoi(ptr+1);
      snprintf(sysentry, FS_PATH_MAX, "%s/%s", SYS_FPGA_CLASS_PATH, dirent->d_name);

      // Get bdf (bus, device, function) from symlink "device -> ../../../0000:06:00.0"
      snprintf(syspath, FS_PATH_MAX, "%s/%s", sysentry, "device");
      if (busdevfnFromSymlink(syspath, & acceldev.bdf) < 0)
      {
         log_error("%s: Entry %s: failed to get bdf from symlink %s", logtag, sysentry, syspath);
         continue;
      }

      // Get pci vendor and device identifiers
      snprintf(syspath, FS_PATH_MAX, "%s/%s/%s", sysentry, "device", "vendor");
      acceldev.vendorId = (int) sysfsReadUint64(syspath);
      if (acceldev.vendorId < 0)
         continue;
      snprintf(syspath, FS_PATH_MAX, "%s/%s/%s", sysentry, "device", "device");
      acceldev.deviceId = (int) sysfsReadUint64(syspath);
      if (acceldev.deviceId < 0)
         continue;

      // Create FME object if found
      snprintf(devname, FILE_NAME_MAX, SYS_FME_NAME_FMT, acceldev.slotId);
      snprintf(acceldev.syspathAccel, FS_PATH_MAX, "%s/%s", sysentry, devname);
      if (stat(acceldev.syspathAccel, &stats) == 0)
      {
         snprintf(acceldev.devpath[0], FS_PATH_MAX, "%s/%s", LINUX_DEV_PATH, devname);

         ifme = nbFmeDevices ++;
         fmeDevice[ifme] = acceldev;
         fmeptr = & fmeDevice[ifme];

         log_info("%s: New FME device: name %s, instance %d, pcidev %04x:%04x, devnode %s", logtag,
               fmeDevice[ifme].bdf.str, fmeDevice[ifme].slotId,
               fmeDevice[ifme].vendorId, fmeDevice[ifme].deviceId, fmeDevice[ifme].devpath[0]);
      }

      // Create PORT object if found
      snprintf(devname, FILE_NAME_MAX, SYS_PORT_NAME_FMT, acceldev.slotId);
      snprintf(acceldev.syspathAccel, FS_PATH_MAX, "%s/%s", sysentry, devname);
      if (stat(acceldev.syspathAccel, &stats) == 0)
      {
         snprintf(acceldev.devpath[0], FS_PATH_MAX, "%s/%s", LINUX_DEV_PATH, devname);

         iport = *nbAcceldev;
         acceldevList[iport] = acceldev;
         acceldevList[iport].enginetype = ACCEL_ENGINE_INTEL;
         acceldevList[iport].privdata = fmeptr;

         if (readPortInfo(& acceldevList[iport], sysentry) < 0)
            continue;

         (*nbAcceldev) ++;
         log_info("%s: New PORT device: name %s, instance %d, pcidev %04x:%04x, devnode %s, afuid %s (fct %d)",
               logtag, acceldevList[iport].bdf.str, acceldevList[iport].slotId,
               acceldevList[iport].vendorId, acceldevList[iport].deviceId,
               acceldevList[iport].devpath[0], acceldevList[iport].funcHwid, acceldevList[iport].accelfunc);
      }
   }

   ret = 0;

   if (sysdir != NULL)
      closedir(sysdir);
   return ret;
}


// Load green bitstream to an Intel AFU
static int loadBitstream(t_acceldev *acceldev, t_accelfuncConf *accelfuncConf)
{
   char cmd[FS_PATH_MAX];

   sprintf(cmd, "fpgaconf -b %d -d %d -f %d %s/%s",
         acceldev->bdf.bus, acceldev->bdf.device, acceldev->bdf.function,
         intelOpaeEngine.bistreamPath, accelfuncConf->bistreamFile);
   if (system(cmd) != 0)
   {
      log_error("%s: Device %s: engine failed to load function %s", logtag, acceldev->bdf.str, accelfuncIndexToName(accelfuncConf->funcID));
      return -1;
   }

   // Update AFU UUID
   if (readAfuId(acceldev) < 0)
   {
      log_error("%s: Device %s: failed to read AFU id after reconfig", logtag, acceldev->bdf.str);
      return -1;
   }

   if (strcmp(acceldev->funcHwid, accelfuncConf->accelID) != 0)
   {
      log_error("%s: Device %s: expected AFU id %s after reconfig but has %s",
            logtag, acceldev->bdf.str, accelfuncConf->accelID, acceldev->funcHwid);
      return -1;
   }

   log_info("%s: Device %s: function %s loaded", logtag, acceldev->bdf.str, accelfuncIndexToName(accelfuncConf->funcID));

   return 0;
}



static t_accelOps intelOpaeOps = {
   .enumerate = enumerate,
   .loadBitstream = loadBitstream
};

t_accelEngine * intelOpaeRegister()
{
   intelOpaeEngine.sysentriesRW = (char **) intelSysentriesRW;
   intelOpaeEngine.nbsysentries = nitems(intelSysentriesRW);

   intelOpaeEngine.libsnames = (char **) intelAccelLibs;
   intelOpaeEngine.nblibs = nitems(intelAccelLibs);

   intelOpaeEngine.accelops = & intelOpaeOps;

   return & intelOpaeEngine;
}
