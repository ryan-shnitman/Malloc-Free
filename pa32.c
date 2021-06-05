
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7) 
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Compute address of header and footer */
#define HDRP(p) ((char *)(p) - 4)
#define FTRP(p) ((char *)(p) + GET_SIZE(HDRP(p))-8)

/* Compute address of next and previous block */
#define NEXT_BLKP(p) ((char *)(p) + GET_SIZE(HDRP(p)))
#define PREV_BLKP(p) ((char *)(p) - GET_SIZE(HDRP(p) - 4))

/* Helper function prototype declarations */
static void place(void *bp, size_t asize);
static void *compact(void *bpM, addrs_t *bp);

/* Default size for VInit and Redirection Table */
#define DEFAULT_MEM_SIZE (1<<20)
#define MAX_NUM_BLOCKS (DEFAULT_MEM_SIZE/16)

/* Calculates difference between 2 addresses */
#define ADDR_DIFF(p1, p2) ((int)((unsigned long long)(p1) - (unsigned long long)(p2)))

/* HEAP CHECKER global variables and macros */
long num_alloc_blks = 0;
long num_free_blks = 0;
long Rtotal_alloc_bytes = 0;
long Ptotal_alloc_bytes = 0;
long Rtotal_free_bytes = 0;
long Atotal_free_bytes = 0;
long total_malloc_reqs = 0;
long total_free_reqs = 0;
long total_req_fails = 0;
long avg_malloc_cycles = 0;
long avg_free_cycles = 0;
long total_cycles = 0;

#define RDTSC(var)                                              \
  {                                                             \
    uint32_t var##_lo, var##_hi;                                \
    asm volatile("lfence\n\trdtsc" : "=a"(var##_lo), "=d"(var##_hi));     \
    var = var##_hi;                                             \
    var <<= 32;                                                 \
    var |= var##_lo;                                            \
  } 
//#define rdtsc(x)	__asm__ __volatile__("rdtsc \n\t" : "=A" (*(x)))

unsigned long long start, finish;
long total_malloc_cycles = 0;
long total_free_cycles = 0;

/* helper function prototypes */
static void place(void *bp, size_t asize);
static void *compact(void *bp);

typedef char *addrs_t;
typedef void *any_t;

addrs_t baseptr = 0;
addrs_t RT[MAX_NUM_BLOCKS];  //redirection table

/* Initialize M2 region in memory with size bytes */
void VInit(size_t size) {
	
	/* check bad requests */
	if (size == 0) {
		printf("attempt to initialize M2 of 0 bytes \n");
		return;
	}

	baseptr = (addrs_t)malloc(size);
	unsigned long long shift = ((unsigned long long)(baseptr) %8);
	baseptr = (char *)(baseptr) + shift;				//aligned start address of M1

	PUT(baseptr, 0);						//alignment padding
	PUT(baseptr + 4, PACK(8, 1));					//prologue header
	PUT(baseptr + 8, PACK(8, 1));					//prologue footer
	PUT(baseptr + 12, PACK(size - 16 - shift, 0));			//header for initial free block chunk
	PUT(baseptr + size - 8 - shift, PACK(size - 16 - shift, 0));	//footer for intitial free block chunk
	PUT((baseptr + size - 8 - shift), PACK(0, 1));			//epilogue

	baseptr += 16;

	memset(RT, NULL, MAX_NUM_BLOCKS * sizeof(void *));

}

/* Allocate size bytes in M2 and return pointer to the start address of malloced region */
addrs_t *VMalloc(size_t size) {
	//RDTSC(start);
	/* check bad requests */
	if (baseptr == 0) {
		printf("M2 uninitialized \n");
		return NULL;
	}	
	else if (size == 0) {		
		printf("cannot malloc zero bytes \n");
		return NULL;
	}
	else	{	
		
		size_t asize;

		/* adjust payload for alignment and overhead */
		if (size <= 8)	
			asize = 16;
		else
			asize = 8 * ((size + 15) / 8);

		if (GET_SIZE(HDRP(baseptr)) >= asize) {     //fit found
			int i;
			for (i = 0; i < MAX_NUM_BLOCKS; i++) {
				
				if (RT[i] == NULL) {
					RT[i] = baseptr;
					place(baseptr, asize);

				/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_malloc_cycles += time;
	avg_malloc_cycles = total_malloc_cycles / total_malloc_reqs; */
					
					return (RT + i);
				}
			}
		}
		
		else	//not fit
			/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_malloc_cycles += time;
	avg_malloc_cycles = total_malloc_cycles / total_malloc_reqs; */
			
			return NULL;
		
	}

}

/* Deallocate the block of M2 at the address stored as an element of RT at address addr.
* Coalesces and compacts. */
void VFree(addrs_t *addr) {
	//RDTSC(start)
	/* check bad requests */
	if (baseptr == 0) { 
		printf("%s", "M2 uninitialized \n");
		return;
	}	

	if ((addr == NULL) || (*(addr) == NULL)) {
		printf("%s", "invalid address \n");
		return;
	}

	addrs_t addrM = *(addr);
	size_t size = GET_SIZE(HDRP(addrM));	//size of freed block
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(addrM)));

	if (next_alloc == 0) {	//just coalesce

		size += GET_SIZE(HDRP(NEXT_BLKP(addrM)));  //size of free chunk including freed block

		PUT(HDRP(addrM), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(addrM)), PACK(size, 0));
	
		baseptr = addrM;
		*(addr) = NULL;
	}

	else {	//compact and coalesce
	
		compact (addrM, addr);
	}	

