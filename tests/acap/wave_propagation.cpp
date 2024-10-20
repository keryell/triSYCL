/* Demo of wave propagation for AI Engine

   Simulation with a conic drop, a circle shoal and a square harbor.

   Recycle MINES ParisTech/ISIA/Telecom Bretagne MSc hands-on HPC labs
   from Ronan Keryell

   https://en.wikipedia.org/wiki/Boussinesq_approximation_(water_waves)
   Joseph Valentin Boussinesq, 1872

   RUN: %{execute}%s
*/

/** Predicate for time-step comparison with sequential cosimulation

    0: for no co-simulation

    1: compare the parallel execution with sequential execution
*/
#define COMPARE_WITH_SEQUENTIAL_EXECUTION 0

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>

#include <experimental/mdspan>

#include <sycl/sycl.hpp>
#include "triSYCL/vendor/Xilinx/graphics.hpp"

// Some headers used when debugging
#include <chrono>
#include <thread>
using namespace std::chrono_literals;

#include <boost/thread.hpp>

using namespace sycl::vendor::xilinx;

/// The type used to do all the computations
using data_t = float;

// The size of the machine to use
//using layout = acap::aie::layout::size<5,4>;
// For a 1920x1080 display
using layout = acap::aie::layout::size<18,8>;
// For a 3440x1440 display
//using layout = acap::aie::layout::size<33,12>;
using geography = acap::aie::geography<layout>;
boost::barrier b1 { geography::size };
boost::barrier b2 { geography::size };
boost::barrier b3 { geography::size };
boost::barrier b4 { geography::size };

static auto constexpr K = 1/300.0;
static auto constexpr g = 9.81;
static auto constexpr alpha = K*g;
/// Some dissipation factor to avoid divergence
static auto constexpr damping = 0.999;

/// Edge size of the tile square images
static auto constexpr image_size = 100
;
/// Add a drop almost between tile (1,1) and (2,2)
static auto constexpr x_drop = image_size*2 - 3;
static auto constexpr y_drop = image_size*2;
static auto constexpr drop_value = 100;

/** Time-step interval between each display.
    Use 1 to display all the frames, 2 for half the frame and so on. */
static auto constexpr display_time_step = 2;

graphics::application a;

auto epsilon = 0.01;

#if COMPARE_WITH_SEQUENTIAL_EXECUTION == 1
/** Compare the values of 2 2D mdspan of the same geometry

    Display any discrepancy between an acap and reference mdspan
*/
auto compare_2D_mdspan = [](auto message, const auto &acap, const auto &ref) {
  assert(acap.extent(0) == ref.extent(0));
  assert(acap.extent(1) == ref.extent(1));
  for (int j = 0; j < acap.extent(0); ++j)
    for (int i = 0; i < acap.extent(1); ++i)
      if (std::abs(acap[j, i] - ref[j, i]) > epsilon) {
        TRISYCL_DUMP_T(std::dec << '\t' << message
                       << " acap(" << j << ',' << i << ") = " << acap[j, i]
                       << "  ref(" << j << ',' << i << ") = " << re[j, i]);
      }
};
#endif


/// Compute the square power of a value
auto square = [] (auto v) constexpr { return v*v; };


/// Compute the contribution of a drop to the water height
auto add_a_drop = [] (auto x, auto y) constexpr {
  auto constexpr drop_radius = 30.0;
  // The square radius to the drop center
  auto r = square(x - x_drop) + square(y - y_drop);
  // A cone of height drop_value centered on the drop center
  return r < square(drop_radius)
             ? drop_value*(square(drop_radius) - r)/square(drop_radius) : 0;
};


/// Add a circular shoal in the water with half the depth
auto shoal_factor = [] (auto x, auto y) constexpr {
  /// The shoal center coordinates
  auto constexpr x_shoal = image_size*8 - 3;
  auto constexpr y_shoal = image_size*4;
  auto constexpr shoal_radius = 200.0;

  // The square radius to the shoal center
  auto r = square(x - x_shoal) + square(y - y_shoal);
  // A disk centered on the shoal center
  return r < square(shoal_radius) ? 0.5 : 1;
};


