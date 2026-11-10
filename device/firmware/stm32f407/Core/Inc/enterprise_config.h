#ifndef RG_ENTERPRISE_CONFIG_H
#define RG_ENTERPRISE_CONFIG_H
/* Public safe build defaults. Generate a private include with the build tool to provision a device. */
#ifndef RG_PRIVATE_CONFIG
#define RG_DEVICE_ID "STM32-CAM-001"
#define RG_API_BASE ""
#define RG_API_HOST ""
#define RG_DEVICE_TOKEN ""
#define RG_CELLULAR_ENABLED 0
#define RG_APN ""
#define RG_STORAGE_PROVISIONED 0
#define RG_STORAGE_UID ""
#define RG_STORAGE_BACKUP_SHA256 ""
static const char RG_CA_CERT[]="";
#else
#include "enterprise_private.h"
#endif
#endif
