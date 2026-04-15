// Copyright (c) 2026
// Temporary PoS transition shim: keep rx_* symbols available without RandomX.

#include <stddef.h>
#include <stdint.h>

#include "hash-ops.h"

static uint32_t g_rx_miner_thread = 0;

uint64_t rx_seedheight(const uint64_t height)
{
  return height;
}

void rx_seedheights(const uint64_t height, uint64_t *seedheight, uint64_t *nextheight)
{
  if (seedheight) *seedheight = height;
  if (nextheight) *nextheight = height + 1;
}

void rx_set_main_seedhash(const char *seedhash, size_t max_dataset_init_threads)
{
  (void)seedhash;
  (void)max_dataset_init_threads;
}

void rx_slow_hash(const char *seedhash, const void *data, size_t length, char *result_hash)
{
  (void)seedhash;
  cn_fast_hash(data, length, result_hash);
}

void rx_set_miner_thread(uint32_t value, size_t max_dataset_init_threads)
{
  (void)max_dataset_init_threads;
  g_rx_miner_thread = value;
}

uint32_t rx_get_miner_thread(void)
{
  return g_rx_miner_thread;
}

void rx_slow_hash_allocate_state(void) {}
void rx_slow_hash_free_state(void) {}
