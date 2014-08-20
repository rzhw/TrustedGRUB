/*      This file contains functions and utilities for the Trusted GRUB project
        at http://www.prosec.rub.de. The SHA1-implementation has been written by
        Marko Wolf <mwolf@crypto.rub.de> and tested according to FIPS-180.
        All other functions and updates have been done by Marcel Selhorst
        <m.selhorst@sirrix.com> and are licensed under the same license as GRUB.
        For reuasage of the SHA1-implementation, please contact the original author. */

#include "sha1.c"
int main (int argc, char *argv[])
{
  char systemcall[1024];
  int i;
  t_U32 hash_result[5];

    if (argc == 1)
    {
        printf("Missing arguments! Usage: %s {filename}\n \n",argv[0]);
        return -1;
    }

    sprintf(systemcall,"sha1sum %s",argv[1]);

  /* Display result */
    if (calculate_sha1(argv[1], hash_result))
    {
	printf("Error during SHA1-calculation\n");
	return -1;
    }

//    printf("Results via sha1:\n");
    for (i=0; i<5; i++)
	printf("%08lx",hash_result[i]);
    printf("  %s\n",argv[1]);
//    printf("Results via sha1sum:\n");
//    system(systemcall);
    return 0;
}
