#include <swilib.h>
#include "config.h"

CONFIG CFG = {
    {CFG_STR_WIN1251, "Host", 0, 127},
    "95.31.215.212",
    {CFG_STR_WIN1251, "MP CSM addr", 0, 15},
    "A068ED54",
};

char CFG_PATH[] = "?:\\zbin\\etc\\SieMPCtl.bcfg";

void InitConfig() {
    CFG_PATH[0] = BCFG_GetDefaultDisk();
    if (BCFG_LoadConfig(CFG_PATH, &CFG, sizeof(CONFIG)) == -1) {
        BCFG_SaveConfig(CFG_PATH, &CFG, sizeof(CONFIG));
    }
}
