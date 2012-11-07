#ifndef MEMPOOL_H
#define MEMPOOL_H

struct memunit {
	struct memunit* next;
	struct memunit* prev;
};

struct mempool {
	void* memblock;
	
	struct memunit* allocated_memblock;
	struct memunit* free_memblock;
	
	unsigned long unit_size;
	unsigned long memblock_size;
};

struct mempool*  init_mempool(unsigned long usize, unsigned long num_units);
void* mempool_alloc(struct mempool* mp, unsigned long usize);
void  mempool_free(struct mempool* mp, void* p);
void free_mempool(struct mempool* mp);

#endif