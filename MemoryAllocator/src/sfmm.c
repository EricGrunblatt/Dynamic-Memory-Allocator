/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"
#include "init.h"

void initAll() {
    /*** ALLOCATE SPACE RIGHT AWAY ***/
    sf_mem_grow();

    /*** INITIALIZE PROLOGUE ***/
    void *memStart = sf_mem_start();
    // Add 24 to skip over the 3 unused rows
    memStart += 24;
    sf_block *prologueP = (sf_block *)memStart;
    // 32 indicates size, THIS_BLOCK_ALLOCATED allocates block
    prologueP->header = 32 + THIS_BLOCK_ALLOCATED;
    // Add 24 to get from header to footer
    memStart += 24; 
    sf_footer *prologueF = (sf_footer *)memStart;
    // 32 indicates size, THIS_BLOCK_ALLOCATED allocates block
    (*prologueF) = 32 + THIS_BLOCK_ALLOCATED; 

    /*** INITIALIZE FREE BLOCK ***/
    // Add 8 to get the starting address of the newly allocated page
    memStart += 8;
    sf_block *freeBlock = (sf_block *)memStart;
    // Allocate size of page in header, keep it free
    freeBlock->header = PAGE_SZ - 64;
    freeBlock->body.links.next = freeBlock;
    freeBlock->body.links.prev = freeBlock;
    // Add PAGE_SIZE, but subtract 8 to get the start of the footer
    memStart += PAGE_SZ- 64 - 8;
    sf_footer *freeBlockF = (sf_footer *)memStart;
    (*freeBlockF) = PAGE_SZ - 64;

    /*** INITIALIZE EPILOGUE ***/
    void *memEnd = sf_mem_end();
    // Subtract 32 to get to the start of the header
    memEnd -= 8;
    sf_header *epilogueP = (sf_header *)memEnd;
    // Make size 32 and make sure this block is allocated
    *epilogueP = THIS_BLOCK_ALLOCATED;
    
    /*** INITIALIZE FREE LISTS ***/
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
    sf_free_list_heads[7].body.links.next = freeBlock;
    sf_free_list_heads[7].body.links.prev = freeBlock;
    freeBlock->body.links.next = &sf_free_list_heads[7];
    freeBlock->body.links.prev = &sf_free_list_heads[7];
}

int isValidPointer(void *pp) {
    // The pointer is null
    if(pp == NULL) return 0;

    // The pointer is not 32-byte aligned
    size_t isAligned = (size_t) pp % 32;
    if (isAligned != 0) return 0;

    // Get the block set up to check its contents
    void *startAddr = pp - 8;
    sf_block *blockCheck = (sf_block *)startAddr;
    size_t blockSize = blockCheck->header;

    // The allocated bit in the header is 0
    if((blockSize & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) return 0;

    // Get the sizes
    blockSize -= THIS_BLOCK_ALLOCATED;
    startAddr += (blockSize - 8);
    sf_footer *blockF = (sf_footer *)startAddr;

    // The block size is less than the minimum block size of 32
    if(blockSize < 32) return 0;

    // The block size is not a multiple of 32
    if(blockSize % 32 != 0) return 0;

    // Header and footer are not equal
    if(blockSize != (*blockF - 16)) return 0;

    // The header of the block is before the start of the first block of the heap
    void *heapStart = sf_mem_start();
    // Add to account for 3 unused rows (24 bytes) and prologue (32 bytes)
    heapStart += 56;
    if((pp - 8) < heapStart) return 0;

    // Or the footer of the block is after the end of the last block in the heap
    void *heapEnd = sf_mem_end();
    // Subtract 1 unused row for epilogue (8 bytes)
    heapEnd -= 8;
    if(startAddr > heapEnd) return 0;

    // Otherwise, the pointer is valid
    return 1;
}

int findFreeListIndex(size_t size, int isLast) {
    size_t m = 32;
    if(isLast) return 7;
    else if(size == m) return 0;
    else if(size == (2 * m)) return 1;
    else if(size == (3 * m)) return 2;
    else if(size > (3 * m) && size <= (5 * m)) return 3;
    else if(size > (5 * m) && size <= (8 * m)) return 4;
    else if(size > (8 * m) && size <= (13 * m)) return 5;
    else return 6;

}

int removeFromFreeList(void *pp) {
    int isFound = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        if(isFound) break;
        sf_block *start = &sf_free_list_heads[i];
        sf_block *curr = sf_free_list_heads[i].body.links.next;
        while(curr != start) {
            if(curr == pp) {
                curr->body.links.next->body.links.prev = curr->body.links.prev;
                curr->body.links.prev->body.links.next = curr->body.links.next;
                isFound = 1;
                break;
            }
            curr = curr->body.links.next;
        }
    }
    return isFound;
}

