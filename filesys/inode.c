#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
};

// struct inode_disk {
//   uint32_t length;                        /* int32_t 4 Bytes */
//   uint32_t direct_block_sectors[10];      /* direct block */
//   uint32_t indirect_block_sector;         /* indirect blocks */
//   uint32_t doubly_indirect_block_sector;  /* dindirect blocks */
//   unsigned magic;                         /* magic - detect overflow */
//   uint32_t unused[114];                   /* pad to fit struct */
// };


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

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

struct inode_disk {
  uint32_t length;                        /* int32_t 4 Bytes */
  uint32_t direct_block_sectors[10];      /* direct block */
  uint32_t indirect_block;                /* indirect blocks */
  uint32_t double_indirect_block;         /* dindirect blocks */
  unsigned magic;                         /* magic - detect overflow */
  uint32_t unused[114];                   /* pad to fit struct */
};


int extend_inode_direct_new(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {

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
    disk_inode->sectors_allocated += count_direct_blocks_to_allocate;
    return sectors_to_allocate - count_direct_blocks_to_allocate;
}

int extend_inode_indirect_new(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {

  ASSERT(disk_inode != NULL);
  ASSERT(disk_inode->sectors_allocated > 10);

  int indirect_blocks_allocated = disk_inode->sectors_allocated - 10;
  int indirect_blocks_unallocated = 128 - indirect_blocks_allocated;

  int indirect_blocks_to_allocate = sectors_to_allocate > indirect_blocks_unallocated
                                      ? indirect_blocks_unallocated
                                      : sectors_to_allocate;

  block_sector_t start;
  int i = 0;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (indirect_blocks_allocated != 0)
    block_read(fs_device, disk_inode->double_indirect_block_sector, mock_sector);

  //This function panic's to the kernel if not successful. We can assume it works
  chunk_sector_blocks_new(mock_sector, indirect_blocks_to_allocate, indirect_blocks_to_allocate, indirect_blocks_allocated);

  disk_inode->sectors_allocated += indirect_blocks_to_allocate;
  return sectors_to_allocate - indirect_blocks_to_allocate;
}


int chunk_sector_blocks_new(void *page, int num_to_allocate, int chunk_size, int start_idx) {

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
        memset(page + page_idx * sizeof(uint32_t), start + i, sizeof(uint32_t));
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

int chunk_disk_sector_blocks(void *page, int num_to_allocate, int chunk_size) {

  block_sector_t start;
  size_t sectors_allocated = 0;
  static char zeroes[BLOCK_SECTOR_SIZE];

  while (sectors_allocated < num_to_allocate) {

    if ((num_to_allocate - sectors_allocated) < chunk_size)
      chunk_size = num_to_allocate - sectors_allocated;

    if (free_map_allocate(chunk_size, &start)) {
      int num_alloc = 0;
      for (; num_alloc < chunk_size; num_alloc++) {
        memset(page + sectors_allocated * sizeof(uint32_t), start + num_alloc, sizeof(uint32_t));
        block_write(fs_disk, start + num_alloc, zeroes);
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

// Duplicated Code -> lots of it
int extend_inode_direct(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {
  ASSERT(disk_inode != NULL);
  ASSERT(disk_inode->sec_alloc < 10);

  int blocks_allocated = 0;
  int direct_block_start = disk_inode->/*sec_alloc*/;

  int max_alloc = 10 - direct_block_start;
  int num_alloc = max_alloc > sectors_to_allocate ? sectors_to_allocate : max_alloc;

  block_sector_t start;

  int i = 0;
  if (free_map_allocate(num_alloc, &start)) {
    for (; i < num_alloc; i++) {
      disk_inode->direct_block_sectors[i] = start + i;
      blocks_allocated++;
    }
  } else {
    for (i = direct_block_start; i < direct_block_start + num_alloc; i++) {
      if (free_map_allocate(1, &start)) {
        disk_inode->direct_block_sectors[i] = start;
        blocks_allocated++;
      } else {
        PANIC("OUT OF FILE SPACE\n");
      }
    }
  }

  return sectors_to_allocate - blocks_allocated;
}

int extend_inode_indirect(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {
  ASSERT(disk_inode != NULL);

  int blocks_allocated = 0;
  block_sector_t start;
  int indirect_sector = disk_inode->sec_alloc - 10;
  int indirect_sectors_to_allocate = ((128 - indirect_sector) > sectors_to_allocate) ? sectors_to_allocate : 128 - indirect_sector;

  int i = indirect_sector;

  if (free_map_allocate(indirect_sectors_to_allocate, &start)) {
    for (; i < indirect_sectors_to_allocate + indirect_sector; i++)
  }

  for (i; i < indirect_sectors_to_allocate + indirect_sector; i++) {
    free_map_allocate()
  }

  // foo bar
  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);
  block_read(fs_device, disk_inode->indirect_block_sector, mock_sector);
}


int inode_double_indirect_ptr(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {
  ASSERT(sectors_to_allocate >= 0);

  if (sectors_to_allocate == 0)
    return 0;

  size_t sectors_allocated = 0;

  int num_double_indirect_sectors = DIV_ROUND_UP(sectors_to_allocate, 128);
  inode_indirect_ptr(disk_inode, num_double_indirect_sectors, true, -1);
  if (num_double_indirect_sectors != inode_indirect_ptr(disk_inode, num_double_indirect_sectors, true, -1))
    PANIC("OUT OF DISK SPACE");

  int i = 0;
  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);
  block_read(fs_device, disk_inode->double_indirect_block_sector, mock_sector);
  int sector;
  for (; i < num_double_indirect_sectors; i++) {
    memcpy(&sector, mock_sector + i * sizeof(uint32_t), sizeof(uint32_t));

    int sectors_to_alloc = sectors_to_allocate > 128 ? 128 : sectors_to_allocate;
    sectors_allocated += inode_indirect_ptr(NULL, sectors_to_allocate, false, -1);
  }

  free(mock_sector);

  return sectors_allocated;
}


int chunk_disk_sector_blocks(void *page, int num_to_allocate, int chunk_size) {

  block_sector_t start;
  size_t sectors_allocated = 0;
  static char zeroes[BLOCK_SECTOR_SIZE];

  while (sectors_allocated < num_to_allocate) {

    if ((num_to_allocate - sectors_allocated) < chunk_size)
      chunk_size = num_to_allocate - sectors_allocated;

    if (free_map_allocate(chunk_size, &start)) {
      int num_alloc = 0;
      for (; num_alloc < chunk_size; num_alloc++) {
        memset(page + sectors_allocated * sizeof(uint32_t), start + num_alloc, sizeof(uint32_t));
        block_write(fs_disk, start + num_alloc, zeroes);
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


int inode_indirect_ptr(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate, bool double_indirect, int direct_sector) {

  ASSERT(sectors_to_allocate >= 0);

  if (sectors_to_allocate == 0)
    return 0;

  size_t sectors_allocated;
  block_sector_t start;

  int num_sectors_in_indirect = BLOCK_SECTOR_SIZE / 4; //(512 / sizeofint)
  int num_sectors_to_allocate = (sectors_to_allocate / num_sectors_in_indirect > 0)
                                        ? num_sectors_in_indirect
                                        : sectors_to_allocate;

  if ((direct_sector >= 0) && free_map_allocate(1, &start)) {
    if (double_indirect)
      disk_inode->double_indirect_block_sector = start;
    else
      disk_inode->indirect_block_sector = start;
  } else {
    PANIC("OUT OF DISK SPACE");
  }

  void *indirect_block = malloc(BLOCK_SECTOR_SIZE);

  if (direct_sector >= 0) {
    sectors_allocated = chunk_disk_sector_blocks(indirect_block, num_sectors_to_allocate, 50);
    block_write(fs_device, direct_sector, indirect_block);
  }

  else if (double_indirect) {
    sectors_allocated = chunk_disk_sector_blocks(indirect_block, num_sectors_to_allocate, 1);
    block_write(fs_device, disk_inode->double_indirect_block_sector, indirect_block);
  }
  else {
    sectors_allocated = chunk_disk_sector_blocks(indirect_block, num_sectors_to_allocate, 50);
    block_write(fs_device, disk_inode->indirect_block_sector, indirect_block);
  }

  free(indirect_block);

  return sectors_allocated;
}

// https://www.youtube.com/watch?v=FBVITUka_30
// Mozart Concerto D Minor K466 Freiburger Mozart-Orchester, Michael Erren,Valentina Lisitsa


int inode_create_direct_ptr(struct inode_disk *disk_inode, block_sector_t sectors_to_allocate) {

  ASSERT(sectors_to_allocate >= 0);

  size_t sectors_allocated = 0;
  block_sector_t start;

  if (sectors_to_allocate == 0) /* This is ok */
    return 0;

  int num_sectors_to_allocate = (sectors_to_allocate / 10 > 0 ? 10) : sectors_to_allocate;
  //Try to allocate MAX or block of 10

  int i = 0;
  static char zeroes[BLOCK_SECTOR_SIZE];
  if (free_map_allocate(num_sectors_to_allocate, &start)) {
    for (; i < num_sectors_to_allocate; i++) {
      disk_inode->direct_block_sectors[i] = start + i;
      block_write(fs_device, start + i, zeroes);
      sectors_allocated++;
    }
  } else {
    for (i = 0; i < num_sectors_to_allocate; i++) {
      if (free_map_allocate(1, &start)) {
        disk_inode->direct_block_sectors[i] = start;
        block_write(fs_device, start, zeroes);
        sectors_allocated++;
      } else {
        PANIC("OUT OF FILE SPACE\n");
      }
    }
  }

  return sectors_allocated;
}

bool inode_create_nathan(block_sector_t sector, off_t length) {
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0 && length < 8388608);
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    size_t sectors_allocated = 0;
    block_sector_t start;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;

    sectors_allocated = inode_create_direct_ptr(disk_inode, sectors);
    sectors -= sectors_allocated;

    if (sectors > 0) {
      sectors_allocated = inode_indirect_ptr(disk_inode, sectors, 0);
      sectors -= sectors_allocated;
    }

    if (sectors > 0) {
      sectors_allocated = inode_double_indirect_ptr(disk_inode, sectors);
      sectors -= sectors_allocated;
    }

    ASSERT (sectors == 0);

    block_write(fs_device, sector, disk_inode);
    success = true;
    free(disk_inode);
  }

  return success;
}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */


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

bool is_direct_block_sequential(struct inode *inode, int num_to_free_capped) {
  if (num_to_free_capped == 0)
    return false;

  int first_sector = inode->data->direct_block_sectors[0];
  int i = 0;
  for (; i < num_to_free_capped; i++) {
    if (i == 0)
      continue;

    if (first_sector + i != inode->data->direct_block_sectors[i])
      return false;
  }
  return true;
}

// Returns Num Left
int release_direct_block(struct inode *inode, int sectors_to_clear) {

  if (sectors_to_clear == 0 || inode == NULL)
    return 0;

  int num_to_free = sectors_to_clear > 10 ? 10 : sectors_to_clear;

  if (is_direct_block_sequential(inode, num_to_free)) {
    free_map_release(inode->data->direct_block_sectors[0], num_to_free);
  } else {
    int i = 0;
    for (; i < num_to_free; i++)
      free_map_release(inode->data->direct_block_sectors[i], 1);

    return sectors_to_clear - num_to_free;
  }
}

int release_indirect_block(struct inode *inode, int sectors_to_clear) {
  if (sectors_to_clear == 0 || inode == NULL)
    return 0;

  return release_block(inode->data->indirect_block_sector, sectors_to_clear);
}

int release_double_indirect_block(struct inode *inode, int sectors_to_clear) {
  if (sectors_to_clear == 0 || inode == null)
    return 0;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (mock_sector == NULL)
    PANIC("int release_double_indirect_block(%x, %d) - malloc(%d) == NULLPTR\n", inode, sectors_to_clear, BLOCK_SECTOR_SIZE);

  block_sector_t sector;
  // int sectors_left_to_clear =
  block_read(fs_device, inode->data->double_indirect_block_sector, mock);

  int i = 0;
  int sectors_to_free = DIV_ROUND_UP(sectors_to_clear, 128);
  for (; i < sectors_to_free; i++) {
    memcpy(&sector, mock_sector + i * sizeof(uint32_t), sizeof(uint32_t));
    sectors_to_clear = release_block(sector, sectors_to_clear);
    free_map_release(sector, 1);
  }

}

//max in block is 128
// void * memcpy ( void * destination, const void * source, size_t num );
int release_block(block_sector_t sector, int sectors_to_clear) {
  if (sectors_to_clear == 0)
    return 0;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);

  if (mock_sector == NULL)
    PANIC("int release_block(%d, %d) - malloc(%d) == NULLPTR\n", sector, sectors_to_clear, BLOCK_SECTOR_SIZE);

  block_read(fs_device, sector, mock_sector);

  int sectors_cleared = 0;
  block_sector_t sector;
  while (sectors_cleared < 128 && sectors_to_clear > sectors_cleared) {
    memcpy(&sector, mock_sector + sectors_cleared * sizeof(uint32_t), sizeof(uint32_t));
    free_map_release(sector, 1);
    sectors_cleared++;
  }

  return sectors_to_clear - sectors_cleared;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
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


void inode_close_new (struct inode *inode, bool zero) {
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

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) {
  ASSERT (inode != NULL);
  inode->removed = true;
}


block_sector_t inode_offset_to_sector(struct inode_disk *inode_d, off_t current_offset) {

  ASSERT(inode_d->sectors_alloc * 512 >= current_offset);

  off_t sector_count = DIV_ROUND_UP(current_offset, BLOCK_SECTOR_SIZE);

  if (sector_count >= 1 && sector_count <= 10)
    return inode_d->direct_block_sectors[sector_count - 1];

  sector_count -= 10;

  void *mock_sector = malloc(BLOCK_SECTOR_SIZE);
  if (sector_count >= 1 && sector_count <= 128) {
    block_sector_t sector;
    block_read(fs_device, inode_d->indirect_block_sector, mock_sector);
    memcpy(&sector, mock_sector + sizeof(uint32_t) * (sector_count - 1), sizeof(uint32_t));
    free(mock_sector);
    return sector;
  }
  sector_count -= 128;

  if (sector_count >= 1 && sector_count <= 16384) { /* 128 * 128 */
    int indirect_sector = (sector_count - 1) / 128;
    block_sector_t sector;
    block_read(fs_device, inode_d->double_indirect_block_sector, mock_sector);
    memcpy(&sector, mock_sector + sizeof(uint32_t) * (sector_count - 1), sizeof(uint32_t));
    block_read(fs_device, sector, mock_sector);

    int pos = (sector_count - 1) % BLOCK_SECTOR_SIZE;
    memcpy(&sector, mock_sector + sizeof(uint32_t) * pos, sizeof(uint32_t));

    free(mock_sector);
    return sector;
  }

  return -1;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */


off_t inode_read_at_nathan (struct inode *inode, void *buffer_, off_t size, off_t offset) {
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) {
    block_sector_t sector_idx = inode_offset_to_sector(inode->data, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      block_read(fs_device, sector_idx, bytes_read);
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


off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  // inode   = 0xbff
  // buffer_ = 0xdeadbeef
  // size    = 1554
  // offset  = 53

  while (size > 0) {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);           // 1553 / 512 -> 3
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;                          // 53 % 512   -> 53

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;                     //1800 - 53 = 1,747
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;                     //512 - 53  = 459
      int min_left = inode_left < sector_left ? inode_left : sector_left;   // 1747 < 459 ? 1747 : 459 -> 459

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;                   //1554 < 459 ? 1554 : 459 -> 459
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
          /* Read full sector directly into caller's buffer. */
          //printf("Inode calling block read\n");
          block_read (fs_device, sector_idx, buffer + bytes_read);
          //printf("Inode finished\n");
        } else {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      //printf("Size: %d\t Offset: %d\t Bytes_read: %t\n", size, offset, bytes_read);
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  return bytes_read;
}

off_t
inode_write_at_new (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = inode_offset_to_sector(inode->data, offset);
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

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
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
