/* boot.c - load and bootstrap a kernel */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999,2000,2001,2002,2003,2004  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "shared.h"
#include "freebsd.h"
#include "imgact_aout.h"
#include "i386-elf.h"

static int cur_addr;
entry_func entry_addr;
static struct mod_list mll[99];
static int linux_mem_size;

/* Begin TCG extension */

    /* update_pcr is an internal function to extend the TPM PCR with
    a given hash-value. The parameters are the PCR-Register (between
    8 and 15) and the SHA1-hash-value (in 5 unsigned long variables) */

int update_pcr(unsigned char pcr, unsigned long *hash_result)
{
    int i;
    if ((pcr < 8) || (pcr > 15))
    {
	printf("\ntGRUB: Wrong PCR register, allowed values are 8...15\n");
	return -1;
    }
    // Updating the ASM-Code with the correct parameters:
    
    // Input Parameter Block
    
    ((char*)TCG_BUFFER_ADDR)[0xF012] = 0x2a;
    ((char*)TCG_BUFFER_ADDR)[0xF013] = 0x0; // 0x002a ibl +8
    ((char*)TCG_BUFFER_ADDR)[0xF014] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF015] = 0x0; // 0x0000
    ((char*)TCG_BUFFER_ADDR)[0xF016] = 0x22;
    ((char*)TCG_BUFFER_ADDR)[0xF017] = 0x0; // 0x0022 obl +4
    ((char*)TCG_BUFFER_ADDR)[0xF018] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF019] = 0x0; // 0x0000
    
    // TCG Command

    ((char*)TCG_BUFFER_ADDR)[0xF01A] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF01B] = 0xc1; // 0x00c1 tag
    ((char*)TCG_BUFFER_ADDR)[0xF01C] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF01D] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF01E] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF01F] = 0x22; // 0x00000022 length
    ((char*)TCG_BUFFER_ADDR)[0xF020] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF021] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF022] = 0x0;
    ((char*)TCG_BUFFER_ADDR)[0xF023] = 0x14; // 0x00000014 command ordinal

#ifdef DEBUG
    printf("\ntGRUB: Updating PCR-Register %d",pcr);
#endif
    ((char*)TCG_BUFFER_ADDR)[0xF024] = 0;
    ((char*)TCG_BUFFER_ADDR)[0xF025] = 0;
    ((char*)TCG_BUFFER_ADDR)[0xF026] = 0;
    ((char*)TCG_BUFFER_ADDR)[0xF027] = pcr;

    // Copying the hash-result into the correct memory position for the TPM-call
    for (i=0; i<5; i++)
    {
	((char*)TCG_BUFFER_ADDR)[0xF028+0+(4*i)] = ((hash_result[i] >> 24) & 0xff);
	((char*)TCG_BUFFER_ADDR)[0xF028+1+(4*i)] = ((hash_result[i] >> 16) & 0xff);
	((char*)TCG_BUFFER_ADDR)[0xF028+2+(4*i)] = ((hash_result[i] >>  8) & 0xff);
	((char*)TCG_BUFFER_ADDR)[0xF028+3+(4*i)] = ((hash_result[i]      ) & 0xff);
    }
#ifdef DEBUG
    printf("\ntGRUB: Input Parameter Block: ");
    for (i=0; i<8; i++)
	printf("%x ",((char*)TCG_BUFFER_ADDR)[0xF012+i]&0xff);
    printf("\ntGRUB: To TPM: ");
    for (i=8; i<42; i++)
	printf("%x ",((char*)TCG_BUFFER_ADDR)[0xF012+i]&0xff);
#endif

    tcg_hash_extend_pcr();

#ifdef DEBUG
    printf("\ntGRUB: Results of BIOS call: %x", give_tpm_answer());
    printf("\ntGRUB: Output Parameter Block: ");
    for (i=0; i<4; i++)
	printf("%x ",((char*)TCG_BUFFER_ADDR)[0xF012+i]&0xff);
    printf("\ntGRUB: From TPM: ");
    for (i=4; i<38; i++)
	printf("%x ",((char*)TCG_BUFFER_ADDR)[0xF012+i]&0xff);
    printf("\nPress any key to continue\n");
    getkey();
#endif
    return 0;
}

    /* The following function loads the file which is referenced by the variable 
    checkfile and extracts all data out of it. These data have the format SHA1 
    hash value followed by the file which belongs to it. The task of this routine 
    is to verify if the hash value is still as is should be, i.e., the integrity of 
    arbitrary files can be checked. All hash values generated here are written into 
    PCR 13. This command is also available via the command line*/