int isPowerOfTwo(size_t size) {
    if(size == 0) return 0;
    while(size != 1) {
        size = size/2;
        if(size % 2 != 0 && size != 1) return 0;
    }
    return 1;
}

void *sf_malloc(size_t size) {
    // First check if heap is not initialized
    if(!isInit) {
        isInit = 1;
        initAll();
    }
    // Check if size > 0
    if(size <= 0) return NULL;

    // Making the size a multiple of 32, if not already
    size_t currSize = size;
    currSize += 16;
    if(currSize < 32) currSize = 32;
    size_t remainder = currSize % 32;
    size_t newSize = currSize;
    if(remainder != 0) {
        // Add padding to size
        newSize += (32 - remainder);
    }

    // Check Free Lists
    int hasFit = 0;
    void *returnPointer = NULL;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
        if(hasFit) break;
        // Starting pointer so while loop knows when to stop
        void *startingP = &sf_free_list_heads[i];
        sf_block *currBlock = sf_free_list_heads[i].body.links.next;
        while(currBlock != startingP) {

            // Go through
            if(newSize <= currBlock->header) {
                if((currBlock->header & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) {
                    if(newSize < currBlock->header) {
                        // To be used when making new header
                        size_t currSize = currBlock->header;
                        returnPointer = currBlock;
                        void *tempPointer = returnPointer;
                        // Update current header
                        currBlock->header = newSize;
                        if((currSize - newSize) < 32 && (currSize - newSize) != 0) continue;
                        currBlock->header += THIS_BLOCK_ALLOCATED;
                        tempPointer += (newSize - 8);
                        // Create new footer
                        sf_footer *allocF = (sf_footer *)tempPointer;
                        *allocF = currBlock->header;
                        tempPointer += 8;
                        // Create new block
                        sf_block *newFreeBlock = (sf_block *)tempPointer;
                        newFreeBlock->header = currSize - newSize;
                        tempPointer += (currSize - newSize - 8);
                        // Update current footer
                        sf_footer *freeF = (sf_footer *)tempPointer;
                        (*freeF) = currSize - newSize;

                        // Put free block in free list
                        void *memEnd = sf_mem_end();
                        memEnd -= 16;
                        int isEnd = 0;
                        if(memEnd == tempPointer) isEnd = 1;
                        int freeListIndex = findFreeListIndex(newFreeBlock->header, isEnd);
                        newFreeBlock->body.links.next = sf_free_list_heads[freeListIndex].body.links.next;
                        newFreeBlock->body.links.prev = &sf_free_list_heads[freeListIndex];
                        sf_free_list_heads[freeListIndex].body.links.next = newFreeBlock;
                        newFreeBlock->body.links.next->body.links.prev = newFreeBlock;
                    } else {
                        currBlock->header += THIS_BLOCK_ALLOCATED;
                        returnPointer = currBlock;
                        void *tempPointer = currBlock;
                        tempPointer += newSize - 8;
                        sf_footer *currBlockF = (sf_footer *)tempPointer;
                        *currBlockF = currBlock->header;
                    }
                    // Update free list
                    for(int j = 0; j < NUM_FREE_LISTS; j++) {
                        void *freeStartP = &sf_free_list_heads[j];
                        sf_block *currFree = sf_free_list_heads[j].body.links.next;
                        while(currFree != freeStartP) {
                            if((currFree->header & THIS_BLOCK_ALLOCATED) == THIS_BLOCK_ALLOCATED ||
                            currFree->header == 0) {
                                sf_block *temp = currFree->body.links.next;
                                currFree->body.links.prev->body.links.next = currFree->body.links.next;
                                temp->body.links.prev = currFree->body.links.prev;
                            }
                            currFree = currFree->body.links.next;
                        }
                        
                    }
                    hasFit = 1;
                    break;
                }
            }
            currBlock = currBlock->body.links.next;
        }
    }
    if(!hasFit) {
        void *extHeap = sf_mem_grow();
        if(extHeap == NULL) {
            sf_errno = ENOMEM;
            return NULL;
        }
        // Check if wilderness block exists
        if(&sf_free_list_heads[NUM_FREE_LISTS-1] != sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next) {
            sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next->header += PAGE_SZ;
            void *newEnd = sf_mem_end();
            newEnd -= 16;
            sf_footer *newFooter = (sf_footer *)newEnd;
            *newFooter = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next->header;
            newEnd += 8;
            sf_header *newEpilogue = (sf_header *)newEnd;
            *newEpilogue = THIS_BLOCK_ALLOCATED;
        } else {
            void *currEnd = sf_mem_end() - PAGE_SZ - 8;
            // Start of epilogue, but changing to block
            sf_block *newPage = (sf_block *)currEnd;
            newPage->header = PAGE_SZ;
            // Adding footer
            currEnd += (PAGE_SZ - 8);
            sf_footer *newPageFooter = (sf_footer *)currEnd;
            *newPageFooter = PAGE_SZ;

            // Adding new epilogue at end
            currEnd += 8;
            sf_header *newEp = (sf_header *)currEnd;
            *newEp += THIS_BLOCK_ALLOCATED;

            // Adding wilderness block
            newPage->body.links.next = sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next;
            newPage->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];
            sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next->body.links.prev = newPage;
            sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = newPage;
        }
        
        // Otherwise initialize normally
        return sf_malloc(size);
    }
    
    returnPointer += 8;
    return returnPointer;
}

