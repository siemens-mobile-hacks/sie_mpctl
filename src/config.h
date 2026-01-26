#pragma once

#include <cfg_items.h>

typedef struct {
    const CFG_HDR cfghdr_0;
    char host[128];
    const CFG_HDR cfghdr_1;
    char mp_csm_addr[16];
} CONFIG;

extern CONFIG CFG;
extern char CFG_PATH[];

void InitConfig();
