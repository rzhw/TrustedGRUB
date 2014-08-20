#include "etherboot.h"
#include "nic.h"
#include "pci.h"

typedef unsigned char   byte;
typedef unsigned short  word;
typedef unsigned long   dword;
typedef unsigned long   address;

static unsigned long iobase = 0;

#define PCNET32_LOG_TX_BUFFERS 4
#define PCNET32_LOG_RX_BUFFERS 4
#define TX_RING_SIZE		(1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK	(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS	((PCNET32_LOG_TX_BUFFERS) << 12)
#define RX_RING_SIZE		(1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK	(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS	((PCNET32_LOG_RX_BUFFERS) << 4)
#define PKT_BUF_SZ		1544

#define PCNET32_RDP	0x10
#define PCNET32_RAP	0x12
#define PCNET32_RST	0x14
#define PCNET32_BDP	0x16


/* The PCNET32 Rx and Tx ring descriptors. */
struct pcnet32_rx_head {
    dword base;
    short buf_length;
    short status;    
    dword msg_length;
    dword reserved;
} rx_ring[RX_RING_SIZE];
static int cur_rx = 0;

	
struct pcnet32_tx_head {
    dword base;
    short length;
    short status;
    dword misc;
    dword reserved;
} tx_ring[TX_RING_SIZE];
static int cur_tx = 0;


/* The PCNET32 32-Bit initialization block, described in databook. */
struct pcnet32_init_block {
    word mode;
    word tlen_rlen;
    byte phys_addr[6];
    word reserved;
    dword filter[2];
    /* Receive and transmit ring base, along with extra bits. */    
    dword rx_ring;
    dword tx_ring;
} init_block __attribute__ ((aligned(16)));



#define _outw(v,p)	__outw((v),(p))
#define _inw(p)		__inw((p))

#define READ_CSR(x) ({__asm__ __volatile__ ("nop");_outw((x),iobase+PCNET32_RAP); _inw(iobase+PCNET32_RDP);})
#define READ_BCR(x) ({__asm__ __volatile__ ("nop");_outw((x),iobase+PCNET32_RAP); _inw(iobase+PCNET32_BDP);})
#define WRITE_CSR(x,v) ({__asm__ __volatile__ ("nop");_outw((x),iobase+PCNET32_RAP);_outw((v),iobase+PCNET32_RDP);})
#define WRITE_BCR(x,v) ({__asm__ __volatile__ ("nop");_outw((x),iobase+PCNET32_RAP);_outw((v),iobase+PCNET32_BDP);})


static unsigned char rx_buffers[RX_RING_SIZE][PKT_BUF_SZ];
static unsigned char tx_buff[2048];

static void
pcnet32_getaddress(unsigned char* adr)
{
    int i;
    for (i = 0; i < 6; i++)
	adr[i] = inb(iobase + i);
};


static void
pcnet32_reset(struct nic *card)
{
};


static void
pcnet32_disable(struct nic *card)
{
  /* reset */
  _inw(iobase+PCNET32_RST);
  WRITE_CSR(0, 4);	  
};


static int
pcnet32_init(struct nic *card)
{
    int i;
    word val;

    /* reset */
    _inw(iobase+PCNET32_RST);

    READ_CSR(0);
    if ((READ_CSR(88) | (READ_CSR(89) << 16)) != 0x2621003)
	return 0;
    
    /* enable 32-bit mode */
    WRITE_BCR(0x0014, 0x0002);
    
    {
	/* set/reset autoselect bit */
	_outw(0x2, iobase+PCNET32_RAP);
	val = _inw(iobase+PCNET32_BDP) & ~2;
	val |= 2;
	_outw(val, iobase+PCNET32_BDP);
	
	/* handle full duplex setting */
	_outw(0x0009, iobase+PCNET32_RAP);
	val = _inw(iobase+PCNET32_BDP) & ~3;
	_outw(val, iobase+PCNET32_BDP);
	
	/* set/reset GPSI bit in test register */
	_outw(0x007c, iobase+PCNET32_RAP);
	val = _inw(iobase+PCNET32_RDP) & ~0x10;
	_outw(val, iobase+PCNET32_RDP);
	
    }
    
    for (i = 0; i < RX_RING_SIZE; i++)
    {
	rx_ring[i].base = (dword) (rx_buffers[i]);
	rx_ring[i].status = 0x8000;
	rx_ring[i].buf_length = -PKT_BUF_SZ;
    };
    for (i = 0; i < TX_RING_SIZE; i++)
	tx_ring[i].base = tx_ring[i].status = 0;
    
    /* set init block */
    init_block.mode = 0x0000;
    init_block.tlen_rlen = TX_RING_LEN_BITS | RX_RING_LEN_BITS;
    init_block.filter[0] = 0x00000000;
    init_block.filter[1] = 0x00000000;
    for (i = 0; i < 6; i++)
	init_block.phys_addr[i] = inb(iobase + i);
    init_block.rx_ring = (dword) &rx_ring;
    init_block.tx_ring = (dword) &tx_ring;
    WRITE_CSR(0x0001, (((dword)(&init_block)) & 0xFFFF));
    WRITE_CSR(0x0002, ((((dword)(&init_block)) >> 16) & 0xFFFF));
    
    WRITE_CSR(0x0004, 0x0915);
    WRITE_CSR(0x0000, 0x0001);
    
    i = 0;
    while (i++ < 100)
    {
	if (_inw(iobase+PCNET32_RDP) & 0x0100)
	    break;
    };
    _outw(0x0042, iobase+PCNET32_RDP);
    
    READ_CSR(0);
    WRITE_CSR(0, 0x7940);
    
    {
	word old;
	old = _inw(iobase+PCNET32_RAP);
	_outw(0x0070, iobase+PCNET32_RAP);
	_inw(iobase+PCNET32_RDP);
	_outw(old, iobase+PCNET32_RAP);
    }
    WRITE_CSR(0, 4);
    WRITE_CSR(0, 1);
    
    while (!(_inw(iobase+PCNET32_RDP) & 0x0100))
	/* wait */;

    _outw(0x0042, iobase+PCNET32_RDP);
    READ_CSR(0);
    WRITE_CSR(0, 0x7940);
    
    printf("AMD-PCNet32 base 0x%x, addr ", iobase);

    pcnet32_getaddress(card->node_addr);
    for (i=0; i<ETH_ALEN; i++) 
      {
	printf("%x", (unsigned char) (card->node_addr[i]));
	if (i < ETH_ALEN-1) printf (":");
      };
    printf("\n");
    
    return 1;
};


