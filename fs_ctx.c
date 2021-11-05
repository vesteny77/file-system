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
 * CSC369 Assignment 1 - File system runtime context implementation.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs_ctx.h"
#include "a1fs.h"


bool fs_ctx_init(fs_ctx *fs, void *image, size_t size) {
    fs->image = image;
    fs->size = size;

    // ADDED: check if the file system image can be mounted
    a1fs_superblock *superblock = (a1fs_superblock *) image;
    if (superblock->magic != A1FS_MAGIC) {
        return false;
    }

    // and initialize its runtime state
    fs->inode_bitmap = (unsigned char *) (uint64_t) image + A1FS_BLOCK_SIZE;
    fs->data_bitmap = (unsigned char *) (uint64_t) image + A1FS_BLOCK_SIZE
                      + superblock->inode_bitmap_length * A1FS_BLOCK_SIZE;
    fs->inode_table = (a1fs_inode *) (uint64_t) (image + A1FS_BLOCK_SIZE
            + superblock->inode_bitmap_length * A1FS_BLOCK_SIZE
            + superblock->data_bitmap_length * A1FS_BLOCK_SIZE);
    fs->data_block = (uint64_t) (image + A1FS_BLOCK_SIZE
                           + superblock->inode_bitmap_length * A1FS_BLOCK_SIZE
                           + superblock->data_bitmap_length * A1FS_BLOCK_SIZE
                           + superblock->inode_table_length * A1FS_BLOCK_SIZE);
    fs->available_blocks = &(superblock->available_blocks);
    fs->num_inodes = superblock->num_inodes;
    fs->available_inodes = &(superblock->available_inodes);
    fs->num_of_data_blocks = superblock->available_blocks;
    return true;
}

void fs_ctx_destroy(fs_ctx *fs) {
    // ADDED: cleanup any resources allocated in fs_ctx_init()
    (void) fs;
}

/* ==========================
 * HELPER FUNCTIONS
 * ==========================
 */

int roundup(double n) {
    if ((n - (double) ((int) n)) != 0.0) {
        return ((int) n) + 1;
    } else {
        return (int) n;
    }
}

int path_lookup(fs_ctx *fs, const char *path) {
    if (path[0] != '/') {
        fprintf(stderr, "Not an absolute path\n");
        return -1;
    }

    a1fs_inode *itable = fs->inode_table;

    char *token = strtok((char *) path, "/");

    // if the path contains only the root directory, i.e. "/"
    if (token == NULL) {
        return ROOT_INODE;
    }

    // otherwise, get the entries of the root directory
    int tmp_inode = -1;
    uint32_t dir_count = 0;

    for (uint32_t i = 0; i < itable[ROOT_INODE].extent_num; i++) {
        a1fs_dentry *dir_entry_list = (a1fs_dentry *) (fs->data_block + ((a1fs_extent *)itable[ROOT_INODE].
                indirect_pt)[i].start * A1FS_BLOCK_SIZE);
        uint32_t num_dentries_in_extent = ((a1fs_extent *)itable[ROOT_INODE].indirect_pt)[i].count
                * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
        for (uint32_t j = 0; j < num_dentries_in_extent; j++) {

            if (strcmp(token, dir_entry_list[j].name) == 0) {
                tmp_inode = dir_entry_list[j].ino;
                goto NEXT;
            }

            dir_count++;

            // no more directory entries left
            if (dir_count >= itable[ROOT_INODE].num_dir_entry) {
                return -1;
            }
        }
    }

    NEXT:
    token = strtok(NULL, "/");

    while (token != NULL) {
        // parent directory is empty
        if (itable[tmp_inode].size == 0) {
            return -1;
        }

        // file in the middle of the path
        if (S_ISREG(itable[tmp_inode].mode)) {
            return -2;
        }

        dir_count = 0;

        for (uint32_t i = 0; i < itable[tmp_inode].extent_num; i++) {
            a1fs_dentry *dir_entry_list = (a1fs_dentry *) (fs->data_block + ((a1fs_extent *)itable[tmp_inode].
                    indirect_pt)[i].start * A1FS_BLOCK_SIZE);
            uint32_t num_dentries_in_extent = ((a1fs_extent *)itable[tmp_inode].indirect_pt)[i].count
                    * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
            for (uint32_t j = 0; j < num_dentries_in_extent; j++) {

                if (strcmp(token, dir_entry_list[j].name) == 0) {
                    tmp_inode = dir_entry_list[j].ino;
                    goto UPDATE;
                }

                dir_count++;

                // no more directory entries left
                if (dir_count >= itable[tmp_inode].num_dir_entry) {
                    return -1;
                }
            }
        }
        UPDATE:
        token = strtok(NULL, "/");
    }

//    END:
    return tmp_inode;
}

