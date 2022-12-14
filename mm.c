

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>

#include "memlib.h"
#include "mm.h"


typedef uint64_t word_t;

// Word and header size (bytes)
static const size_t wsize = sizeof(word_t);

// Double word size (bytes)
static const size_t dsize = 2 * sizeof(word_t);

/*
  Minimum useable block size (bytes):
  two words for header & footer, two words for payload
*/
static const size_t min_block_size = 4 * sizeof(word_t);

/* Initial heap size (bytes), requires (chunksize % 16 == 0)
*/
static const size_t chunksize = (1 << 12);    

// Mask to extract allocated bit from header
static const word_t alloc_mask = 0x1;

/*
 * Assume: All block sizes are a multiple of 16
 * and so can use lower 4 bits for flags
 */
static const word_t size_mask = ~(word_t) 0xF;

/*
  All blocks have both headers and footers

  Both the header and the footer consist of a single word containing the
  size and the allocation flag, where size is the total size of the block,
  including header, (possibly payload), unused space, and footer
*/

typedef struct block block_t;

/* Representation of the header and payload of one block in the heap */ 
struct block
{
    /* Header contains: 
    *  a. size
    *  b. allocation flag 
    */
    word_t header;

    union
    {
        struct
        {
            block_t *prev;
            block_t *next;
        } links;
        /*
        * We don't know what the size of the payload will be, so we will
        * declare it as a zero-length array.  This allows us to obtain a
        * pointer to the start of the payload.
        */
        unsigned char data[0];

    /*
     * Payload contains: 
     * a. only data if allocated
     * b. pointers to next/previous free blocks if unallocated
     */
    } payload;

    /*
     * We can't declare the footer as part of the struct, since its starting
     * position is unknown
     */
};

/* Global variables */

// Pointer to first block
static block_t *heap_start = NULL;

// Pointer to the first block in the free list
static block_t *free_list_head = NULL;

/* Function prototypes for internal helper routines */

static size_t max(size_t x, size_t y);
static block_t *find_fit(size_t asize);
static block_t *coalesce_block(block_t *block);
static void split_block(block_t *block, size_t asize);

static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc);

static size_t extract_size(word_t header);
static size_t get_size(block_t *block);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);

static void write_header(block_t *block, size_t size, bool alloc);
static void write_footer(block_t *block, size_t size, bool alloc);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);
static word_t *header_to_footer(block_t *block);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

static bool check_heap();
static void examine_heap();

static block_t *extend_heap(size_t size);
static void insert_block(block_t *free_block);
static void remove_block(block_t *free_block);

/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    word_t *start = (word_t *)(mem_sbrk(2*wsize));
    if ((size_t)start == -1) {
        printf("ERROR: mem_sbrk failed in mm_init, returning %p\n", start);
        return -1;
    }
    
    /* Prologue footer */
    start[0] = pack(0, true);
    /* Epilogue header */
    start[1] = pack(0, true); 

    /* Heap starts with first "block header", currently the epilogue header */
    heap_start = (block_t *) &(start[1]);

    /* Extend the empty heap with a free block of chunksize bytes */
    block_t *free_block = extend_heap(chunksize);
    if (free_block == NULL) {
        printf("ERROR: extend_heap failed in mm_init, returning");
        return -1;
    }

    /* Set the head of the free list to this new free block */
    free_list_head = free_block;
    free_list_head->payload.links.prev = NULL;
    free_list_head->payload.links.next = NULL;

    return 0;
}

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
void *mm_malloc(size_t size)
{
    size_t asize;      // Allocated block size
    block_t *bp;        //I make a new block pointer

    if (size == 0) // Ignore spurious request
        return NULL;

    // Too small block
    if (size <= dsize) {        
        asize = min_block_size;
    } else {
        // Round up and adjust to meet alignment requirements    
        asize = round_up(size + dsize, dsize);
    }

  // TODO: Implement mm_malloc.  You can change or remove any of the above
  // code.  It is included as a suggestion of where to start.
  // You will want to replace this return statement...

  //Ok so I guess I need to find use find fit first.
  //Now does the size need to be just asize, or should it be asize + sizeof(header) + sizeof(footer)?
  //I don't think so because like, the header and footer are just part of the block. So asize should already account for them
  //right?
  
  if((bp = find_fit(asize)) == NULL)  //If the heap has nothing free
  {
      bp = extend_heap(asize * 2);      //extend it by 2 so we have to extend it less
  }
   // insert_block(bp);  //Called this in extend so I don't need it here
  
  if(bp == NULL)
  {
      return NULL;
  }

  
  write_header(bp, get_size(bp), 1);
  write_footer(bp, get_size(bp), 1);
                                
  split_block(bp, asize);

  remove_block(bp);

  return header_to_payload(bp);         //This needs to just return since the return type is void.

}


