/*      This file contains functions and utilities for the Trusted GRUB project
        at http://www.prosec.rub.de. The SHA1-implementation has been written by
        Marko Wolf <mwolf@crypto.rub.de> and tested according to FIPS-180.
	The SHA1-macros are from "Christophe Devine" <devine@cr0.net>.
        All other functions and updates have been done by Marcel Selhorst
        <m.selhorst@sirrix.com> and are licensed under the same license as GRUB.
        For reuasage of the SHA1-implementation, please contact the original authors. */

typedef unsigned long      t_U32;
typedef unsigned short     t_U16;
typedef unsigned char      t_U8;
typedef signed long        t_S32;
typedef signed short       t_S16;
typedef signed char        t_S8;
typedef char*              t_string;

typedef struct
{
  t_U32 total_bytes_Hi; /* high word of 64-bit value for bytes count */
  t_U32 total_bytes_Lo; /* low word of 64-bit value for bytes count  */
  t_U32 vector[5];      /* 5  32-bit hash words                     */
  t_U8  buffer[64];     /* 64 byte buffer                            */
} sha1_context;

#define NULL ((void*)0)

// concatenates 4 × 8-bit words (= 1 byte) to one 32-bit word
#define CONCAT_4_BYTES( w32, w8, w8_i)            \
{                                                 \
    (w32) = ( (t_U32) (w8)[(w8_i)    ] << 24 ) |  \
            ( (t_U32) (w8)[(w8_i) + 1] << 16 ) |  \
            ( (t_U32) (w8)[(w8_i) + 2] <<  8 ) |  \
            ( (t_U32) (w8)[(w8_i) + 3]       );   \
}

// splits a 32-bit word into 4 × 8-bit words (= 1 byte)
#define SPLIT_INTO_4_BYTES( w32, w8, w8_i)        \
{                                                 \
    (w8)[(w8_i)    ] = (t_U8) ( (w32) >> 24 );    \
    (w8)[(w8_i) + 1] = (t_U8) ( (w32) >> 16 );    \
    (w8)[(w8_i) + 2] = (t_U8) ( (w32) >>  8 );    \
    (w8)[(w8_i) + 3] = (t_U8) ( (w32)       );    \
}

// FIPS-180-1 padding sequence
static t_U8 sha1_padding[64] =
{
 (t_U8) 0x80, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0,
 (t_U8)    0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0,
 (t_U8)    0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0,
 (t_U8)    0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0, (t_U8) 0
};

int sha1_init(sha1_context *ctx )
{

  // parameter check
  if ( ctx == NULL )
  {
    return -1;
  }

  // byte length = 0
  ctx->total_bytes_Lo = 0;
  ctx->total_bytes_Hi = 0;

  // FIPS 180-1 init values
  ctx->vector[0] = 0x67452301;
  ctx->vector[1] = 0xEFCDAB89;
  ctx->vector[2] = 0x98BADCFE;
  ctx->vector[3] = 0x10325476;
  ctx->vector[4] = 0xC3D2E1F0;

  // successful
  return 0;
}


