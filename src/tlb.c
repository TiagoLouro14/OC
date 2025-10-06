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

// Search L1 TLB for a virtual page
// return -1 if not found
int tlb_l1_lookup(tlb_entry_t *tlb_l1, va_t virtual_page_number) {
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      return i;
    }
  }
  return -1; 
}

// Search L2 TLB for a virtual page
// return -1 if not found
int tlb_l2_lookup(tlb_entry_t *tlb_l2, va_t virtual_page_number) {
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
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

// Handle L1 TLB hit
pa_dram_t handle_l1_hit(int l1_idx, op_t op, va_t page_offset) {
  tlb_l1_hits++;
  increment_time(TLB_L1_LATENCY_NS);

  tlb_l1[l1_idx].last_access = get_time();
  if (op == OP_WRITE) {
    tlb_l1[l1_idx].dirty = true;
  }
  return (tlb_l1[l1_idx].physical_page_number << PAGE_SIZE_BITS) | page_offset;
}

// Handle L2 TLB hit
pa_dram_t handle_l2_hit(int l2_idx, op_t op, va_t page_offset) {
  tlb_l2_hits++;
  increment_time(TLB_L2_LATENCY_NS);

  // Move entry from L2 to L1 
  tlb_entry_t l2_entry = tlb_l2[l2_idx];
  tlb_l2[l2_idx].valid = false;
  l2_entry.last_access = get_time();
  if (op == OP_WRITE) {
    l2_entry.dirty = true;
  }
  int lru_l1 = find_lru_entry(tlb_l1, TLB_L1_SIZE);
  
  if (tlb_l1[lru_l1].valid) { // If L1 entry being evicted is valid, move it to L2
    int lru_l2 = find_lru_entry(tlb_l2, TLB_L2_SIZE);
    evict_entry(&tlb_l2[lru_l2]); 
    tlb_l2[lru_l2] = tlb_l1[lru_l1]; 
  }
  
  tlb_l1[lru_l1] = l2_entry; // Insert L2 entry into L1
  
  return (l2_entry.physical_page_number << PAGE_SIZE_BITS) | page_offset;
}

// Handle TLB miss - insert new entry from page table
pa_dram_t handle_tlb_miss(va_t virtual_address, va_t virtual_page_number, op_t op) {
  tlb_l2_misses++;
  increment_time(TLB_L2_LATENCY_NS);
  
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t physical_page_number = physical_address >> PAGE_SIZE_BITS;

  // Insert into L1
  int lru_l1 = find_lru_entry(tlb_l1, TLB_L1_SIZE);
  
  if (tlb_l1[lru_l1].valid) { // If L1 entry being evicted is valid, move it to L2
    int lru_l2 = find_lru_entry(tlb_l2, TLB_L2_SIZE);
    evict_entry(&tlb_l2[lru_l2]); 
    tlb_l2[lru_l2] = tlb_l1[lru_l1]; 
  }
  
  tlb_insert(tlb_l1, lru_l1, virtual_page_number, physical_page_number, op, get_time());
  return physical_address;
}

void tlb_invalidate(va_t virtual_page_number) {
  // Invalidate in L1
  increment_time(TLB_L1_LATENCY_NS);
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      evict_entry(&tlb_l1[i]);
      tlb_l1_invalidations++;
      return;
    }
  }
  
  // If not in L1, check L2
  increment_time(TLB_L2_LATENCY_NS);
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
      evict_entry(&tlb_l2[i]);
      tlb_l2_invalidations++;
      return;
    }
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  va_t virtual_page_number = virtual_address >> PAGE_SIZE_BITS;
  va_t page_offset = virtual_address & PAGE_OFFSET_MASK;
  
  // Try L1 TLB
  int l1_idx = tlb_l1_lookup(tlb_l1, virtual_page_number);
  if (l1_idx >= 0) {
    return handle_l1_hit(l1_idx, op, page_offset);
  }

  // L1 miss
  tlb_l1_misses++;
  increment_time(TLB_L1_LATENCY_NS);
  
  // Try L2 TLB
  int l2_idx = tlb_l2_lookup(tlb_l2, virtual_page_number);
  if (l2_idx >= 0) { 
    return handle_l2_hit(l2_idx, op, page_offset);
  }

  // L2 miss - go to page table
  return handle_tlb_miss(virtual_address, virtual_page_number, op);
}