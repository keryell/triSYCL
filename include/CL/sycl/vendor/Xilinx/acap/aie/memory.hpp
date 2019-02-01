#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_HPP

/** \file

    The basic AI Engine Memory Module

    Based on Math Engine (ME) Architecture Specification, Revision v1.4
    March 2018

    Section 4.5 ME Memory Module, p. 118

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
 */

#include <thread>

#include "lock.hpp"

namespace cl::sycl::vendor::xilinx::acap::aie {

/// \ingroup aie
///   @{

/** The AI Engine Memory Module infrastructure

    This is the type you need to inherit from to define the content of
    a CGRA memory tile.

    \param AIE is the type representing the full CGRA with the
    programs and memory contents

    \param X is the horizontal coordinate of the memory module

    \param Y is the vertical coordinate of the memory module
*/
template <typename AIE //< The type representing the full CGRA
          , int X //< The horizontal coordinate of the memory module
          , int Y //< The vertical coordinate of the memory module
          >
struct memory {
  /** The horizontal tile coordinates in the CGRA grid (starting at 0
      and increasing to the right) */
  static auto constexpr x = X;
  /** The vertical tile coordinates in the CGRA grid (starting at
      increasing to the top) */
  static auto constexpr y = Y;

  /// The lock unit of the memory tile
  lock_unit lu;
};

/// @} End the aie Doxygen group

}

/*
    # Some Emacs stuff:
    ### Local Variables:
    ### ispell-local-dictionary: "american"
    ### eval: (flyspell-prog-mode)
    ### End:
*/

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_MEMORY_HPP
