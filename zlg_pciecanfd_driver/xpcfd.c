//
// PCI/PCIE-CAN device driver
// Copyright (C) Guangzhou Zhiyuan Electronics Co., Ltd. All rights reserved.
//
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/miscdevice.h>
#include <linux/time.h>
#include <linux/can.h>
#include <linux/can/dev.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define CAN_PUT_ECHO_SKB(skb, dev, idx)  can_put_echo_skb(skb, dev, idx, 0)
#define CAN_GET_ECHO_SKB(dev, idx)  can_get_echo_skb(dev, idx, NULL)
#else
#define CAN_PUT_ECHO_SKB(skb, dev, idx)  can_put_echo_skb(skb, dev, idx)
#define CAN_GET_ECHO_SKB(dev, idx)  can_get_echo_skb(dev, idx)
#endif

// module info

#define PCIXX_MODULE_INFO  "zhiyuan pci/pcie-can"
#define PCIXX_DRIVER_NAME  "zpcicanfd"
#define PCIXX_DRIVER_VERS  "1.0"

static const struct pci_device_id pcixx_id_table[] = {
    {0x10ee, 0x9a01, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0x10ee, 0x9a02, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0x10ee, 0x9a04, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0x10ee, 0x9a12, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0x10ee, 0x9a22, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0x1feb, 0x0108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
    {0}
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(PCIXX_MODULE_INFO);
MODULE_DEVICE_TABLE(pci, pcixx_id_table);

static unsigned ioctlcan = 0;
module_param(ioctlcan, uint, 0644);

static unsigned apollocan = 0;
module_param(apollocan, uint, 0644);

static unsigned tdc_en = 0;
module_param(tdc_en, uint, 0644);

static unsigned tdc_cfg = 0;
module_param(tdc_cfg, uint, 0644);

// debug

static unsigned dbg = 0;
module_param(dbg, uint, 0644);

#define _MSG_(fmt, args...)  printk(KERN_INFO PCIXX_DRIVER_NAME ": %s(%d): " fmt "\n", __FUNCTION__, __LINE__, ##args)
#define _DBG_(fmt, args...)  if (dbg) { _MSG_(fmt, ##args); }

// macros

#define PCIXX_MAX_BARS  6
#define PCIXX_MAX_PORTS  12
#define PCIXX_SAFE_IPORT(d,i)  ((i) < (d)->nports ? (i) : 0)
#define PCIXX_SAFE_PPORT(d,i)  ((d)->ports[PCIXX_SAFE_IPORT((d),(i))])

#define R_REG(base,offs)      readl((u32*)((u8*)(base)+(offs)))
#define W_REG(base,offs,val)  writel(val,(u32*)((u8*)(base)+(offs)))

#ifndef CANFD_FDF
#define CANFD_FDF  4
#endif

// types & ports

enum { PCIE9A01, PCIE9A02, PCIE9A04, PCIE9A12, PCIE9A22, PCIE0108, PCIXX_HW_TYPES };
const int pcixx_ports[] = {1, 2, 4, 2, 2, 8};
spinlock_t glock;
u32 gfound = 0;

// ioctl

typedef struct canfd_frame socket_canframe;

#pragma pack(push, 1)
typedef struct {
    u32 id;
    u8 len;
    u8 dat[8];
    struct __kernel_timespec ts;
} apollo_canframe;

typedef struct {
    u32 port;
    struct {            // abt,dbt @ 500K
        u32 phase_seg1; // 0x23, 7
        u32 phase_seg2; // 0x0a, 2
        u32 prop_seg;   // 0x22, 6
        u32 sjw;        // 0x01, 1
        u32 brp;        // 0x01, 5
    } abt, dbt;
} pcixx_cfg;

typedef struct {
    u32 port;
    u32 len;
    void *buf;
} pcixx_tx_hdr;

typedef struct {
    u32 port;
    u32 len;
    void *buf;
} pcixx_rx_hdr;

typedef struct {
    u32 port;
    void *buf;
} pcixx_get_err;
#pragma pack(pop)

#define IOCTL_TX       _IOWR(100, 0, pcixx_tx_hdr)
#define IOCTL_RX       _IOWR(100, 1, pcixx_rx_hdr)
#define IOCTL_CFG      _IOWR(100, 2, pcixx_cfg)
#define IOCTL_GET_ERR  _IOWR(100, 3, pcixx_get_err)

// fpga

#define CAN_CLK  40000000
#define MAX_CAN_FILTERS  64

#define TDC_EN  0x10000
#define TDC_MASK  0x3f00

#define DMA_FIFO_SIZE  8192
#define DMA_FIFO_MASK  8191

#define DMA_CACHE_SIZE  0x100000

#define SPI_BUFF_SIZE  256
#define FLASH_PAGE_SIZE  256
#define FLASH_BLOCK_SIZE  0x10000

typedef enum {
    REG_SPI_CMD = 0x00,
    REG_SPI_TDAT = 0x04,
    REG_SPI_RDAT = 0x08,
    REG_SPI_BUSY = 0x0c,
    REG_SPI_RST = 0x10,
    REG_SPI_TRST = 0x14,
    REG_SPI_RRST = 0x18,
    REG_SPI_LEN = 0x1c,
    REG_SPI_RFIFO = 0x20,
} SPI_REGS;

typedef enum {
    FLASH_BUSY = 1 << 0,
    FLASH_WEL = 1 << 1,
    FLASH_BP0 = 1 << 2,
    FLASH_BP1 = 1 << 3,
    FLASH_BP2 = 1 << 4,
    FLASH_BP3 = 1 << 5,
    FLASH_QE = 1 << 6,
    FLASH_SRWD = 1 << 7,
} FLASH_SR;

#define FLASH_CMD_RD_ID        (0x9fu << 24)
#define FLASH_CMD_WR_EN        (0x06u << 24)
#define FLASH_CMD_WR_DIS       (0x04u << 24)
#define FLASH_CMD_RD_SR1       (0x05u << 24)
#define FLASH_CMD_RD_DATA      (0x6bu << 24)
#define FLASH_CMD_PAGE_PROG    (0x02u << 24)
#define FLASH_CMD_BLOCK_ERASE  (0xd8u << 24)

#define FLASH_SIZE            0x800000
#define FLASH_ADDR_AUTH       (FLASH_SIZE - 0x10000)
#define FLASH_ADDR_CARD_SN    (FLASH_SIZE - 0x20000)
#define FLASH_ADDR_CARD_DESC  (FLASH_SIZE - 0x30000)

typedef enum {
    REG_ATH_CTRL = 0x00,
    REG_ATH_STAT = 0x00,
    REG_ENC_KEY = 0x04,
    REG_DNA_L = 0x24,
    REG_DNA_H = 0x28,
    REG_ATH_CHK = 0x2c,
    REG_ATH_KEY = 0x4c,
} AUTH_REGS;

typedef enum {
    REG_ISR = 0x0000,
    REG_ICR = 0x0004,
    REG_IER = 0x0008,
    REG_CFG_END = 0x000c,
    REG_CFG_RUN = 0x0010,
    REG_TX_EN = 0x0014,
    REG_DMA_EN = 0x0018,
    REG_TL_EN = 0x001c,
    REG_DMA_ST = 0x0020,
    REG_TL_CFG = 0x0024,
    REG_TTX_RAM_P = 0x0028,
    REG_TTX_RAM_D = 0x002c,
    REG_TS_L = 0x0030,
    REG_TS_H = 0x0034,
    REG_TS_RST = 0x0038,
    REG_VERS = 0x003c,
    REG_TX_TMO = 0x0040,
    REG_DMA_TRG_T = 0x0044,
    REG_DMA_TRG_C = 0x0048,
    REG_TX_TOTAL = 0x004c,
    REG_RX_TOTAL = 0x0050,
    REG_ER_TOTAL = 0x00f0,
    REG_BUSLD_EN = 0x00f4,
    REG_RX_WI = 0x0070,
    REG_RX_RI = 0x0074,
    REG_RX_BASE = 0x005c,
    REG_DBG_TTX = 0x00f8,
    REG_TTX_CTL_A = 0x00fc,
    REG_TTX_CTL_B = 0x0100,
    REG_TTX_CTL_C = 0x0104,
    REG_TTX_CTL_D = 0x0108,
    REG_QTX_CLR = 0x010c,
    REG_TTX_CTL_A1 = 0x0110,
    REG_TTX_CTL_B1 = 0x0114,
    REG_TTX_CTL_C1 = 0x0118,
    REG_TTX_CTL_D1 = 0x011c,
    REG_TTX_CTL_A2 = 0x0120,
    REG_TTX_CTL_B2 = 0x0124,
    REG_TTX_CTL_C2 = 0x0128,
    REG_TTX_CTL_D2 = 0x012c,
    REG_TERM_RES = 0x0130,
    REG_CARD_INFO_A = 0x0134,
    REG_CARD_INFO_B = 0x0138,
    REG_ID_SW = 0x013c,
    REG_LED_MODE = 0x0140,
    REG_LED_G = 0x0144,
    REG_LED_R = 0x0148,
} CAN_COMM_REGS;

typedef enum {
    REG_QTX_BASE = 0x0054,
    REG_RTX_BASE = 0x0058,
    REG_QTX_WI = 0x0060,
    REG_QTX_RI = 0x0064,
    REG_RTX_WI = 0x0068,
    REG_RTX_RI = 0x006c,
    REG_MSR = 0x0078,
    REG_ABRPR = 0x007c,
    REG_ABTR = 0x0080,
    REG_DBRPR = 0x0084,
    REG_DBTR = 0x0088,
    REG_AFM = 0x008c,
    REG_AFD = 0x0090,
    REG_AFR = 0x0094,
    REG_BUSLD_T_A = 0x0098,
    REG_BUSLD_T_B = 0x009c,
    REG_BUSLD_CNT = 0x00a0,
    REG_BUSLD_CFG = 0x00a4,
    REG_BUSLD_WND = 0x00ac,
    REG_BUSLD_TSL = 0x00b0,
    REG_BUSLD_TSH = 0x00b4,
    REG_BUSLD_TEL = 0x00b8,
    REG_BUSLD_TEH = 0x00bc,
    REG_DBG_MAIN = 0x00a8,
} CAN_PORT_REGS;

#pragma pack(push, 1)
typedef union {
    struct {
        u32 hdr;
        u32 tsh;
        u32 tsl;
        u32 gap;
        u32 pad;
        u32 id;
        union {
            struct {
                u32 len: 8;
                u32 chn: 8;
                u32 ttype: 2;
                u32 tx: 1;
                u32 echo: 1;
                u32 fd: 1;
                u32 rtr: 1;
                u32 ext: 1;
                u32 pad0: 1;
                u32 brs: 1;
                u32 esi: 1;
                u32 dly: 1;
                u32 pad1: 5;
            };
            u32 inf;
        };
        u32 dat[25];
    } tx;
    struct {
        u32 tsh;
        u32 tsl;
        u32 id;
        union {
            struct {
                u32 len: 8;
                u32 chn: 8;
                u32 pad0: 2;
                u32 echo: 1;
                u32 pad1: 1;
                u32 fd: 1;
                u32 rtr: 1;
                u32 ext: 1;
                u32 err: 1;
                u32 brs: 1;
                u32 esi: 1;
                u32 pad2: 6;
            };
            u32 inf;
        };
        u32 dat[27];
    } rx;
} can_msg;
#pragma pack(pop)

// device/port context

#define PCIXX_RX_FIFO_SIZE  8192
#define PCIXX_RX_FIFO_MASK  (PCIXX_RX_FIFO_SIZE - 1)
typedef struct {
    u32 w;
    u32 r;
    union {
        socket_canframe sc[PCIXX_RX_FIFO_SIZE];
        apollo_canframe ac[PCIXX_RX_FIFO_SIZE];
    };
} pcixx_rx_fifo;

typedef struct pcixx_port_t {
    struct can_priv can;
    struct net_device *ndev;
    struct pcixx_device_t *parent;
    struct can_berr_counter bec;
    void __iomem *regs;
    int index;
    struct {
        wait_queue_head_t wait;
        atomic_t locked;
    } mtx_cfg;
    struct {
        wait_queue_head_t wait;
        atomic_t locked;
    } mtx_tx;
    struct {
        wait_queue_head_t wait;
        atomic_t locked;
    } mtx_rx;
    struct {
        wait_queue_head_t wait;
        atomic_t pend;
    } sig_tx;
    struct {
        wait_queue_head_t wait;
        atomic_t pend;
    } sig_rx;
    struct {
        dma_addr_t kpa;
        void *kva;
    } dma;
    pcixx_rx_fifo rx_fifo;
    u64 last_berr;
} pcixx_port;

typedef struct pcixx_device_t {
    pcixx_port *ports[PCIXX_MAX_PORTS];
    struct pci_dev *pcidev;
    struct miscdevice miscdev;
    struct timer_list timer;
    char miscname[256];
    bool miscdev_rdy;
    struct {
        size_t start;
        size_t end;
        size_t size;
        void __iomem *ptr;
    } bars[PCIXX_MAX_BARS];
    spinlock_t lock;
    int irq;
    int type;
    u8 nports;
    void __iomem *pcie;
    void __iomem *spi;
    void __iomem *regs;
    void __iomem *auth;
    struct {
        u32 pcie_remap;
        dma_addr_t kpa;
        void *kva;
    } dma;
    u32 ttx_ptr;
    u32 ttx_len;
    u8 ttx_cfg[128];
} pcixx_device;

#define file_to_dev(file)  (container_of(file->private_data, pcixx_device, miscdev))

#define PCIXX_MUTEX_INIT(m)    init_waitqueue_head(&(m)->wait)
#define PCIXX_MUTEX_LOCK(m)    wait_event((m)->wait, !atomic_cmpxchg(&(m)->locked, 0, 1))
#define PCIXX_MUTEX_UNLOCK(m)  atomic_xchg(&(m)->locked, 0),wake_up(&(m)->wait)

#define PCIXX_SIG_INIT(s)    init_waitqueue_head(&(s)->wait)
#define PCIXX_SIG_PEND(s)    atomic_xchg(&(s)->pend, 1)
#define PCIXX_SIG_WAIT(s,t)  wait_event_interruptible_timeout((s)->wait, !atomic_read(&(s)->pend), (t))
#define PCIXX_SIG_WAKE(s)    atomic_xchg(&(s)->pend, 0),wake_up(&(s)->wait)

// fifo

inline void pcixx_rx_fifo_clr(pcixx_rx_fifo *fifo)
{
    fifo->r = fifo->w;
}

inline u32 pcixx_rx_fifo_cnt(pcixx_rx_fifo *fifo)
{
    return fifo->w - fifo->r;
}

static socket_canframe* pcixx_rx_fifo_wlck(pcixx_rx_fifo *fifo)
{
    if (fifo->w - fifo->r >= PCIXX_RX_FIFO_SIZE)
        return NULL;
    if (apollocan) {
        return (socket_canframe*)(fifo->ac + (fifo->w & PCIXX_RX_FIFO_MASK));
    } else {
        return (socket_canframe*)(fifo->sc + (fifo->w & PCIXX_RX_FIFO_MASK));
    }
}

static socket_canframe* pcixx_rx_fifo_rlck(pcixx_rx_fifo *fifo)
{
    if (fifo->w == fifo->r)
        return NULL;
    if (apollocan) {
        return (socket_canframe*)(fifo->ac + (fifo->r & PCIXX_RX_FIFO_MASK));
    } else {
        return (socket_canframe*)(fifo->sc + (fifo->r & PCIXX_RX_FIFO_MASK));
    }
}

static bool pcixx_rx_fifo_put(pcixx_rx_fifo *fifo)
{
    if (fifo->w - fifo->r >= PCIXX_RX_FIFO_SIZE)
        return false;
    fifo->w++;
    return true;
}

static bool pcixx_rx_fifo_get(pcixx_rx_fifo *fifo)
{
    if (fifo->w == fifo->r)
        return false;
    fifo->r++;
    return true;
}

// bars

static void pcixx_map_bars(pcixx_device *d)
{
    int i;
    for (i = 0; i < PCIXX_MAX_BARS; i++) {
        if (!pci_resource_len(d->pcidev, i)) continue;
        d->bars[i].start = pci_resource_start(d->pcidev, i);
        d->bars[i].end = pci_resource_end(d->pcidev, i);
        d->bars[i].size = d->bars[i].end + 1 - d->bars[i].start;
        d->bars[i].ptr = pci_iomap(d->pcidev, i, d->bars[i].size);
        _MSG_("bar%d: 0x%x~0x%x, len=0x%x(%u), ptr=0x%08lx",
            i, (u32)d->bars[i].start, (u32)d->bars[i].end,
            (u32)d->bars[i].size, (u32)d->bars[i].size, (long)d->bars[i].ptr);
    }
}

static void pcixx_unmap_bars(pcixx_device *d)
{
    int i;
    for (i = 0; i < PCIXX_MAX_BARS; i++) {
        if (!d->bars[i].ptr) continue;
        _MSG_("bar%d: 0x%x~0x%x, len=0x%x(%u), ptr=0x%08lx",
            i, (u32)d->bars[i].start, (u32)d->bars[i].end,
            (u32)d->bars[i].size, (u32)d->bars[i].size, (long)d->bars[i].ptr);
        pci_iounmap(d->pcidev, d->bars[i].ptr);
        d->bars[i].ptr = NULL;
    }
}

// hw

static int pcixx_hw_detect(struct pci_dev *dev)
{
    int ret, i;
    u16 vendor, device;
    ret = pci_read_config_word(dev, PCI_VENDOR_ID, &vendor);
    if (ret) {
        _MSG_("pci_read_config_word(PCI_VENDOR_ID) failed");
        return ret;
    }
    ret = pci_read_config_word(dev, PCI_DEVICE_ID, &device);
    if (ret) {
        _MSG_("pci_read_config_word(PCI_DEVICE_ID) failed");
        return ret;
    }
    for (i = 0; i < PCIXX_HW_TYPES; i++) {
        if (vendor == pcixx_id_table[i].vendor && device == pcixx_id_table[i].device)
            return i;
    }
    _MSG_("unknown device: vendor=0x%04x, device=0x%04x", vendor, device);
    return -1;
}

static void pcixx_hw_init(pcixx_device *d, int init)
{
    void *bar0= d->bars[0].ptr;
    _MSG_("+++ bar0=0x%08lx", (long)bar0);
    W_REG(d->regs, REG_IER,       0);
    W_REG(d->regs, REG_DMA_EN,    0);
    W_REG(d->regs, REG_TX_EN,     0);
    W_REG(d->regs, REG_TX_TMO,    100000000);
    W_REG(d->regs, REG_DMA_TRG_T, 100000);
    W_REG(d->regs, REG_DMA_TRG_C, 1);
    W_REG(d->regs, REG_ICR,       0xffffffff);
    W_REG(d->regs, REG_ICR,       0);
    _DBG_("---");
}

// spi

static void pcixx_spi_init(pcixx_device *d)
{
    void *regs = d->spi;
    _DBG_("+++ base=0x%08lx", (long)regs);
    W_REG(regs, REG_SPI_RST, 1);
    W_REG(regs, REG_SPI_TRST, 1);
    W_REG(regs, REG_SPI_RRST, 1);
    udelay(1);
    W_REG(regs, REG_SPI_RST, 0);
    W_REG(regs, REG_SPI_TRST, 0);
    W_REG(regs, REG_SPI_RRST, 0);
    udelay(1);
    _DBG_("---");
}

static bool pcixx_spi_wait_idle(pcixx_device *d, u32 wait_us)
{
    void *regs = d->spi;
    bool ret = false;
    u32 retry = 0;
    _DBG_("+++");
    for (retry = 0; retry < wait_us; retry++) {
        if (!R_REG(regs, REG_SPI_BUSY)) {
            ret = true;
            break;
        }
        udelay(1);
    }
    _DBG_("--- ret=%d, retry/timeout=%d/%d", ret, retry, wait_us);
    return ret;
}

static bool pcixx_flash_send_cmd(pcixx_device *d, u32 cmd, u8 *buff, u32 size)
{
    void *regs = d->spi;
    bool ret = false;
    u32 i;
    for (i = 0; i < size; i++) {
        W_REG(regs, REG_SPI_TDAT, buff[i]);
    }
    W_REG(regs, REG_SPI_CMD, cmd);
    ret = pcixx_spi_wait_idle(d, 1000);
    _DBG_("pcixx_spi_wait_idle: ret=%d", ret);
    return ret;
}

static bool pcixx_flash_read_id(pcixx_device *d, u32 *val)
{
    void *regs = d->spi;
    bool ret = false;
    *val = 0;
    do {
        ret = pcixx_flash_send_cmd(d, FLASH_CMD_RD_ID, NULL, 0);
        _DBG_("pcixx_flash_send_cmd: ret=%d", ret);
        if (!ret) break;
        *val = R_REG(regs, REG_SPI_RDAT);
        _DBG_("val=0x%08x", *val);
    } while (0);
    return true;
}

static bool pcixx_flash_read_status(pcixx_device *d, u32 *val)
{
    void *regs = d->spi;
    bool ret = false;
    *val = 0;
    do {
        ret = pcixx_flash_send_cmd(d, FLASH_CMD_RD_SR1, NULL, 0);
        _DBG_("pcixx_flash_send_cmd: ret=%d", ret);
        if (!ret) break;
        *val = R_REG(regs, REG_SPI_RDAT);
        _DBG_("val=0x%08x", *val);
    } while (0);
    return true;
}

static bool pcixx_flash_wait_idle(pcixx_device *d, u32 wait_us)
{
    u32 retry = 0;
    u32 sr = FLASH_BUSY;
    _DBG_("+++");
    for (retry = 0; retry < wait_us; retry++) {
        if (pcixx_flash_read_status(d, &sr) && !(sr & FLASH_BUSY))
            break;
        udelay(1);
    }
    _DBG_("--- ret=%d, retry/timeout=%d/%d", !(sr & FLASH_BUSY), retry, wait_us);
    return !(sr & FLASH_BUSY);
}

static bool pcixx_flash_read_page(pcixx_device *d, u32 addr, u8 data[FLASH_PAGE_SIZE])
{
    void *regs = d->spi;
    int i;

    W_REG(regs, REG_SPI_LEN, FLASH_PAGE_SIZE);
    W_REG(regs, REG_SPI_CMD, FLASH_CMD_RD_DATA | addr);

    if (!pcixx_spi_wait_idle(d, 1000)) {
        _DBG_("pcixx_spi_wait_idle failed");
        return false;
    }
    if (!pcixx_flash_wait_idle(d, 10000)) {
        _DBG_("pcixx_flash_wait_idle failed");
        return false;
    }
    for (i = 0; i < FLASH_PAGE_SIZE; i++) {
        *data++ = (u8)R_REG(regs, REG_SPI_RFIFO);
    }
    return true;
}

static bool pcixx_auth(pcixx_device *d)
{
    u32 i, v, *p;
    union {
        struct {
            u32 dna[8];
            u32 enc[8];
            u32 ath[8];
        };
        u8 dat[FLASH_PAGE_SIZE];
    } buff = { 0 };

    pcixx_spi_init(d);
    pcixx_flash_read_id(d, &v);
    pcixx_flash_read_id(d, &v);
    pcixx_flash_read_id(d, &v);

    if (!pcixx_flash_read_page(d, FLASH_ADDR_AUTH, buff.dat)) {
        _DBG_("pcixx_flash_read_page failed");
        return false;
    }

    p = buff.dna;
    _DBG_("dna=%08x %08x %08x %08x %08x %08x %08x %08x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p = buff.enc;
    _DBG_("enc=%08x %08x %08x %08x %08x %08x %08x %08x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p = buff.ath;
    _DBG_("ath=%08x %08x %08x %08x %08x %08x %08x %08x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

    for (i = 0; i < 8; i++) {
        W_REG(d->auth, REG_ENC_KEY + i * 4, buff.enc[i]);
        W_REG(d->auth, REG_ATH_CHK + i * 4, buff.ath[i]);
    }
    W_REG(d->auth, REG_ATH_CTRL, 2);
    udelay(1000);
    v = R_REG(d->auth, REG_ATH_STAT);
    _DBG_("REG_ATH_STAT=0x%08x", v);

    return true;
}

// can

static const struct can_bittiming_const pcixx_can_abtr_const = {
    .name = PCIXX_DRIVER_NAME,
    .tseg1_min = 1,
    .tseg1_max = 256,
    .tseg2_min = 1,
    .tseg2_max = 128,
    .sjw_max = 128,
    .brp_min = 1,
    .brp_max = 256,
    .brp_inc = 1,
};

static const struct can_bittiming_const pcixx_can_dbtr_const = {
    .name = PCIXX_DRIVER_NAME,
    .tseg1_min = 1,
    .tseg1_max = 32,
    .tseg2_min = 1,
    .tseg2_max = 16,
    .sjw_max = 16,
    .brp_min = 1,
    .brp_max = 16,
    .brp_inc = 1,
};

static int pcixx_can_run(pcixx_port *port)
{
    _DBG_("*** can%d", port->index);
    return 0;
}

static int pcixx_can_set_mode(struct net_device *ndev, enum can_mode mode)
{
    pcixx_port *p = netdev_priv(ndev);
    int ret = -EOPNOTSUPP;

    _DBG_("+++ can%d: mode=%d", p->index, mode);

    switch (mode) {
    case CAN_MODE_START:
        pcixx_can_run(p);
        netif_wake_queue(ndev);
        ret = 0;
        break;
    default:
        break;
    }

    _DBG_("--- can%d: mode=%d, ret=%d", p->index, mode, ret);
    return ret;
}

static int pcixx_can_get_berr_counter(const struct net_device *ndev, struct can_berr_counter *bec)
{
    pcixx_port *p = netdev_priv(ndev);
    bec->txerr = p->bec.txerr;
    bec->rxerr = p->bec.rxerr;
    //_DBG_("can%d: txerr=0x%02x, rxerr=0x%02x", p->index, bec->txerr, bec->rxerr);
    return 0;
}

static void pcixx_can_cfg(pcixx_port *port)
{
    pcixx_device *d = port->parent;
    unsigned long flags;
    u32 v;
    spin_lock_irqsave(&d->lock, flags);
    v = R_REG(d->regs, REG_CFG_RUN);
    v &= ~(1 << port->index);
    W_REG(d->regs, REG_CFG_RUN, v);
    v = R_REG(d->regs, REG_CFG_RUN);
    v |= (1 << port->index);
    W_REG(d->regs, REG_CFG_RUN, v);
    v = R_REG(d->regs, REG_CFG_RUN);
    spin_unlock_irqrestore(&d->lock, flags);
}

static int pcixx_can_open(pcixx_port *port, bool apollo)
{
    pcixx_device *d = port->parent;
    struct can_bittiming *abt = &port->can.bittiming;
    struct can_bittiming *dbt = &port->can.data_bittiming;
    u32 ito = (abt->brp == 1) ? 0x800 : 0;
    u32 oneshot = (port->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT) ? 4 : 0;
    union {
        struct {
            u32 ts1: 8;
            u32 ts2: 8;
            u32 sjw: 8;
            u32 pad: 8;
        };
        u32 val;
    } at = {
        .ts1 = abt->prop_seg + abt->phase_seg1 - 1,
        .ts2 = abt->phase_seg2 - 1,
        .sjw = abt->sjw - 1,
    }, dt = {
        .ts1 = dbt->prop_seg + dbt->phase_seg1 - 1,
        .ts2 = dbt->phase_seg2 - 1,
        .sjw = dbt->sjw - 1,
    };
    int i, err;
    u32 v;

    _MSG_("+++ can%d: "
        "abt: phase_seg1=0x%02x, phase_seg2=0x%02x, prop_seg=0x%02x, sjw=0x%02x, brp=0x%02x, "
        "dbt: phase_seg1=0x%02x, phase_seg2=0x%02x, prop_seg=0x%02x, sjw=0x%02x, brp=0x%02x",
        port->index,
        abt->phase_seg1, abt->phase_seg2, abt->prop_seg, abt->sjw, abt->brp,
        dbt->phase_seg1, dbt->phase_seg2, dbt->prop_seg, dbt->sjw, dbt->brp
        );

    _MSG_("can%d: "
        "abt: ts1=0x%02x, ts2=0x%02x, sjw=0x%02x, brp=0x%02x, "
        "dbt: ts1=0x%02x, ts2=0x%02x, sjw=0x%02x, brp=0x%02x",
        port->index,
        at.ts1, at.ts2, at.sjw, abt->brp-1,
        dt.ts1, dt.ts2, dt.sjw, dbt->brp-1
        );

    err = apollo ? 0 : open_candev(port->ndev);
    if (err) {
        _MSG_("can%d: open_candev failed: err=%d", port->index, err);
        return err;
    }

    W_REG(port->regs, REG_ABRPR, abt->brp - 1);
    W_REG(port->regs, REG_ABTR,  at.val);
    W_REG(port->regs, REG_DBRPR, (dbt->brp - 1) | (tdc_en ? TDC_EN : 0) | ((tdc_cfg & 0x3f) << 8));
    W_REG(port->regs, REG_DBTR,  dt.val);
    W_REG(port->regs, REG_AFM,   0);
    W_REG(port->regs, REG_AFD,   0);
    W_REG(port->regs, REG_AFR,   1);

    for (i = 0; i < 3; i++) {
        switch (i) {
        case 0: W_REG(port->regs, REG_MSR, 0x10000); break;
        case 1: W_REG(port->regs, REG_MSR, ito | oneshot); break;
        case 2: W_REG(port->regs, REG_MSR, ito | oneshot | 0x20080); break;
        }
        pcixx_can_cfg(port);
        udelay(10);
    }

    v = R_REG(d->regs, REG_CFG_END);
    _DBG_("can%d: REG_CFG_END=0x%08x", port->index, v);

    W_REG(d->regs, REG_IER,    0xffffffff);
    W_REG(d->regs, REG_TX_EN,  0xfff);
    W_REG(d->regs, REG_DMA_EN, 0xfff);

    port->can.state = CAN_STATE_ERROR_ACTIVE;

    port->last_berr = 0;

    if (!apollo) {
        _DBG_("can%d: netif_start_queue", port->index);
        netif_start_queue(port->ndev);
    }

    _DBG_("---");
    return 0;
}

static int pcixx_can_stop(pcixx_port *port)
{
    _DBG_("*** can%d", port->index);
    W_REG(port->regs, REG_MSR, 0x10000);
    pcixx_can_cfg(port);
    return 0;
}

// -1: full before write
// 0: full after written
// 1: not full after written
static int pcixx_tx_fifo_w(pcixx_port *port, void *src, bool canfd, bool apollo, bool echo)
{
    struct canfd_frame *src_ndo = (struct canfd_frame*)src;
    apollo_canframe *src_apo = (apollo_canframe*)src;
    int ret = -1;
    can_msg *dst = NULL;
    u32 wi, ri, filled;

    _DBG_("*** can%d: canfd=%d, apollo=%d, echo=%d", port->index, canfd, apollo, echo);
    do {
        wi = R_REG(port->regs, REG_RTX_WI);
        ri = R_REG(port->regs, REG_RTX_RI);
        filled = (wi - ri) & 0x3fff;
        if (filled >= DMA_FIFO_SIZE) {
            _DBG_("can%d: tx busy", port->index);
            break;
        }

        dst = port->dma.kva;
        dst += (wi & DMA_FIFO_MASK);

        dst->tx.hdr = 3;
        dst->tx.tsh = 0;
        dst->tx.tsl = 0;
        dst->tx.gap = 0;
        dst->tx.pad = port->index << 28;
        dst->tx.id = apollo ? src_apo->id : src_ndo->can_id;
        dst->tx.inf = 0;
        dst->tx.ttype = 0;
        dst->tx.tx = 1;
        dst->tx.echo = echo;
        dst->tx.fd = canfd;
        dst->tx.rtr = apollo ? 0 : (!!(src_ndo->can_id & CAN_RTR_FLAG));
        dst->tx.ext = apollo ? 0 : (!!(src_ndo->can_id & CAN_EFF_FLAG));
        dst->tx.brs = apollo ? 0 : (canfd && (src_ndo->flags & CANFD_BRS));
        dst->tx.esi = apollo ? 0 : (canfd && (src_ndo->flags & CANFD_ESI));
        dst->tx.dly = 0;
        dst->tx.chn = port->index;
        dst->tx.len = apollo ? src_apo->len : src_ndo->len;

        memcpy(dst->tx.dat, apollo ? src_apo->dat : src_ndo->data, dst->tx.len);

        {
            u32 *p = (u32*)dst;
            _DBG_("can%d: FIFO=[%08x:%08x#%u], dat="
                "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                port->index,
                wi, ri, filled,
                p[0x0], p[0x1], p[0x2], p[0x3], p[0x4], p[0x5], p[0x6], p[0x7],
                p[0x8], p[0x9], p[0xa], p[0xb], p[0xc], p[0xd], p[0xe], p[0xf]);
        }

        W_REG(port->regs, REG_RTX_WI, wi + 1);
        ret = filled < DMA_FIFO_MASK;
    } while (0);
    return ret;
}

static netdev_tx_t pcixx_can_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
    pcixx_port *port = netdev_priv(ndev);
    int ret = NETDEV_TX_OK;
    struct canfd_frame *src = (struct canfd_frame *)skb->data;
    bool canfd = can_is_canfd_skb(skb);
    struct net_device_stats *stats = &ndev->stats;
    bool loopback = !!(port->can.ctrlmode & CAN_CTRLMODE_LOOPBACK);

    _DBG_("*** can%d: canfd=%d", port->index, canfd);
    do {
        if (can_dropped_invalid_skb(ndev, skb)) {
            _DBG_("can%d: can_dropped_invalid_skb", port->index);
            break;
        }
        CAN_PUT_ECHO_SKB(skb, port->ndev, 0);
        ret = pcixx_tx_fifo_w(port, src, canfd, false, loopback);
        CAN_GET_ECHO_SKB(port->ndev, 0);

        switch (ret) {
        case -1:
            _MSG_("fifo full before write");
            netif_stop_queue(port->ndev);
            break;
        case 0:
            netif_stop_queue(port->ndev);
            break;
        default:
            break;
        }
        if (ret >= 0) {
            stats->tx_packets++;
            stats->tx_bytes += canfd ? sizeof(struct canfd_frame) : sizeof(struct can_frame);
            ret = NETDEV_TX_OK;
        } else {
            ret = NETDEV_TX_BUSY;
        }
    } while (0);
    return ret;
}

static int pcixx_ndo_open(struct net_device *ndev)
{
    pcixx_port *port = netdev_priv(ndev);
    return pcixx_can_open(port, false);
}

static int pcixx_ndo_stop(struct net_device *ndev)
{
    pcixx_port *port = netdev_priv(ndev);
    _DBG_("*** can%d", port->index);
    _DBG_("can%d: netif_stop_queue", port->index);
    netif_stop_queue(port->ndev);
    close_candev(port->ndev);
    port->can.state = CAN_STATE_STOPPED;
    pcixx_can_stop(port);
    return 0;
}

static const struct net_device_ops pcixx_can_ops = {
    .ndo_open = pcixx_ndo_open,
    .ndo_stop = pcixx_ndo_stop,
    .ndo_start_xmit = pcixx_can_start_xmit,
    .ndo_change_mtu = can_change_mtu,
};

// misc dev

static int pcixx_f_open(struct inode *inode, struct file *file)
{
    //pcixx_device *d = file_to_dev(file);
    _DBG_("+++");
    _DBG_("---");
    return 0;
}

static int pcixx_f_close(struct inode *inode, struct file *file)
{
    //pcixx_device *d = file_to_dev(file);
    _DBG_("+++");
    _DBG_("---");
    return 0;
}

static long pcixx_f_ioctl(struct file *file, unsigned int cmd, unsigned long _arg)
{
    void *arg = (void*)_arg;
    pcixx_device *d = file_to_dev(file);
    pcixx_port *port = NULL;
    unsigned long flags;
    int ret = 0;
    switch (cmd) {
    case IOCTL_CFG:
        {
            pcixx_cfg cfg;
            _DBG_(">>> IOCTL_CFG");
            ret = copy_from_user(&cfg, arg, sizeof(cfg));
            if (ret) {
                _DBG_("copy_from_user(cfg) failed");
                ret = -1;
                break;
            }
            port = PCIXX_SAFE_PPORT(d, cfg.port);
            PCIXX_MUTEX_LOCK(&port->mtx_cfg);
            {
                struct can_bittiming *abt = &port->can.bittiming;
                struct can_bittiming *dbt = &port->can.data_bittiming;
                abt->phase_seg1 = cfg.abt.phase_seg1;
                abt->phase_seg2 = cfg.abt.phase_seg2;
                abt->prop_seg   = cfg.abt.prop_seg;
                abt->sjw        = cfg.abt.sjw;
                abt->brp        = cfg.abt.brp;
                dbt->phase_seg1 = cfg.dbt.phase_seg1;
                dbt->phase_seg2 = cfg.dbt.phase_seg2;
                dbt->prop_seg   = cfg.dbt.prop_seg;
                dbt->sjw        = cfg.dbt.sjw;
                dbt->brp        = cfg.dbt.brp;
                pcixx_can_open(port, true);
            }
            PCIXX_MUTEX_UNLOCK(&port->mtx_cfg);
            ret = 0;
        }
        break;
    case IOCTL_GET_ERR:
        {
            pcixx_get_err get_err;
            u64 err = 0;
            _DBG_(">>> IOCTL_GET_ERR");
            ret = copy_from_user(&get_err, arg, sizeof(get_err));
            if (ret) {
                _DBG_("copy_from_user(hdr) failed");
                ret = -1;
                break;
            }
            port = PCIXX_SAFE_PPORT(d, get_err.port);
            spin_lock_irqsave(&d->lock, flags);
            err = port->last_berr;
            port->last_berr = 0;
            spin_unlock_irqrestore(&d->lock, flags);
            ret = copy_to_user(get_err.buf, &err, sizeof(u64));
            if (ret) {
                _DBG_("copy_to_user(dat) failed");
                ret = -EFAULT;
            }
        }
        break;
    case IOCTL_TX:
        {
            pcixx_tx_hdr tx;
            union {
                socket_canframe sc;
                apollo_canframe ac;
            } dat;
            int len = apollocan ? sizeof(apollo_canframe) : sizeof(socket_canframe);
            _DBG_(">>> IOCTL_TX");
            ret = copy_from_user(&tx, arg, sizeof(tx));
            if (ret) {
                _DBG_("copy_from_user(hdr) failed");
                ret = -EFAULT;
                break;
            }
            ret = copy_from_user(&dat, tx.buf, len);
            if (ret) {
                _DBG_("copy_from_user(dat) failed");
                ret = -EFAULT;
                break;
            }
            port = PCIXX_SAFE_PPORT(d, tx.port);
            PCIXX_MUTEX_LOCK(&port->mtx_tx);
            PCIXX_SIG_PEND(&port->sig_tx);
            // apollocan: never send fd frames
            // socketcan: flags.bit7=canfd
            ret = pcixx_tx_fifo_w(port, &dat.sc,
                apollocan ? false : (dat.sc.flags & 0x80),
                apollocan, // using apollo_canframe struct
                true // using echo to indicate tx-done
                );
            if (ret >= 0) {
                _DBG_("can%d: pcixx_tx_fifo_w succeeded", tx.port);
                ret = PCIXX_SIG_WAIT(&port->sig_tx, HZ);
                if (ret > 0 || !port->sig_tx.pend.counter) {
                    _DBG_("can%d: PCIXX_SIG_WAIT succeeded: ret=%d", tx.port, ret);
                    ret = 0;
                } else {
                    _DBG_("can%d: PCIXX_SIG_WAIT failed: ret=%d", tx.port, ret);
                    ret = -EIO;
                }
            } else {
                _DBG_("can%d: pcixx_tx_fifo_w failed", tx.port);
                ret = -EAGAIN;
            }
            PCIXX_MUTEX_UNLOCK(&port->mtx_tx);
        }
        break;
    case IOCTL_RX:
        {
            pcixx_rx_hdr rx;
            socket_canframe *data = NULL;
            _DBG_(">>> IOCTL_RX");
            ret = copy_from_user(&rx, arg, sizeof(rx));
            if (ret) {
                _DBG_("copy_from_user(hdr) failed");
                ret = -EFAULT;
                break;
            }
            port = PCIXX_SAFE_PPORT(d, rx.port);
            PCIXX_MUTEX_LOCK(&port->mtx_rx);
            if (rx.buf) {
                PCIXX_SIG_PEND(&port->sig_rx);
                data = pcixx_rx_fifo_rlck(&port->rx_fifo);
                if (!data) {
                    _DBG_("can%d: rx-fifo empty, wait for irq/timeout and check again", rx.port);
                    PCIXX_SIG_WAIT(&port->sig_rx, HZ);
                    data = pcixx_rx_fifo_rlck(&port->rx_fifo);
                }
                if (data) {
                    int len = apollocan ? sizeof(apollo_canframe) : sizeof(socket_canframe);
                    ret = copy_to_user(rx.buf, data, len);
                    if (ret) {
                        _DBG_("can%d: copy_to_user(dat) failed", rx.port);
                        ret = -EFAULT;
                    }
                    pcixx_rx_fifo_get(&port->rx_fifo);
                } else {
                    ret = -EAGAIN;
                }
            } else {
                ret = pcixx_rx_fifo_cnt(&port->rx_fifo);
                if (ret) {
                    _DBG_("can%d: rx-fifo: %d/%d", rx.port, ret, PCIXX_RX_FIFO_SIZE);
                }
            }
            PCIXX_MUTEX_UNLOCK(&port->mtx_rx);
        }
        break;
    }
    return ret;
}

static const struct file_operations pcixx_fops = {
    .open = pcixx_f_open,
    .release = pcixx_f_close,
    .llseek = no_llseek,
    .unlocked_ioctl = pcixx_f_ioctl,
};

static void pcixx_miscdev_deregister(pcixx_device *d)
{
    _DBG_("+++");
    if (d->miscdev_rdy)
        misc_deregister(&d->miscdev);
    d->miscdev_rdy = false;
    _DBG_("---");
}

static int pcixx_miscdev_register(pcixx_device *d)
{
    int ret = -1;
    unsigned long flags;
    int idx;

    _DBG_("+++");
    do {
        spin_lock_irqsave(&glock, flags);
        for (idx = 0; idx < 32; idx++) {
            if (gfound & (1<<idx))
                continue;
            gfound |= 1 << idx;
            break;
        }
        spin_unlock_irqrestore(&glock, flags);
        if (idx >= 32) {
            _MSG_("idx >= 32");
            break;
        }
        sprintf(d->miscname, PCIXX_DRIVER_NAME "%d", idx);

        INIT_LIST_HEAD(&d->miscdev.list);
        d->miscdev.minor = MISC_DYNAMIC_MINOR;
        d->miscdev.name = d->miscname;
        d->miscdev.fops = &pcixx_fops;
        ret = misc_register(&d->miscdev);
        if (ret) {
            _MSG_("misc_register failed");
            break;
        }
        d->miscdev_rdy = !ret;
    } while (0);
    _DBG_("--- name='%s', ret=%d", d->miscname, ret);
    return ret;
}

// alloc & free

static void pcixx_del_port(pcixx_device *d, int idx)
{
    pcixx_port *port = d->ports[idx];
    _DBG_("+++ can%d", idx);
    if (port) {
        struct net_device *ndev = port->ndev;
        if (ndev) {
            netdev_info(ndev, "can%d: unregister_netdev\n", idx);
            if (ndev->netdev_ops) {
                unregister_netdev(ndev);
                ndev->netdev_ops = NULL;
            }
            if (port->dma.kva) {
                _DBG_("dma_free_coherent: kpa=0x%08llx, kva=0x%p", (u64)port->dma.kpa, port->dma.kva);
                dma_free_coherent(&d->pcidev->dev, DMA_CACHE_SIZE, port->dma.kva, port->dma.kpa);
                port->dma.kva = NULL;
            }
            free_candev(ndev);
            _DBG_("can%d: free_candev", idx);
        }
        d->ports[idx] = NULL;
    }
    _DBG_("--- can%d", idx);
}

static int pcixx_add_port(pcixx_device *d, int idx, void __iomem *regs)
{
    int err = -ENODEV;
    struct net_device *ndev = NULL;
    pcixx_port *port = NULL;
    _DBG_("+++ can%d: d.base=0x%p, p.base=0x%p", idx, d->regs, regs);
    do {
        ndev = alloc_candev(sizeof(*port), 1);
        if (!ndev) {
            _MSG_("can%d: alloc_candev(%d) failed", idx, (unsigned)sizeof(*port));
            break;
        }
        _DBG_("can%d: alloc_candev(%d) succeeded", idx, (unsigned)sizeof(*port));

        d->ports[idx] = port = netdev_priv(ndev);
        port->ndev = ndev;
        port->parent = d;
        port->regs = regs;
        port->index = idx;

        PCIXX_MUTEX_INIT(&port->mtx_cfg);
        PCIXX_MUTEX_INIT(&port->mtx_tx);
        PCIXX_MUTEX_INIT(&port->mtx_rx);
        PCIXX_SIG_INIT(&port->sig_tx);
        PCIXX_SIG_INIT(&port->sig_rx);

        port->dma.kva = dma_alloc_coherent(&d->pcidev->dev, DMA_CACHE_SIZE, &port->dma.kpa, GFP_KERNEL);
        if (port->dma.kva == NULL) {
            _MSG_("dma_alloc_coherent failed");
            break;
        }
        _DBG_("dma_alloc_coherent: kpa=0x%08llx, kva=0x%p", (u64)port->dma.kpa, port->dma.kva);

        W_REG(port->regs, REG_RTX_BASE, (u32)port->dma.kpa - d->dma.pcie_remap);

        port->can.state = CAN_STATE_STOPPED;
        port->can.clock.freq = CAN_CLK;
        port->can.bittiming_const = &pcixx_can_abtr_const;
        port->can.data_bittiming_const = &pcixx_can_dbtr_const;
        port->can.do_set_mode = pcixx_can_set_mode;
        port->can.do_get_berr_counter = pcixx_can_get_berr_counter;
        port->can.ctrlmode_supported =
            CAN_CTRLMODE_LISTENONLY |
            CAN_CTRLMODE_ONE_SHOT |
            CAN_CTRLMODE_3_SAMPLES |
            CAN_CTRLMODE_FD |
            CAN_CTRLMODE_LOOPBACK |
            CAN_CTRLMODE_BERR_REPORTING;

        ndev->netdev_ops = &pcixx_can_ops;
        ndev->flags |= IFF_ECHO;

        SET_NETDEV_DEV(ndev, &d->pcidev->dev);
        err = register_candev(ndev);
        if (err) {
            netdev_err(ndev, "can%d: register_candev failed: err=%d\n", idx, err);
            break;
        }
        _DBG_("can%d: register_candev succeeded", idx);
        err = 0;
    } while (0);
    if (err) pcixx_del_port(d, idx);
    _DBG_("--- can%d: err=%d", idx, err);
    return err;
}

// timer
static void pcixx_timer(struct timer_list *timer)
{
    pcixx_device *d = from_timer(d, timer, timer);
    pcixx_port *port = NULL;
    unsigned long flags;
    int i;
    u32 wi, ri, filled;

    spin_lock_irqsave(&d->lock, flags);
    for (i = 0; i < d->nports; i++) {
        port = PCIXX_SAFE_PPORT(d, i);
        wi = R_REG(port->regs, REG_RTX_WI);
        ri = R_REG(port->regs, REG_RTX_RI);
        filled = (wi - ri) & 0x3fff;
        if (filled < DMA_FIFO_SIZE && netif_queue_stopped(port->ndev)) {
            netif_wake_queue(port->ndev);
        }
    }
    spin_unlock_irqrestore(&d->lock, flags);
    mod_timer(timer, jiffies + msecs_to_jiffies(10));
}

// irq

irqreturn_t pcixx_isr(int irq, pcixx_device *d)
{
    pcixx_port *port = NULL;
    struct net_device *ndev = NULL;
    struct net_device_stats *stats = NULL;
    struct sk_buff *skb = NULL;
    struct canfd_frame *dst = NULL;
    void *regs = d->regs;
    bool canfd, caner, echo;
    u32 ier, isr, wi, ri, filled;
    can_msg *src;
    u32 retry = 100;

    ier = R_REG(regs, REG_IER);
    isr = R_REG(regs, REG_ISR);
    W_REG(regs, REG_IER, 0);

    wi = R_REG(regs, REG_RX_WI);
    ri = R_REG(regs, REG_RX_RI);
    filled = (wi - ri) & 0x3fff;

    _DBG_("+++ ier=0x%08x, isr=0x%08x, FIFO=[%04x/%04x#%d]", ier, isr, wi, ri, filled);

    spin_lock(&d->lock);
    while (retry--) {
        wi = R_REG(regs, REG_RX_WI);
        ri = R_REG(regs, REG_RX_RI);
        filled = (wi - ri) & 0x3fff;
        if (!filled) break;

        src = d->dma.kva;
        src += (ri & DMA_FIFO_MASK);
        port = PCIXX_SAFE_PPORT(d, (int)src->rx.chn);
        ndev = port->ndev;
        stats = &ndev->stats;

        caner = !!src->rx.err;
        canfd = !!src->rx.fd;
        echo  = !!src->rx.echo;

        {
            u32 *p = (u32*)src;
            _DBG_("can%d: FIFO=[%08x:%08x#%u], dat="
                "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                port->index,
                wi, ri, filled,
                p[0x0], p[0x1], p[0x2], p[0x3], p[0x4], p[0x5], p[0x6], p[0x7],
                p[0x8], p[0x9], p[0xa], p[0xb], p[0xc], p[0xd], p[0xe], p[0xf]);
        }

        if (echo && !caner) {
            _DBG_("can%d: echo", port->index);
            W_REG(regs, REG_RX_RI, ri + 1);
            PCIXX_SIG_WAKE(&port->sig_tx);
            continue;
        }

        if (caner) {
            // filter bus-errs
            u64 berr = *(u64*)src->rx.dat;
            if (port->last_berr == berr) {
                W_REG(regs, REG_RX_RI, ri + 1);
                continue;
            }
            _DBG_("berr=0x%016llx", berr);
            port->last_berr = berr;
        }

        // ioctl
        if (ioctlcan) {
            if (!caner) {
                if (apollocan) {
                    apollo_canframe *dst = (apollo_canframe*)pcixx_rx_fifo_wlck(&port->rx_fifo);
                    u8 *p = (u8*)src->rx.dat;
                    _DBG_("apollocan: %08x: %02x %02x %02x %02x %02x %02x %02x %02x", src->rx.id, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
                    if (!dst) {
                        _DBG_("pcixx_rx_fifo_wlck failed");
                    } else {
                        dst->id = src->rx.id;
                        dst->len = src->rx.len;
                        memcpy(dst->dat, src->rx.dat, src->rx.len);
                        pcixx_rx_fifo_put(&port->rx_fifo);
                    }
                } else {
                    socket_canframe *dst = pcixx_rx_fifo_wlck(&port->rx_fifo);
                    u8 *p = (u8*)src->rx.dat;
                    _DBG_("socketcan: %08x: %02x %02x %02x %02x %02x %02x %02x %02x", src->rx.id, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
                    if (!dst) {
                        _DBG_("pcixx_rx_fifo_wlck failed");
                    } else {
                        dst->flags = canfd ? CANFD_FDF : 0;
                        if (src->rx.brs) dst->flags |= CANFD_BRS;
                        if (src->rx.esi) dst->flags |= CANFD_ESI;
                        dst->can_id = src->rx.id;
                        if (src->rx.rtr) dst->can_id |= CAN_RTR_FLAG;
                        if (src->rx.ext) dst->can_id |= CAN_EFF_FLAG;
                        dst->len = src->rx.len;
                        memcpy(dst->data, src->rx.dat, src->rx.len);
                        pcixx_rx_fifo_put(&port->rx_fifo);
                    }
                }
            }
            W_REG(regs, REG_RX_RI, ri + 1);
            if (!caner) {
                PCIXX_SIG_WAKE(&port->sig_rx);
            }
            continue;
        }

        // socketcan
        skb = !caner ? (canfd ? alloc_canfd_skb(ndev, &dst) : alloc_can_skb(ndev, (struct can_frame**)&dst)) : alloc_can_err_skb(ndev, (struct can_frame**)&dst);
        if (!skb || !dst) {
            _MSG_("alloc_xxx_skb failed");
            break;
        }

        if (caner) {
            dst->can_id = CAN_ERR_FLAG;
            dst->can_id |= CAN_ERR_BUSERROR;
            dst->len = 0;
            port->can.can_stats.bus_error++;
            _DBG_("caner: count=%d", port->can.can_stats.bus_error);
        } else if (canfd) {
            u8 *p = dst->data;
            dst->flags = 0;
            if (src->rx.brs) dst->flags |= CANFD_BRS;
            if (src->rx.esi) dst->flags |= CANFD_ESI;
            dst->can_id = src->rx.id;
            if (src->rx.rtr) dst->can_id |= CAN_RTR_FLAG;
            if (src->rx.ext) dst->can_id |= CAN_EFF_FLAG;
            dst->len = src->rx.len;
            memcpy(dst->data, src->rx.dat, src->rx.len);
            _DBG_("canfd: %08x: %02x %02x %02x %02x %02x %02x %02x %02x", dst->can_id, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        } else {
            u8 *p = dst->data;
            dst->can_id = src->rx.id;
            if (src->rx.rtr) dst->can_id |= CAN_RTR_FLAG;
            if (src->rx.ext) dst->can_id |= CAN_EFF_FLAG;
            dst->len = src->rx.len;
            memcpy(dst->data, src->rx.dat, src->rx.len);
            _DBG_("can20: %08x: %02x %02x %02x %02x %02x %02x %02x %02x", dst->can_id, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        }

        W_REG(regs, REG_RX_RI, ri + 1);

        stats->rx_packets++;
        stats->rx_bytes += caner ? 0 : (canfd ? sizeof(struct canfd_frame) : sizeof(struct can_frame));

        netif_rx(skb);
    }
    spin_unlock(&d->lock);

    _DBG_("--- ier=0x%08x, isr=0x%08x, FIFO=[%04x/%04x#%d]", ier, isr, wi, ri, filled);

    W_REG(regs, REG_ICR, 0xffffffff);
    W_REG(regs, REG_IER, 0xffffffff);
    return isr ? IRQ_HANDLED : IRQ_NONE;
}

// sysfs

static bool h2v(char c, u8 *v)
{
    if (c >= 'A' && c <= 'F')
        c += 'a' - 'A';
    if (c >= '0' && c <= '9')
        *v = c - '0';
    else if (c >= 'a' && c <= 'f')
        *v = c - 'a' + 10;
    else
        return false;
    return true;
}

static u32 s2n(const char *s, size_t cnt)
{
    bool hex = cnt > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    u32 v = 0;
    u8 i, c, t;
    if (hex) {
        for (i = 0, s += 2; (c = *s) && (i < min((size_t)8, cnt-3)); i++, s++) {
            if (!h2v(c, &t)) return 0;
            v = (v << 4) | t;
        }
    } else {
        for (i = 0; (c = *s) && (i < min((size_t)10, cnt-1)); i++, s++) {
            if (c < '0' || c > '9') return 0;
            t = c - '0';
            v = v * 10 + t;
        }
    }
    return v;
}

static int hex2bytes(const char *s, u8 *b, size_t cnt)
{
    u32 ret;
    u8 h, l;
    for (ret = 0; *s; ret++) {
        while (*s == ' ') { s++; continue; }
        if (!*s) break;
        if (!(h2v(*s++, &h))) return -1;
        if (!(h2v(*s++, &l))) return -1;
        *b++ = (h << 4) | l;
    }
    return ret;
}

// hw version
ssize_t pcixx_sysfs_show_hw_version(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "0x%08x\n", R_REG(d->bars[0].ptr + 0x1000, 0));
}

ssize_t pcixx_sysfs_store_hw_version(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    return cnt;
}

// fw version
ssize_t pcixx_sysfs_show_fw_version(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "0x%08x\n", R_REG(d->regs, REG_CARD_INFO_A));
}

ssize_t pcixx_sysfs_store_fw_version(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    return cnt;
}

// id-switch
ssize_t pcixx_sysfs_show_id_sw(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "0x%08x\n", R_REG(d->regs, REG_ID_SW));
}

ssize_t pcixx_sysfs_store_id_sw(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    return cnt;
}

// term-res
ssize_t pcixx_sysfs_show_term_res(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "0x%08x\n", R_REG(d->regs, REG_TERM_RES));
}

ssize_t pcixx_sysfs_store_term_res(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    W_REG(d->regs, REG_TERM_RES, s2n(buf, cnt));
    return cnt;
}

// can index
ssize_t pcixx_sysfs_show_can_idx(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "%s\n", d->ports[0]->ndev->name);
}

ssize_t pcixx_sysfs_store_can_idx(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    return cnt;
}

// debug filter
ssize_t pcixx_sysfs_show_dbg(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    void *p = d->regs;
    u32 rx_wi = R_REG(p, REG_RX_WI);
    u32 rx_ri = R_REG(p, REG_RX_RI);
    u32 tx_wi, tx_ri;
    u32 i;
    char tmp[256];
    sprintf(buf,
        "dbg=0x%08x, "
        "TS=0x%08x%08x, "
        "CFG_RUN=0x%08x, "
        "CFG_END=0x%08x, "
        "DMA_ST=0x%08x, "
        "TX_EN=0x%08x, "
        "DMA_EN=0x%08x, "
        "RX=[%08x/%08x#%u], "
        "TOTAL(T/R/E)=[%d,%d,%d]\n",
        dbg,
        R_REG(p, REG_TS_H), R_REG(p, REG_TS_L),
        R_REG(p, REG_CFG_RUN), R_REG(p, REG_CFG_END),
        R_REG(p, REG_DMA_ST),
        R_REG(p, REG_TX_EN), R_REG(p, REG_DMA_EN),
        rx_wi, rx_ri, (rx_wi - rx_ri) & 0x3fff,
        R_REG(p, REG_TX_TOTAL), R_REG(p, REG_RX_TOTAL), R_REG(p, REG_ER_TOTAL)
        );
    for (i= 0; i < d->nports; i++) {
        p = d->ports[i]->regs;
        tx_wi = R_REG(p, REG_RTX_WI);
        tx_ri = R_REG(p, REG_RTX_RI);
        sprintf(tmp,
            "can%d: DBG_MAIN=%08x, ABRPR=%08x, ABTR=%08x, DBRPR=%08x, DBTR=%08x, AFM=%08x, AFD=%08x, AFR=%08x, MSR=%08x, TX=[%08x/%08x#%u]\n",
            i,
            R_REG(p, REG_DBG_MAIN),
            R_REG(p, REG_ABRPR), R_REG(p, REG_ABTR), R_REG(p, REG_DBRPR), R_REG(p, REG_DBTR),
            R_REG(p, REG_AFM), R_REG(p, REG_AFD), R_REG(p, REG_AFR), R_REG(p, REG_MSR),
            tx_wi, tx_ri, (tx_wi - tx_ri) & 0x3fff
            );
        strcat(buf, tmp);
    }
    return strlen(buf);
}
ssize_t pcixx_sysfs_store_dbg(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    dbg = s2n(buf, cnt);
    return cnt;
}

// timed/cyclic tx table ptr
ssize_t pcixx_sysfs_show_ttx_ptr(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    return sprintf(buf, "ttx_ptr=%d\n", d->ttx_ptr);
}
ssize_t pcixx_sysfs_store_ttx_ptr(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    d->ttx_ptr = s2n(buf, cnt);
    return cnt;
}

// timed/cyclic tx table cfg
ssize_t pcixx_sysfs_show_ttx_cfg(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    u8 *b = d->ttx_cfg;
    return sprintf(buf, "cfg[%d]: len=%d, dat=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
        d->ttx_ptr, d->ttx_len,
        b[0x0],b[0x1],b[0x2],b[0x3],b[0x4],b[0x5],b[0x6],b[0x7],
        b[0x8],b[0x9],b[0xa],b[0xb],b[0xc],b[0xd],b[0xe],b[0xf]
        );
}
ssize_t pcixx_sysfs_store_ttx_cfg(struct device *dev, struct device_attribute *attr, const char *buf, size_t cnt)
{
    struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
    pcixx_device *d = pci_get_drvdata(pdev);
    u8 *b = d->ttx_cfg;
    d->ttx_len = hex2bytes(buf, d->ttx_cfg, sizeof(d->ttx_cfg));
    _DBG_("cfg[%d]: len=%d, dat=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
        d->ttx_ptr, d->ttx_len,
        b[0x0],b[0x1],b[0x2],b[0x3],b[0x4],b[0x5],b[0x6],b[0x7],
        b[0x8],b[0x9],b[0xa],b[0xb],b[0xc],b[0xd],b[0xe],b[0xf]
        );
    return cnt;
}

// sysfs

static DEVICE_ATTR(hw_version, 0644, pcixx_sysfs_show_hw_version, pcixx_sysfs_store_hw_version);
static DEVICE_ATTR(fw_version, 0644, pcixx_sysfs_show_fw_version, pcixx_sysfs_store_fw_version);
static DEVICE_ATTR(id_sw,      0644, pcixx_sysfs_show_id_sw,      pcixx_sysfs_store_id_sw);
static DEVICE_ATTR(term_res,   0644, pcixx_sysfs_show_term_res,   pcixx_sysfs_store_term_res);
static DEVICE_ATTR(can_idx,    0644, pcixx_sysfs_show_can_idx,    pcixx_sysfs_store_can_idx);
static DEVICE_ATTR(dbg,        0644, pcixx_sysfs_show_dbg,        pcixx_sysfs_store_dbg);
static DEVICE_ATTR(ttx_ptr,    0644, pcixx_sysfs_show_ttx_ptr,    pcixx_sysfs_store_ttx_ptr);
static DEVICE_ATTR(ttx_cfg,    0644, pcixx_sysfs_show_ttx_cfg,    pcixx_sysfs_store_ttx_cfg);

static void pcixx_sysfs_create(struct device *dev)
{
    device_create_file(dev, &dev_attr_hw_version);
    device_create_file(dev, &dev_attr_fw_version);
    device_create_file(dev, &dev_attr_id_sw);
    device_create_file(dev, &dev_attr_term_res);
    device_create_file(dev, &dev_attr_can_idx);
    device_create_file(dev, &dev_attr_dbg);
    device_create_file(dev, &dev_attr_ttx_ptr);
    device_create_file(dev, &dev_attr_ttx_cfg);
}

static void pcixx_sysfs_remove(struct device *dev)
{
    device_remove_file(dev, &dev_attr_hw_version);
    device_remove_file(dev, &dev_attr_fw_version);
    device_remove_file(dev, &dev_attr_id_sw);
    device_remove_file(dev, &dev_attr_term_res);
    device_remove_file(dev, &dev_attr_can_idx);
    device_remove_file(dev, &dev_attr_dbg);
    device_remove_file(dev, &dev_attr_ttx_ptr);
    device_remove_file(dev, &dev_attr_ttx_cfg);
}

// probe/remove

static void pcixx_remove(struct pci_dev *dev)
{
    pcixx_device *d = pci_get_drvdata(dev);
    u32 port;
    _MSG_("+++");
    pci_set_drvdata(dev, NULL);
    if (d) {
        del_timer_sync(&d->timer);
        pcixx_miscdev_deregister(d);
        pcixx_sysfs_remove(&d->pcidev->dev);
        for (port = 0; port < d->nports; port++)
            pcixx_del_port(d, port);
        pcixx_unmap_bars(d);
        if (d->irq != -1) free_irq(d->irq, d);
        if (d->dma.kva) {
            _DBG_("dma_free_coherent: kpa=0x%08llx, kva=0x%p", (u64)d->dma.kpa, d->dma.kva);
            dma_free_coherent(&d->pcidev->dev, DMA_CACHE_SIZE, d->dma.kva, d->dma.kpa);
            d->dma.kva = NULL;
        }
        _MSG_("kfree(device)");
        kfree(d);
    }
    pci_release_regions(dev);
    pci_disable_device(dev);
    _MSG_("---");
}

static int pcixx_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int err = -ENODEV, hw;
    pcixx_device *d = NULL;
    u32 port;
    u32 v;

    _MSG_("+++ dev=0x%08lx", (long)dev);
    do {
        pci_set_drvdata(dev, d);
        if (pci_enable_device(dev)) {
            _MSG_("pci_enable_device failed");
            break;
        }
        _DBG_("pci_enable_device succeeded");
        pci_set_master(dev);
        if (pci_request_regions(dev, PCIXX_DRIVER_NAME)) {
            _MSG_("pci_request_regions failed");
            break;
        }
        _DBG_("pci_request_regions succeeded");
        hw = pcixx_hw_detect(dev);
        if (hw < 0) {
            _MSG_("pcixx_hw_detect failed");
            break;
        }
        _DBG_("pcixx_hw_detect succeeded: hw=%d", hw);
        d = kzalloc(sizeof(*d), GFP_KERNEL);
        if (!d) {
            _MSG_("kzalloc(%u) failed", (unsigned)sizeof(*d));
            break;
        }
        pci_set_drvdata(dev, d);
        d->pcidev = dev;
        _DBG_("kzalloc(%u) succeeded", (unsigned)sizeof(*d));

        pcixx_map_bars(d);

        spin_lock_init(&d->lock);
        d->irq = -1;
        d->type = hw;
        d->nports = pcixx_ports[hw];
        d->pcie = d->bars[0].ptr + 0x00000;
        d->spi = d->bars[0].ptr + 0x10000;
        d->regs = d->bars[0].ptr + 0x20000;
        d->auth = d->bars[0].ptr + 0x30000;

        v = R_REG(d->regs, REG_CARD_INFO_A);
        d->nports = min(d->nports, (v >> 8) & 0x0f);

        dma_set_coherent_mask(&d->pcidev->dev, DMA_BIT_MASK(28));
        d->dma.kva = dma_alloc_coherent(&d->pcidev->dev, DMA_CACHE_SIZE, &d->dma.kpa, GFP_KERNEL);

        d->dma.pcie_remap = (u32)d->dma.kpa & ~0x3fffffffu;

        if (d->dma.kva == NULL) {
            _MSG_("dma_alloc_coherent failed");
            break;
        }
        _DBG_("dma_alloc_coherent: kpa=0x%08llx, kva=0x%p", (u64)d->dma.kpa, d->dma.kva);

        W_REG(d->pcie, 0x20c, d->dma.pcie_remap);
        _MSG_("pcie_remap=0x%08x", R_REG(d->pcie, 0x20c));

        W_REG(d->regs, REG_RX_BASE, (u32)d->dma.kpa - d->dma.pcie_remap);

        pcixx_auth(d);
        pcixx_hw_init(d, 0);

        W_REG(d->regs, REG_RX_RI, R_REG(d->regs, REG_RX_WI));

        for (port = 0; port < d->nports; port++) {
            switch (d->type) {
            case PCIE9A01:
            case PCIE9A02:
            case PCIE9A04:
            case PCIE9A12:
            case PCIE9A22:
            case PCIE0108:
                pcixx_add_port(d, port, d->regs + 0x100 * port);
                break;
            }
        }

        err = request_irq(dev->irq, (irq_handler_t)pcixx_isr, IRQF_SHARED, PCIXX_DRIVER_NAME, d);
        if (err) {
            _MSG_("request_irq failed: err=%d", err);
            break;
        }
        d->irq = dev->irq;

        pcixx_sysfs_create(&d->pcidev->dev);

        pcixx_hw_init(d, 1);

        err = pcixx_miscdev_register(d);
        if (err) {
            _MSG_("pcixx_miscdev_register failed: err=%d", err);
            break;
        }

        timer_setup(&d->timer, pcixx_timer, 0);
        mod_timer(&d->timer, jiffies + msecs_to_jiffies(10));
    } while (0);
    if (err) pcixx_remove(dev);
    _MSG_("--- err=%d", err);
    return err;
}

// pci_driver

static int pcixx_suspend(struct pci_dev *dev, pm_message_t state)
{
    pcixx_device *d = pci_get_drvdata(dev);
    _DBG_("+++ type=%d", d->type);
    pci_save_state(dev);
    pci_disable_device(dev);
    pci_set_power_state(dev, pci_choose_state(dev, state));
    _DBG_("---");
    return 0;
}

static int pcixx_resume(struct pci_dev *dev)
{
    pcixx_device *d = pci_get_drvdata(dev);
    int err;
    _DBG_("+++ type=%d", d->type);
    pci_set_power_state(dev, PCI_D0);
    pci_restore_state(dev);
    err = pci_enable_device(dev);
    if (!err && d) pcixx_hw_init(d, 2);
    _DBG_("--- pci_enable_device: err=%d", err);
    return err;
}

static struct pci_driver pcixx_driver = {
    .name = PCIXX_DRIVER_NAME,
    .id_table = pcixx_id_table,
    .probe = pcixx_probe,
    .remove = pcixx_remove,
    .suspend = pcixx_suspend,
    .resume = pcixx_resume,
};

static int pcixx_module_init(void)
{
    int err;
    _DBG_("***");
    spin_lock_init(&glock);
    err = pci_register_driver(&pcixx_driver);
    return err;
}

static void pcixx_module_exit(void)
{
    _DBG_("***");
    pci_unregister_driver(&pcixx_driver);
}

module_init(pcixx_module_init);
module_exit(pcixx_module_exit);
