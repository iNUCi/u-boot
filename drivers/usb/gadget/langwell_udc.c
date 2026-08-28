// SPDX-License-Identifier: GPL-2.0+
/*
 * Intel Langwell/Penwell USB Device Controller driver
 *
 * This is the USB 2.0 device controller found on the Intel MID SoCs
 * (Moorestown/Langwell, Medfield/Penwell and Clover Trail/Cloverview).
 * On the PCI bus it enumerates as function 8086:e006 (00:02.3 on the
 * Acer Iconia A1-830 "ducati" board).
 *
 * The controller core is ChipIdea-like: transfers are described by
 * device transfer descriptors (dTD) chained from per-endpoint queue
 * heads (dQH), configured through per-endpoint control registers.
 *
 * Parts of this driver are based on the Linux langwell_udc.c driver
 * (Copyright (C) 2008-2009, Intel Corporation) and on the U-Boot
 * ci_udc.c driver.
 */

#include <cpu_func.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <memalign.h>
#include <pci.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <wait_bit.h>

/*
 * Check if the system has too long cachelines. If the cachelines are
 * longer than 128 bytes, the driver will not be able to flush/invalidate
 * data cache over separate dQH entries. We use 128 bytes because one dQH
 * entry is 64 bytes long and there are always two dQH entries for each
 * endpoint.
 */
#if ARCH_DMA_MINALIGN > 128
#error This driver can not work on systems with caches longer than 128 bytes
#endif

/*
 * Every dTD must be individually aligned, since we can program any dTD's
 * address into HW. Cache flushing requires ARCH_DMA_MINALIGN, and the USB
 * HW requires 32-byte alignment. Align to both:
 */
#define ILIST_ALIGN		roundup(ARCH_DMA_MINALIGN, 32)
/* Each dTD is this size */
#define ILIST_ENT_RAW_SZ	sizeof(struct lnw_dtd)
/*
 * Align the size of the dTD too, so we can add this value to each dTD's
 * address to get another aligned address.
 */
#define ILIST_ENT_SZ		roundup(ILIST_ENT_RAW_SZ, ILIST_ALIGN)
/* For each endpoint, we need 2 dTDs, one for each of IN and OUT */
#define ILIST_SZ		(NUM_ENDPOINTS * 2 * ILIST_ENT_SZ)

#define EP0_MAX_PACKET_SIZE	64
#define EP_MAX_LENGTH_TRANSFER	0x4000

#define FLUSH_TIMEOUT_MS	1000
#define RESET_TIMEOUT_MS	1000

#ifndef NUM_ENDPOINTS
/*
 * EP0 for control transfers plus three general purpose endpoints,
 * enough for e.g. a CDC ACM serial gadget (bulk IN + bulk OUT +
 * interrupt IN).
 */
#define NUM_ENDPOINTS		4
#endif

/* Capability registers: only DCCPARAMS is of interest to us */
#define DCCPARAMS		0x24
#define DCCPARAMS_DC		BIT(7)
#define DCCPARAMS_DEN(x)	((x) & 0x1f)

/* Operational registers start at BAR0 + OP_REG_OFFSET */
#define OP_REG_OFFSET		0x28

struct langwell_op_regs {
	u32 extsts;			/* 0x28 */
	u32 extintr;			/* 0x2c */
	u32 usbcmd;			/* 0x30 */
	u32 usbsts;			/* 0x34 */
	u32 usbintr;			/* 0x38 */
	u32 frindex;			/* 0x3c */
	u32 ctrldssegment;		/* 0x40 */
	u32 deviceaddr;			/* 0x44 */
	u32 endptlistaddr;		/* 0x48 */
	u32 ttctrl;			/* 0x4c */
	u32 burstsize;			/* 0x50 */
	u32 txfilltuning;		/* 0x54 */
	u32 txttfilltuning;		/* 0x58 */
	u32 ic_usb;			/* 0x5c */
	u32 ulpi_viewport;		/* 0x60 */
	u32 reserved0[(0x70 - 0x64) / 4];
	u32 configflag;			/* 0x70 */
	u32 portsc1;			/* 0x74 */
	u32 reserved1[(0xb4 - 0x78) / 4];
	u32 devlc;			/* 0xb4 */
	u32 reserved2[(0xf4 - 0xb8) / 4];
	u32 otgsc;			/* 0xf4 */
	u32 usbmode;			/* 0xf8 */
	u32 reserved3;			/* 0xfc */
	u32 endptnak;			/* 0x100 */
	u32 endptnaken;			/* 0x104 */
	u32 endptsetupstat;		/* 0x108 */
	u32 endptprime;			/* 0x10c */
	u32 endptflush;			/* 0x110 */
	u32 endptstat;			/* 0x114 */
	u32 endptcomplete;		/* 0x118 */
	u32 endptctrl[16];		/* 0x11c */
};

/* USBCMD bits */
#define CMD_ITC(x)		((((x) > 0xff) ? 0xff : (x)) << 16)
#define CMD_RST			BIT(1)
#define CMD_RUN			BIT(0)

/* USBSTS bits */
#define STS_SLI			BIT(8)		/* DC suspend */
#define STS_URI			BIT(6)		/* USB reset received */
#define STS_PCI			BIT(2)		/* port change detect */
#define STS_UEI			BIT(1)		/* USB error interrupt */
#define STS_UI			BIT(0)		/* USB interrupt */

/* DEVLC port speed */
#define DEVLC_PSPD(x)		(((x) >> 25) & 3)
#define PSPD_HIGH_SPEED		2

/* PORTSC1 bits */
#define PORTS_PP		BIT(12)		/* port power */

/* USBMODE controller mode */
#define MODE_DEVICE		2