int
load_checkfile (char *checkfile)
{

    int MY_BUFFER_SIZE = 2 * TCG_BUFFER_SIZE;
    int i;
    int result;
    int general_error;
    int max_length;
    int curr_length;
    int file_ok = 0;
    unsigned long hash_result[5];
    unsigned char sha1_hash_buf_string[41];
    unsigned char hash_input_buf[41];
    unsigned char file_name_buf[1024];
    unsigned char check_file_data[MY_BUFFER_SIZE];
#ifdef SHOW_SHA1  
    printf("\ntGRUB: Opening checkfile %s\n",checkfile);
#endif
    /* Disabling loadage SHA1-measurement. This is because we call our internal
    SHA1-function and do not need to measure via grub_read */
    if (perform_sha1)
    {
	old_perform_sha1_value = perform_sha1;
	perform_sha1 = 0;
    }

  /* Try to open checkfile. If it fails, GRUB has to be stopped */
    result = grub_open (checkfile);
    if (!result)
    {
	printf("\ntGRUB error: Could not open checkfile (error code %d)\n",result);
	return -1;
    }
  
  if (filemax > MY_BUFFER_SIZE)
    {
      printf("\ntGRUB error: File list too large\n");
      grub_close ();
      return -1;
    }

  /* Extracts all data out of the checkfile and closes it */
  max_length = filemax;
  grub_read (check_file_data, max_length);
  grub_close ();

  curr_length = 0;
  general_error = 0;
  file_ok = 0;

  /* All entries within the checkfile are regarded subsequently */
  while (curr_length < max_length)
    {
      /* Extracts the hash value (the reference value). In correct case, its length has to be 40 bytes (2 bytes for every character, thus representing a 160 bit SHA1 value) */
      for (i=0; i<40; i++)
		{
		  hash_input_buf[i] = check_file_data[curr_length];
		  curr_length++;
		}

      hash_input_buf[40] = '\0';

      /* Is the entry in correct format? (i.e., a hash value, followed by a single white space character and a file name) */
      if (check_file_data[curr_length] != ' ')
		{
		  printf("\ntGRUB error: File list not in correct format\n"
		  "File format has to be: \n"
		  "<20 Byte hash> <single white space character>"
		  "<filename (in GRUB format)> <new line character>\n"
		  ",e.g.:aabbccddeeff0011223344556677889900aabbcc (hd0,0)/boot/grub/file\n"); 
		  return 1;
		}

      curr_length++;
      i = 0;

      /* Reads out the name of the file that has to be checked */
      while (1)
		{
		  /* Every entry in the checkfile has to be terminated by a new line character */
		  if (check_file_data[curr_length] == '\n')
			{
			  curr_length++;
			  file_name_buf[i] = '\0';
			  break;
			}
	  
		  file_name_buf[i] = check_file_data[curr_length];
		  i++;
		  curr_length++;

		  /* The length of the file name is limited to 1024 bytes. There is no general reason for choosing this value but longer file names do not appear in practice and, without limitation, a buffer overflow may be possible */
		  if (i > 1023)
			{
			  printf("\ntGRUB error: File name too long\n");
			  return -1;
			}
		}

    // Calculate the SHA1-value and store it into hash_result.
    if (calculate_sha1(file_name_buf, hash_result,0))
    {
        printf("\ntGRUB error during SHA1-calculation. Is your checkfile-syntax OK?\n");
        return -1;
    }
    
    // convert the hash result from our sha1-function into a char-buffer
    for (i=0; i<5; i++)
    {
	sprintf(&sha1_hash_buf_string[8*i],"%x%x%x%x%x%x%x%x",
	((hash_result[i]>>28)&0x0f),
	((hash_result[i]>>24)&0x0f),
	((hash_result[i]>>20)&0x0f),
	((hash_result[i]>>16)&0x0f),
	((hash_result[i]>>12)&0x0f),
	((hash_result[i]>> 8)&0x0f),
	((hash_result[i]>> 4)&0x0f),
	((hash_result[i]    )&0x0f));
    }
    sha1_hash_buf_string[40] = 0;

#ifdef SHOW_SHA1
    if ((xy++)>17){ cls(); xy=0; }
    gotoxy(0,(xy&0xff));
    printf("Chkf:[%s]->PCR[%d] ",sha1_hash_buf_string,PCR_CHECKFILE);
    for (i=0; i<22; i++)
    {
	if (file_name_buf[i])
	    printf("%c",file_name_buf[i]);
	else
	    break;
    }
    printf("\n");
#endif

    /* Compares the actual hash value with the reference value */
    if (strcmp(sha1_hash_buf_string, hash_input_buf))
    {
	file_ok = 1;
	general_error++;
    }
    
    if (file_ok)
    {
#ifdef SHOW_SHA1
	gotoxy(12,(xy&0xff));
	printf(" -= Integrity Error! =- ");
#endif
#ifndef SHOW_SHA1
	printf("\ntGRUB: Verifying %s -> Integrity Error!",file_name_buf);
#endif
	file_ok = 0;	
    }
    else
    {
	if (!tpm_present())
	{
	    // Update the PCR register 13 with the calculated SHA1-value
	    if (update_pcr(PCR_CHECKFILE,hash_result))
		    printf("\ntGRUB: Error during PCR extension\n");
	}
    }
    } // end while (curr_length < max_length)
  
  /* If not all computed hash values have been equal to the referenced ones, the user has to decide whether he wants to continue booting or not. The integrity of the checkfile itself can not be measured here, it has to be verfied later, together with all other data which have been measured by tGRUB */
    if (general_error)
    {
      printf("\ntGRUB: Data integrity not guaranteed, %d problem(s) occured\n",general_error);
      printf("\ntGRUB: Press ESC to stop booting or any other key to continue ...\n");
      
      if (ASCII_CHAR (getkey ()) == '\e')
		{
		  grub_halt (0);
		}
    }
    if (old_perform_sha1_value)
	perform_sha1 = old_perform_sha1_value;
    return 0;
}
/* End TCG extension */

/*
 *  The next two functions, 'load_image' and 'load_module', are the building
 *  blocks of the multiboot loader component.  They handle essentially all
 *  of the gory details of loading in a bootable image and the modules.
 */