static void sha1_process(sha1_context *ctx, t_U8 *byte_64_block )
{
  // declarations
  t_U32 temp, W[16];
  t_U32 A, B, C, D, E;

  // concatenate 64 bytes to 16 × 32-bit words
  CONCAT_4_BYTES( W[0],  byte_64_block,  0 );
  CONCAT_4_BYTES( W[1],  byte_64_block,  4 );
  CONCAT_4_BYTES( W[2],  byte_64_block,  8 );
  CONCAT_4_BYTES( W[3],  byte_64_block, 12 );
  CONCAT_4_BYTES( W[4],  byte_64_block, 16 );
  CONCAT_4_BYTES( W[5],  byte_64_block, 20 );
  CONCAT_4_BYTES( W[6],  byte_64_block, 24 );
  CONCAT_4_BYTES( W[7],  byte_64_block, 28 );
  CONCAT_4_BYTES( W[8],  byte_64_block, 32 );
  CONCAT_4_BYTES( W[9],  byte_64_block, 36 );
  CONCAT_4_BYTES( W[10], byte_64_block, 40 );
  CONCAT_4_BYTES( W[11], byte_64_block, 44 );
  CONCAT_4_BYTES( W[12], byte_64_block, 48 );
  CONCAT_4_BYTES( W[13], byte_64_block, 52 );
  CONCAT_4_BYTES( W[14], byte_64_block, 56 );
  CONCAT_4_BYTES( W[15], byte_64_block, 60 );

// rotate left by n bits
#define ROTATE_N_LEFT(x,n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

// extends 16 × 32-bit words to 80 × 32-bit words
#define EXTENDED_W(t)                                 \
(                                                     \
  temp = W[(t -  3) & 0x0F] ^ W[(t - 8) & 0x0F] ^     \
         W[(t - 14) & 0x0F] ^ W[ t      & 0x0F],      \
  ( W[t & 0x0F] = ROTATE_N_LEFT(temp,1) )             \
)

// main formula
#define P(a,b,c,d,e,Wi)                               \
{                                                     \
  e += ROTATE_N_LEFT(a,5) + F(b,c,d) + K + Wi;        \
  b  = ROTATE_N_LEFT(b,30);                           \
}

  // init A..E
  A = ctx->vector[0];
  B = ctx->vector[1];
  C = ctx->vector[2];
  D = ctx->vector[3];
  E = ctx->vector[4];

// round I (0..19)
#define F(x,y,z) (z ^ (x & (y ^ z)))
#define K 0x5A827999

  P( A, B, C, D, E, W[0]  );
  P( E, A, B, C, D, W[1]  );
  P( D, E, A, B, C, W[2]  );
  P( C, D, E, A, B, W[3]  );
  P( B, C, D, E, A, W[4]  );
  P( A, B, C, D, E, W[5]  );
  P( E, A, B, C, D, W[6]  );
  P( D, E, A, B, C, W[7]  );
  P( C, D, E, A, B, W[8]  );
  P( B, C, D, E, A, W[9]  );
  P( A, B, C, D, E, W[10] );
  P( E, A, B, C, D, W[11] );
  P( D, E, A, B, C, W[12] );
  P( C, D, E, A, B, W[13] );
  P( B, C, D, E, A, W[14] );
  P( A, B, C, D, E, W[15] );
  P( E, A, B, C, D, EXTENDED_W(16) );
  P( D, E, A, B, C, EXTENDED_W(17) );
  P( C, D, E, A, B, EXTENDED_W(18) );
  P( B, C, D, E, A, EXTENDED_W(19) );

#undef K
#undef F

// round II (20..39)
#define F(x,y,z) (x ^ y ^ z)
#define K 0x6ED9EBA1

  P( A, B, C, D, E, EXTENDED_W(20) );
  P( E, A, B, C, D, EXTENDED_W(21) );
  P( D, E, A, B, C, EXTENDED_W(22) );
  P( C, D, E, A, B, EXTENDED_W(23) );
  P( B, C, D, E, A, EXTENDED_W(24) );
  P( A, B, C, D, E, EXTENDED_W(25) );
  P( E, A, B, C, D, EXTENDED_W(26) );
  P( D, E, A, B, C, EXTENDED_W(27) );
  P( C, D, E, A, B, EXTENDED_W(28) );
  P( B, C, D, E, A, EXTENDED_W(29) );
  P( A, B, C, D, E, EXTENDED_W(30) );
  P( E, A, B, C, D, EXTENDED_W(31) );
  P( D, E, A, B, C, EXTENDED_W(32) );
  P( C, D, E, A, B, EXTENDED_W(33) );
  P( B, C, D, E, A, EXTENDED_W(34) );
  P( A, B, C, D, E, EXTENDED_W(35) );
  P( E, A, B, C, D, EXTENDED_W(36) );
  P( D, E, A, B, C, EXTENDED_W(37) );
  P( C, D, E, A, B, EXTENDED_W(38) );
  P( B, C, D, E, A, EXTENDED_W(39) );

#undef K
#undef F

// round III (40..59)
#define F(x,y,z) ((x & y) | (z & (x | y)))
#define K 0x8F1BBCDC

  P( A, B, C, D, E, EXTENDED_W(40) );
  P( E, A, B, C, D, EXTENDED_W(41) );
  P( D, E, A, B, C, EXTENDED_W(42) );
  P( C, D, E, A, B, EXTENDED_W(43) );
  P( B, C, D, E, A, EXTENDED_W(44) );
  P( A, B, C, D, E, EXTENDED_W(45) );
  P( E, A, B, C, D, EXTENDED_W(46) );
  P( D, E, A, B, C, EXTENDED_W(47) );
  P( C, D, E, A, B, EXTENDED_W(48) );
  P( B, C, D, E, A, EXTENDED_W(49) );
  P( A, B, C, D, E, EXTENDED_W(50) );
  P( E, A, B, C, D, EXTENDED_W(51) );
  P( D, E, A, B, C, EXTENDED_W(52) );
  P( C, D, E, A, B, EXTENDED_W(53) );
  P( B, C, D, E, A, EXTENDED_W(54) );
  P( A, B, C, D, E, EXTENDED_W(55) );
  P( E, A, B, C, D, EXTENDED_W(56) );
  P( D, E, A, B, C, EXTENDED_W(57) );
  P( C, D, E, A, B, EXTENDED_W(58) );
  P( B, C, D, E, A, EXTENDED_W(59) );

#undef K
#undef F

// round IV (60..79)
#define F(x,y,z) (x ^ y ^ z)
#define K 0xCA62C1D6

  P( A, B, C, D, E, EXTENDED_W(60) );
  P( E, A, B, C, D, EXTENDED_W(61) );
  P( D, E, A, B, C, EXTENDED_W(62) );
  P( C, D, E, A, B, EXTENDED_W(63) );
  P( B, C, D, E, A, EXTENDED_W(64) );
  P( A, B, C, D, E, EXTENDED_W(65) );
  P( E, A, B, C, D, EXTENDED_W(66) );
  P( D, E, A, B, C, EXTENDED_W(67) );
  P( C, D, E, A, B, EXTENDED_W(68) );
  P( B, C, D, E, A, EXTENDED_W(69) );
  P( A, B, C, D, E, EXTENDED_W(70) );
  P( E, A, B, C, D, EXTENDED_W(71) );
  P( D, E, A, B, C, EXTENDED_W(72) );
  P( C, D, E, A, B, EXTENDED_W(73) );
  P( B, C, D, E, A, EXTENDED_W(74) );
  P( A, B, C, D, E, EXTENDED_W(75) );
  P( E, A, B, C, D, EXTENDED_W(76) );
  P( D, E, A, B, C, EXTENDED_W(77) );
  P( C, D, E, A, B, EXTENDED_W(78) );
  P( B, C, D, E, A, EXTENDED_W(79) );

#undef K
#undef F

  // assign vectors
  ctx->vector[0] += A;
  ctx->vector[1] += B;
  ctx->vector[2] += C;
  ctx->vector[3] += D;
  ctx->vector[4] += E;
}

