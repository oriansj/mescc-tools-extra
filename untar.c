/* Copyright (C) 2009 Tim Kientzle
 * Copyright (C) 2021 Jeremiah Orians
 * This file is part of mescc-tools-extra
 *
 * mescc-tools-extra is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mescc-tools-extra is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mescc-tools-extra.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * "untar" is an extremely simple tar extractor:
 *  * A single C source file, so it should be easy to compile
 *    and run on any system with a C compiler.
 *  * Extremely portable standard C.  The only non-ANSI function
 *    used is mkdir().
 *  * Reads basic ustar tar archives.
 *  * Does not require libarchive or any other special library.
 *
 * To compile: cc -o untar untar.c
 *
 * Usage:  untar <archive>
 *
 * In particular, this program should be sufficient to extract the
 * distribution for libarchive, allowing people to bootstrap
 * libarchive on systems that do not already have a tar program.
 *
 * To unpack libarchive-x.y.z.tar.gz:
 *    * gunzip libarchive-x.y.z.tar.gz
 *    * untar libarchive-x.y.z.tar
 *
 * Written by Tim Kientzle, March 2009.
 *
 * Released into the public domain.
 */

/* These are all highly standard and portable headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* This is for mkdir(); this may need to be changed for some platforms. */
#include <sys/stat.h>  /* For mkdir() */
#include "M2libc/bootstrappable.h"

int FUZZING;
int VERBOSE;
int STRICT;

/* Parse an octal number, ignoring leading and trailing nonsense. */
int parseoct(char const* p, size_t n)
{
	int i = 0;
	int h;

	while(((p[0] < '0') || (p[0] > '7')) && (n > 0))
	{
		p = p + 1;
		n = n - 1;
	}

	while((p[0] >= '0') && (p[0] <= '7') && (n > 0))
	{
		i = i << 3;
		h = p[0];
		i = i + h - 48;
		p = p + 1;
		n = n - 1;
	}

	return i;
}

/* Returns true if this is 512 zero bytes. */
int is_end_of_archive(char const* p)
{
	int n;

	for(n = 511; n >= 0; n = n - 1)
	{
		if(p[n] != 0)
		{
			return FALSE;
		}
	}

	return TRUE;
}

/* Create a directory, including parent directories as necessary. */
int create_dir(char *pathname, int mode)
{
	char *p;
	int r;
	int len;

	require(NULL != pathname, "untar: empty path in archive\n");
	len = strlen(pathname);
	require(0 < len, "untar: empty path in archive\n");

	/* Strip trailing '/' */
	if((1 < len) && (pathname[len - 1] == '/'))
	{
		pathname[len - 1] = '\0';
	}

	/* Try creating the directory. */
	if(!FUZZING)
	{
		r = mkdir(pathname, mode);

		if(r != 0)
		{
			/* On failure, try creating parent directory. */
			p = strrchr(pathname, '/');

			if(p != NULL)
			{
				p[0] = '\0';
				create_dir(pathname, 0755);
				p[0] = '/';
				r = mkdir(pathname, mode);
			}
		}

		if(r != 0)
		{
			fputs("Could not create directory ", stderr);
			fputs(pathname, stderr);
			fputc('\n', stderr);
			return FALSE;
		}
	}
	return TRUE;
}

/* Create a file, including parent directory as necessary. */
int create_file(char *pathname)
{
	if(FUZZING) return -1;
	int f;
	f = open(pathname, O_WRONLY|O_CREAT|O_TRUNC, 0600);

	if(f < 0)
	{
		/* Try creating parent dir and then creating file. */
		char *p = strrchr(pathname, '/');

		if(p != NULL)
		{
			p[0] = '\0';
			if(!create_dir(pathname, 0755))
			{
				p[0] = '/';
				return -1;
			}
			p[0] = '/';
			f = open(pathname, O_WRONLY|O_CREAT|O_TRUNC, 0600);
		}
	}

	return f;
}