uint32_t get_num_blks_of_file(a1fs_inode ino) {

    uint32_t result = 0;

    for (uint32_t i = 0; i < ino.extent_num; i++) {
        result += ((a1fs_extent *)ino.indirect_pt)[i].count;
    }
    return result;
}

uint32_t get_exact_num_blks_of_file(a1fs_inode ino) {

    uint32_t result = 0;
    if (ino.size == 0) {
        return result;
    } else {
        result = 1;
        for (uint32_t i = 0; i < ino.extent_num; i++) {
            result += ((a1fs_extent *)ino.indirect_pt)[i].count;
        }
    }
    return result;
}

int is_bit_set(uint32_t index, unsigned char *bitmap) {
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    if ((bitmap[byte_index] & (1 << bit_index)) == 0) {
        return 0; // unset
    } else {
        return 1; // set
    }
}

uint32_t get_first_available_position(uint32_t n, unsigned char *bitmap) {
    for (uint32_t i = 0; i < n; i++) {
        if (is_bit_set(i, bitmap) == 0) {
            return i;
        }
    }
    return (uint32_t) 2e32-1;
}

void set_bitmap(unsigned char *bitmap, uint32_t index) {
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    bitmap[byte_index] |= (1 << bit_index);
}

void unset_bitmap(unsigned char *bitmap, uint32_t index) {
    uint32_t byte_index = index / 8;
    uint32_t bit_index = index % 8;
    bitmap[byte_index] &= ~(1 << bit_index);
}


void extract_parent_path(char *path, char *buf) {
    char *last = strrchr(path, '/');
    size_t diff = strlen(path) - strlen(last);
    if (diff == 0) {
        strcpy(buf, "/");
        buf[1] = '\0';
    } else {
        strncpy(buf, path, diff);
    }
}

void extract_child_path(char *path, char *buf) {
    char *last = strrchr(path, '/');
    strncpy(buf, last + 1, strlen(last) + 1);
    buf[strlen(last) + 1] = '\0';
}

uint32_t find_last_block(fs_ctx *fs, int inode_num){
    a1fs_inode inode = fs->inode_table[inode_num];
    return (uint32_t) ((a1fs_extent *)inode.indirect_pt)[inode.extent_num - 1].start
        + (uint32_t) ((a1fs_extent *)inode.indirect_pt)[inode.extent_num - 1].count - 1;
}

uint64_t get_addr_of_block(fs_ctx *fs, uint32_t block_num){
    return fs->data_block + (uint64_t)block_num * A1FS_BLOCK_SIZE;
}

uint32_t get_num_of_block(fs_ctx *fs, uint64_t block_addr){
    return (uint32_t)((block_addr - fs->data_block) / A1FS_BLOCK_SIZE);
}

uint64_t get_addr_of_starting_write_point(fs_ctx *fs, uint32_t file_inode_num, uint64_t offset){

    
    uint64_t count = 0;
    for(uint32_t i = 0; i < fs->inode_table[file_inode_num].extent_num; i++){ //traverse each extent
        a1fs_extent temp_extent = ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[i];
        for(uint32_t j = 0; j < temp_extent.count; j++){ // traverse each block
            uint64_t temp_block_addr = get_addr_of_block(fs, temp_extent.start + j);
            if (offset - count < A1FS_BLOCK_SIZE){
                return temp_block_addr + offset - count;
            }else{
                count += A1FS_BLOCK_SIZE;
            }
        }
    }

    return (uint64_t)NULL;


}

