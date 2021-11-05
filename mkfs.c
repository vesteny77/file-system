/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"
#include "fs_ctx.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	// ADDED: check if the image already contains a valid a1fs superblock
	a1fs_superblock *superblock = (a1fs_superblock *)image;
	return superblock->magic == A1FS_MAGIC;
}

/**
 * Helper: round up n
 */
int roundup(double n) {
	if ((n - (double) ((int) n)) != 0.0) {
		return ((int) n) + 1;
	} else {
		return (int) n;
	}
}

/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	// ADDED: initialize the superblock and create an empty root directory
//	unsigned char *image_addr = (unsigned char *)image;
	a1fs_superblock *superblock = (a1fs_superblock *)image;
	superblock->magic = A1FS_MAGIC;
	superblock->size = size;
	superblock->num_inodes = (uint32_t) opts->n_inodes;
	superblock->available_inodes = superblock->num_inodes - 1;

	// set the address of inode_bitmap
	superblock->inode_bitmap = (uint64_t) (image + A1FS_BLOCK_SIZE);
	// set the corresponding bit of the root inode to 1
	unsigned char *bm = (unsigned char *) superblock->inode_bitmap;
	bm[0] |= 1;
	// compute the number of blocks that the inode bitmap takes
	superblock->inode_bitmap_length = (uint32_t) roundup(
			(double) superblock->num_inodes / A1FS_BLOCK_SIZE);

	// set the address of data bitmap
	superblock->data_bitmap = superblock->inode_bitmap
			+ superblock->inode_bitmap_length * A1FS_BLOCK_SIZE;

	superblock->available_blocks = (uint32_t) size / A1FS_BLOCK_SIZE - 1
			- superblock->inode_bitmap_length;
	// compute the number of blocks the data bitmap takes
	superblock->data_bitmap_length = (uint32_t) roundup(
			(double) superblock->available_blocks / A1FS_BLOCK_SIZE);

	// set the address of the inode table
	superblock->inode_table = superblock->data_bitmap
			+ superblock->data_bitmap_length * A1FS_BLOCK_SIZE;
	// compute the number of blocks that the inode table takes
	superblock->inode_table_length = (uint32_t) roundup(
			 (double) superblock->num_inodes * sizeof(a1fs_inode) / A1FS_BLOCK_SIZE);

	superblock->available_blocks -= superblock->data_bitmap_length;
	superblock->available_blocks -= superblock->inode_table_length;
//	superblock->reserved_inodes = 1;
//	superblock->reserved_blocks = 1 + superblock->inode_bitmap_length
//			+ superblock->data_bitmap_length + superblock->inode_table_length;

	// ADDED: check if mkfs will succeed
	uint32_t num_reserved_blocks = 1 + superblock->inode_bitmap_length
			+ superblock->data_bitmap_length + superblock->inode_table_length;
	if (num_reserved_blocks * A1FS_BLOCK_SIZE >= size || superblock->available_blocks <= 0) {
		return false;
	}

	// NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	// ADDED: configure root directory inode
	superblock->root_directory_inode = 0;
	a1fs_inode root_inode;
	root_inode.mode = S_IFDIR | 0777;
	root_inode.size = 0;
	root_inode.links = 2; // parent(which is itself) and itself
	if (clock_gettime(CLOCK_REALTIME, &root_inode.mtime) == -1) {
		perror("Set system time failed");
		exit(1);
	}
	root_inode.extent_num = 0;
	root_inode.num_dir_entry = 0;
    ((a1fs_inode *) superblock->inode_table)[0] = root_inode;
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
