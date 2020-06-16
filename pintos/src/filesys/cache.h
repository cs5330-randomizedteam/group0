#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

void cache_init(void);
void cache_read(block_sector_t, void*);
void cache_write(block_sector_t, void*);
void cache_flush(void);

#endif /* filesys/cache.h */