kernel_t
load_image (char *kernel, char *arg, kernel_t suggested_type,
	    unsigned long load_flags)
{
  int len, i, exec_type = 0, align_4k = 1;
  entry_func real_entry_addr = 0;
  kernel_t type = KERNEL_TYPE_NONE;
  unsigned long flags = 0, text_len = 0, data_len = 0, bss_len = 0;
  char *str = 0, *str2 = 0;
  struct linux_kernel_header *lh;
  union
    {
      struct multiboot_header *mb;
      struct exec *aout;
      Elf32_Ehdr *elf;
    }
  pu;
  /* presuming that MULTIBOOT_SEARCH is large enough to encompass an
     executable header */
  unsigned char buffer[MULTIBOOT_SEARCH];
/* BEGIN TCG EXTENSION */
    measure_kernel=1;
/* END TCG EXTENSION */

  /* sets the header pointer to point to the beginning of the
     buffer by default */
  pu.aout = (struct exec *) buffer;

  if (!grub_open (kernel))
    return KERNEL_TYPE_NONE;

  if (!(len = grub_read (buffer, MULTIBOOT_SEARCH)) || len < 32)
    {
      grub_close ();
      
      if (!errnum)
	errnum = ERR_EXEC_FORMAT;

      return KERNEL_TYPE_NONE;
    }

  for (i = 0; i < len; i++)
    {
      if (MULTIBOOT_FOUND ((int) (buffer + i), len - i))
	{
	  flags = ((struct multiboot_header *) (buffer + i))->flags;
	  if (flags & MULTIBOOT_UNSUPPORTED)
	    {
	      grub_close ();
	      errnum = ERR_BOOT_FEATURES;
	      return KERNEL_TYPE_NONE;
	    }
	  type = KERNEL_TYPE_MULTIBOOT;
	  str2 = "Multiboot";

	/* Check to see if the kernel wants a video mode
	*/
	if ( (flags & MULTIBOOT_VIDEO_MODE) != 0 )
	{
		struct multiboot_header* mbh = (struct multiboot_header *) (buffer + i);

		if (mbh->mode_type == 0) {
			match_vbe(mbh->width, mbh->height, mbh->depth, 1);
		} else {
			// EGA mode... umm
		}
	}

	  break;
	}
    }

  /* Use BUFFER as a linux kernel header, if the image is Linux zImage
     or bzImage.  */
  lh = (struct linux_kernel_header *) buffer;
  
  /* ELF loading supported if multiboot, FreeBSD, OpenBSD and NetBSD.  */
  if ((type == KERNEL_TYPE_MULTIBOOT
       || pu.elf->e_ident[EI_OSABI] == ELFOSABI_FREEBSD
       || grub_strcmp (pu.elf->e_ident + EI_BRAND, "FreeBSD") == 0
       || suggested_type == 0
       || suggested_type == KERNEL_TYPE_OPENBSD
       || suggested_type == KERNEL_TYPE_NETBSD)
      && len > sizeof (Elf32_Ehdr)
      && BOOTABLE_I386_ELF ((*((Elf32_Ehdr *) buffer))))
    {
      if (type == KERNEL_TYPE_MULTIBOOT)
	entry_addr = (entry_func) pu.elf->e_entry;
      else
	entry_addr = (entry_func) (pu.elf->e_entry & 0xFFFFFF);

      if (entry_addr < (entry_func) 0x100000)
	errnum = ERR_BELOW_1MB;

      /* don't want to deal with ELF program header at some random
         place in the file -- this generally won't happen */
      if (pu.elf->e_phoff == 0 || pu.elf->e_phnum == 0
	  || ((pu.elf->e_phoff + (pu.elf->e_phentsize * pu.elf->e_phnum))
	      >= len))
	errnum = ERR_EXEC_FORMAT;
      str = "elf";

      if (type == KERNEL_TYPE_NONE)
	{
	  /* At the moment, there is no way to identify a NetBSD ELF
	     kernel, so rely on the suggested type by the user.  */
	  if (suggested_type == KERNEL_TYPE_NETBSD)
	    {
	      str2 = "NetBSD";
	      type = suggested_type;
	    }
	  else if (suggested_type == KERNEL_TYPE_FREEBSD ||
              pu.elf->e_ident[EI_OSABI] == ELFOSABI_FREEBSD)
	    {
	      str2 = "FreeBSD";
	      type = KERNEL_TYPE_FREEBSD;
	    }
	  else
	    {
	      str2 = "OpenBSD";
	      type = KERNEL_TYPE_OPENBSD;
	    }
	}
    }
  else if (flags & MULTIBOOT_AOUT_KLUDGE)
    {
      pu.mb = (struct multiboot_header *) (buffer + i);
      entry_addr = (entry_func) pu.mb->entry_addr;
      cur_addr = pu.mb->load_addr;
      /* first offset into file */
      grub_seek (i - (pu.mb->header_addr - cur_addr));

      /* If the load end address is zero, load the whole contents.  */
      if (! pu.mb->load_end_addr)
	pu.mb->load_end_addr = cur_addr + filemax;
      
      text_len = pu.mb->load_end_addr - cur_addr;
      data_len = 0;

      /* If the bss end address is zero, assume that there is no bss area.  */
      if (! pu.mb->bss_end_addr)
	pu.mb->bss_end_addr = pu.mb->load_end_addr;
      
      bss_len = pu.mb->bss_end_addr - pu.mb->load_end_addr;

      if (pu.mb->header_addr < pu.mb->load_addr
	  || pu.mb->load_end_addr <= pu.mb->load_addr
	  || pu.mb->bss_end_addr < pu.mb->load_end_addr
	  || (pu.mb->header_addr - pu.mb->load_addr) > i)
	errnum = ERR_EXEC_FORMAT;

      if (cur_addr < 0x100000)
	errnum = ERR_BELOW_1MB;

      pu.aout = (struct exec *) buffer;
      exec_type = 2;
      str = "kludge";
    }
  else if (len > sizeof (struct exec) && !N_BADMAG ((*(pu.aout))))
    {
      entry_addr = (entry_func) pu.aout->a_entry;

      if (type == KERNEL_TYPE_NONE)
	{
	  /*
	   *  If it doesn't have a Multiboot header, then presume
	   *  it is either a FreeBSD or NetBSD executable.  If so,
	   *  then use a magic number of normal ordering, ZMAGIC to
	   *  determine if it is FreeBSD.
	   *
	   *  This is all because freebsd and netbsd seem to require
	   *  masking out some address bits...  differently for each
	   *  one...  plus of course we need to know which booting
	   *  method to use.
	   */
	  entry_addr = (entry_func) ((int) entry_addr & 0xFFFFFF);
	  
	  if (buffer[0] == 0xb && buffer[1] == 1)
	    {
	      type = KERNEL_TYPE_FREEBSD;
	      cur_addr = (int) entry_addr;
	      str2 = "FreeBSD";
	    }
	  else
	    {
	      type = KERNEL_TYPE_OPENBSD;
	      cur_addr = (int) entry_addr & 0xF00000;
	      if (N_GETMAGIC ((*(pu.aout))) != NMAGIC)
		align_4k = 0;
	      str2 = "OpenBSD";
	    }
	}

      /* first offset into file */
      grub_seek (N_TXTOFF (*(pu.aout)));
      text_len = pu.aout->a_text;
      data_len = pu.aout->a_data;
      bss_len = pu.aout->a_bss;

      if (cur_addr < 0x100000)
	errnum = ERR_BELOW_1MB;

      exec_type = 1;
      str = "a.out";
    }
  else if (lh->boot_flag == BOOTSEC_SIGNATURE
	   && lh->setup_sects <= LINUX_MAX_SETUP_SECTS)
    {
      int big_linux = 0;
      int setup_sects = lh->setup_sects;

      if (lh->header == LINUX_MAGIC_SIGNATURE && lh->version >= 0x0200)
	{
	  big_linux = (lh->loadflags & LINUX_FLAG_BIG_KERNEL);
	  lh->type_of_loader = LINUX_BOOT_LOADER_TYPE;

	  /* Put the real mode part at as a high location as possible.  */
	  linux_data_real_addr
	    = (char *) ((mbi.mem_lower << 10) - LINUX_SETUP_MOVE_SIZE);
	  /* But it must not exceed the traditional area.  */
	  if (linux_data_real_addr > (char *) LINUX_OLD_REAL_MODE_ADDR)
	    linux_data_real_addr = (char *) LINUX_OLD_REAL_MODE_ADDR;

	  if (lh->version >= 0x0201)
	    {
	      lh->heap_end_ptr = LINUX_HEAP_END_OFFSET;
	      lh->loadflags |= LINUX_FLAG_CAN_USE_HEAP;
	    }

	  if (lh->version >= 0x0202)
	    lh->cmd_line_ptr = linux_data_real_addr + LINUX_CL_0202_PRM_OFFSET;
	  else
	    {
	      lh->cl_magic = LINUX_CL_MAGIC;
	      lh->cl_offset = LINUX_CL_OFFSET;
	      lh->setup_move_size = LINUX_SETUP_MOVE_SIZE;
	    }
	}
      else
	{
	  /* Your kernel is quite old...  */
	  lh->cl_magic = LINUX_CL_MAGIC;
	  lh->cl_offset = LINUX_CL_OFFSET;
	  
	  setup_sects = LINUX_DEFAULT_SETUP_SECTS;

	  linux_data_real_addr = (char *) LINUX_OLD_REAL_MODE_ADDR;
	}
      
      /* If SETUP_SECTS is not set, set it to the default (4).  */
      if (! setup_sects)
	setup_sects = LINUX_DEFAULT_SETUP_SECTS;

      data_len = setup_sects << 9;
      text_len = filemax - data_len - SECTOR_SIZE;

      linux_data_tmp_addr = (char *) LINUX_BZIMAGE_ADDR + text_len;
      
      if (! big_linux
	  && text_len > linux_data_real_addr - (char *) LINUX_ZIMAGE_ADDR)
	{
#ifdef DEBUG
	  grub_printf (" linux 'zImage' kernel too big, try 'make bzImage'\n");
#endif
	  errnum = ERR_WONT_FIT;
	}
      else if (linux_data_real_addr + LINUX_SETUP_MOVE_SIZE
	       > RAW_ADDR ((char *) (mbi.mem_lower << 10)))
	errnum = ERR_WONT_FIT;
      else
	{
#ifdef DEBUG
	  grub_printf ("   [Linux-%s, setup=0x%x, size=0x%x]\n",
		       (big_linux ? "bzImage" : "zImage"), data_len, text_len);
#endif

	  /* Video mode selection support. What a mess!  */
	  /* NOTE: Even the word "mess" is not still enough to
	     represent how wrong and bad the Linux video support is,
	     but I don't want to hear complaints from Linux fanatics
	     any more. -okuji  */
	  {
	    char *vga;
	
	    /* Find the substring "vga=".  */
	    vga = grub_strstr (arg, "vga=");
	    if (vga)
	      {
		char *value = vga + 4;
		int vid_mode;
	    
		/* Handle special strings.  */
		if (substring ("normal", value) < 1)
		  vid_mode = LINUX_VID_MODE_NORMAL;
		else if (substring ("ext", value) < 1)
		  vid_mode = LINUX_VID_MODE_EXTENDED;
		else if (substring ("ask", value) < 1)
		  vid_mode = LINUX_VID_MODE_ASK;
		else if (safe_parse_maxint (&value, &vid_mode))
		  ;
		else
		  {
		    /* ERRNUM is already set inside the function
		       safe_parse_maxint.  */
		    grub_close ();
		    return KERNEL_TYPE_NONE;
		  }
	    
		lh->vid_mode = vid_mode;
	      }
	  }

	  /* Check the mem= option to limit memory used for initrd.  */
	  {
	    char *mem;
	
	    mem = grub_strstr (arg, "mem=");
	    if (mem)
	      {
		char *value = mem + 4;
	    
		safe_parse_maxint (&value, &linux_mem_size);
		switch (errnum)
		  {
		  case ERR_NUMBER_OVERFLOW:
		    /* If an overflow occurs, use the maximum address for
		       initrd instead. This is good, because MAXINT is
		       greater than LINUX_INITRD_MAX_ADDRESS.  */
		    linux_mem_size = LINUX_INITRD_MAX_ADDRESS;
		    errnum = ERR_NONE;
		    break;
		
		  case ERR_NONE:
		    {
		      int shift = 0;
		  
		      switch (grub_tolower (*value))
			{
			case 'g':
			  shift += 10;
			case 'm':
			  shift += 10;
			case 'k':
			  shift += 10;
			default:
			  break;
			}
		  
		      /* Check an overflow.  */
		      if (linux_mem_size > (MAXINT >> shift))
			linux_mem_size = LINUX_INITRD_MAX_ADDRESS;
		      else
			linux_mem_size <<= shift;
		    }
		    break;
		
		  default:
		    linux_mem_size = 0;
		    errnum = ERR_NONE;
		    break;
		  }
	      }
	    else
	      linux_mem_size = 0;
	  }
      
	  /* It is possible that DATA_LEN + SECTOR_SIZE is greater than
	     MULTIBOOT_SEARCH, so the data may have been read partially.  */
	  if (data_len + SECTOR_SIZE <= MULTIBOOT_SEARCH)
	    grub_memmove (linux_data_tmp_addr, buffer,
			  data_len + SECTOR_SIZE);
	  else
	    {
	      grub_memmove (linux_data_tmp_addr, buffer, MULTIBOOT_SEARCH);
	      grub_read (linux_data_tmp_addr + MULTIBOOT_SEARCH,
			 data_len + SECTOR_SIZE - MULTIBOOT_SEARCH);
	    }
	  
	  if (lh->header != LINUX_MAGIC_SIGNATURE ||
	      lh->version < 0x0200)
	    /* Clear the heap space.  */
	    grub_memset (linux_data_tmp_addr + ((setup_sects + 1) << 9),
			 0,
			 (64 - setup_sects - 1) << 9);
      
	  /* Copy command-line plus memory hack to staging area.
	     NOTE: Linux has a bug that it doesn't handle multiple spaces
	     between two options and a space after a "mem=" option isn't
	     removed correctly so the arguments to init could be like
	     {"init", "", "", NULL}. This affects some not-very-clever
	     shells. Thus, the code below does a trick to avoid the bug.
	     That is, copy "mem=XXX" to the end of the command-line, and
	     avoid to copy spaces unnecessarily. Hell.  */
	  {
	    char *src = skip_to (0, arg);
	    char *dest = linux_data_tmp_addr + LINUX_CL_OFFSET;
	
	    while (dest < linux_data_tmp_addr + LINUX_CL_END_OFFSET && *src)
	      *(dest++) = *(src++);
	
	    {
	    char *src = skip_to (0, arg);
	    char *dest = linux_data_tmp_addr + LINUX_CL_0202_PRM_OFFSET;
	
	    while (dest < linux_data_tmp_addr + LINUX_CL_0202_PRM_END_OFFSET && *src)
	      *(dest++) = *(src++);
	    *dest = 0;
	    }

	    /* Old Linux kernels have problems determining the amount of
	       the available memory.  To work around this problem, we add
	       the "mem" option to the kernel command line.  This has its
	       own drawbacks because newer kernels can determine the
	       memory map more accurately.  Boot protocol 2.03, which
	       appeared in Linux 2.4.18, provides a pointer to the kernel
	       version string, so we could check it.  But since kernel
	       2.4.18 and newer are known to detect memory reliably, boot
	       protocol 2.03 already implies that the kernel is new
	       enough.  The "mem" option is added if neither of the
	       following conditions is met:
	       1) The "mem" option is already present.
	       2) The "kernel" command is used with "--no-mem-option".
	       3) GNU GRUB is configured not to pass the "mem" option.
	       4) The kernel supports boot protocol 2.03 or newer.  */
	    if (! grub_strstr (arg, "mem=")
		&& ! (load_flags & KERNEL_LOAD_NO_MEM_OPTION)
		&& lh->version < 0x0203		/* kernel version < 2.4.18 */
		&& dest + 15 < linux_data_tmp_addr + LINUX_CL_END_OFFSET)
	      {
		*dest++ = ' ';
		*dest++ = 'm';
		*dest++ = 'e';
		*dest++ = 'm';
		*dest++ = '=';
	    
		dest = convert_to_ascii (dest, 'u', (extended_memory + 0x400));
		*dest++ = 'K';
	      }
	
	    *dest = 0;
	  }
      
	  /* offset into file */
	  grub_seek (data_len + SECTOR_SIZE);
      
	  cur_addr = (int) linux_data_tmp_addr + LINUX_SETUP_MOVE_SIZE;
	  grub_read ((char *) LINUX_BZIMAGE_ADDR, text_len);
      
	  if (errnum == ERR_NONE)
	    {
	      grub_close ();
	  
	      /* Sanity check.  */
	      if (suggested_type != KERNEL_TYPE_NONE
		  && ((big_linux && suggested_type != KERNEL_TYPE_BIG_LINUX)
		      || (! big_linux && suggested_type != KERNEL_TYPE_LINUX)))
		{
		  errnum = ERR_EXEC_FORMAT;
		  return KERNEL_TYPE_NONE;
		}
	  
	      /* Ugly hack.  */
	      linux_text_len = text_len;
	  
	      return big_linux ? KERNEL_TYPE_BIG_LINUX : KERNEL_TYPE_LINUX;
	    }
	}
    }
  else				/* no recognizable format */
    errnum = ERR_EXEC_FORMAT;

  /* return if error */
  if (errnum)
    {
      grub_close ();
      return KERNEL_TYPE_NONE;
    }

  /* fill the multiboot info structure */
  mbi.cmdline = (int) arg;
  mbi.mods_count = 0;
  mbi.mods_addr = 0;
  mbi.boot_device = (current_drive << 24) | current_partition;
  mbi.flags &= ~(MB_INFO_MODS | MB_INFO_AOUT_SYMS | MB_INFO_ELF_SHDR);
  mbi.syms.a.tabsize = 0;
  mbi.syms.a.strsize = 0;
  mbi.syms.a.addr = 0;
  mbi.syms.a.pad = 0;

#ifdef DEBUG
  printf ("   [%s-%s", str2, str);
#endif

  str = "";

  if (exec_type)		/* can be loaded like a.out */
    {
      if (flags & MULTIBOOT_AOUT_KLUDGE)
	str = "-and-data";

#ifdef DEBUG
      printf (", loadaddr=0x%x, text%s=0x%x", cur_addr, str, text_len);
#endif
      /* read text, then read data */
      if (grub_read ((char *) RAW_ADDR (cur_addr), text_len) == text_len)
	{
	  cur_addr += text_len;

	  if (!(flags & MULTIBOOT_AOUT_KLUDGE))
	    {
	      /* we have to align to a 4K boundary */
	      if (align_4k)
		cur_addr = (cur_addr + 0xFFF) & 0xFFFFF000;
#ifdef DEBUG
	      else
		printf (", C");

	      printf (", data=0x%x", data_len);
#endif
	      if ((grub_read ((char *) RAW_ADDR (cur_addr), data_len)
		   != data_len)
		  && !errnum)
		errnum = ERR_EXEC_FORMAT;
	      cur_addr += data_len;
	    }

	  if (!errnum)
	    {
	      memset ((char *) RAW_ADDR (cur_addr), 0, bss_len);
	      cur_addr += bss_len;

#ifdef DEBUG
	      printf (", bss=0x%x", bss_len);
#endif
	    }
	}
      else if (!errnum)
	errnum = ERR_EXEC_FORMAT;

      if (!errnum && pu.aout->a_syms
	  && pu.aout->a_syms < (filemax - filepos))
	{
	  int symtab_err, orig_addr = cur_addr;

	  /* we should align to a 4K boundary here for good measure */
	  if (align_4k)
	    cur_addr = (cur_addr + 0xFFF) & 0xFFFFF000;

	  mbi.syms.a.addr = cur_addr;

	  *((int *) RAW_ADDR (cur_addr)) = pu.aout->a_syms;
	  cur_addr += sizeof (int);
	  
#ifdef DEBUG
	  printf (", symtab=0x%x", pu.aout->a_syms);
#endif
	  if (grub_read ((char *) RAW_ADDR (cur_addr), pu.aout->a_syms)
	      == pu.aout->a_syms)
	    {
	      cur_addr += pu.aout->a_syms;
	      mbi.syms.a.tabsize = pu.aout->a_syms;

	      if (grub_read ((char *) &i, sizeof (int)) == sizeof (int))
		{
		  *((int *) RAW_ADDR (cur_addr)) = i;
		  cur_addr += sizeof (int);

		  mbi.syms.a.strsize = i;

		  i -= sizeof (int);

#ifdef DEBUG
		  printf (", strtab=0x%x", i);
#endif
		  symtab_err = (grub_read ((char *) RAW_ADDR (cur_addr), i)
				!= i);
		  cur_addr += i;
		}
	      else
		symtab_err = 1;
	    }
	  else
	    symtab_err = 1;

	  if (symtab_err)
	    {
#ifdef DEBUG
	      printf ("(bad)");
#endif
	      cur_addr = orig_addr;
	      mbi.syms.a.tabsize = 0;
	      mbi.syms.a.strsize = 0;
	      mbi.syms.a.addr = 0;
	    }
	  else
	    mbi.flags |= MB_INFO_AOUT_SYMS;
	}
    }
  else
    /* ELF executable */
    {
      unsigned loaded = 0, memaddr, memsiz, filesiz;
      Elf32_Phdr *phdr;

      /* reset this to zero for now */
      cur_addr = 0;

      /* scan for program segments */
      for (i = 0; i < pu.elf->e_phnum; i++)
	{
	  phdr = (Elf32_Phdr *)
	    (pu.elf->e_phoff + ((int) buffer)
	     + (pu.elf->e_phentsize * i));
	  if (phdr->p_type == PT_LOAD)
	    {
	      /* offset into file */
	      grub_seek (phdr->p_offset);
	      filesiz = phdr->p_filesz;
	      
	      if (type == KERNEL_TYPE_FREEBSD || type == KERNEL_TYPE_NETBSD 
		  || type == KERNEL_TYPE_OPENBSD )
		memaddr = RAW_ADDR (phdr->p_paddr & 0xFFFFFF);
	      else
		memaddr = RAW_ADDR (phdr->p_paddr);
	      
	      memsiz = phdr->p_memsz;
	      if (memaddr < RAW_ADDR (0x100000))
		errnum = ERR_BELOW_1MB;

	      /* If the memory range contains the entry address, get the
		 physical address here.  */
	      if (type == KERNEL_TYPE_MULTIBOOT
		  && (unsigned) entry_addr >= phdr->p_vaddr
		  && (unsigned) entry_addr < phdr->p_vaddr + memsiz)
		real_entry_addr = (entry_func) ((unsigned) entry_addr
						+ memaddr - phdr->p_vaddr);
		
	      /* make sure we only load what we're supposed to! */
	      if (filesiz > memsiz)
		filesiz = memsiz;
	      /* mark memory as used */
	      if (cur_addr < memaddr + memsiz)
		cur_addr = memaddr + memsiz;
#ifdef DEBUG
	      printf (", <0x%x:0x%x:0x%x>", memaddr, filesiz,
		      memsiz - filesiz);
#endif
	      /* increment number of segments */
	      loaded++;

	      /* load the segment */
	      if (memcheck (memaddr, memsiz)
		  && grub_read ((char *) memaddr, filesiz) == filesiz)
		{
		  if (memsiz > filesiz)
		    memset ((char *) (memaddr + filesiz), 0, memsiz - filesiz);
		}
	      else
		break;
	    }
	}

      if (! errnum)
	{
	  if (! loaded)
	    errnum = ERR_EXEC_FORMAT;
	  else
	    {
	      /* Load ELF symbols.  */
	      Elf32_Shdr *shdr = NULL;
	      int tab_size, sec_size;
	      int symtab_err = 0;

	      mbi.syms.e.num = pu.elf->e_shnum;
	      mbi.syms.e.size = pu.elf->e_shentsize;
	      mbi.syms.e.shndx = pu.elf->e_shstrndx;
	      
	      /* We should align to a 4K boundary here for good measure.  */
	      if (align_4k)
		cur_addr = (cur_addr + 0xFFF) & 0xFFFFF000;
	      
	      tab_size = pu.elf->e_shentsize * pu.elf->e_shnum;
	      
	      grub_seek (pu.elf->e_shoff);
	      if (grub_read ((char *) RAW_ADDR (cur_addr), tab_size)
		  == tab_size)
		{
		  mbi.syms.e.addr = cur_addr;
		  shdr = (Elf32_Shdr *) mbi.syms.e.addr;
		  cur_addr += tab_size;
		  
#ifdef DEBUG
		  printf (", shtab=0x%x", cur_addr);
#endif
  		  
		  for (i = 0; i < mbi.syms.e.num; i++)
		    {
		      /* This section is a loaded section,
			 so we don't care.  */
		      if (shdr[i].sh_addr != 0)
			continue;
		      
		      /* This section is empty, so we don't care.  */
		      if (shdr[i].sh_size == 0)
			continue;
		      
		      /* Align the section to a sh_addralign bits boundary.  */
		      cur_addr = ((cur_addr + shdr[i].sh_addralign) & 
				  - (int) shdr[i].sh_addralign);
		      
		      grub_seek (shdr[i].sh_offset);
		      
		      sec_size = shdr[i].sh_size;

		      if (! (memcheck (cur_addr, sec_size)
			     && (grub_read ((char *) RAW_ADDR (cur_addr),
					    sec_size)
				 == sec_size)))
			{
			  symtab_err = 1;
			  break;
			}
		      
		      shdr[i].sh_addr = cur_addr;
		      cur_addr += sec_size;
		    }
		}
	      else 
		symtab_err = 1;
	      
	      if (mbi.syms.e.addr < RAW_ADDR(0x10000))
		symtab_err = 1;
	      
	      if (symtab_err) 
		{
#ifdef DEBUG
		  printf ("(bad)");
#endif
		  mbi.syms.e.num = 0;
		  mbi.syms.e.size = 0;
		  mbi.syms.e.addr = 0;
		  mbi.syms.e.shndx = 0;
		  cur_addr = 0;
		}
	      else
		mbi.flags |= MB_INFO_ELF_SHDR;
	    }
	}
    }

  if (! errnum)
    {
#ifdef DEBUG
      grub_printf (", entry=0x%x]\n", (unsigned) entry_addr);
#endif      
      /* If the entry address is physically different from that of the ELF
	 header, correct it here.  */
      if (real_entry_addr)
	entry_addr = real_entry_addr;
    }
  else
    {
      putchar ('\n');
      type = KERNEL_TYPE_NONE;
    }

  grub_close ();

  /* Sanity check.  */
  if (suggested_type != KERNEL_TYPE_NONE && suggested_type != type)
    {
      errnum = ERR_EXEC_FORMAT;
      return KERNEL_TYPE_NONE;
    }
  
  return type;
}

