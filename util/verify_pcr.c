/*      This file contains functions and utilities for the Trusted GRUB project
        at http://www.prosec.rub.de. The SHA1-implementation has been written by
        Marko Wolf <mwolf@crypto.rub.de> and tested according to FIPS-180.
        All other functions and updates have been done by Marcel Selhorst
        <m.selhorst@sirrix.com> and are licensed under the same license as GRUB.
        For reuasage of the SHA1-implementation, please contact the original author. */

#include "sha1.c"
//#define DEBUG
int main (int argc, char *argv[])
{
    int i,j,ret;
    int no_of_files;
    char filename[1024];
    unsigned char pcr[20];
    unsigned char pcr2[40];
    t_U32 hash_result[5];
    sha1_context my_sha1;


    if (argc < 3)
    {
        printf("Missing arguments! Usage: %s  <pcr initial value {NULL | 20 byte hex}> {filenames-1 ... filenames-n}\n \n",argv[0]);
        return -1;
    }

    // Testing PCR-Register
    if (!strcmp(argv[1],"NULL"))
    {
#ifdef DEBUG
	printf("Setting PCR-Register to 0\n");
#endif
	memset(pcr,0,20);
    }
    else
    {
#ifdef DEBUG
	printf("Testing PCR value: ");
#endif
	for (i=0; i<40; i++)
	{
	    switch((argv[1])[i])
	    {
		case '0': pcr2[i]=0; break;
		case '1': pcr2[i]=1; break;
		case '2': pcr2[i]=2; break;
		case '3': pcr2[i]=3; break;
		case '4': pcr2[i]=4; break;
		case '5': pcr2[i]=5; break;
		case '6': pcr2[i]=6; break;
		case '7': pcr2[i]=7; break;
		case '8': pcr2[i]=8; break;
		case '9': pcr2[i]=9; break;
		case 'a': pcr2[i]=0xa; break;
		case 'b': pcr2[i]=0xb; break;
		case 'c': pcr2[i]=0xc; break;
		case 'd': pcr2[i]=0xd; break;
		case 'e': pcr2[i]=0xe; break;
		case 'f': pcr2[i]=0xf; break;
		default: printf("Failure, please give correct 20 byte hex string!\n"); return -1;
	    }
	}
#ifdef DEBUG
	printf("OK\nSetting PCR-Register to ");
#endif
	for (i=0; i<20; i++)
	{
	    pcr[i] = pcr2[2*i]<<4 | pcr2[(2*i)+1];
#ifdef DEBUG
	    printf("%02x",pcr[i]);
#endif
	}
#ifdef DEBUG
	printf("\n");
#endif
    }

    // Testing files    
    no_of_files = argc-1;
#ifdef DEBUG
    printf("Testing %d Files\n",no_of_files-1);
#endif
    for (i=2; i <= no_of_files; i++)
    {
#ifdef DEBUG
	printf("Testing %s: ",argv[i]);
#endif
	snprintf(filename, sizeof(filename), "test -e %s",argv[i]);
	ret = system(filename);
	if (ret)
	{
    	    printf("Error!\nWrong filename %s! Usage: %s <pcr initial value {NULL | 20 byte hex}> {filenames-1 ... filenames-n}\n \n",argv[i],argv[0]);
    	    return -1;
	}
	else
	{
#ifdef DEBUG
	    printf("OK\n");
#endif
	}
    }
    
    // Calculating SHA1

    for (j=2; j<= no_of_files; j++)
    {
	if (calculate_sha1(argv[j], hash_result))
	{
	    printf("Error during SHA1-calculation\n");
	    return -1;
	}
	// Copying current PCR content into new buffer
	memcpy(pcr2,pcr,20);
	for (i=0; i<5; i++)
	{
	    pcr2[20+(4*i)] = ((hash_result[i] >> 24) & 0xff);
	    pcr2[21+(4*i)] = ((hash_result[i] >> 16) & 0xff);
	    pcr2[22+(4*i)] = ((hash_result[i] >>  8) & 0xff);
	    pcr2[23+(4*i)] = ((hash_result[i]      ) & 0xff);
	}

        /* Display result */
#ifdef DEBUG
	printf("Hashing 40 Bytes: ");
	for (i=0; i<40; i++)
	    printf("%02x",pcr2[i]);
	printf("\n");
#endif
	sha1_init(&my_sha1);
	sha1_update(&my_sha1, pcr2, 40);
	sha1_finish(&my_sha1, hash_result);	
	for (i=0; i<5; i++)
	{
	    pcr[0+(4*i)] = ((hash_result[i] >> 24) & 0xff);
	    pcr[1+(4*i)] = ((hash_result[i] >> 16) & 0xff);
	    pcr[2+(4*i)] = ((hash_result[i] >>  8) & 0xff);
	    pcr[3+(4*i)] = ((hash_result[i]      ) & 0xff);
	}
#ifdef DEBUG
	printf("Provisional result for PCR: ");
	for (i=0; i<20; i++)
    	    printf("%02x",pcr[i]);
	printf("\n");
#endif
    }
    printf(   "*******************************************************************************\n* Result for PCR: ");
    for (i=0; i<20; i++)
        printf("%02x ",pcr[i]);
    printf("*\n*******************************************************************************\n");
    return 0;
}
