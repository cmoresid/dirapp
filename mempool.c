#include <stdlib.h>

#include "mempool.h"

struct mempool*  init_mempool(unsigned long usize, unsigned long num_units) {
	struct mempool* new_mempool;
	struct memunit* cur_unit;
	unsigned long i;
	
	new_mempool = (struct mempool*) malloc(sizeof(struct mempool));
	new_mempool->unit_size = usize;
	new_mempool->allocated_memblock = NULL;
	new_mempool->free_memblock = NULL;
	new_mempool->memblock_size = num_units * (usize+sizeof(struct memunit));
	new_mempool->memblock = malloc(new_mempool->memblock_size);
	
	if (new_mempool->memblock != NULL) {
		for (i = 0; i < num_units; i++) {
			cur_unit = (struct memunit*) ( (char*)new_mempool->memblock + i*(new_mempool->unit_size+sizeof(struct memunit)));
			
			cur_unit->prev = NULL;
			cur_unit->next = new_mempool->free_memblock;
			
			if (new_mempool->free_memblock != NULL) {
				new_mempool->free_memblock->prev = cur_unit;
			}
			
			new_mempool->free_memblock = cur_unit;
		}
	}
}

void free_mempool(struct mempool* mp) {
	free(mp->memblock);
}

void* mempool_alloc(struct mempool* mp, unsigned long usize) {
	if (usize > mp->unit_size || mp->memblock == NULL || mp->free_memblock == NULL) {
		return malloc(usize);
	}
	
	struct memunit* cur_unit = mp->free_memblock;
	
	mp->free_memblock = cur_unit->next;
	if (mp->free_memblock != NULL) {
		mp->free_memblock->prev = NULL;
	}
	
	cur_unit->next = mp->allocated_memblock;
	
	if (mp->allocated_memblock != NULL) {
		mp->allocated_memblock->prev = cur_unit;
	}
	
	mp->allocated_memblock = cur_unit;
	
	return (void*)((char*) cur_unit + sizeof(struct memunit));
}

void mempool_free(struct mempool* mp, void* p) {
	if (mp->memblock < p && p < (void*)((char*)mp->memblock+mp->memblock_size)) {
		struct memunit* cur_unit = (struct memunit*) ((char*)p - sizeof(struct memunit));
		
		mp->allocated_memblock = cur_unit->next;
		if (mp->allocated_memblock != NULL) {
			mp->allocated_memblock->prev = NULL;
		}
		
		cur_unit->next = mp->free_memblock;
		if (mp->free_memblock != NULL) {
			mp->free_memblock->prev = cur_unit;
		}
		
		mp->free_memblock = cur_unit;
	} else {
		free(p);
	}
}

