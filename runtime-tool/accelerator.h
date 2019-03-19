#ifndef __INCLUDE_ACCELERATOR_DEVICE_H__
#define __INCLUDE_ACCELERATOR_DEVICE_H__

#include <stdint.h>
#include <stdbool.h>

#include "utils.h"

#define LINUX_DEV_PATH  "/dev"
#define PCI_BDF_FMT "%02x:%02x.%x"

#define ACCEL_DEVICE_ENGINE_MAX 64
#define ACCEL_DEVICE_MAX       256
#define PCI_BDF_LEN        10
#define FUNCTION_HWID_LEN 128
#define ENGINE_NAME_LEN    64


//-----------------------
// Acceleration functions
//-----------------------

#define ACCELFUNC_UNKNOWN (-1)

typedef struct {
   int  funcID;
   char accelID[FUNCTION_HWID_LEN];  // intel: AFU UUID, aws: AGFI id
   int  nbHugepage2M;
   int  nbHugepage1G;
   char bistreamFile[FILE_NAME_MAX];
} t_accelfuncConf;


//---------------------
// Acceleration devices
//---------------------

typedef enum {
   ACCEL_ENGINE_INTEL = 0,
   ACCEL_ENGINE_XILINX,
   ACCEL_ENGINE_MAX
} e_accelengine;

typedef enum {
   PCIFUNC_PHYSICAL,
   PCIFUNC_VIRTUAL
} e_pciFunction;

typedef struct {
   int  bus;
   int  device;
   int  function;
   char str[PCI_BDF_LEN];
} t_pcibdf;

// AWS xdma driver has many entries per device, ex /dev/xdma0_*
#define NB_DEVPATH_MAX 64

typedef struct {
   e_accelengine enginetype;
   int      accelfunc; // function currently loaded into device
   char     funcHwid[FUNCTION_HWID_LEN];
   char     devpath[NB_DEVPATH_MAX][FS_PATH_MAX];
   char     syspathAccel[FS_PATH_MAX];  // accel  device syspath to be mounted RW to container (if not empty)
   char     syspathEngine[FS_PATH_MAX]; // engine device syspath to be mounted RW to container (if not empty)
   int      slotId;
   int      vendorId;
   int      deviceId;
   t_pcibdf bdf;
   e_pciFunction pcifnType;
   void    *privdata;
} t_acceldev;


//---------------------
// Acceleration engines
//---------------------

typedef struct {
  int (*enumerate)(t_acceldev acceldevList[], int *nbAcceldev);
  int (*loadBitstream)(t_acceldev *acceldev, t_accelfuncConf *accelfuncConf);
} t_accelOps;

typedef struct {
   char src[FS_PATH_MAX];
   char dst[FS_PATH_MAX];
   bool rdonly;
} t_mountpath;

typedef struct {
   bool installed;
   char name[ENGINE_NAME_LEN];
   char bistreamPath[FS_PATH_MAX];
   bool reconfigPhysfn;
   bool reconfigVirtfn;
   bool sriovMode;

   t_mountpath *mountlist;
   size_t nbmount;

   t_accelfuncConf *funclist;
   size_t nbfunc;

   char **sysentriesRW;
   size_t nbsysentries;

   char **libsnames;
   char **libspaths;
   size_t nblibs;

   t_accelOps *accelops;
} t_accelEngine;



int acceleratorReadConf(char *conffile);
int acceleratorEnumerate();
void acceleratorEnd();

int acceleratorAddAlldev(t_acceldev **attachdevList, int *nbAttachdev);
int acceleratorAddDev(char *device, t_acceldev **attachdevList, int *nbAttachdev);
bool acceleratorReconfigSupport(t_acceldev *acceldev, e_pciFunction pcifnType);
int acceleratorLoadBitstream(t_acceldev *acceldev, int accelfunc);
int acceleratorHugepage2M(t_acceldev *acceldev);
int acceleratorHugepage1G(t_acceldev *acceldev);
int acceleratorFuncHwidToIndex(e_accelengine enginetype, char *hwid);

int accelengineHostDeviceSetup(e_accelengine enginetype, t_acceldev *acceldev);
int accelengineMountPaths(char *rootfs, e_accelengine enginetype);
int accelengineAttachLibs(char *rootfs, e_accelengine enginetype);

t_accelEngine * intelOpaeRegister();
t_accelEngine * xilinxAwsRegister();

int containerSetup(pid_t pid, char *rootfs, t_acceldev **acceldevList, int nbAcceldev);

int accelSettingsReadConf(char *conffile, t_accelEngine * accelEngineList[]);
int accelfuncNameToIndex(char *funcName);
char *accelfuncIndexToName(int accelfunc);


#endif // __INCLUDE_ACCELERATOR_DEVICE_H__

