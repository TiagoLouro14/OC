// tlb.c — L1+L2 inclusivo, com write-back nas expulsões dirty
#include "tlb.h"

#include <stdbool.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "memory.h"
#include "page_table.h"
#include "log.h"

typedef struct {
  bool      valid, dirty;
  uint64_t  last_access;
  va_t      vpn;   // virtual page number (tag)
  pa_dram_t ppn;   // physical page number (DRAM)
} tlb_entry_t;

// Initializing both L1 and L2 TLBs
static tlb_entry_t l1[TLB_L1_SIZE], l2[TLB_L2_SIZE];
static uint64_t l1_hits = 0, l1_misses = 0, l1_inv = 0;
static uint64_t l2_hits = 0, l2_misses = 0, l2_inv = 0;

// L1 getters
uint64_t get_total_tlb_l1_hits(void)          { return l1_hits; }
uint64_t get_total_tlb_l1_misses(void)        { return l1_misses; }
uint64_t get_total_tlb_l1_invalidations(void) { return l1_inv; }

// L2 getters
uint64_t get_total_tlb_l2_hits(void)          { return l2_hits; }
uint64_t get_total_tlb_l2_misses(void)        { return l2_misses; }
uint64_t get_total_tlb_l2_invalidations(void) { return l2_inv; }

// Function to update last_access
static inline void touch_l1(int i){ l1[i].last_access = get_time(); }
static inline void touch_l2(int i){ l2[i].last_access = get_time(); }

/**
 * lookup_l1 - searches the L1 TLB for a specific virtual adress
 *
 * @param vpn: the specific virtual adress to look for
 *
 * @return: the index for vpn in the L1 TLB or -1 if not found
 */
static int lookup_l1(va_t vpn){
  for(int i = 0; i < TLB_L1_SIZE; i++)
    if(l1[i].valid && l1[i].vpn == vpn) return i;
  return -1;
}

/**
 * lookup_l2 - searches the L2 TLB for a specific virtual adress
 *
 * @param vpn: the specific virtual adress to look for
 *
 * @return the index for vpn in the L2 TLB or -1 if not found
 */
static int lookup_l2(va_t vpn){
  for(int i = 0; i < TLB_L2_SIZE; i++)
    if(l2[i].valid && l2[i].vpn == vpn) return i;
  return -1;
}

/**
 * pick_lru_l1 - finds the least recently used entry in the L1 TLB
 *
 * @return index for the lru entry
 */
static int pick_lru_l1(void){
  int lru = 0; 
  uint64_t oldest = l1[0].last_access;

  for(int i = 0; i < TLB_L1_SIZE; i++){
    if(!l1[i].valid) return i;
    if(l1[i].last_access < oldest){ 
      oldest = l1[i].last_access; 
      lru = i; 
    }
  }
  return lru;
}

/**
 * pick_lru_l2 - finds an empty or least recently used entry in the L2 TLB
 *
 * @return index for the lru entry
 */
static int pick_lru_l2(void){
  int lru = 0; 
  uint64_t oldest = l2[0].last_access;

  for(int i = 0; i < TLB_L2_SIZE; i++){
    if(!l2[i].valid) return i;
    if(l2[i].last_access < oldest){ 
      oldest = l2[i].last_access;
      lru = i; 
    }
  }
  return lru;
}

/**
 * l2_writeback_if_needed - writes the contents of an index in the TLB back to main memory
 * if dirty = 1
 *
 * @param index_l2: index to write back
 */
static inline void l2_writeback_if_needed(int i2){
  if(l2[i2].valid && l2[i2].dirty){
    // escrever a página mapeada por esta tradução (PPN<<PAGE_SIZE_BITS)
    write_back_tlb_entry(l2[i2].ppn << PAGE_SIZE_BITS);
    // Nota: write_back_tlb_entry() já simula a latência via dram_access()
    // Depois do write-back, podemos limpar o dirty (vai ser reescrita já de seguida)
    l2[i2].dirty = false;
  }
}

/**
 * push_victim_to_l2 - Copies an entry from the L1 TLB to the L2 TLB.
 * If the entry is already in use, removes it and writes back, if necessary.
 *
 * @param index_l1 index in L1 TLB
 */
static void push_victim_to_l2(int i1){
  int j = lookup_l2(l1[i1].vpn);
  if(j >= 0){
    // refresh inclusivo
    l2[j].dirty |= l1[i1].dirty;
    l2[j].ppn = l1[i1].ppn;
    touch_l2(j);
    return;
  }

  int k = pick_lru_l2();
  if(l2[k].valid){
    l2_writeback_if_needed(k);
  }
  
  l2[k] = (tlb_entry_t){
    .valid = true, 
    .dirty = l1[i1].dirty, 
    .last_access = get_time(),
    .vpn = l1[i1].vpn, 
    .ppn = l1[i1].ppn
  };
}