/* ENDPTCTRL bits */
#define CTRL_TXE		BIT(23)		/* TX endpoint enable */
#define CTRL_TXR		BIT(22)		/* TX data toggle reset */
#define CTRL_TXT(t)		((t) << 18)	/* TX endpoint type */
#define CTRL_TXS		BIT(16)		/* TX endpoint stall */
#define CTRL_RXE		BIT(7)		/* RX endpoint enable */
#define CTRL_RXR		BIT(6)		/* RX data toggle reset */
#define CTRL_RXT(t)		((t) << 2)	/* RX endpoint type */
#define CTRL_RXS		BIT(0)		/* RX endpoint stall */

/* dQH config word */
#define CFG_MAX_PKT(n)		((n) << 16)
#define CFG_ZLT			BIT(29)		/* stop on zero-len xfer */
#define CFG_IOS			BIT(15)		/* IRQ on setup */

/* dTD bits */
#define TERMINATE		1
#define INFO_BYTES(n)		((n) << 16)
#define INFO_IOC		BIT(15)
#define INFO_ACTIVE		BIT(7)
#define INFO_HALTED		BIT(6)
#define INFO_BUFFER_ERROR	BIT(5)
#define INFO_TX_ERROR		BIT(3)

/* Endpoint bit in the prime/flush/status/complete registers */
#define EPT_TX(x)		(1 << ((x) + 16))
#define EPT_RX(x)		(1 << (x))

/*
 * The UDC is a PCI function. On the Acer Iconia A1-830 (ducati) it is
 * located at 00:02.3 with vendor/device ID 8086:e006; BAR0 points at the
 * controller register window (0xffa60000).
 */
#define LANGWELL_PCI_DEVFN	((2 << 3) | 3)
#define LANGWELL_PCI_DID	0xe006

#define PCI_CONFIG_ADDRESS(reg) \
	(0x80000000 | (LANGWELL_PCI_DEVFN << 8) | ((reg) & ~3))
#define PCI_CONFIG_ADDR_PORT	0xcf8
#define PCI_CONFIG_DATA_PORT	0xcfc

/*
 * dQH: Device Queue Head, describes where all transfers are managed.
 * 48-byte data structure, aligned on a 64-byte boundary.
 */
struct lnw_dqh {
	/* endpoint capabilities and characteristics */
	u32 config;
	/* current dTD pointer (read-only) */
	u32 current;

	/* transfer overlay, hardware parts of a struct lnw_dtd */
	u32 next;
	u32 info;
	u32 page0;
	u32 page1;
	u32 page2;
	u32 page3;
	u32 page4;
	u32 reserved_0;

	u8 setup_data[8];

	u32 reserved_1[4];
} __aligned(64);

/*
 * dTD: Device Transfer Descriptor, describes to the device controller
 * the location and quantity of data to be sent/received for given
 * transfer.
 */
struct lnw_dtd {
	u32 next;
	u32 info;
	u32 page0;
	u32 page1;
	u32 page2;
	u32 page3;
	u32 page4;
	u32 reserved;
};

struct langwell_req {
	struct usb_request req;
	struct list_head queue;
	/* Bounce buffer allocated if needed to align the transfer */
	u8 *b_buf;
	u32 b_len;
	/* Buffer for the current transfer, either req.buf or b_buf */
	u8 *hw_buf;
	u32 dtd_count;
};

struct langwell_ep {
	struct usb_ep ep;
	struct langwell_udc *udc;
	struct list_head queue;
	bool req_primed;
};

struct langwell_udc {
	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	u8 __iomem *regs_base;
	struct langwell_op_regs __iomem *op_regs;
	struct lnw_dqh *epts;
	u8 *items_mem;
	struct langwell_ep ep[NUM_ENDPOINTS];
	struct langwell_req *ep0_req;
	struct usb_endpoint_descriptor ep0_desc;
	bool ep0_data_phase;
	bool started;
	bool softconnect;
	u8 next_device_address;
};

static const struct usb_ep_ops lnw_ep_ops;

/* Init values for USB endpoints. */
static const struct usb_ep lnw_ep_init[NUM_ENDPOINTS] = {
	[0] = {	/* EP 0 */
		.maxpacket	= EP0_MAX_PACKET_SIZE,
		.name		= "ep0",
	},
	[1] = {
		.maxpacket	= 512,
		.name		= "ep1in-bulk",
	},
	[2] = {
		.maxpacket	= 512,
		.name		= "ep2out-bulk",
	},
	[3] = {
		.maxpacket	= 512,
		.name		= "ep3in-int",
	},
};

static inline struct langwell_udc *gadget_to_langwell(struct usb_gadget *g)
{
	return container_of(g, struct langwell_udc, gadget);
}

static inline struct langwell_ep *to_langwell_ep(struct usb_ep *ep)
{
	return container_of(ep, struct langwell_ep, ep);
}

static inline struct langwell_req *to_langwell_req(struct usb_request *req)
{
	return container_of(req, struct langwell_req, req);
}

/**
 * lnw_get_qh() - return queue head for endpoint
 *
 * @udc:	controller instance
 * @ep_num:	endpoint number
 * @dir_in:	direction of the endpoint (IN = 1, OUT = 0)
 *
 * The dQH list is such that each two subsequent entries N and N+1
 * represent one endpoint: the Nth entry is the OUT configuration and the
 * N+1th entry is the IN configuration.
 *
 * Return: pointer to the dQH
 */
static struct lnw_dqh *lnw_get_qh(struct langwell_udc *udc, int ep_num,
				  int dir_in)
{
	return &udc->epts[(ep_num * 2) + dir_in];
}

/**
 * lnw_get_qtd() - return preallocated transfer descriptor for endpoint
 *
 * @udc:	controller instance
 * @ep_num:	endpoint number
 * @dir_in:	direction of the endpoint (IN = 1, OUT = 0)
 *
 * Return: pointer to the dTD
 */
static struct lnw_dtd *lnw_get_qtd(struct langwell_udc *udc, int ep_num,
				   int dir_in)
{
	int index = (ep_num * 2) + dir_in;

	return (struct lnw_dtd *)(udc->items_mem + index * ILIST_ENT_SZ);
}

