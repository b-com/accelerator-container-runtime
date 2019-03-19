/*
 * Docker runtime prestart hook
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <argp.h>

#include "accelerator.h"

#define ACCEL_SETTINGS_CONFFILE "/etc/acceleration.json"


static t_acceldev *attachDevList[ACCEL_DEVICE_MAX];
static int nbAttachDev = 0;


static error_t commandParser(int, char *, struct argp_state *);

static struct argp usage = {
   (const struct argp_option[]){
      {NULL, 0, NULL, 0, "Options:", -1},
      {"pid", 'p', "PID", 0, "Container PID", -1},
      {"rootfs", 'r', "ROOTFS", 0, "Container root filesystem", -1},
      {"devices", 'd', "DEV", 0, "List of requested accelerators", -1},
      {"functions", 'f', "FUNC", 0, "List of expected functions", -1},
      {"log", 'l', "FILE", 0, "Log file absolute path and name", -1},
      {"loglevel", 'L', "LEVEL", 0, "Log level (syslog facility)", -1},
      {"COMMAND:", 0, NULL, OPTION_DOC|OPTION_NO_USAGE, "", 0},
      //  {"info", 0, NULL, OPTION_DOC|OPTION_NO_USAGE, "Report information about the driver and devices", 0},
      //  {"list", 0, NULL, OPTION_DOC|OPTION_NO_USAGE, "List driver components", 0},
      {"  configure", 0, NULL, OPTION_DOC|OPTION_NO_USAGE, "Configure a container with accelerator support", 0},
      {0},
   },
   commandParser,
   "COMMAND",
   "Command line utility for manipulating accelerator containers.",
   NULL,
   NULL,
   NULL,
};

struct context
{
   int   logLevel;
   char *logFile;
   pid_t pid;
   char *rootfs;
   char *devices;
   char *functions;
   char *command;
};
static error_t commandParser(int key, char *arg, struct argp_state *state)
{
   struct context *ctx = state->input;
   switch (key) {
      case 'p':
         ctx->pid = atoi(arg);
         break;
      case 'r':
         ctx->rootfs = arg;
         break;
      case 'd':
         ctx->devices = arg;
         break;
      case 'f':
         ctx->functions = arg;
         break;
      case 'l':
         ctx->logFile = arg;
         break;
      case 'L':
         ctx->logLevel = atoi(arg);
         break;
      case ARGP_KEY_ARGS:
        state->argv += state->next;
         state->argc -= state->next;
         ctx->command = state->argv[0];
         break;
      default:
         return (ARGP_ERR_UNKNOWN);
   }
   return 0;
}


// Parse requested comma separated list of devices and find associated accelerator devices
static int getConfiguredDevices(char *devices)
{
   char *device;
   char *end;

   while ((device = strsep(&devices, ",")) != NULL)
   {
      // remove spaces
      if (strlen(device) > 0)
      {
         while (isspace((unsigned char)*device)) device++;
         end = device + strlen(device) - 1;
         while (end > device && isspace((unsigned char)*end)) end--;
         *(end+1) = 0;
      }
      if (strlen(device) == 0)
         continue;

      // if all devices requested, add all intel & xilinx accelerators to devices list
      if (strcasecmp(device, "all") == 0)
      {
         acceleratorAddAlldev(attachDevList, & nbAttachDev);
         break;
      }
      else
      {
         if (acceleratorAddDev(device, attachDevList, & nbAttachDev) < 0)
         {
            log_fatal("Accelerator device %s not found", device);
            return -1;
         }
      }
   }

   return 0;
}


// Parse requested comma separated list of functions.
// foreach (device, requested function)
//      if device already loaded with function, ok
//    elif device is a physical PCIe function and engine supports physical fn reconfig, load function
//    elif device is a virtual PCIe function  and engine supports virtual fn reconfig, load function
static int loadConfiguredFunctions(char *functions)
{
   char *function;
   char *end;
   int idev = 0;
   int accelfunc;
   int devAccelfunc[ACCEL_DEVICE_MAX];

   while ((function = strsep(&functions, ",")) != NULL)
   {
      // remove spaces
      if (strlen(function) > 0)
      {
         while (isspace((unsigned char)*function)) function++;
         end = function + strlen(function) - 1;
         while (end > function && isspace((unsigned char)*end)) end--;
         *(end+1) = 0;
      }
      if (strlen(function) == 0)
         continue;

      accelfunc = accelfuncNameToIndex(function);
      if (accelfunc == ACCELFUNC_UNKNOWN)
      {
         log_fatal("Acceleration function %s not supported", function);
         return -1;
      }

      devAccelfunc[idev++] = accelfunc;
   }

   if (idev == 0)
   {
      log_warn("Acceleration function(s) not provided: use accelerators current functions");
      return 0;
   }

   // if less functions configured than devices, all remaining devices will have same function
   // (alternative: if more than one function and less than devices, fatal error)
   for ( ; idev < nbAttachDev; idev ++)
   {
      devAccelfunc[idev] = accelfunc;
   }

   // Load expected accelerator functions if possible
   for (idev = 0 ; idev < nbAttachDev; idev ++)
   {
      if (attachDevList[idev]->accelfunc == devAccelfunc[idev])
      {
         log_info("Device %s: function %s already loaded", attachDevList[idev]->bdf.str, accelfuncIndexToName(accelfunc));
      }
      else if (acceleratorReconfigSupport(attachDevList[idev], attachDevList[idev]->pcifnType))
      {
         log_info("Device %s: try to load function %s ...",
               attachDevList[idev]->bdf.str, accelfuncIndexToName(devAccelfunc[idev]));

         if (acceleratorLoadBitstream(attachDevList[idev], devAccelfunc[idev]) < 0)
         {
            log_fatal("Device %s: failed to load function %s",
                  attachDevList[idev]->bdf.str, accelfuncIndexToName(devAccelfunc[idev]));
            return -1;
         }
      }
      else
      {
         log_fatal("Device %s has not function %s and is not reconfigurable",
               attachDevList[idev]->bdf.str, accelfuncIndexToName(devAccelfunc[idev]));
         return -1;
      }
   }

   return 0;
}


// Adjust host device files permissions
static int hostSetup(pid_t pid, t_acceldev **acceldevList, int nbAcceldev)
{
   int idev;
   int idevpath;

   for (idev = 0; idev < nbAcceldev; idev++)
   {
      // Set rw group+other permissions to device node
      for (idevpath = 0; idevpath < NB_DEVPATH_MAX; idevpath++)
      {
         if (strlen(acceldevList[idev]->devpath[idevpath]) == 0)
            continue;
         if (chmod(acceldevList[idev]->devpath[idevpath], 0666) < 0)
         {
            log_error("Device %s: failed to chmod %s: %s",
                  attachDevList[idev]->bdf.str, acceldevList[idev]->devpath[idevpath], strerror(errno));
            return -1;
         }
      }

      if (accelengineHostDeviceSetup(attachDevList[idev]->enginetype, attachDevList[idev]) < 0)
         return -1;

      log_info("Device %s: host files user permissions set", attachDevList[idev]->bdf.str);
   }

   return 0;
}

// Do configure command
static int doConfigure(struct context *ctx)
{
   log_info("Configure devices %s on root FS %s", ctx->devices, ctx->rootfs);

   if (getConfiguredDevices(ctx->devices) < 0)
   {
      return EXIT_FAILURE;
   }

   if (loadConfiguredFunctions(ctx->functions) < 0)
   {
      return EXIT_FAILURE;
   }

   if (hostSetup(ctx->pid, attachDevList, nbAttachDev) < 0)
   {
      log_fatal("Failed to setup host for accelerator(s) %s", ctx->devices);
      return EXIT_FAILURE;
   }

   if (containerSetup(ctx->pid, ctx->rootfs, attachDevList, nbAttachDev) < 0)
   {
      log_fatal("Failed to setup container for accelerator(s) %s", ctx->devices);
      return EXIT_FAILURE;
   }

   return EXIT_SUCCESS;
}


int main(int argc, char *argv[])
{
   int ret = EXIT_FAILURE;

   struct context ctx = { LOG_ERR, "", 0, "", "", "", "" };
   argp_parse(&usage, argc, argv, ARGP_IN_ORDER, NULL, &ctx);

   logOpen(ctx.logFile, ctx.logLevel);

   if (acceleratorReadConf(ACCEL_SETTINGS_CONFFILE) < 0)
   {
      log_fatal("Failed to read acceleration config");
   }
   else if (acceleratorEnumerate() < 0)
   {
      log_fatal("Failed to detect accelerator engine(s)");
   }
   else
   {
      if (!strcmp(ctx.command, "configure"))
      {
         ret = doConfigure(& ctx);
      }
      else
      {
         log_fatal("Unknown command %s", ctx.command);
      }
   }

   acceleratorEnd();
   logClose();
   return (ret);
}
