/*-----------------------------------------------------------------------*\
 |  Originally by Kevin Kayes, but he refused any responsibility for it. |
 |                                                                       |
 |  Copyright 1986-1988 by Parag Patel.  All Rights Reserved.            |
 |  Copyright 1994 by CodeGen, Inc.  All Rights Reserved.                |
\*-----------------------------------------------------------------------*/


#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "cpmdisc.h"
#include <sys/types.h>
#include <sys/stat.h>

unsigned char	disc[DISCSIZE];
unsigned char	buf[128];
unsigned char	*bufptr = buf;
int		sector, track;

void
usage(void)
{
    printf("Usage: makedisc [-bc] discname [filename]\n");
    printf("       -b = binary file (no conversion - default)\n");
    printf("       -c = text file (convert newlines to CRLF)\n");
    printf("       discname = A: B: etc.\n");
    printf("       filename = file to be inserted into the disc (1 max)\n");
    printf("       If an option (-b or -c) is given the optional filename must\n");
    printf("       be supplied. The -b and -c options are mutually exclusive.\n");

    exit(1);
}

void
writesector(unsigned char *sectordata)
{
    unsigned char	*cp;
    int			i;
    int			physical;

    /* incoming sector is logical (0 -> (SECTORSPERTRACK - 1)) */
    physical = sectorxlat[sector];

/* printf("Writesector: track = %d sector = %d physical = %d\n", track, sector, physical); */

    cp = &disc[SECTORSIZE 
			* (physical - SECTOROFFSET 
				+ (SECTORSPERTRACK 
					* (track - TRACKOFFSET)
				  )
			  )
	      ];
    for (i = 0; i < SECTORSIZE; i++) *cp++ = *sectordata++;
    if (++sector == SECTORSPERTRACK) {
	sector = 0;
	track++;
    }
}

void
writechar(int c)
{
    if ((bufptr - buf) == SECTORSIZE) {
	writesector(buf);
	bufptr = buf;
    }
    *bufptr++ = c;
}

void
flushsector(int c)
{
    int		i;

    for (i = (bufptr - buf); i < SECTORSIZE; i++) writechar(c);
    writesector(buf);
    bufptr = buf;
}

int
main(int argc, const char **argv)
{
    int		i, n, size, ext, block;
    int		drive, file, convert;
    int		infile  = 0, outfile = 0;
    const char	*fp_out;
    const char	*fp_in = NULL;
    const char	*opts;
    char	c, lastc, *cp;
    const char	*f;
    char	newname[128];
    struct stat	statbuf;
    unsigned char *dp;

    convert = file = drive = 0;

    if ((argc > 4) || (argc < 2)) usage();
    for (i = 0; i < (argc - 1); i ++) {
	opts = *++argv;
	if (*opts == '-') {
	    if (strlen(opts) != 2) usage();
	    if (drive || file) usage();
	    if (i != 0) usage();
	    convert = opts[1];
	    if ((convert != 'c') && (convert != 'b')) usage();
	} else {
	    if (!drive) {
		if ((strlen(opts) != 2) || (opts[1] != ':')) usage();
		if (!isupper(*opts)) {
		    printf("Disc filename must be single capital letter ");
		    printf("followed by semicolon (A: B: etc.)\n");
		    return 1;
		}
		strcpy(newname, "?-drive");
		newname[0] = opts[0];
		fp_out = newname;
		drive = 1;
		if ((outfile = open(fp_out, O_RDWR|O_CREAT|O_TRUNC, 0666)) < 0) {
		    printf("makecpm: cannot create \"%s\" : %d\n", fp_out, errno);
		    return 1;
		}
	    } else {
		fp_in = opts;
		file = 1;
		if ((infile = open(fp_in, O_RDONLY)) < 0) {
		    printf("makecpm: cannot open \"%s\" for reading\n", fp_in);
		    return 1;
		}
	    }
	}
    }

    convert = (convert == 'c');

    /* format disc */
    dp = disc;
    for (i = 0; i < DISCSIZE; i++) *dp++ = 0xE5;

    c = size = 0;
    ext = 0;
    block = (TOTALEXTENTS / EXTENTSPERSECTOR) / SECTORSPERBLOCK;
    track = RESERVEDTRACKS + TRACKOFFSET;
    sector = 0;
    if (file) {
	/* find file size */
	if (convert) {
	    do {
		lastc = c;
		n = read(infile, &c, 1);
		if (n == 1) {
		    if (convert && (lastc != 0xD) && (c == 0xA)) {
			size++;
		    }
		    size++;
		}
	    } while (n == 1);
	} else {
	    if (fstat(infile, &statbuf) < 0) {
		printf("makecpm: cannot get stats on input file\n");
		return 1;
	    }
	    size = statbuf.st_size;
	}

	if (size > DISCSIZE) {
	    printf("makecpm: file is to large.\n");
	    return 1;
	}

	/* build directory */
	f = fp_in + strlen(fp_in);
	while ((f != fp_in) && (*f != '/')) f--;
	if (*f == '/') f++;
	strcpy(newname, f);
	for (cp = newname; *cp; cp++) *cp = toupper(*cp);
	printf("%s => %s, size = %d\n", fp_in, newname, size);
	while(size > 0) {
		c = 0x00;
		writechar(c);
		for (i = 0, cp = newname; 
		     (i < 8) && (*cp != 0) && (*cp != '.'); 
		     i++, cp++) {
		    writechar(*cp);
		}
		while ((*cp != 0) && (*cp != '.')) cp++;
		c = ' ';
		while (i < 8) { writechar(c); i++; }
		if (*cp == '.') {
		    cp++;
		    while ((*cp != 0) && (i < 11)) {
			writechar(*cp++);
			i++;
		    }
		}
		while (i < 11) { writechar(c); i++; }
		c = ext;
		writechar(c);
		c = 0x00;
		writechar(c);
		writechar(c);
		if (size >= SECTORSIZE * SECTORSPEREXTENT)
		    c = SECTORSPEREXTENT;
		else
		    c = (size / SECTORSIZE) + ((size % SECTORSIZE) ? 1 : 0);
		writechar(c);
		for (i = 0; 
		     (i < 16) && (size > 0); 
		     i++, size -= SECTORSIZE * SECTORSPERBLOCK
		    ) {
		    c = block++;
		    writechar(c);
		}
		c = 0;
		for (; i < 16; i++) {
		    writechar(c);
		}
		ext++;
	}
	flushsector(0xE5);

	/* copy file */
	size = (TOTALEXTENTS + EXTENTSPERSECTOR - 1) / EXTENTSPERSECTOR;
	track = RESERVEDTRACKS + TRACKOFFSET + size / SECTORSPERTRACK;
	sector = size % SECTORSPERTRACK;

	close(infile);
	if ((infile = open(fp_in, O_RDONLY)) < 0) {
		printf("makecpm: cannot open \"%s\" for reading\n", fp_in);
		return 1;
	}
	if (convert) {
	    do {
		lastc = c;
		n = read(infile, &c, 1);
		if (n == 1) {
		    if ((lastc != 0xD) && (c == 0xA)) {
			lastc = 0xD;
			writechar(lastc);
		    }
		    writechar(c);
		}
	    } while (n == 1);
	    flushsector(0x1A);
	} else {
	    do {
		n = read(infile, &c, 1);
		if (n == 1)
		    writechar(c);
	    } while (n == 1);
	    flushsector(0x1A);
	}
    }

    /* finish writing disc file */
    if (write(outfile, disc, DISCSIZE) != DISCSIZE) {
	printf("makecpm: trouble writing output file\n");
    }
    close(outfile);
    printf("done\n");
    return 0;
}