static void lnw_flush_qh(struct langwell_udc *udc, int ep_num)
{
	unsigned long start = (unsigned long)lnw_get_qh(udc, ep_num, 0);

	flush_dcache_range(start, start + 2 * sizeof(struct lnw_dqh));
}

static void lnw_invalidate_qh(struct langwell_udc *udc, int ep_num)
{
	unsigned long start = (unsigned long)lnw_get_qh(udc, ep_num, 0);

	invalidate_dcache_range(start, start + 2 * sizeof(struct lnw_dqh));
}

static void lnw_flush_qtd(struct langwell_udc *udc, int ep_num)
{
	unsigned long start = (unsigned long)lnw_get_qtd(udc, ep_num, 0);

	flush_dcache_range(start, start + 2 * ILIST_ENT_SZ);
}

static void lnw_invalidate_qtd(struct langwell_udc *udc, int ep_num)
{
	unsigned long start = (unsigned long)lnw_get_qtd(udc, ep_num, 0);

	invalidate_dcache_range(start, start + 2 * ILIST_ENT_SZ);
}

static void lnw_flush_td(struct lnw_dtd *dtd)
{
	flush_dcache_range((unsigned long)dtd,
			   (unsigned long)dtd + ILIST_ENT_SZ);
}

static void lnw_invalidate_td(struct lnw_dtd *dtd)
{
	invalidate_dcache_range((unsigned long)dtd,
				(unsigned long)dtd + ILIST_ENT_SZ);
}

/* ---------------------------------------------------------------------- */
/* PCI glue                                                                */
/* ---------------------------------------------------------------------- */

static u32 langwell_pci_read(unsigned int reg)
{
	outl(PCI_CONFIG_ADDRESS(reg), PCI_CONFIG_ADDR_PORT);

	return inl(PCI_CONFIG_DATA_PORT);
}

static void langwell_pci_write(unsigned int reg, u32 val)
{
	outl(PCI_CONFIG_ADDRESS(reg), PCI_CONFIG_ADDR_PORT);
	outl(val, PCI_CONFIG_DATA_PORT);
}

static u32 langwell_pci_read16(unsigned int reg)
{
	return (langwell_pci_read(reg) >> ((reg & 3) * 8)) & 0xffff;
}

static void langwell_pci_write16(unsigned int reg, u16 val)
{
	unsigned int shift = (reg & 3) * 8;
	u32 v = langwell_pci_read(reg);

	v = (v & ~(0xffffu << shift)) | ((u32)val << shift);
	langwell_pci_write(reg, v);
}

/**
 * langwell_enable_device() - bring the UDC PCI function into working state
 *
 * @bar:	returned physical base address of the controller registers
 *
 * Makes sure the function is powered up (D0), that memory decoding and bus
 * mastering are enabled, and returns the firmware-assigned MMIO window.
 *
 * Return: 0 if OK, -ve on error
 */
static int langwell_enable_device(phys_addr_t *bar)
{
	unsigned int ptr;
	u32 vendor, cmd;

	vendor = langwell_pci_read(PCI_VENDOR_ID);
	if (vendor != ((LANGWELL_PCI_DID << 16) | PCI_VENDOR_ID_INTEL))
		return -ENODEV;

	/*
	 * Walk the capability list looking for the power management
	 * capability and make sure the function is in the D0 power state.
	 */
	for (ptr = langwell_pci_read16(PCI_CAPABILITY_LIST) & ~3u;
	     ptr >= 0x40 && ptr <= 0xfc; ptr &= ~3u) {
		u32 hdr = langwell_pci_read(ptr);

		if ((hdr & 0xff) == PCI_CAP_ID_PM) {
			/* PMCSR (power state bits 1:0) lives at offset 4 */
			u16 pmcsr = langwell_pci_read16(ptr + 4);

			if (pmcsr & 0x3) {
				langwell_pci_write16(ptr + 4,
						     pmcsr & ~(u16)0x3);
				mdelay(20);	/* max wakeup time */
			}
			break;
		}

		ptr = (hdr >> 8) & 0xffu;
	}

	cmd = langwell_pci_read(PCI_COMMAND);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	langwell_pci_write(PCI_COMMAND, cmd);

	*bar = langwell_pci_read(PCI_BASE_ADDRESS_0) & ~0xfu;

	return 0;
}

/* ---------------------------------------------------------------------- */
/* Request handling                                                        */
/* ---------------------------------------------------------------------- */

static void request_complete(struct usb_ep *ep, struct langwell_req *req,
			     int status)
{
	if (req->req.status == -EINPROGRESS)
		req->req.status = status;

	req->req.complete(ep, &req->req);
}

static void request_complete_list(struct usb_ep *ep, struct list_head *list,
				  int status)
{
	struct langwell_req *req, *tmp;

	list_for_each_entry_safe(req, tmp, list, queue) {
		list_del_init(&req->queue);
		request_complete(ep, req, status);
	}
}

static struct usb_request *
lnw_ep_alloc_request(struct usb_ep *ep, unsigned int gfp_flags)
{
	struct langwell_ep *lwe = to_langwell_ep(ep);
	struct langwell_udc *udc = lwe->udc;
	struct langwell_req *req;
	int num = -1;

	if (ep->desc)
		num = ep->desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;

	if (num == 0 && udc->ep0_req)
		return &udc->ep0_req->req;

	req = calloc(1, sizeof(*req));
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	if (num == 0)
		udc->ep0_req = req;

	return &req->req;
}

static void lnw_ep_free_request(struct usb_ep *ep, struct usb_request *_req)
{
	struct langwell_ep *lwe = to_langwell_ep(ep);
	struct langwell_udc *udc = lwe->udc;
	struct langwell_req *req = to_langwell_req(_req);
	int num = -1;

	if (ep->desc)
		num = ep->desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;

	if (num == 0) {
		if (!udc->ep0_req || udc->ep0_req != req)
			return;
		udc->ep0_req = NULL;
	}

	free(req->b_buf);
	free(req);
}

/* ---------------------------------------------------------------------- */
/* Endpoint management                                                     */
/* ---------------------------------------------------------------------- */

