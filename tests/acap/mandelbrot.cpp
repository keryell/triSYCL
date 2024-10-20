/* Mandelbrot set for AI Engine

   RUN: %{execute}%s
*/

#include "triSYCL/vendor/Xilinx/graphics.hpp"
#include <complex>
#include <cstdint>
#include <sycl/sycl.hpp>

using namespace sycl::vendor::xilinx;
auto constexpr image_size = 229;
graphics::application a;

// All the memory modules are the same
template <typename AIE, int X, int Y>
struct memory : acap::aie::memory<AIE, X, Y> {
  // The local pixel tile inside the complex plane
  std::uint8_t plane[image_size][image_size];
};

// All the tiles run the same Mandelbrot program
template <typename AIE, int X, int Y>
struct mandelbrot : acap::aie::tile<AIE, X, Y> {
  using t = acap::aie::tile<AIE, X, Y>;
  // Computation rectangle in the complex plane
  static auto constexpr x0 = -2.1, y0 = -1.2, x1 = 0.6, y1 = 1.2;
  static auto constexpr D = 100; // Divergence norm
  // Size of an image tile
  static auto constexpr xs = (x1 - x0) / t::geo::x_size / image_size;
  static auto constexpr ys = (y1 - y0) / t::geo::y_size / image_size;

  void operator()() {
    // Access to its own memory
    auto& m = t::mem();
    while (!a.is_done()) {
      for (int j = 0; j < image_size; ++j)
        for (int k, i = 0; i < image_size; ++i) {
          std::complex c { x0 + xs * (X * image_size + i),
                           y0 + ys * (Y * image_size + j) };
          std::complex z { 0.0 };
          for (k = 0; norm(z = z * z + c) < D && k <= 255; k++)
            ;
          m.plane[j][i] = k;
        }
      a.update_tile_data_image(t::x, t::y, &m.plane[0][0], 0, 255);
    }
  }
};

int main(int argc, char* argv[]) {
  acap::aie::device<acap::aie::layout::size<2, 3>> aie;
  // Open a graphic view of a AIE array
  a.start(argc, argv, aie.x_size, aie.y_size, image_size, image_size, 1)
      .image_grid()
      .get_palette()
      .set(graphics::palette::rainbow, 100, 2, 0);

  // Launch the AI Engine program
  aie.run<mandelbrot, memory>();
}