/// Add a square harbor in the water
auto is_harbor = [] (auto x, auto y) constexpr -> bool {
  /// The square harbor center coordinates
  auto constexpr x_harbor = image_size*14 - image_size/3;
  auto constexpr y_harbor = image_size*6 - image_size/3;
  auto constexpr length_harbor = image_size;

  // A square centered on the harbor center
  auto harbor =
    x_harbor -length_harbor/2 <= x && x <= x_harbor + length_harbor/2
    && y_harbor - length_harbor/2 <= y && y <= y_harbor + length_harbor/2;
  // Add also a breakwater below
  auto constexpr width_breakwater = image_size/20;
  auto breakwater = x_harbor <= x && x <= x_harbor + width_breakwater
    && y < y_harbor - image_size
    // Add some 4-pixel holes every image_size/2
    && (y/4)%(image_size/8);
  return harbor || breakwater;
};


/// A sequential reference implementation of wave propagation
template <auto size_x, auto size_y, auto display_tile_size>
struct reference_wave_propagation {
  using space = std::mdspan<data_t, std::extents<std::size_t, size_y, size_x>>;
  // It would be nice to have a constexpr static member to express this,
  // but right now size() is a member function
  // data_t u_m[space::size()];
  static auto constexpr linear_size = size_x*size_y;
  data_t u_m[linear_size];
  space u { u_m }; // Horizontal speed
  data_t v_m[linear_size];
  space v { v_m }; // Vertical speed
  data_t w_m[linear_size];
  space w { w_m }; // Local delta depth
  data_t side_m[linear_size];
  space side { side_m }; // Hard wall limit
  data_t depth_m[linear_size];
  space depth { depth_m }; // Average depth

  /// Initialize the state variables
  reference_wave_propagation() {
    for (int j = 0; j < size_y; ++j)
      for (int i = 0; i < size_x; ++i) {
        u[j, i] = v[j, i] = w[j, i] = 0;
        side[j, i] = K*(!is_harbor(i, j));
        depth[j, i] = 2600.0*shoal_factor(i, j);
        w[j, i] += add_a_drop(i, j);
      }
  }


  /// Compute a time-step of wave propagation
  void compute() {
    for (int j = 0; j < size_y; ++j)
      for (int i = 0; i < size_x - 1; ++i) {
        // dw/dx
        auto north = w[j, i + 1] - w[j, i];
        // Integrate horizontal speed
        u[j, i] += north*alpha;
      }
    for (int j = 0; j < size_y - 1; ++j)
      for (int i = 0; i < size_x; ++i) {
        // dw/dy
        auto vp = w[j + 1, i] - w[j, i];
        // Integrate vertical speed
        v[j, i] += vp*alpha;
      }
    for (int j = 1; j < size_y; ++j)
      for (int i = 1; i < size_x; ++i) {
        // div speed
        auto wp = (u[j, i] - u[j, i - 1]) + (v[j, i] - v[j - 1, i]);
        wp *= side[j, i]*(depth[j, i] + w[j, i]);
        // Integrate depth
        w[j, i] += wp;
        // Add some dissipation for the damping
        w[j, i] *= damping;
      }
  }


  /// Run the wave propagation
  void run() {
    /// Loop on simulated time
    while (!a.is_done()) {
      compute();
      for (int j = 0; j < size_y/display_tile_size; ++j)
        for (int i = 0; i <  size_x/display_tile_size; ++i) {
          /* Split the data in sub-windows with a subspan

             Display actually one redundant line/column on each
             South/West to mimic the halo in the ACAP case
          */
          auto sp = std::experimental::submdspan
            (w,
             std::make_pair(j*display_tile_size,
                            1 + (j + 1)*display_tile_size),
             std::make_pair(i*display_tile_size,
                            1 + (i + 1)*display_tile_size));
          a.update_tile_data_image(i, j, sp, -1.0, 1.0);
        }
    }
  }

