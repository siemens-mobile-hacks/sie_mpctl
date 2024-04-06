#include <sie/sie.h>

char CFG_PATH[] = "0:\\zbin\\etc\\SieMPCtl.bcfg";

void InitConfig() {
    __CONFIG_EXTERN(0, cfghdr_0);
    if (!Sie_Config_Load(CFG_PATH, __CONFIG(0, cfghdr_0))) {
        CFG_PATH[0] = '4';
        Sie_Config_Load(CFG_PATH, __CONFIG(0, cfghdr_0));
    }
}