/* 
 * mm_free - Free a block 
 */
void mm_free(void *bp)
{

    if (bp == NULL)
        return;

    block_t *block = payload_to_header(bp);

    bool bpAlloc = get_alloc(block);    //Use this for if the alloc bit is 0 i just return, idk maybe makes it incrementally faster.
   
    size_t bpSize;                  
    
    bpSize = get_size(block);          //Get the size of the current block
    
   

    if (bpAlloc == 0)               //Just leave if what they want to free is already free
        return;
    
    
    write_header(block, bpSize, 0);  //So i just keep the size and all the same, just change the alloc bit from true to false.
    write_footer(block, bpSize, 0);

                                //So i don't call insert block here since i do it in coalesce_block

                                //Do i want to worry about having adjacent free blocks or should I leave that till i write coalesce?
    
    insert_block(block);

    coalesce_block(block);

    return;
    // TODO: Implement mm_free

}

/*
 * insert_block - Insert block at the head of the free list (e.g., LIFO policy)
 */
static void insert_block(block_t *free_block)
{
    
    if(free_list_head == NULL)  //If the list is currently empty
    {
        free_list_head = free_block;
        free_list_head->payload.links.next = NULL;
        free_list_head->payload.links.prev = NULL;
        
        return;
    }

    free_block->payload.links.next = free_list_head;
    free_block->payload.links.prev = NULL;
    
    free_list_head->payload.links.prev = free_block;
    free_list_head = free_block;

    return;
  /* 
    block_t *curr = free_list_head;

    while(curr->payload.links.next != NULL)
    {
        curr = curr->payload.links.next;
    }

    curr->payload.links.next = free_block;
    free_block->payload.links.next = NULL;
    free_block->payload.links.prev = curr;
    
    return;
    */
                                            
   
	// TODO: Implement insert block on the free list

}

/*
 * remove_block - Remove a free block from the free list
 */
static void remove_block(block_t *free_block) 
{
   

    if(free_block->payload.links.prev == NULL)  //If it's the first thing in the list
    {
        if(free_block->payload.links.next == NULL)  //If it's also the only thing in the list
        {
            free_block = NULL;   //New thing, this seems right
            free_list_head = NULL; //This is new, now i just segfault instead of getting the payload overlaps, poggers?

            return;
        }
        else                                        //If it's not the only thing in the list
        {
            block_t *next_block = free_block->payload.links.next;
            next_block->payload.links.prev = NULL;
            free_list_head = next_block;
            free_block = NULL;
            return;
        }
    }


    if((free_block->payload.links.next == NULL) && (free_block->payload.links.prev != NULL))  //If it's the last thing in the last
    {
        block_t *prev_block = free_block->payload.links.prev;   
        prev_block->payload.links.next = NULL;
        free_block = NULL;  
        return;
    }
    
    if((free_block->payload.links.next != NULL) && (free_block->payload.links.prev != NULL)) //if it's not on either end
    {
    block_t *prev_block = free_block->payload.links.prev;
    block_t *next_block = free_block->payload.links.next;

    prev_block->payload.links.next = next_block;  

    next_block->payload.links.prev = prev_block;  //these two lines adjust the next and prev to go past the block we will remove
   
    free_block = NULL;
   
    return;
    }


    return;



    // TODO: Implement remove block from the free list 

}