int
load_module (char *module, char *arg)
{
/* BEGIN TCG EXTENSION */
#define maxlen 1024
  int len;
  char find_args[10][maxlen];
  int number_args = 0;
  int arg_counter = 0;
  int global_counter = 0;
    // 11 Bytes, 10 Bytes for Password and 1 for NULL-Termination
    // 10 because of substitution of --with-password-dialog...
  char module_pass1[11];
  char module_pass2[11];
  char new_cmdline[grub_strlen(arg)];
  char temp[grub_strlen(arg)];
  grub_memset(new_cmdline,0,grub_strlen(arg));
  grub_memset(temp,0,grub_strlen(arg));
  int x = getxy()>>8;
  int y = (getxy() & 0xff);

if (grub_strstr(arg,"--with-password-dialog"))
{
#ifdef DEBUG
    printf("tGRUB: Module contains --with-password-dialog\n");
    printf("tGRUB: Module argument length: %d\n",grub_strlen(arg));
#endif
    // Read Arguments
  for (global_counter = 0; global_counter < (grub_strlen(arg)+1); global_counter++) 
  {
	if (grub_isspace(arg[global_counter]))
	{
	    find_args[number_args][arg_counter] = 0;    
	    number_args++;
	    arg_counter = 0;
	    global_counter++;
	}
	find_args[number_args][arg_counter] = arg[global_counter];
	arg_counter++;
    }
    find_args[number_args][arg_counter] = 0;    
    number_args++;

    // Find --with-password-dialog
    for (arg_counter = 0; arg_counter < number_args ; arg_counter++)
    {
	int with_quotation_mark = 0;
	int print_mismatch = 0;
	
	if(!grub_memcmp(find_args[arg_counter],"--with-password-dialog\"",24))
	    with_quotation_mark = 1;
	    
	if(!grub_memcmp(find_args[arg_counter],"--with-password-dialog",23) ||
	    with_quotation_mark) 
	{
    // Read Password
#ifdef DEBUG
	    printf("tGRUB: Module %s received the %s argument\n",find_args[0],find_args[arg_counter]);
#endif
	    do {
#ifndef DEBUG
		gotoxy(0,y+1);
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		gotoxy(0,y+1);
#endif
		if (print_mismatch)
		    printf("\n  !!! Passwords do not match !!!\n");
		grub_memset(module_pass1,0,sizeof(module_pass1));
		grub_memset(module_pass2,0,sizeof(module_pass2));
    		get_cmdline("  Please enter  password: ", module_pass1, sizeof(module_pass1)-1, '*', 0);
	        get_cmdline("  Please retype password: ", module_pass2, sizeof(module_pass2)-1, '*', 0);
		print_mismatch=1;
	    }
	    while (check_password(module_pass1,module_pass2,PASSWORD_PLAIN));
	    if (with_quotation_mark)
	        sprintf(temp, "--password \'%s\'\" ",module_pass1);
	    else
	        sprintf(temp, "--password \'%s\' ",module_pass1);
    // Modify Commandline
	    memcpy(&new_cmdline[grub_strlen(new_cmdline)],&temp,grub_strlen(temp));
	}
	else
	{
	    memcpy(&new_cmdline[grub_strlen(new_cmdline)],&find_args[arg_counter],grub_strlen(find_args[arg_counter]));
	    // Set space after each argument
	    if (arg_counter < (number_args-1))
		memset(&new_cmdline[grub_strlen(new_cmdline)],' ',1);
	}
	
    }
#ifdef DEBUG
    printf("\ntGRUB: [before] Argument length: %d\ntGRUB: %s\n",grub_strlen(arg),arg);
#endif
	    grub_memset(arg,0,grub_strlen(arg));
	    memcpy(arg,&new_cmdline,grub_strlen(new_cmdline));
#ifdef DEBUG
    printf("\ntGRUB: [after]  Argument length: %d\ntGRUB: %s\n",grub_strlen(arg),arg);
    getkey();
#endif
}
#ifndef DEBUG
		gotoxy(0,y+1);
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		printf("                                                                               \n");
		gotoxy(x,y);
#endif
/* END TCG EXTENSION */

  /* if we are supposed to load on 4K boundaries */
  cur_addr = (cur_addr + 0xFFF) & 0xFFFFF000;

  if (!grub_open (module))
    return 0;

  if ((cur_addr + filemax) >= (1024*(1024+mbi.mem_upper)))
    {
      printf("Want to load module to 0x%x len 0x%x but only have 0x%x RAM\n",
	     cur_addr, filemax, 1024*(1024+mbi.mem_upper));
      errnum = ERR_BADMODADDR;
      grub_close ();
      return 0;
    }

  len = grub_read ((char *) cur_addr, -1);
  if (! len)
    {
      grub_close ();
      return 0;
    }

#ifdef DEBUG
  printf ("\n[Multiboot-module @ 0x%x, 0x%x bytes]\n", cur_addr, len);
#endif

  /* these two simply need to be set if any modules are loaded at all */
  mbi.flags |= MB_INFO_MODS;
  mbi.mods_addr = (int) mll;

  mll[mbi.mods_count].cmdline = (int) arg;
  mll[mbi.mods_count].mod_start = cur_addr;
  cur_addr += len;
  mll[mbi.mods_count].mod_end = cur_addr;
  mll[mbi.mods_count].pad = 0;

  /* increment number of modules included */
  mbi.mods_count++;

  grub_close ();
/* BEGIN TCG EXTENSION */
#ifdef DEBUG
    getkey();
#endif
/* END TCG EXTENSION */
  return 1;
}

