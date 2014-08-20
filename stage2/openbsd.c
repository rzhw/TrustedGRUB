/*
 * openbsd.c
 *
 * Hack to make GRUB boot OpenBSD from the network
 * Booting from the hard drive will require a slightly
 * different boot vector
 */


/**************************
 * From sys/stand/boot/bootarg.h
 */
#define	BOOTARG_APIVER	(BAPIV_VECTOR|BAPIV_ENV|BAPIV_BMEMMAP)
#define	BAPIV_ANCIENT	0x00000000	/* MD old i386 bootblocks */
#define	BAPIV_VARS	0x00000001	/* MD structure w/ add info passed */
#define	BAPIV_VECTOR	0x00000002	/* MI vector of MD structures passed */
#define	BAPIV_ENV	0x00000004	/* MI environment vars vector */
#define	BAPIV_BMEMMAP	0x00000008	/* MI memory map passed is in bytes */
typedef struct _boot_args {
	int ba_type;
	long ba_size;
	struct _boot_args *_dummy;
//	int ba_arg[1];
} bootarg_t;
#define	BOOTARG_END	-1


/**************************
 * From sys/arch/i386/include/biosvar.h
 */

/*
 * BIOS memory info
 */
#define	BOOTARG_MEMMAP 0
#define	BIOS_MAP_END	0x00	/* End of array XXX - special */
#define	BIOS_MAP_FREE	0x01	/* Usable memory */
#define	BIOS_MAP_RES	0x02	/* Reserved memory */
#define	BIOS_MAP_ACPI	0x03	/* ACPI Reclaim memory */
#define	BIOS_MAP_NVS	0x04	/* ACPI NVS memory */
typedef struct _bios_memmap {
    unsigned long long addr;	/* Beginning of block */
    unsigned long long size;	/* Size of block */
    unsigned long type;		/* Type of block */
} bios_memmap_t;

/*
 * console info
 * tty00 is major=8/minor=0
 */
#define BOOTARG_CONSDEV 5
typedef struct _bios_consdev {
        int     consdev;
        int     conspeed;
} bios_consdev_t;

/*
 * diskless info
 */
/**************************
 * my bad hack
 */

/*
 * our custom block
 */
struct bootinfo_s {
    bootarg_t memarg;
    bios_memmap_t memmap[3];
    bootarg_t ttyarg;
    bios_consdev_t ttyinfo;
    bootarg_t endarg;
};

static void xxx_memcpy(long *dst, long *src, int len) {
    while(len>0) {
	*dst++ = *src++;
	len -= 4;
    }
}

#include "term.h"
extern long boot_serial_unit;
extern long boot_serial_speed;

/*
 *  All "*_boot" commands depend on the images being loaded into memory
 *  correctly, the variables in this file being set up correctly, and
 *  the root partition being set in the 'saved_drive' and 'saved_partition'
 *  variables.
 */
void
openbsd_boot (kernel_t type, int bootdev, char *arg)
{
  char *str;
  int clval = 0;
  int i;
  struct bootinfo_s bv;

  /* call entry point */
  unsigned long end_mark;

#ifdef GRUB_UTIL
  entry_addr = (entry_func) bsd_boot_entry;
#else
  stop_floppy ();
#endif

  while(*(++arg) && *arg != ' ');
  str = arg;
  while(*str) {
      if(*str == '-') {
	  while (*str && *str != ' ') {
	      if (*str == 'C')
		  clval |= RB_CDROM;
	      if (*str == 'a')
		  clval |= RB_ASKNAME;
	      if (*str == 'b')
		  clval |= RB_HALT;
	      if (*str == 'c')
		  clval |= RB_CONFIG;
	      if (*str == 'd')
		  clval |= RB_KDB;
	      if (*str == 'h')
		  clval |= RB_SERIAL;
	      if (*str == 'r')
		  clval |= RB_DFLTROOT;
	      if (*str == 's')
		  clval |= RB_SINGLE;
	      if (*str == 'v')
		  clval |= RB_VERBOSE;
	      str++;
	  }
	  continue;
      }
      str++;
  }
  /*
   *  We now pass the various bootstrap parameters to the loaded
   *  image via the argument list.
   *
   *  This is the unofficial list:
   *
   *  arg0 = '_boothowto' (flags)
   *  arg1 = '_bootdev' boot device - not used
   *  arg2 = '_bootapiver' (boot capabilities/version flags)
   *  arg3 = '_esym' - not used
   *  arg4 = '_extmem' - extended memory - not used
   *  arg5 = '_cnvmem' - standard memory - not used
   *  arg6 = '_bootargc' - size of the boot argument vector (bytes)
   *  arg7 = '_bootargv' - the boot argument vector
   */

  /*
   * I don't know what this stuff is, so I keep it
   */
  if (mbi.flags & MB_INFO_AOUT_SYMS)
      end_mark = (mbi.syms.a.addr + 4
	      + mbi.syms.a.tabsize + mbi.syms.a.strsize);
  else {
      /* FIXME: it should be mbi.syms.e.size.  */
      end_mark = mbi.syms.e.addr;
  }
  /* fillup parameter block */
  bv.memarg.ba_type = BOOTARG_MEMMAP;
  bv.memarg.ba_size = sizeof(bv.memarg) + sizeof(bv.memmap);
  bv.memmap[0].addr = 0;
  bv.memmap[0].size = mbi.mem_lower * 1024;
  bv.memmap[0].type = BIOS_MAP_FREE;
  bv.memmap[1].addr = 0x100000;
  bv.memmap[1].size = extended_memory * 1024;
  bv.memmap[1].type = BIOS_MAP_FREE;
  bv.memmap[2].addr = 0;
  bv.memmap[2].size = 0;
  bv.memmap[2].type = BIOS_MAP_END;
  bv.ttyarg.ba_type = BOOTARG_CONSDEV;
  bv.ttyarg.ba_size = sizeof(bv.ttyarg)+sizeof(bv.ttyinfo);
  if(current_term && !grub_strcmp(current_term->name, "serial")) {
    bv.ttyinfo.consdev = 0x800+boot_serial_unit;
    bv.ttyinfo.conspeed = boot_serial_speed;
  } else {
    bv.ttyinfo.consdev = 0;
    bv.ttyinfo.conspeed = 0;
  }
  bv.endarg.ba_type = BOOTARG_END;
  bv.endarg.ba_size = sizeof(bv.endarg);

  // copy args at 0x2000, and launch the thing with 8 args
  xxx_memcpy((long *)0x2000, (long *)&bv, sizeof(bv));  
  (*(void (*)(int, int, int, int, int, int, int, int))entry_addr)
      (clval, bootdev, BOOTARG_APIVER, end_mark,
	  extended_memory, mbi.mem_lower, sizeof(bv), 0x2000);
}
