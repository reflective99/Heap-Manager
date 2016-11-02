#include <stdio.h>  // needed for size_t
#include <unistd.h> // needed for sbrk
#include <assert.h> // needed for asserts
#include "dmm.h"

typedef struct metadata {
  /* size_t is the return type of the sizeof operator. Since the size of an
   * object depends on the architecture and its implementation, size_t is used
   * to represent the maximum size of any object in the particular
   * implementation. size contains the size of the data object or the number of
   * free bytes
   */
  size_t size;
  struct metadata* next;
  struct metadata* prev;
  bool allocated;
} metadata_t;

typedef struct footer {
  size_t size;
  bool allocated;
} footer;


/* freelist maintains all the blocks which are not in use; freelist is kept
 * sorted to improve coalescing efficiency
 */

static metadata_t* freelist = NULL;

// Global Variables for Prologue and Epilogue
static metadata_t* pro = NULL;
static metadata_t* ep = NULL;


// Implementation of the malloc function

void* dmalloc(size_t numbytes) {

  /* initialize through sbrk call first time */
  if(freelist == NULL) {
    if(!dmalloc_init())
      return NULL;
  }
  //printf("start of freelist %p\n",(void*)freelist);
  assert(numbytes > 0);
  size_t numbytes_aligned = ALIGN(numbytes);
  if(numbytes_aligned > 1024){
    return NULL;
  }

  metadata_t* currentheader = freelist;

  while(currentheader->next != NULL){

    if(currentheader->size >= numbytes_aligned){

      //if split
      if((int)numbytes_aligned <= (int)(currentheader->size - METADATA_T_ALIGNED-sizeof(footer)-ALIGNMENT)){
        ////printf("inside second if....\n");
        metadata_t* newfreeblock = (void*)currentheader+numbytes_aligned + METADATA_T_ALIGNED + sizeof(footer);
      	newfreeblock->size = currentheader->size - numbytes_aligned - METADATA_T_ALIGNED - sizeof(footer);
        newfreeblock->allocated = false;
      	currentheader->size = numbytes_aligned;

        //free list linked list stuff
        currentheader->next->prev = newfreeblock;
      	currentheader->prev->next = newfreeblock;
      	newfreeblock->next = currentheader->next;
        newfreeblock->prev = currentheader->prev;

        if(currentheader == freelist){
          freelist = newfreeblock;
        }

        currentheader->allocated = true;

        //footer
        //currentheader->size -= sizeof(footer);//allocated block loses space the size of a new footer
        footer* newfooter = (void*)newfreeblock - sizeof(footer);//put before the new, split, newfreeblock
        newfooter->size = currentheader->size;
        newfooter->allocated = true;
        footer* existingfooter = (void*)newfreeblock + newfreeblock->size + METADATA_T_ALIGNED;//find the footer of the whole block, now it's the smaller-size footer of the newly split free block
        existingfooter->size = newfreeblock->size;
        existingfooter->allocated = newfreeblock->allocated;

      } else {
        //else
        currentheader->prev->next = currentheader->next;
        currentheader->next->prev = currentheader->prev;

        if(currentheader == freelist){
          freelist = currentheader->next;
        }

        freelist->prev = pro;
        currentheader->allocated = true;
      }

      return (void*)currentheader+METADATA_T_ALIGNED;
    }
     currentheader = currentheader->next;
  }
  //printf("\n\nreturning null\n\n");
  return NULL;
}

// Implementation of the free function

void dfree(void* ptr) {

  metadata_t* freehead = ptr - METADATA_T_ALIGNED;

  if(freehead->allocated == false){
    return;
  }

  freehead->allocated = false;
  footer* freefoot = (void*)freehead + freehead->size + METADATA_T_ALIGNED;
  freefoot->allocated = false;

  ////printf("In free!!!\n\n");
  bool leftcoal = false;
  bool rightcoal = false;

  //printf("%d right, %d left\n\n", rightcoal, leftcoal);

  metadata_t* righthead = (void*)freefoot + sizeof(footer);
  rightcoal = !righthead->allocated;

  //printf("%d rightallocated\n\n", righthead->allocated);

  //there is a free block to the left of freehead
  if((void*)freelist < (void*)freehead){
    footer* leftfoot = (void*)freehead - sizeof(footer);
    leftcoal = !leftfoot->allocated;
  }

  // DEBUG
  if(rightcoal){
    //printf("%d right, %d left PRINT THIS\n\n", rightcoal, leftcoal);
  }

  // Done!
  if(rightcoal && leftcoal){
    footer* leftfoot = (void*)freehead - sizeof(footer);
    metadata_t* lefthead = (void*)leftfoot - leftfoot->size - METADATA_T_ALIGNED;
    footer* rightfoot = (void*)righthead + righthead->size + METADATA_T_ALIGNED;
    lefthead->size += 2*sizeof(footer) + 2*METADATA_T_ALIGNED + freehead->size + righthead->size;
    lefthead->next = righthead->next;
    righthead->next->prev = lefthead;
  }

  // Also Done!
  if(!rightcoal && leftcoal){
    footer* leftfoot = (void*)freehead - sizeof(footer);
    metadata_t* lefthead = (void*)leftfoot - leftfoot->size - METADATA_T_ALIGNED;
    lefthead->size += sizeof(footer) + METADATA_T_ALIGNED + freehead->size;
    freefoot->size = lefthead->size;
  }

  // Also also Done!
  if(rightcoal && !leftcoal){
    //printf("Right Coal\n\n");
    freehead->size = freehead->size + righthead->size + sizeof(footer) + METADATA_T_ALIGNED;
    footer* rightfoot = (void*)righthead + righthead->size + METADATA_T_ALIGNED;
    rightfoot->size = freehead->size;

    //free list
    righthead->next->prev = freehead;
    righthead->prev->next = freehead;

    if((void*)freelist > (void*)freehead){
      freelist = freehead;
    }
  }

  // Also also also Done
  if(!rightcoal && !leftcoal){

    metadata_t* left = freelist;
    metadata_t* right = left->next;
    if((void*)left > (void*)freehead){
      freehead->next = freelist;
      freelist->prev = freehead;
      freelist = freehead;

    } else {

      while(right!=NULL){

        if((void*)left < (void*)freehead && (void*)freehead < (void*)right){
          freehead->prev = left;
          freehead->next = right;
          left->next = freehead;
          right->prev = freehead;
          return;
        }

        right = right->next;
        left = left->next;
      }
    }
  }
}