void sf_free(void *pp) {
    // First check if heap is not initialized
    if(!isInit) {
        isInit = 1;
        initAll();
    }
    // Check if valid
    if(!isValidPointer(pp)) abort();

    // Check address starts at current address
    void *checkAddress = pp - 8;
    sf_block *allocBlock = (sf_block *)checkAddress;

    // Make allocBlock become free
    allocBlock->header -= 16;

    // Set the footer to the new header
    void *temp = checkAddress;
    temp += (allocBlock->header - 8);
    sf_footer *allocF = (sf_footer *)temp;
    *allocF = allocBlock->header;

    // Check previous and next sizes
    checkAddress -= 8;
    sf_footer *prev = (sf_footer *)checkAddress;
    checkAddress += (8 + allocBlock->header);
    sf_block *next = (sf_block *)checkAddress;

    if(((*prev & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) ||
    ((next->header & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED)) {
        if((*prev & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) {
            // Change block size
            void *currAddress = pp - 8;
            sf_block *curr = (sf_block *)currAddress;
            currAddress -= *prev;
            sf_block *prevCurr = (sf_block *)currAddress;
            prevCurr->header += curr->header;
            currAddress += (prevCurr->header - 8);
            sf_footer *prevCurrF = (sf_footer *)currAddress;
            *prevCurrF = prevCurr->header;

            // Remove prev from free list
            currAddress = (pp - 8 - *prev);

            removeFromFreeList(currAddress);

            if((next->header & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) {
                // Change block size
                void *nextBlock = next;
                currAddress = (nextBlock + next->header - 8);
                prevCurr->header += next->header;

                sf_footer *newPrevCurrF = (sf_footer *)currAddress;
                *newPrevCurrF = prevCurr->header;


                
                // Remove next from free list
                removeFromFreeList(next);    
            }
            // Add prevCurr to free list
            void *memEnd = sf_mem_end();
            memEnd -= 8;
            currAddress += 8;
            int isLast = 0;
            if(currAddress == memEnd) isLast = 1;
            int prevCurrIndex = findFreeListIndex(prevCurr->header, isLast);
            prevCurr->body.links.next = sf_free_list_heads[prevCurrIndex].body.links.next;
            prevCurr->body.links.prev = &sf_free_list_heads[prevCurrIndex];
            prevCurr->body.links.next->body.links.prev = prevCurr;
            sf_free_list_heads[prevCurrIndex].body.links.next = prevCurr;

        } else if((next->header & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) {
            // Change block size
            size_t nextSize = next->header;
            void *startAddress = (pp - 8);
            sf_block *currNext = (sf_block *)startAddress;
            currNext->header += nextSize;
            startAddress += (currNext->header - 8);
            sf_footer *currNextF = (sf_footer *)startAddress;
            *currNextF = currNext->header;

            // Remove from free list by finding the address
            removeFromFreeList(next);
            // Add to free list
            void *memEnd = sf_mem_end();
            memEnd -= 8;
            startAddress = currNext;
            startAddress += currNext->header;
            int isLast = 0;
            if(startAddress == memEnd) isLast = 1;
            int currNextIndex = findFreeListIndex(currNext->header, isLast);
            currNext->body.links.next = sf_free_list_heads[currNextIndex].body.links.next;
            currNext->body.links.prev = &sf_free_list_heads[currNextIndex];
            currNext->body.links.next->body.links.prev = currNext;
            sf_free_list_heads[currNextIndex].body.links.next = currNext;
            
        }
    } else {
        void *tempAddress = pp;
        tempAddress -= 8;
        sf_block *currBlock = (sf_block *)tempAddress;
        void *memEnd = sf_mem_end();
        memEnd -= 8;
        tempAddress += currBlock->header;
        int isLast = 0;
        if(tempAddress == memEnd) isLast = 1;
        int currIndex = findFreeListIndex(currBlock->header, isLast);
        currBlock->body.links.next = sf_free_list_heads[currIndex].body.links.next;
        currBlock->body.links.prev = &sf_free_list_heads[currIndex];
        currBlock->body.links.next->body.links.prev = currBlock;
        sf_free_list_heads[currIndex].body.links.next = currBlock;
    }
}

void *sf_realloc(void *pp, size_t rsize) {
    // First check if heap is not initialized
    if(!isInit) {
        isInit = 1;
        initAll();
    }

    if(!isValidPointer(pp)) {
        sf_errno = EINVAL;
        abort();
    } 
    if(rsize == 0) {
        sf_free(pp);
        return NULL;
    }
    void *mainAddr = pp - 8;
    sf_block *currBlock = (sf_block *)mainAddr;
    size_t currSize = currBlock->header - THIS_BLOCK_ALLOCATED;
    size_t newSize = rsize + 16;
    int rem = newSize % 32;
    if(rem != 0) newSize += (32 - rem);

    // Reallocating to a larger size
    if(currSize < newSize) {
        void *newPtr = sf_malloc(rsize);
        if(newPtr == NULL) return NULL;
        memcpy(newPtr, pp, currSize - 16);
        sf_free(pp);
        return newPtr;
    }
    // Reallocating to a smaller size
    else if(currSize > newSize) {
        if(currSize - newSize < 32 && (currSize - newSize) % 32 != 0) {
            // Don't split
            return pp;
        } else {
            // Split
            // Update current block header and footer to newSize
            currBlock->header = newSize + THIS_BLOCK_ALLOCATED;
            mainAddr += (currBlock->header - 8 - THIS_BLOCK_ALLOCATED);
            sf_footer *currBlockF = (sf_footer *)mainAddr;
            *currBlockF = currBlock->header;

            // Split to make the free block and set the header and footer
            mainAddr += 8;
            void *freeAddr = mainAddr;
            sf_block *newFreeBlock = (sf_block *)mainAddr;
            newFreeBlock->header = (currSize - newSize) + THIS_BLOCK_ALLOCATED;
            mainAddr += (newFreeBlock->header - 8 - THIS_BLOCK_ALLOCATED);
            sf_footer *newFreeBlockF = (sf_footer *)mainAddr;
            *newFreeBlockF = newFreeBlock->header;
            
            // Free the block
            sf_free(freeAddr + 8);
        }
    }
    return pp;
}

void *sf_memalign(size_t size, size_t align) {
    // First check if heap is not initialized
    if(!isInit) {
        isInit = 1;
        initAll();
    }
    
    if(align == 0) return NULL;
    if(!isPowerOfTwo(align) || align < 32) {
        sf_errno = EINVAL;
        return NULL;
    }
    // Requested size (size_t size) + alignment (size_t align) + min block size (32)
    void *newPtr = sf_malloc(size + align + 32);
    if(newPtr == NULL) return NULL;
    
    void *startPtr = newPtr - 8;
    // Check if block is aligned
    sf_block *newMem = (sf_block *)startPtr;
    size_t newSize = newMem->header - THIS_BLOCK_ALLOCATED;
    size_t shiftRight = (size_t) newPtr % align;
    if(shiftRight != 0) shiftRight = align - shiftRight;

    // If remainder > min block size
    if(shiftRight >= 32 && shiftRight % 32 == 0) {
        // Change header and footer to be the correct size
        newMem->header = shiftRight + THIS_BLOCK_ALLOCATED;
        void *currPtr = newPtr - 8;
        currPtr += (shiftRight - 8);
        sf_footer *newMemFtr = (sf_footer *)currPtr;
        *newMemFtr = newMem->header;

        // Create new block
        currPtr += 8;
        sf_block *alignedBlock = (sf_block *)currPtr;
        void *alignPtr = currPtr + 8;
        alignedBlock->header = (newSize - shiftRight + THIS_BLOCK_ALLOCATED);
        currPtr += (alignedBlock->header - 8 - THIS_BLOCK_ALLOCATED);
        sf_footer *alignedBlockF = (sf_footer *)currPtr;
        *alignedBlockF = alignedBlock->header;

        // Free newPtr - 8
        sf_free(newPtr);
        void *retPtr = sf_realloc(alignPtr, size);
        return retPtr;
    }
    else if(shiftRight == 0) {
        void *retPtr = sf_realloc(newPtr, size);
        return retPtr;
    }
    return NULL;
}
