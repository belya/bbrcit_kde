#include <cmath>
#include <limits>
#include <type_traits>

namespace bbrcit {

// Idea taken from:
// http://en.cppreference.com/w/cpp/types/numeric_limits/epsilon
template <typename T> 
typename std::enable_if<!std::numeric_limits<T>::is_integer, bool>::type 
cppref_almost_equal(T lhs, T rhs, int ulp) {
  // case 1: relative epsilon comparison for normalized numbers. 
  // case 2: absolute error comparison for subnormal numbers.
  return std::abs(lhs-rhs) < std::numeric_limits<T>::epsilon() * ulp * std::abs(lhs+rhs)
         || std::abs(lhs-rhs) < std::numeric_limits<T>::min();
}

// compares whether two floating numbers are ``nearly equal''. in particular,
// nearly_equal() returns true iff one of the following hold:
// 1. their absolute error is at most the minimum normalized number.
// 2. their relative error is at most the machine epsilon. 
//
// Idea taken from:
// https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
//
// A similar idea: 
// http://en.cppreference.com/w/cpp/types/numeric_limits/epsilon
template <typename T> 
typename std::enable_if<!std::numeric_limits<T>::is_integer, bool>::type 
almost_equal(T lhs, T rhs) {

  T diff = std::abs(lhs-rhs);

  // absolute error
  if (diff <= std::numeric_limits<T>::min()) { return true; }

  // relative error
  return diff <= std::numeric_limits<T>::epsilon() * std::max(std::abs(lhs), std::abs(rhs));
}

// lexicographic comparison of PointT objects. note that the 
// equality comparison is == even for floats; this is intentional. 
template<typename PointT> 
bool LexCompare(const PointT &lhs, const PointT &rhs) {
  int i = 0; while (i < lhs.dim() && lhs[i] == rhs[i]) { ++i; }
  return i != lhs.dim() && lhs[i] < rhs[i];
}

}