static void ep_enable_hw(struct langwell_udc *udc, int num, int in, int type,
			 int maxpacket)
{
	unsigned int n;

	n = readl(&udc->op_regs->endptctrl[num]);
	if (in) {
		n &= ~CTRL_TXS;
		n |= CTRL_TXE | CTRL_TXR | CTRL_TXT(type);
	} else {
		n &= ~CTRL_RXS;
		n |= CTRL_RXE | CTRL_RXR | CTRL_RXT(type);
	}

	if (num != 0) {
		struct lnw_dqh *head = lnw_get_qh(udc, num, in);

		head->config = CFG_MAX_PKT(maxpacket) | CFG_ZLT;
		lnw_flush_qh(udc, num);
	}
	writel(n, &udc->op_regs->endptctrl[num]);
}

static int ep_disable_hw(struct langwell_udc *udc, int num, int in)
{
	unsigned int ep_bit, enable_bit;
	int err;

	if (in) {
		ep_bit = EPT_TX(num);
		enable_bit = CTRL_TXE;
	} else {
		ep_bit = EPT_RX(num);
		enable_bit = CTRL_RXE;
	}

	/* clear primed buffers */
	do {
		writel(ep_bit, &udc->op_regs->endptflush);
		err = wait_for_bit_le32(&udc->op_regs->endptflush, ep_bit,
					false, FLUSH_TIMEOUT_MS, false);
		if (err)
			return err;
	} while (readl(&udc->op_regs->endptstat) & ep_bit);

	clrbits_le32(&udc->op_regs->endptctrl[num], enable_bit);

	return 0;
}

static int lnw_ep_enable(struct usb_ep *ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct langwell_ep *lwe = to_langwell_ep(ep);
	struct langwell_udc *udc = lwe->udc;
	int num, in, type;

	num = desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	in = !!(desc->bEndpointAddress & USB_DIR_IN);
	type = usb_endpoint_type(desc);

	ep->desc = desc;

	if (num) {
		int max = get_unaligned_le16(&desc->wMaxPacketSize);

		if (max > 64 && udc->gadget.speed != USB_SPEED_HIGH)
			max = 64;
		ep->maxpacket = max;
	}

	ep_enable_hw(udc, num, in, type, ep->maxpacket);

	return 0;
}

static int lnw_ep_disable(struct usb_ep *ep)
{
	struct langwell_ep *lwe = to_langwell_ep(ep);
	struct langwell_udc *udc = lwe->udc;
	LIST_HEAD(req_list);
	int num, in, ret;

	if (!ep->desc)
		return 0;

	num = ep->desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	in = !!(ep->desc->bEndpointAddress & USB_DIR_IN);

	ret = ep_disable_hw(udc, num, in);

	/*
	 * Clear the descriptor before completing the requests: a completion
	 * handler trying to re-queue its request must see the endpoint as
	 * disabled and give up.
	 */
	lwe->req_primed = false;
	ep->desc = NULL;

	list_splice_init(&lwe->queue, &req_list);
	request_complete_list(ep, &req_list, -ESHUTDOWN);

	return ret;
}

/* ---------------------------------------------------------------------- */
/* Transfer submission                                                     */
/* ---------------------------------------------------------------------- */

static int lnw_bounce(struct langwell_req *lreq, int in)
{
	struct usb_request *req = &lreq->req;
	unsigned long addr = (unsigned long)req->buf;
	unsigned long hwaddr;
	u32 aligned_used_len;

	/* Input buffer address is not aligned. */
	if (addr & (ARCH_DMA_MINALIGN - 1))
		goto align;

	/* Input buffer length is not aligned. */
	if (req->length & (ARCH_DMA_MINALIGN - 1))
		goto align;

	/* The buffer is well aligned, only flush cache. */
	lreq->hw_buf = req->buf;
	goto flush;

align:
	if (lreq->b_buf && req->length > lreq->b_len) {
		free(lreq->b_buf);
		lreq->b_buf = NULL;
	}
	if (!lreq->b_buf) {
		lreq->b_len = roundup(req->length, ARCH_DMA_MINALIGN);
		lreq->b_buf = memalign(ARCH_DMA_MINALIGN, lreq->b_len);
		if (!lreq->b_buf)
			return -ENOMEM;
	}
	lreq->hw_buf = lreq->b_buf;

	if (in)
		memcpy(lreq->hw_buf, req->buf, req->length);

flush:
	hwaddr = (unsigned long)lreq->hw_buf;
	if (!hwaddr)
		return 0;

	aligned_used_len = roundup(req->length, ARCH_DMA_MINALIGN);
	flush_dcache_range(hwaddr, hwaddr + aligned_used_len);

	return 0;
}

static void lnw_debounce(struct langwell_req *lreq, int in)
{
	struct usb_request *req = &lreq->req;
	unsigned long addr = (unsigned long)req->buf;
	unsigned long hwaddr = (unsigned long)lreq->hw_buf;
	u32 aligned_used_len;

	if (in || !hwaddr)
		return;

	aligned_used_len = roundup(req->actual, ARCH_DMA_MINALIGN);
	invalidate_dcache_range(hwaddr, hwaddr + aligned_used_len);

	if (addr == hwaddr)
		return; /* not a bounce */

	memcpy(req->buf, lreq->hw_buf, req->actual);
}

