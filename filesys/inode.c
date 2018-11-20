#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
#ifndef FILESYS

static block_sector_t byte_to_sector (const struct inode *inode, off_t pos) {
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

#endif

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/*
Wires up the indirect pointers in struct inode.
*/

#ifdef FILESYS

int extend_inode_direct(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate, bool write_back) {

    ASSERT(disk_inode != NULL);
    ASSERT(disk_inode->sectors_allocated < 10);

    if (sectors_to_allocate == 0)
      return 0;

    int max_direct_we_can_allocate = 10 - disk_inode->sectors_allocated;
    int count_direct_blocks_to_allocate = sectors_to_allocate > max_direct_we_can_allocate
                                          ? max_direct_we_can_allocate
                                          : sectors_to_allocate;

    block_sector_t start;

    int i = 0;
    if (free_map_allocate(count_direct_blocks_to_allocate, &start)) {
      for (; i < count_direct_blocks_to_allocate; i++)
        disk_inode->direct_block_sectors[i + disk_inode->sectors_allocated] = i + start;
      goto done;
    } else {
      for (; i < count_direct_blocks_to_allocate; i++) {
        if (free_map_allocate(1, &start)) {
          disk_inode->direct_block_sectors[i + disk_inode->sectors_allocated] = start;
        } else {
          PANIC("OUT OF FILE SPACE\n");
        }
      }
      goto done;
    }

  done:

   if (write_back)
    block_write(fs_device, disk_inode->sector, disk_inode);
  disk_inode->sectors_allocated += count_direct_blocks_to_allocate;
  return sectors_to_allocate - count_direct_blocks_to_allocate;
}

int extend_inode_indirect(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {

  // printf("In extend_inode_indirect\n");

  ASSERT(disk_inode != NULL);
  // printf("disk_inode->sectors_allocated = %d\n", disk_inode->sectors_allocated);
  ASSERT(disk_inode->sectors_allocated >= 10);

  int indirect_blocks_allocated = disk_inode->sectors_allocated - 10;
  int indirect_blocks_unallocated = 128 - indirect_blocks_allocated;

  int indirect_blocks_to_allocate = sectors_to_allocate > indirect_blocks_unallocated
                                      ? indirect_blocks_unallocated
                                      : sectors_to_allocate;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (indirect_blocks_allocated != 0)
    block_read(fs_device, disk_inode->indirect_block_sector, mock_sector);

  //This function panic's to the kernel if not successful. We can assume it works
  chunk_sector_blocks(mock_sector, indirect_blocks_to_allocate, indirect_blocks_to_allocate, indirect_blocks_allocated);
  block_write(fs_device, disk_inode->indirect_block_sector, mock_sector);

  disk_inode->sectors_allocated += indirect_blocks_to_allocate;
  return sectors_to_allocate - indirect_blocks_to_allocate;
}


int extend_inode_dbl_indirect(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {
  ASSERT(disk_inode != NULL);

  int blocks_allocated = 0;
  int dbl_indirect_sectors = disk_inode->sectors_allocated - 10 - 128;

  ASSERT((16384 - dbl_indirect_sectors) > sectors_to_allocate);


  int current_blocks_used = DIV_ROUND_UP(dbl_indirect_sectors, 128);
  int blocks_needed = DIV_ROUND_UP(dbl_indirect_sectors + sectors_to_allocate, 128);

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  //Note: This doesn't count as allocated blocks.
  // I am only counting blocks that point to sectors
  // intended for use by files (not blocks of sector #)
  if (blocks_needed != current_blocks_used) {
    int difference = blocks_needed - current_blocks_used;
    chunk_sector_blocks(mock_sector, difference, difference, current_blocks_used);
    block_write(fs_device, disk_inode->double_indirect_block, mock_sector);
  }

  memset(mock_sector, 0, BLOCK_SECTOR_SIZE);
  block_sector_t sector;

  while (sectors_to_allocate > 0) {
    int base_sector = sectors_to_allocate / 128;
    int sector_ofs = dbl_indirect_sectors % 128;
    int max_sector_create = 128 - sector_ofs;
    int num_to_create_round = max_sector_create > sectors_to_allocate ? sectors_to_allocate : max_sector_create;

    //get the sector;
    block_read(fs_device, disk_inode->double_indirect_block, mock_sector);
    sector = ((uint32_t *)mock_sector) [base_sector];

    // memcpy(mock_sector + base_sector * sizeof(uint32_t), &sector, sizeof(uint32_t));

    //get the second block
    if (sector_ofs != 0)
      block_read(fs_device, sector, mock_sector);
    chunk_sector_blocks(mock_sector, num_to_create_round, num_to_create_round, sector_ofs);
    block_write(fs_device, sector, mock_sector);

    blocks_allocated += num_to_create_round;
    sectors_to_allocate -= num_to_create_round;
    dbl_indirect_sectors += num_to_create_round;
  }

  return sectors_to_allocate; //number of sectors left. Will be 0 / if not kernel would panic earlier.
}



int chunk_sector_blocks(void *page, int num_to_allocate, int chunk_size, int start_idx) {

  ASSERT((num_to_allocate + start_idx) < 128);
  block_sector_t start;
  int sectors_allocated = 0;

  while (sectors_allocated < num_to_allocate) {

    int sectors_left_to_allocate = num_to_allocate - sectors_allocated;

    if (sectors_left_to_allocate < chunk_size)
      chunk_size = sectors_left_to_allocate;

    if (free_map_allocate(chunk_size, &start)) {
      int i = 0;
      for (; i < chunk_size; i++) {
        int page_idx = sectors_allocated + start_idx;
        ((uint32_t *)page) [page_idx] = start + i;
        sectors_allocated++;
      }
    } else {
      if (chunk_size == 1)
        PANIC("OUT OF DISK SPACE");

      if (chunk_size >= 100)
        chunk_size -= 10;
      else if (chunk_size >= 50)
        chunk_size -= 5;
      else if (chunk_size >= 25)
        chunk_size -= 2;
      else
        chunk_size -= 1;
    }
  }
  return sectors_allocated;
}

block_sector_t get_individual_sector(void) {
  block_sector_t start;
  if (free_map_allocate(1, &start))
    return start;
  else
    PANIC("Out Of Disk Space");
}

bool inode_create(block_sector_t sector, off_t length) {

  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0 && length < 8388608);
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    size_t sectors_allocated = 0;

    disk_inode->sectors_allocated = 0;
    disk_inode->length = length;
    disk_inode->sector = sector;
    disk_inode->magic = INODE_MAGIC;

    sectors = extend_inode_direct(disk_inode, sectors, false);
    // sectors -= sectors_allocated;

    disk_inode->indirect_block_sector = get_individual_sector();
    disk_inode->double_indirect_block = get_individual_sector();

    if (sectors > 0) {
      // sectors_allocated = extend_inode_indirect(disk_inode, sectors);
      sectors = extend_inode_indirect(disk_inode, sectors);
      // sectors -= sectors_allocated;
    }

    if (sectors > 0) {
      sectors = extend_inode_dbl_indirect(disk_inode, sectors);
      // sectors_allocated = extend_inode_dbl_indirect(disk_inode, sectors);
      // sectors -= sectors_allocated;
    }

    ASSERT (sectors == 0);

    block_write(fs_device, sector, disk_inode);
    success = true;
    free(disk_inode);
  }
  return success;
}

int release_direct_block(struct inode *inode, int sectors_to_clear) {

  if (sectors_to_clear == 0 || inode == NULL)
    return 0;

  int num_to_free = sectors_to_clear > 10 ? 10 : sectors_to_clear;

  if (is_direct_block_sequential(inode, num_to_free)) {
    free_map_release(inode->data.direct_block_sectors[0], num_to_free);
  } else {
    int i = 0;
    for (; i < num_to_free; i++)
      free_map_release(inode->data.direct_block_sectors[i], 1);

    return sectors_to_clear - num_to_free;
  }
}

bool is_direct_block_sequential(struct inode *inode, int num_to_free_capped) {

  ASSERT(10 > num_to_free_capped);

  if (num_to_free_capped == 0)
    return false;

  int first_sector = inode->data.direct_block_sectors[0];
  int i = 0;
  for (; i < num_to_free_capped; i++) {
    if (i == 0)
      continue;

    if (first_sector + i != inode->data.direct_block_sectors[i])
      return false;
  }
  return true;
}

int release_indirect_block(struct inode *inode, int sectors_to_clear) {
  if (sectors_to_clear == 0 || inode == NULL)
    return 0;

  return release_block(inode->data.indirect_block_sector, sectors_to_clear);
}

int release_block(block_sector_t sector_to_release, int sectors_to_clear) {
  if (sectors_to_clear == 0)
    return 0;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (mock_sector == NULL)
    PANIC("int release_block(%d, %d) - malloc(%d) == NULLPTR\n", sector_to_release, sectors_to_clear, BLOCK_SECTOR_SIZE);

  block_read(fs_device, sector_to_release, mock_sector);

  int sectors_cleared = 0;
  block_sector_t sector;
  while (sectors_cleared < 128 && sectors_to_clear > sectors_cleared) {
    memcpy(&sector, mock_sector + sectors_cleared * sizeof(uint32_t), sizeof(uint32_t));
    free_map_release(sector, 1);
    sectors_cleared++;
  }

  return sectors_to_clear - sectors_cleared;
}


int release_double_indirect_block(struct inode *inode, int sectors_to_clear) {
  if (sectors_to_clear == 0 || inode == NULL)
    return 0;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (mock_sector == NULL)
    PANIC("int release_double_indirect_block(%p, %d) - malloc(%d) == NULLPTR\n", inode, sectors_to_clear, BLOCK_SECTOR_SIZE);

  block_sector_t sector;
  // int sectors_left_to_clear =
  block_read(fs_device, inode->data.double_indirect_block, mock_sector);

  int i = 0;
  int sectors_to_free = DIV_ROUND_UP(sectors_to_clear, 128);
  for (; i < sectors_to_free; i++) {
    memcpy(&sector, mock_sector + i * sizeof(uint32_t), sizeof(uint32_t));
    sectors_to_clear = release_block(sector, sectors_to_clear);
    free_map_release(sector, 1);
  }

}

void inode_close(struct inode *inode) {
  if (inode == NULL)
    return;

  if (--inode->open_cnt == 0) {
    list_remove(&inode->elem);

    if (inode->removed) {
      int num_sectors_to_free = bytes_to_sectors(inode->data.length);

      num_sectors_to_free = release_direct_block(inode, num_sectors_to_free);

      if (num_sectors_to_free > 0)
        num_sectors_to_free = release_indirect_block(inode, num_sectors_to_free);

      if (num_sectors_to_free > 0)
        num_sectors_to_free = release_double_indirect_block(inode, num_sectors_to_free);

      ASSERT(num_sectors_to_free == 0);

      free_map_release(inode->sector, 1);
    }
    free(inode);
  }
}

// Check this logic @@@
block_sector_t inode_offset_to_sector(struct inode_disk *inode_d, off_t current_offset) {

  ASSERT(inode_d->sectors_allocated * 512 >= current_offset);

  // printf("inode_offset_to_sector(%p, %d)\n", inode_d, current_offset);
  int sector_count = current_offset / 512;


  if (sector_count >= 0 && sector_count <= 9)
    return inode_d->direct_block_sectors[sector_count];

  sector_count -= 10;

  uint32_t *mock_sector = malloc(BLOCK_SECTOR_SIZE);
  if (sector_count >= 0 && sector_count <= 127) {
    block_sector_t sector;
    block_read(fs_device, inode_d->indirect_block_sector, mock_sector);
    sector = mock_sector[sector_count];
    // memcpy(&sector, mock_sector + sizeof(uint32_t) * (sector_count - 1), sizeof(uint32_t));
    // printf("\nSector is: %d\n", sector);
    free(mock_sector);
    return sector;
  }
  sector_count -= 128;

  if (sector_count >= 0 && sector_count <= 16383) { /* 128 * 128 */
    // int indirect_sector = (sector_count - 1) / 128;
    int indirect_sector = sector_count / 128;
    block_sector_t sector;
    block_read(fs_device, inode_d->double_indirect_block, mock_sector);
    sector = mock_sector[indirect_sector];
    // memcpy(&sector, mock_sector + sizeof(uint32_t) * indirect_sector, sizeof(uint32_t));
    block_read(fs_device, sector, mock_sector);

    // int pos = (sector_count - 1) % BLOCK_SECTOR_SIZE;
    int pos = sector_count % BLOCK_SECTOR_SIZE;
    sector = mock_sector[pos];
    // memcpy(&sector, mock_sector + sizeof(uint32_t) * pos, sizeof(uint32_t));

    free(mock_sector);
    return sector;
  }

  return -1;
}

/* ENDIFENDIFENDIFENDIFENDIFENDIFENDIFENDIFENDIF */
#endif


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

#ifndef FILESYS

bool inode_create (block_sector_t sector, off_t length) {
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0)
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;

              for (i = 0; i < sectors; i++)
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

#endif

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open (block_sector_t sector) {
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber (const struct inode *inode) {
  return inode->sector;
}


/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */

#ifndef FILESYS

void inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));
        }

      free (inode);
    }
}

#endif


/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) {
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */


off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset) {
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) {

#ifdef FILESYS
    block_sector_t sector_idx = inode_offset_to_sector(&inode->data, offset);
#else
    block_sector_t sector_idx = byte_to_sector (inode, offset);
#endif

    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      block_read(fs_device, sector_idx, buffer + bytes_read);
    } else {
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      block_read (fs_device, sector_idx, bounce);
      memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  free (bounce);
  return bytes_read;
}


off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset) {
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
#ifdef FILESYS
      block_sector_t sector_idx = inode_offset_to_sector(&inode->data, offset);
      printf("sector_idx: %d\n", sector_idx);
#else
      block_sector_t sector_idx = byte_to_sector (inode, offset);
#endif
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
