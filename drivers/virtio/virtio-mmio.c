#include "types.h"
#include "log.h"
#include "param.h"
#include "allocpage.h"
#include "lib.h"
#include "virtio.h"
#include "virtio-mmio.h"
#include "irq.h"
#include "malloc.h"
#include "mm.h"
#include "panic.h"
#include "memlayout.h"

#define LO(addr)  (u32)((u64)(addr) & 0xffffffff)
#define HI(addr)  (u32)(((u64)(addr) >> 32) & 0xffffffff)

static void vtmmio_intr(void *arg);

int virtio_net_probe(struct virtio_mmio_dev *dev);

static inline u32 vtmmio_read(struct virtio_mmio_dev *dev, u32 off) {
  return *(volatile u32 *)((volatile char *)dev->base + off);
}

static inline void vtmmio_write(struct virtio_mmio_dev *dev, u32 off, u32 val) {
  *(volatile u32 *)((volatile char *)dev->base + off) = val;
  dsb(sy);
}

static int vtmmio_probe(void *base, int intid) {
  struct virtio_mmio_dev *dev = malloc(sizeof(*dev));

  dev->base = iomap((u64)base, 0x200);
  dev->intid = intid;
  dev->vqs = NULL;

  if(vtmmio_read(dev, VIRTIO_MMIO_MAGICVALUE) != VIRTIO_MAGIC) {
    vmm_warn("virtio_mmio: invalid magic\n");
    goto err;
  }

  if(vtmmio_read(dev, VIRTIO_MMIO_VERSION) != 2) {
    vmm_warn("virtio_mmio: support v2 only\n");
    goto err;
  }

  u8 status = 0;
  vtmmio_write(dev, VIRTIO_MMIO_STATUS, status);
  status = vtmmio_read(dev, VIRTIO_MMIO_STATUS) | DEV_STATUS_ACKNOWLEDGE;
  vtmmio_write(dev, VIRTIO_MMIO_STATUS, status);
  status = vtmmio_read(dev, VIRTIO_MMIO_STATUS) | DEV_STATUS_DRIVER;
  vtmmio_write(dev, VIRTIO_MMIO_STATUS, status);

  irq_register(intid, vtmmio_intr, dev); 

  u32 devid = vtmmio_read(dev, VIRTIO_MMIO_DEVICE_ID);

  dev->dev_id = devid;

  switch(devid) {
    case VIRTIO_DEV_NET:
      return virtio_net_probe(dev);
    default:
      vmm_warn("unknown device: %d\n");
      goto err;
  }

err:
  return -1;
}

int vtmmio_driver_ok(struct virtio_mmio_dev *dev) {
  u32 status = vtmmio_read(dev, VIRTIO_MMIO_STATUS);

  vtmmio_write(dev, VIRTIO_MMIO_STATUS, status | DEV_STATUS_DRIVER_OK);

  if(!(vtmmio_read(dev, VIRTIO_MMIO_STATUS) & DEV_STATUS_DRIVER_OK))
    return -1;

  return 0;
}

int vtmmio_negotiate(struct virtio_mmio_dev *dev, u64 features) {
  u32 host_f1, host_f0;

  vtmmio_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
  host_f1 = vtmmio_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);

  vtmmio_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
  host_f0 = vtmmio_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);

  u64 host_features = ((u64)host_f1 << 32) | host_f0;

  features &= host_features;

  printf("virtio: features %p\n", features);

  vtmmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
  vtmmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, (u32)(features >> 32));

  vtmmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
  vtmmio_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, (u32)features);

  u32 status = vtmmio_read(dev, VIRTIO_MMIO_STATUS);

  vtmmio_write(dev, VIRTIO_MMIO_STATUS, status | DEV_STATUS_FEATURES_OK);

  if(!(vtmmio_read(dev, VIRTIO_MMIO_STATUS) & DEV_STATUS_FEATURES_OK))
    return -1;

  return 0;
}

static void vtmmio_vqlist_add(struct virtio_mmio_dev *dev, struct virtq *vq) {
  vq->next = dev->vqs;
  dev->vqs = vq;
}

int vtmmio_set_virtq(struct virtio_mmio_dev *dev, struct virtq *vq, int qsel) {
  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, qsel);

  int qmax = vtmmio_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(qmax < NQUEUE)
    return -1;

  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_NUM, NQUEUE);

  u64 p_desc = V2P(vq->desc);
  u64 p_avail = V2P(vq->avail);
  u64 p_used = V2P(vq->used);

  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, LO(p_desc));
  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, HI(p_desc));

  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_LOW, LO(p_avail));
  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, HI(p_avail));

  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_LOW, LO(p_used));
  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, HI(p_used));

  vtmmio_vqlist_add(dev, vq);

  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_READY, 1);

  return 0;
}

void vtmmio_notify(struct virtio_mmio_dev *dev, int qsel) {
  vtmmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, qsel);
}

static void vtmmio_intr(void *arg) {
  struct virtio_mmio_dev *dev = arg;

  u32 status = vtmmio_read(dev, VIRTIO_MMIO_INTERRUPT_STATUS);
  vtmmio_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, status);

  if(status & VIRTIO_MMIO_USED_BUFFER_NOTIFY) {
    for(struct virtq *vq = dev->vqs; vq != NULL; vq = vq->next) {
      if(vq->intr_handler)
        vq->intr_handler(vq);
      else
        panic("intrhandler?");
    }
  }

  if(status & VIRTIO_MMIO_CONFIG_NOTIFY) {
    /* ignored */
  }
}

/* FIXME */
#define VIRTIO0     0x0a000000

int virtio_mmio_init(void) {
  return vtmmio_probe((void *)VIRTIO0, 48);
}
