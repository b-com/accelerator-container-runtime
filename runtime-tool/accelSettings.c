#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <errno.h>
#include <json-c/json.h>

#include "accelerator.h"

#define ACCEL_JSON_GLOBAL         "global"
#define ACCEL_JSON_LOG_LEVEL          "loglevel"
#define ACCEL_JSON_FUNCTIONS      "accelerationFunctions"
#define ACCEL_JSON_FUNCTION_NAME      "name"
#define ACCEL_JSON_FUNCTION_DESC      "description"
#define ACCEL_JSON_ENGINES        "acceleratorEngines"
#define ACCEL_JSON_ENGINE_NAME            "name"
#define ACCEL_JSON_ENGINE_BS_LOCATION     "bitstreamLocation"
#define ACCEL_JSON_ENGINE_RECONFIG_PHYSFN "partialConfigPhysfn"
#define ACCEL_JSON_ENGINE_RECONFIG_VIRTFN "partialConfigVirtfn"
#define ACCEL_JSON_ENGINE_ACTIVATE_SRIOV  "activateSriov"
#define ACCEL_JSON_ENGINE_FUNCTIONS       "functions"
#define ACCEL_JSON_ENGINE_FUNC_NAME       "name"
#define ACCEL_JSON_ENGINE_FUNC_HWID       "hwID"
#define ACCEL_JSON_ENGINE_FUNC_HUGEPAGE2M "hugepage2M"
#define ACCEL_JSON_ENGINE_FUNC_HUGEPAGE1G "hugepage1G"
#define ACCEL_JSON_ENGINE_FUNC_BS_FILE    "bistreamFile"
#define ACCEL_JSON_ENGINE_XILINX_SDX_RTE  "xilinxSdxRTE"

#define ACCEL_ENGINE_XILINX_SDX_RTE_PATH  "/opt/Xilinx/SDx/rte"

#define FUNCTION_NAME_LEN  32
#define FUNCTION_DESC_LEN 256
typedef struct {
   char name[FUNCTION_NAME_LEN];
   char desc[FUNCTION_DESC_LEN];
} t_accelfunction;

static t_accelfunction *accelfuncList = NULL;
static int accelfuncNb = 0;


static int readConffile(char *filename, char **jsonData)
{
   FILE *pFd;
   int fileLen = -1;
   int readLen;
   int ret = 0;

   if ((! filename) || ( !jsonData))
   {
      log_error("%s assert error", __FUNCTION__);
      return -1;
   }

   pFd = fopen(filename, "r");
   if (! pFd)
   {
      log_error("Failed to open config file %s: %s", filename, strerror(errno));
      return -1;
   }

   // get filesize
   if (fseek(pFd, 0, SEEK_END) == 0)
      fileLen = ftell(pFd);
   if ((fileLen < 0) || (fseek(pFd, 0, SEEK_SET) < 0))
   {
      log_error("Failed to get size of config file %s: %s", filename, strerror(errno));
      fclose(pFd);
      return -1;
   }

   // allocate memory
   *jsonData = (char *)malloc(fileLen);
   if (! jsonData)
   {
      log_error("Memory allocation failed");
      fclose(pFd);
      return -1;
   }

   // read config file content
   readLen = fread(*jsonData, 1, fileLen, pFd);
   if (ferror(pFd))
   {
      log_error("Failed to read config file %s: %s", filename, strerror(errno));
      ret = -1;
   }
   else if (readLen != fileLen)
   {
      log_error("Read config file %s: Filesize %d and number of bytes read %d don't match", filename, fileLen, readLen);
      ret = -1;
   }
   fclose(pFd);

   if (ret < 0)
      free(*jsonData);
   return (ret);
}


static void dumpEnginesConf(t_accelEngine * accelEngineList[])
{
   int i,j;
   log_debug("BEGIN DUMP CONFIG");
   for (i = 0; i < accelfuncNb; i++)
   {
      log_debug("   Function %s : %s", accelfuncList[i].name, accelfuncList[i].desc);
   }
   for (i = 0; i < ACCEL_ENGINE_MAX; i++)
   {
      if (accelEngineList[i])
      {
         log_debug("   Engine %s: installed %d, physfn %d, virtfn %d, sriov %d, path %s", accelEngineList[i]->name,
               accelEngineList[i]->installed, accelEngineList[i]->reconfigPhysfn,
               accelEngineList[i]->reconfigVirtfn, accelEngineList[i]->sriovMode,
               accelEngineList[i]->bistreamPath);

         for (j = 0; j < accelEngineList[i]->nbfunc; j++)
         {
            log_debug("     fct %s: accelID %s, hugepage2M %d, hugepage1G %d, file %s",
                  accelfuncIndexToName(accelEngineList[i]->funclist[j].funcID), accelEngineList[i]->funclist[j].accelID,
                  accelEngineList[i]->funclist[j].nbHugepage2M, accelEngineList[i]->funclist[j].nbHugepage1G,
                  accelEngineList[i]->funclist[j].bistreamFile);
         }
      }
   }
   log_debug("END DUMP CONFIG");
}