static void lnw_ep_submit_next_request(struct langwell_ep *lwe)
{
	struct langwell_udc *udc = lwe->udc;
	struct lnw_dtd *item, *dtd, *extra;
	struct lnw_dqh *head;
	struct langwell_req *lreq;
	u8 *buf;
	u32 len_left, len_this_dtd, bit;
	int num, in, len;

	num = lwe->ep.desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	in = !!(lwe->ep.desc->bEndpointAddress & USB_DIR_IN);
	item = lnw_get_qtd(udc, num, in);
	head = lnw_get_qh(udc, num, in);

	lreq = list_first_entry(&lwe->queue, struct langwell_req, queue);
	len = lreq->req.length;

	head->next = (unsigned long)item;
	head->info = 0;

	lreq->dtd_count = 0;
	buf = lreq->hw_buf;
	len_left = len;
	dtd = item;

	do {
		len_this_dtd = min(len_left, (u32)EP_MAX_LENGTH_TRANSFER);

		dtd->info = INFO_BYTES(len_this_dtd) | INFO_ACTIVE;
		dtd->page0 = (unsigned long)buf;
		dtd->page1 = ((unsigned long)buf & 0xfffff000) + 0x1000;
		dtd->page2 = ((unsigned long)buf & 0xfffff000) + 0x2000;
		dtd->page3 = ((unsigned long)buf & 0xfffff000) + 0x3000;
		dtd->page4 = ((unsigned long)buf & 0xfffff000) + 0x4000;

		len_left -= len_this_dtd;
		buf += len_this_dtd;

		if (len_left) {
			extra = memalign(ILIST_ALIGN, ILIST_ENT_SZ);
			memset(extra, 0, ILIST_ENT_SZ);
			dtd->next = (unsigned long)extra;
			dtd = extra;
		}

		lreq->dtd_count++;
	} while (len_left);

	item = dtd;

	/*
	 * When sending the data for an IN transaction, the attached host
	 * knows that all data for the IN is sent when one of the following
	 * occurs:
	 * a) A zero-length packet is transmitted.
	 * b) A packet with length that isn't an exact multiple of the ep's
	 *    maxpacket is transmitted.
	 * c) Enough data is sent to exactly fill the host's maximum expected
	 *    IN transaction size.
	 *
	 * One of these conditions MUST apply at the end of an IN transaction,
	 * or the transaction will not be considered complete by the host. If
	 * none of (a)..(c) already applies, then we must force (a) to apply
	 * by explicitly sending an extra zero-length packet.
	 */
	/*  IN    !a     !b                              !c */
	if (in && len && !(len % lwe->ep.maxpacket) && lreq->req.zero) {
		/*
		 * Each endpoint has 2 items allocated, even though typically
		 * only 1 is used at a time since either an IN or an OUT but
		 * not both is queued. For an IN transaction, item currently
		 * points at the second of these items, so we know that we
		 * can use the other to transmit the extra zero-length packet.
		 */
		struct lnw_dtd *other_item = lnw_get_qtd(udc, num, 0);

		item->next = (unsigned long)other_item;
		item = other_item;
		item->info = INFO_ACTIVE;
	}

	item->next = TERMINATE;
	item->info |= INFO_IOC;

	lnw_flush_qtd(udc, num);

	item = (struct lnw_dtd *)(unsigned long)head->next;
	while (item->next != TERMINATE) {
		lnw_flush_td((struct lnw_dtd *)(unsigned long)item->next);
		item = (struct lnw_dtd *)(unsigned long)item->next;
	}

	lnw_flush_qh(udc, num);

	bit = in ? EPT_TX(num) : EPT_RX(num);
	writel(bit, &udc->op_regs->endptprime);

	lwe->req_primed = true;
}

static int lnw_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct langwell_ep *lwe = to_langwell_ep(_ep);
	struct langwell_req *lreq;

	list_for_each_entry(lreq, &lwe->queue, queue) {
		if (&lreq->req == _req)
			break;
	}

	if (&lreq->req != _req)
		return -EINVAL;

	list_del_init(&lreq->queue);

	if (lreq->req.status == -EINPROGRESS) {
		lreq->req.status = -ECONNRESET;
		if (lreq->req.complete)
			lreq->req.complete(_ep, _req);
	}

	return 0;
}

static int lnw_ep_queue(struct usb_ep *ep, struct usb_request *_req,
			gfp_t gfp_flags)
{
	struct langwell_ep *lwe = to_langwell_ep(ep);
	struct langwell_req *lreq = to_langwell_req(_req);
	int num, ret;

	if (!ep->desc)
		return -ESHUTDOWN;

	num = ep->desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;

	if (!num && lwe->req_primed) {
		/*
		 * The flipping of ep0 between IN and OUT relies on
		 * lnw_ep_queue consuming the current IN/OUT setting
		 * immediately. If this is deferred to a later point when the
		 * req is pulled out of the queue, then the IN/OUT setting
		 * may have been changed since the req was queued, and state
		 * will get out of sync.
		 */
		printf("langwell_udc: ep0 transaction already in progress\n");
		return -EPROTO;
	}

	ret = lnw_bounce(lreq, !!(ep->desc->bEndpointAddress & USB_DIR_IN));
	if (ret)
		return ret;

	lreq->req.status = -EINPROGRESS;

	list_add_tail(&lreq->queue, &lwe->queue);

	if (!lwe->req_primed)
		lnw_ep_submit_next_request(lwe);

	return 0;
}

static const struct usb_ep_ops lnw_ep_ops = {
	.enable		= lnw_ep_enable,
	.disable	= lnw_ep_disable,
	.queue		= lnw_ep_queue,
	.dequeue	= lnw_ep_dequeue,
	.alloc_request	= lnw_ep_alloc_request,
	.free_request	= lnw_ep_free_request,
};

/* ---------------------------------------------------------------------- */
/* Setup packet / interrupt handling                                       */
/* ---------------------------------------------------------------------- */

static void flip_ep0_direction(struct langwell_udc *udc)
{
	if (udc->ep0_desc.bEndpointAddress == USB_DIR_IN)
		udc->ep0_desc.bEndpointAddress = 0;
	else
		udc->ep0_desc.bEndpointAddress = USB_DIR_IN;
}

/*
 * This function explicitly sets the address, without the "USBADRA"
 * (advance) feature.
 */
static void lnw_set_address(struct langwell_udc *udc, u8 address)
{
	writel(address << 25, &udc->op_regs->deviceaddr);
}

