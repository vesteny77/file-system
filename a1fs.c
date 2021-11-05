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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st) {
	(void) path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	st->f_bsize = A1FS_BLOCK_SIZE;
	st->f_frsize = A1FS_BLOCK_SIZE;

	// ADDED: assign metadata based on information in the superblock
	st->f_bfree = *(fs->available_blocks);
	st->f_bavail = *(fs->available_blocks);
	st->f_files = fs->num_inodes;
	st->f_ffree = *(fs->available_inodes);
	st->f_favail = *(fs->available_inodes);
	st->f_namemax = A1FS_NAME_MAX;
	return 0;

}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode.
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	// ADDED: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	int inode_num = path_lookup(fs, path);
	if (inode_num == -1) {
		return -ENOENT;
	}
	if (inode_num == -2) {
		return -ENOTDIR;
	}
	a1fs_inode inode_entry = fs->inode_table[inode_num];
	st->st_mode = inode_entry.mode;
	st->st_nlink = (nlink_t) inode_entry.links;
	st->st_size = inode_entry.size;
	st->st_blocks = get_exact_num_blks_of_file(inode_entry);
	st->st_mtim = inode_entry.mtime;

	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	// ADDED: lookup the directory inode for given path and iterate through its
	// directory entries
	int inode_num = path_lookup(fs, path);
	filler(buf, "." , NULL, 0);
	filler(buf, "..", NULL, 0);
	a1fs_inode *itable = fs->inode_table;
	uint32_t dir_count = 0;
	for (uint32_t i = 0; i < itable[inode_num].extent_num; i++) {
		a1fs_dentry *dir_entry_list = (a1fs_dentry *) (fs->data_block + ((a1fs_extent *) itable[inode_num].
                indirect_pt)[i].start * A1FS_BLOCK_SIZE);
        uint32_t num_dentries_in_extent = ((a1fs_extent *)itable[inode_num].indirect_pt)[i].count
                * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (uint32_t j = 0; j < num_dentries_in_extent; j++) {

			if (filler(buf, dir_entry_list[j].name, NULL, 0) == 1) {
			    return -ENOMEM;
			}

			dir_count++;

			// no more directory entries left
			if (dir_count >= itable[inode_num].num_dir_entry) {
				goto END;
			}
		}
	}
    END:
	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode) {
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	// ADDED: create a directory at given path with given mode
	// my comment: when we create a directory entry, we always add to the end of
	// the last existing extent if there's any space left

	// create a new directory
	a1fs_inode new_dir;
	new_dir.mode = mode;
	new_dir.links = 2; // by itself and its parent
	new_dir.size = 0;
	if (clock_gettime(CLOCK_REALTIME, &(new_dir.mtime)) == -1) {
		fprintf(stderr, "Set system time failed");
	}
	new_dir.extent_num = 0;
	new_dir.num_dir_entry = 0;
	if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        return -ENOSPC;
    }
	uint32_t new_inode = get_first_available_position(fs->num_inodes, fs->inode_bitmap);
	set_bitmap(fs->inode_bitmap, new_inode);
	*(fs->available_inodes) -= 1;
	fs->inode_table[new_inode] = new_dir;

	// modify information in the parent directory
    char parent_dir[A1FS_PATH_MAX] = {'\0'};
    extract_parent_path((char *) path, parent_dir);
    int inode_num = path_lookup(fs, parent_dir); // inode num of the parent directory


	if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[inode_num].mtime)) == -1) {
		fprintf(stderr, "Set system time failed");
	}

	// Add the new directory entry to the parent directory

	// when the parent is empty;
	if(fs->inode_table[inode_num].num_dir_entry == 0){ 
		// have to firstly allocate extent for parent
		if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        	return -ENOSPC;
    	}
		uint32_t new_single_indirect_block_num =
				get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
		set_bitmap(fs->data_bitmap, new_single_indirect_block_num);
		*(fs->available_blocks) -= 1;
		uint64_t new_single_indirect_block_addr =
				get_addr_of_block(fs, new_single_indirect_block_num);
		fs->inode_table[inode_num].indirect_pt = new_single_indirect_block_addr;

		// create new data block
		if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        	return -ENOSPC;
    	}
		uint32_t new_data_block = get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
		set_bitmap(fs->data_bitmap, new_data_block);
		*(fs->available_blocks) -= 1;
		((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[0].start = new_data_block;
		((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[0].count = 1;
		a1fs_dentry* new_data_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_data_block);
		

		//create new entry 
		a1fs_dentry new_entry;
		new_entry.ino = (a1fs_ino_t)new_inode;
		extract_child_path((char*)path, new_entry.name);
		new_data_block_addr[0] = new_entry; 

		// update parent's info
		fs->inode_table[inode_num].size += sizeof(a1fs_dentry);
		fs->inode_table[inode_num].links++;
		fs->inode_table[inode_num].num_dir_entry++;
		fs->inode_table[inode_num].extent_num++;
		return 0;
	}

	// the parent is not empty when reach here.
	uint32_t last_block = find_last_block(fs,inode_num); // last block's number
	if(fs->inode_table[inode_num].size % A1FS_BLOCK_SIZE != 0){
		// if the parents' last block is not full
		uint64_t num_of_entry_in_last_block = (fs->inode_table[inode_num].num_dir_entry) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));// number of entry in last block

		a1fs_dentry new_entry;
		new_entry.ino = (a1fs_ino_t)new_inode;
		extract_child_path((char*)path, new_entry.name);
		a1fs_dentry* last_block_addr = (a1fs_dentry*)get_addr_of_block(fs, last_block);
		last_block_addr[num_of_entry_in_last_block] = new_entry;
	}else{
		// if the parents' last block is full
		if(is_bit_set(last_block + 1, fs->data_bitmap) == 0){ // the next block is free
			uint32_t new_block_num = last_block + 1; // new block's number
			set_bitmap(fs->data_bitmap, new_block_num);
			*(fs->available_blocks) -= 1;
			a1fs_dentry* new_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_block_num); // new block's address
			a1fs_dentry new_entry;
			new_entry.ino = (a1fs_ino_t)new_inode;
			extract_child_path((char*)path, new_entry.name);
			new_block_addr[0] = new_entry;

			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num - 1].count++;
		}else{ // the next block is not free

			// create new block
			if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        		return -ENOSPC;
    		}
			uint32_t new_data_block_num = (uint32_t)get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
			set_bitmap(fs->data_bitmap, new_data_block_num);
			*(fs->available_blocks) -= 1;

			// create new extent
			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num].start = new_data_block_num;
			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num].count = 1;

			// create entry
			a1fs_dentry new_entry;
			new_entry.ino = (a1fs_ino_t)new_inode;
			extract_child_path((char*)path, new_entry.name);

			a1fs_dentry* new_data_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_data_block_num);
			new_data_block_addr[0] = new_entry;
			

			fs->inode_table[inode_num].extent_num++;
		}
	}

	// update parent's info
	fs->inode_table[inode_num].size += sizeof(a1fs_dentry);
	fs->inode_table[inode_num].links++;
	fs->inode_table[inode_num].num_dir_entry++;
	

	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	// ADDED: remove the directory at given path (only if it's empty)

	char parent_dir[A1FS_PATH_MAX] = {'\0'};
    extract_parent_path((char *) path, parent_dir);
    uint32_t parent_inode_num = (uint32_t)path_lookup(fs, parent_dir);
	
	// firstly change the modification time of the parent dirctory
	if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[parent_inode_num].mtime)) == -1) {
		fprintf(stderr, "Set system time failed");
	}

	uint32_t target_dir_inode_num = (uint32_t)path_lookup(fs, path);
	
	// check if the target dir is empty 
	if(fs->inode_table[target_dir_inode_num].size != 0){
		return -ENOTEMPTY; 
	}

	// the target is empty
	// firstly  place (the last dentry in the dir) on the place of the target dentry.

	// pick the last entry
	uint32_t parent_last_block_num = find_last_block(fs, parent_inode_num);
	uint32_t num_of_entry_in_last_block = (fs->inode_table[parent_inode_num].num_dir_entry) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)); // number of entry in last block
	if(num_of_entry_in_last_block == 0){
		num_of_entry_in_last_block = A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
	}

	a1fs_dentry* parent_last_block_addr = (a1fs_dentry*)get_addr_of_block(fs, parent_last_block_num);
	a1fs_dentry parent_last_dentry = parent_last_block_addr[num_of_entry_in_last_block - 1];

	a1fs_dentry target_dentry;

	if(parent_last_dentry.ino == target_dir_inode_num){ // the last dentry is the target dentry, no need to replace
		target_dentry = parent_last_dentry;
		goto NEXT;
	}
	
	// find the target dentry, then hold it, then place the last dentry on the place of the target dentry
	
	for(uint32_t i = 0; i < fs->inode_table[parent_inode_num].extent_num; i++){ // traverse each extent
		a1fs_extent temp_extent = ((a1fs_extent *)fs->inode_table[parent_inode_num].indirect_pt)[i];
		for(uint32_t j = 0; j < temp_extent.count; j++){ // traverse each block
			uint64_t temp_block_addr = get_addr_of_block(fs, temp_extent.start + j); 
			for(uint32_t k = 0; k < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); k++){ // traverse each dentry
				a1fs_dentry temp_dentry = ((a1fs_dentry*)temp_block_addr)[k];
				if(temp_dentry.ino == target_dir_inode_num){ // find the target dentry
					target_dentry = temp_dentry; // hold the target dentry
					((a1fs_dentry*)temp_block_addr)[k] = parent_last_dentry; // place the last dentry on the place of the target dentry
					goto NEXT;
				}
			}
		}
	}

	NEXT: // at this time we have target_dentry and it place have been replaced by the last dentry.
	
	// when the target entry is the only entry in parent
	if(fs->inode_table[parent_inode_num].num_dir_entry == 1){
		
		// firstly clean up itself
		unset_bitmap(fs->inode_bitmap, target_dentry.ino); // it is empty so no data blocks needs to be free, the only thing is to unset inode_bitmap
		*fs->available_inodes += 1;
		// then clean up its parent
		
		// parent's data block need to be cleaned
		uint32_t parent_indirect_block_num = get_num_of_block(fs, (uint64_t)(fs->inode_table[parent_inode_num].indirect_pt));
		unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
		*fs->available_blocks += 1;
		unset_bitmap(fs->data_bitmap, parent_indirect_block_num); // clean the indirect pointer
		*fs->available_blocks += 1;

		// parent's data need to be cleaned, it should be totally empty
		fs->inode_table[parent_inode_num].extent_num = 0;
		fs->inode_table[parent_inode_num].links = 2;
		fs->inode_table[parent_inode_num].num_dir_entry = 0;
		fs->inode_table[parent_inode_num].size = 0;
			

	}else{ // it is not the only entry in parent
		if(fs->inode_table[parent_inode_num].num_dir_entry % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)) != 1){ 
			// the last dentry of the parent is not the first dentry inside a data block, so no need to unset any data block

			unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
			*fs->available_inodes += 1;

			// set parent's data
			fs->inode_table[parent_inode_num].links -= 1;
			fs->inode_table[parent_inode_num].num_dir_entry -= 1;
			fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);

		}else{ // the last dentry of the parent is the first dentry of a datablock, we need to free 1 data block
			uint32_t parent_extent_num = fs->inode_table[parent_inode_num].extent_num; // how many extents are there in the parent

			if(((a1fs_extent *)fs->inode_table[parent_inode_num].indirect_pt)[parent_extent_num - 1].count > 1){
				// the last extent's count > 1, so no need to free extent, but have to make extent.count--

				unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
				*fs->available_inodes += 1;

				// free the last data block of the parent
				unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
				*fs->available_blocks += 1;
				((a1fs_extent *)fs->inode_table[parent_inode_num].indirect_pt)[parent_extent_num - 1].count -= 1;

				// modify parent's data
				fs->inode_table[parent_inode_num].links -= 1;
				fs->inode_table[parent_inode_num].num_dir_entry -= 1;
				fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);

			}else{ // the last extent's count == 1, so need to free one extent

				unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
				*fs->available_inodes += 1;
				
				// free the last block of the parent
				unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
				*fs->available_blocks += 1;

				//free the extent
				fs->inode_table[parent_inode_num].extent_num -= 1;

				// modify parent's data
				fs->inode_table[parent_inode_num].links -= 1;
				fs->inode_table[parent_inode_num].num_dir_entry -= 1;
				fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);
			}			
		}
	
	}

	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();


	// ADDED: create a file at given path with given mode
	if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        return -ENOSPC;
    }
	a1fs_inode new_dir;
	new_dir.mode = mode;
	new_dir.links = 1; // by itself and its parent
	new_dir.size = 0;
	new_dir.extent_num = 0;
	new_dir.num_dir_entry = 0;
	if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        return -ENOSPC;
    }
	uint32_t new_inode = get_first_available_position(fs->num_inodes, fs->inode_bitmap);
	set_bitmap(fs->inode_bitmap, new_inode);
	*(fs->available_inodes) -= 1;
	fs->inode_table[new_inode] = new_dir;

	// modify information in the parent directory
    char parent_dir[A1FS_NAME_MAX] = {'\0'};
    extract_parent_path((char *) path, parent_dir);
    int inode_num = path_lookup(fs, parent_dir); // inode num of the parent directory


	if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[inode_num].mtime)) == -1) {
		fprintf(stderr, "Set system time failed");
	}

	// add the directory entry in the parent directory, possibly allocating new blocks for it

	// when the parent is empty;
	if(fs->inode_table[inode_num].num_dir_entry == 0){ 
		// have to firstly allocate extent for parent
		if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        	return -ENOSPC;
    	}
		uint32_t new_single_indirect_block_num =
				get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
		set_bitmap(fs->data_bitmap, new_single_indirect_block_num);
		*(fs->available_blocks) -= 1;
		uint64_t new_single_indirect_block_addr =
				get_addr_of_block(fs, new_single_indirect_block_num);
		fs->inode_table[inode_num].indirect_pt = new_single_indirect_block_addr;

		// create new data block
		if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        	return -ENOSPC;
    	}
		uint32_t new_data_block = get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
		set_bitmap(fs->data_bitmap, new_data_block);
		*(fs->available_blocks) -= 1;
		((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[0].start = new_data_block;
		((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[0].count = 1;
		a1fs_dentry* new_data_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_data_block);
		
		

		//create new entry 
		a1fs_dentry new_entry;
		new_entry.ino = (a1fs_ino_t)new_inode;
		extract_child_path((char*)path, new_entry.name);
		new_data_block_addr[0] = new_entry; 

		// update parent's info
		fs->inode_table[inode_num].size += sizeof(a1fs_dentry);
		fs->inode_table[inode_num].num_dir_entry++;
		fs->inode_table[inode_num].extent_num++;
		return 0;
	}

	// the parent is not empty when reach here.
	uint32_t last_block = find_last_block(fs,inode_num); // last block's number
	if(fs->inode_table[inode_num].size % A1FS_BLOCK_SIZE != 0){
		// if the parents' last block is not full
		uint64_t num_of_entry_in_last_block = (fs->inode_table[inode_num].num_dir_entry) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));// number of entry in last block

		a1fs_dentry new_entry;
		new_entry.ino = (a1fs_ino_t)new_inode;
		extract_child_path((char*)path, new_entry.name);
		a1fs_dentry* last_block_addr = (a1fs_dentry*)get_addr_of_block(fs, last_block);
		last_block_addr[num_of_entry_in_last_block] = new_entry;
	}else{
		// if the parents' last block is full
		if(is_bit_set(last_block + 1, fs->data_bitmap) == 0){ // the next block is free
			uint32_t new_block_num = last_block + 1; // new block's number
			set_bitmap(fs->data_bitmap, new_block_num);
			*(fs->available_blocks) -= 1;
			a1fs_dentry* new_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_block_num); // new block's address
			a1fs_dentry new_entry;
			new_entry.ino = (a1fs_ino_t)new_inode;
			extract_child_path((char*)path, new_entry.name);
			new_block_addr[0] = new_entry;

			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num - 1].count++;
		}else{ // the next block is not free

			// create new block
			if (*(fs->available_inodes) == 0 || *(fs->available_blocks) == 0) {
        		return -ENOSPC;
    		}
			uint32_t new_data_block_num = (uint32_t)get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
			set_bitmap(fs->data_bitmap, new_data_block_num);
			*(fs->available_blocks) -= 1;

			// create new extent
			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num].start = new_data_block_num;
			((a1fs_extent *)fs->inode_table[inode_num].indirect_pt)[fs->inode_table[inode_num].extent_num].count = 1;

			// create entry
			a1fs_dentry new_entry;
			new_entry.ino = (a1fs_ino_t)new_inode;
			extract_child_path((char*)path, new_entry.name);

			a1fs_dentry* new_data_block_addr = (a1fs_dentry*)get_addr_of_block(fs, new_data_block_num);
			new_data_block_addr[0] = new_entry;
			

			fs->inode_table[inode_num].extent_num++;
		}
	}

	// update parent's info
	fs->inode_table[inode_num].size += sizeof(a1fs_dentry);
	fs->inode_table[inode_num].num_dir_entry++;
	

	return 0;

}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	// remove the file at given path
	// Luke
	char parent_dir[A1FS_PATH_MAX] = {'\0'};
    extract_parent_path((char *) path, parent_dir);
    uint32_t parent_inode_num = (uint32_t)path_lookup(fs, parent_dir);
	
	// firstly change the modification time of the parent dirctory
	if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[parent_inode_num].mtime)) == -1) {
		fprintf(stderr, "Set system time failed");
	}

	uint32_t target_inode_num = (uint32_t)path_lookup(fs, path);
	

	if(fs->inode_table[target_inode_num].size != 0){ // if the target file is not empty
		// clean up the target file's data block and indirect pointer. ie. makes the file to be an empty file

		for(uint32_t i = 0; i < fs->inode_table[target_inode_num].extent_num; i++){ // traverse through each extent
			a1fs_extent this_extent = ((a1fs_extent *)fs->inode_table[target_inode_num].indirect_pt)[i];
			for(uint32_t j = 0; j < (uint32_t)this_extent.count; j++){ // traverse through each block and free them
				unset_bitmap(fs->data_bitmap, this_extent.start + j);
				*fs->available_blocks += 1;
			}
		}

		// free target file's indirect block
		uint32_t target_file_indirect_num = get_num_of_block(fs, (uint64_t)(fs->inode_table[target_inode_num].indirect_pt));
		unset_bitmap(fs->data_bitmap, target_file_indirect_num);
		*fs->available_blocks += 1;

		// set file's stat
		fs->inode_table[target_inode_num].extent_num = 0;
		fs->inode_table[target_inode_num].size = 0;

	}


	// the target is empty
	// first place (the last dentry in the dir) on the place of the target dentry.

	// pick the last entry
	uint32_t parent_last_block_num = find_last_block(fs, parent_inode_num);
	uint32_t num_of_entry_in_last_block = (fs->inode_table[parent_inode_num].num_dir_entry) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)); // number of entry in last block
	if(num_of_entry_in_last_block == 0){
		num_of_entry_in_last_block = A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
	}
	a1fs_dentry* parent_last_block_addr = (a1fs_dentry*)get_addr_of_block(fs, parent_last_block_num);
	a1fs_dentry parent_last_dentry = parent_last_block_addr[num_of_entry_in_last_block - 1];

	a1fs_dentry target_dentry;

	if(parent_last_dentry.ino == target_inode_num){ // the last dentry is the target dentry, no need to replace
		target_dentry = parent_last_dentry;
		goto COUNTINUE;
	}
	
	// find the target dentry, then hold it, then place the last dentry on the place of the target dentry
	
	for(uint32_t i = 0; i < fs->inode_table[parent_inode_num].extent_num; i++){ // traverse each extent
		a1fs_extent temp_extent = ((a1fs_extent *)fs->inode_table[parent_inode_num].indirect_pt)[i];
		for(uint32_t j = 0; j < temp_extent.count; j++){ // traverse each block
			uint64_t temp_block_addr = get_addr_of_block(fs, temp_extent.start + j); 
			for(uint32_t k = 0; k < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); k++){ // traverse each dentry
				a1fs_dentry temp_dentry = ((a1fs_dentry*)temp_block_addr)[k];
				if(temp_dentry.ino == target_inode_num){ // find the target dentry
					target_dentry = temp_dentry; // hold the target dentry
					((a1fs_dentry*)temp_block_addr)[k] = parent_last_dentry; // place the last dentry on the place of the target dentry
					goto COUNTINUE;
				}
			}
		}
	}

	COUNTINUE: // at this time we have target_dentry and it place have been replaced by the last dentry.
	
	// when the target entry is the only entry in parent
	if(fs->inode_table[parent_inode_num].num_dir_entry == 1){
		
		// firstly clean up itself
		unset_bitmap(fs->inode_bitmap, target_dentry.ino); // it is empty so no data blocks needs to be free, the only thing is to unset inode_bitmap
		*fs->available_inodes += 1;
		// then clean up its parent
		
		// parent's data block need to be cleaned
		uint32_t parent_indirect_block_num = get_num_of_block(fs, (uint64_t)(fs->inode_table[parent_inode_num].indirect_pt));
		unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
		*fs->available_blocks += 1;
		unset_bitmap(fs->data_bitmap, parent_indirect_block_num); // clean the indirect pointer
		*fs->available_blocks += 1;

		// parent's data need to be cleaned, it should be totally empty
		fs->inode_table[parent_inode_num].extent_num = 0;
		fs->inode_table[parent_inode_num].links = 2;
		fs->inode_table[parent_inode_num].num_dir_entry = 0;
		fs->inode_table[parent_inode_num].size = 0;
			

	}else{ // it is not the only entry in parent
		if(fs->inode_table[parent_inode_num].num_dir_entry % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)) != 1){ 
			// the last dentry of the parent is not the first dentry inside a data block, so no need to unset any data block

			unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
			*fs->available_inodes += 1;

			// set parent's data
			fs->inode_table[parent_inode_num].num_dir_entry -= 1;
			fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);

		}else{ // the last dentry of the parent is the first dentry of a datablock, we need to free 1 data block
			uint32_t parent_extent_num = fs->inode_table[parent_inode_num].extent_num; // how many extents are there in the parent

			if(((a1fs_extent *)fs->inode_table[parent_inode_num].indirect_pt)[parent_extent_num - 1].count > 1){
				// the last extent's count > 1, so no need to free extent, but have to make extent.count--

				unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
				*fs->available_inodes += 1;

				// free the last data block of the parent
				unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
				*fs->available_blocks += 1;
				((a1fs_extent *) fs->inode_table[parent_inode_num].indirect_pt)[parent_extent_num - 1].count -= 1;

				// modify parent's data
				fs->inode_table[parent_inode_num].num_dir_entry -= 1;
				fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);

			}else{ // the last extent's count == 1, so need to free one extent

				unset_bitmap(fs->inode_bitmap, target_dentry.ino); // clean up the target dir's inode
				*fs->available_inodes += 1;
				
				// free the last block of the parent
				unset_bitmap(fs->data_bitmap, parent_last_block_num); // clean the block
				*fs->available_blocks += 1;

				//free the extent
				fs->inode_table[parent_inode_num].extent_num -= 1;

				// modify parent's data
				fs->inode_table[parent_inode_num].num_dir_entry -= 1;
				fs->inode_table[parent_inode_num].size -= sizeof(a1fs_dentry);
			}			
		}
	
	}

	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	// ADDED: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page

	int inode_num = path_lookup(fs, path);
    if (times == NULL) {
        clock_gettime(CLOCK_REALTIME, &(fs->inode_table[inode_num].mtime));
        return 0;
    }
    fs->inode_table[inode_num].mtime = times[1];
    return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	// ADDED: set new file size, possibly "zeroing out" the uninitialized range

	uint32_t file_inode_num = (uint32_t) path_lookup(fs, path);
	uint64_t file_original_size = fs->inode_table[file_inode_num].size;


	if((uint64_t) size == file_original_size){

		return 0;

	}else if((uint64_t) size < file_original_size){


		uint32_t new_file_block_count;
		if(size % A1FS_BLOCK_SIZE == 0){
			new_file_block_count = (uint32_t) size / A1FS_BLOCK_SIZE;
		}else{
			new_file_block_count = (uint32_t) size / A1FS_BLOCK_SIZE + 1;
		}


		
		uint32_t target_extent_index = 0; // the extent that we are keeping
		uint32_t count = 0;
		uint32_t target_block_num_in_extent = 1;
		a1fs_extent temp_extent;
		for(uint32_t i = 0; i < fs->inode_table[file_inode_num].extent_num; i++) { // traverse each extent
			temp_extent = ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[i];
			if(temp_extent.count + count >= new_file_block_count){ // this is the target extent
				target_extent_index = i;
				target_block_num_in_extent = new_file_block_count - count; // this is the target block's order in the extent
				goto FOUND;
			}
			count += temp_extent.count;

			
		}
		FOUND: // we have found the target block and extent

		// free other blocks

		// free the other blocks in the target extent
		for(uint32_t i = target_block_num_in_extent; i < temp_extent.count; i++){
			unset_bitmap(fs->data_bitmap,temp_extent.start + i - 1);
			*fs->available_blocks += 1;
		}
		((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[target_extent_index].count = target_block_num_in_extent;

		// free other blocks
		for(uint32_t i = target_extent_index + 1; i < fs->inode_table[file_inode_num].extent_num; i++){ //traverse these extents

			for(uint32_t j = 0; j < ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[i].count; j++){ // traverse each blocks
				unset_bitmap((unsigned char *) fs->data_block, ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[i].start + j); // free them
				*fs->available_blocks += 1;
			}

		}

		// free the extents
		fs->inode_table[file_inode_num].extent_num = target_extent_index + 1;

		// update size and mtime
		fs->inode_table[file_inode_num].size = (uint64_t) size;

		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}

		return 0;


		

	}else{ // when we have to extend the file

		uint32_t original_file_block_count = get_num_blks_of_file(fs->inode_table[file_inode_num]);
		
		uint32_t new_file_block_count;
		if(size % A1FS_BLOCK_SIZE == 0){
			new_file_block_count = (uint32_t) size / A1FS_BLOCK_SIZE;
		}else{
			new_file_block_count = (uint32_t) size / A1FS_BLOCK_SIZE + 1;
		}

		uint32_t num_blocks_we_have_to_add = new_file_block_count - original_file_block_count;

		for(uint32_t i = 0; i < num_blocks_we_have_to_add; i++){
			
			if(growing_a_block_for_file(fs, file_inode_num) == -1){
				return -ENOSPC;
			}

		}

		// update size and mtime
		fs->inode_table[file_inode_num].size = (uint64_t) size;

		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}

		return 0;
	}

}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	// ADDED: read data from the file at given offset into the buffer
	int inode_num = path_lookup(fs, path);
	a1fs_inode inode = fs->inode_table[inode_num];
	// return 0 when offset is beyond EOF
	if ((uint32_t) offset >= inode.size) {
		return 0;
	}

	int ret;
	if ((uint32_t) offset + size > inode.size) { // offset + size is beyond EOF
		ret = (int) (inode.size - (uint32_t) offset);
	} else { // offset and size are both valid
		ret = (int) size;
	}

	uint64_t offset_in_blk = (uint64_t) offset % A1FS_BLOCK_SIZE; // bytes
	off_t offset_copy = (uint64_t) offset;
	uint32_t extent_index = 0;

	// get the index of the extent at which the offset is located
	for (uint32_t i = 0; i < inode.extent_num; i++) {
		if (offset_copy - ((a1fs_extent *)(inode.indirect_pt))[i].count * A1FS_BLOCK_SIZE < 0) {
			extent_index = i;
			break;
		}
		offset_copy -= ((a1fs_extent *) inode.indirect_pt)[i].count * A1FS_BLOCK_SIZE;
	}

	// get the index of the block in the acquired extent at which the offset is located
	uint32_t blk_index_in_extent = 0;
	for (uint32_t j = 0; j < ((a1fs_extent *) inode.indirect_pt)[extent_index].count; j++) {
		if (offset_copy - A1FS_BLOCK_SIZE < 0) {
			blk_index_in_extent = j;
			break;
		}
		offset_copy -= A1FS_BLOCK_SIZE;
	}

	char *data_to_read = (char *) ((uint64_t) (((a1fs_extent *)inode.indirect_pt)[extent_index].start
			+ blk_index_in_extent) * A1FS_BLOCK_SIZE + fs->data_block + offset_in_blk);
	memcpy(buf, data_to_read, ret);
	return ret;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	// ADDED: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	// Luke

	if(size == 0){
		return 0;
	}

	// init essential block
	uint32_t file_inode_num = (uint32_t) path_lookup(fs,path);
	uint64_t file_size = fs->inode_table[file_inode_num].size;


	// the most normal case, size + offset <= file_size ie.(case 4)
	if(size + offset <= file_size){
		// find the address where we need to begin to write data

		// offset % A1FS_BLOCK_SIZE must != 0, since size != 0 at this point.
		char* addr_of_write_begin = (char *)get_addr_of_starting_write_point(fs, file_inode_num, (uint64_t) offset);

		memcpy(addr_of_write_begin, buf, size); // write in the data
		// update mtime
		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}
		goto END_WRITE;
	}

	// file_size < size + offset <= num_of_blocks_in_the_file * block_size (case 5 and 6)
	if(size + offset <= A1FS_BLOCK_SIZE * get_num_blks_of_file(fs->inode_table[file_inode_num])){

		// offset % A1FS_BLOCK_SIZE must != 0, since size != 0 at this point.
		char* addr_of_write_begin = (char *)get_addr_of_starting_write_point(fs, file_inode_num, (uint64_t) offset);
		
		if((uint64_t) offset <= file_size){ // (case 5)
			memset(addr_of_write_begin, '\0', A1FS_BLOCK_SIZE * get_num_blks_of_file(fs->inode_table[file_inode_num]) - offset); // firstly zero all bytes after offset inside that block.
			memcpy(addr_of_write_begin, buf, size); // write in the data
			
		
		}else{ // (case 6)
		    // get the index of the last block of the file
			uint32_t file_last_block = find_last_block(fs, file_inode_num);
            uint64_t file_last_block_addr = get_addr_of_block(fs, file_last_block);

			char* hole_begin = (char*)(file_last_block_addr + A1FS_BLOCK_SIZE - (A1FS_BLOCK_SIZE * get_num_blks_of_file(fs->inode_table[file_inode_num]) - file_size));
			memset(hole_begin, '\0', A1FS_BLOCK_SIZE * get_num_blks_of_file(fs->inode_table[file_inode_num]) - file_size); // firslty zero all bytes after offset inside that block.
			memcpy(addr_of_write_begin, buf, size); // write in the data

		}

		// upadate file size and mtime
		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}
		fs->inode_table[file_inode_num].size = size + offset;
		goto END_WRITE;

	}
	
	// num_of_blocks_in_the_file * block_size < size + offset <= (num_of_blocks_in_the_file + 1) * block_size (case 1, 2)
	if(size + offset <= A1FS_BLOCK_SIZE * (1 + get_num_blks_of_file(fs->inode_table[file_inode_num])) ){

		if(growing_a_block_for_file(fs, file_inode_num) == -1){
			return -ENOSPC;
		}


		char* current_last_block_addr = (char*)get_addr_of_block(fs, find_last_block(fs, file_inode_num));
		char* write_begin = current_last_block_addr + offset % A1FS_BLOCK_SIZE;
		memcpy(write_begin, buf, size);

		// update file size and mtime
		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}
		fs->inode_table[file_inode_num].size = size + offset;
		goto END_WRITE;

	}

    // size + offset > (num_of_blocks_in_the_file + 1) * block_size (case 3)
	if(size + offset > A1FS_BLOCK_SIZE * (1 + get_num_blks_of_file(fs->inode_table[file_inode_num])) ){

		if(growing_a_block_for_file(fs, file_inode_num) == -1){
			return -ENOSPC;
		}

		if(growing_a_block_for_file(fs, file_inode_num) == -1){
			return -ENOSPC;
		}

		char* begin_write = (char*)get_addr_of_block(fs, find_last_block(fs, file_inode_num));
		memcpy(begin_write, buf, size);

		// update file size and mtime
		if (clock_gettime(CLOCK_REALTIME, &(fs->inode_table[file_inode_num].mtime)) == -1) {
			fprintf(stderr, "Set system time failed");
		}
		fs->inode_table[file_inode_num].size = size + offset;
		goto END_WRITE;

	}

	END_WRITE:
	return (int)size;

}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
