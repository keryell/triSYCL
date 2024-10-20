#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_INFRASTRUCTURE_DETAIL_TILE_INFRASTRUCTURE_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_INFRASTRUCTURE_DETAIL_TILE_INFRASTRUCTURE_HPP

/** \file

    The basic AI Engine homogeneous tile, with the common
    infrastructure to all the tiles, i.e. independent of x & y
    coordinates, but also from the tile program itself.

    This tile can be seen as the raw CGRA subdevice to run elemental
    functions.

    This is owned by the device, so for example the AXI stream switch
    configuration and packet can survive to some program changes.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include <array>
#include <future>
#include <memory>
#include <optional>

#include "magic_enum.hpp"
#include <boost/format.hpp>
#include <range/v3/all.hpp>

#include "../../axi_stream_switch.hpp"
#include "../../dma.hpp"
#include "triSYCL/detail/fiber_pool.hpp"
#include "triSYCL/detail/ranges.hpp"
#include "triSYCL/vendor/Xilinx/config.hpp"
#include "triSYCL/vendor/Xilinx/latex.hpp"

namespace trisycl::vendor::xilinx::acap::aie::detail {

/// \ingroup aie
/// @{

/** The AI Engine tile infrastructure common to all the tiles

    This allows some type erasure while accessing the common
    tile infrastructure.

    \param AIE is the type representing the full CGRA with the
    programs and memory contents
*/
template <typename Geography> class tile_infrastructure {

 public:
  using geo = Geography;
  using axi_ss_geo = typename geo::core_axi_stream_switch;
  using mpl = typename axi_ss_geo::master_port_layout;
  using spl = typename axi_ss_geo::slave_port_layout;
  using axi_ss_t = axi_stream_switch<axi_ss_geo>;

 private:
  /// Keep the horizontal coordinate
  int x_coordinate;

  /// Keep the vertical coordinate
  int y_coordinate;

  /** Keep track of the aie::detail::device for hardware resource
      control in device mode or for debugging purpose for better
      messages.

      Use void* for now to avoid cyclic header dependencies for now
      instead of the aie::detail::device */
  void* dev [[maybe_unused]];

  /// The AXI stream switch of the core tile
  axi_ss_t axi_ss;

  /** Keep track of all the infrastructure tile memories
      of this device */
  aie::memory_infrastructure mi;

  /** Sending DMAs

      Use std::optional to postpone initialization */
  std::array<std::optional<sending_dma<axi_ss_t>>, axi_ss_geo::s_dma_size>
      tx_dmas;

#if TRISYCL_XILINX_AIE_TILE_CODE_ON_FIBER
  /// Keep track of the fiber executor
  ::trisycl::detail::fiber_pool* fe;

  /// To shepherd the working fiber
  ::trisycl::detail::fiber_pool::future<void> future_work;
#else
  /// Keep track of the std::thread execution in this tile
  std::future<void> future_work;
#endif

  /** Map the user input port number to the AXI stream switch port

      \param[in] port is the user port to use
  */
  static auto translate_input_port(int port) {
    return axi_ss_t::translate_port(port, spl::me_0, spl::me_last,
                                    "The core input port is out of range");
  }

  /** Map the user output port number to the AXI stream switch port

      \param[in] port is the user port to use
  */
  static auto translate_output_port(int port) {
    return axi_ss_t::translate_port(port, mpl::me_0, mpl::me_last,
                                    "The core output port is out of range");
  }

 public:
  /** Start the tile infrastructure associated to the AIE device tile

      \param[in] x is the horizontal coordinate for this tile

      \param[in] y is the vertical coordinate for this tile

      \param[in] dev is the aie::detail::device used to control
      hardware when using real hardware and provide some debug
      information from inside the tile_infrastructure. Use auto
      concept here to avoid explicit type causing circular dependency

      \param[in] fiber_executor is the executor used to run
      infrastructure details
  */
  tile_infrastructure(int x, int y, auto& dev,
                      ::trisycl::detail::fiber_pool& fiber_executor)
      : x_coordinate { x }
      , y_coordinate { y }
      , dev { &dev }
      , mi { dev }
#if TRISYCL_XILINX_AIE_TILE_CODE_ON_FIBER
      , fe { &fiber_executor }
#endif
  {
    // Connect the core receivers to its AXI stream switch
    for (auto p : views::enum_type(mpl::me_0, mpl::me_last))
      output(p) =
          std::make_shared<port_receiver<axi_ss_t>>(axi_ss, "core_receiver");
    axi_ss.start(x, y, fiber_executor);
    /* Create the core tile receiver DMAs and make them directly the
       switch output ports */
    for (auto p : axi_ss_geo::m_dma_range)
      output(p) =
          std::make_shared<receiving_dma<axi_ss_t>>(axi_ss, fiber_executor);
    /* Create the core tile sender DMAs and connect them internally to
       their switch input ports */
    for (const auto& [d, p] :
         ranges::views::zip(tx_dmas, axi_ss_geo::s_dma_range))
      d.emplace(fiber_executor, input(p));
  }

  /// Get the horizontal coordinate
  int x() { return x_coordinate; }

  /// Get the vertical coordinate
  int y() { return y_coordinate; }

  /// Access to the common infrastructure part of a tile memory
  auto& mem() {
    return mi;
  }

  /** Get the user input connection from the AXI stream switch

      \param[in] port is the port to use
  */
  auto& in_connection(int port) {
    /* The input port for the core is actually the corresponding
       output on the switch */
    return axi_ss.out_connection(translate_output_port(port));
  }

  /** Get the user output connection to the AXI stream switch

      \param[in] port is port to use
  */
  auto& out_connection(int port) {
    /* The output port for the core is actually the corresponding
       input on the switch */
    return axi_ss.in_connection(translate_input_port(port));
  }

  /** Get the user input port from the AXI stream switch

      \param[in] port is the port to use
  */
  auto& in(int port) {
    TRISYCL_DUMP_T("in(" << port << ") on tile(" << x_coordinate << ','
                         << y_coordinate << ')');
    return *in_connection(port);
  }

  /** Get the user output port to the AXI stream switch

      \param[in] port is the port to use
  */
  auto& out(int port) {
    TRISYCL_DUMP_T("out(" << port << ") on tile(" << x_coordinate << ','
                          << y_coordinate << ')');
    return *out_connection(port);
  }

  /** Get access to a receiver DMA

      \param[in] id specifies which DMA to access */
  auto& rx_dma(int id) {
    /** The output of the switch is actually a receiving DMA, so we
        can view it as a DMA */
    return static_cast<receiving_dma<axi_ss_t>&>(*output(
        axi_ss_t::translate_port(id, mpl::dma_0, mpl::dma_last,
                                 "The receiver DMA port is out of range")));
  }

  /** Get access to a transmit DMA

      \param[in] id specifies which DMA to access */
  auto& tx_dma(int id) { return *tx_dmas.at(id); }

  /** Get the input router port of the AXI stream switch

      \param p is the slave_port_layout for the stream
  */
  auto& input(spl p) {
    // No index validation required because of type safety
    return axi_ss.input(p);
  }

  /** Get the output router port of the AXI stream switch

      \param p is the master_port_layout for the stream
  */
  auto& output(mpl p) {
    // No index validation required because of type safety
    return axi_ss.output(p);
  }

  /// Launch an invocable on this tile
  template <typename Work> void single_task(Work&& f) {
    if (future_work.valid())
      throw std::logic_error("Something is already running on this tile!");
    // Launch the tile program immediately on a new executor engine
#if TRISYCL_XILINX_AIE_TILE_CODE_ON_FIBER
    future_work = fe->submit(std::forward<Work>(f));
#else
    future_work = std::async(std::launch::async, std::forward<Work>(f));
#endif
  }

  /// Wait for the execution of the callable on this tile
  void wait() { if (future_work.valid())
      future_work.get();
  }

  /// Configure a connection of the core tile AXI stream switch
  void connect(typename geo::core_axi_stream_switch::slave_port_layout sp,
               typename geo::core_axi_stream_switch::master_port_layout mp) {
    axi_ss.connect(sp, mp);
  }

  /// Compute the size of the graphics representation of the processor
  static vec<int, 2> display_core_size() {
    // This is the minimum rectangle fitting all the processor outputs & inputs
    return { 1 + ranges::distance(axi_ss_geo::m_me_range),
             1 + ranges::distance(axi_ss_geo::s_me_range) };
  }

  /// Compute the size of the graphics representation of the tile
  static vec<int, 2> display_size() {
    // Just the sum of the size of its content
    return display_core_size() + axi_ss_t::display_size();
  }

  /// Display the tile to a LaTeX context
  void display(latex::context& c) const {
    auto get_tikz_coordinate = [&](auto x, auto y) {
      auto const [x_size, y_size] = display_size();
      return (boost::format { "(%1%,%2%)" }
              // Scale real LaTeX coordinate to fit
              % c.scale(x_coordinate * x_size + x) %
              c.scale(y_coordinate * y_size + y))
          .str();
    };
    c.add((boost::format { "  \\begin{scope}[name prefix = TileX%1%Y%2%]" } %
           x_coordinate % y_coordinate)
              .str());
    axi_ss.display(c, display_core_size(), get_tikz_coordinate);

    // Connect the core receivers to its AXI stream switch
    for (auto [i, p] : axi_ss_geo::m_me_range | ranges::views::enumerate) {
      c.add(
          (boost::format { R"(
    \coordinate(CoreIn%1%) at %2%;
    \node[rotate=90,anchor=east](CoreIn%1%Label) at %2% {in(%1%)};
    \draw (node cs:name=MMe%1%)
       -| (node cs:name=CoreIn%1%);)" } %
           i %
           get_tikz_coordinate(i, ranges::distance(axi_ss_geo::m_me_range) + 1))
              .str());
    };
    // Connect the core senders to its AXI stream switch
    for (auto [i, p] : axi_ss_geo::s_me_range | ranges::views::enumerate) {
      c.add(
          (boost::format { R"(
    \coordinate(CoreOut%1%) at %2%;
    \node[anchor=east](CoreOut%1%Label) at %2%  {out(%1%)};
    \draw (node cs:name=CoreOut%1%)
       -| (node cs:name=SMe%1%);)" } %
           i %
           get_tikz_coordinate(ranges::distance(axi_ss_geo::s_me_range), i + 1))
              .str());
    };
    c.add((boost::format { R"(
    \node[black] () at %1% {\texttt{tile<%2%,%3%>}};
    \begin{scope}[on background layer]
      \node [fill=orange!30, fit={(node cs:name=CoreIn0Label)
                                  (node cs:name=CoreOut0Label)}]
            (Core) {};
    \end{scope}
  \end{scope}

)" } % get_tikz_coordinate(1, 0) %
           x_coordinate % y_coordinate)
              .str());
  }
};

/// @} End the aie Doxygen group

} // namespace trisycl::vendor::xilinx::acap::aie::detail

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_INFRASTRUCTURE_DETAIL_TILE_INFRASTRUCTURE_HPP