static void handle_ep_complete(struct langwell_ep *lwe)
{
	struct langwell_udc *udc = lwe->udc;
	struct lnw_dtd *item, *next_td;
	struct langwell_req *lreq;
	int num, in, len, j;

	/* Set the device address that was previously sent by SET_ADDRESS */
	if (udc->next_device_address) {
		lnw_set_address(udc, udc->next_device_address);
		udc->next_device_address = 0;
	}

	num = lwe->ep.desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	in = !!(lwe->ep.desc->bEndpointAddress & USB_DIR_IN);
	item = lnw_get_qtd(udc, num, in);
	lnw_invalidate_qtd(udc, num);
	lreq = list_first_entry(&lwe->queue, struct langwell_req, queue);

	/* Check all dTDs are completed, otherwise wait for next interrupt */
	next_td = item;
	for (j = 0; j < lreq->dtd_count; j++) {
		lnw_invalidate_td(next_td);
		if (next_td->info & INFO_ACTIVE)
			return;
		if (j != lreq->dtd_count - 1)
			next_td = (struct lnw_dtd *)(unsigned long)
				next_td->next;
	}

	next_td = item;
	len = 0;
	for (j = 0; j < lreq->dtd_count; j++) {
		lnw_invalidate_td(next_td);
		item = next_td;
		len += (item->info >> 16) & 0x7fff;
		if (item->info & 0xff)
			printf("langwell_udc: EP%d/%s FAIL info=%x pg0=%x\n",
			       num, in ? "in" : "out", item->info, item->page0);
		if (j != lreq->dtd_count - 1)
			next_td = (struct lnw_dtd *)(unsigned long)
				item->next;
		if (j != 0)
			free(item);
	}

	list_del_init(&lreq->queue);
	lwe->req_primed = false;

	if (!list_empty(&lwe->queue))
		lnw_ep_submit_next_request(lwe);

	lreq->req.actual = lreq->req.length - len;
	lreq->req.status = 0;
	lnw_debounce(lreq, in);

	if (num != 0 || udc->ep0_data_phase)
		lreq->req.complete(&lwe->ep, &lreq->req);
	if (num == 0 && udc->ep0_data_phase) {
		/*
		 * Data Stage is complete, so flip ep0 dir for Status Stage,
		 * which always transfers a packet in the opposite direction.
		 */
		flip_ep0_direction(udc);
		udc->ep0_data_phase = false;
		lreq->req.length = 0;
		usb_ep_queue(&lwe->ep, &lreq->req, 0);
	}
}

#define SETUP(type, request)	(((type) << 8) | (request))

static void handle_setup(struct langwell_udc *udc)
{
	struct langwell_ep *lwe = &udc->ep[0];
	struct usb_request *req;
	struct lnw_dqh *head;
	struct usb_ctrlrequest r;
	char *buf;
	int status = 0;
	int num, in, _num, _in, i, type;

	req = &udc->ep0_req->req;
	head = lnw_get_qh(udc, 0, 0);	/* EP0 OUT */

	lnw_invalidate_qh(udc, 0);
	memcpy(&r, head->setup_data, sizeof(r));
	writel(EPT_RX(0), &udc->op_regs->endptsetupstat);

	/* Set EP0 dir for Data Stage based on Setup Stage data */
	if (r.bRequestType & USB_DIR_IN)
		udc->ep0_desc.bEndpointAddress = USB_DIR_IN;
	else
		udc->ep0_desc.bEndpointAddress = 0;

	if (le16_to_cpu(r.wLength)) {
		udc->ep0_data_phase = true;
	} else {
		/* 0 length -> no Data Stage. Flip dir for Status Stage */
		flip_ep0_direction(udc);
		udc->ep0_data_phase = false;
	}

	list_del_init(&udc->ep0_req->queue);
	lwe->req_primed = false;

	switch (SETUP(r.bRequestType, r.bRequest)) {
	case SETUP(USB_RECIP_ENDPOINT, USB_REQ_CLEAR_FEATURE):
		_num = le16_to_cpu(r.wIndex) & 15;
		_in = !!(le16_to_cpu(r.wIndex) & 0x80);

		if ((le16_to_cpu(r.wValue) == 0) &&
		    (le16_to_cpu(r.wLength) == 0)) {
			req->length = 0;
			for (i = 0; i < NUM_ENDPOINTS; i++) {
				struct langwell_ep *ep = &udc->ep[i];

				if (!ep->ep.desc)
					continue;
				num = ep->ep.desc->bEndpointAddress
					& USB_ENDPOINT_NUMBER_MASK;
				in = !!(ep->ep.desc->bEndpointAddress
					& USB_DIR_IN);
				type = usb_endpoint_type(ep->ep.desc);
				if (num == _num && in == _in) {
					ep_enable_hw(udc, num, in, type,
						     ep->ep.maxpacket);
					usb_ep_queue(udc->gadget.ep0, req, 0);
					break;
				}
			}
		}
		return;

	case SETUP(USB_RECIP_DEVICE, USB_REQ_SET_ADDRESS):
		/*
		 * write address delayed (will take effect
		 * after the next IN txn)
		 */
		udc->next_device_address = le16_to_cpu(r.wValue);
		req->length = 0;
		usb_ep_queue(udc->gadget.ep0, req, 0);
		return;

	case SETUP(USB_DIR_IN | USB_RECIP_DEVICE, USB_REQ_GET_STATUS):
		req->length = 2;
		buf = (char *)req->buf;
		buf[0] = 1 << USB_DEVICE_SELF_POWERED;
		buf[1] = 0;
		usb_ep_queue(udc->gadget.ep0, req, 0);
		return;
	}

	/* pass request up to the gadget driver */
	if (udc->driver)
		status = udc->driver->setup(&udc->gadget, &r);
	else
		status = -ENODEV;

	if (!status)
		return;

	debug("langwell_udc: STALL req %02x type %02x value %04x index %04x\n",
	      r.bRequest, r.bRequestType, le16_to_cpu(r.wValue),
	      le16_to_cpu(r.wIndex));
	writel((1 << 16) | (1 << 0), &udc->op_regs->endptctrl[0]);
}