/**
 * insert_l1 - writes an entry to the L1 TLB and pushes the old entry to L2, if applicable.
 * If the entry already exists, simply updates it's contents.
 *
 * @param vpn virtual page number
 * @param ppn physical page number
 * @param dirty dirty bit
 *
 * @return index where the entry was placed
 */
static int insert_l1(va_t vpn, pa_dram_t ppn, bool dirty){
  int hit = lookup_l1(vpn);
  if(hit >= 0){
    l1[hit].ppn = ppn;
    l1[hit].dirty |= dirty;
    touch_l1(hit);
    return hit;
  }

  int i = pick_lru_l1();
  if(l1[i].valid){
    push_victim_to_l2(i); // esta chamada já trata de eventual write-back em L2
  }
  
  l1[i] = (tlb_entry_t){
    .valid = true, 
    .dirty = dirty,
    .last_access = get_time(), 
    .vpn = vpn, 
    .ppn = ppn
  };
  return i;
}

/**
 * insert_l2 - writes an entry to the L2 TLB and pushes the old entry to main memory, if applicable.
 * If the entry already exists, simply updates it's contents.
 *
 * @param vpn virtual page number
 * @param ppn physical page number
 * @param dirty dirty bit
 *
 * @return index where the entry was placed
 */
static int insert_l2(va_t vpn, pa_dram_t ppn, bool dirty){
  int hit = lookup_l2(vpn);
  if(hit >= 0){
    l2[hit].ppn = ppn;
    l2[hit].dirty |= dirty;
    touch_l2(hit);
    return hit;
  }
  int i = pick_lru_l2();
  if(l2[i].valid){
    l2_writeback_if_needed(i);
  }
  l2[i] = (tlb_entry_t){
    .valid = true, 
    .dirty = dirty, 
    .last_access = get_time(), 
    .vpn = vpn, 
    .ppn = ppn
  };
  return i;
}

/**
 * tlb_init - function to initialize both L1 and L2 TLBs.
 */
void tlb_init(void){
  memset(l1, 0, sizeof(l1));
  memset(l2, 0, sizeof(l2));
  l1_hits = l1_misses = l1_inv = 0;
  l2_hits = l2_misses = l2_inv = 0;
}

/**
 * tlb_invalidate - sets all occurences both in L1 and L2 of a certain virtual adress to invalid
 *
 * @param vpn virtual adress to invalidate
 */
void tlb_invalidate(va_t vpn){
  // invalida TODAS as ocorrências (evita entradas "fantasma")
  for(int i = 0; i < TLB_L1_SIZE; i++){
    if(l1[i].valid && l1[i].vpn == vpn){ 
      l1[i].valid = false; 
      l1_inv++; 
    }
  }

  for(int i = 0; i < TLB_L2_SIZE; i++){
    if(l2[i].valid && l2[i].vpn == vpn){
      l2[i].valid = false;
      l2_inv++;
    }
  }
}

/**
 * tlb_translate - checks through L1 and L2 TLBs to check for a hit. If it's a miss, handles it appropriately
 * and puts the new entry in the necessary TLB(s). Finnaly, returns the physical adress corresponding to
 * the virtual adress.
 *
 * @param va virtual adress
 * @param op operation to perform (read or write)
 *
 * @return the physical adress corresponding to va
 */
pa_dram_t tlb_translate(va_t va, op_t op){
  va_t vpn = va >> PAGE_SIZE_BITS;
  uint64_t off = va & PAGE_OFFSET_MASK;

  // L1 lookup
  int i1 = lookup_l1(vpn);
  increment_time(TLB_L1_LATENCY_NS);
  if(i1 >= 0){
    l1_hits++;
    if(op == OP_WRITE) l1[i1].dirty = true;
    touch_l1(i1);
    return (l1[i1].ppn << PAGE_SIZE_BITS) | off;
  }
  l1_misses++;

  // L2 lookup
  int i2 = lookup_l2(vpn);
  increment_time(TLB_L2_LATENCY_NS);
  if(i2 >= 0){
    l2_hits++;
    pa_dram_t ppn = l2[i2].ppn;
    bool dirty = l2[i2].dirty || (op==OP_WRITE);
    insert_l1(vpn, ppn, dirty); // promove p/ L1 (L2 continua com cópia)
    touch_l2(i2);
    return (ppn << PAGE_SIZE_BITS) | off;
  }
  l2_misses++;

  // Miss total → Page Table (latências tratadas em page_table/memory)
  
  // Aplicar invalidação para todas as páginas - isso garante que tanto
  // para páginas dirty quanto clean, a TLB seja invalidada corretamente
  tlb_invalidate(vpn);
  
  pa_dram_t pa  = page_table_translate(va, op);
  pa_dram_t ppn = pa >> PAGE_SIZE_BITS;

  insert_l1(vpn, ppn, (op == OP_WRITE));
  insert_l2(vpn, ppn, (op == OP_WRITE));
  return pa;
}