void                  
create_vbe_module(void *ctrl_info, int ctrl_info_len,
		  void *mode_info, int mode_info_len,
		  int mode, int pmif, int pmif_len,
		  unsigned int version)
{             
  /* if we are supposed to load on 4K boundaries */
  cur_addr = (cur_addr + 0xFFF) & 0xFFFFF000;

#ifdef DEBUG
  printf ("\n[VESA %d.%d info @ 0x%x, 0x%x bytes]\n",
      version >> 8, version & 0xFF,
      cur_addr, ctrl_info_len + mode_info_len);
#endif

  grub_memmove((char*)cur_addr, ctrl_info, ctrl_info_len);
  mbi.vbe_control_info = (int)cur_addr;
  cur_addr += ctrl_info_len;

  grub_memmove((char*)cur_addr, mode_info, mode_info_len);
  mbi.vbe_mode_info    = (int)cur_addr;
  cur_addr += mode_info_len;

  mbi.flags |= MB_INFO_VIDEO_INFO;

  mbi.vbe_mode         = mode; 
  mbi.vbe_interface_seg = (pmif >> 16) & 0xFFFF;
  mbi.vbe_interface_off =  pmif        & 0xFFFF;
  mbi.vbe_interface_len = pmif_len;
}             

int
load_initrd (char *initrd)
{
  int len;
  unsigned long moveto;
  unsigned long max_addr;
  struct linux_kernel_header *lh
    = (struct linux_kernel_header *) (cur_addr - LINUX_SETUP_MOVE_SIZE);
  
#ifndef NO_DECOMPRESSION
  no_decompression = 1;
#endif
  
  if (! grub_open (initrd))
    goto fail;

  len = grub_read ((char *) cur_addr, -1);
  if (! len)
    {
      grub_close ();
      goto fail;
    }

  if (linux_mem_size)
    moveto = linux_mem_size;
  else
    moveto = (mbi.mem_upper + 0x400) << 10;
  
  moveto = (moveto - len) & 0xfffff000;
  max_addr = (lh->header == LINUX_MAGIC_SIGNATURE && lh->version >= 0x0203
	      ? lh->initrd_addr_max : LINUX_INITRD_MAX_ADDRESS);
  if (moveto + len >= max_addr)
    moveto = (max_addr - len) & 0xfffff000;
  
  /* XXX: Linux 2.3.xx has a bug in the memory range check, so avoid
     the last page.
     XXX: Linux 2.2.xx has a bug in the memory range check, which is
     worse than that of Linux 2.3.xx, so avoid the last 64kb. *sigh*  */
  moveto -= 0x10000;
  memmove ((void *) RAW_ADDR (moveto), (void *) cur_addr, len);

#ifdef DEBUG
  printf ("\n[Linux-initrd @ 0x%x, 0x%x bytes]\n", moveto, len);
#endif

  /* FIXME: Should check if the kernel supports INITRD.  */
  lh->ramdisk_image = RAW_ADDR (moveto);
  lh->ramdisk_size = len;

  grub_close ();

 fail:
  
#ifndef NO_DECOMPRESSION
  no_decompression = 0;
#endif

  return ! errnum;
}