  template <typename Array, typename MDspan_ref>
  void compare_with_sequential_reference_e(const char *message, int x, int y,
                                           Array &arr,
                                           const MDspan_ref &ref) {
    const std::experimental::mdspan md {
      &arr[0][0], std::experimental::extents { image_size, image_size }
    };

    // Take into account 1 line/column of overlapping halo
    int x_offset = md.extent(1) - 1;
    int y_offset = md.extent(0) - 1;
    auto mdref =
      std::experimental::submdspan(ref,
                                   std::make_pair(y*y_offset,
                                                  1 + (y + 1)*y_offset),
                                   std::make_pair(x*x_offset,
                                                  1 + (x + 1)*x_offset));
    compare_2D_mdspan(message, md, mdref);
  }


  /* The global time of the simulation

     Do not put it inside compare_with_sequential_reference because,
     since it is templated, there is then an instance per tile and the
     chaos happens
  */
  static inline int global_time = 0;
  static inline std::mutex protect_time;
  static inline acap::debug::bsp_checker<geography> bsp_checker;

  template <typename Mem>
  void compare_with_sequential_reference(int time, int x, int y, Mem &m) {
    bsp_checker.check(x, y);

#if COMPARE_WITH_SEQUENTIAL_EXECUTION
    {
      std::lock_guard lg { protect_time };
      TRISYCL_DUMP_T(std::dec << "TILE(" << x << ',' << y << ") Time local: "
                     << time << ", global: " << global_time);
      if (global_time != time) {
        /* Advance the sequential computation by one step so that we
           can do the comparison */
        compute();
        ++global_time;
      }
      compare_with_sequential_reference_e("w", x, y, m.w, w);
      compare_with_sequential_reference_e("u", x, y, m.u, u);
      compare_with_sequential_reference_e("v", x, y, m.v, v);
    }
#endif
  }
};


/** A sequential reference implementation of the wave propagation

    Use (image_size - 1) for the tile size to skip the halo zone of 1
    pixel in X and Y
*/
reference_wave_propagation
<(image_size - 1)*acap::aie::geography<layout>::x_size + 1,
 (image_size - 1)*acap::aie::geography<layout>::y_size + 1,
 image_size - 1> seq;


/// All the memory modules are the same
template <typename AIE, int X, int Y>
struct memory : acap::aie::memory<AIE, X, Y> {
  data_t u[image_size][image_size]; //< Horizontal speed
  data_t v[image_size][image_size]; //< Vertical speed
  data_t w[image_size][image_size]; //< Local delta depth
  data_t side[image_size][image_size]; //< Hard wall limit
  data_t depth[image_size][image_size]; //< Average depth
};


TRISYCL_DEBUG_ONLY(
static auto minmax_element(const data_t value[image_size][image_size]) {
  return std::minmax_element(&value[0][0],
                             &value[image_size][image_size]);
}
)

/// All the tiles run the same program
template <typename AIE, int X, int Y>
struct tile : acap::aie::tile<AIE, X, Y> {
  using t = acap::aie::tile<AIE, X, Y>;

  void initialize_space() {
    auto& m = t::mem();
    for (int j = 0; j < image_size; ++j)
      for (int i = 0; i < image_size; ++i) {
        m.u[j][i] = m.v[j][i] = m.w[j][i] = 0;
        m.side[j][i] = K*(!is_harbor(i + (image_size - 1)*X,
                                     j + (image_size - 1)*Y));
        m.depth[j][i] = 2600.0*shoal_factor(i + (image_size - 1)*X,
                                            j + (image_size - 1)*Y);
        // Add a drop using the global coordinate taking into account the halo
        m.w[j][i] += add_a_drop(i + (image_size - 1)*X, j + (image_size - 1)*Y);
      }
  }

