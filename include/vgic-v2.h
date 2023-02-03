#ifndef VGIC_V2_H
#define VGIC_V2_H

#include "vgic.h"
#include "types.h"

void vgic_v2_virq_set_target(struct vgic_irq *virq, u64 vcpuid);

#endif