/* Verify the tar checksum. */
int verify_checksum(char const* p)
{
	int n;
	int u = 0;
	unsigned h;

	for(n = 0; n < 512; n = n + 1)
	{
		/* Standard tar checksum adds unsigned bytes. */
		if((n < 148) || (n > 155))
		{
			h = p[n];
			u = u + h;
		}
		else
		{
			u = u + 0x20;
		}
	}

	int r = parseoct(p + 148, 8);

	return (u == r);
}

/* Extract a tar archive. */
int untar(FILE *a, char const* path)
{
	char* target = calloc(101, sizeof(char));
	char* buff = calloc(514, sizeof(char));
	int f = -1;
	size_t bytes_read;
	int bytes_written;
	int symlink_ret;
	int filesize;
	int op;
	if(VERBOSE)
	{
		fputs("Extracting from ", stdout);
		puts(path);
	}

	while(TRUE)
	{
		memset(buff, 0, 514);
		bytes_read = fread(buff, sizeof(char), 512, a);

		if(bytes_read < 512)
		{
			fputs("Short read on ", stderr);
			fputs(path, stderr);
			fputs(": expected 512, got ", stderr);
			fputs(int2str(bytes_read, 10, TRUE), stderr);
			fputc('\n', stderr);
			return FALSE;
		}

		if(is_end_of_archive(buff))
		{
			if(VERBOSE)
			{
				fputs("End of ", stdout);
				puts(path);
			}
			return TRUE;
		}

		if(!verify_checksum(buff))
		{
			fputs("Checksum failure\n", stderr);
			return FALSE;
		}

		filesize = parseoct(buff + 124, 12);

		/**
		 * Long linknames are stored in a special file with the name "././@LongLink"
		 * and are unsupported by this program.
		 */
		if (strcmp(buff, "././@LongLink") == 0) {
			fputs("unable to create long symlink\n", stderr);
			exit(EXIT_FAILURE);
		}

		op = buff[156];
		if('1' == op)
		{
			if(STRICT)
			{
				fputs("unable to create hardlinks\n", stderr);
				exit(EXIT_FAILURE);
			}
			fputs(" Ignoring hardlink ", stdout);
			puts(buff);
		}
		else if('2' == op)
		{
			memcpy(target, buff + 157, 100);
			target[100] = '\0';
			if(VERBOSE)
			{
				fputs(" Extracting file ", stdout);
				puts(buff);
			}
			if(!FUZZING) {
				symlink_ret = symlink(target, buff);
				if (symlink_ret != 0) {
					fputs("Failed to create symlink\n", stderr);
					if(STRICT) exit(EXIT_FAILURE);
				}
			}
		}
		else if('3' == op)
		{
			if(STRICT)
			{
				fputs("unable to create character devices\n", stderr);
				exit(EXIT_FAILURE);
			}
			fputs(" Ignoring character device ", stdout);
			puts(buff);
		}
		else if('4' == op)
		{
			if(STRICT)
			{
				fputs("unable to create block devices\n", stderr);
				exit(EXIT_FAILURE);
			}
			fputs(" Ignoring block device ", stdout);
			puts(buff);
		}
		else if('5' == op)
		{
			if(VERBOSE)
			{
				fputs(" Extracting dir ", stdout);
				puts(buff);
			}
			if(!create_dir(buff, parseoct(buff + 100, 8))) return FALSE;
			filesize = 0;
		}
		else if('6' == op)
		{
			if(STRICT)
			{
				fputs("unable to create FIFO\n", stderr);
				exit(EXIT_FAILURE);
			}
			fputs(" Ignoring FIFO ", stdout);
			puts(buff);
		}
		else
		{
			if(VERBOSE)
			{
				fputs(" Extracting file ", stdout);
				puts(buff);
			}
			f = create_file(buff);
			if((f < 0) && !FUZZING)
			{
				fputs("Failed to create file ", stderr);
				fputs(buff, stderr);
				fputc('\n', stderr);
				if(STRICT) return FALSE;
			}
		}

		while(filesize > 0)
		{
			bytes_read = fread(buff, 1, 512, a);

			if(bytes_read < 512)
			{
				fputs("Short read on ", stderr);
				fputs(path, stderr);
				fputs(": Expected 512, got ", stderr);
				puts(int2str(bytes_read, 10, TRUE));
				return FALSE;
			}

			if(filesize < 512)
			{
				bytes_read = filesize;
			}

			if(f >= 0)
			{
				if(!FUZZING)
				{
					bytes_written = write(f, buff, bytes_read);
					if(bytes_written != bytes_read)
					{
						fputs("Failed write\n", stderr);
						close(f);
						f = -1;
						if(STRICT) return FALSE;
					}
				}
			}

			filesize = filesize - bytes_read;
		}

		if(f >= 0)
		{
			if(0 != close(f))
			{
				fputs("Failed close\n", stderr);
				if(STRICT) return FALSE;
			}
			f = -1;
		}
	}
	return TRUE;
}

