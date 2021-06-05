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
static void *find_first_fit(size_t asize);
static void *coalesce(void *bp);

typedef char *addrs_t;
typedef void *any_t;

addrs_t baseptr = 0;

/* Initialize M1 region of size bytes */
void Init(size_t size) {
	
	if (size == 0) {
		
		printf("attempt to initialize M2 of 0 bytes \n");
		return;
	}

	baseptr = (addrs_t)malloc(size);
	unsigned long long shift = ((unsigned long long)(baseptr) % 8);
	baseptr = (char *)(baseptr) + shift;				//aligned start address of M1
		
	PUT(baseptr, 0);						//alignment padding
	PUT(baseptr + 4, PACK(8, 1));					//prologue header
	PUT(baseptr + 8, PACK(8, 1));					//prologue footer
	PUT(baseptr + 12, PACK(size - 16 - shift, 0));			//header for initial free block chunk
	PUT(baseptr + size - 8 - shift, PACK(size - 16 - shift, 0));	//footer for intitial free block chunk
	PUT((baseptr + size - 8 - shift), PACK(0, 1));			//epilogue

	baseptr += 16; 		//baseptr points to payload
	
	num_free_blks++;
	Rtotal_free_bytes += (size - shift - 24);
}

/* Allocates size bytes in M1. */
addrs_t Malloc(size_t size) {
	//RDTSC(start);
	total_malloc_reqs;

	/* check bad request */
	if (size == 0) {
		printf("can't malloc 0 bytes \n");
		total_req_fails++;
		return NULL;
	}

	size_t asize;
	char *bp;
	
	/* adjust for overhead and alignment */
	if (size <= 8)
		asize = 16;	
	
	else
		asize = 8 * ((size + 15) / 8);
	
	if ((bp = find_first_fit(asize)) != NULL){	//found fit

		place(bp, asize);
		
		num_alloc_blks++;
		Rtotal_alloc_bytes += size;

		/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_malloc_cycles += time;
	avg_malloc_cycles = total_malloc_cycles / total_malloc_reqs; */

		return bp;
	
	}
	else {	//no fit found
	
		total_req_fails++;
		
		/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_malloc_cycles += time;
	avg_malloc_cycles = total_malloc_cycles / total_malloc_reqs; */

		return NULL;

	}

}

/* Deallocates block at addr in M1. */
void Free(addrs_t addr) {
	
	//RDTSC(start);
	total_free_reqs++;

	/* check bad requests */
	if (addr == NULL) {
		printf("invalid address \n");
		total_req_fails++;
		return;
	}
		
	size_t size = GET_SIZE (HDRP (addr));

	PUT (HDRP (addr), PACK (size, 0));
	PUT (FTRP (addr), PACK (size, 0));
	coalesce (addr);
	
	num_alloc_blks--;
	Rtotal_alloc_bytes -= (size - 8);
	Ptotal_alloc_bytes -= size;
	/*RDTSC(finish);
	long time = (long)(finish - start);
	total_cycles += time;
	total_free_cycles += time;
	avg_free_cycles = total_free_cycles / total_free_reqs;*/ 
}

/* Helper function for Malloc.
 * Updates size and allocated bit for newly allocated block.
 * Splits block into allocated and free if minimum block size met */
static void place (void *bp, size_t asize){
	
	size_t csize = GET_SIZE (HDRP (bp));
	if ((csize-asize) >= 16){		//split block and create free block
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize-asize, 0));
		PUT(FTRP(bp), PACK(csize-asize, 0));
		
		Ptotal_alloc_bytes += asize;		
		
	}

	else{					//can't make free block of minimum size
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		Ptotal_alloc_bytes += csize;
		num_free_blks--;
	}
}

/* Helper function for Malloc.
 * Locates first free block in M1 that fits asize bytes. */
static void *find_first_fit(size_t asize){
	void *bp;
	for (bp = baseptr; GET_SIZE(HDRP(bp))>0; bp = NEXT_BLKP(bp)){
		
		if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
			return bp;
	}
	return NULL;
}

/* Helper function for Free.
 * Coalesces contiguous free blocks into single free block. */
static void *coalesce(void *bp){

	size_t prev_alloc = GET_ALLOC (FTRP (PREV_BLKP (bp)));
	size_t next_alloc = GET_ALLOC (HDRP (NEXT_BLKP (bp)));
	size_t size = GET_SIZE (HDRP (bp));

	if (prev_alloc && next_alloc) {	/* Case 1 */
		num_free_blks++;
		
		return bp;
	}

	else if (prev_alloc && !next_alloc) {	/* Case 2 */
		size += GET_SIZE (HDRP (NEXT_BLKP (bp)));
		PUT (HDRP (bp), PACK (size, 0));
		PUT (FTRP (NEXT_BLKP (bp)), PACK (size, 0));
	}

	else if (!prev_alloc && next_alloc) {	/* Case 3 */
		size += GET_SIZE (HDRP (PREV_BLKP (bp)));
		PUT (FTRP (bp), PACK (size, 0));
		PUT (HDRP (PREV_BLKP (bp)), PACK (size, 0));
		bp = PREV_BLKP (bp);
	}

	else {						/* Case 4 */
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
		
		num_free_blks--;
	}
	return bp;
}

/* Copy size bytes of data into allocated region of M2 */
addrs_t Put(any_t data, size_t size){
	
	/* check bad requests */
	if (baseptr == 0) {
		printf("M1 uninitialized \n");
		return NULL;
	}
	if (data == NULL) {
		printf("invalid data");
		return NULL;
	}
	
	addrs_t bp = Malloc(size);
	
	if (bp == NULL) {
		printf("no fit for data found \n");
		return NULL;
	}	
	
	char *temp = (char *) data;
	memcpy(bp, temp, size);
			
	return bp;
	
}
/* Copies size bytes from address addr in M1 to return_data.
 * Then frees block pointed to by addr */
void Get(any_t return_data, addrs_t addr, size_t size){
	
	/* check bad request */
	if (baseptr == 0) {
		printf("M1 uninitialized");
		return;
	}
	if ((addr == NULL) || (return_data == NULL)) {
		printf("invalid address");
		return;
	}
	
	char *temp = (char *) return_data;
	memcpy(temp, addr, size);
			
	Free(addr);
	
}

/* Prints heap checker statistics */
void HEAP_CHECKER() {
	
	printf("Number of allocated blocks: %ld \n", num_alloc_blks);
	printf("Number of free blocks: %ld \n", num_free_blks);
	printf("Raw total number of bytes allocated: %ld \n", Rtotal_alloc_bytes);
	printf("Padded total number of bytes allocated: %ld \n", Ptotal_alloc_bytes);
	printf("Raw total number of bytes free: %ld \n", Rtotal_free_bytes);
	printf("Aligned total number of bytes free: %ld \n", Atotal_free_bytes);
	printf("Total number of Malloc requests: %ld \n", total_malloc_reqs);
	printf("Total number of Free requests: %ld \n", total_free_reqs);
	printf("Total number of request failures: %ld \n", total_req_fails);
	printf("Average clock cycles for a Malloc request: %ld \n", avg_malloc_cycles);
	printf("Average clock cycles for a Free request: %ld \n", avg_free_cycles);
	printf("Total clock cycles for all requests: %ld \n", total_cycles);	

}	
