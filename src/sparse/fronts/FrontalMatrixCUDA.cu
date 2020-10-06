/*
 * STRUMPACK -- STRUctured Matrices PACKage, Copyright (c) 2014, The
 * Regents of the University of California, through Lawrence Berkeley
 * National Laboratory (subject to receipt of any required approvals
 * from the U.S. Dept. of Energy).  All rights reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at TTD@lbl.gov.
 *
 * NOTICE. This software is owned by the U.S. Department of Energy. As
 * such, the U.S. Government has been granted for itself and others
 * acting on its behalf a paid-up, nonexclusive, irrevocable,
 * worldwide license in the Software to reproduce, prepare derivative
 * works, and perform publicly and display publicly.  Beginning five
 * (5) years after the date permission to assert copyright is obtained
 * from the U.S. Department of Energy, and subject to any subsequent
 * five (5) year renewals, the U.S. Government is granted for itself
 * and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce,
 * prepare derivative works, distribute copies to the public, perform
 * publicly and display publicly, and to permit others to do so.
 *
 * Developers: Pieter Ghysels, Francois-Henry Rouet, Xiaoye S. Li.
 *             (Lawrence Berkeley National Lab, Computational Research
 *             Division).
 *
 */
#include "FrontalMatrixGPUKernels.hpp"

#include <complex>
#include <iostream>
#include <thrust/complex.h>

// this is valid for compute capability 3.5 -> 8.0 (and beyond?)
//const unsigned int MAX_BLOCKS_X = 4294967295; // 2^32-1
const unsigned int MAX_BLOCKS_Y = 65535;
const unsigned int MAX_BLOCKS_Z = 65535;


namespace strumpack {
  namespace gpu {

    /**
     * Get the real T type corresponding to a scalar, for instance T,
     * std::complex<T> or thrust::complex<T>, to be used for instance
     * to compute norms or absolute value.
     */
    template<class T> struct real_type { typedef T value_type; };
    template<class T> struct real_type<thrust::complex<T>> { typedef T value_type; };
    template<class T> struct real_type<std::complex<T>> { typedef T value_type; };

    /**
     * The types float2 and double2 are binary the same as
     * std::complex or thrust::complex, but they can be used as
     * __shared__ variables, whereas thrust::complex cannot because it
     * doesn't have a no-argument default constructor.
     */
    template<class T> struct primitive_type { typedef T value_type; };
    template<> struct primitive_type<thrust::complex<float>> { typedef float2 value_type; };
    template<> struct primitive_type<thrust::complex<double>> { typedef double2 value_type; };
    template<> struct primitive_type<std::complex<float>> { typedef float2 value_type; };
    template<> struct primitive_type<std::complex<double>> { typedef double2 value_type; };

    /**
     * Get the corresponding thrust::complex for std::complex
     */
    template<class T> struct cuda_type { typedef T value_type; };
    template<class T> struct cuda_type<std::complex<T>> { typedef thrust::complex<T> value_type; };


    /**
     * Put elements of the sparse matrix in the F11 part of the front.
     * The sparse elements are taken from F.e11, which is a list of
     * triplets {r,c,v}. The front is assumed to be initialized to
     * zero.
     *
     * Use this with a 1-dimensional grid, where the number of grid
     * blocks in the x direction is the number of fronts, with f0
     * being the first front pointed to by dat. The threadblock should
     * also be 1d.
     */
    template<typename T> __global__ void
    assemble_11_kernel(unsigned int f0, AssembleData<T>* dat) {
      int idx = blockIdx.x * blockDim.x + threadIdx.x;
      auto& F = dat[blockIdx.y + f0];
      if (idx >= F.n11) return;
      auto& t = F.e11[idx];
      F.F11[t.r + t.c*F.d1] = t.v;
    }
    /**
     * Put elements of the sparse matrix in the F12 anf F21 parts of
     * the front. These two are combined because F.n12 and F.n21 are
     * (probably always?) equal, and to save on overhead of launching
     * kernels/blocks.
     *
     * Use this with a 1-dimensional grid, where the number of grid
     * blocks in the x direction is the number of fronts, with f0
     * being the first front pointed to by dat. The threadblock should
     * also be 1d.
     */
    template<typename T> __global__ void
    assemble_12_21_kernel(unsigned int f0, AssembleData<T>* dat) {
      int idx = blockIdx.x * blockDim.x + threadIdx.x;
      auto& F = dat[blockIdx.y + f0];
      if (idx < F.n12) {
        auto& t = F.e12[idx];
        F.F12[t.r + t.c*F.d1] = t.v;
      }
      if (idx < F.n21) {
        auto& t = F.e21[idx];
        F.F21[t.r + t.c*F.d2] = t.v;
      }
    }


