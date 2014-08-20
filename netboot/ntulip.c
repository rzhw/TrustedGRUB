/*
    Tulip and clone Etherboot Driver
    By Marty Connor (mdc@thinguin.org) 
    This software may be used and distributed according to the terms
    of the GNU Public License, incorporated herein by reference.

    Based on Ken Yap's Tulip Etherboot Driver and Donald Becker's
    Linux Tulip Driver. Supports N-Way speed auto-configuration on
    MX98715, MX98715A and MX98725. Support inexpensive PCI 10/100 cards
    based on the Macronix MX987x5 chip, such as the SOHOware Fast
    model SFA110A, and the LinkSYS model LNE100TX. The NetGear
    model FA310X, based on the LC82C168 chip is supported.
    The TRENDnet TE100-PCIA NIC which uses a genuine Intel 21143-PD
    chipset is supported.

    Documentation and source code used:  
      Source for Etherboot driver at
        http://www.slug.org.au/etherboot/
      MX98715A Data Sheet and MX98715A Application Note
        on http://www.macronix.com/  (PDF format files)
      Source for Linux tulip driver at
        http://cesdis.gsfc.nasa.gov/linux/drivers/tulip.html

    Adapted by Ken Yap from
    FreeBSD netboot DEC 21143 driver
    Author: David Sharp
      date: Nov/98

    Some code fragments were taken from verious places, Ken Yap's
    etherboot, FreeBSD's if_de.c, and various Linux related files.
    DEC's manuals for the 21143 and SROM format were very helpful.
    The Linux de driver development page has a number of links to
    useful related information.  Have a look at:
    ftp://cesdis.gsfc.nasa.gov/pub/linux/drivers/tulip-devel.html 
*/

/*********************************************************************/
/* Revision History                                                  */
/*********************************************************************/

/*
  29 Feb 2000   mdc     0.75b7
     Increased reset delay to 3 seconds because Macronix cards seem to
     need more reset time before card comes back to a usable state.
  26 Feb 2000   mdc     0.75b6
     Added a 1 second delay after initializing the transmitter because
     some cards seem to need the time or they drop the first packet
     transmitted.
  23 Feb 2000   mdc     0.75b5
     removed udelay code and used currticks() for more reliable delay
     code in reset pause and sanity timeouts.  Added function prototypes
     and TX debugging code.
  21 Feb 2000   mdc     patch to Etherboot 4.4.3
     Incorporated patches from Bob Edwards and Paul Mackerras of
     Linuxcare's OZLabs to deal with inefficiencies in ntulip_transmit
     and udelay.  We now wait for packet transmission to complete
     (or sanity timeout).
  04 Feb 2000   Robert.Edwards@anu.edu.au patch to Etherboot 4.4.2
     patch to ntulip.c that implements the automatic selection of the MII
     interface on cards using the Intel/DEC 21143 reference design, in
     particular, the TRENDnet TE100-PCIA NIC which uses a genuine Intel
     21143-PD chipset.
  11 Jan 2000   mdc     0.75b4
     Added support for NetGear FA310TX card based on the LC82C168
     chip.  This should also support Lite-On LC82C168 boards.
     Added simple MII support. Re-arranged code to better modularize 
     initializations.
  04 Dec 1999   mdc     0.75b3  
     Added preliminary support for LNE100TX PCI cards.  Should work for
     PNIC2 cards. No MII support, but single interface (RJ45) tulip
     cards seem to not care.
  03 Dec 1999   mdc     0.75b2
     Renamed from mx987x5 to ntulip, merged in original tulip init code
     from tulip.c to support other tulip compatible cards.
  02 Dec 1999   mdc     0.75b1
     Released Beta MX987x5 Driver for code review and testing to netboot
     and thinguin mailing lists.
*/


/*********************************************************************/
/* Declarations                                                      */
/*********************************************************************/

#include "etherboot.h"
#include "nic.h"
#include "pci.h"
#include "cards.h"

#undef NTULIP_DEBUG
#undef NTULIP_DEBUG_WHERE
#undef NTULIP_FORCE_BNC

#define TX_TIME_OUT       2*TICKS_PER_SEC

static int tulip_id = 0;

#define DC21140		1
#define DC21143		2
#define COMET		3
#define LC82C168	4
#define DC21142		5

typedef unsigned char  u8;
typedef   signed char  s8;
typedef unsigned short u16;
typedef   signed short s16;
typedef unsigned int   u32;
typedef   signed int   s32;