/*
 * Finds a free block that of size at least asize
 */
static block_t *find_fit(size_t asize)  //first fit
{
   /* 
   
   block_t * bp;   //make a block pointer
    
   // bp = find_next(heap_start);  //Point at the top of heap
        bp = heap_start;
    

    while(get_size(bp) != 0)   //While we are not at the end of the heap
                            //Althought you know what, will it count the epilogue header as not null? Could that mess stuff up?
    {
        if((get_size(bp) >= asize) && (get_alloc(bp) == 0))      //If the size of the free block is greater than the size needed
        {
           
            return bp;     //Is this all I need to do to put it in? Is this is? Seems too easy idk
        }
        bp = find_next(bp);

    }
*/
    block_t *curr = free_list_head;

    while(curr != NULL) //While we not at the end of the list
    {   
        if(get_size(curr) >= asize)
        {
            //remove_block(curr);  //I must have to do this, because we are gonna use it so it won't be free anymore
            return curr;
        }
        curr = curr->payload.links.next;
    }
   
    
    return NULL; // no fit found
}

/*
 * Coalesces current block with previous and next blocks if either or both are unallocated; otherwise the block is not modified.
 * Returns pointer to the coalesced block. After coalescing, the immediate contiguous previous and next blocks must be allocated.
 */
static block_t *coalesce_block(block_t *block)
{

    block_t *prev_block = find_prev(block); 
    block_t *next_block = find_next(block);
    //block_t *old_block = block;
    size_t combined_block = get_size(block);

   
	  
       if(get_alloc(next_block) == 0)        //If the next block is also free
      {
         

          combined_block += get_size(next_block);

           write_header(block, combined_block, 0);
           write_footer(block, combined_block, 0);
          
           remove_block(next_block); //Ok so I need this here since now the next_block is combined with block, so it ain't really there.

      }
     
      if(extract_alloc(*find_prev_footer(block)) == 0)     //If the previous block is also free
      {

           

          combined_block += get_size(prev_block);

          write_header(prev_block, combined_block, 0); 
          write_footer(prev_block, combined_block, 0);  

          remove_block(block);  //Same idea with this one. Combine prev block and the current block, now current block is just a part of prev block


           block = prev_block;      //This is needed for if there are three in a row that are free
      }

     
                   
    return block;
}

/*
 * See if new block can be split one to satisfy allocation
 * and one to keep free
 */
static void split_block(block_t *block, size_t asize)
{
    
	if((get_size(block) - asize) >= min_block_size)          //If the block has enough leftover to be more than the minimum size
    {
        size_t *next_block_size = get_size(block) - asize;

        write_header(block, asize, 1);                       //rewrite the header and footer to only use the space needed
        write_footer(block, asize, 1);                      
                                                            
        //remove_block(block);  //Now I do this in malloc so don't need it here

        write_header(find_next(block), next_block_size, 0); //write headers and footers for the new free part
        write_footer(find_next(block), next_block_size, 0);

        insert_block(find_next(block));

        //Would call insert here, but since I do it in coalesce I don't need to here

       coalesce_block(find_next(block));
    }

  // remove_block(block); //Coalesce has the remove stuff I need so don't need this here

    return;

}


/*
 * Extends the heap with the requested number of bytes, and recreates end header. 
 * Returns a pointer to the result of coalescing the newly-created block with previous free block, 
 * if applicable, or NULL in failure.
 */
static block_t *extend_heap(size_t size) 
{
    void *bp;

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk(size)) == (void *)-1) {
        return NULL;
    }

    // bp is a pointer to the new memory block requested
     
    // TODO: Implement extend_heap.
   

    bp = find_prev_footer(bp);  //old epilogue header is this blocks header

    block_t *bp_next;

    write_header( bp, size, 0);  //This write the header. alloc is 0 since this is free right?
    write_footer( bp, size, 0);

    insert_block(bp);


    bp_next = find_next(bp);
     

    write_header( bp_next, 0, 1);

  

    return coalesce_block(bp); 



  
   
}

