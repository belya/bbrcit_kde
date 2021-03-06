#include <utility>
#include <limits>
#include <cassert>
#include <iomanip>
#include <random>
#include <stdexcept>

#include <Kernels/ConvKernelAssociator.h>
#include <Kernels/KernelTraits.h>
#include <KdeTraits.h>

namespace bbrcit {

template<typename KT, typename FT, typename AT>
FT lsq_numint_cross_validate( 
#ifndef __CUDACC__
  const KernelDensity<2,KT,FT,AT> &kde, 
  FT start_x, FT end_x, int steps_x, 
  FT start_y, FT end_y, int steps_y,
  FT rel_err, FT abs_err, int qtree_leaf_nmax
#else 
  const KernelDensity<2,KT,FT,AT> &kde, 
  FT start_x, FT end_x, int steps_x, 
  FT start_y, FT end_y, int steps_y,
  FT rel_err, FT abs_err, int qtree_leaf_nmax,
  size_t block_size
#endif
  ) {

  using KernelDensityType = KernelDensity<2,KT,FT,AT>;
  using KdtreeType = typename KernelDensityType::KdtreeType;
  using DataPointType = typename KernelDensityType::DataPointType;

  // compute leave one out contribution
  // ----------------------------------

  // construct a reference query tree out of the data tree to perform 
  // dual tree self-evaluation. we use copy-assignment since we would like
  // to preserve the same ordering of points in both trees. 
  KdtreeType rtree = kde.data_tree();

  // all pairs self-evaluation
#ifndef __CUDACC__
  kde.eval(rtree, rel_err, abs_err);
#else
  kde.eval(rtree, rel_err, abs_err, block_size);
#endif

  // compute leave one out score
  FT llo_cv = ConstantTraits<FT>::zero(), val = ConstantTraits<FT>::zero();
  for (size_t i = 0; i < rtree.points().size(); ++i) {

    // the dual tree gives contributions from all points; must 
    // subtract away the self contribution
    val = rtree.points()[i].attributes().value();
    val -= kde.points()[i].attributes().mass() * kde.kernel().normalization();

    // contribution is weighted
    llo_cv += kde.points()[i].attributes().weight() * val;
  }


  // compute square integral contribution
  // ------------------------------------

  // generate integration grid and build a query tree out of it
  std::vector<DataPointType> q_grid;
  double delta_x = (end_x-start_x)/steps_x;
  double delta_y = (end_y-start_y)/steps_y;
  for (int j = 0; j < steps_y; ++j) {
    for (int i = 0; i < steps_x; ++i) {
      q_grid.push_back({{start_x+i*delta_x, start_y+j*delta_y}});
    }
  }
  KdtreeType qtree(std::move(q_grid), qtree_leaf_nmax);

  // evaluate the kernel density at every grid points
#ifndef __CUDACC__
  kde.eval(qtree, rel_err, abs_err);
#else
  kde.eval(qtree, rel_err, abs_err, block_size);
#endif

  // compute the square integral term. 
  FT self_cv = ConstantTraits<FT>::zero();
  for (const auto &p : qtree.points()) {

    val = p.attributes().value();

    // remember to square the kde value. 
    // we also multiply the area element for numerical purposes. move outside?
    self_cv += val*val*delta_x*delta_y;
  }

  return self_cv - 2 * llo_cv;

}

template<int D, typename KT, typename FT, typename AT>
  template <typename RNG> 
typename KernelDensity<D,KT,FT,AT>::DataPointType 
KernelDensity<D,KT,FT,AT>::simulate(RNG &e) const {
  static std::vector<FloatType> p(D, 0.0);
  DataPointType q; 
  simulate(e, p);
  for (size_t i = 0; i < D; i++) { q[i] = p[i]; }
  return q;
}


template<int D, typename KT, typename FT, typename AT>
  template <typename RNG> 
void KernelDensity<D,KT,FT,AT>::simulate(RNG &e, std::vector<FloatType> &p) const {
  
  // Step 1: choose a random point from the reference tree, but weighted by `weight`
  // i.e. choose point `i` if `i` is the smallest index such that `cum_sum[i]` 
  // is strictly larger than `d(e)`, a random number sampled from uniform(0,1). 

  static std::uniform_real_distribution<FloatType> d(0, 1);

  auto it = std::upper_bound(cum_weights_.begin(), cum_weights_.end(), d(e));
  assert(it != cum_weights_.end());

  const DataPointType &ref_pt = data_tree_.points()[it - cum_weights_.begin()];

  // Step 2: choose a random point from the kernel, but accounting for the local
  // adaptive bandwidth correction. 
  // Note: the cast between float types are necessary. Consider a way to fix this?
  static std::vector<KernelFloatType> q(D, 0.0);
  kernel_.simulate(e, q, static_cast<KernelFloatType>(ref_pt.attributes().abw()));

  // Step 3: combine the result
  p.resize(D);
  for (size_t i = 0; i < D; ++i) { p[i] = ref_pt[i] + q[i]; }

  return;

}

// perform least squares cross validation on the current kernel
// configuration. 
template<int D, typename KT, typename FT, typename AT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::lsq_convolution_cross_validate( 
#ifndef __CUDACC__ 
    FloatType rel_err, FloatType abs_err
#else
    FloatType rel_err, FloatType abs_err, size_t block_size
#endif
    ) const {


  // construct a query tree out of the data tree to perform 
  // dual tree self-evaluation. we use copy-assignment since we would like
  // to preserve the same ordering of points in both trees. 
  KdtreeType query_tree = data_tree_;

  // compute the leave one out contribution
  // --------------------------------------

  // all pairs computation using the default kernel
#ifndef __CUDACC__
  eval(query_tree, kernel_, rel_err, abs_err);
#else
  eval(query_tree, kernel_, rel_err, abs_err, block_size);
#endif

  FloatType val = 0.0;

  // compute leave one out score
  FloatType llo_cv = ConstantTraits<FloatType>::zero();
  for (size_t i = 0; i < query_tree.points_.size(); ++i) {

    // the dual tree gives contributions from all points; must 
    // subtract away the self contribution
    val = query_tree.points_[i].attributes().value();
    val -= data_tree_.points_[i].attributes().mass() * kernel_.normalization();

    // contribution is weighted
    llo_cv += data_tree_.points_[i].attributes().weight() * val;
  }

  // compute the square integral contribution
  // ----------------------------------------

  // induce the convolution kernel out of the default kernel
  typename ConvKernelAssociator<KernelType>::ConvKernelType conv_kernel = 
    ConvKernelAssociator<KernelType>::make_convolution_kernel(kernel_);

  // all pairs computation using the convolution kernel
#ifndef __CUDACC__
  eval(query_tree, conv_kernel, rel_err, abs_err);
#else
  eval(query_tree, conv_kernel, rel_err, abs_err, block_size);
#endif

  // compute square integral score
  FloatType sq_cv = ConstantTraits<FloatType>::zero();
  for (size_t i = 0; i < query_tree.points_.size(); ++i) {

    val = query_tree.points_[i].attributes().value();

    // contribution is weighted
    sq_cv += data_tree_.points_[i].attributes().weight() * val;
  }

  return sq_cv - 2*llo_cv;

}



// perform likelihood cross validation on the current kernel
// configuration. 
template<int D, typename KT, typename FT, typename AT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::likelihood_cross_validate( 
#ifndef __CUDACC__ 
    FloatType rel_err, FloatType abs_err
#else
    FloatType rel_err, FloatType abs_err, size_t block_size
#endif
    ) const {

  // construct a query tree out of the data tree to perform 
  // dual tree self-evaluation. we use copy-assignment since we would like
  // to preserve the same ordering of points in both trees. 
  KdtreeType query_tree = data_tree_;

#ifndef __CUDACC__
  eval(query_tree, kernel_, rel_err, abs_err);
#else
  eval(query_tree, kernel_, rel_err, abs_err, block_size);
#endif

  // compute the cross validation score
  FloatType cv = ConstantTraits<FloatType>::zero();

  FloatType cv_i;
  for (size_t i = 0; i < query_tree.points_.size(); ++i) {

    // the dual tree gives contributions from all points; must 
    // subtract away the self contribution
    cv_i = query_tree.points_[i].attributes().value();
    cv_i -= data_tree_.points_[i].attributes().mass() * kernel_.normalization();

    // the cross validation score is the log of the leave one out contribution
    cv += data_tree_.points_[i].attributes().weight() * std::log(cv_i);
  }

  return cv;
}

template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::unadapt_density() {

  // reset reference data point attributes
  for (size_t i = 0; i < data_tree_.points_.size(); ++i) {

    // set local bandwidth corrections to 1.0
    data_tree_.points_[i].attributes().set_lower_abw(
        ConstantTraits<FloatType>::one());
    data_tree_.points_[i].attributes().set_upper_abw(
        ConstantTraits<FloatType>::one());

    // set masses to equal weights
    data_tree_.points_[i].attributes().set_mass(
      data_tree_.points_[i].attributes().weight());
  }

  // update node attributes
  data_tree_.refresh_node_attributes(data_tree_.root_);
}


// Calling this method repurposes `this` KernelDensity object to become 
// an adaptive kernel density. In particular, the following attributes in
// the data_tree must be updated:
//
// + For each node, update the min/max local bandwidth corrections of 
//   points under it. 
//
// + Update masses for each point and each node. Node masses are 
//   induced from point masses, while point masses are computed by 
//   scaling. e.g. if the `i`th point has weight `w_i` and 
//   local bandwidth correction `abw_i`, then set the mass to 
//   `w_i / pow(abw_i,D)`. 
//
// This prescription is described in page 101 of Silverman's book
// `Density Estimation for Statistics and Data Analysis`. 
template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::adapt_density(
#ifndef __CUDACC__
    FloatType alpha, FloatType rel_err, FloatType abs_err
#else
    FloatType alpha, FloatType rel_err, FloatType abs_err,
    size_t block_size
#endif
    ) {

  if (alpha < 0 || alpha > 1) { 
    throw std::invalid_argument("adapt_density: alpha must be between [0, 1]. ");
  }

  // must first reset to non-adaptive densities before computing 
  // the pilot estimate. 
  unadapt_density();

  // already done if `alpha` is 0. Checking for exact equality, since
  // it is not impossible that a user requests only a slight adaptation. 
  if (alpha == 0) { return; }

  // compute pilot estimate
  // ----------------------

  // construct a query tree out of the data tree to perform 
  // dual tree self-evaluation. we use copy-assignment since we would like
  // to preserve the same ordering of points in both trees. 
  KdtreeType query_tree = data_tree_;

#ifndef __CUDACC__
  eval(query_tree, kernel_, rel_err, abs_err);
#else
  eval(query_tree, kernel_, rel_err, abs_err, block_size);
#endif

  // compute local bandwidth corrections
  // -----------------------------------

  FloatType g = 0;
  std::vector<FloatType> local_bw(query_tree.points_.size());
  for (size_t i = 0; i < query_tree.points_.size(); ++i) {
    local_bw[i] = query_tree.points_[i].attributes().value();
    g += data_tree_.points_[i].attributes().weight() * std::log(local_bw[i]);
  }
  g = std::exp(g);

  for (auto &bw : local_bw) {
    bw = std::pow(bw/g, -alpha);
  }

  // update data tree attributes
  // ---------------------------
  for (size_t i = 0; i < data_tree_.points_.size(); ++i) {

    // local bandwidth corrections
    data_tree_.points_[i].attributes().set_lower_abw(local_bw[i]);
    data_tree_.points_[i].attributes().set_upper_abw(local_bw[i]);

    // scale masses
    data_tree_.points_[i].attributes().set_mass(
      data_tree_.points_[i].attributes().weight() * pow(local_bw[i], -D));
  }

  // update node attributes
  data_tree_.refresh_node_attributes(data_tree_.root_);

  return;
}

template<int D, typename KT, typename FT, typename AT>
inline size_t KernelDensity<D,KT,FT,AT>::size() const { return data_tree_.size(); }

template<int D, typename KT, typename FT, typename AT>
inline const std::vector<typename KernelDensity<D,KT,FT,AT>::DataPointType>& 
KernelDensity<D,KT,FT,AT>::points() const {
  return data_tree_.points();
}

template<int D, typename KT, typename FT, typename AT>
inline const typename KernelDensity<D,KT,FT,AT>::KdtreeType&
KernelDensity<D,KT,FT,AT>::data_tree() const {
  return data_tree_;
}

template<int D, typename KT, typename FT, typename AT>
void swap(KernelDensity<D,KT,FT,AT> &lhs, KernelDensity<D,KT,FT,AT> &rhs) {
  using std::swap;
  swap(lhs.kernel_, rhs.kernel_);
  swap(lhs.data_tree_, rhs.data_tree_);
  return;
}

template<int D, typename KT, typename FT, typename AT>
KernelDensity<D,KT,FT,AT>::KernelDensity() : 
  kernel_(), data_tree_(), cum_weights_() {}

template<int D, typename KT, typename FT, typename AT>
KernelDensity<D,KT,FT,AT>::KernelDensity(
    const std::vector<DataPointType> &pts, int leaf_max) 
  : kernel_() {

  std::vector<DataPointType> ref_pts = pts;
  initialize_attributes(ref_pts);
  data_tree_ = KdtreeType(std::move(ref_pts), leaf_max);
  initialize_cum_weights();

}

template<int D, typename KT, typename FT, typename AT>
KernelDensity<D,KT,FT,AT>::KernelDensity(
    std::vector<DataPointType> &&pts, int leaf_max) 
  : kernel_() {

  initialize_attributes(pts);
  data_tree_ = KdtreeType(std::move(pts), leaf_max);
  initialize_cum_weights();

}

template<int D, typename KT, typename FT, typename AT>
inline const typename KernelDensity<D,KT,FT,AT>::KernelType& 
KernelDensity<D,KT,FT,AT>::kernel() const {
  return kernel_;
}

template<int D, typename KT, typename FT, typename AT>
inline typename KernelDensity<D,KT,FT,AT>::KernelType& 
KernelDensity<D,KT,FT,AT>::kernel() {
  return const_cast<KernelType&>(
           static_cast<const KernelDensity<D,KT,FT,AT>&>(*this).kernel()
      );
}

template<int D, typename KT, typename FT, typename AT>
inline void KernelDensity<D,KT,FT,AT>::set_kernel(const KernelType &k) {
  kernel_ = k;
}

template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::initialize_attributes(
    std::vector<DataPointType> &pts) {

  // normalize point weights
  normalize_weights(pts);

  // set masses
  for (auto &p : pts) { 
    p.attributes().set_mass(
        p.attributes().weight() * pow(p.attributes().abw(), -D));
  }
}

template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::normalize_weights(std::vector<DataPointType> &pts) {

  FloatType weight_total = ConstantTraits<FloatType>::zero();
  for (const auto &p : pts) { weight_total += p.attributes().weight(); }
  for (auto &p : pts) { 
    p.attributes().set_weight(
        p.attributes().weight() / weight_total
    );
  }

}

// note: points weights in the data tree should have already been 
// normalized; i.e. sum over all weights is 1.0
template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::initialize_cum_weights() {

  // start with a clean slate 
  cum_weights_.clear(); cum_weights_.reserve(data_tree_.size());

  // cum_weights_[i] contains the sum of weights up to and including 
  // the weight at point i. 
  FloatType cum_sum = 0.0;
  for (const auto &p : data_tree_.points()) {
    cum_sum += p.attributes().weight();
    cum_weights_.push_back(cum_sum);
  }

  // assign roundoff errors to the last element... for lack 
  // of a better idea
  if (data_tree_.size()) { cum_weights_[data_tree_.size()-1] = 1.0; }
}

template<int D, typename KT, typename FT, typename AT>
KernelDensity<D,KT,FT,AT>::~KernelDensity() {}



// user wrapper for single tree kde evaluation. 
// computes with the default kernel. 
template<int D, typename KT, typename FT, typename AT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::eval(DataPointType &p, 
    FloatType rel_err, FloatType abs_err) const {

  FloatType result = eval(p.point(), kernel_, rel_err, abs_err);

  p.attributes().set_upper(result); 
  p.attributes().set_lower(result);

  return result;

}

// single point kde evaluation. based on the following algorithms:
// + ''Multiresolution Instance-Based Learning'' by Deng and Moore
// + ''Nonparametric Density Estimation: Toward Computational Tractability'' 
//   by Gray and Moore
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::eval(
    const GeomPointType &p, 
    const KernT &kernel,
    FloatType rel_err, FloatType abs_err) const {

  // each reference point `d` contributes some proportion of its mass 
  // towards the kde at point `p`. 
  // Note: we factor out the overall normalization during the tree traversal 
  //       and normalize later. 
  //
  // initialization: 
  // + upper: upper bound on the kde value. initially, take all of the mass. 
  // + lower: lower bound on the kde value. initially, take none of the mass. 
  // + du: the upper bound on the proportion of mass each point contributes.
  // + dl: the lower bound on the proportion of mass each point contributes. 
  FloatType upper = data_tree_.root_->attr_.mass();
  FloatType lower = ConstantTraits<FloatType>::zero();
  FloatType du = 1.0, dl = 0.0;

  // tighten the bounds by the single_tree algorithm. since we include the
  // overall normalization afterwards, we need to scale abs_err accordingly
  FloatType normalization = kernel.normalization();
  single_tree(data_tree_.root_, p, kernel,
              upper, lower, du, dl, 
              rel_err, abs_err / normalization);

  // take the mean of the bounds and remember to include the normalization
  FloatType result = normalization * (lower + (upper - lower) / 2);

  // error reporting: notify the user of any loss of precision
  report_error(std::cerr, p, 
               normalization*upper, 
               normalization*lower, 
               rel_err, abs_err);

  return result;

}


template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::single_tree(
    const TreeNodeType *D_node, const GeomPointType &p, const KernT &kernel,
    FloatType &upper, FloatType &lower, 
    FloatType du, FloatType dl, 
    FloatType rel_err, FloatType abs_err) const {

  // update the kernel contributions due to points in `D_node` 
  // towards the upper/lower bounds on the kde value at point `p`. 
  FloatType du_new, dl_new; 
  estimate_contributions(D_node, p, kernel, du_new, dl_new);

  // bound: approximate the total contribution due to `D_node` and 
  // decide whether to prune. 
  if (can_approximate(D_node, du_new, dl_new, du, dl, 
                      upper, lower, rel_err, abs_err)) { 

    // prune: still need to tighten the lower/upper bounds
    tighten_bounds(D_node, du_new, dl_new, du, dl, upper, lower);

    return; 
  }

  // branch: case 1: reached a leaf. brute force computation. 
  if (D_node->is_leaf()) {

    single_tree_base(D_node, p, kernel, du, dl, upper, lower);

  // branch: case 2: non-leaf. recursively tighten the bounds. 
  } else {

    // tighten the bounds for faster convergence
    tighten_bounds(D_node, du_new, dl_new, du, dl, upper, lower);

    // decide which halfspace is closer to the query
    TreeNodeType *closer = D_node->left_, *further = D_node->right_;
    apply_closer_heuristic(&closer, &further, p);
    
    // recursively tighten the bounds, closer halfspace first 
    single_tree(closer, p, kernel, upper, lower, du_new, dl_new, rel_err, abs_err);
    single_tree(further, p, kernel, upper, lower, du_new, dl_new, rel_err, abs_err);

  }
}

// input invariants:
// + lower <= upper, dl <= du
//
// output invariants:
// + lower <= upper
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::single_tree_base(
    const TreeNodeType *D_node, const GeomPointType &p, const KernT &kernel,
    FloatType du, FloatType dl, 
    FloatType &upper, FloatType &lower) const {

  FloatType delta;
  for (auto i = D_node->start_idx_; i <= D_node->end_idx_; ++i) {

    delta = kernel.unnormalized_eval(p, data_tree_.points_[i].point(), 
                                        data_tree_.points_[i].attributes().abw());

    delta *= data_tree_.points_[i].attributes().mass();
    upper += delta; lower += delta;
  }
  upper -= D_node->attr_.mass() * du; 
  lower -= D_node->attr_.mass() * dl;

  // see comment in tighten_bounds. 
  if (lower > upper) { upper = lower; }

}


// user wrapper for tree multi-point kernel density evaluation.
// computes with the default kernel. 
template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::eval(

#ifndef __CUDACC__
    std::vector<DataPointType> &queries, 
    FloatType rel_err, FloatType abs_err, 
    int leaf_nmax
#else
    std::vector<DataPointType> &queries, 
    FloatType rel_err, FloatType abs_err, 
    int leaf_nmax, 
    size_t block_size
#endif
    
    ) const {


  // construct a query tree
  KdtreeType query_tree(std::move(queries), leaf_nmax);

#ifndef __CUDACC__
  eval(query_tree, kernel_, rel_err, abs_err);
#else
  eval(query_tree, kernel_, rel_err, abs_err, block_size);
#endif

  // move the results back
  queries = std::move(query_tree.points_);

}

// user wrapper for tree multi-point kernel density evaluation.
// computes with the default kernel. 
template<int D, typename KT, typename FT, typename AT>
inline void KernelDensity<D,KT,FT,AT>::eval(

#ifndef __CUDACC__
    KdtreeType &query_tree, 
    FloatType rel_err, FloatType abs_err
#else
    KdtreeType &query_tree,
    FloatType rel_err, FloatType abs_err, 
    size_t block_size
#endif
    
    ) const {

#ifndef __CUDACC__
  eval(query_tree, kernel_, rel_err, abs_err);
#else
  eval(query_tree, kernel_, rel_err, abs_err, block_size);
#endif

}


// tree multi-point kde evaluation. computes with arbitrary kernels.
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::eval(

#ifndef __CUDACC__
    KdtreeType &query_tree, const KernT &kernel,
    FloatType rel_err, FloatType abs_err
#else
    KdtreeType &query_tree, const KernT &kernel,
    FloatType rel_err, FloatType abs_err, 
    size_t block_size
#endif
    
    ) const {

  // initialize upper/lower bounds of individual queries to be
  // such that all data points contribute maximally/minimally
  for (auto &q : query_tree.points_) { 
    q.attributes().set_lower(0);
    q.attributes().set_upper(data_tree_.root_->attr_.mass());
  }
  query_tree.refresh_node_attributes(query_tree.root_);

  FloatType du = 1.0, dl = 0.0;


  // dual tree algorithm
  FloatType normalization = kernel.normalization(); 

#ifndef __CUDACC__
  dual_tree(data_tree_.root_, query_tree.root_, kernel,
            du, dl, rel_err, abs_err/normalization, query_tree);
#else
  CudaDirectKde<D,KernelFloatType,KernT> 
    cu_kde(data_tree_.points(), query_tree.points());
  cu_kde.kernel() = kernel;

  std::vector<KernelFloatType> host_result_cache(query_tree.size());

  dual_tree(data_tree_.root_, query_tree.root_, kernel,
            du, dl, rel_err, abs_err/normalization, query_tree,
            cu_kde, host_result_cache, block_size);
#endif

  // remember to normalize
  for (auto &q : query_tree.points_) { 

    q.attributes().set_lower(q.attributes().lower()*normalization);
    q.attributes().set_upper(q.attributes().upper()*normalization);

    report_error(std::cerr, q.point(), 
                 q.attributes().upper(), q.attributes().lower(), 
                 rel_err, abs_err);
  }

  return;
}


// tighten the contribution from all points in D_node to the upper/lower
// bounds of Q_node as well as each individual queries in Q_node.
// `du`, `dl` are upper and lower bounds on the proportion of mass 
// contributions of every point in D_node to points in Q_node. 
//
// the lower/upper bounds of Q_node is the min/max of all lower/upper 
// bounds of the individual queries 
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::dual_tree(

#ifndef __CUDACC__
    const TreeNodeType *D_node, TreeNodeType *Q_node, const KernT &kernel,
    FloatType du, FloatType dl, FloatType rel_err, FloatType abs_err,
    KdtreeType &query_tree
#else
    const TreeNodeType *D_node, TreeNodeType *Q_node, const KernT &kernel,
    FloatType du, FloatType dl, FloatType rel_err, FloatType abs_err,
    KdtreeType &query_tree,
    CudaDirectKde<D,KernelFloatType,KernT> &cu_kde,
    std::vector<KernelFloatType> &host_result_cache,
    size_t block_size
#endif
    
    ) const {

  // update the kernel contributions due to D_node
  FloatType du_new, dl_new;
  estimate_contributions(D_node, Q_node->bbox_, kernel, du_new, dl_new);

  // BOUND: decide whether the approximation satsifies the error guarantees
  if (can_approximate(D_node, Q_node, 
        du_new, dl_new, du, dl, rel_err, abs_err)) {

    // tighten the lower/upper bound of Q_node itself
    tighten_bounds(D_node, Q_node, du_new, dl_new, du, dl);

    // tighten the individual queries
    FloatType upper_q, lower_q;
    for (auto i = Q_node->start_idx_; i <= Q_node->end_idx_; ++i) {

      upper_q = query_tree.points_[i].attributes().upper();
      lower_q = query_tree.points_[i].attributes().lower();

      // du/dl are set to 1.0/0.0 because they were never 
      // updated since initialization
      tighten_bounds(D_node, du_new, dl_new, 1.0, 0.0, upper_q, lower_q);

      query_tree.points_[i].attributes().set_upper(upper_q);
      query_tree.points_[i].attributes().set_lower(lower_q);
    }

    return;
  }

  // BRANCH: any node pair that reaches this point requires expansion 
  // to further tighten their contributions

  // base case: Q and D both leaves; brute force
  if (Q_node->is_leaf() && D_node->is_leaf()) {

#ifndef __CUDACC__
    dual_tree_base(D_node, Q_node, kernel, du, dl, query_tree);
#else
    dual_tree_base(D_node, Q_node, kernel, du, dl, query_tree, 
                   cu_kde, host_result_cache, block_size);
#endif
    
  } else {

    // recursive cases below. 

    // case 1: Q is a leaf. tighten recursively with D_node's daughters.
    if (Q_node->is_leaf()) {

      // tighten Q_node bounds for faster convergence. 
      // this is just an optimization. 
      tighten_bounds(D_node, Q_node, du_new, dl_new, du, dl);

      // closer heuristic
      TreeNodeType *closer = D_node->left_, *further = D_node->right_;
      apply_closer_heuristic(&closer, &further, Q_node->bbox_);

#ifndef __CUDACC__
      dual_tree(closer, Q_node, kernel, 
          du_new, dl_new, rel_err, abs_err, query_tree);
      dual_tree(further, Q_node, kernel, 
          du_new, dl_new, rel_err, abs_err, query_tree);
#else
      dual_tree(closer, Q_node, kernel,
          du_new, dl_new, rel_err, abs_err, query_tree,
          cu_kde, host_result_cache, block_size);
      dual_tree(further, Q_node, kernel,
          du_new, dl_new, rel_err, abs_err, query_tree,
          cu_kde, host_result_cache, block_size);
#endif

    } else {

      // in the cases below, proceed in two steps:
      //
      // + recursively tighten the contributions of D_node's daughters to 
      //   Q_node's daughters. 
      //
      // + obtain Q_node's bounds by taking the min/max daughter bounds. 

      // tighten bounds for faster convergence. this is just an optimization; 
      // one still needs to combine after recursion finishes.
      tighten_bounds(D_node, Q_node->left_, du_new, dl_new, du, dl);
      tighten_bounds(D_node, Q_node->right_, du_new, dl_new, du, dl);

      // case 2: D is a leaf
      if (D_node->is_leaf()) {

#ifndef __CUDACC__
        dual_tree(D_node, Q_node->left_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
        dual_tree(D_node, Q_node->right_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
#else 
        dual_tree(D_node, Q_node->left_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
        dual_tree(D_node, Q_node->right_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
#endif

      // case 3: neither Q nor D are leaves
      } else {

        // tighten Q->left
        TreeNodeType *closer = D_node->left_, *further = D_node->right_;
        apply_closer_heuristic(&closer, &further, Q_node->left_->bbox_);

#ifndef __CUDACC__
        dual_tree(closer, Q_node->left_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
        dual_tree(further, Q_node->left_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
#else
        dual_tree(closer, Q_node->left_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
        dual_tree(further, Q_node->left_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
#endif

        // tighten Q->right
        closer = D_node->left_; further = D_node->right_;
        apply_closer_heuristic(&closer, &further, Q_node->right_->bbox_);

#ifndef __CUDACC__
        dual_tree(closer, Q_node->right_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
        dual_tree(further, Q_node->right_, kernel, 
            du_new, dl_new, rel_err, abs_err, query_tree);
#else
        dual_tree(closer, Q_node->right_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
        dual_tree(further, Q_node->right_, kernel,
            du_new, dl_new, rel_err, abs_err, query_tree,
            cu_kde, host_result_cache, block_size);
#endif

      }

      // combine the daughters' bounds to update Q_node's bounds
      Q_node->attr_.set_lower(
          std::min(Q_node->left_->attr_.lower(), 
                   Q_node->right_->attr_.lower()));
      Q_node->attr_.set_upper(
          std::max(Q_node->left_->attr_.upper(), 
                   Q_node->right_->attr_.upper()));
    }
  }
}


template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::dual_tree_base(
#ifndef __CUDACC__
    const TreeNodeType *D_node, TreeNodeType *Q_node, const KernT &kernel,
    FloatType du, FloatType dl, 
    KdtreeType &query_tree
#else
    const TreeNodeType *D_node, TreeNodeType *Q_node, const KernT &kernel,
    FloatType du, FloatType dl, 
    KdtreeType &query_tree,
    CudaDirectKde<D,KernelFloatType,KernT> &cu_kde,
    std::vector<KernelFloatType> &host_result_cache,
    size_t block_size
#endif
    ) const {

#ifdef __CUDACC__
  cu_kde.unnormalized_eval(
      D_node->start_idx_, D_node->end_idx_, 
      Q_node->start_idx_, Q_node->end_idx_, 
      host_result_cache, block_size);
#endif

  FloatType min_q = std::numeric_limits<FloatType>::max();
  FloatType max_q = std::numeric_limits<FloatType>::min();

  FloatType lower_q, upper_q;
  for (auto i = Q_node->start_idx_; i <= Q_node->end_idx_; ++i) {

    // update the contribution of each point due to D_node
    upper_q = query_tree.points_[i].attributes().upper();
    lower_q = query_tree.points_[i].attributes().lower();

#ifndef __CUDACC__

    single_tree_base(
        D_node, query_tree.points_[i].point(), kernel,
        1.0, 0.0, upper_q, lower_q);

#else

    upper_q += host_result_cache[i-(Q_node->start_idx_)]; 
    upper_q -= D_node->attr_.mass();

    lower_q += host_result_cache[i-(Q_node->start_idx_)]; 

    if (lower_q > upper_q) { upper_q = lower_q; }

#endif

    query_tree.points_[i].attributes().set_lower(lower_q);
    query_tree.points_[i].attributes().set_upper(upper_q);

    min_q = std::min(lower_q, min_q);
    max_q = std::max(upper_q, max_q);

  }

  Q_node->attr_.set_lower(min_q);
  Q_node->attr_.set_upper(max_q);

}


template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::tighten_bounds(
    const TreeNodeType *D_node, TreeNodeType *Q_node,
    FloatType du_new, FloatType dl_new, 
    FloatType du, FloatType dl) const {

  FloatType upper = Q_node->attr_.upper();
  FloatType lower = Q_node->attr_.lower();

  tighten_bounds(D_node, du_new, dl_new, du, dl, upper, lower);

  Q_node->attr_.set_upper(upper);
  Q_node->attr_.set_lower(lower);
}


// input invariants:
// + lower <= upper, dl <= du, dl_new <= du_new
// + dl <= dl_new, du >= du_new
//
// output invariants:
// + lower <= upper
template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::tighten_bounds(
    const TreeNodeType *D_node,
    FloatType du_new, FloatType dl_new, 
    FloatType du, FloatType dl, 
    FloatType &upper, FloatType &lower) const {

  // add the new contributions, but remember to subtract away the old ones
  lower += D_node->attr_.mass() * (dl_new - dl);
  upper += D_node->attr_.mass() * (du_new - du); 

  // the input invariants guarantee, mathematically, that lower <= upper.
  // however, roundoff error (approx. cancellation) can break this gurantee.
  //
  // to enforce the output invariant, we explicitly set lower = upper 
  // if the cancellation overshoots. 
  if (lower > upper) { upper = lower; } 
}



template<int D, typename KT, typename FT, typename AT>
inline bool KernelDensity<D,KT,FT,AT>::can_approximate(
    const TreeNodeType *D_node, const TreeNodeType *Q_node,
    FloatType du_new, FloatType dl_new, 
    FloatType du, FloatType dl, 
    FloatType rel_err, FloatType abs_err) const {

  // safe to approximate only if all points can be approximated
  return can_approximate(D_node, du_new, dl_new, du, dl, 
                         Q_node->attr_.upper(), Q_node->attr_.lower(),
                         rel_err, abs_err);
}


// decide whether the current updates allow a prune
//
// + For the condition that gurantees the absolute errors, see 
//   Section 5. of Deng and Moore
//
// + For the condition that gurantees the relative errors, see 
//   Section 4.3. of Gray and Moore
template<int D, typename KT, typename FT, typename AT>
bool KernelDensity<D,KT,FT,AT>::can_approximate(
    const TreeNodeType *D_node,
    FloatType du_new, FloatType dl_new, 
    FloatType du, FloatType dl, 
    FloatType upper, FloatType lower, 
    FloatType rel_err, FloatType abs_err) const {

  FloatType abs_tol = 2 * abs_err / data_tree_.size();

  // exclusion pruning guaranteeing that the absolute error <= abs_err
  if (std::abs(du_new) <= abs_tol) { return true; }

  // approximation pruning
  // condition 1: guarantee absolute error <= abs_err
  // condition 2: guarantee relative error <= rel_err
  if (std::abs(du_new - dl_new) <= abs_tol) { return true; }

  tighten_bounds(D_node, du_new, dl_new, du, dl, upper, lower);

  if (std::abs(upper-lower) <= abs_err || 
      std::abs(upper-lower) <= std::abs(lower)*rel_err) { return true; }

  return false;

}




template<int D, typename KT, typename FT, typename AT>
  template<typename ObjT>
inline void KernelDensity<D,KT,FT,AT>::apply_closer_heuristic(
    TreeNodeType **closer, TreeNodeType **further, const ObjT &obj) const {

  if ((*closer)->bbox_.min_dist(obj) > (*further)->bbox_.min_dist(obj)) {
    std::swap(*closer, *further);
  }

}


template<int D, typename KT, typename FT, typename AT>
  template<typename ObjT, typename KernT> 
void KernelDensity<D,KT,FT,AT>::estimate_contributions(
    const TreeNodeType *D_node, const ObjT &obj, const KernT &kernel,
    FloatType &du, FloatType &dl) const {

  GeomPointType proxy;
  const static GeomPointType origin;

  // use the minimum(maximum) distance to the argument in each 
  // dimension to bound the min/max kernel contributions

  for (int i = 0; i < D; ++i) { proxy[i] = D_node->bbox_.min_dist(i, obj); }
  du = kernel.unnormalized_eval(proxy, origin, D_node->attr_.upper_abw());

  for (int i = 0; i < D; ++i) { proxy[i] = D_node->bbox_.max_dist(i, obj); }
  dl = kernel.unnormalized_eval(proxy, origin, D_node->attr_.lower_abw());

}


template<int D, typename KT, typename FT, typename AT>
void KernelDensity<D,KT,FT,AT>::report_error(
    std::ostream &os, const GeomPointType &p,
    FloatType upper, FloatType lower, 
    FloatType rel_err, FloatType abs_err) const {

  if (std::abs(upper - lower) > abs_err) {
    if (lower) {
      if (std::abs((upper-lower)/lower) > rel_err) {
        os << std::setprecision(6);
        os << "Relative loss when querying " << p << ": " << std::endl;
        os << std::setprecision(15);
        os << "\tlower:   " << lower << std::endl;
        os << "\tupper:   " << upper << std::endl;
        os << "\tabs_err: " << std::abs(upper - lower) << " (c.f. " << abs_err << ")" << std::endl;
        os << "\trel_err: " << std::abs(upper - lower) / lower << " (c.f. " << rel_err << ")" << std::endl;
        os << std::endl;
      }
    } else {
      os << std::setprecision(6);
      os << "Absolute precision loss when querying " << p << ": " << std::endl;
      os << std::setprecision(15);
      os << "\tlower:   " << lower << std::endl;
      os << "\tupper:   " << upper << std::endl;
      os << "\tabs_err: " << std::abs(upper - lower) << " (c.f. " << abs_err << ")" << std::endl;
      os << "\trel_err: --- " << std::endl;
      os << std::endl;
    }
  }

}

// user wrapper for direct kernel density evaluation.
// computes with the default kernel. 
template<int D, typename KT, typename FT, typename AT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::direct_eval(DataPointType &p) const {
  FloatType result = direct_eval(p.point(), kernel_);
  p.attributes().set_upper(result);
  p.attributes().set_lower(result);
  return result;
}

// direct kernel density evaluation. computes using arbitrary kernels. 
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
typename KernelDensity<D,KT,FT,AT>::FloatType
KernelDensity<D,KT,FT,AT>::direct_eval(
    const GeomPointType &p, const KernT &kernel) const {

  FloatType total = ConstantTraits<FloatType>::zero();
  for (const auto &datum : data_tree_.points()) {
    total += 
      datum.attributes().mass() * 
      kernel.unnormalized_eval(p, datum.point(), 
                                  datum.attributes().abw() );
  }
  total *= kernel.normalization();
  return total;

}


// user wrapper for direct kernel density evaluation.
#ifndef __CUDACC__
template<int D, typename KT, typename FT, typename AT>
inline void KernelDensity<D,KT,FT,AT>::direct_eval(
    std::vector<DataPointType> &queries) const {
  direct_eval(queries, kernel_);
  return; 
}
#else 
template<int D, typename KT, typename FT, typename AT>
inline void KernelDensity<D,KT,FT,AT>::direct_eval(
    std::vector<DataPointType> &queries, size_t block_size
    ) const {
  direct_eval(queries, kernel_, block_size);
  return; 
}
#endif



// direct kernel density evaluation. computes using arbitrary kernels. 
template<int D, typename KT, typename FT, typename AT>
  template<typename KernT>
void KernelDensity<D,KT,FT,AT>::direct_eval(

#ifndef __CUDACC__
    std::vector<DataPointType> &queries, const KernT &kernel
#else
    std::vector<DataPointType> &queries, const KernT &kernel, size_t block_size
#endif

    ) const {

#ifndef __CUDACC__
  for (auto &q : queries) {
    FloatType result = direct_eval(q.point(), kernel);
    q.attributes().set_lower(result);
    q.attributes().set_upper(result);
  }
#else
  std::vector<KernelFloatType> host_results(queries.size());

  CudaDirectKde<D,KernelFloatType,KernT> cuda_kde(data_tree_.points(), queries);
  cuda_kde.kernel() = kernel;

  cuda_kde.eval(0, data_tree_.size()-1, 0, queries.size()-1, 
                host_results, block_size);

  for (size_t i = 0; i < queries.size(); ++i) {
    queries[i].attributes().set_lower(host_results[i]);
    queries[i].attributes().set_upper(host_results[i]);
  }
#endif

  return; 
}

}

