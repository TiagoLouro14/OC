// tlb.c
#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

// -----------------------------
//   MODO DE OPERAÇÃO DA TLB
// -----------------------------
#define TLB_MODE_L1_ONLY 1
#define TLB_MODE_L1_L2   2

#ifndef TLB_ACTIVE_MODE
#define TLB_ACTIVE_MODE TLB_MODE_L1_L2
#endif

#ifndef PAGE_TABLE_LATENCY_NS
#define PAGE_TABLE_LATENCY_NS 32
#endif

typedef struct {
  bool valid;
  bool dirty;                       // só livro de registo (não gera latência na TLB)
  uint64_t last_access;             // para LRU
  va_t     virtual_page_number;     // "tag"
  pa_dram_t physical_page_number;   // PPN
} tlb_entry_t;

// Tabelas
static tlb_entry_t tlb_l1[TLB_L1_SIZE];
static tlb_entry_t tlb_l2[TLB_L2_SIZE];

// Contadores
static uint64_t tlb_l1_hits = 0, tlb_l1_misses = 0, tlb_l1_invalidations = 0;
static uint64_t tlb_l2_hits = 0, tlb_l2_misses = 0, tlb_l2_invalidations = 0;

// Getters (usados pelo main)
uint64_t get_total_tlb_l1_hits(void)           { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses(void)         { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations(void)  { return tlb_l1_invalidations; }
uint64_t get_total_tlb_l2_hits(void)           { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses(void)         { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations(void)  { return tlb_l2_invalidations; }

// -----------------------------
//        Helpers LRU
// -----------------------------
static inline void lru_touch_l1(int idx) { tlb_l1[idx].last_access = get_time(); }
static inline void lru_touch_l2(int idx) {
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_ONLY) return;
  tlb_l2[idx].last_access = get_time();
}

static int lru_pick_l1(void) {
  // devolve um índice inválido primeiro (free slot), senão o LRU
  int lru = 0;
  uint64_t oldest = tlb_l1[0].last_access;
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (!tlb_l1[i].valid) return i;
    if (tlb_l1[i].last_access < oldest) {
      oldest = tlb_l1[i].last_access;
      lru = i;
    }
  }
  return lru;
}

static int lru_pick_l2(void) {
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_ONLY) return -1;
  int lru = 0;
  uint64_t oldest = tlb_l2[0].last_access;
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (!tlb_l2[i].valid) return i;
    if (tlb_l2[i].last_access < oldest) {
      oldest = tlb_l2[i].last_access;
      lru = i;
    }
  }
  return lru;
}

// procura em L1; devolve índice ou -1
static int l1_lookup(va_t vpn) {
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == vpn) return i;
  }
  return -1;
}

// procura em L2; devolve índice ou -1
static int l2_lookup(va_t vpn) {
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_ONLY) return -1;
  for (int i = 0; i < TLB_L2_SIZE; i++) {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == vpn) return i;
  }
  return -1;
}

// move uma entrada (já válida) de L1 → L2 (victim) sem custos
static void l1_victim_to_l2(int l1_idx) {
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_ONLY) return;

  // Se já existir na L2, apenas atualiza "dirty" e LRU
  int hit2 = l2_lookup(tlb_l1[l1_idx].virtual_page_number);
  if (hit2 >= 0) {
    tlb_l2[hit2].dirty |= tlb_l1[l1_idx].dirty;
    lru_touch_l2(hit2);
    return;
  }

  // Coloca na L2 (evict se necessário), sem cobrar latências
  int i2 = lru_pick_l2();
  tlb_l2[i2].valid = true;
  tlb_l2[i2].dirty = tlb_l1[l1_idx].dirty;
  tlb_l2[i2].virtual_page_number   = tlb_l1[l1_idx].virtual_page_number;
  tlb_l2[i2].physical_page_number  = tlb_l1[l1_idx].physical_page_number;
  lru_touch_l2(i2);
}

// insere/atualiza uma entrada na L1 (pode evictar alguém para L2)
static int l1_insert(va_t vpn, pa_dram_t ppn, bool dirty) {
  int idx = lru_pick_l1();

  // Se o slot escolhido estava ocupado, envia vítima para L2
  if (tlb_l1[idx].valid) {
    l1_victim_to_l2(idx); // sem custos, TLB não escreve memória
  }

  tlb_l1[idx].valid = true;
  tlb_l1[idx].dirty = dirty;
  tlb_l1[idx].virtual_page_number  = vpn;
  tlb_l1[idx].physical_page_number = ppn;
  lru_touch_l1(idx);
  return idx;
}

// -----------------------------
//        API pública
// -----------------------------
void tlb_init(void) {
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = tlb_l1_misses = tlb_l1_invalidations = 0;
  tlb_l2_hits = tlb_l2_misses = tlb_l2_invalidations = 0;
}

void tlb_invalidate(va_t virtual_page_number) {
  // invalida em L1
  for (int i = 0; i < TLB_L1_SIZE; i++) {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number) {
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
      break;
    }
  }
  // invalida em L2 (se existir)
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_L2) {
    for (int i = 0; i < TLB_L2_SIZE; i++) {
      if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number) {
        tlb_l2[i].valid = false;
        tlb_l2_invalidations++;
        break;
      }
    }
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op) {
  const va_t vpn   = virtual_address >> PAGE_SIZE_BITS;
  const uint64_t off = virtual_address & PAGE_OFFSET_MASK;

  // ---------- L1 lookup ----------
  int i1 = l1_lookup(vpn);
  increment_time(TLB_L1_LATENCY_NS); // custo do acesso à L1 (hit ou miss)

  if (i1 >= 0) {
    // L1 HIT
    tlb_l1_hits++;
    if (op == OP_WRITE) tlb_l1[i1].dirty = true;
    lru_touch_l1(i1);
    return (tlb_l1[i1].physical_page_number << PAGE_SIZE_BITS) | off;
  }

  // L1 MISS
  tlb_l1_misses++;

  // ---------- L2 lookup (se ativo) ----------
  if (TLB_ACTIVE_MODE == TLB_MODE_L1_L2) {
    int i2 = l2_lookup(vpn);
    increment_time(TLB_L2_LATENCY_NS); // custo da consulta à L2 (hit ou miss)

    if (i2 >= 0) {
    // L2 HIT → promover para L1
    tlb_l2_hits++;
    pa_dram_t ppn = tlb_l2[i2].physical_page_number;
    bool dirty = tlb_l2[i2].dirty || (op == OP_WRITE);

    // Inserir na L1 (pode empurrar vítima da L1 para L2)
    int new_i1 = l1_insert(vpn, ppn, dirty);
    (void)new_i1; // só para calar -Wunused-variable se não usarmos o índice

    // (Opcional) Se quiseres evitar duplicados L1/L2:
    // tlb_l2[i2].valid = false;

    lru_touch_l2(i2);

    return (ppn << PAGE_SIZE_BITS) | off;
  } else {
    // L2 MISS
    tlb_l2_misses++;
  }

  }

  // ---------- Miss total → Page Table ----------
  // A latência de memória/disco é simulada dentro de page_table_translate/memory.

  pa_dram_t phys_addr = page_table_translate(virtual_address, op);
  pa_dram_t ppn = phys_addr >> PAGE_SIZE_BITS;

  // Insere na L1. (Não insere diretamente na L2 — L2 é victim cache.)
  (void) l1_insert(vpn, ppn, (op == OP_WRITE));

  return phys_addr;
}