/******** The remaining content below are helper and debug routines ********/

/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void* p)
{
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * examine_heap -- Print the heap by iterating through it as an implicit free list. 
 */
static void examine_heap() {
  block_t *block;

  /* print to stderr so output isn't buffered and not output if we crash */
  fprintf(stderr, "free_list_head: %p\n", (void *)free_list_head);

  for (block = heap_start; /* first block on heap */
      get_size(block) > 0 && block < (block_t*)mem_heap_hi();
      block = find_next(block)) {

    /* print out common block attributes */
    fprintf(stderr, "%p: %ld %d\t", (void *)block, get_size(block), get_alloc(block));

    /* and allocated/free specific data */
    if (get_alloc(block)) {
      fprintf(stderr, "ALLOCATED\n");
    } else {
      fprintf(stderr, "FREE\tnext: %p, prev: %p\n",
      (void *)block->payload.links.next,
      (void *)block->payload.links.prev);
    }
  }
  fprintf(stderr, "END OF HEAP\n\n");
}


/* check_heap: checks the heap for correctness; returns true if
 *               the heap is correct, and false otherwise.
 */
static bool check_heap()
{

    // Implement a heap consistency checker as needed.

    /* Below is an example, but you will need to write the heap checker yourself. */

    if (!heap_start) {
        printf("NULL heap list pointer!\n");
        return false;
    }

    block_t *curr = heap_start;
    block_t *next;
    block_t *hi = mem_heap_hi();

    while ((next = find_next(curr)) + 1 < hi) {
        word_t hdr = curr->header;
        word_t ftr = *find_prev_footer(next);

        if (hdr != ftr) {
            printf(
                    "Header (0x%016lX) != footer (0x%016lX)\n",
                    hdr, ftr
                  );
            return false;
        }

        curr = next;
    }

    return true;
}


/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *****************************************************************************
 */

/*
 * max: returns x if x > y, and y otherwise.
 */
static size_t max(size_t x, size_t y)
{
    return (x > y) ? x : y;
}


/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n)
{
    return n * ((size + (n-1)) / n);
}


/*
 * pack: returns a header reflecting a specified size and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise.
 */
static word_t pack(size_t size, bool alloc)
{
    return alloc ? (size | alloc_mask) : size;
}


/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word)
{
    return (word & size_mask);
}


/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block)
{
    return extract_size(block->header);
}


/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word)
{
    return (bool) (word & alloc_mask);
}


/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block)
{
    return extract_alloc(block->header);
}


/*
 * write_header: given a block and its size and allocation status,
 *               writes an appropriate value to the block header.
 */
static void write_header(block_t *block, size_t size, bool alloc)
{
    block->header = pack(size, alloc);
}


/*
 * write_footer: given a block and its size and allocation status,
 *               writes an appropriate value to the block footer by first
 *               computing the position of the footer.
 */
static void write_footer(block_t *block, size_t size, bool alloc)
{
    word_t *footerp = header_to_footer(block);
    *footerp = pack(size, alloc);
}


/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block)
{
    return (block_t *) ((unsigned char *) block + get_size(block));
}


/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block)
{
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}


/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block)
{
    word_t *footerp = find_prev_footer(block);
    size_t size = extract_size(*footerp);
    return (block_t *) ((unsigned char *) block - size);
}


/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp)
{
    return (block_t *) ((unsigned char *) bp - offsetof(block_t, payload));
}


/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload data.
 */
static void *header_to_payload(block_t *block)
{
    return (void *) (block->payload.data);
}


/*
 * header_to_footer: given a block pointer, returns a pointer to the
 *                   corresponding footer.
 */
static word_t *header_to_footer(block_t *block)
{
    return (word_t *) (block->payload.data + get_size(block) - dsize);
}
