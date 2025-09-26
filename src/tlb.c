#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

// Function declaration for get_clock
uint64_t get_clock(void);

typedef struct {
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

tlb_entry_t tlb_l1[TLB_L1_SIZE];
tlb_entry_t tlb_l2[TLB_L2_SIZE];

uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t tlb_l2_hits = 0;
uint64_t tlb_l2_misses = 0;
uint64_t tlb_l2_invalidations = 0;

uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses() { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations() { return tlb_l2_invalidations; }

void tlb_init() {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
  tlb_l2_hits = 0;
  tlb_l2_misses = 0;
  tlb_l2_invalidations = 0;
}

void tlb_invalidate(va_t virtual_page_number) {
  // Invalidate entries in L1 TLB
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
      break; // Only one entry per virtual page number should exist
    }
  }
  
  // Invalidate entries in L2 TLB
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
      tlb_l2[i].valid = false;
      tlb_l2_invalidations++;
      break; // Only one entry per virtual page number should exist
    }
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  // Extract virtual page number and offset from virtual address
  va_t virtual_page_number = virtual_address >> PAGE_SIZE_BITS;
  uint64_t offset = virtual_address & PAGE_OFFSET_MASK;
  
  uint64_t current_time = get_clock();
  
  // First, search in L1 TLB
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      // L1 TLB hit
      tlb_l1_hits++;
      tlb_l1[i].last_access = current_time;
      
      // Mark as dirty if it's a write operation
      if (op == OP_WRITE) {
        tlb_l1[i].dirty = true;
      }
      
      // Return physical address (physical page number + offset)
      return (tlb_l1[i].physical_page_number << PAGE_SIZE_BITS) | offset;
    }
  }
  
  // L1 TLB miss, search in L2 TLB
  tlb_l1_misses++;
  
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
      // L2 TLB hit
      tlb_l2_hits++;
      tlb_l2[i].last_access = current_time;
      
      // Mark as dirty if it's a write operation
      if (op == OP_WRITE) {
        tlb_l2[i].dirty = true;
      }
      
      // Promote to L1 TLB (find LRU entry to replace)
      int lru_index = 0;
      uint64_t oldest_time = tlb_l1[0].last_access;
      
      for (int j = 1; j < TLB_L1_SIZE; j++) {
        if (!tlb_l1[j].valid) {
          // Found invalid entry, use it
          lru_index = j;
          break;
        }
        if (tlb_l1[j].last_access < oldest_time) {
          oldest_time = tlb_l1[j].last_access;
          lru_index = j;
        }
      }
      
      // Copy entry from L2 to L1
      tlb_l1[lru_index].valid = true;
      tlb_l1[lru_index].dirty = tlb_l2[i].dirty;
      tlb_l1[lru_index].last_access = current_time;
      tlb_l1[lru_index].virtual_page_number = virtual_page_number;
      tlb_l1[lru_index].physical_page_number = tlb_l2[i].physical_page_number;
      
      // Return physical address
      return (tlb_l2[i].physical_page_number << PAGE_SIZE_BITS) | offset;
    }
  }
  
  // L2 TLB miss - need to access page table
  tlb_l2_misses++;
  
  // Get physical address from page table
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;
  
  // Add entry to L2 TLB (find LRU entry to replace)
  int lru_index = 0;
  uint64_t oldest_time = tlb_l2[0].last_access;
  
  for (int i = 1; i < TLB_L2_SIZE; i++) {
    if (!tlb_l2[i].valid) {
      // Found invalid entry, use it
      lru_index = i;
      break;
    }
    if (tlb_l2[i].last_access < oldest_time) {
      oldest_time = tlb_l2[i].last_access;
      lru_index = i;
    }
  }
  
  // Insert new entry into L2 TLB
  tlb_l2[lru_index].valid = true;
  tlb_l2[lru_index].dirty = (op == OP_WRITE);
  tlb_l2[lru_index].last_access = current_time;
  tlb_l2[lru_index].virtual_page_number = virtual_page_number;
  tlb_l2[lru_index].physical_page_number = physical_page_number;
  
  return physical_address;
}
