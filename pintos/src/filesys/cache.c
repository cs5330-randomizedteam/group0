#include <list.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

#define MAX_CACHE_SIZE 64

struct cache_line
{	
	block_sector_t idx;
	bool is_dirty;
	struct list_elem elem;
	int buffer_idx;
};

struct list cache_list;
void* sector_buffers[MAX_CACHE_SIZE];
uint64_t buffer_map;

int ctzll (long long x)
{
    int i;
    for (i = 0; i < 8 * sizeof (long long); ++i)
       if (x & ((long long) 1  << i))
                      break;
    return i;
}

static void cache_move_front(struct list_elem *e) {
	list_remove(e);
	list_push_front(&cache_list, e);
}

void cache_init(void) {
	list_init (&cache_list);
	buffer_map = ~0;
	for (int i = 0; i < MAX_CACHE_SIZE; i++)
		sector_buffers[i] = malloc(BLOCK_SECTOR_SIZE);
}


void cache_read(struct block *fs_device, block_sector_t sector, void* buffer) {
	struct list_elem *e;

	for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
	{
	    struct cache_line* cl = list_entry (e, struct cache_line, elem);
	    if (cl->idx == sector) {
	    	memcpy(buffer, sector_buffers[cl->buffer_idx], BLOCK_SECTOR_SIZE);
	    	cache_move_front(e);
	    	return;
	    }
	}

	if (buffer_map != 0) {
		struct cache_line* new_cache = malloc(sizeof(struct cache_line));
		new_cache->idx = sector;
		new_cache->is_dirty = 0;
		new_cache->buffer_idx = ctzll(buffer_map);
		buffer_map &= buffer_map - 1;
		block_read(fs_device, sector, sector_buffers[new_cache->buffer_idx]);
		list_push_front(&cache_list, &new_cache->elem);
		memcpy(buffer, sector_buffers[new_cache->buffer_idx], BLOCK_SECTOR_SIZE);
	} else {
		struct cache_line* cl = list_entry(list_back(&cache_list), struct cache_line, elem);
		if (cl->is_dirty) block_write(fs_device, cl->idx, sector_buffers[cl->buffer_idx]);

		cl->idx = sector;
		cl->is_dirty = 0;
		block_read(fs_device, sector, sector_buffers[cl->buffer_idx]);
		cache_move_front(&cl->elem);
		memcpy(buffer, sector_buffers[cl->buffer_idx], BLOCK_SECTOR_SIZE);
	}
}


void cache_write(struct block *fs_device, block_sector_t sector, void* buffer) {
	struct list_elem *e;

	for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
	{
	    struct cache_line* cl = list_entry (e, struct cache_line, elem);
	    if (cl->idx == sector) {
	    	memcpy(sector_buffers[cl->buffer_idx], buffer, BLOCK_SECTOR_SIZE);
	    	cl->is_dirty = 1;
	    	cache_move_front(e);
	    	return;
	    }
	}

	if (buffer_map != 0) {
		struct cache_line* new_cache = malloc(sizeof(struct cache_line));
		new_cache->idx = sector;
		new_cache->is_dirty = 1;
		new_cache->buffer_idx = ctzll(buffer_map);
		buffer_map &= buffer_map - 1;
		list_push_front(&cache_list, &new_cache->elem);
		memcpy(sector_buffers[new_cache->buffer_idx], buffer, BLOCK_SECTOR_SIZE);
	} else {
		struct cache_line* cl = list_entry(list_back(&cache_list), struct cache_line, elem);
		if (cl->is_dirty) block_write(fs_device, cl->idx, sector_buffers[cl->buffer_idx]);

		cl->idx = sector;
		cl->is_dirty = 1;
		cache_move_front(&cl->elem);
		memcpy(sector_buffers[cl->buffer_idx], buffer, BLOCK_SECTOR_SIZE);
	}
}

void cache_flush(struct block *fs_device) {
	struct list_elem *e;

	for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
	{
	    struct cache_line* cl = list_entry (e, struct cache_line, elem);
		if (cl->is_dirty) block_write(fs_device, cl->idx, sector_buffers[cl->buffer_idx]);
		cl->is_dirty = 0;
	}
}