/* Register offsets for tulip device */
enum ntulip_offsets {
   CSR0=0,     CSR1=0x08,  CSR2=0x10,  CSR3=0x18,  CSR4=0x20,  CSR5=0x28,
   CSR6=0x30,  CSR7=0x38,  CSR8=0x40,  CSR9=0x48, CSR10=0x50, CSR11=0x58,
  CSR12=0x60, CSR13=0x68, CSR14=0x70, CSR15=0x78, CSR16=0x80, CSR20=0xA0 
};

#define DEC_21142_CSR6_TTM     0x00400000      /* Transmit Threshold Mode */
#define DEC_21142_CSR6_HBD     0x00080000      /* Heartbeat Disable */
#define DEC_21142_CSR6_PS      0x00040000      /* Port Select */

/* EEPROM Address width definitions */
#define EEPROM_ADDRLEN 6
#define EEPROM_SIZE    128              /* 2 << EEPROM_ADDRLEN */

/* Data Read from the EEPROM */
static unsigned char ee_data[EEPROM_SIZE];

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD    (5 << addr_len)
#define EE_READ_CMD     (6 << addr_len)
#define EE_ERASE_CMD    (7 << addr_len)

/* EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK    0x02    /* EEPROM shift clock. */
#define EE_CS           0x01    /* EEPROM chip select. */
#define EE_DATA_WRITE   0x04    /* EEPROM chip data in. */
#define EE_WRITE_0      0x01
#define EE_WRITE_1      0x05
#define EE_DATA_READ    0x08    /* EEPROM chip data out. */
#define EE_ENB          (0x4800 | EE_CS)

/* Delay between EEPROM clock transitions.  Even at 33Mhz current PCI
   implementations don't overrun the EEPROM clock.  We add a bus
   turn-around to insure that this remains true.  */
#define eeprom_delay()  inl(ee_addr)

/* helpful macro if on a big_endian machine for changing byte order.
   not strictly needed on Intel */
#define le16_to_cpu(val) (val)

/* transmit and receive descriptor format */
struct txrxdesc {
  volatile unsigned long   status;         /* owner, status */
  unsigned long   buf1sz:11,      /* size of buffer 1 */
    buf2sz:11,                    /* size of buffer 2 */
    control:10;                   /* control bits */
  unsigned char *buf1addr;      /* buffer 1 address */
  unsigned char *buf2addr;      /* buffer 2 address */
};

/* Size of transmit and receive buffers */
#define BUFLEN 1600

/*********************************************************************/
/* Global Storage                                                    */
/*********************************************************************/

/* PCI Bus parameters */
static unsigned short vendor, dev_id;
static unsigned long ioaddr;

/* Note: transmit and receive buffers must be longword aligned and
   longword divisable */

/* transmit descriptor and buffer */
static volatile struct txrxdesc txd __attribute__ ((aligned(4)));
static unsigned char txb[BUFLEN] __attribute__ ((aligned(4)));
 
/* receive descriptor(s) and buffer(s) */
#define NRXD 4
static volatile struct txrxdesc rxd[NRXD] __attribute__ ((aligned(4)));
static unsigned char rxb[NRXD][BUFLEN] __attribute__ ((aligned(4)));
static int rxd_tail;

/* buffer for ethernet header */
static unsigned char ehdr[ETH_HLEN];

/*********************************************************************/
/* Function Prototypes                                               */
/*********************************************************************/
static void inline whereami (char *str);
static int mdio_read(int phy_id, int location);
static void mdio_write(int phy_id, int location, int value);
static void do_mii();
static int read_eeprom(int location, int addr_len);
struct nic *ntulip_probe(struct nic *card, unsigned short *io_addrs,
			 struct pci_device *pci);
static void ntulip_init_ring(struct nic *card);
static void ntulip_reset(struct nic *card);
static void ntulip_transmit(struct nic *card, const char *d, unsigned int t,
                           unsigned int s, const char *p);
static int ntulip_poll(struct nic *card);
static void ntulip_disable(struct nic *card);


/*********************************************************************/
/* Utility Routines                                                  */
/*********************************************************************/

static void inline whereami (char *str)
{
#ifdef NTULIP_DEBUG_WHERE
  printf("%s\n", str);
  //  sleep(2);
#endif
}

