#include "aarch64.h"
#include "tlb.h"
#include "mm.h"
#include "memory.h"
#include "param.h"
#include "memlayout.h"
#include "panic.h"
#include "lib.h"
#include "earlycon.h"
#include "iomem.h"
#include "device.h"
#include "printf.h"
#include "compiler.h"
#include "assert.h"

struct system_memory system_memory;

char vmm_pagetable[4096] __aligned(4096);

static int root_level;

u64 pvoffset;

extern char __boot_pgt_l1[];

void set_ttbr0_el2(u64 ttbr0_el2);

u64 *pagewalk(u64 *pgt, u64 va, int root, int create) {
  for(int level = root; level < 3; level++) {
    u64 *pte = &pgt[PIDX(level, va)];

    if((*pte & PTE_VALID) && (*pte & PTE_TABLE)) {
      pgt = P2V(PTE_PA(*pte));
    } else if(create) {
      pgt = alloc_page();
      if(!pgt)
        panic("nomem");

      *pte = PTE_PA(V2P(pgt)) | PTE_TABLE | PTE_VALID;
    } else {
      /* unmapped */
      return NULL;
    }
  }

  return &pgt[PIDX(3, va)];
}

static void __memmap(u64 va, u64 pa, u64 size, u64 memflags) {
  if(!PAGE_ALIGNED(pa) || !PAGE_ALIGNED(va) || size % PAGESIZE != 0)
    panic("__memmap");

  for(u64 p = 0; p < size; p += PAGESIZE, va += PAGESIZE, pa += PAGESIZE) {
    u64 *pte = pagewalk(vmm_pagetable, va, root_level, 1);

    if(*pte & PTE_AF)
      panic("this entry has been used: %p", va);

    *pte = PTE_PA(pa) | PTE_AF | PTE_V | memflags;
  }
}

static void __pagemap(u64 va, u64 pa, u64 memflags) {
  u64 *pte = pagewalk(vmm_pagetable, va, root_level, 1);
  if(*pte & PTE_AF)
      panic("this entry has been used: %p", va);

  *pte = PTE_PA(pa) | PTE_AF | PTE_V | memflags;
}

static inline int hugepage_level(u64 size) {
  switch(size) {
    case BLOCKSIZE_L2:    /* 2 MB */
      return 2;
    case BLOCKSIZE_L1:    /* 1 GB */
      return 1;
  }

  return 0;
}

static int memmap_huge(u64 va, u64 pa, u64 memflags, u64 size) {
  u64 *hugepte;
  u64 *pgt = vmm_pagetable;

  if(pa % size != 0 || va % size != 0)
    return -1;

  int level = hugepage_level(size);
  if(!level)
    return -1;

  for(int lv = root_level; lv < level; lv++) {
    u64 *pte = &pgt[PIDX(lv, va)];

    if((*pte & PTE_VALID) && (*pte & PTE_TABLE)) {
      pgt = (u64 *)PTE_PA(*pte);
    } else {
      pgt = alloc_page();
      if(!pgt)
        panic("nomem");

      *pte = PTE_PA(V2P(pgt)) | PTE_TABLE | PTE_VALID;
    }
  }

  hugepte = &pgt[PIDX(level, va)];

  if((*hugepte & PTE_TABLE) || (*hugepte & PTE_AF))
    return -1;

  *hugepte |= PTE_PA(pa) | PTE_AF | PTE_VALID | memflags;

  return 0;
}

/*
 *  mapping device memory
 */
void *iomap(u64 pa, u64 size) {
  u64 memflags = PTE_DEVICE_nGnRE | PTE_XN;

  size = PAGE_ALIGN(size);

  void *va = iomalloc(pa, size);

  for(u64 p = 0; p < size; p += PAGESIZE) {
    u64 *pte = pagewalk(vmm_pagetable, (u64)va + p, root_level, 1);
    if(*pte & PTE_AF) {
      panic("bad iomap");
    }
    
    *pte = PTE_PA(pa + p) | PTE_AF | PTE_V | memflags;
  }

  return (void *)va;
}

