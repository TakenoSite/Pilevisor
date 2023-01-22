#include "types.h"
#include "device.h"
#include "malloc.h"
#include "localnode.h"
#include "lib.h"
#include "fdt.h"

static inline struct device_node *next_node(struct device_node *prev) {
  if(!prev)
    return localnode.device_tree;

  if(prev->child)
    return prev->child;

  if(!prev->next) {
    struct device_node *n = prev->parent;

    while(n->parent && !n->next)
      n = n->parent;

    return n->next;
  }

  return prev->next;
}

struct device_node *dt_node_alloc(struct device_node *parent) {
  struct device_node *node = malloc(sizeof(*node));
  if(!node)
    panic("nomem");

  node->parent = parent;
  node->child = NULL;
  node->name = NULL;
  node->prop = NULL;

  if(parent) {
    node->next = parent->child;
    parent->child = node;
  }

  return node;
}

struct property *dt_prop_alloc(struct device_node *node) {
  struct property *p = malloc(sizeof(*p));
  if(!p)
    panic("nomem");

  p->next = node->prop;
  node->prop = p;

  return p;
}

fdt32 *dt_node_prop_raw(struct device_node *node, const char *name, u32 *len) {
  struct property *p;

  for(p = node->prop; p; p = p->next) {
    if(strcmp(p->name, name) == 0) {
      if(len)
        *len = p->data_len;
      return p->data;
    }
  }

  return NULL;
}

int dt_node_propa(struct device_node *node, const char *name, u32 *buf) {
  if(!buf)
    return -1;

  struct property *p = node->prop;

  for(; p; p = p->next) {
    if(strcmp(p->name, name) == 0) {
      fdt32 *data = p->data;
      for(int i = 0; i < p->data_len / 4; i++) {
        fdt32 d = data[i];
        buf[i] = fdt32_to_u32(d);
      }

      return 0;
    }
  }

  return -1;
}

int dt_node_propa64(struct device_node *node, const char *name, u64 *buf) {
  if(!buf)
    return -1;

  struct property *p = node->prop;

  for(; p; p = p->next) {
    if(strcmp(p->name, name) == 0) {
      fdt64 *data = p->data;
      for(int i = 0; i < p->data_len / 8; i++) {
        fdt64 d = data[i];
        buf[i] = fdt64_to_u64(d);
      }

      return 0;
    }
  }

  return -1;
}

const char *dt_node_props(struct device_node *node, const char *name) {
  struct property *p = node->prop;

  for(; p; p = p->next) {
    if(strcmp(p->name, name) == 0)
      return (const char *)p->data;
  }

  return NULL;
}

bool dt_node_propb(struct device_node *node, const char *name) {
  struct property *p = node->prop;

  for(; p; p = p->next) {
    if(strcmp(p->name, name) == 0)
      return true;
  }

  return false;
}

static inline u64 prop_read_number(fdt32 *prop, int size) {
  u64 n = 0;

  while(size-- > 0) {
    n = (n << 32) | fdt32_to_u32(*prop++);
  }

  return n;
}

static int dt_bus_ncells(struct device_node *bus, u32 *addr_cells, u32 *size_cells) {
  u32 na, ns;
  int rc;

  rc = dt_node_propa(bus, "#address-cells", &na);
  if(rc < 0)
    return -1;

  rc = dt_node_propa(bus, "#size-cells", &ns);
  if(rc < 0)
    return -1;

  if(addr_cells)
    *addr_cells = na;
  if(size_cells)
    *size_cells = ns;

  return 0;
}

int dt_bus_addr_translate(struct device_node *bus_node, u64 local_addr, u64 *addr) {
  ;
}

int dt_node_prop_addr(struct device_node *node, int index, u64 *addr, u64 *size) {
  struct device_node *bus = node->parent;
  fdt32 *regs;
  u32 na, ns, len;
  int rc, i;

  rc = dt_bus_ncells(bus, &na, &ns);
  if(rc < 0)
    return -1;

  regs = dt_node_prop_raw(node, "reg", &len);
  if(!regs)
    return -1;

  len /= 4;

  i = index * (na + ns);

  if(i + na + ns > len)
    return -1;

  if(addr) {
    *addr = prop_read_number(regs + i, na);
  }

  if(size) {
    *size = prop_read_number(regs + i + na, ns);
  }

  return 0;
}

struct device_node *dt_find_node_type_cont(struct device_node *node, const char *type,
                                            struct device_node *cont) {
  if(!node || !type)
    return NULL;

  struct device_node *s = cont ? cont->next : node->child;

  for(struct device_node *child = s; child; child = child->next) {
    if(strcmp(child->device_type, type) == 0)
      return child;
  }

  return NULL;
}

struct device_node *dt_find_node_type(struct device_node *node, const char *type) {
  if(!node || !type)
    return NULL;

  for(struct device_node *child = node->child; child; child = child->next) {
    if(strcmp(child->device_type, type) == 0)
      return child;
  }

  return NULL;
}

struct device_node *dt_find_node_path(struct device_node *node, const char *path) {
  while(*path == '/')
    path++;

  for(struct device_node *child = node->child; child; child = child->next) {
    if(strcmp(child->name, path) == 0)
      return child;
  }

  return NULL;
}

struct device_node *next_match_node(struct dt_device *table, struct dt_device **dev,
                                    struct device_node *prev) {
  struct dt_device *d;

  for(struct device_node *n = next_node(prev); n; n = next_node(n)) {
    if((d = dt_compatible_device(table, n)) != NULL) {
      if(dev)
        *dev = d;

      return n;
    }
  }

  return NULL;
}

struct dt_device *dt_compatible_device(struct dt_device *table, struct device_node *node) {
  const char *compat = dt_node_props(node, "compatible");

  if(!compat)
    return NULL;

  for(struct dt_device *dev = table; dev->dev_name || dev->compat; dev++) {
    for(struct dt_compatible *c = dev->compat; c->comp; c++) {
      if(strcmp(c->comp, compat) == 0)
        return dev;
    }
  }

  return NULL;
}

int compat_dt_device_init(struct dt_device *table, struct device_node *node,
                           const char *compat) {
  for(struct dt_device *dev = table; dev->dev_name[0] || dev->compat; dev++) {
    for(struct dt_compatible *c = dev->compat; c->comp; c++) {
      if(strcmp(c->comp, compat) == 0) {
        if(dev->init) {
          dev->init(node);
          return 0;
        }
      }
    }
  }

  return -1;
}

void device_tree_init(void *fdt_base) {
  struct fdt fdt;

  fdt_probe(&fdt, fdt_base);

  struct device_node *root = fdt_parse(&fdt);

  const char *mach = dt_node_props(root, "compatible");
  if(mach)
    earlycon_puts(mach);

  localnode.device_tree = root;
}
