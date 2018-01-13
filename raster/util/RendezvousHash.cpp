/*
 * Copyright (c) 2015, Facebook, Inc.
 * Copyright 2017 Yeolar
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "raster/util/RendezvousHash.h"

#include <algorithm>
#include <limits>
#include <map>
#include <vector>
#include <math.h>

#include "raster/util/Hash.h"

namespace rdd {

void RendezvousHash::build(
    std::vector<std::pair<std::string, uint64_t>>& nodes) {
  for (auto& p : nodes) {
    std::string key = p.first;
    uint64_t weight = p.second;
    weights_.emplace_back(computeHash(key.c_str(), key.size()), weight);
  }
}

/*
 * The algorithm of RendezvousHash goes like this:
 * Assuming we have 3 clusters with names and weights:
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 * | n="ash4c07"  | n="frc1c12"  | n="prn1c11"  |
 * | w=100        | w=400        | w=500        |
 * ==============================================
 * To prepare, we calculate a hash for each cluster based on its name:
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 * | n="ash4c07"  | n="frc1c12"  | n="prn1c11"  |
 * | w = 100      | w=400        | w=500        |
 * | h = hash(n)  | h = hash(n)  | h = hash(n)  |
 * ==============================================
 *
 * When a key comes, we have to decide which cluster we want to assign it to:
 * E.g., k = 10240
 *
 * For each cluster, we calculate a combined hash with the sum of key
 * and the cluster's hash:
 *
 *
 *                             ==============================================
 *                             | Cluster1     | Cluster2     | Cluster3     |
 *                                               ...
 *        k                    | h=hash(n)    | h = hash(n)  | h=hash(n)    |
 *        |                    ==============================================
 *        |                                          |
 *        +-------------+----------------------------+
 *                      |
 *                  ch=hash(h + k)
 *                      |
 *                      v
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 * | n="ash4c07"  | n="frc1c12"  | n="prn1c11"  |
 * | w=100        | w=400        | w=500        |
 * | h=hash(n)    | h = hash(n)  | h=hash(n)    |
 * |ch=hash(h+k)  |ch = hash(h+k)|ch=hash(h+k)  |
 * ==============================================
 *
 * ch is now a random variable from 0 to max_int that follows
 * uniform distribution,
 * we need to scale it to a r.v. * from 0 to 1 by dividing it with max_int:
 *
 * scaledHash = ch / max_int
 *
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 *                    ....
 * |ch=hash(h+k)  |ch = hash(h+k)|ch=hash(h+k)  |
 * ==============================================
 *                      |
 *                    sh=ch/max_int
 *                      |
 *                      v
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 *                    ....
 * |ch=hash(h+k)  |ch = hash(h+k)|ch=hash(h+k)  |
 * |sh=ch/max_int |sh=ch/max_int |sh=ch/max_int |
 * ==============================================
 *
 * We also need to respect the weights, we have to scale it again with
 * a function of its weight:
 *
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 *                    ....
 * |sh=ch/max_int |sh=ch/max_int |sh=ch/max_int |
 * ==============================================
 *                      |
 *                      |
 *               sw = pow(sh, 1/w)
 *                      |
 *                      V
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 *                    ....
 * |sh=ch/max_int |sh=ch/max_int |sh=ch/max_int |
 * |sw=pow(sh,1/w)|sw=pow(sh,1/w)|sw=pow(sh,1/w)|
 * ==============================================
 *
 * We now calculate who has the largest sw, that is the cluster that we are
 * going to map k into:
 * ==============================================
 * | Cluster1     | Cluster2     | Cluster3     |
 *                    ....
 * |sw=pow(sh,1/w)|sw=pow(sh,1/w)|sw=pow(sh,1/w)|
 * ==============================================
 *                      |
 *                     max(sw)
 *                      |
 *                      V
 *                   Cluster
 *
 */
size_t RendezvousHash::get(const uint64_t key, const size_t rank) const {
  size_t modRank = rank % weights_.size();

  std::vector<std::pair<double, size_t>> scaledWeights;
  size_t i = 0;
  for (auto& entry : weights_) {
    // combine the hash with the cluster together
    double combinedHash = computeHash(entry.first + key);
    double scaledHash = (double)combinedHash /
      std::numeric_limits<uint64_t>::max();
    double scaledWeight = 0;
    if (entry.second != 0) {
      scaledWeight = pow(scaledHash, (double)1/entry.second);
    }
    scaledWeights.emplace_back(scaledWeight, i++);
  }
  std::nth_element(scaledWeights.begin(), scaledWeights.begin()+modRank,
                   scaledWeights.end(),
                   std::greater<std::pair<double, size_t>>());
  return scaledWeights[modRank].second;
}

uint64_t RendezvousHash::computeHash(const char* data, size_t len) const {
  return rdd::hash::fnv64_buf(data, len);
}

uint64_t RendezvousHash::computeHash(uint64_t i) const {
  return rdd::hash::twang_mix64(i);
}

double RendezvousHash::getMaxErrorRate() const {
  return 0;
}

} // namespace rdd
