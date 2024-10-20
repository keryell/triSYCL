#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP

/** \file

    The basic AI Engine homogeneous tile, with common content to all
    the tiles (i.e. independent of x & y coordinates) but depending on
    a collective program.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include "tile_infrastructure.hpp"

namespace trisycl::vendor::xilinx::acap::aie {

/// \ingroup aie
/// @{

/** The AI Engine tile infrastructure common to all the tiles

    This allows some type erasure while accessing the common
    tile infrastructure.

    \param AIE_Program is the type representing the full CGRA program
    with the tile programs and memory contents
*/
template <typename AIE_Program>
class tile_base {

  using geo = typename AIE_Program::device::geo;

protected:

  /// Keep a reference to the AIE_Program with the full tile and memory view
  /// \todo can probably be removed
  AIE_Program *program;

  /// Keep a reference to the tile_infrastructure hardware features
  tile_infrastructure<geo> ti;

public:

  /** Provide a run member function that does nothing so it is
      possible to write a minimum AI Engine program that does nothing.

      Note that even if this function is not virtual, in the common
      case a programmer implements it to specify the program executed
      by a tile
  */
  void run() {
  }


  /// Submit a callable on this tile
  template <typename Work>
  void single_task(Work &&f) {
    ti.single_task(std::forward<Work>(f));
  }


  /// Wait for the execution of the callable on this tile
  void wait() {
    ti.wait();
  }


  /// Access the cascade connections
  auto &cascade() {
    // \todo the cascade should be part of the tile infrastructure instead
    return program->cascade();
  }


  /** Get the user input connection from the AXI stream switch

      \param[in] port is the port to use
  */
  auto &in_connection(int port) {
    return ti.in_connection(port);
  }


  /** Get the user output connection to the AXI stream switch

      \param[in] port is port to use
  */
  auto &out_connection(int port) {
    return ti.out_connection(port);
  }


  /// Get the user input port from the AXI stream switch
  auto &in(int port) {
    return ti.in(port);
  }


  /// Get the user output port to the AXI stream switch
  auto &out(int port) {
    return ti.out(port);
  }


  /// Store a way to access to the program
  void set_program(AIE_Program &p) {
    program = &p;
  }


  /// Store a way to access to hardware infrastructure of the tile
  void set_tile_infrastructure(tile_infrastructure<geo> t) {
    ti = t;
  }

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

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP
