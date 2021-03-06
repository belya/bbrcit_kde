#ifndef BBRCITKDE_RECTANGLE_H__
#define BBRCITKDE_RECTANGLE_H__

#include <cmath>
#include <vector>
#include <sstream>
#include <string>
#include <utility>
#include <algorithm>

#include <Point.h>
#include <Interval.h>
#include <KdeTraits.h>

// API
// ---

namespace bbrcit {

template <int D, typename T> class Rectangle;

template <int D, typename T> 
void swap(Rectangle<D,T>&, Rectangle<D,T>&);

// prints Rectangle<>'s as { e1, e2, ..., eD } and ei's are the printed edges. 
template <int D, typename T> 
std::ostream& operator<<(std::ostream&, const Rectangle<D,T>&);

// returns true if the arguments intersect.
template <int D, typename T> 
bool intersect(const Rectangle<D,T>&, const Rectangle<D,T>&);

// Rectangle<>'s model rectangle's in D-dim euclidean space. 
template <int D, typename T=double>
class Rectangle {

  public: 

    using FloatType = T;
    using EdgeType = Interval<T>;
    static constexpr int dim() { return D; }

    friend void swap<>(Rectangle<D,T>&, Rectangle<D,T>&);

    Rectangle();
    Rectangle(const Point<D,T>&, const Point<D,T>&);
    Rectangle(const Rectangle<D,T>&);
    Rectangle(Rectangle<D,T>&&) noexcept;
    Rectangle& operator=(const Rectangle<D,T>&);
    Rectangle& operator=(Rectangle<D,T>&&) noexcept;
    ~Rectangle();

    // index access to edge intervals. 
    const EdgeType& operator[](int) const;
    EdgeType& operator[](int);

    // resize the interval of the d'th dimension to e.
    void resize(size_t d, const EdgeType &e);

    // resize this Rectangle<> to that defined by the Point<>'s in the argument.
    void resize(const Point<D,T>&, const Point<D,T>&);

    // return the lower/upper halfspace when splitting in the 
    // dth dimension at value v
    Rectangle<D,T> lower_halfspace(size_t d, const T &v) const;
    Rectangle<D,T> upper_halfspace(size_t, const T&) const;

    // returns true if the argument is fully contained in this Rectangle<>. 
    // Examples of GT: Rectangle<>, Point<>. 
    template<typename GT> bool contains(const GT&) const;

    // returns the min/max L2 distance from the argument to this Rectangle<>. 
    // Examples of GT: Rectangle<>, Point<>. 
    template <typename GT> T min_dist(const GT &g) const;
    template <typename GT> T max_dist(const GT &g) const;

    // returns the min/max distance from the argument to this Rectangle<> in 
    // the `i`th dimension. 
    // Examples of GT: Rectangle<>, Point<>. 
    template <typename GT> T min_dist(size_t i, const GT &g) const;
    template <typename GT> T max_dist(size_t i, const GT &g) const;

  private:

    // Rectangle<D,T>'s are represented as a list of pair<T,T>'s of length D.
    // each pair<> represents the closed interval [lower, upper], where upper >= lower. 
    std::vector<EdgeType> intervals_;
};

// Implementations
// ---------------

template <int D, typename T>
Rectangle<D,T> Rectangle<D,T>::lower_halfspace(size_t d, const T &v) const {

  if (!intervals_[d].contains(v)) {
    std::ostringstream sout;
    sout << "Rectangle<>: lower_halfspace(size_t d, const T &v): ";
    sout << "partition point " << v << " must be contained in edge " << intervals_[d] << " of ";
    sout << "Rectangle<> " << *this << std::endl;
    throw std::length_error(sout.str());
  }

  Rectangle<D,T> r(*this);
  r.resize(d, EdgeType(intervals_[d].lower(), v));
  return r;
}

template <int D, typename T>
Rectangle<D,T> Rectangle<D,T>::upper_halfspace(size_t d, const T &v) const {

  if (!intervals_[d].contains(v)) {
    std::ostringstream sout;
    sout << "Rectangle<>: upper_halfspace(size_t d, const T &v): ";
    sout << "partition point " << v << " must be contained in edge " << intervals_[d] << " of ";
    sout << "Rectangle<> " << *this << std::endl;
    throw std::length_error(sout.str());
  }

  Rectangle<D,T> r(*this);
  r.resize(d, EdgeType(v, intervals_[d].upper()));
  return r;
}

template <int D, typename T>
void swap(Rectangle<D,T> &lhs, Rectangle<D,T> &rhs) {
  using std::swap; swap(lhs.intervals_, rhs.intervals_);
}

template <int D, typename T>
Rectangle<D,T>::Rectangle() : intervals_(D, EdgeType(T(), T()) ) {}

template <int D, typename T>
Rectangle<D,T>::~Rectangle() {}

template <int D, typename T>
Rectangle<D,T>::Rectangle(const Point<D,T> &p1, const Point<D,T> &p2) {
  for (int i = 0; i < D; ++i) { 
    auto p = std::minmax(p1[i], p2[i]);
    intervals_.emplace_back(p.first, p.second); 
  }
}

template <int D, typename T>
Rectangle<D,T>::Rectangle(const Rectangle &r) : intervals_(r.intervals_) {}

template <int D, typename T>
Rectangle<D,T>::Rectangle(Rectangle &&r) noexcept : intervals_(std::move(r.intervals_)) {}

template <int D, typename T>
Rectangle<D,T>& Rectangle<D,T>::operator=(const Rectangle &r) {
  if (this == &r) { return *this; }
  intervals_ = r.intervals_;
  return *this;
}

template <int D, typename T>
Rectangle<D,T>& Rectangle<D,T>::operator=(Rectangle &&r) noexcept {
  if (this == &r) { return *this; }
  intervals_ = std::move(r.intervals_);
  return *this;
}

template <int D, typename T>
  template <typename GT> 
T Rectangle<D,T>::min_dist(const GT &g) const {

  T total = ConstantTraits<T>::zero(); 
  T curr = ConstantTraits<T>::zero(); 

  for (int i = 0; i < D; ++i) {
    curr = intervals_[i].min_dist(g[i]);
    curr *= curr;
    total += curr;
  }
  return std::sqrt(total);
}

template <int D, typename T>
  template <typename GT> 
T Rectangle<D,T>::max_dist(const GT &g) const {

  T total = ConstantTraits<T>::zero(); 
  T curr = ConstantTraits<T>::zero(); 

  for (int i = 0; i < D; ++i) {
    curr = intervals_[i].max_dist(g[i]);
    curr *= curr;
    total += curr;
  }
  return std::sqrt(total);
}

template <int D, typename T>
  template <typename GT> 
inline T Rectangle<D,T>::min_dist(size_t i, const GT &g) const {
  return intervals_[i].min_dist(g[i]);
}

template <int D, typename T>
  template <typename GT> 
inline T Rectangle<D,T>::max_dist(size_t i, const GT &g) const {
  return intervals_[i].max_dist(g[i]);
}

template <int D, typename T>
template <typename GT>
bool Rectangle<D,T>::contains(const GT &g) const {
  bool is_contained = true;
  for (int i = 0; i < D && is_contained; ++i) {
    is_contained = intervals_[i].contains(g[i]);
  }
  return is_contained;
}

template <int D, typename T>
inline void Rectangle<D,T>::resize(size_t d, const EdgeType &e) { (*this)[d] = e; }

template <int D, typename T>
void Rectangle<D,T>::resize(const Point<D,T> &p1, const Point<D,T> &p2) { 
  for (int i = 0; i < D; ++i) { 
    auto p = std::minmax(p1[i], p2[i]);
    intervals_[i] = EdgeType(p.first, p.second);
  }
}

template <int D, typename T> 
bool intersect(const Rectangle<D,T> &lhs, const Rectangle<D,T> &rhs) {
  bool is_intersect = true;
  for (int i = 0; i < D && is_intersect; ++i) { 
    is_intersect = intersect(lhs[i], rhs[i]); 
  }
  return is_intersect;
}

template <int D, typename T> 
std::ostream& operator<<(std::ostream &os, const Rectangle<D,T> &r) {
  os << "{ "; os << r[0];
  for (int i = 1; i < D; ++i) { os << ", "; os << r[i]; }
  os << " }"; 
  return os;
}

template <int D, typename T>
inline const typename Rectangle<D,T>::EdgeType& 
Rectangle<D,T>::operator[](int i) const { return intervals_.at(i); }

template <int D, typename T>
inline typename Rectangle<D,T>::EdgeType& 
Rectangle<D,T>::operator[](int i) { 
  return const_cast<EdgeType&>(static_cast<const Rectangle&>(*this)[i]);
}


}

#endif