int sha1_update(sha1_context *ctx, t_U8 *chunk_data, t_U32 chunk_length)
{

  // declarations
  t_U32 left, fill;
  t_U32 i;

  // parameter check
  if ( (ctx == NULL) || (chunk_data == NULL) || (chunk_length < 1) )
  {
    return -1;
  }

  // chunk_length =? 0
  if ( chunk_length == 0 ) return -1;

  // chunk_length = n * 64 byte + left
  left = ctx->total_bytes_Lo & 0x3F;

  // fill bytes remain to 64 byte block
  fill = 64 - left;

  // total = total + chunk_length
  ctx->total_bytes_Lo += chunk_length;
  
  // mask 32 bit
  ctx->total_bytes_Lo &= 0xFFFFFFFF;

  if ( ctx->total_bytes_Lo < chunk_length )
  {
    ctx->total_bytes_Hi++;
  }

  // if we have something in the buffer (left > 0) and 
  // the chunk has enougth data to fill a 64 byte block (chunk_length >= fill)
  if ( (left > 0) && (chunk_length >= fill) )
  {
     // fill buffer with data from new chunk
     for ( i = 0; i < fill; i++ )
     {
        ctx->buffer[left + i] = chunk_data[i];
     }

     // process 64 byte buffer block
     sha1_process( ctx, ctx->buffer );

     // dec chunk_length by fill
     chunk_length -= fill;

     // move data pointer by fill
     chunk_data  += fill;

     // buffer is fully processed
     left = 0;
  }

  // process all remaining 64 byte chunks
  while( chunk_length >= 64 )
  {
     sha1_process( ctx, chunk_data );
     chunk_length -= 64;
     chunk_data  += 64;
  }

  // if final chunk_length between 1..63 byte
  if ( chunk_length > 0 )
  {
     // append remainder to 64 byte into buffer resp. fill the empty buffer
     for ( i = 0; i < chunk_length; i++ )
     {
       ctx->buffer[left + i] = chunk_data[i];
     }
  }

  // successfull
  return 0;
}