static void stop_activity(struct langwell_udc *udc)
{
	int i;

	lnw_set_address(udc, 0);

	writel(readl(&udc->op_regs->endptcomplete),
	       &udc->op_regs->endptcomplete);
	writel(readl(&udc->op_regs->endptsetupstat),
	       &udc->op_regs->endptsetupstat);
	writel(readl(&udc->op_regs->endptstat),
	       &udc->op_regs->endptstat);
	writel(0xffffffff, &udc->op_regs->endptflush);

	for (i = 1; i < NUM_ENDPOINTS; i++)
		writel(0, &udc->op_regs->endptctrl[i]);
}

static void langwell_udc_irq(struct langwell_udc *udc)
{
	u32 n = readl(&udc->op_regs->usbsts);
	int bit, i, num, in;

	writel(n, &udc->op_regs->usbsts);

	n &= STS_SLI | STS_URI | STS_PCI | STS_UI | STS_UEI;
	if (!n)
		return;

	if (n & STS_URI) {
		stop_activity(udc);

		/* Let the gadget layer disable its functions */
		if (udc->driver && udc->driver->disconnect)
			udc->driver->disconnect(&udc->gadget);
	}

	if (n & STS_SLI)
		debug("langwell_udc: suspend\n");

	if (n & STS_PCI) {
		int max = 64;
		enum usb_device_speed speed = USB_SPEED_FULL;

		bit = DEVLC_PSPD(readl(&udc->op_regs->devlc));
		if (bit == PSPD_HIGH_SPEED) {
			speed = USB_SPEED_HIGH;
			max = 512;
		}
		udc->gadget.speed = speed;
		for (i = 1; i < NUM_ENDPOINTS; i++) {
			unsigned int limit = lnw_ep_init[i].maxpacket;

			udc->ep[i].ep.maxpacket = min(limit, (unsigned int)max);
		}
	}

	if ((n & STS_UI) || (n & STS_UEI)) {
		n = readl(&udc->op_regs->endptsetupstat);
		if (n & EPT_RX(0))
			handle_setup(udc);

		n = readl(&udc->op_regs->endptcomplete);
		if (n)
			writel(n, &udc->op_regs->endptcomplete);

		for (i = 0; i < NUM_ENDPOINTS && n; i++) {
			struct langwell_ep *lwe = &udc->ep[i];

			if (!lwe->ep.desc)
				continue;
			num = lwe->ep.desc->bEndpointAddress
				& USB_ENDPOINT_NUMBER_MASK;
			in = !!(lwe->ep.desc->bEndpointAddress & USB_DIR_IN);
			bit = in ? EPT_TX(num) : EPT_RX(num);
			if (n & bit)
				handle_ep_complete(lwe);
		}
	}
}

static int langwell_handle_interrupts(struct udevice *dev)
{
	struct langwell_udc *udc = dev_get_priv(dev);
	u32 value = readl(&udc->op_regs->usbsts);

	if (value)
		langwell_udc_irq(udc);

	return value;
}

/* ---------------------------------------------------------------------- */
/* Gadget operations                                                       */
/* ---------------------------------------------------------------------- */

static int lnw_gadget_pullup(struct usb_gadget *gadget, int is_on)
{
	struct langwell_udc *udc = gadget_to_langwell(gadget);

	if (is_on) {
		udc->softconnect = true;
		/*
		 * Connect the D+ pull-up, but only if the controller has
		 * been started already. Otherwise udc_start() will connect
		 * once it finished initializing the controller.
		 */
		if (udc->started)
			setbits_le32(&udc->op_regs->usbcmd,
				     CMD_ITC(0x08) | CMD_RUN);
	} else {
		udc->softconnect = false;
		clrbits_le32(&udc->op_regs->usbcmd, CMD_RUN);
		udelay(800);
		stop_activity(udc);
	}

	return 0;
}

static int lnw_gadget_start(struct usb_gadget *gadget,
			    struct usb_gadget_driver *driver)
{
	struct langwell_udc *udc = gadget_to_langwell(gadget);
	struct langwell_op_regs __iomem *regs = udc->op_regs;
	int ret;

	udc->driver = driver;
	udc->next_device_address = 0;

	/* Reset the controller */
	writel(CMD_ITC(0x08) | CMD_RST, &regs->usbcmd);
	ret = wait_for_bit_le32(&regs->usbcmd, CMD_RST, false,
				RESET_TIMEOUT_MS, false);
	if (ret) {
		printf("langwell_udc: reset timeout\n");
		return ret;
	}

	/* Program the endpoint list address */
	writel((unsigned long)udc->epts, &regs->endptlistaddr);

	/* Select device mode */
	writel(MODE_DEVICE, &regs->usbmode);

	/* Make sure the port power is on */
	setbits_le32(&regs->portsc1, PORTS_PP);

	/* Flush all endpoints */
	writel(0xffffffff, &regs->endptflush);

	udc->started = true;

	if (udc->softconnect)
		setbits_le32(&regs->usbcmd, CMD_ITC(0x08) | CMD_RUN);

	return 0;
}

static int lnw_gadget_stop(struct usb_gadget *gadget)
{
	struct langwell_udc *udc = gadget_to_langwell(gadget);

	udc->softconnect = false;
	clrbits_le32(&udc->op_regs->usbcmd, CMD_RUN);
	udelay(800);
	stop_activity(udc);

	udc->driver = NULL;
	udc->started = false;

	return 0;
}

static const struct usb_gadget_ops lnw_gadget_ops = {
	.pullup		= lnw_gadget_pullup,
	.udc_start	= lnw_gadget_start,
	.udc_stop	= lnw_gadget_stop,
};

/* ---------------------------------------------------------------------- */
/* Driver model integration                                                */
/* ---------------------------------------------------------------------- */

