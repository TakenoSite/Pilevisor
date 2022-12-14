/*
 *  pl011 uart driver
 */

#include "types.h"
#include "param.h"
#include "uart.h"
#include "vcpu.h"
#include "irq.h"
#include "mm.h"
#include "localnode.h"
#include "compiler.h"

static void *uartbase;

#define DR      0x00
#define FR      0x18
#define IBRD    0x24
#define FBRD    0x28
#define LCRH    0x2c
#define CR      0x30
#define IFLS    0x34
#define IMSC    0x38
#define MIS     0x40
#define ICR     0x44

#define FR_RXFE (1<<4)  // recieve fifo empty
#define FR_TXFF (1<<5)  // transmit fifo full
#define FR_RXFF (1<<6)  // recieve fifo full
#define FR_TXFE (1<<7)  // transmit fifo empty

#define INT_RX_ENABLE   (1<<4)
#define INT_TX_ENABLE   (1<<5)

#define LCRH_FEN        (1<<4)
#define LCRH_WLEN_8BIT  (3<<5)

#define UART_FREQ   48000000ull

static u32 pl011_read(unsigned int reg) {
  return *(volatile u32 *)((u64)uartbase + reg);
}

static void pl011_write(unsigned int reg, u32 val) {
  *(volatile u32 *)((u64)uartbase + reg) = val;
}

static void pl011_putc(char c) {
  while(pl011_read(FR) & FR_TXFF)
    ;

  pl011_write(DR, c);
}

static void pl011_puts(char *s) {
  char c;

  while((c = *s++))
    uart_putc(c);
}

static int pl011_getc() {
  if(pl011_read(FR) & FR_RXFE)
    return -1;
  else
    return pl011_read(DR);
}

static void pl011_set_baudrate(int baud) {
  u64 bauddiv = (UART_FREQ * 1000) / (16 * baud);

  u32 ibrd = bauddiv / 1000;
  u32 fbrd = ((bauddiv - ibrd * 1000) * 64 + 500) / 1000;

  pl011_write(IBRD, ibrd);
  pl011_write(FBRD, fbrd);
}

static void pl011_intr(__unused void *arg) {
  int status = pl011_read(MIS);

  if(status & INT_RX_ENABLE) {
    for(;;) {
      int c = pl011_getc();
      if(c < 0)
        break;

      printf("uartintr\n");
    }
  }

  pl011_write(ICR, INT_RX_ENABLE);
}

static struct uartchip pl011 = {
  .name = "pl011",
  .putc = pl011_putc,
  .puts = pl011_puts,
};

static void pl011_dt_init(struct device_node *dev) {
  u64 reg[2];

  int rc = dt_node_propa64(dev, "reg", reg);
  if(rc < 0)
    panic("?");

  u64 uart_base = reg[0];
  u64 uart_size = reg[1];

  uartbase = iomap(uart_base, uart_size);

  /* disable uart */
  pl011_write(CR, 0);
  pl011_write(LCRH, 0);
  pl011_write(IFLS, 0);
  pl011_write(IMSC, 0);

  pl011_set_baudrate(115200);

  pl011_write(LCRH, LCRH_WLEN_8BIT);

  pl011_write(IMSC, 0);

  /* enable uart */
  pl011_write(CR, 0x301);   /* RXE, TXE, UARTEN */

  // irq_register(33, pl011_intr);
  localnode.uart = &pl011;

  printf("pl011 detected: %p\n", uartbase);
}

struct dt_compatible pl011_compat[] = {
  { "arm,pl011" },
  {},
};

DT_SERIAL_INIT(pl011, pl011_compat, pl011_dt_init);