void dump_par_el1(void) {
  u64 par = read_sysreg(par_el1);

  if(par & 1) {
    printf("translation fault\n");
    printf("FST : %p\n", (par >> 1) & 0x3f);
    printf("PTW : %p\n", (par >> 8) & 1);
    printf("S   : %p\n", (par >> 9) & 1);
  } else {
    printf("address: %p\n", par);
  }
} 

static u64 at_hva2pa(u64 hva) {
  u64 tmp = read_sysreg(par_el1);

  asm volatile ("at s1e2r, %0" :: "r"(hva));

  u64 par = read_sysreg(par_el1);

  write_sysreg(par_el1, tmp);

  if(par & 1) {
    return 0;
  } else {
    return (par & 0xfffffffff000ul) | PAGE_OFFSET(hva);
  }
}

static void *remap_fdt(u64 fdt_base) {
  u64 memflags = PTE_NORMAL | PTE_SH_INNER | PTE_RO | PTE_XN;
  u64 offset;
  int rc;

  offset = fdt_base & (BLOCKSIZE_L2 - 1);
  fdt_base = fdt_base & ~(BLOCKSIZE_L2 - 1);
  /* map 2MB */
  rc = memmap_huge(FDT_SECTION_BASE, fdt_base, memflags, 0x200000);
  if(rc < 0)
    return NULL;

  return (void *)(FDT_SECTION_BASE + offset);
}

static u64 early_phys_start, early_phys_end;

static void remap_kernel() {
  u64 start_phys = at_hva2pa(VMM_SECTION_BASE);
  u64 size = (u64)vmm_end - (u64)vmm_start;
  u64 memflags, i;

  early_phys_start = start_phys;

  pvoffset = VMM_SECTION_BASE - start_phys;

  for(i = 0; i < size; i += PAGESIZE) {
    u64 p = start_phys + i;
    u64 v = VMM_SECTION_BASE + i;

    memflags = PTE_NORMAL | PTE_SH_INNER;

    if(!is_vmm_text(v))
      memflags |= PTE_XN;

    if(is_vmm_text(v) || is_vmm_rodata(v))
      memflags |= PTE_RO;

    __pagemap(v, p, memflags);
  }

  early_phys_end = start_phys + i;
}

static void remap_earlycon() {
  u64 flags = PTE_DEVICE_nGnRE | PTE_XN;

  __pagemap(EARLY_PL011_BASE, EARLY_PL011_BASE, flags);
}

static void map_memory(void *fdt) {
  device_tree_init(fdt);

  /* system_memory available here */

  u64 vbase = VIRT_BASE;
  u64 pbase = system_memory_base();
  u64 memflags;
  struct memblock *mem;
  int nslot = system_memory.nslot;

  for(mem = system_memory.slots; mem < &system_memory.slots[nslot]; mem++) {
    u64 phys_off = mem->phys_start - pbase;
    u64 size = mem->size;

    for(u64 i = 0; i < size; i += PAGESIZE) {
      u64 p = mem->phys_start + i;
      u64 kv = phys2kern(p);
      memflags = PTE_NORMAL | PTE_SH_INNER;

      if(!is_vmm_text(kv))
        memflags |= PTE_XN;

      if(is_vmm_text(kv) || is_vmm_rodata(kv))
        memflags |= PTE_RO;

      __pagemap(vbase + phys_off + i, p, memflags);
    }
  }
}

void *setup_pagetable(u64 fdt_base) {
  void *virt_fdt;

  root_level = 1;

  assert(PAGE_ALIGNED(vmm_pagetable));

  memset(vmm_pagetable, 0, PAGESIZE);

  remap_kernel();
  remap_earlycon();

  virt_fdt = remap_fdt(fdt_base);

  set_ttbr0_el2(V2P(vmm_pagetable));

  map_memory(virt_fdt);

  return virt_fdt;
}
