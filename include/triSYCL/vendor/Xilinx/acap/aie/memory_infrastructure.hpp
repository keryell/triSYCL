#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_INFRASTRUCTURE_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_INFRASTRUCTURE_HPP

/** \file

    The basic AI Engine homogeneous memory infrastructure,
    i.e. independent of x & y coordinates.

    This is owned by a device tile, so for example the lock
    configuration can survive to some program changes.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include "lock.hpp"

namespace trisycl::vendor::xilinx::acap::aie {

/// \ingroup aie
/// @{

/** The AI Engine memory infrastructure common to all the tiles

    This allows some type erasure while accessing the common
    memory infrastructure.

    \param Geography is the \c geography type representing the full
    layout of the architecture
*/
class memory_infrastructure {
  /// The lock unit of the memory tile
  lock_unit memory_locking_unit;

  /** Keep track of the aie::detail::device for hardware resource
      control in device mode or for debugging purpose for better
      messages.

      Use void* for now to avoid cyclic header dependencies for now
      instead of the aie::detail::device */
  void* dev [[maybe_unused]];

 public:
  /** Start the memory infrastructure associated to the AIE device tile

      \param[in] dev is the aie::detail::device used to control
      hardware when using real hardware and provide some debug
      information from inside the tile_infrastructure.

      Use auto concept here to avoid explicit type causing circular
      dependency
  */
  memory_infrastructure(auto& dev)
      : dev { &dev } {}

  /// Get access to a specific lock in this memory module
  auto& lock(int i) { return memory_locking_unit.lock(i); }
};

/// @} End the aie Doxygen group

} // namespace trisycl::vendor::xilinx::acap::aie

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_INFRASTRUCTURE_HPP
