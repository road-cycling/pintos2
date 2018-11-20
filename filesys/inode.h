#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

#ifdef FILESYS

int extend_inode_direct(struct inode_disk *, block_sector_t, bool);
int extend_inode_indirect(struct inode_disk *, block_sector_t);
int extend_inode_dbl_indirect(struct inode_disk *, block_sector_t);
int chunk_sector_blocks(void *, int, int, int);
block_sector_t get_individual_sector();
int release_block(block_sector_t, int);
bool is_direct_block_sequential(struct inode *, int);
int release_direct_block(struct inode *, int);
int release_indirect_block(struct inode *, int);
int release_double_indirect_block(struct inode *, int);

block_sector_t inode_offset_to_sector(struct inode_disk, off_t);
#endif

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
