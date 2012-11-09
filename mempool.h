#ifndef MEMPOOL_H
#define MEMPOOL_H

/* Stores the next and previous beginnings 
   of an entry in mempool */
struct memunit {
	struct memunit* next;
	struct memunit* prev;
};

/* A pool of memory */
struct mempool {
	void* memblock;
	
	struct memunit* allocated_memblock;
	struct memunit* free_memblock;
	
	unsigned long unit_size;
	unsigned long memblock_size;
};

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  init_mempool(unsigned long usize, unsigned long num_units)
 *  Description:  Initializes a mempool structure
 *	  Arguments:  usize     : The size of the structure to store in the memory pool
 *				  num_units : How many structures that can be stored in mempool
 *        Locks:  None
 *      Returns:  A new mempool structure
 *		  Free?:  Yes
 * =====================================================================================
 */
struct mempool*  init_mempool(unsigned long usize, unsigned long num_units);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mempool_alloc(struct mempool* mp, unsigned long usize)
 *  Description:  Returns a portion of memory managed by the memory pool
 *	  Arguments:  mp    : The memory pool to grab a chunk out of
 *				  usize : Size of structure to allocate
 *        Locks:  None
 *      Returns:  A chunk of memory in mp, or a newly malloced chunk of memory if
 *				  mempool is full
 *		  Free?:  Yes, with mempool_free
 * =====================================================================================
 */
void* mempool_alloc(struct mempool* mp, unsigned long usize);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  mempool_free(struct mempool* mp, void* p)
 *  Description:  Frees p from mp
 *	  Arguments:  mp  : The memory pool to operate on
 *				  p   : The object to release back into mp
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
void  mempool_free(struct mempool* mp, void* p);

/* 
 * ===  FUNCTION  ======================================================================
 *         Name:  free_mempool(struct mempool* mp)
 *  Description:  Frees the chunk of memory used by mp
 *	  Arguments:  mp    : The memory pool to free
 *        Locks:  None
 *      Returns:  (void)
 * =====================================================================================
 */
void free_mempool(struct mempool* mp);

#endif