/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_malloc_cycles += time;
	avg_malloc_cycles = total_malloc_cycles / total_malloc_reqs; */
 
}	

/* Updates header and footer for newly allocated block.
* Splits free block if one of a minimum size 16 can be made. */
static void place (void *bp, size_t asize) {

	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= 16) {	//split block and create free block
		
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		bp = NEXT_BLKP(bp);

		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));

		baseptr = bp;
	}

	else {		//can't make free block of minimum size
		
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
		
		baseptr = NEXT_BLKP(bp);
	}
}

/* Performs compaction for VFree.
 * Removes reorganizes blocks and coalesces so there is one free chunk */
static void *compact(void *bpM, addrs_t *bp) {
		
	/* copy allocated blocks over to fill the gap */
	int free_extend = GET_SIZE(HDRP(bpM));
	size_t size = GET_SIZE(HDRP(baseptr)) + free_extend;
	addrs_t copy_bpM = NEXT_BLKP(bpM);
	int bytes_copied = ADDR_DIFF(baseptr - 4, copy_bpM);

	memcpy(bpM - 4, copy_bpM - 4, bytes_copied);
	
	/* update free block */	
	baseptr -= free_extend;
		
	PUT(HDRP(baseptr), PACK(size, 0));
	PUT(FTRP(baseptr), PACK(size, 0));
	
	/* update redirection table */	
	*(bp) = NULL;
	
	addrs_t runner_bpM = bpM;
	int i;
	for (i = 0; i < MAX_NUM_BLOCKS; i++) {
		if ((RT[i] != NULL) && (RT[i] >= bpM)) {
			RT[i] -= free_extend;
		}
	}
	 	
}

/* Copy size bytes from data into malloced region */
addrs_t *VPut(any_t data, size_t size){

	/* check bad requests */
	if (baseptr == 0) {
		printf("%s", "M2 uninitialized \n");
		return NULL;
	}
	if (data == NULL) {
		printf("%s", "invalid data address \n");
		return NULL;
	}

	/* malloc and copy bytes */
	addrs_t *bp = VMalloc(size);

	if (bp == NULL)	//no fit 	
		return NULL;
	else {
		char *data_char = (char *) data;
		addrs_t bpM = *(bp);

		memcpy(bpM, data_char, size);
			
		return bp;
	}
}

/* Copy size bytes of region of M2 pointed to entry of RT addr into return_data.
 * Free that region of M2. */
void VGet(any_t return_data, addrs_t *addr, size_t size) {
	
	/* check bad requests */
	if (baseptr == 0) {
		printf("%s", "M2 uninitialized \n");
		return;
	}
	if (return_data == NULL) {
		printf("%s", "invalid return_data address \n");
		return;
	}
	if ((addr == NULL) || (*(addr) == NULL)) {
		printf("%s", "invalid addr address \n");
		return;
	}
	
	/* copy bytes and free */
	addrs_t addrM = *(addr);
	char *return_data_char = (char *) return_data;

	memcpy(return_data_char, addrM, size);
			
	VFree(addr);
	
}
