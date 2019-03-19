/*
 * Xilinx AWS FPGA engine and accelerators
 *
 * NOTE: for now use static libfpga_pci.a to parse sysfs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>

#include "fpga_mgmt.h"

#include "accelerator.h"

#define AWS_FPFGA_LIB_MGMT "libfpga_mgmt.so"
#define AWS_FPFGA_DRIVER "xdma"

#define XILINK_SYSFS_DEVPATH_FMT "/sys/bus/pci/devices/0000:" PCI_BDF_FMT

static t_accelEngine xilinxAwsEngine = {
   .name = "XilinxAWS",
   .bistreamPath = "",  // unused
   .reconfigPhysfn = true,
   .reconfigVirtfn = false,
   .sriovMode = false
};

static char *logtag = xilinxAwsEngine.name;

// Accelerator device sysfs entries needing user read/write access on host
static const char * const xilinxSysentriesRW[] = {
#ifndef XILINX_DEBUG
   "resource0",
   "resource4"
#endif
};


#ifndef XILINX_DEBUG
// Enumerate all FPGA engines and accelerators
static int enumerate(t_acceldev acceldevList[], int *nbAcceldev)
{
   int (*fpga_pci_get_all_slot_specs)(struct fpga_slot_spec fpgaSlot[], int size);
   int (*fpga_mgmt_describe_local_image)(int slot_id, struct fpga_mgmt_image_info *info, uint32_t flags);
   void *handle;

   struct fpga_slot_spec fpgaSlot[FPGA_SLOT_MAX];
   struct fpga_mgmt_image_info info;
   char devpath[FS_PATH_MAX];
   int islot, idev;

   // Load fpga_mgmt library
   handle = dlopen(AWS_FPFGA_LIB_MGMT, RTLD_NOW);
   if (handle == NULL)
   {
      log_warn("%s: library %s not installed", logtag, AWS_FPFGA_LIB_MGMT);
      return 0;
   }
   fpga_pci_get_all_slot_specs = (int (*)()) dlsym(handle, "fpga_pci_get_all_slot_specs");
   fpga_mgmt_describe_local_image = (int (*)()) dlsym(handle, "fpga_mgmt_describe_local_image");
   if ((! fpga_pci_get_all_slot_specs) || (! fpga_mgmt_describe_local_image))
   {
      log_error("%s: library %s: symbol not found", logtag, AWS_FPFGA_LIB_MGMT);
      dlclose(handle);
      return -1;
   }

   // Get all FPGA slots from sysfs
   memset(fpgaSlot, 0, sizeof(fpgaSlot));
   if (fpga_pci_get_all_slot_specs(fpgaSlot, nitems(fpgaSlot)) < 0)
   {
      log_error("%s: failed to get FPGA slots", logtag);
      dlclose(handle);
      return -1;
   }
   for (islot = 0; islot < (int) nitems(fpgaSlot); ++islot)
   {
      if (fpgaSlot[islot].map[FPGA_APP_PF].vendor_id == 0)
         continue;

      // Get image info
      memset(&info, 0, sizeof(struct fpga_mgmt_image_info));
      if (fpga_mgmt_describe_local_image(islot, &info, 0) < 0)
      {
         log_error("%s: slot %d: failed to get image info", logtag, islot);
         dlclose(handle);
         return -1;
      }

      idev = *nbAcceldev;

      acceldevList[idev].slotId = islot;
      acceldevList[idev].pcifnType = PCIFUNC_PHYSICAL;
      acceldevList[idev].enginetype = ACCEL_ENGINE_XILINX;
      acceldevList[idev].accelfunc = acceleratorFuncHwidToIndex(ACCEL_ENGINE_XILINX, info.ids.afi_id);
      strcpy(acceldevList[idev].funcHwid, info.ids.afi_id);
      acceldevList[idev].vendorId = fpgaSlot[islot].map[FPGA_APP_PF].vendor_id;
      acceldevList[idev].deviceId = fpgaSlot[islot].map[FPGA_APP_PF].device_id;
      acceldevList[idev].bdf.bus = fpgaSlot[islot].map[FPGA_APP_PF].bus;
      acceldevList[idev].bdf.device = fpgaSlot[islot].map[FPGA_APP_PF].dev;
      acceldevList[idev].bdf.function = fpgaSlot[islot].map[FPGA_APP_PF].func;
      snprintf(acceldevList[idev].bdf.str, PCI_BDF_LEN, PCI_BDF_FMT,
            acceldevList[idev].bdf.bus, acceldevList[idev].bdf.device, acceldevList[idev].bdf.function);

      // Get all driver entries /dev/xdma0*
      snprintf(devpath, FS_PATH_MAX, "%s/%s%d*", LINUX_DEV_PATH, AWS_FPFGA_DRIVER, islot);
      fspathGetEntries(devpath, acceldevList[idev].devpath, NB_DEVPATH_MAX);
      // Both accelerator sysfs path and engine sysfs path need to be mounted to container
      snprintf(acceldevList[idev].syspathAccel, FS_PATH_MAX, XILINK_SYSFS_DEVPATH_FMT,
            fpgaSlot[islot].map[FPGA_APP_PF].bus, fpgaSlot[islot].map[FPGA_APP_PF].dev, fpgaSlot[islot].map[FPGA_APP_PF].func);
      snprintf(acceldevList[idev].syspathEngine, FS_PATH_MAX, XILINK_SYSFS_DEVPATH_FMT,
            fpgaSlot[islot].map[FPGA_MGMT_PF].bus, fpgaSlot[islot].map[FPGA_MGMT_PF].dev, fpgaSlot[islot].map[FPGA_MGMT_PF].func);

      (*nbAcceldev) ++;
   }

   dlclose(handle);
   return 0;
}
#else
// Fake accel device for tests
static int enumerate(t_acceldev acceldevList[], int *nbAcceldev)
{
   int idev = *nbAcceldev;

   acceldevList[idev].slotId = 0;
   acceldevList[idev].pcifnType = PCIFUNC_PHYSICAL;
   acceldevList[idev].enginetype = ACCEL_ENGINE_XILINX;
   acceldevList[idev].accelfunc = 2;  // sha512
   strcpy(acceldevList[idev].funcHwid, "agfi-0b55312dafbf39918");
   acceldevList[idev].vendorId = 0x1000;
   acceldevList[idev].deviceId = 0x1000;
   acceldevList[idev].bdf.bus = 6;
   acceldevList[idev].bdf.device = 0;
   acceldevList[idev].bdf.function = 0;
   snprintf(acceldevList[idev].bdf.str, PCI_BDF_LEN, PCI_BDF_FMT,
         acceldevList[idev].bdf.bus, acceldevList[idev].bdf.device, acceldevList[idev].bdf.function);
   fspathGetEntries("/dev/ttyS2*", acceldevList[idev].devpath, NB_DEVPATH_MAX);
//   snprintf(acceldevList[idev].syspathAccel, FS_PATH_MAX, XILINK_SYSFS_DEVPATH_FMT, 0, 0x1f, 2);
//   snprintf(acceldevList[idev].syspathEngine, FS_PATH_MAX, XILINK_SYSFS_DEVPATH_FMT, 0, 0x1f, 3);
   (*nbAcceldev) ++;
   log_debug("%s: return one fake accel device", logtag);
   return 0;
}
#endif


// Load blue bitstream to Xilinx accelerator
static int loadBitstream(t_acceldev *acceldev, t_accelfuncConf *accelfuncConf)
{
   int (*fpga_mgmt_describe_local_image)(int slot_id, struct fpga_mgmt_image_info *info, uint32_t flags);
   struct fpga_mgmt_image_info info;
   char cmd[FS_PATH_MAX];
   void *handle;

    // Add --request-timeout ??
   sprintf(cmd, "fpga-load-local-image -S %d -I %s", acceldev->slotId, accelfuncConf->accelID);
   if (system(cmd) != 0)
   {
      log_error("%s: Device %s: engine failed to load function %s", logtag, acceldev->bdf.str, accelfuncIndexToName(accelfuncConf->funcID));
      return -1;
   }

   // Reload image info
   if ((handle = dlopen(AWS_FPFGA_LIB_MGMT, RTLD_NOW)) != NULL)
   {
      fpga_mgmt_describe_local_image = (int (*)()) dlsym(handle, "fpga_mgmt_describe_local_image");
      if (fpga_mgmt_describe_local_image != NULL)
      {
         memset(&info, 0, sizeof(struct fpga_mgmt_image_info));
         if (fpga_mgmt_describe_local_image(acceldev->slotId, &info, 0) < 0)
         {
            log_error("%s: slot %d: failed to get image info", logtag, acceldev->slotId);
            return -1;
         }
         strcpy(acceldev->funcHwid, info.ids.afi_id);
      }
      dlclose(handle);
   }

   // check slot contains expected image
   if (strcmp(acceldev->funcHwid, accelfuncConf->accelID) != 0)
   {
      log_error("%s: Device %s: expected AGFI id %s after reconfig but has %s",
            logtag, acceldev->bdf.str, accelfuncConf->accelID, acceldev->funcHwid);
      return -1;
   }

   log_info("%s: Device %s: function %s loaded", logtag, acceldev->bdf.str, accelfuncIndexToName(accelfuncConf->funcID));

   return 0;
}


static t_accelOps xilinxAwsOps = {
   .enumerate = enumerate,
   .loadBitstream = loadBitstream
};

t_accelEngine * xilinxAwsRegister()
{
   xilinxAwsEngine.sysentriesRW = (char **) xilinxSysentriesRW;
   xilinxAwsEngine.nbsysentries = nitems(xilinxSysentriesRW);

   xilinxAwsEngine.nblibs = 0;

   xilinxAwsEngine.accelops = & xilinxAwsOps;

   return & xilinxAwsEngine;
}


