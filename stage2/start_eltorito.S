/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1994-2002  H. Peter Anvin
 *  Copyright (C) 1999,2000,2001,2004	Free Software Foundation, Inc.
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
 *
 */
	
/*
 Most of this file was originally "isolinux.asm" from SYSLINUX package.
 It has been very heavily modified.
*/

#define ASM_FILE
#include "stage1.h"
#include "shared.h"
#include "iso9660.h"

#ifndef STAGE1_5
#include "stage2_size.h"
#endif


	/* Absolute addresses
	   This makes the assembler generate the address without support
	   from the linker. (ELF can't relocate 16-bit addresses!) */
#define ABS(x)			(x-_start+BOOTSEC_LOCATION)

#ifdef STAGE1_5
# define STAGE_ADDR		0x2000
#else
# define STAGE_ADDR		0x8000
#endif /* STAGE1_5 */

	.file	"start_eltorito.S"

	.text

	/* Tell GAS to generate 16-bit instructions so that this code works
	   in real mode. */
	.code16

	.globl	start, _start

/*
 * Primary entry point.	 Because BIOSes are buggy, we only load the first
 * CD-ROM sector (2K) of the file, so the number one priority is actually
 * loading the rest.
 */
start:
_start:

	cli
	ljmp	$0, $ABS(real_start)

	. = _start + 8			    /* Pad to file offset 8 */

		/* This table gets filled in by mkisofs using the
		   -boot-info-table option */
bi_pvd:		.long 0xDEADBEEF	    /* LBA of primary volume descript */
bi_file:	.long 0xDEADBEEF	    /* LBA of boot file */
bi_length:	.long 0xDEADBEEF	    /* Length of boot file */
bi_csum:	.long 0xDEADBEEF	    /* Checksum of boot file */
bi_reserved:	.space (10*4)		    /* Reserved */

real_start:
	xor	%ax, %ax
	mov	%ax, %ss
	mov	%ax, %ds
	mov	%ax, %es
	mov	%ax, %fs
	mov	%ax, %gs
	mov	$STAGE1_STACKSEG, %sp	    /* set up the REAL stack */
	sti
	cld

	/* save drive reference first thing! */
	mov	%dl, ABS(BootDrive)

load_image:
	/* Set up boot file sector, size, load address */
	mov	ABS(bi_length), %eax
	add	$(ISO_SECTOR_SIZE-1), %eax
	shr	$ISO_SECTOR_BITS, %eax	    /* dwords->sectors */
	mov	%ax, %bp		    /* boot file sectors */
	mov	$(STAGE_ADDR >> 4), %bx
	mov	%bx, %es
	xor	%bx, %bx
	mov	ABS(bi_file), %eax
	call	getlinsec
	mov	%ds, %ax
	mov	%ax, %es

bootit:

	/* Begin TCG extension */
	/* Hashes the rest of stage2 and writes the result into PCR9.
	   For details see README file. */
	
	/* Store registers which have to be modified */
	pushw %es
	pushw %ds
	pushw %si
	pushw %di
	pusha

	/* Create own segment for TCG */
	movw $TCG_SEG, %ax
	movw %ax, %es
	movw %ax, %ds
	
	/* Preparing and executing TCG_HashAll function call */
	movw $0x0010, 0x00			/* length input parameter block */
	movl $ABS(STAGE_ADDR), 0x04		/* start address of data to be hashed */
	movl $bi_length, 0x08			/* length of data to be hashed */
	movl $0x00000004, 0x0C			/* algorithm = SHA1 */

	movw $0xBB05, %ax			/* function = TCG_HashAll */
	movl $0x41504354, %ebx			/* TCG value */
	xorl %ecx, %ecx				/* must be zero */
	xorl %edx, %edx				/* must be zero */
	movw $0x00, %di				/* pointer to input block */
	movw $0x28, %si				/* pointer to digest block */
	int $0x1A				/* TCG interrupt call */

	/* Preparing and executing TCG_PassThroughToTPM function call */
	movw $0x002A, 0x12			/* length input parameter block */
	movw $0x0022, 0x16			/* size of output parameter block */
	movw $0xC100, 0x1A			/* TPM_TAG_RQU_COMMAND */
	movl $0x22000000, 0x1C			/* total number of input bytes */
	movl $0x14000000, 0x20			/* command ordinal */
	movl $0x09000000, 0x24			/* PCR to be updated */

	movw $0xBB02, %ax			/* function = TCG_PassThroughToTPM */
	movl $0x41504354, %ebx			/* TCG value */
	movw $0x12, %di				/* pointer to input block */
	movw $0x60, %si				/* pointer to output block */
	int $0x1A				/* TCG interrupt call */

	/* Restore registers saved at the beginning */
	popa
	popw %di
	popw %si
	popw %ds
	popw %es
		
	/* End TCG extension */

	/* save the sector number of the second sector in %ebp */
	mov	$ABS(firstlist - BOOTSEC_LISTSIZE), %si
	mov	(%si), %ebp
	mov	ABS(BootDrive), %dl	    /* this makes sure %dl is our "boot" drive */
	ljmp	$0, $(STAGE_ADDR+SECTOR_SIZE)  /* jump to main() in asm.S */

