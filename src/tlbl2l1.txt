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

static tlb_entry_t l1[TLB_L1_SIZE], l2[TLB_L2_SIZE];
static uint64_t l1_hits=0, l1_misses=0, l1_inv=0;
static uint64_t l2_hits=0, l2_misses=0, l2_inv=0;

uint64_t get_total_tlb_l1_hits(void)          { return l1_hits; }
uint64_t get_total_tlb_l1_misses(void)        { return l1_misses; }
uint64_t get_total_tlb_l1_invalidations(void) { return l1_inv; }
uint64_t get_total_tlb_l2_hits(void)          { return l2_hits; }
uint64_t get_total_tlb_l2_misses(void)        { return l2_misses; }
uint64_t get_total_tlb_l2_invalidations(void) { return l2_inv; }

static inline void touch_l1(int i){ l1[i].last_access = get_time(); }
static inline void touch_l2(int i){ l2[i].last_access = get_time(); }

static int lookup_l1(va_t vpn){
  for(int i=0;i<TLB_L1_SIZE;i++)
    if(l1[i].valid && l1[i].vpn==vpn) return i;
  return -1;
}
static int lookup_l2(va_t vpn){
  for(int i=0;i<TLB_L2_SIZE;i++)
    if(l2[i].valid && l2[i].vpn==vpn) return i;
  return -1;
}

static int pick_lru_l1(void){
  int lru=0; uint64_t oldest=l1[0].last_access;
  for(int i=0;i<TLB_L1_SIZE;i++){
    if(!l1[i].valid) return i;
    if(l1[i].last_access<oldest){ oldest=l1[i].last_access; lru=i; }
  }
  return lru;
}
static int pick_lru_l2(void){
  int lru=0; uint64_t oldest=l2[0].last_access;
  for(int i=0;i<TLB_L2_SIZE;i++){
    if(!l2[i].valid) return i;
    if(l2[i].last_access<oldest){ oldest=l2[i].last_access; lru=i; }
  }
  return lru;
}

// --- write-back helper: se a entrada L2 a expulsar for dirty, escreve-a ---
static inline void l2_writeback_if_needed(int i2){
  if(l2[i2].valid && l2[i2].dirty){
    // escrever a página mapeada por esta tradução (PPN<<PAGE_SIZE_BITS)
    write_back_tlb_entry(l2[i2].ppn << PAGE_SIZE_BITS);
    // Nota: write_back_tlb_entry() já simula a latência via dram_access()
    // Depois do write-back, podemos limpar o dirty (vai ser reescrita já de seguida)
    l2[i2].dirty = false;
  }
}

// Victim L1 → L2 (inclusivo). Pode expulsar alguém da L2; se dirty, faz write-back.
static void push_victim_to_l2(int i1){
  int j = lookup_l2(l1[i1].vpn);
  if(j>=0){
    // refresh inclusivo
    l2[j].dirty |= l1[i1].dirty;
    l2[j].ppn    = l1[i1].ppn;
    touch_l2(j);
    return;
  }
  int k = pick_lru_l2();
  if(l2[k].valid){
    l2_writeback_if_needed(k);
  }
  l2[k] = (tlb_entry_t){
    .valid=true, .dirty=l1[i1].dirty, .last_access=get_time(),
    .vpn=l1[i1].vpn, .ppn=l1[i1].ppn
  };
}

// Inserção/atualização em L1 (evita duplicados; pode empurrar vítima para L2)
static int insert_l1(va_t vpn, pa_dram_t ppn, bool dirty){
  int hit = lookup_l1(vpn);
  if(hit>=0){
    l1[hit].ppn   = ppn;
    l1[hit].dirty |= dirty;
    touch_l1(hit);
    return hit;
  }
  int i = pick_lru_l1();
  if(l1[i].valid){
    push_victim_to_l2(i); // esta chamada já trata de eventual write-back em L2
  }
  l1[i] = (tlb_entry_t){
    .valid=true, .dirty=dirty, .last_access=get_time(), .vpn=vpn, .ppn=ppn
  };
  return i;
}

// Inserção/atualização em L2 (inclusivo). Se expulsar dirty, write-back.
static int insert_l2(va_t vpn, pa_dram_t ppn, bool dirty){
  int hit = lookup_l2(vpn);
  if(hit>=0){
    l2[hit].ppn   = ppn;
    l2[hit].dirty |= dirty;
    touch_l2(hit);
    return hit;
  }
  int i = pick_lru_l2();
  if(l2[i].valid){
    l2_writeback_if_needed(i);
  }
  l2[i] = (tlb_entry_t){
    .valid=true, .dirty=dirty, .last_access=get_time(), .vpn=vpn, .ppn=ppn
  };
  return i;
}

void tlb_init(void){
  memset(l1,0,sizeof(l1));
  memset(l2,0,sizeof(l2));
  l1_hits=l1_misses=l1_inv=0;
  l2_hits=l2_misses=l2_inv=0;
}

void tlb_invalidate(va_t vpn){
  // invalida TODAS as ocorrências (evita entradas "fantasma")
  for(int i=0;i<TLB_L1_SIZE;i++)
    if(l1[i].valid && l1[i].vpn==vpn){ l1[i].valid=false; l1_inv++; }
  for(int i=0;i<TLB_L2_SIZE;i++)
    if(l2[i].valid && l2[i].vpn==vpn){ l2[i].valid=false; l2_inv++; }
}


pa_dram_t tlb_translate(va_t va, op_t op){
  va_t vpn = va >> PAGE_SIZE_BITS;
  uint64_t off = va & PAGE_OFFSET_MASK;

  // L1 lookup
  int i1 = lookup_l1(vpn);
  increment_time(TLB_L1_LATENCY_NS);
  if(i1>=0){
    l1_hits++;
    if(op==OP_WRITE) l1[i1].dirty = true;
    touch_l1(i1);
    return (l1[i1].ppn<<PAGE_SIZE_BITS) | off;
  }

  // L2 lookup
  l1_misses++;
  int i2 = lookup_l2(vpn);
  increment_time(TLB_L2_LATENCY_NS);
  if(i2>=0){
    l2_hits++;
    pa_dram_t ppn = l2[i2].ppn;
    bool dirty = l2[i2].dirty || (op==OP_WRITE);
    insert_l1(vpn, ppn, dirty); // promove p/ L1 (L2 continua com cópia)
    touch_l2(i2);
    return (ppn<<PAGE_SIZE_BITS) | off;
  }
  l2_misses++;

  // Miss total → Page Table (latências tratadas em page_table/memory)
  
  // Aplicar invalidação para todas as páginas - isso garante que tanto
  // para páginas dirty quanto clean, a TLB seja invalidada corretamente
  tlb_invalidate(vpn);
  
  pa_dram_t pa  = page_table_translate(va, op);
  pa_dram_t ppn = pa >> PAGE_SIZE_BITS;

  insert_l1(vpn, ppn, (op==OP_WRITE));
  insert_l2(vpn, ppn, (op==OP_WRITE));
  return pa;
}