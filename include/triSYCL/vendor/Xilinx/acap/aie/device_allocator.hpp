#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_DEVICE_ALLOCATOR_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_DEVICE_ALLOCATOR_HPP

/** \file

    contains hardware specific informations and linker scripts details of
    how the memory is used an partitioned
    TODO: One important optimization that could be done is adding a freelist.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include <cstddef>
#include <cstdint>

#include "hardware.hpp"
#include "log.hpp"

namespace trisycl::vendor::xilinx::acap {

/// This allocator is designed to minimize the memory overhead, to to be fast.
namespace heap {

constexpr unsigned min_alloc_size = 8;
/// Must be a power of 2;
constexpr unsigned alloc_align = 4;

/// metadata associated with each dynamic allocation.
/// 
struct block_header {
  /// TODO We could use the size to find the next block.
  /// TODO make this doubly linked.
  hw::stable_pointer<block_header> next;
  uint32_t size : 31;
  uint32_t in_use : 1;
#if defined(__SYCL_DEVICE_ONLY__)
  static block_header* get_header(void* ptr) {
    /// The block header is always just before the allocation in memory.
    return ((block_header*)ptr) - 1;
  }
  /// return a pointer to the section of memory this block header tracks.
  /// This region is just after the block_header.
  void* get_alloc() {
    return (void*)(this + 1);
  }
  block_header* get_next() {
    return next;
  }
  void* get_end() {
    return (void*)((char*)(this + 1) + size);
  }
  /// Check if the block is large enough to fit a block header plus some data.
  /// If not there is nothing to be gained by splitting the block.
  bool is_splitable(uint32_t new_size) {
    return size >= new_size + sizeof(block_header) + min_alloc_size;
  }
  /// resize the current block to new_size and create a block with the rest of the size.
  void split(uint32_t new_size) {
    block_header* old_next = get_next();
    uint32_t old_size = size;
    this->size = new_size;
    block_header* new_next = (block_header*)get_end();
    __builtin_memset(new_next, 0, sizeof(block_header));
    this->next = new_next;
    new_next->size = size - new_size - sizeof(block_header);
    new_next->next = old_next;
  }
#endif
};

struct allocator_global {
  hw::stable_pointer<block_header> total_list;
#if defined(__SYCL_DEVICE_ONLY__)
  static allocator_global* get() {
    return (allocator_global *)(hw::self_tile_addr(hw::get_parity_dev()) +
                                hw::heap_begin_offset);
  }
  static block_header *create_block(void *p, uint32_t s) {
    block_header *block = (block_header *)p;
    __builtin_memset(block, 0, sizeof(block_header));
    block->size = s - sizeof(block_header);
    return block;
  }
#endif
};

#if defined(__SYCL_DEVICE_ONLY__)

void init_allocator() {
  allocator_global *ag = allocator_global::get();
  ag->total_list = allocator_global::create_block(
      (void *)(hw::self_tile_addr(hw::get_parity_dev()) +
               hw::heap_begin_offset + sizeof(allocator_global)),
      hw::heap_size);
}

/// This malloc will return nullptr on failure.
void *try_malloc(uint32_t size) {
  /// extend size to the next multiple of alloc_align;
  size = (size + (alloc_align - 1)) & ~(alloc_align - 1);
  allocator_global *ag = allocator_global::get();
  block_header *bh = ag->total_list;
  while (bh) {
    if (bh->size >= size && !bh->in_use) {
      if (bh->is_splitable(size))
        bh->split(size);
      bh->in_use = 1;
      return bh->get_alloc();
    }
    bh = bh->get_next();
  }
  return nullptr;
}

/// This malloc will assert on allocation failure.
void* malloc(uint32_t size) {
  void* ret = try_malloc(size);
  assert(ret != 0 && "unhandled dynamic allocation failure");
  return ret;
}

void free(void* p) {
  block_header *bh = block_header::get_header(p);
  bh->in_use = 0;
  /// TODO merge with nearby unused blocks.
}

#endif

}

}

#endif
