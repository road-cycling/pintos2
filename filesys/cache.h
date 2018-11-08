#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

struct cache_block;

//Reserve block in b_cache to hold sector
// we could evict some other unused buffer
// grant exclusive / shared access
struct cache_block *cache_get_block(disk_sector_t, bool exclusive);

//release access
void cache_put_block(struct cache_block *);

//read cache from block -> pointer to data
void* cache_read_block(struct cache_block *);

//fill cache block with zeroes -> ptr to data
void* cache_zero_block(struct cache_block *);

//mark dirty (wb cache)
void cache_mark_block_dirty(struct cache_block *);

//init cache
void cache_block_init(void);

void cache_block_read_ahead(void);

void cache_block_shutdown(void);

#endif

//per block locking
//multiple reader single writer
//lru @ stack

/*

b = cache_get_block(n, _);
cache_read_block(b);
cache_readahead(next(n));


queue q;
cache_readahead(Sector s) {
  q.lock()
  q.add(requests(S));
  qcond.signal()
  q.unlock
}

cache_read_ahead_daemon()
  while(true)
    q.lock()
    while (q.empty())
      qcond.wait()
    s = q.pop()
    q.unlock()
    read sector s


*/
