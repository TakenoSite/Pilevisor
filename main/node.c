#include "types.h"
#include "param.h"
#include "vcpu.h"
#include "node.h"
#include "nodectl.h"
#include "msg.h"
#include "lib.h"
#include "log.h"
#include "cluster.h"

/* node 0(bootstrap node) controller */

static void initvm() {
  struct vm_desc *desc = localnode.vm_desc;

  struct guest *os = desc->os_img;
  struct guest *fdt = desc->fdt_img;
  struct guest *initrd = desc->initrd_img;

  if(!os)
    panic("guest-os img is required");

  if(desc->nallocate % PAGESIZE != 0)
    panic("invalid mem size");

  vmm_log("[vm] create vm `%s`\n", os->name);
  vmm_log("[vm] use %d vcpu(s)\n", desc->nvcpu);
  vmm_log("[vm] allocated ram: %d byte\n", desc->nallocate);
  vmm_log("[vm] img_start %p img_size %p byte\n", os->start, os->size);
  if(fdt)
    vmm_log("[vm] fdt_start %p fdt_size %p byte\n", fdt->start, fdt->size);
  else
    vmm_log("[vm] fdt not found\n");
  if(initrd)
    vmm_log("[vm] initrd_start %p initrd_size %p byte\n", initrd->start, initrd->size);
  else
    vmm_log("[vm] initrd not found\n");

  map_guest_image(localnode.vttbr, os, desc->entrypoint);
  if(fdt)
    map_guest_image(localnode.vttbr, fdt, desc->fdt_base);
  if(initrd)
    map_guest_image(localnode.vttbr, initrd, desc->initrd_base);
}

static void wait_for_init_ack() {
  // TODO: now Node 1 only
  while(cluster_node(1)->status != NODE_ACK)
    wfi();
}

static void wait_for_sub_init_done() {
  // TODO: now Node 1 only
  while(cluster_node(1)->status != NODE_ONLINE)
    wfi();
}

static void node0_init_vcpu0() {
  vcpu_initstate();

  current->reg.elr = localnode.vm_desc->entrypoint;
  current->reg.x[0] = localnode.vm_desc->fdt_base;

  current->online = true;
}

static void node0_init() {
  localnode.nodeid = 0;

  initvm();

  /* me */
  cluster_ack_node(localnode.nic->mac, localnode.nvcpu, localnode.nalloc);

  /* send initialization request to sub-node */
  node0_broadcast_init_request();
  wait_for_init_ack();

  /* broadcast cluster information to sub-node */
  node0_broadcast_cluster_info();
  wait_for_sub_init_done();

  cluster_node_me_init();

  /* init vcpu0 */
  node0_init_vcpu0();
}

static void node0_start() {
  vmm_log("node0@cpu%d: start\n", cpuid());
  cluster_dump();

  intr_enable();

  wait_for_current_vcpu_online();

  vcpu_entry();
}

static struct nodectl node0_ctl = {
  .init = node0_init,
  .start = node0_start,
};

void nodectl_init() {
  localnode.ctl = &node0_ctl;
}
