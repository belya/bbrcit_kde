#include <iostream>
#include <fstream>
#include <vector>
#include <utility>
#include <random>

#include <Rectangle.h>
#include <Point.h>
#include <Kdtree.h>

using namespace std;
using bbrcit::Rectangle;
using bbrcit::Point;
using bbrcit::Kdtree;

int main() {

  using Kdtree2d = Kdtree<2>;
  using DataPointType = typename Kdtree2d::DataPointType;
  using RectangleType = typename Kdtree2d::RectangleType;

  random_device rd;
  default_random_engine e(rd());
  normal_distribution<> g(0.0, 1.0);
  uniform_real_distribution<> u(0, 1);

  vector<DataPointType> data;
  for (int i = 0; i < 10000; ++i) {
    double x = g(e), y = g(e);
    if (u(e) < 0.2) {
      x *= 0.5; y *= 0.3;
      x += 2.0; y += 2.0;
    } else {
      x *= 2.0; y *= 1.0;
      x = 0.8660254 * x + 0.5 * y;
      y = -0.5 * x + 0.8660254 * y;
    }
    data.push_back({{x, y}, 1});
  }

  ofstream fout1("kdtree3_leaves.out");
  ofstream fout2("kdtree3_results.out");
  ofstream fout3("kdtree3_query_rectangle.out");
  ofstream fout4("kdtree3_partitions.out");

  Kdtree2d tree(data);
  tree.print_points(fout1);
  RectangleType query({0.5,0.5}, {1.2,1.2});
  tree.print_range_search(query, fout2);
  fout3 << query << endl;

  tree.print_partitions(9, fout4);


  return 0;
}