    /**
     * Single extend-add operation from one contribution block into
     * the parent front. d1 is the size of F11, d2 is the size of F22.
     */
    template<typename T> __device__ void
    ea_kernel(int x, int y, int d1, int d2, int dCB,
              T* F11, T* F12, T* F21, T* F22,
              T* CB, std::size_t* I) {
      if (x >= dCB || y >= dCB) return;
      auto Ix = I[x], Iy = I[y];
      if (Ix < d1) {
        if (Iy < d1) F11[Iy+Ix*d1] += CB[y+x*dCB];
        else F21[Iy-d1+Ix*d2] += CB[y+x*dCB];
      } else {
        if (Iy < d1) F12[Iy+(Ix-d1)*d1] += CB[y+x*dCB];
        else F22[Iy-d1+(Ix-d1)*d2] += CB[y+x*dCB];
      }
    }

    template<typename T> __global__ void
    extend_add_kernel_left(unsigned int by0, AssembleData<T>* dat) {
      int x = blockIdx.x * blockDim.x + threadIdx.x,
        y = (blockIdx.y + by0) * blockDim.y + threadIdx.y;
      auto& F = dat[blockIdx.z];
      if (F.CB1)
        ea_kernel(x, y, F.d1, F.d2, F.dCB1,
                  F.F11, F.F12, F.F21, F.F22, F.CB1, F.I1);
    }
    template<typename T> __global__ void
    extend_add_kernel_right(unsigned int by0, AssembleData<T>* dat) {
      int x = blockIdx.x * blockDim.x + threadIdx.x,
        y = (blockIdx.y + by0) * blockDim.y + threadIdx.y;
      auto& F = dat[blockIdx.z];
      if (F.CB2)
        ea_kernel(x, y, F.d1, F.d2, F.dCB2,
                  F.F11, F.F12, F.F21, F.F22, F.CB2, F.I2);
    }


    template<typename T> void
    assemble(unsigned int nf, AssembleData<T>* dat,
             AssembleData<T>* ddat) {
      { // front assembly from sparse matrix
        unsigned int nt1 = 128, nt2 = 32, nb1 = 0, nb2 = 0;
        for (int f=0; f<nf; f++) {
          unsigned int b = dat[f].n11 / nt1 + (dat[f].n11 % nt1 != 0);
          if (b > nb1) nb1 = b;
          b = dat[f].n12 / nt2 + (dat[f].n12 % nt2 != 0);
          if (b > nb2) nb2 = b;
          b = dat[f].n21 / nt2 + (dat[f].n21 % nt2 != 0);
          if (b > nb2) nb2 = b;
        }
        for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Y) {
          dim3 grid(nb1, std::min(nf-f, MAX_BLOCKS_Y));
          assemble_11_kernel<<<grid,nt1>>>(f, ddat);
        }
        if (nb2)
          for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Y) {
            dim3 grid(nb2, std::min(nf-f, MAX_BLOCKS_Y));
            assemble_12_21_kernel<<<grid,nt2>>>(f, ddat);
          }
      }
      cudaDeviceSynchronize();
      { // extend-add
        unsigned int nt = 16, nb = 0;
        for (int f=0; f<nf; f++) {
          int b = dat[f].dCB1 / nt + (dat[f].dCB1 % nt != 0);
          if (b > nb) nb = b;
          b = dat[f].dCB2 / nt + (dat[f].dCB2 % nt != 0);
          if (b > nb) nb = b;
        }
        dim3 block(nt, nt);
        using T_ = typename cuda_type<T>::value_type;
        auto dat_ = reinterpret_cast<AssembleData<T_>*>(ddat);
        for (unsigned int b1=0; b1<nb; b1+=MAX_BLOCKS_Y) {
          int nb1 = std::min(nb-b1, MAX_BLOCKS_Y);
          for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Z) {
            dim3 grid(nb, nb1, std::min(nf-f, MAX_BLOCKS_Z));
            extend_add_kernel_left<<<grid, block>>>(b1, dat_+f);
          }
        }
        cudaDeviceSynchronize();
        for (unsigned int b1=0; b1<nb; b1+=MAX_BLOCKS_Y) {
          int nb1 = std::min(nb-b1, MAX_BLOCKS_Y);
          for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Z) {
            dim3 grid(nb, nb1, std::min(nf-f, MAX_BLOCKS_Z));
            extend_add_kernel_right<<<grid, block>>>(b1, dat_+f);
          }
        }
      }
    }


    // /**
    //  * This only works if value >= 0.
    //  * It's assuming two's complement for the int.
    //  * __float_as_int is like reinterpret_cast<int&>(value)
    //  */
    // __device__ __forceinline__ void atomicAbsMax(float* data, float value) {
    //   atomicMax((int *)data, __float_as_int(value));
    // }
    // __device__ __forceinline__ void atomicAbsMax(double* addr, double value) {
    //   // why does this not compile?
    //   atomicMax((long long int *)addr, __double_as_longlong(value));
    // }


    /**
     * LU with row pivoting, with a single NTxNT thread block. The
     * matrix size n must be less than NT.
     *
     * This is a naive implementation. The goal here is to reduce
     * kernel launch overhead by batching many small LU
     * factorizations.
     *
     * Use thrust::complex instead of std::complex.
     */
    template<typename T, int NT> __device__ void
    LU_block_kernel(int n, T* F, int* piv) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      using real_t = typename real_type<T>::value_type;
      __shared__ int p;
      __shared__ cuda_primitive_t M_[NT*NT];
      T* M = reinterpret_cast<T*>(M_);
      __shared__ real_t Mmax, cabs[NT];

      int j = threadIdx.x, i = threadIdx.y;

      // copy F from global device storage into shared memory
      if (i < n && j < n)
        M[i+j*NT] = F[i+j*n];

      for (int k=0; k<n; k++) {
        // only 1 thread looks for the pivot element
        // this should be optimized?
// #if 0
//         real_t pa = 0.;
//         Mmax = 0.;
//         piv[k] = k + 1;
//         __syncthreads();
//         if (j == k && i >= k) {
//           pa = abs(M[i+k*NT]);
//           // see above, not working for double?
//           atomicAbsMax(&Mmax, pa);
//         }
//         __syncthreads();
//         if (j == k && i > k)
//           if (Mmax == pa)
//             atomicMin(piv+k, i+1);
#if 0
        if (j == k && i == k) {
          p = k;
          Mmax = abs(M[k+k*NT]);
          for (int l=k+1; l<n; l++) {
            auto tmp = abs(M[l+k*NT]);
            if (tmp > Mmax) {
              Mmax = tmp;
              p = l;
            }
          }
          piv[k] = p + 1;
        }
#else
        if (j == k && i >= k)
            cabs[i] = abs(M[i+j*NT]);
        __syncthreads();
        if (j == k && i == k) {
          p = k;
          Mmax = cabs[k]; //abs(M[k+k*NT]);
          for (int l=k+1; l<n; l++) {
            auto tmp = cabs[l]; //abs(M[l+k*NT]);
            if (tmp > Mmax) {
              Mmax = tmp;
              p = l;
            }
          }
          piv[k] = p + 1;
        }
#endif
        __syncthreads();
        // swap row k with the pivot row
        if (j < n && i == k && p != k)
          //if (i == k && p != k)
          for (int l=0; l<n; l++) {
            auto tmp = M[k+l*NT];
            M[k+l*NT] = M[p+l*NT];
            M[p+l*NT] = tmp;
          }
        __syncthreads();
        // divide by the pivot element
        if (j == k && i > k && i < n)
          M[i+k*NT] /= Mmax;
        __syncthreads();
        // Schur update
        if (j > k && i > k && j < n && i < n)
          //if (j > k && i > k)
          M[i+j*NT] -= M[i+k*NT] * M[k+j*NT];
        __syncthreads();
      }
      // write back from shared to global device memory
      if (i < n && j < n)
        F[i+j*n] = M[i+j*NT];
    }

    template<typename T, int NT> __global__ void
    LU_block_kernel_batched(FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      LU_block_kernel<T,NT>(A.n1, A.F11, A.piv);
    }


    /**
     * LU solve with matrix F factor in LU, with pivot vector piv. F
     * is n x n, and n <= NT. X is the right hand side, and is n x
     * m. Both F and X have leading dimension n.
     *
     * NTxNT is the dimension of the thread block.
     *
     * This doesn't work for T = std::complex<?>, use
     * T=thrust::complex<?> instead.
     */
    template<typename T, int NT> __device__ void
    solve_block_kernel(int n, int m, T* F, T* X, int* piv) {
      using primitive_t = typename primitive_type<T>::value_type;
      __shared__ int P[NT];
      __shared__ primitive_t A_[NT*NT], B_[NT*NT];
      T *B = reinterpret_cast<T*>(B_), *A = reinterpret_cast<T*>(A_);
      int j = threadIdx.x, i = threadIdx.y;
      if (j == 0)
        P[i] = i;
      __syncthreads();
      if (i == 0 && j == 0)
        for (int k=0; k<n; k++) {
          auto p = piv[k]-1;
          auto tmp = P[k];
          P[k] = P[p];
          P[p] = tmp;
        }
      // put matrix F in shared memory
      if (i < n && j < n)
        A[j+i*NT] = F[i+j*n];
      __syncthreads();

      // loop over blocks of NT columns of X
      for (int b=0; b<m; b+=NT) {
        int c = b + j;

        // put X in shared memory, while applying the permutation
        if (i < n && c < m)
          B[j+i*NT] = X[P[i]+c*n];
        __syncthreads();

        // solve with L (unit diagonal)
        for (int k=0; k<n; k++) {
          if (i > k && i < n && c < m)
            B[j+i*NT] -= A[k+i*NT] * B[j+k*NT];
          __syncthreads();
        }

        // solve with U
        for (int k=n-1; k>=0; k--) {
          if (i == k && c < m)
            B[j+i*NT] /= A[i+i*NT];
          __syncthreads();
          if (i < k && c < m)
            B[j+i*NT] -= A[k+i*NT] * B[j+k*NT];
          __syncthreads();
        }

        // write from shared back to global device memory
        if (i < n && c < m)
          X[i+c*n] = B[j+i*NT];
      }
    }

    template<typename T, int NT> __global__ void
    solve_block_kernel_batched(FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      solve_block_kernel<T,NT>(A.n1, A.n2, A.F11, A.F12, A.piv);
    }


    /**
     * Compute F -= F21 * F12, where F is d2 x d2 and F12 is d1 x d2.
     * d1 is <= NT. This should be called with a single NT x NT thread
     * block.
     */
    template<typename T, int NT> __device__ void
    Schur_block_kernel(int d1, int d2, T* F12, T* F21, T* F22) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      __shared__ cuda_primitive_t B_[NT*NT], A_[NT*NT];
      T *B = reinterpret_cast<T*>(B_), *A = reinterpret_cast<T*>(A_);
      int j = threadIdx.x, i = threadIdx.y;
      A[j+i*NT] = B[j+i*NT] = 0.;
      for (int cb=0; cb<d2; cb+=NT) {
        int c = cb + j;
        // put NT columns of F12 in shared memory B
        if (i < d1 && c < d2)
          B[j+i*NT] = F12[i+c*d1];
        __syncthreads();
        for (int rb=0; rb<d2; rb+=NT) {
          int r = rb + i;
          // put NT rows of F21 in shared memory A
          if (r < d2 && j < d1)
            A[j+i*NT] = F21[r+j*d2];
          __syncthreads(); // wait for A and B
          if (c < d2 && r < d2) {
            T tmp(0.);
            // k < d1 <= NT, by using k<NT this can be unrolled
            for (int k=0; k<NT; k++)
              tmp += A[k+i*NT] * B[j+k*NT];
            F22[r+c*d2] -= tmp;
          }
          __syncthreads(); // sync before reading new A/B
        }
      }
    }

    template<typename T, int NT> __global__ void
    Schur_block_kernel_batched(FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      Schur_block_kernel<T,NT>(A.n1, A.n2, A.F12, A.F21, A.F22);
    }


    template<typename T, int NT> void
    factor_block_batch(unsigned int count, FrontData<T>* dat) {
      if (!count) return;
      using T_ = typename cuda_type<T>::value_type;
      auto dat_ = reinterpret_cast<FrontData<T_>*>(dat);
      dim3 block(NT, NT), grid(count, 1, 1);
      LU_block_kernel_batched<T_,NT><<<count, block>>>(dat_);
      solve_block_kernel_batched<T_,NT><<<count, block>>>(dat_);
      Schur_block_kernel_batched<T_,NT><<<count, block>>>(dat_);
    }


    template<typename T, int NT> __global__ void
    solve_block_kernel_batched(int nrhs, FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      solve_block_kernel<T,NT>(A.n1, nrhs, A.F11, A.F12, A.piv);
    }

    template<typename T, int NT, int alpha, int beta> __device__ void
    gemmNN_block_inner_kernel(int m, int n, int k,
                              T* Aglobal, T* Bglobal, T* Cglobal) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      __shared__ cuda_primitive_t B_[NT*NT], A_[NT*NT];
      T *B = reinterpret_cast<T*>(B_), *A = reinterpret_cast<T*>(A_);
      int j = threadIdx.x, i = threadIdx.y;
      for (int cb=0; cb<n; cb+=NT) {
        int c = cb + j;
        // put NT columns of Bglobal in shared memory B
        if (i < k && c < n)
          B[j+i*NT] = Bglobal[i+c*k];
        __syncthreads();
        for (int rb=0; rb<m; rb+=NT) {
          int r = rb + i;
          // put NT rows of F21 in shared memory A
          if (r < m && j < k)
            A[j+i*NT] = Aglobal[r+j*m];
          __syncthreads(); // wait for A and B
          if (c < n && r < m) {
            T tmp(0.);
            // l < n <= NT, by using k<NT this can be unrolled
            for (int l=0; l<k; l++)
              tmp += A[l+i*NT] * B[j+l*NT];
            Cglobal[r+c*m] = T(alpha) * tmp + T(beta) * Cglobal[r+c*m];
          }
          __syncthreads(); // sync before reading new A/B
        }
      }
    }
    /**
     * Compute C = alpha*A*B + beta*C, with a single thread block,
     * with NT x NT threads. A is m x k, and k <= NT. B is m x n and C
     * is m x n.
     */
    template<typename T, int NT, int alpha, int beta> __global__ void
    gemmNN_block_inner_kernel_batched(int nrhs, FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      gemmNN_block_inner_kernel<T,NT,alpha,beta>
        (A.n2, nrhs, A.n1, A.F21, A.F12, A.F22);
    }

    /**
     * Compute a matrix vector product C = alpha*A + beta*C with a
     * single 1 x NT thread block. A is m x k, with m <= NT. B is k x
     * 1, C is m x 1.
     */
    template<typename T, int NT, int alpha, int beta> __device__ void
    gemvN_block_inner_kernel(int m, int k,
                             T* Aglobal, T* Bglobal, T* Cglobal) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      __shared__ cuda_primitive_t B_[NT];
      T *B = reinterpret_cast<T*>(B_);
      int i = threadIdx.y;
      B[i] = Bglobal[i];
      __syncthreads();
      for (int r=i; r<m; r+=NT) {
        T tmp(0.);
        for (int j=0; j<k; j++) // j < k <= NT
          tmp += Aglobal[r+j*m] * B[j];
        Cglobal[r] = T(alpha) * tmp + T(beta) * Cglobal[r];
      }
    }
    template<typename T, int NT, int alpha, int beta> __global__ void
    gemvN_block_inner_kernel_batched(FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      gemvN_block_inner_kernel<T,NT,alpha,beta>
        (A.n2, A.n1, A.F21, A.F12, A.F22);
    }

    /**
     * Single extend-add operation along the column dimension, for the
     * solve.  d1 is the size of F11, d2 is the size of F22.
     */
    template<typename T> __device__ void
    ea_rhs_kernel(int x, int y, int nrhs, int dsep, int dupd, int dCB,
                  T* b, T* bupd, T* CB, std::size_t* I) {
      if (x >= nrhs || y >= dCB) return;
      auto Iy = I[y];
      if (Iy < dsep) b[Iy+x*dsep] += CB[y+x*dCB];
      else bupd[Iy-dsep+x*dupd] += CB[y+x*dCB];
    }

    template<typename T> __global__ void
    extend_add_rhs_kernel_left
    (int nrhs, unsigned int by0, AssembleData<T>* dat) {
      int x = blockIdx.x * blockDim.x + threadIdx.x,
        y = (blockIdx.y + by0) * blockDim.y + threadIdx.y;
      auto& F = dat[blockIdx.z];
      if (F.CB1)
        ea_rhs_kernel(x, y, nrhs, F.d1, F.d2, F.dCB1,
                      F.F11, F.F21, F.CB1, F.I1);
    }
    template<typename T> __global__ void
    extend_add_rhs_kernel_right
    (int nrhs, unsigned int by0, AssembleData<T>* dat) {
      int x = blockIdx.x * blockDim.x + threadIdx.x,
        y = (blockIdx.y + by0) * blockDim.y + threadIdx.y;
      auto& F = dat[blockIdx.z];
      if (F.CB2)
        ea_rhs_kernel(x, y, nrhs, F.d1, F.d2, F.dCB2,
                      F.F11, F.F21, F.CB2, F.I2);
    }


    template<typename T> void
    extend_add_rhs(int nrhs, unsigned int nf,
                   AssembleData<T>* dat, AssembleData<T>* ddat) {
      unsigned int nty = 64, nby = 0;
      for (int f=0; f<nf; f++) {
        int b = dat[f].dCB1 / nty + (dat[f].dCB1 % nty != 0);
        if (b > nby) nby = b;
        b = dat[f].dCB2 / nty + (dat[f].dCB2 % nty != 0);
        if (b > nby) nby = b;
      }
      int ntx = (nrhs == 1) ? 1 : 16;
      int nbx = nrhs / ntx + (nrhs % ntx != 0);
      dim3 block(ntx, nty);
      using T_ = typename cuda_type<T>::value_type;
      auto dat_ = reinterpret_cast<AssembleData<T_>*>(ddat);
      for (unsigned int by=0; by<nby; by+=MAX_BLOCKS_Y) {
        int nbyy = std::min(nby-by, MAX_BLOCKS_Y);
        for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Z) {
          dim3 grid(nbx, nbyy, std::min(nf-f, MAX_BLOCKS_Z));
          extend_add_rhs_kernel_left<<<grid, block>>>(nrhs, by, dat_+f);
        }
      }
      cudaDeviceSynchronize();
      for (unsigned int by=0; by<nby; by+=MAX_BLOCKS_Y) {
        int nbyy = std::min(nby-by, MAX_BLOCKS_Y);
        for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Z) {
          dim3 grid(nbx, nbyy, std::min(nf-f, MAX_BLOCKS_Z));
          extend_add_rhs_kernel_right<<<grid, block>>>(nrhs, by, dat_+f);
        }
      }
    }

    template<typename T, int NT> void
    fwd_block_batch(int nrhs, unsigned int count,
                    FrontData<T>* dat) {
      if (!count) return;
      using T_ = typename cuda_type<T>::value_type;
      auto dat_ = reinterpret_cast<FrontData<T_>*>(dat);
      dim3 block(NT, NT);
      solve_block_kernel_batched<T_,NT><<<count, block>>>(nrhs, dat_);
      if (nrhs == 1) {
        dim3 block1(1, NT);
        gemvN_block_inner_kernel_batched
          <T_,NT,-1,1><<<count, block1>>>(dat_);
      } else
        gemmNN_block_inner_kernel_batched
          <T_,NT,-1,1><<<count, block>>>(nrhs, dat_);
    }



    /**
     * Single extend-add operation along the column dimension, for the
     * solve.  d1 is the size of F11, d2 is the size of F22.
     */
    template<typename T> __device__ void
    extract_rhs_kernel(int x, int y, int nrhs, int dsep, int dupd, int dCB,
                       T* b, T* bupd, T* CB, std::size_t* I) {
      if (x >= nrhs || y >= dCB) return;
      auto Iy = I[y];
      if (Iy < dsep) CB[y+x*dCB] = b[Iy+x*dsep];
      else CB[y+x*dCB] = bupd[Iy-dsep+x*dupd];
    }

    template<typename T> __global__ void
    extract_rhs_kernel(int nrhs, unsigned int by0, AssembleData<T>* dat) {
      int x = blockIdx.x * blockDim.x + threadIdx.x,
        y = (blockIdx.y + by0) * blockDim.y + threadIdx.y;
      auto& F = dat[blockIdx.z];
      if (F.CB1)
        extract_rhs_kernel(x, y, nrhs, F.d1, F.d2, F.dCB1,
                           F.F11, F.F21, F.CB1, F.I1);
      if (F.CB2)
        extract_rhs_kernel(x, y, nrhs, F.d1, F.d2, F.dCB2,
                           F.F11, F.F21, F.CB2, F.I2);
    }

    template<typename T> void
    extract_rhs(int nrhs, unsigned int nf, AssembleData<T>* dat,
                AssembleData<T>* ddat) {
      unsigned int nty = 64, nby = 0;
      for (int f=0; f<nf; f++) {
        int b = dat[f].dCB1 / nty + (dat[f].dCB1 % nty != 0);
        if (b > nby) nby = b;
        b = dat[f].dCB2 / nty + (dat[f].dCB2 % nty != 0);
        if (b > nby) nby = b;
      }
      int ntx = (nrhs == 1) ? 1 : 16;
      int nbx = nrhs / ntx + (nrhs % ntx != 0);
      dim3 block(ntx, nty);
      using T_ = typename cuda_type<T>::value_type;
      auto dat_ = reinterpret_cast<AssembleData<T_>*>(ddat);
      for (unsigned int by=0; by<nby; by+=MAX_BLOCKS_Y) {
        int nbyy = std::min(nby-by, MAX_BLOCKS_Y);
        for (unsigned int f=0; f<nf; f+=MAX_BLOCKS_Z) {
          dim3 grid(nbx, nbyy, std::min(nf-f, MAX_BLOCKS_Z));
          extract_rhs_kernel<<<grid, block>>>(nrhs, by, dat_+f);
        }
      }
    }



    /**
     * Compute a matrix vector product C = alpha*A + beta*C with a
     * single 1 x NT thread block. A is m x k, with k <= NT. B is k x
     * 1, C is m x 1.
     */
    template<typename T, int NT, int alpha, int beta> __device__ void
    gemvN_block_outer_kernel(int m, int k,
                             T* Aglobal, T* Bglobal, T* Cglobal) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      __shared__ cuda_primitive_t B_[NT], C_[NT];
      T *B = reinterpret_cast<T*>(B_), *C = reinterpret_cast<T*>(C_);
      int i = threadIdx.y;
      C[i] = T(0.);
      for (int c=0; c<k; c+=NT) {
        B[i] = Bglobal[c+i];
        __syncthreads();
        if (i < m) {
          T tmp(0.);
          for (int j=0; j<min(NT, k-c); j++)
            tmp += Aglobal[i+(c+j)*m] * B[j];
          C[i] += tmp;
        }
      }
      if (i < m)
        Cglobal[i] = T(alpha) * C[i] + T(beta) * Cglobal[i];
    }
    template<typename T, int NT, int alpha, int beta> __global__ void
    gemvN_block_outer_kernel_batched(FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      // F12 is F12, F21 holds yupd, F11 holds y
      gemvN_block_outer_kernel<T,NT,alpha,beta>
        (A.n1, A.n2, A.F12, A.F21, A.F11);
    }


    template<typename T, int NT, int alpha, int beta> __device__ void
    gemmNN_block_outer_kernel(int m, int n, int k,
                              T* Aglobal, T* Bglobal, T* Cglobal) {
      using cuda_primitive_t = typename primitive_type<T>::value_type;
      __shared__ cuda_primitive_t B_[NT*NT], A_[NT*NT], C_[NT*NT];
      T *A = reinterpret_cast<T*>(A_), *B = reinterpret_cast<T*>(B_),
        *C = reinterpret_cast<T*>(C_);
      int j = threadIdx.x, i = threadIdx.y;
      for (int nb=0; nb<n; nb+=NT) {
        int n_ = nb + j;
        C[i+j*NT] = T(0.);
        for (int kb=0; kb<k; kb+=NT) {
          int dk = min(NT, k-kb);
          if (j < dk && i < m)
            A[i+j*NT] = Aglobal[i+(kb+j)*m];
          if (i < dk && n_ < n)
            B[i+j*NT] = Bglobal[(kb+i)+n_*k];
          __syncthreads();
          if (i < m && n_ < n) {
            T tmp(0.);
            for (int l=0; l<dk; l++)
              tmp += A[i+l*NT] * B[l+j*NT];
            C[i+j*NT] += tmp;
          }
          __syncthreads();
        }
        if (i < m && n_ < n)
          Cglobal[i+n_*m] = T(alpha) * C[i+j*NT] + T(beta) * Cglobal[i+n_*m];
      }
    }
    /**
     * Compute C = alpha*A*B + beta*C, with a single thread block,
     * with NT x NT threads. A is m x k, and k <= NT. B is m x n and C
     * is m x n.
     */
    template<typename T, int NT, int alpha, int beta> __global__ void
    gemmNN_block_outer_kernel_batched(int nrhs, FrontData<T>* dat) {
      FrontData<T>& A = dat[blockIdx.x];
      gemmNN_block_outer_kernel<T,NT,alpha,beta>
        (A.n1, nrhs, A.n2, A.F12, A.F21, A.F11);
    }


    template<typename T, int NT> void
    bwd_block_batch(int nrhs, unsigned int count,
                    FrontData<T>* dat) {
      if (!count) return;
      using T_ = typename cuda_type<T>::value_type;
      auto dat_ = reinterpret_cast<FrontData<T_>*>(dat);
      if (nrhs == 1) {
        dim3 block(1, NT);
        gemvN_block_outer_kernel_batched
          <T_,NT,-1,1><<<count, block>>>(dat_);
      } else {
        dim3 block(NT, NT);
        gemmNN_block_outer_kernel_batched
          <T_,NT,-1,1><<<count, block>>>(nrhs, dat_);
      }
    }


    // explicit template instantiations
    template void assemble(unsigned int, AssembleData<float>*, AssembleData<float>*);
    template void assemble(unsigned int, AssembleData<double>*, AssembleData<double>*);
    template void assemble(unsigned int, AssembleData<std::complex<float>>*, AssembleData<std::complex<float>>*);
    template void assemble(unsigned int, AssembleData<std::complex<double>>*, AssembleData<std::complex<double>>*);

    template void extend_add_rhs(int, unsigned int, AssembleData<float>*, AssembleData<float>*);
    template void extend_add_rhs(int, unsigned int, AssembleData<double>*, AssembleData<double>*);
    template void extend_add_rhs(int, unsigned int, AssembleData<std::complex<float>>*, AssembleData<std::complex<float>>*);
    template void extend_add_rhs(int, unsigned int, AssembleData<std::complex<double>>*, AssembleData<std::complex<double>>*);

    template void extract_rhs(int, unsigned int, AssembleData<float>*, AssembleData<float>*);
    template void extract_rhs(int, unsigned int, AssembleData<double>*, AssembleData<double>*);
    template void extract_rhs(int, unsigned int, AssembleData<std::complex<float>>*, AssembleData<std::complex<float>>*);
    template void extract_rhs(int, unsigned int, AssembleData<std::complex<double>>*, AssembleData<std::complex<double>>*);

    template void factor_block_batch<float,8>(unsigned int, FrontData<float>*);
    template void factor_block_batch<double,8>(unsigned int, FrontData<double>*);
    template void factor_block_batch<std::complex<float>,8>(unsigned int, FrontData<std::complex<float>>*);
    template void factor_block_batch<std::complex<double>,8>(unsigned int, FrontData<std::complex<double>>*);

    template void factor_block_batch<float,16>(unsigned int, FrontData<float>*);
    template void factor_block_batch<double,16>(unsigned int, FrontData<double>*);
    template void factor_block_batch<std::complex<float>,16>(unsigned int, FrontData<std::complex<float>>*);
    template void factor_block_batch<std::complex<double>,16>(unsigned int, FrontData<std::complex<double>>*);

    template void factor_block_batch<float,32>(unsigned int, FrontData<float>*);
    template void factor_block_batch<double,32>(unsigned int, FrontData<double>*);
    template void factor_block_batch<std::complex<float>,32>(unsigned int, FrontData<std::complex<float>>*);
    template void factor_block_batch<std::complex<double>,32>(unsigned int, FrontData<std::complex<double>>*);


    template void fwd_block_batch<float,8>(int, unsigned int, FrontData<float>*);
    template void fwd_block_batch<double,8>(int, unsigned int, FrontData<double>*);
    template void fwd_block_batch<std::complex<float>,8>(int, unsigned int, FrontData<std::complex<float>>*);
    template void fwd_block_batch<std::complex<double>,8>(int, unsigned int, FrontData<std::complex<double>>*);

    template void fwd_block_batch<float,16>(int, unsigned int, FrontData<float>*);
    template void fwd_block_batch<double,16>(int, unsigned int, FrontData<double>*);
    template void fwd_block_batch<std::complex<float>,16>(int, unsigned int, FrontData<std::complex<float>>*);
    template void fwd_block_batch<std::complex<double>,16>(int, unsigned int, FrontData<std::complex<double>>*);

    template void fwd_block_batch<float,32>(int, unsigned int, FrontData<float>*);
    template void fwd_block_batch<double,32>(int, unsigned int, FrontData<double>*);
    template void fwd_block_batch<std::complex<float>,32>(int, unsigned int, FrontData<std::complex<float>>*);
    template void fwd_block_batch<std::complex<double>,32>(int, unsigned int, FrontData<std::complex<double>>*);


    template void bwd_block_batch<float,8>(int, unsigned int, FrontData<float>*);
    template void bwd_block_batch<double,8>(int, unsigned int, FrontData<double>*);
    template void bwd_block_batch<std::complex<float>,8>(int, unsigned int, FrontData<std::complex<float>>*);
    template void bwd_block_batch<std::complex<double>,8>(int, unsigned int, FrontData<std::complex<double>>*);

    template void bwd_block_batch<float,16>(int, unsigned int, FrontData<float>*);
    template void bwd_block_batch<double,16>(int, unsigned int, FrontData<double>*);
    template void bwd_block_batch<std::complex<float>,16>(int, unsigned int, FrontData<std::complex<float>>*);
    template void bwd_block_batch<std::complex<double>,16>(int, unsigned int, FrontData<std::complex<double>>*);

    template void bwd_block_batch<float,32>(int, unsigned int, FrontData<float>*);
    template void bwd_block_batch<double,32>(int, unsigned int, FrontData<double>*);
    template void bwd_block_batch<std::complex<float>,32>(int, unsigned int, FrontData<std::complex<float>>*);
    template void bwd_block_batch<std::complex<double>,32>(int, unsigned int, FrontData<std::complex<double>>*);

  } // end namespace gpu
} // end namespace strumpack
