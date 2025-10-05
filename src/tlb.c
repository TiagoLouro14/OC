#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

typedef struct {
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

tlb_entry_t tlb_l1[TLB_L1_SIZE];

uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return 0; }
uint64_t get_total_tlb_l2_misses() { return 0; }
uint64_t get_total_tlb_l2_invalidations() { return 0; }

void tlb_init() {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  tlb_l1_hits = 0;
  tlb_l1_misses = 0;
  tlb_l1_invalidations = 0;
}

// Find LRU entry in TLB
// return index
int find_lru_entry(tlb_entry_t *tlb_l1, int size) {
  int lru_idx = 0;
  uint64_t oldest_time = tlb_l1[0].last_access;
  
  for (int i = 0; i < size; i++) {
    if (!tlb_l1[i].valid) {
      return i; // invalid entry first
    }
    if (tlb_l1[i].last_access < oldest_time) {
      oldest_time = tlb_l1[i].last_access;
      lru_idx = i;
    }
  }
  return lru_idx;
}

// Evict and write back if dirty
void evict_entry(tlb_entry_t *entry) {
  if (entry->valid && entry->dirty) {
    write_back_tlb_entry(entry->physical_page_number << PAGE_SIZE_BITS);
  }
  entry->valid = false;
}

void tlb_invalidate(va_t virtual_page_number) {
  // Invalidate in L1 TLB
  increment_time(TLB_L1_LATENCY_NS);
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      evict_entry(&tlb_l1[i]);
      tlb_l1_invalidations++;
      break;
    }
  }
}

// Search TLB for a virtual page
// return -1 if not found
int tlb_l1_lookup(tlb_entry_t *tlb_l1, va_t virtual_page_number) {
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      return i;
    }
  }
  return -1; 
}

// Insert entry into TLB
void tlb_insert(tlb_entry_t *tlb, int idx, va_t virtual_page_number, pa_dram_t ppn, op_t op, time_ns_t time) {
  tlb[idx].valid = true;
  tlb[idx].dirty = (op == OP_WRITE);
  tlb[idx].last_access = time;
  tlb[idx].virtual_page_number = virtual_page_number;
  tlb[idx].physical_page_number = ppn;
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  va_t virtual_page_number = virtual_address >> PAGE_SIZE_BITS;
  va_t page_offset = virtual_address & PAGE_OFFSET_MASK;
  
  // Try L1 TLB first
  int l1_idx = tlb_l1_lookup(tlb_l1, virtual_page_number);
  if (l1_idx >= 0) { // L1 hit
    tlb_l1_hits++;
    tlb_l1[l1_idx].last_access = get_time();
    if (op == OP_WRITE) {
      tlb_l1[l1_idx].dirty = true;
    }
    increment_time(TLB_L1_LATENCY_NS);
    return (tlb_l1[l1_idx].physical_page_number << PAGE_SIZE_BITS) | page_offset;
  }

  // L1 miss - go to page table
  tlb_l1_misses++;
  increment_time(TLB_L1_LATENCY_NS);
  
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;

  // Insert into L1 only
  int lru_l1 = find_lru_entry(tlb_l1, TLB_L1_SIZE);
  evict_entry(&tlb_l1[lru_l1]);
  tlb_insert(tlb_l1, lru_l1, virtual_page_number, physical_page_number, op, get_time());

  return physical_address;
}