static void ntulip_wait(unsigned int nticks)
{
  unsigned int to = currticks() + nticks;
  while (currticks() < to)
    /* wait */ ;
}


/*********************************************************************/
/* Media Descriptor Code                                             */
/*********************************************************************/
/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() inl(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK		0x10000
#define MDIO_DATA_WRITE0	0x00000
#define MDIO_DATA_WRITE1	0x20000
#define MDIO_ENB		0x00000	/* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ		0x80000

static int mdio_read(int phy_id, int location)
{
  int i;
  int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
  int retval = 0;
  long mdio_addr = ioaddr + CSR9;

  if (tulip_id == LC82C168) {
    i = 1000;
    outl(0x60020000 + (phy_id<<23) + (location<<18), ioaddr + 0xA0);
    inl(ioaddr + 0xA0);
    inl(ioaddr + 0xA0);
    while (--i > 0)
    if ( ! ((retval = inl(ioaddr + 0xA0)) & 0x80000000))
      return retval & 0xffff;
    return 0xffff;
  }

  if (tulip_id == COMET) {
    if (phy_id == 1) {
      if (location < 7)
        return inl(ioaddr + 0xB4 + (location<<2));
      else if (location == 17)
        return inl(ioaddr + 0xD0);
      else if (location >= 29 && location <= 31)
        return inl(ioaddr + 0xD4 + ((location-29)<<2));
    }
    return 0xffff;
  }

  /* Establish sync by sending at least 32 logic ones. */
  for (i = 32; i >= 0; i--) {
    outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
    mdio_delay();
    outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  /* Shift the read command bits out. */
  for (i = 15; i >= 0; i--) {
    int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

    outl(MDIO_ENB | dataval, mdio_addr);
    mdio_delay();
    outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  /* Read the two transition, 16 data, and wire-idle bits. */
  for (i = 19; i > 0; i--) {
    outl(MDIO_ENB_IN, mdio_addr);
    mdio_delay();
    retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
    outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  return (retval>>1) & 0xffff;
}

static void mdio_write(int phy_id, int location, int value)
{
  int i;
  int cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
  long mdio_addr = ioaddr + CSR9;

  if (tulip_id == LC82C168) {
    i = 1000;
    outl(cmd, ioaddr + 0xA0);
    do
      if ( ! (inl(ioaddr + 0xA0) & 0x80000000))
        break;
    while (--i > 0);
    return;
  }

  if (tulip_id == COMET) {
    if (phy_id != 1)
      return;
    if (location < 7)
      outl(value, ioaddr + 0xB4 + (location<<2));
    else if (location == 17)
      outl(value, ioaddr + 0xD0);
    else if (location >= 29 && location <= 31)
      outl(value, ioaddr + 0xD4 + ((location-29)<<2));
    return;
  }

  /* Establish sync by sending 32 logic ones. */
  for (i = 32; i >= 0; i--) {
    outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
    mdio_delay();
    outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  /* Shift the command bits out. */
  for (i = 31; i >= 0; i--) {
    int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
    outl(MDIO_ENB | dataval, mdio_addr);
    mdio_delay();
    outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  /* Clear out extra bits. */
  for (i = 2; i > 0; i--) {
    outl(MDIO_ENB_IN, mdio_addr);
    mdio_delay();
    outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
    mdio_delay();
  }
  return;
}

static void do_mii(void)
{
  int phy, phy_idx, mii_cnt;

  whereami("do_mii\n");

  mii_cnt = 0;
  for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < 4; phy++) {

    int mii_status = mdio_read(phy, 1);

    if ((mii_status & 0x8301) == 0x8001 ||
        ((mii_status & 0x8000) == 0  && (mii_status & 0x7800) != 0)) {

      int mii_reg0 = mdio_read(phy, 0);
      int mii_advert = mdio_read(phy, 4);
      int mii_reg4 = ((mii_status >> 6) & 0x01E1) | 1;

      phy_idx++;
      printf("MII trcvr #%d "
             "config %x status %x advertising %x reg4 %x.\n",
             phy, mii_reg0, mii_status, mii_advert, mii_reg4);
      
      mdio_write(phy, 0, mii_reg0 | 0x1000);
      if (mii_advert != mii_reg4)
        mdio_write(phy, 4, mii_reg4);
    }
  }
  mii_cnt = phy_idx;

#ifdef NTULIP_DEBUG
  printf("mii_cnt = %d\n", mii_cnt);
#endif

}

/*********************************************************************/
/* EEPROM Reading Code                                               */
/*********************************************************************/
/* EEPROM routines adapted from the Linux Tulip Code */
/* Reading a serial EEPROM is a "bit" grungy, but we work our way
   through:->.
*/
static int read_eeprom(int location, int addr_len)
{
  int i;
  unsigned short retval = 0;
  long ee_addr = ioaddr + CSR9;
  int read_cmd = location | EE_READ_CMD;

  whereami("read_eeprom\n");

  outl(EE_ENB & ~EE_CS, ee_addr);
  outl(EE_ENB, ee_addr);

  /* Shift the read command bits out. */
  for (i = 4 + addr_len; i >= 0; i--) {
    short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
    outl(EE_ENB | dataval, ee_addr);
    eeprom_delay();
    outl(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
    eeprom_delay();
  }
  outl(EE_ENB, ee_addr);

  for (i = 16; i > 0; i--) {
    outl(EE_ENB | EE_SHIFT_CLK, ee_addr);
    eeprom_delay();
    retval = (retval << 1) | ((inl(ee_addr) & EE_DATA_READ) ? 1 : 0);
    outl(EE_ENB, ee_addr);
    eeprom_delay();
  }

  /* Terminate the EEPROM access. */
  outl(EE_ENB & ~EE_CS, ee_addr);
  return retval;
}


/*********************************************************************/
/* ntulip_init_ring - setup the tx and rx descriptors                */
/*********************************************************************/
static void ntulip_init_ring(struct nic *card)
{
  int i;

  /* setup the transmit descriptor */
  txd.buf1addr = &txb[0];
  txd.buf2addr = 0;
  txd.buf1sz   = 192;             /* setup packet must be 192 bytes */
  txd.buf2sz   = 0;
  txd.control  = 0x028;           /* setup packet + TER */
  txd.status   = 0x80000000;      /* give ownership to device */

  /* construct perfect filter frame with mac address as first match
     and broadcast address for all others */
  for (i=0; i<192; i++) txb[i] = 0xFF;
  txb[0] = card->node_addr[0];
  txb[1] = card->node_addr[1];
  txb[4] = card->node_addr[2];
  txb[5] = card->node_addr[3];
  txb[8] = card->node_addr[4];
  txb[9] = card->node_addr[5];

  /* setup receive descriptor */
  for (i=0; i<NRXD; i++) {
    rxd[i].buf1addr = &rxb[i][0];
    rxd[i].buf2addr = 0;        /* not used */
    rxd[i].buf1sz   = BUFLEN;
    rxd[i].buf2sz   = 0;        /* not used */
    rxd[i].control  = 0x0;
    rxd[i].status   = 0x80000000;   /* give ownership to device */
  }

  /* Set Receive end of ring on last descriptor */
  rxd[NRXD - 1].control = 0x008;
  rxd_tail = 0;

}

/*********************************************************************/
/* eth_reset - Reset adapter                                         */
/*********************************************************************/
static void ntulip_reset(struct nic *card)
{
  unsigned long to, csr6;
  
  whereami("ntulip_reset\n");
  
  tulip_id = 0;

  /* Stop Tx and RX */
  outl(inl(ioaddr + CSR6) & ~0x00002002, ioaddr + CSR6);

  if (vendor == PCI_VENDOR_ID_MACRONIX && dev_id == PCI_DEVICE_ID_MX987x5) {
    /* set up 10-BASE-T Control Port */
    outl(0xFFFFFFFF, ioaddr + CSR14);
    /* set up 10-BASE-T Status Port  */
    outl(0x00001000, ioaddr + CSR12);
    /* Set Operation Control Register (CSR6) for MX987x5 
       to allow N-Way Active Speed selection, and
       start the chip's Tx to process setup frame. 
       While it is possible to force speed selection,
       this is probably more useful most of the time.
    */
    outl(0x01A80200, ioaddr + CSR6);

  } else if (vendor == PCI_VENDOR_ID_LINKSYS && dev_id == PCI_DEVICE_ID_LC82C115) {
    /* This is MX987x5 init code. It seems to work for the LNE100TX 
       but should be replaced when we figure out the right way
       to do this initialization
    */
    outl(0xFFFFFFFF, ioaddr + CSR14);
    outl(0x00001000, ioaddr + CSR12);
    outl(0x01A80200, ioaddr + CSR6);

  } else if (vendor == PCI_VENDOR_ID_LINKSYS && dev_id == PCI_DEVICE_ID_DEC_TULIP) {

    tulip_id = LC82C168;
    do_mii();

  } else if (vendor == PCI_VENDOR_ID_DEC) {

    char sbuf[64];
    char *s;
    int offset;
    
    sprintf(sbuf, "(unknown, ID=0x%x)", ((unsigned int *)ee_data)[0]);
    s = sbuf;
    
    switch (((unsigned int *)ee_data)[0]) 
    {
      case 0x11001186:
      case 0x91001282:
        s = "21140";
        tulip_id = DC21140;
        offset = ee_data [27] + (ee_data [28] << 8);
        do_mii();
        outl(ee_data[offset+2] | 0x100, ioaddr + CSR12);
        outl(0x00040000, ioaddr + CSR6);
        break;
      case 0x11111011:
      case 0x12071113:
        s = "21143-PD";
        tulip_id = DC21143;
        break;
      case 0: // Hack
      case 0x1430146c:
      case 0x423511f0:
	s = "21142";
	tulip_id = DC21142;
	do_mii();
	outl(0x82020000, ioaddr + CSR6);
	outl(0x00000000, ioaddr + CSR13);
	outl(0x00000000, ioaddr + CSR14);
	outl(0x820e0000, ioaddr + CSR6);
	outl(0x00040000, ioaddr + CSR6);
	break;
      default:
        break;
    }
    printf("Network card is a DEC Tulip %s\n", s);

  } else {
    /* If we don't know what to do with the card, set to 10Mbps half-duplex */

    outl(0x00000000, ioaddr + CSR13);
    outl(0x7F3F0000, ioaddr + CSR14);
    outl(0x08000008, ioaddr + CSR15);
    outl(0x00000000, ioaddr + CSR13);
    outl(0x00000001, ioaddr + CSR13);
    outl(0x02404000, ioaddr + CSR6);
    outl(0x08AF0008, ioaddr + CSR15);
    outl(0x00050008, ioaddr + CSR15);

  }

  /* Reset the chip, holding bit 0 set at least 50 PCI cycles. */
  outl(0x00000001, ioaddr + CSR0);
  ntulip_wait(TICKS_PER_SEC/2);
  
  /* turn off reset and set cache align=16lword, burst=unlimit */
  if (tulip_id == DC21143 || tulip_id == DC21142)
    outl(0x00A08000, ioaddr + CSR0);  // 21143 Bugfix
  else       
    outl(0x01A08000, ioaddr + CSR0);  // default

  /* set up transmit and receive descriptors */
  ntulip_init_ring(card);

  /* Point to receive descriptor */
  outl((unsigned long)&rxd[0], ioaddr + CSR3);
  outl((unsigned long)&txd   , ioaddr + CSR4);

  csr6 = 0x02404000;

  /* Chip specific init code */

  if (vendor == PCI_VENDOR_ID_MACRONIX && dev_id == PCI_DEVICE_ID_MX987x5) {
    csr6 = 0x01880200;
    /* Set CSR16 and CSR20 to values that allow device modification */
    outl(0x0B3C0000 | inl(ioaddr + CSR16), ioaddr + CSR16);
    outl(0x00011000 | inl(ioaddr + CSR20), ioaddr + CSR20);

  } else if (vendor == PCI_VENDOR_ID_LINKSYS && dev_id == PCI_DEVICE_ID_LC82C115) {
    /* This is MX987x5 init code. It seems to work for the LNE100TX 
       but should be replaced when we figure out the right way
       to do this initialization.
    */
    csr6 = 0x01880200;
    outl(0x0B3C0000 | inl(ioaddr + CSR16), ioaddr + CSR16);
    outl(0x00011000 | inl(ioaddr + CSR20), ioaddr + CSR20);

  } else if (vendor == PCI_VENDOR_ID_LINKSYS && dev_id == PCI_DEVICE_ID_DEC_TULIP) {

    csr6 = 0x814C0000;
    outl(0x00000001, ioaddr + CSR15);

#if 0
  } else if (vendor == PCI_VENDOR_ID_DEC && dev_id == PCI_DEVICE_ID_DEC_21142) {
     /* check SROM for evidence of an MII interface */
     /* get Controller_0 Info Leaf Offset from SROM - assume already in ee_data */
     int offset = ee_data [27] + (ee_data [28] << 8);

     /* check offset range and if we have an extended type 3 Info Block */
     if ((offset >= 30) && (offset < 120) && (ee_data [offset + 3] > 128) &&
	(ee_data [offset + 4] == 3)) {
	/* must have an MII interface - disable heartbeat, select port etc. */
	csr6 |= (DEC_21142_CSR6_HBD | DEC_21142_CSR6_PS);
	csr6 &= ~(DEC_21142_CSR6_TTM);
     }
#endif

  } else if (vendor == PCI_VENDOR_ID_DEC) {

    int offset;
    switch (tulip_id) 
    {
      case DC21140:
        offset = ee_data [27] + (ee_data [28] << 8);
        outl(ee_data[offset+2] | 0x100, ioaddr + CSR12);
        csr6 = 0x020E0000;
        break;

      case DC21143:
        outl(0x00000001, ioaddr + CSR13);
        outl(0x0003ffff, ioaddr + CSR14);
#ifdef NTULIP_FORCE_BNC
        outl(0x82420200, ioaddr + CSR6);
        outl(0x08af0000, ioaddr + CSR15);
        outl(0x00a50000, ioaddr + CSR15);
        outl(0x00001301, ioaddr + CSR12);
        csr6 = 0x82420200;
#else
        outl(0x83860000, ioaddr + CSR6);
        outl(0x08af0008, ioaddr + CSR15);
        outl(0x00a50008, ioaddr + CSR15);
        outl(0x00001301, ioaddr + CSR12);
        csr6 = 0x83860000;
#endif
        break;

      case DC21142:
	outl(0x82020000, ioaddr + CSR6);
	outl(0x00000000, ioaddr + CSR13);
	outl(0x00000000, ioaddr + CSR14);
	csr6 = 0x820e0000;
	break;
    }
  }
  
  ntulip_wait(2*TICKS_PER_SEC);

  /* Start the chip's Tx to process setup frame. */
  outl(csr6, ioaddr + CSR6);

  /* Start Tx */
  outl(inl(ioaddr + CSR6) | 0x00002000, ioaddr + CSR6);
  /* immediate transmit demand */
  outl(0, ioaddr + CSR1);

  to = currticks() + TX_TIME_OUT;
  while ((txd.status & 0x80000000) && (currticks() < to))
    /* wait */ ;

  if (currticks() >= to) {
    printf ("TX Setup Timeout!\n");
  }

#ifdef NTULIP_DEBUG
  printf("txd.status = %X\n", txd.status);
  printf("ticks = %d\n", currticks() - (to - TX_TIME_OUT));
#endif

  /* enable RX */
  outl(inl(ioaddr + CSR6) | 0x00000002, ioaddr + CSR6);
  /* immediate poll demand */
  outl(0, ioaddr + CSR2);

}


/*********************************************************************/
/* eth_transmit - Transmit a frame                                   */
/*********************************************************************/
static void ntulip_transmit(struct nic *card, const char *d, unsigned int t,
                             unsigned int s, const char *p)
{
  unsigned long to;

  whereami("ntulip_transmit\n");

  /* Stop Tx */
  outl(inl(ioaddr + CSR6) & ~0x00002000, ioaddr + CSR6);

  /* setup ethernet header */
  memcpy(ehdr, d, ETH_ALEN);
  memcpy(&ehdr[ETH_ALEN], card->node_addr, ETH_ALEN);
  ehdr[ETH_ALEN*2] = (t >> 8) & 0xFF;
  ehdr[ETH_ALEN*2+1] = t & 0xFF;

  /* setup the transmit descriptor */
  txd.buf1addr = &ehdr[0];        /* ethernet header */
  txd.buf1sz   = ETH_HLEN;
  txd.buf2addr = (unsigned char*)p; /* packet to transmit */
  txd.buf2sz   = s;
  txd.control  = 0x00000188;      /* LS+FS+TER */
  txd.status   = 0x80000000;      /* give it the device */

  /* Point to transmit descriptor */
  outl((unsigned long)&txd, ioaddr + CSR4);

  /* Start Tx */
  outl(inl(ioaddr + CSR6) |  0x00002000, ioaddr + CSR6);

  /* immediate transmit demand */
  outl(0, ioaddr + CSR1);

  to = currticks() + TX_TIME_OUT;
  while ((txd.status & 0x80000000) && (currticks() < to))
    /* wait */ ;

  if (currticks() >= to) {
    printf ("TX Timeout!\n");
  }
}

/*********************************************************************/
/* eth_poll - Wait for a frame                                       */
/*********************************************************************/
static int ntulip_poll(struct nic *card)
{

  whereami("ntulip_poll\n");
  if (rxd[rxd_tail].status & 0x80000000)
    return 0;

  whereami("ntulip_poll got one\n");

  card->packetlen = (rxd[rxd_tail].status & 0x3FFF0000) >> 16;

  if (rxd[rxd_tail].status & 0x00008000) {
      /* CRC error */
      rxd[rxd_tail].status = 0x80000000;
      rxd_tail++;
      if (rxd_tail == NRXD) rxd_tail = 0;
      return 0;
  }

  /* copy packet to working buffer */
  /* XXX - this copy could be avoided with a little more work
     but for now we are content with it because the optimised
     memcpy is quite fast */

  memcpy(card->packet, &rxb[rxd_tail][0], card->packetlen);

  /* return the descriptor and buffer to receive ring */
  rxd[rxd_tail].status = 0x80000000;
  rxd_tail++;
  if (rxd_tail == NRXD) rxd_tail = 0;

  return 1;
}

/*********************************************************************/
/* eth_disable - Disable the interface                               */
/*********************************************************************/
static void ntulip_disable(struct nic *card)
{
  whereami("ntulip_disable\n");

  /* The other Etherboot drivers don't seem to do anything here,
     so for now, we will not either */
  
  /* disable interrupts */
  outl(0x00000000, ioaddr + CSR7);

  /* Stop the chip's Tx and Rx processes. */
  outl(inl(ioaddr + CSR6) & ~0x00002002, ioaddr + CSR6);

  /* Clear the missed-packet counter. */
  (volatile unsigned long)inl(ioaddr + CSR8);
}

/*********************************************************************/
/* eth_probe - Look for an adapter                                   */
/*********************************************************************/
struct nic *ntulip_probe(struct nic *card, unsigned short *io_addrs,
                          struct pci_device *pci)
{               
  int i;
  unsigned char chip_rev;

  whereami("ntulip_probe\n");

  if (io_addrs == 0 || *io_addrs == 0)
    return 0;

  vendor  = pci->vendor;
  dev_id  = pci->dev_id;
  ioaddr  = *io_addrs;

  /* read chip revision */
  pcibios_read_config_byte(pci->bus, pci->devfn, 0x08, &chip_rev);

  /* wakeup chip */
  pcibios_write_config_dword(pci->bus, pci->devfn, 0x40, 0x00000000);

  /* Stop the chip's Tx and Rx processes. */
  outl(inl(ioaddr + CSR6) & ~0x00002002, ioaddr + CSR6);

  /* Clear the missed-packet counter. */
  (volatile unsigned long)inl(ioaddr + CSR8);

  /* Get MAC Address */

  /* Hardware Address retrieval method for LC82C168 */
  if (vendor == PCI_VENDOR_ID_LINKSYS && dev_id == PCI_DEVICE_ID_DEC_TULIP) {
    for (i = 0; i < 3; i++) {
      int value, boguscnt = 100000;
      outl(0x600 | i, ioaddr + 0x98);
      do
        value = inl(ioaddr + CSR9);
      while (value < 0  && --boguscnt > 0);
      card->node_addr[i*2]     = (u8)((value >> 8) & 0xFF);
      card->node_addr[i*2 + 1] = (u8)( value       & 0xFF);
    }
  } else {
    /* read EEPROM data */
    for (i = 0; i < sizeof(ee_data)/2; i++)
      ((unsigned short *)ee_data)[i] =
        le16_to_cpu(read_eeprom(i, EEPROM_ADDRLEN));

    /* extract MAC address from EEPROM buffer */
    for (i=0; i<6; i++)
      card->node_addr[i] = ee_data[20+i];
  }

  printf("Tulip %b:%b:%b:%b:%b:%b rev 0x%b at ioaddr 0x%x\n",
    card->node_addr[0],card->node_addr[1],card->node_addr[2],card->node_addr[3],
    card->node_addr[4],card->node_addr[5],chip_rev,ioaddr);

  /* initialize device */
  ntulip_reset(card);

  card->reset    = ntulip_reset;
  card->poll     = ntulip_poll;
  card->transmit = ntulip_transmit;
  card->disable  = ntulip_disable;

  return card;
}
