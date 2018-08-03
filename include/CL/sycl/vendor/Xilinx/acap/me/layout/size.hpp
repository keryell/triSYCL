#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_LAYOUT_SIZE_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_LAYOUT_SIZE_HPP

/** \file Flexible layout for a MathEngine array with any 2D size

    Ronan at Keryell point FR

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include "full.hpp"

namespace cl::sycl::vendor::xilinx::acap::me::layout {

/** Describe the layout of a tiny MathEngine array with only 1 PE
 */
template <int X_size, int Y_size>
struct size : full {
  static_assert(X_size >= 1 && Y_size >= 1, "Sorry, "
                "we do not yet support antimatter or black matter cores :-)");
  static auto constexpr x_max = X_size - 1;
  static auto constexpr y_max = Y_size - 1;
};

}
/*
    # Some Emacs stuff:
    ### Local Variables:
    ### ispell-local-dictionary: "american"
    ### eval: (flyspell-prog-mode)
    ### End:
*/

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_ME_LAYOUT_SIZE_HPP
