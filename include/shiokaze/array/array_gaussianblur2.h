/*
**	array_gaussianblur2.h
**
**	This is part of Shiokaze, a research-oriented fluid solver for computer graphics.
**	Created by Ryoichi Ando <rand@nii.ac.jp> on Feb 14, 2018.
**
**	Permission is hereby granted, free of charge, to any person obtaining a copy of
**	this software and associated documentation files (the "Software"), to deal in
**	the Software without restriction, including without limitation the rights to use,
**	copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
**	Software, and to permit persons to whom the Software is furnished to do so,
**	subject to the following conditions:
**
**	The above copyright notice and this permission notice shall be included in all copies
**	or substantial portions of the Software.
**
**	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
**	INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
**	PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
**	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
**	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
**	OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
//
#ifndef SHKZ_ARRAY_GAUSSIANBLUR2_H
#define SHKZ_ARRAY_GAUSSIANBLUR2_H
//
#include "shared_array2.h"
#include <cmath>
#include <cstdlib>
//
SHKZ_BEGIN_NAMESPACE
//
/** @file */
/// \~english @brief Namespace that performs gaussian blur.
/// \~japanese @brief ガウスブラーを計算する名前空間。
namespace array_gaussianblur2 {
	/**
	 \~english @brief Perform gaussian blur on grids.
	 @param[in] array Grid to peform gaussian blur.
	 @param[in] r Raidus in index space.
	 @param[in] mask Mask. Only non-zero on mask is modified. Can be omitted as nullptr.
	 \~japanese @brief ガウスブラーを行う。
	 @param[in] array ガウスブラーを行うグリッド。
	 @param[in] r インデックス空間での半径。
	 @param[in] mask マスク。ゼロでない部分だけを変更する。nullptr で省略可。
	 */
	template<class T> void gaussian_blur( const array2<T> &array, array2<T> &result, double r, const bitarray2 *mask=nullptr ) {
		int rs = std::ceil(r * 2.57);
		std::vector<double> exp_w(2*rs+1);
		for(int q=-rs; q<rs+1; q++) {
			exp_w[q+rs] = std::exp(-(q*q)/(2.0*r*r))/std::sqrt(M_PI*2.0*r*r);
		}
		auto valid = [&]( int i, int j ) {
			if( ! mask ) return true;
			else {
				return (*mask)(mask->shape().clamp(i,j)) != 0;
			}
		};
		shared_array2<T> save (array);
		//
		for( int dim : DIMS2 ) {
			result.parallel_all([&](int i, int j, auto &it) {
				if( valid(i,j)) {
					T val (0.0);
					double wsum (0.0);
					for(int q=-rs; q<rs+1; q++) {
						int ni = i+(dim==0)*q;
						int nj = j+(dim==1)*q;
						const double &wght = exp_w[q+rs];
						if( valid(ni,nj)) {
							T value = save()(save->shape().clamp(ni,nj));
							val += value * wght;  wsum += wght;
						}
					}
					if( wsum ) result.set(i,j, val / wsum );
					else it.set(T(0.0));
				}
			});
			save->copy(result);
		}
	}
};
//
SHKZ_END_NAMESPACE
//
#endif
//