int accelSettingsReadConf(char *conffile, t_accelEngine * accelEngineList[])
{
   char *jsonData;
   const char *jsonString;
   json_object *jsonRoot      = NULL;
   json_object *jsonGlobal    = NULL;
   json_object *jsonFuncList  = NULL;
   json_object *jsonFunc      = NULL;
   json_object *jsonEngineList= NULL;
   json_object *jsonEngine    = NULL;
   json_object *object        = NULL;
   int nbEngine;
   int ifunc, iengine=0, iconf;
   bool bret;

   if (readConffile(conffile, & jsonData))
   {
      return -1;
   }

   jsonRoot = json_tokener_parse(jsonData);
   if (jsonRoot == NULL)
   {
      log_error("Json failed to parse config file %s", conffile);
      free(jsonData);
      return -1;
   }

   // Get global section parameters
   if (json_object_object_get_ex(jsonRoot, ACCEL_JSON_GLOBAL, &jsonGlobal))
   {
      if (json_object_object_get_ex(jsonGlobal, ACCEL_JSON_LOG_LEVEL, &object))
      {
         jsonString = json_object_get_string(object);
         if (! strcmp(jsonString, "error"))
            logSetLevel(LOG_ERR);
         else if (! strcmp(jsonString, "info"))
            logSetLevel(LOG_INFO);
         else if (! strcmp(jsonString, "debug"))
            logSetLevel(LOG_DEBUG);
         else
            log_warn("log level %s unknown", jsonString);
      }
   }

   // Get list of acceleration functions
   bret = json_object_object_get_ex(jsonRoot, ACCEL_JSON_FUNCTIONS, & jsonFuncList);
   if (bret)
      accelfuncNb = json_object_array_length(jsonFuncList);
   if ((! bret) || (accelfuncNb == 0))
   {
      log_error("config file %s: no acceleration function found", conffile);
      free(jsonData);
      return -1;
   }

   accelfuncList = (t_accelfunction *) calloc(accelfuncNb, sizeof(t_accelfunction));
   if (! accelfuncList)
   {
      log_error("Memory allocation failed");
      free(jsonData);
      return -1;
   }
   for (ifunc = 0; ifunc < accelfuncNb; ifunc++)
   {
      jsonFunc = json_object_array_get_idx(jsonFuncList, ifunc);
      if (! jsonFunc)
         continue;

      if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_FUNCTION_NAME, &object))
      {
         strncpy(accelfuncList[ifunc].name, json_object_get_string(object), FUNCTION_NAME_LEN-1);
      }
      if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_FUNCTION_DESC, &object))
      {
         strncpy(accelfuncList[ifunc].desc, json_object_get_string(object), FUNCTION_DESC_LEN-1);
      }
   }

   // Get list of accelerator engines
   bret = json_object_object_get_ex(jsonRoot, ACCEL_JSON_ENGINES, & jsonEngineList);
   if (bret)
      nbEngine = json_object_array_length(jsonEngineList);
   if ((! bret) || (nbEngine == 0))
   {
      log_error("config file %s: no accelerator engine found", conffile);
      free(jsonData);
      return -1;
   }
   for (iconf = 0; iconf < nbEngine; iconf++)
   {
      jsonEngine = json_object_array_get_idx(jsonEngineList, iconf);
      if (! jsonEngine)
         continue;
      if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_NAME, &object))
      {
         jsonString = json_object_get_string(object);
         for (iengine = 0; iengine < ACCEL_ENGINE_MAX; iengine++)
         {
            if ((accelEngineList[iengine]) && (! strcasecmp(accelEngineList[iengine]->name, jsonString)))
            {
               log_debug("config file %s: read engine %s settings", conffile, jsonString);
               break;
            }
         }
         if (iengine == ACCEL_ENGINE_MAX)
         {
            log_warn("config file %s: unknown engine %s: ignore", conffile, jsonString);
            continue;
         }
      }
      if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_BS_LOCATION, &object))
      {
         strncpy(accelEngineList[iengine]->bistreamPath, json_object_get_string(object), FS_PATH_MAX-1);
      }
      if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_RECONFIG_PHYSFN, &object))
      {
         accelEngineList[iengine]->reconfigPhysfn = json_object_get_boolean(object);
      }
      if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_RECONFIG_VIRTFN, &object))
      {
         accelEngineList[iengine]->reconfigVirtfn = json_object_get_boolean(object);
      }
      if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_ACTIVATE_SRIOV, &object))
      {
         accelEngineList[iengine]->sriovMode = json_object_get_boolean(object);
      }

      // Xilinx: expect SDx realtime kernel path in config
      if (iengine == ACCEL_ENGINE_XILINX)
      {
         accelEngineList[iengine]->nbmount = 1;
         accelEngineList[iengine]->mountlist = (t_mountpath *) calloc(accelEngineList[iengine]->nbmount, sizeof(t_mountpath));
         if (! accelEngineList[iengine]->mountlist)
         {
            log_error("Memory allocation failed");
            free(jsonData);
            return -1;
         }
         if (json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_XILINX_SDX_RTE, &object))
         {
            strncpy(accelEngineList[iengine]->mountlist[0].src, json_object_get_string(object), FS_PATH_MAX-1);
         }
         if (strlen(accelEngineList[iengine]->mountlist[0].src) == 0)
            strcpy(accelEngineList[iengine]->mountlist[0].src, ACCEL_ENGINE_XILINX_SDX_RTE_PATH);
         strcpy(accelEngineList[iengine]->mountlist[0].dst, ACCEL_ENGINE_XILINX_SDX_RTE_PATH);
         accelEngineList[iengine]->mountlist[0].rdonly = true;
      }

      bret = json_object_object_get_ex(jsonEngine, ACCEL_JSON_ENGINE_FUNCTIONS, & jsonFuncList);
      if (bret)
         accelEngineList[iengine]->nbfunc = json_object_array_length(jsonFuncList);
      if ((! bret) || (accelEngineList[iengine]->nbfunc == 0))
      {
         log_warn("config file %s: no acceleration function found", conffile);
      }
      else
      {
         accelEngineList[iengine]->funclist = (t_accelfuncConf *) calloc(accelEngineList[iengine]->nbfunc, sizeof(t_accelfuncConf));
         if (! accelfuncList)
         {
            log_error("Memory allocation failed");
            free(jsonData);
            return -1;
         }
         for (ifunc = 0; ifunc < accelEngineList[iengine]->nbfunc; ifunc++)
         {
            accelEngineList[iengine]->funclist[ifunc].funcID = ACCELFUNC_UNKNOWN;
            jsonFunc = json_object_array_get_idx(jsonFuncList, ifunc);
            if (! jsonFunc)
               continue;

            if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_ENGINE_FUNC_NAME, &object))
            {
               jsonString = json_object_get_string(object);
               accelEngineList[iengine]->funclist[ifunc].funcID = accelfuncNameToIndex((char *)jsonString);
               if (accelEngineList[iengine]->funclist[ifunc].funcID == ACCELFUNC_UNKNOWN)
               {
                  log_warn("config file %s: engine %s: unknown function %s: ignore", conffile, accelEngineList[iengine]->name, jsonString);
                  continue;
               }
            }
            if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_ENGINE_FUNC_HWID, &object))
            {
              strncpy(accelEngineList[iengine]->funclist[ifunc].accelID, json_object_get_string(object), FUNCTION_HWID_LEN-1);
            }
            if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_ENGINE_FUNC_HUGEPAGE2M, &object))
            {
               accelEngineList[iengine]->funclist[ifunc].nbHugepage2M = json_object_get_int(object);
            }
            if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_ENGINE_FUNC_HUGEPAGE1G, &object))
            {
               accelEngineList[iengine]->funclist[ifunc].nbHugepage1G = json_object_get_int(object);
            }
            if (json_object_object_get_ex(jsonFunc, ACCEL_JSON_ENGINE_FUNC_BS_FILE, &object))
            {
              strncpy(accelEngineList[iengine]->funclist[ifunc].bistreamFile, json_object_get_string(object), FILE_NAME_MAX-1);
            }
         }
      }
   } // for engine

   dumpEnginesConf(accelEngineList);

   free(jsonData);
   return 0;
}


int accelfuncNameToIndex(char *funcName)
{
   int ifunc;
   for (ifunc = 0; ifunc < accelfuncNb; ifunc++)
   {
      if (! strcasecmp(accelfuncList[ifunc].name, funcName))
         return ifunc;
   }
   return ACCELFUNC_UNKNOWN;
}

char *accelfuncIndexToName(int accelfunc)
{
   if ((accelfunc >= 0) && (accelfunc < accelfuncNb))
      return accelfuncList[accelfunc].name;
   else
      return "";
}
