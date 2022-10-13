#ifndef VARM_POC_VSM_H
#define VARM_POC_VSM_H

#include "types.h"
#include "param.h"
#include "memory.h"
#include "mm.h"

struct vcpu;
struct node;

#define CACHE_PAGE_NUM    64

#define CONFIG_PAGE_CACHE

/*
 *  cache page manager:
 *
 *  cache page flags
 *
 *  | ?????? | L | OOOOO | ?????????? |
 *   63    38  37 36   32 31         0
 *
 *  ?: reserved
 *  O: owner
 *  L: lock
 */

struct cache_page {
  u64 flags;
};

#define CACHE_PAGE_LOCK_BIT(p)  (((p)->flags >> 37) & 0x1)
#define CACHE_PAGE_OWNER_SHIFT  32
#define CACHE_PAGE_OWNER_MASK   0x1f
#define CACHE_PAGE_OWNER(p)     (((p)->flags >> CACHE_PAGE_OWNER_SHIFT) & CACHE_PAGE_OWNER_MASK)

/* 128 MiB per Node */
#define NR_CACHE_PAGES          (128*1024*1024 / PAGESIZE)

int vsm_access(struct vcpu *vcpu, char *buf, u64 ipa, u64 size, bool wr);
void *vsm_read_fetch_page(u64 page_ipa);
void *vsm_write_fetch_page(u64 page_ipa);

void vsm_init(void);
void vsm_node_init(struct memrange *mem);

#endif