int growing_a_block_for_file(fs_ctx *fs, uint32_t file_inode_num){

    if(*fs->available_blocks == 0){
        return -1;
    }


    if(fs->inode_table[file_inode_num].size == 0){ // when original file is empty

        // there should be at least 2 available blocks
        if (*(fs->available_blocks) == 1) {
        	    return -1;
    	}

        //allocate indirect pointer for the file
		uint32_t new_single_indirect_block_num = get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
		set_bitmap(fs->data_bitmap, new_single_indirect_block_num);
		*(fs->available_blocks) -= 1;

        // make the first extent
		fs->inode_table[file_inode_num].indirect_pt = get_addr_of_block(fs, new_single_indirect_block_num);

        // allocate data block 
        ((a1fs_extent *) fs->inode_table[file_inode_num].indirect_pt)[0].start = (uint32_t)get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
        set_bitmap(fs->data_bitmap, new_single_indirect_block_num);
		*(fs->available_blocks) -= 1;

        ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[0].count = 1;

        // set inode
        fs->inode_table[file_inode_num].size = A1FS_BLOCK_SIZE;
        fs->inode_table[file_inode_num].extent_num = 1;

        
        memset((char *) get_addr_of_block(fs, ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[0].start), '\0', A1FS_BLOCK_SIZE);

        return 0;
        

    }

    char* original_last_block_addr = (char*)get_addr_of_block(fs, find_last_block(fs, file_inode_num));
    uint_fast32_t original_block_count_of_file = get_num_blks_of_file(fs->inode_table[file_inode_num]);

    if(fs->inode_table[file_inode_num].size % A1FS_BLOCK_SIZE != 0){ // there is a hole after the file inside the block
        memset((original_last_block_addr + fs->inode_table[file_inode_num].size % A1FS_BLOCK_SIZE), '\0', A1FS_BLOCK_SIZE * original_block_count_of_file - fs->inode_table[file_inode_num].size);
    }

    

    if(is_bit_set(find_last_block(fs, file_inode_num) + 1, fs->data_bitmap) == 0){
        // the next block of the last block of the file is free, so no need to add extent
        set_bitmap(fs->data_bitmap, find_last_block(fs, file_inode_num) + 1);
        *(fs->available_blocks) -= 1;

        //


        ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[fs->inode_table[file_inode_num].extent_num - 1].count += 1;
        fs->inode_table[file_inode_num].size =  (original_block_count_of_file + 1)* A1FS_BLOCK_SIZE;  

        // fill the last block with 0
        memset(original_last_block_addr + A1FS_BLOCK_SIZE, '\0', A1FS_BLOCK_SIZE);

        return 0;   

    }else{ // the next block is not free, have to firstly create an extent.

        if(fs->inode_table[file_inode_num].extent_num == 512){
            return -1;
        }

        uint_fast32_t new_block_num = (uint32_t)get_first_available_position(fs->num_of_data_blocks, fs->data_bitmap);
        set_bitmap(fs->data_bitmap, new_block_num);
        *(fs->available_blocks) -= 1;

        //create a new extent
        a1fs_extent new_extent;
        // put the block inside it and put the extent into the indirect pointer
        new_extent.start = new_block_num;
        new_extent.count = 1;
        ((a1fs_extent *)fs->inode_table[file_inode_num].indirect_pt)[fs->inode_table[file_inode_num].extent_num] = new_extent;
        fs->inode_table[file_inode_num].extent_num += 1;
        fs->inode_table[file_inode_num].size = (original_block_count_of_file + 1) * A1FS_BLOCK_SIZE;

        // fill the last block with 0
        
        memset((char *) get_addr_of_block(fs, new_block_num), '\0', A1FS_BLOCK_SIZE);
        return 0;    

    }

    
    
}