/* go here when you need to stop the machine hard after an error condition */
stop:	jmp	stop


/*
 * Get linear sectors - EBIOS LBA addressing, 2048-byte sectors.
 *
 * Note that we can't always do this as a single request, because at least
 * Phoenix BIOSes has a 127-sector limit.  To be on the safe side, stick
 * to 16 sectors (32K) per request.
 *
 * Input:
 *	 EAX	 - Linear sector number
 *	 ES:BX	 - Target buffer
 *	 BP	 - Sector count
 */
getlinsec:
	mov	$ABS(dapa), %si		   /* Load up the DAPA */
	mov	%bx, 4(%si)
	mov	%es, %bx
	mov	%bx, 6(%si)
	mov	%eax, 8(%si)
1:
	push	%bp
	push	%si
	cmp	ABS(MaxTransfer), %bp
	jbe	2f
	mov	ABS(MaxTransfer), %bp
2:
	mov	%bp, 2(%si)
	mov	ABS(BootDrive), %dl
	mov	$0x42, %ah		    /* Extended Read */
	call	xint13
	pop	%si
	pop	%bp
	movzwl	2(%si), %eax		    /* Sectors we read */
	add	%eax, 8(%si)		    /* Advance sector pointer */
	sub	%ax, %bp		    /* Sectors left */
	shl	$(ISO_SECTOR_BITS-4), %ax   /* 2048-byte sectors -> segment */
	add	%ax, 6(%si)		    /* Advance buffer pointer */

	cmp	$0, %bp
	ja	1b
	mov	8(%si), %eax		    /* Return next sector */
	ret

/*
 * INT 13h with retry
 */
xint13:
	movb	$6, ABS(RetryCount)
	pushal
.try:
	int	$0x13
	jc	1f
	add	$(8*4), %sp		    /* Clean up stack */
	ret
1:
	mov	%ah, %dl		    /* Save error code */
	decb	ABS(RetryCount)
	jz	.real_error
	mov	ABS(RetryCount), %al
	mov	ABS(dapa+2), %ah	    /* Sector transfer count */
	cmp	$2, %al			    /* Only 2 attempts left */
	ja	2f
	mov	$1, %ah			    /* Drop transfer size to 1 */
	jmp	.setmaxtr
2:
	cmp	$3, %al
	ja	3f			    /* First time, just try again */
	shr	$1, %ah			    /* Otherwise, try to reduce */
	adc	$0, %ah			    /* the max transfer size, but not */
.setmaxtr:
	mov	%ah, ABS(MaxTransfer)
	mov	%ah, ABS(dapa+2)
3:
	popal
	jmp	.try

.real_error:
	mov	%dl, %al
	popal
	jmp	stop


/**************************************************************************/
/*
 * EBIOS disk address packet
 */
		.align 8
dapa:		.byte 16		   /* Packet size */
		.byte 0			   /* reserved */
		.word 0			   /* +2 Block count */
		.word 0			   /* +4 Offset of buffer */
		.word 0			   /* +6 Segment of buffer */
		.long 0			   /* +8 LBA (LSW) */
		.long 0			   /* +C LBA (MSW) */

VARIABLE(BootDrive)
	.byte 0xFF
VARIABLE(MaxTransfer)
	.word 16			   /* Max sectors per transfer (32Kb) */
VARIABLE(RetryCount)
	.byte 0


/*
 *  This area is an empty space between the main body of code below which
 *  grows up (fixed after compilation, but between releases it may change
 *  in size easily), and the lists of sectors to read, which grows down
 *  from a fixed top location.
 */

	.word 0
	.word 0

	. = _start + SECTOR_SIZE - BOOTSEC_LISTSIZE

	/* fill the first data listing with the default */
blocklist_default_start:/* this is the sector start parameter, in logical
			   sectors from the start of the disk, sector 0 */
	.long 0

blocklist_default_len:	/* this is the number of sectors to read */
#ifdef STAGE1_5
	.word 0
#else
	.word (STAGE2_SIZE + ISO_SECTOR_SIZE - 1) >> ISO_SECTOR_BITS
#endif
blocklist_default_seg:	/* this is the segment of the starting address
			   to load the data into */
	.word (STAGE_ADDR + SECTOR_SIZE) >> 4

firstlist:	/* this label has to be after the list data!!! */