int sha1_finish(sha1_context *ctx, t_U32 *sha1_hash)
{

  // declarations
  t_U32 last, padn;
  t_U32 high, low;
  t_U8  msglen[8];
  int   ret;

  // parameter check
  if ( (ctx == NULL) || (sha1_hash == NULL) )
  {
    *sha1_hash = 0;
    return -1;
  }

  // build msglen array[8 × 8-bit] from total[2 × 32-bit] = n × 64 byte
  high = ( ctx->total_bytes_Lo >> 29 ) | ( ctx->total_bytes_Hi <<  3 );
  low  = ( ctx->total_bytes_Lo <<  3 );
  SPLIT_INTO_4_BYTES( high, msglen, 0 );
  SPLIT_INTO_4_BYTES( low,  msglen, 4 ); 

  // total = n × 64 bytes + last
  last = ctx->total_bytes_Lo & 0x3F;

  // number of padding zeros 
  padn = ( last < 56 ) ? ( 56 - last ) : ( 120 - last );

  // update SHA-1 context with remaining buffer and padding to 64 bytes with bit sequence (1,0,...,0)
  ret = sha1_update( ctx, sha1_padding, padn );

  // update SHA-1 context with total length
  ret = sha1_update( ctx, msglen, 8 );

  // assign final hash words
  sha1_hash[0] = ctx->vector[0];
  sha1_hash[1] = ctx->vector[1];
  sha1_hash[2] = ctx->vector[2];
  sha1_hash[3] = ctx->vector[3];
  sha1_hash[4] = ctx->vector[4];

  // successful
  return 0;
}
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* The maximum size of a TCG data buffer as used within tGRUB */
#define TCG_BUFFER_SIZE 0xEFFF

int calculate_sha1(char* filename, t_U32 *sha1_result)
{
    int fd1;
    int result;
    int bytes_to_copy = 0;
    int filesize = 0;
    int round = 1;
    struct stat attribute;
    char *tcgbuffer;
    tcgbuffer = (char*) malloc(TCG_BUFFER_SIZE);

#ifdef DEBUG
    printf("Calculating SHA1 for file: %s\n",filename);
#endif
    /* Open the input file */
    fd1 = open(filename,O_RDONLY);
    if (fd1 < 0)
    {
	printf("Error opening file\n");
	return -1;
    }
    tcgbuffer = (char*) malloc(TCG_BUFFER_SIZE);
    stat(filename,&attribute);
    filesize = attribute.st_size;
#ifdef DEBUG
    printf("Opened %s with size: %d\n",filename,filesize);
#endif

    /* Initialise SHA1-Context */
    sha1_context my_sha1_context;
    result = sha1_init(&my_sha1_context);
    if (result)
    {
	close (fd1);
	free(tcgbuffer);
        return -1;
    }

    /* calculate rounds */
    bytes_to_copy = filesize; 
#ifdef DEBUG
    printf("Performing %d round(s) for %d bytes \n",((filesize/TCG_BUFFER_SIZE)+1), bytes_to_copy);
#endif

#ifdef DEBUG
	printf("Round %d (%d Bytes to go)\n",round,bytes_to_copy);
#endif
        memset(tcgbuffer,0,TCG_BUFFER_SIZE);

	    while (bytes_to_copy > TCG_BUFFER_SIZE)
	    {
#ifdef DEBUG
		printf("Round %d (%d Bytes to go)\n",round,bytes_to_copy);
#endif
		read(fd1,tcgbuffer,TCG_BUFFER_SIZE);
    		result = sha1_update(&my_sha1_context, (unsigned char*) tcgbuffer, TCG_BUFFER_SIZE);
		if (result)
	        {
		    close (fd1);
		    free(tcgbuffer);
    		    return -1;
		}
    		bytes_to_copy = bytes_to_copy - TCG_BUFFER_SIZE;
		round++;
	    }

	    // And the last one
#ifdef DEBUG
	    printf("Round %d (Last %d Bytes)\n",round,bytes_to_copy);
#endif
	    memset(tcgbuffer,0,TCG_BUFFER_SIZE);
	    read(fd1,tcgbuffer,bytes_to_copy);
    	    result = sha1_update(&my_sha1_context, (unsigned char*) tcgbuffer, bytes_to_copy);
    	    if (result)
	    {
		close (fd1);
		free(tcgbuffer);
    		return -1;
	    }

    close (fd1);
    free(tcgbuffer);
    result = sha1_finish(&my_sha1_context, sha1_result);
    if (result)
	return -1;
    return 0;
}