struct files_queue
{
	char* name;
	FILE* f;
	struct files_queue* next;
};

int main(int argc, char **argv)
{
	struct files_queue* list = NULL;
	struct files_queue* a;
	STRICT = TRUE;
	FUZZING = FALSE;
	int r;
	int ok = TRUE;
	int saw_file = FALSE;

	int i = 1;
	while (i < argc)
	{
		if(NULL == argv[i])
		{
			i = i + 1;
		}
		else if(match(argv[i], "-f") || match(argv[i], "--file"))
		{
			a = calloc(1, sizeof(struct files_queue));
			require(NULL != a, "failed to allocate enough memory to even get the file name\n");
			a->next = list;
			a->name = argv[i+1];
			require(NULL != a->name, "the --file option requires a filename to be given\n");
			saw_file = TRUE;
			a->f = fopen(a->name, "r");
			if(a->f == NULL)
			{
				fputs("Unable to open ", stderr);
				fputs(a->name, stderr);
				fputc('\n', stderr);
				if(STRICT) exit(EXIT_FAILURE);
			}
			else list = a;
			i = i + 2;
		}
		else if(match(argv[i], "--chaos") || match(argv[i], "--fuzz-mode") || match(argv[i], "--fuzzing"))
		{
			FUZZING = TRUE;
			fputs("fuzz-mode enabled, preparing for chaos\n", stderr);
			i = i + 1;
		}
		else if(match(argv[i], "-v") || match(argv[i], "--verbose"))
		{
			VERBOSE = TRUE;
			i = i + 1;
		}

		else if(match(argv[i], "--non-strict") || match(argv[i], "--bad-decisions-mode") || match(argv[i], "--drunk-mode"))
		{
			STRICT = FALSE;
			fputs("non-strict mode enabled, preparing for chaos\n", stderr);
			i = i + 1;
		}
		else if(match(argv[i], "-h") || match(argv[i], "--help"))
		{
			fputs("Usage: ", stderr);
			fputs(argv[0], stderr);
			fputs(" --file $input.gz\n", stderr);
			fputs("--verbose to print list of extracted files\n", stderr);
			fputs("--help to get this message\n", stderr);
			fputs("--fuzz-mode if you wish to fuzz this application safely\n", stderr);
			fputs("--non-strict if you wish to just ignore files not existing\n", stderr);
			exit(EXIT_SUCCESS);
		}
		else
		{
			fputs("Unknown option:", stderr);
			fputs(argv[i], stderr);
			fputs("\nAborting to avoid problems\n", stderr);
			exit(EXIT_FAILURE);
		}
	}

	/* Process the queue one file at a time */
	require(saw_file, "an input file (--file $name) must be provided\n");
	while(NULL != list)
	{
		r = untar(list->f, list->name);
		fputs("The extraction of ", stderr);
		fputs(list->name, stderr);
		if(r) fputs(" was successful\n", stderr);
		else
		{
			fputs(" produced errors\n", stderr);
			ok = FALSE;
		}
		fclose(list->f);
		list = list->next;
	}

	if(ok) return 0;
	return 1;
}
