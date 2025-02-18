/*
 * Copyright (C) 2010-2022 The ESPResSo project
 *
 * This file is part of ESPResSo.
 *
 * ESPResSo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ESPResSo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CORE_ALGORITHM_PERIODIC_FOLD_HPP
#define CORE_ALGORITHM_PERIODIC_FOLD_HPP

#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

// Define a concept that checks if a type T is an integer or a reference to an
// integer
template <typename T>
concept IntegralOrRef = std::integral<std::remove_reference_t<T>>;
template <typename T>
concept FloatingPointOrRef = std::floating_point<std::remove_reference_t<T>>;

namespace Algorithm {
/**
 * @brief Fold value into primary interval.
 *
 * @param x Value to fold
 * @param i Image count before folding
 * @param l Length of primary interval
 * @return x folded into [0, l) and number of folds.
 */
template <FloatingPointOrRef T, IntegralOrRef I>
std::pair<T, I> periodic_fold(T x, I i, T const l) {
  using limits = std::numeric_limits<I>;

  while ((x < T{0}) && (i > limits::min())) {
    x += l;
    --i;
  }

  while ((x >= l) && (i < limits::max())) {
    x -= l;
    ++i;
  }

  return {x, i};
}

/**
 * @brief Fold value into primary interval.
 *
 * @param x Value to fold
 * @param l Length of primary interval
 * @return x folded into [0, l).
 */
template <FloatingPointOrRef T> T periodic_fold(T x, T const l) {
#ifndef __FAST_MATH__
  /* Can't fold if either x or l is nan or inf. */
  if (std::isnan(x) or std::isnan(l) or std::isinf(x) or (l == 0)) {
    return std::nan("");
  }
  if (std::isinf(l)) {
    return x;
  }
#endif // __FAST_MATH__

  while (x < 0) {
    x += l;
  }

  while (x >= l) {
    x -= l;
  }

  return x;
}
} // namespace Algorithm

#endif