void
set_load_addr (int addr)
{
#ifdef DEBUG
  printf ("Setting module load address to 0x%x\n", addr);
#endif
  cur_addr = addr;
}

#ifdef GRUB_UTIL
/* Dummy function to fake the *BSD boot.  */
static void
bsd_boot_entry (int flags, int bootdev, int sym_start, int sym_end,
		int mem_upper, int mem_lower)
{
  stop ();
}

void openbsd_boot (kernel_t type, int bootdev, char *arg)
 { bsd_boot(type, bootdev, arg);}
#else
#include "openbsd.c"
#endif


/*
 *  All "*_boot" commands depend on the images being loaded into memory
 *  correctly, the variables in this file being set up correctly, and
 *  the root partition being set in the 'saved_drive' and 'saved_partition'
 *  variables.
 */


void
bsd_boot (kernel_t type, int bootdev, char *arg)
{
  char *str;
  int clval = 0, i;
  struct bootinfo bi;

#ifdef GRUB_UTIL
  entry_addr = (entry_func) bsd_boot_entry;
#else
  stop_floppy ();
#endif

  while (*(++arg) && *arg != ' ');
  str = arg;
  while (*str)
    {
      if (*str == '-')
	{
	  while (*str && *str != ' ')
	    {
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
	      if (*str == 'D')
		clval |= RB_MULTIPLE;
	      if (*str == 'g')
		clval |= RB_GDB;
	      if (*str == 'h')
		clval |= RB_SERIAL;
	      if (*str == 'm')
		clval |= RB_MUTE;
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

  if (type == KERNEL_TYPE_FREEBSD)
    {
      clval |= RB_BOOTINFO;

      bi.bi_version = BOOTINFO_VERSION;

      *arg = 0;
      while ((--arg) > (char *) MB_CMDLINE_BUF && *arg != '/');
      if (*arg == '/')
	bi.bi_kernelname = arg + 1;
      else
	bi.bi_kernelname = 0;

      bi.bi_nfs_diskless = 0;
      bi.bi_n_bios_used = 0;	/* this field is apparently unused */

      for (i = 0; i < N_BIOS_GEOM; i++)
	{
	  struct geometry geom;

	  /* XXX Should check the return value.  */
	  get_diskinfo (i + 0x80, &geom);
	  /* FIXME: If HEADS or SECTORS is greater than 255, then this will
	     break the geometry information. That is a drawback of BSD
	     but not of GRUB.  */
	  bi.bi_bios_geom[i] = (((geom.cylinders - 1) << 16)
				+ (((geom.heads - 1) & 0xff) << 8)
				+ (geom.sectors & 0xff));
	}

      bi.bi_size = sizeof (struct bootinfo);
      bi.bi_memsizes_valid = 1;
      bi.bi_bios_dev = saved_drive;
      bi.bi_basemem = mbi.mem_lower;
      bi.bi_extmem = extended_memory;

      if (mbi.flags & MB_INFO_AOUT_SYMS)
	{
	  bi.bi_symtab = mbi.syms.a.addr;
	  bi.bi_esymtab = mbi.syms.a.addr + 4
	    + mbi.syms.a.tabsize + mbi.syms.a.strsize;
	}
#if 0
      else if (mbi.flags & MB_INFO_ELF_SHDR)
	{
	  /* FIXME: Should check if a symbol table exists and, if exists,
	     pass the table to BI.  */
	}
#endif
      else
	{
	  bi.bi_symtab = 0;
	  bi.bi_esymtab = 0;
	}

      /* call entry point */
      (*entry_addr) (clval, bootdev, 0, 0, 0, ((int) (&bi)));
    }
  else
    {
      /*
       *  We now pass the various bootstrap parameters to the loaded
       *  image via the argument list.
       *
       *  This is the official list:
       *
       *  arg0 = 8 (magic)
       *  arg1 = boot flags
       *  arg2 = boot device
       *  arg3 = start of symbol table (0 if not loaded)
       *  arg4 = end of symbol table (0 if not loaded)
       *  arg5 = transfer address from image
       *  arg6 = transfer address for next image pointer
       *  arg7 = conventional memory size (640)
       *  arg8 = extended memory size (8196)
       *
       *  ...in actuality, we just pass the parameters used by the kernel.
       */

      /* call entry point */
      unsigned long end_mark;

      if (mbi.flags & MB_INFO_AOUT_SYMS) {
	end_mark = (mbi.syms.a.addr + 4
		    + mbi.syms.a.tabsize + mbi.syms.a.strsize);
      } else
	/* FIXME: it should be mbi.syms.e.size.  */
	end_mark = 0;
      
      (*entry_addr) (clval, bootdev, 0, end_mark,
		     extended_memory, mbi.mem_lower);
    }
}