bool dmalloc_init() {

  /* Two choices:
   * 1. Append prologue and epilogue blocks to the start and the
   * end of the freelist
   *
   * 2. Initialize freelist pointers to NULL
   *
   */

  size_t max_bytes = ALIGN(MAX_HEAP_SIZE);
  /* returns heap_region, which is initialized to freelist */
  freelist = (metadata_t*) sbrk(max_bytes);


  if (freelist == (void *)-1)
    return false;
  //setting the pro and ep

  //printf("Inside init...\n\n");
  //printf("start of heap is: %p", (void*)freelist);
  pro = freelist;
  pro->size = 0;
  pro->next = (void*)freelist + METADATA_T_ALIGNED;
  pro->prev = NULL;
  pro->allocated = true;

  //printf("After pro and ep...\n\n");
  freelist = (void*)freelist + METADATA_T_ALIGNED;
  freelist->prev = pro;
  freelist->size = max_bytes-2*METADATA_T_ALIGNED; //minus pro and header
  freelist->allocated = false;

  metadata_t* ep = (void*)pro+max_bytes-METADATA_T_ALIGNED;
  freelist->size -= METADATA_T_ALIGNED; //minus ep
  ep->size = 0;
  ep->next = NULL;
  ep->prev = freelist;
  ep->allocated = true;
  //printf("freelist start init: %p\n\n", (void *)freelist);

  freelist->next = ep;

  freelist->size -= ALIGN(sizeof(footer)); //minus footer
  footer* freelistfooter = (void*)ep-ALIGN(sizeof(footer));
  freelistfooter->size = freelist->size;
  freelistfooter->allocated = freelist->allocated;


  //printf("size of freelist... %zd\n\n", freelist->size);

  //pro = malloc(sizeof(metadata_t));
  //ep = malloc(sizeof(metadata_t));
  //pro->next = freelist;
  //freelist->next = ep;
  //freelist->prev = pro;
  //ep->prev = freelist;
  //ep->next = NULL;

  /*
    metadata_t* pro = freelist;
    metadata_t* ep;
    pro->size = 0;
    ep->size = 0;
  */
  return true;
}

/* for debugging; can be turned off through -NDEBUG flag*/
void print_freelist() {
  metadata_t *freelist_head = freelist;
  while(freelist_head != NULL) {
    DEBUG("\tFreelist Size:%zd, Head:%p, Prev:%p, Next:%p\t",
	  freelist_head->size,
	  freelist_head,
	  freelist_head->prev,
	  freelist_head->next);
    freelist_head = freelist_head->next;
  }
  DEBUG("\n");
}

/*
void* pmalloc(size_t request){
  printf("dmallocing %d bytes\n",(int)request);
  printf("aligned, that is %d bytes\n",(int)ALIGN(request));
  void* ptr = dmalloc(request);
  printf("allocated address: %p\n",ptr);
  print_freelist();
  printf("------------------------\n");
  return ptr;
}
void pfree(void* ptr){
  printf("freeing ptr %p\n",ptr);
  dfree(ptr);
  print_freelist();
  printf("free<<<<<<<<<<<<<<<<<<<<<<\n");
}

int main(void){
  //printf("\n\n\n\n\n\n\n\n\n\nSTARTING DEBUG ++++++++++++++++++++++++++\n\n\n\n\n");

  void* ptr1 = pmalloc(8);
  void* ptr2 = pmalloc(9);
  void* ptr3 = pmalloc(16);
  void* ptr4 = pmalloc(11);
  void* ptr5 = pmalloc(100);
  pfree(ptr2);
  pfree(ptr3);
  pfree(ptr5);
  pfree(ptr4);
  //pmalloc();
  //pmalloc();
  //pmalloc();
  //pmalloc();



  //while(true){
    int numLoops = 100;

    void* pointers[numLoops];
    int i;
    int a;
    for(i = 0; i < numLoops; i++){
      //printf("\n%d\n",i);
      int request = rand()%10+1;
      void* ptr = dmalloc((size_t)(request));
      if(ptr==NULL){
        break;
      }
      pointers[i] = ptr;

      if(rand()%4>0){
      while(true){
        //printf("ahhhhh");
        int freeindex = rand()%(i+1);
        if(pointers[freeindex]==0){
          continue;
        }
        dfree((void*)pointers[freeindex]);
        pointers[freeindex] = 0;
        break;
      }
      }
      print_freelist();
    }
  //}
  //pfree(ptr1);



  return 0;

}
*/