static void
pcnet32_transmit(struct nic *card, const char *d, 
                 unsigned int t, unsigned int s, const char *p)         
{
    int i;
    /* copy destination and source address to buffer */
    for (i = 0; i < ETH_ALEN; i++)
      {
        tx_buff[i] = d[i];
        tx_buff[ETH_ALEN + i] = card->node_addr[i];
      };
    /* copy packet type to buffer */
    tx_buff[2*ETH_ALEN]     = (t >> 8) & 0xFF;
    tx_buff[2*ETH_ALEN+ 1] =  t       & 0xFF;
    /* copy the whole packet, too */
    for (i = 0; i < s; i++)
        tx_buff[2*ETH_ALEN + 2 + i] = p[i];

    s += 2*ETH_ALEN + 2;

    tx_ring[cur_tx].length =  -s;
    tx_ring[cur_tx].misc = 0x0;
    tx_ring[cur_tx].base = (dword) tx_buff;
    tx_ring[cur_tx].status = 0x8300;
    
    /* transmit poll */
    WRITE_CSR(0, 0x48);
    
    /* acknowledge IRQ */
    _outw(READ_CSR(0) & ~0x004f, iobase+PCNET32_RDP);
    _inw(iobase+PCNET32_RDP);
    WRITE_CSR(0, 0x7940);

    /* increment head pointer */
    cur_tx = (cur_tx + 1) % TX_RING_SIZE;
    
};


static int
pcnet32_poll(struct nic *card)
{
    int i, s;
    unsigned char *p;
    
    if (rx_ring[cur_rx].status < 0)
	return 0;

    /* acknowledge IRQs */
    _outw(READ_CSR(0) & ~0x004f, iobase+PCNET32_RDP);
    _inw(iobase+PCNET32_RDP); WRITE_CSR(0, 0x7940);

    s = rx_ring[cur_rx].msg_length - 4;
    p = (unsigned char*) rx_ring[cur_rx].base;
    /* marke entry free */
    rx_ring[cur_rx].status = 0x8000;
    /* increment tail pointer */
    cur_rx = (cur_rx + 1) % RX_RING_SIZE;
    
    for (i = 0; i < s; i++)
        card->packet[i] = p[i];
    card->packetlen = s;           /* available to caller */
    return 1;
};


struct nic*
pcnet32_probe(struct nic *card, unsigned short *probe_addrs, struct pci_device *pci);
                          
struct nic*
pcnet32_probe(struct nic *card, unsigned short *probe_addrs, struct pci_device *pci)
{
    unsigned short *p;

    for (p = probe_addrs; (iobase = *p) != 0; ++p)
      {
	short pci_cmd;
	pcibios_read_config_word(pci->bus, pci->devfn, PCI_COMMAND, &pci_cmd);
	if (!(pci_cmd & PCI_COMMAND_MASTER)) 
	  {
	    pci_cmd |= PCI_COMMAND_MASTER;
	    pcibios_write_config_word(pci->bus, pci->devfn, PCI_COMMAND, pci_cmd);
	  }
	if (pcnet32_init(card) > 0) 
	  {
	    /* point to NIC specific routines */
	    card->reset    = pcnet32_reset;
	    card->poll     = pcnet32_poll;
	    card->transmit = pcnet32_transmit;
	    card->disable  = pcnet32_disable;
	    return card;
	  }
      }
    /* if board found */
    return 0;
}