  void compute() {
    auto& m = t::mem();

    for (int j = 0; j < image_size; ++j)
      for (int i = 0; i < image_size - 1; ++i) {
        // dw/dx
        auto north = m.w[j][i + 1] - m.w[j][i];
        // Integrate horizontal speed
        m.u[j][i] += north*alpha;
      }

    for (int j = 0; j < image_size - 1; ++j)
      for (int i = 0; i < image_size; ++i) {
        // dw/dy
        auto vp = m.w[j + 1][i] - m.w[j][i];
        // Integrate vertical speed
        m.v[j][i] += vp*alpha;
      }

    t::barrier();

    // Transfer first column of u to next memory module to the West
    if constexpr (Y & 1) {
      if constexpr (t::is_memory_module_east()) {
        auto& east = t::mem_east();
        for (int j = 0; j < image_size; ++j)
          m.u[j][image_size - 1] = east.u[j][0];
      }
    }
    if constexpr (!(Y & 1)) {
      if constexpr (t::is_memory_module_west()) {
        auto& west = t::mem_west();
        for (int j = 0; j < image_size; ++j)
          west.u[j][image_size - 1] = m.u[j][0];
      }
    }

    if constexpr (t::is_memory_module_south()) {
      auto& below = t::mem_south();
      for (int i = 0; i < image_size; ++i)
        below.v[image_size - 1][i] = m.v[0][i];
    }

    t::barrier();

    for (int j = 1; j < image_size; ++j)
      for (int i = 1; i < image_size; ++i) {
        // div speed
        auto wp  = (m.u[j][i] - m.u[j][i - 1]) + (m.v[j][i] - m.v[j - 1][i]);
        wp *= m.side[j][i]*(m.depth[j][i] + m.w[j][i]);
        // Integrate depth
        m.w[j][i] += wp;
        // Add some dissipation for the damping
        m.w[j][i] *= damping;
      }

    t::barrier();

    if constexpr (t::is_memory_module_north()) {
      auto& above = t::mem_north();
      for (int i = 0; i < image_size; ++i)
        above.w[0][i] = m.w[image_size - 1][i];
    }

    //b4.wait();
    t::barrier();

    // Transfer last line of w to next memory module on the East
    if constexpr (Y & 1) {
      if constexpr (t::is_memory_module_east()) {
        auto& east = t::mem_east();
        for (int j = 0; j < image_size; ++j)
          east.w[j][0] = m.w[j][image_size - 1];
      }
    }
    if constexpr (!(Y & 1)) {
      if constexpr (t::is_memory_module_west()) {
        auto& west = t::mem_west();
        for (int j = 0; j < image_size; ++j)
          m.w[j][0] = west.w[j][image_size - 1];
      }
    }

    t::barrier();

    TRISYCL_DEBUG_ONLY(static int iteration = 0;
                       auto [min_element, max_element] = minmax_element(m.w);)
    TRISYCL_DUMP_T(std::dec << "compute(" << X << ',' << Y
                   << ") iteration " << ++iteration << " done, min = "
                   << *min_element << ", max = " << *max_element);
  }

  void run() {
    initialize_space();
    auto& m = t::mem();
    const std::experimental::mdspan md {
      &m.w[0][0], std::experimental::extents { image_size, image_size }
    };
    /// Loop on simulated time
    for (int time = 0; !a.is_done_barrier(); ++time) {
      seq.compare_with_sequential_reference(time, t::x, t::y, m);
      compute();
      // Display every display_time_step
      if (time % display_time_step == 0)
        a.update_tile_data_image(t::x, t::y, md, -1.0, 1.0);
    }
  }
};


int main(int argc, char *argv[]) {
  // An ACAP version of the wave propagation
  acap::aie::device<layout> d;

  a.start(argc, argv, decltype(d)::geo::x_size,
          decltype(d)::geo::y_size,
          image_size, image_size, 1);
  // Clip the level 127 which is the 0 level of the simulation
  a.image_grid().get_palette().set(graphics::palette::rainbow, 150, 2, 127);
#if 0
  // Run the sequential reference implementation
  seq.run();
#endif
  // Launch the AI Engine program
  d.run<tile, memory>();
  // Wait for the graphics to stop
  a.wait();
}
