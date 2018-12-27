#define _USE_MATH_DEFINES

#include <iostream>
#include <fstream>
#include <cmath>
#include <random>
#include <chrono>
#include <algorithm>
#include <numeric>

#include <Kernels/EpanechnikovKernel.h>
#include <Kernels/GaussianKernel.h>
#include <KernelDensity.h>

#include "kde_test_utils.h"

using namespace std;

namespace {
  using FloatType = double;
  using KFloatType = float;
  //using KernelType = bbrcit::EpanechnikovKernel<1, KFloatType>;
  using KernelType = bbrcit::GaussianKernel<1, KFloatType>;
  using KernelDensityType = bbrcit::KernelDensity<1, KernelType, FloatType>;
  using DataPointType = typename KernelDensityType::DataPointType;
}

int main() {

#ifdef __CUDACC__
  cudaDeviceSynchronize();
#endif

  ofstream fout;
  ifstream fin;

  std::chrono::high_resolution_clock::time_point start, end;
  std::chrono::duration<double, std::milli> elapsed;

  // 1. read points from input file
  vector<DataPointType> references;
  
  fin.open("file.csv");
  string str;
  double index, value;

  start = std::chrono::high_resolution_clock::now();
  while (getline(fin, str))
  {
    istringstream sin(str);
    sin >> index >> value;
    references.push_back({{index}, {value}});
  }
  fin.close();

  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;

  cout << "+ reading " << references.size() << " reference points " << endl;
  cout << "  cpu time: " << elapsed.count() << " ms. " << std::endl;

  cout << endl;

  // 2. build the kernel density estimator
  cout << "+ building kde (kdtree construction)" << endl;

  size_t leaf_max = 1024;

  start = std::chrono::high_resolution_clock::now();
  KernelDensityType kde(references, leaf_max); 
  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;
  cout << "  cpu time: " << elapsed.count() << " ms. " << std::endl;
  
  // configure the kernel
  kde.kernel().set_bandwidth(0.1);

  cout << endl;

  // 3. direct kde evaluation
  cout << "+ direct evaluation" << endl; 

  start = std::chrono::high_resolution_clock::now();
  kde.direct_eval(references);
  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;
#ifndef __CUDACC__
  cout << "  cpu time: " << elapsed.count() << " ms. " << std::endl;
#else 
  cout << "  gpu time: " << elapsed.count() << " ms. " << std::endl;
#endif

#ifndef __CUDACC__
  fout.open("result.csv");
#else 
  fout.open("result.csv");
#endif

  write_kde1d_result(fout, references);

  fout.close();

  cout << endl;

  return 0;
}
