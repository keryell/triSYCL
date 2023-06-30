
#ifndef AIE_DETAIL_DEVICE_AND_HOST_HPP
#define AIE_DETAIL_DEVICE_AND_HOST_HPP

#if defined(__AIE_EMULATION__)
#error "should only be used in host or device side for hardware"
#endif

#include "common.hpp"
#include "program_manager.hpp"
#include "xaie_wrapper.hpp"
#include "exec_kernel.hpp"
#include "sync.hpp"

namespace aie::detail {

#if !defined(__SYCL_DEVICE_ONLY__)
struct host_lock_impl {
 private:
  xaie::handle h;
  int id;

 public:
  host_lock_impl(xaie::handle h, hw::dir d, int i) : h(h.on(d)), id{i} {
    assert(i < 16);
  }
  void acquire() { h.acquire(id); }
  void release() { h.release(id); }
  void acquire_with_value(bool val) { h.acquire_with_value(id, val); }
  void release_with_value(bool val) { h.release_with_value(id, val); }
};
using device_lock_impl = device_lock_impl_fallback;
#endif

struct service_device_side {

  static volatile service_device_side* get() {
    return hw::get_object<service_device_side>(
        hw::offset_table::get_service_record_begin_offset());
  }
  soft_barrier::device_side barrier;
  uint32_t index;
  hw::dev_ptr<volatile void> data;
  hw::dev_ptr<volatile void> ret;

  /// This asks the host to wait for on other request from the same device
  /// after processing this request. this exist to prevent le host from
  /// interleaving log_internal requests.
  uint32_t chained_request;

  /// Send data to the host to be processed.
  template <typename Ty> uint32_t perform(Ty&& d, bool chained = false) {
    /// Write the data.
    // Var v = d;
    // data = &v;
    // chained_request = chained;
    // /// Notify the host of the data being available.
    // barrier.wait();
    // /// Wait for the host to process the data.
    // barrier.wait();
    // return ret_val;
  }
};

static_assert(sizeof(service_device_side) == hw::offset_table::get_service_record_size());

struct host_tile_impl : host_tile_impl_fallback {
  void* mem_ptr = nullptr;
  std::size_t mem_size = 0;
  xaie::handle dev_handle;
  template <typename LambdaTy, typename DeviceTileTy>
  void execute(LambdaTy l, DeviceTileTy& dt) {
    exec_kernel<DeviceTileTy> {}.exec(
        dev_handle, l, sizeof(typename DeviceTileTy::self_memory_tile),
        [&]<typename KI>(auto& L, uint32_t dev_addr, uint32_t heap_start) {
          write_lambda<KI>(L, dev_addr, heap_start);
        });
  }
  void notify_has_accessed_mem(void* mem, std::size_t size) {
    mem_ptr = mem;
    mem_size = size;
  }
#if !defined(__SYCL_DEVICE_ONLY__)
  template <typename DeviceImplTy>
  void init(DeviceImplTy& d, hw::position pos) {
    dev_handle = d.get_handle(pos);
    dev_handle.core_reset();
  }
  ~host_tile_impl() {
    for (auto& elem : write_backs) {
      elem();
    }
  }
  std::vector<std::function<void()>> write_backs;
  /// Write the lambda on the device such that the kernel can use it.
  template <typename KernelDesc, typename KernelLambda>
  void write_lambda(KernelLambda& L, uint32_t dev_lambda_addr,
                    uint32_t heap_start) {
    TRISYCL_DUMP2("Lambda address = " << (void*)(std::uintptr_t)dev_lambda_addr,
                  "memory");

    /// Write the lambda to memory, the accessors will get corrected later.
    dev_handle.store<KernelLambda, /*no_check*/ true>(dev_lambda_addr, L);
    for (int i = 0; i < KernelDesc::getNumParams(); i++) {
      kernel_param_desc_t kdesc = KernelDesc::getParamDesc(i);
      if (kdesc.kind != kernel_param_kind_t::kind_accessor)
        continue;
      auto* acc_addr = reinterpret_cast<host_accessor_impl*>(
          reinterpret_cast<char*>(&L) + kdesc.offset);
      device_accessor_impl dev_acc;
      dev_acc.size_ = acc_addr->size();
      unsigned size_in_bytes = dev_acc.size_ * acc_addr->impl->elem_size;
      unsigned dev_data_addr =
          heap::malloc(dev_handle, heap_start, size_in_bytes);
      dev_handle.memcpy_h2d(dev_data_addr, acc_addr->impl->data, size_in_bytes);
      dev_acc.data =
          hw::dev_ptr<char>::create(dev_handle.get_self_dir(), dev_data_addr);
      dev_handle.store<device_accessor_impl, /*no_check*/ true>(
          dev_lambda_addr + kdesc.offset, dev_acc);
      write_backs.push_back(
          [=, dev_handle = dev_handle, host_addr = acc_addr->impl->data]() mutable {
            dev_handle.memcpy_d2h(host_addr, dev_data_addr, size_in_bytes);
          });
    }
    if (mem_ptr) {
      /// If the memory tile was accessed send it to the device.
      dev_handle.memcpy_h2d(hw::offset_table::get_tile_mem_begin_offset(),
                            mem_ptr, mem_size);
    }
  }
  host_lock_impl lock(int i) {
    return host_lock_impl(dev_handle, hw::dir::self, i);
  }
#endif
  void register_accessor(const detail::accessor_common& accessor) {}
};

} // namespace aie::detail

#endif