static void lnw_setup_eps(struct langwell_udc *udc)
{
	int i;

	for (i = 0; i < 2 * NUM_ENDPOINTS; i++) {
		struct lnw_dqh *head = udc->epts + i;

		if (i < 2)
			head->config = CFG_MAX_PKT(EP0_MAX_PACKET_SIZE) |
				       CFG_ZLT | CFG_IOS;
		else
			head->config = CFG_MAX_PKT(512) | CFG_ZLT;
		head->next = TERMINATE;
		head->info = 0;

		if (i & 1) {
			lnw_flush_qh(udc, i / 2);
			lnw_flush_qtd(udc, i / 2);
		}
	}

	INIT_LIST_HEAD(&udc->gadget.ep_list);

	for (i = 0; i < NUM_ENDPOINTS; i++) {
		struct langwell_ep *lwe = &udc->ep[i];

		memcpy(&lwe->ep, &lnw_ep_init[i], sizeof(lwe->ep));
		lwe->ep.ops = &lnw_ep_ops;
		lwe->udc = udc;
		INIT_LIST_HEAD(&lwe->queue);
		lwe->req_primed = false;

		if (!i) {
			udc->ep0_desc.bLength =
				sizeof(struct usb_endpoint_descriptor);
			udc->ep0_desc.bDescriptorType = USB_DT_ENDPOINT;
			udc->ep0_desc.bEndpointAddress = USB_DIR_IN;
			udc->ep0_desc.bmAttributes =
				USB_ENDPOINT_XFER_CONTROL;
			lwe->ep.desc = &udc->ep0_desc;
			udc->gadget.ep0 = &lwe->ep;
			INIT_LIST_HEAD(&lwe->ep.ep_list);
		} else {
			list_add_tail(&lwe->ep.ep_list, &udc->gadget.ep_list);
		}
	}

	/* Allocate the shared ep0 request up front */
	udc->ep0_req = calloc(1, sizeof(*udc->ep0_req));
	if (udc->ep0_req)
		INIT_LIST_HEAD(&udc->ep0_req->queue);
}

static int langwell_udc_probe(struct udevice *dev)
{
	struct langwell_udc *udc = dev_get_priv(dev);
	const int num = 2 * NUM_ENDPOINTS;
	const int eplist_min_align = 4096;
	const int eplist_align = roundup(eplist_min_align, ARCH_DMA_MINALIGN);
	const int eplist_raw_sz = num * sizeof(struct lnw_dqh);
	const int eplist_sz = roundup(eplist_raw_sz, ARCH_DMA_MINALIGN);
	phys_addr_t base;
	u8 __iomem *regs;
	u32 dccparams;
	int ret;

	ret = langwell_enable_device(&base);
	if (ret == -ENODEV || !base) {
		/*
		 * The UDC is not visible on the PCI bus or has no BAR
		 * assigned - fall back to the DT register and assume
		 * firmware enabled the function.
		 */
		base = dev_read_addr_index(dev, 0);
		if (base == FDT_ADDR_T_NONE)
			return -EINVAL;
	} else if (ret) {
		return ret;
	}

	regs = map_sysmem(base, 0x20000);

	dccparams = readl(regs + DCCPARAMS);
	if (!(dccparams & DCCPARAMS_DC)) {
		dev_err(dev, "controller is not device capable (%08x)\n",
			dccparams);
		unmap_sysmem(regs);
		return -ENODEV;
	}

	if (DCCPARAMS_DEN(dccparams) + 1 < NUM_ENDPOINTS) {
		dev_err(dev, "only %d endpoints supported\n",
			DCCPARAMS_DEN(dccparams) + 1);
		unmap_sysmem(regs);
		return -ENODEV;
	}

	udc->regs_base = regs;
	udc->op_regs = (struct langwell_op_regs __iomem *)(regs +
							   OP_REG_OFFSET);

	/* The dQH list must be aligned to 2048 bytes at least. */
	udc->epts = memalign(eplist_align, eplist_sz);
	if (!udc->epts) {
		unmap_sysmem(regs);
		return -ENOMEM;
	}
	memset(udc->epts, 0, eplist_sz);

	udc->items_mem = memalign(ILIST_ALIGN, ILIST_SZ);
	if (!udc->items_mem) {
		free(udc->epts);
		unmap_sysmem(regs);
		return -ENOMEM;
	}
	memset(udc->items_mem, 0, ILIST_SZ);

	udc->gadget.name = "langwell_udc";
	udc->gadget.ops = &lnw_gadget_ops;
	udc->gadget.is_dualspeed = 1;
	udc->gadget.max_speed = USB_SPEED_HIGH;
	udc->gadget.speed = USB_SPEED_UNKNOWN;

	lnw_setup_eps(udc);

	if (!udc->ep0_req) {
		free(udc->items_mem);
		free(udc->epts);
		unmap_sysmem(regs);
		return -ENOMEM;
	}

	ret = usb_add_gadget_udc((struct device *)dev, &udc->gadget);
	if (ret) {
		free(udc->ep0_req);
		free(udc->items_mem);
		free(udc->epts);
		unmap_sysmem(regs);
		return ret;
	}

	return 0;
}

static int langwell_udc_remove(struct udevice *dev)
{
	struct langwell_udc *udc = dev_get_priv(dev);

	usb_del_gadget_udc(&udc->gadget);

	free(udc->ep0_req);
	free(udc->items_mem);
	free(udc->epts);
	unmap_sysmem(udc->regs_base);

	return 0;
}

static const struct udevice_id langwell_udc_ids[] = {
	{ .compatible = "intel,langwell-udc" },
	{ .compatible = "intel,cloverview-udc" },
	{}
};

static const struct usb_gadget_generic_ops langwell_udc_generic_ops = {
	.handle_interrupts	= langwell_handle_interrupts,
};

U_BOOT_DRIVER(langwell_udc) = {
	.name		= "langwell_udc",
	.id		= UCLASS_USB_GADGET_GENERIC,
	.of_match	= langwell_udc_ids,
	.ops		= &langwell_udc_generic_ops,
	.probe		= langwell_udc_probe,
	.remove		= langwell_udc_remove,
	.priv_auto	= sizeof(struct langwell_udc),
};
