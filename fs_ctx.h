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
 * CSC369 Assignment 1 - File system runtime context header file.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "options.h"
#include "a1fs.h"

#define ROOT_INODE 0

/**
 * Mounted file system runtime state - "fs context".
 */
typedef struct fs_ctx {
	/** Pointer to the start of the image. */
	void *image;
	/** Image size in bytes. */
	size_t size;

	// ADDED: useful runtime state of the mounted file system should be cached
	unsigned char *inode_bitmap;
	unsigned char *data_bitmap;
	a1fs_inode *inode_table;
	uint64_t data_block; // address of the first data block
	uint32_t* available_blocks; // a pointer to superblock->avaiable_inode
	uint32_t num_inodes;
	uint32_t* available_inodes; // a pointer to superblock->available_inode
	uint32_t num_of_data_blocks;

} fs_ctx;

/**
 * Initialize file system context.
 *
 * @param fs     pointer to the context to initialize.
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @return       true on success; false on failure (e.g. invalid superblock).
 */
bool fs_ctx_init(fs_ctx *fs, void *image, size_t size);

/**
 * Destroy file system context.
 *
 * Must cleanup all the resources created in fs_ctx_init().
 */
void fs_ctx_destroy(fs_ctx *fs);

/**
 * Return ceil(n)
 */
int roundup(double n);

/** Returns the inode number for the element at the end of the path
 * if it exists.
 *
 * Errors:
 *   - If a component of the path does not exist
 *   	or the path is not an absolute path, return -1.
 *   - If a component of the path prefix is not a directory, return -2.
 *
 */
int path_lookup(fs_ctx *fs, const char *path);

/**
 * Return the number of blocks allocated to the given file
 */
uint32_t get_num_blks_of_file(a1fs_inode ino);

/**
 * Return the exact number(with indirect block if possible) of blocks allocated to the given file
 */
uint32_t get_exact_num_blks_of_file(a1fs_inode ino);

/**
 * Check if bitmap[index] is available or not
 */
int is_bit_set(uint32_t index, unsigned char *bitmap);

/**
 * Return the number of the first available inode in inode_bitmap
 */
uint32_t get_first_available_position(uint32_t n, unsigned char *bitmap);

/**
 * Set the corresponding bit in the inode_bitmap to 1
 */
void set_bitmap(unsigned char *bitmap, uint32_t index);

/**
 * Unset the corresponding bit in the inode_bitmap
 */
void unset_bitmap(unsigned char *bitmap, uint32_t index);

/**
 * Get the path without the last component and store the result in buf
 */
void extract_parent_path(char *path, char *buf);

/**
 * Get the last component in path and store it in buf
 */
void extract_child_path(char *path, char *buf);

/**
 * Return the number of the last block of the given file/dirctory's inode in the image.
 * Precondition: the given file/directory should not be empty.
 */
uint32_t find_last_block(fs_ctx *fs, int inode_num);

/**
 * Return the address of the data block given the number of the data block.
 */
uint64_t get_addr_of_block(fs_ctx *fs, uint32_t block_num);

/**
 * Return the number of the data block given the address of the data block.
 */
uint32_t get_num_of_block(fs_ctx *fs, uint64_t block_addr);

/**
 * Return the address of the byte which we need to begin to write.
 * Precondition: offset % A1FS_BLOCK_SIZE != 0
 */
uint64_t get_addr_of_starting_write_point(fs_ctx *fs, uint32_t file_inode_num, uint64_t offset);

/**
 * growing the file by allocating one more blocks for it.
 * the growing part will be filled by 0.
 * It will return -1 if there is too much extent, or there has no enough blocks; otherwise return 0;
 */
int growing_a_block_for_file(fs_ctx *fs, uint32_t file_inode_num);