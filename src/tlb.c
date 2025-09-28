#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"
#include "clock.h"

typedef struct {
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

// L1 TLB structure and statistics
tlb_entry_t tlb_l1[TLB_L1_SIZE];
uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

// Getter functions for L1 TLB statistics
uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

// Initialize the TLB
void tlb_init() {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
}

// Invalidate TLB entries for a given virtual page number
void tlb_invalidate(va_t virtual_page_number) {
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
      break; // Only one entry per virtual page number should exist
    }
  }
}

// Translate virtual address to physical address using TLB
pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  // Extract virtual page number and offset from virtual address
  va_t virtual_page_number = virtual_address >> PAGE_SIZE_BITS;
  uint64_t offset = virtual_address & PAGE_OFFSET_MASK;
  
  uint64_t current_time = get_time();
  
  // Add L1 TLB lookup latency
  increment_time(TLB_L1_LATENCY_NS);
  
  // Check if the entry is in the L1 TLB
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      // TLB hit
      tlb_l1_hits++;
      tlb_l1[i].last_access = current_time;
      
      // Mark entry as dirty if this is a write operation
      if (op == OP_WRITE) {
        tlb_l1[i].dirty = true;
      }
      
      // Return the physical address
      return (tlb_l1[i].physical_page_number << PAGE_SIZE_BITS) | offset;
    }
  }
  
  // TLB miss
  tlb_l1_misses++;
  
  // Get the physical address from the page table
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;
  
  // Find the LRU entry to replace in the TLB
  int lru_index = 0;
  uint64_t oldest_access = tlb_l1[0].last_access;
  
  for (int i = 1; i < TLB_L1_SIZE; i++) {
    if (!tlb_l1[i].valid) {
      // Found an invalid entry, use it
      lru_index = i;
      break;
    }
    
    if (tlb_l1[i].last_access < oldest_access) {
      oldest_access = tlb_l1[i].last_access;
      lru_index = i;
    }
  }
  
  // Insert the new entry into the TLB
  tlb_l1[lru_index].valid = true;
  tlb_l1[lru_index].dirty = (op == OP_WRITE);
  tlb_l1[lru_index].last_access = current_time;
  tlb_l1[lru_index].virtual_page_number = virtual_page_number;
  tlb_l1[lru_index].physical_page_number = physical_page_number;
  
  return physical_address;
}
