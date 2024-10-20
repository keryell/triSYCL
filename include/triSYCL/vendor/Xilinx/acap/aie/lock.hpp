#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_LOCK_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_LOCK_HPP

/** \file

    The lock mechanism used by some AI Engine tiles

    Note that this AI Engine concept is not a pure lock, but more
    like a lock associated with a conditional variable, to follow the
    C++ jargon.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
 */

#include <memory>
#include <mutex>

#include <boost/fiber/all.hpp>

namespace trisycl::vendor::xilinx::acap::aie {

/// \ingroup aie
/// @{

/** The lock infrastructure used by AI Engine memory modules and shim tiles

    Based on Math Engine (ME) Architecture Specification, Revision v1.5
    June 2018

    4.4.6 Lock Interface, p. 115

    4.7 Lock Unit, p. 129
*/
struct lock_unit {
  // There are 16 hardware locks per memory tile
  auto static constexpr lock_number = 16;

  using lock_id_t = decltype(lock_number);

  /// The individual locking system
  struct locking_device {
    /// The type of the value stored in the locking device
    using value_t = bool;

    /* The problem here is that \c mutex and \c condition_variable
       are not moveable while the instantiation of a memory module uses
       move assignment with Boost.Hana...

       So allocate them dynamically and keep them in a \c std::unique_ptr
       so globally the type is moveable */

    /// The mutex to provide the basic protection mechanism
    std::unique_ptr<boost::fibers::mutex> m { new boost::fibers::mutex { } };

    /// The condition variable to wait/notify for some value
    std::unique_ptr<boost::fibers::condition_variable> cv {
      new boost::fibers::condition_variable { } };

    /// The value to be waited for, initialized to false on reset
    value_t value = false;

    /// Lock the mutex
    void acquire() {
      m->lock();
    }


    /// Unlock the mutex
    void release() {
      m->unlock();
    }


    /// Wait until the internal value has the expectation
    void acquire_with_value(value_t expectation) {
      std::unique_lock lk { *m };
      cv->wait(lk, [&] { return expectation == value; });
    }


    /// Release and update with a new internal value
    void release_with_value(value_t new_value) {
      {
        std::unique_lock lk { *m };
        value = new_value;
      }
      // By construction there should be only one client waiting for it
      cv->notify_one();
    }
  };

  /// The locking units of the locking device
  locking_device locks[lock_number];

  /// Get the requested lock
  auto &lock(int i) {
    assert(0 <= i && i < lock_number);
    return locks[i];
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

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_LOCK_HPP
