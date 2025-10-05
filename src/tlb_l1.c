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

// Helper function to find LRU entry in TLB
int find_lru_entry(tlb_entry_t *tlb, int size) {
  int lru_idx = 0;
  uint64_t oldest_time = tlb[0].last_access;
  
  for (int i = 0; i < size; i++) {
    if (!tlb[i].valid) {
      return i; // Use invalid entry first
    }
    if (tlb[i].last_access < oldest_time) {
      oldest_time = tlb[i].last_access;
      lru_idx = i;
    }
  }
  return lru_idx;
}

// Helper function to evict and write back if dirty
void evict_entry(tlb_entry_t *entry, int index, const char *level) {
  if (entry->valid) {
    log_dbg("Evicting TLB %s entry i=%d VPN=%lx PPN=%lx valid=%d dirty=%d...\n", 
            level, index, entry->virtual_page_number, entry->physical_page_number, 
            entry->valid, entry->dirty);
    
    if (entry->dirty) {
      write_back_tlb_entry(entry->physical_page_number << PAGE_SIZE_BITS);
    }
  }
  entry->valid = false;
}

void tlb_invalidate(va_t virtual_page_number) {
  // Invalidate in L1 TLB
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      evict_entry(&tlb_l1[i], i, "L1");
      tlb_l1_invalidations++;
      log_dbg("Invalidating TLB L1 entry i=%d VPN=%lx\n", i, virtual_page_number);
      break;
    }
  }

  // Invalidate in L2 TLB
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
      evict_entry(&tlb_l2[i], i, "L2");
      tlb_l2_invalidations++;
      log_dbg("Invalidating TLB L2 entry i=%d VPN=%lx\n", i, virtual_page_number);
      break;
    }
  }
}

// Helper function to search TLB for a virtual page
int tlb_lookup(tlb_entry_t *tlb, int size, va_t vpn) {
  for (int i = 0; i < size; i++) {
    if (tlb[i].valid && tlb[i].virtual_page_number == vpn) {
      return i;
    }
  }
  return -1; // Not found
}

// Helper function to insert entry into TLB
void tlb_insert(tlb_entry_t *tlb, int size, int idx, va_t vpn, pa_dram_t ppn, op_t op, time_ns_t time) {
  tlb[idx].valid = true;
  tlb[idx].dirty = (op == OP_WRITE);
  tlb[idx].last_access = time;
  tlb[idx].virtual_page_number = vpn;
  tlb[idx].physical_page_number = ppn;
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  va_t vpn = virtual_address >> PAGE_SIZE_BITS;
  va_t page_offset = virtual_address & PAGE_OFFSET_MASK;
  // Try L1 TLB first
  int l1_idx = tlb_lookup(tlb_l1, TLB_L1_SIZE, vpn);
  if (l1_idx >= 0) {
    // L1 hit
    tlb_l1_hits++;
    tlb_l1[l1_idx].last_access = get_time();
    if (op == OP_WRITE) {
      tlb_l1[l1_idx].dirty = true;
    }
    increment_time(TLB_L1_LATENCY_NS);
    log_dbg("TLB L1 hit i=%d VPN=%lx PPN=%lx\n", l1_idx, vpn, tlb_l1[l1_idx].physical_page_number);
    return (tlb_l1[l1_idx].physical_page_number << PAGE_SIZE_BITS) | page_offset;
  }

  // L1 miss - go directly to page table (ignoring L2 for L1 tests)
  tlb_l1_misses++;
  increment_time(TLB_L1_LATENCY_NS);
  
  pa_dram_t physical_address = page_table_translate(virtual_address, op);
  pa_dram_t ppn = physical_address >> PAGE_SIZE_BITS;

  log_dbg("PTE found (VA=%lx VPN=%lx PA=%lx)\n", virtual_address, vpn, ppn);

  // Insert into L1 only
  int lru_l1 = find_lru_entry(tlb_l1, TLB_L1_SIZE);
  evict_entry(&tlb_l1[lru_l1], lru_l1, "L1");
  tlb_insert(tlb_l1, TLB_L1_SIZE, lru_l1, vpn, ppn, op, get_time());

  return physical_address;
}