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
 */
#ifndef DISTRIBUTED_MATRIX_HPP
#define DISTRIBUTED_MATRIX_HPP

#include <cstddef>
#include <random>
#include <functional>
#include <limits>

#include "misc/MPIWrapper.hpp"
#include "misc/RandomWrapper.hpp"
#include "misc/TaskTimer.hpp"
#include "DenseMatrix.hpp"
#include "ScaLAPACKWrapper.hpp"
#include "BLACSGrid.hpp"

namespace strumpack {

  inline int
  indxl2g(int INDXLOC, int NB, int IPROC, int ISRCPROC, int NPROCS)
  { return NPROCS*NB*((INDXLOC-1)/NB) + (INDXLOC-1) % NB +
      ((NPROCS+IPROC-ISRCPROC) % NPROCS)*NB + 1; }
  inline int
  indxg2l(int INDXGLOB, int NB, int IPROC, int ISRCPROC, int NPROCS)
  { return NB*((INDXGLOB-1)/(NB*NPROCS)) + (INDXGLOB-1) % NB + 1; }
  inline int
  indxg2p(int INDXGLOB, int NB, int IPROC, int ISRCPROC, int NPROCS)
  { return ( ISRCPROC + (INDXGLOB - 1) / NB ) % NPROCS; }


  template<typename scalar_t> class DistributedMatrix {
    using real_t = typename RealType<scalar_t>::value_type;

  protected:
    const BLACSGrid* grid_ = nullptr;
    scalar_t* data_ = nullptr;
    int lrows_;
    int lcols_;
    int desc_[9];

  public:
    DistributedMatrix();
    DistributedMatrix(const BLACSGrid* g, const DenseMatrix<scalar_t>& m);
    DistributedMatrix(const BLACSGrid* g, DenseMatrix<scalar_t>&& m);
    DistributedMatrix(const BLACSGrid* g, DenseMatrixWrapper<scalar_t>&& m);
    DistributedMatrix(const BLACSGrid* g, int M, int N,
                      const DistributedMatrix<scalar_t>& m,
                      int context_all);
    DistributedMatrix(const BLACSGrid* g, int M, int N);
    DistributedMatrix(const BLACSGrid* g, int M, int N, int MB, int NB);
    DistributedMatrix(const BLACSGrid* g, int desc[9]);

    DistributedMatrix(const DistributedMatrix<scalar_t>& m);
    DistributedMatrix(DistributedMatrix<scalar_t>&& m);
    virtual ~DistributedMatrix();

    DistributedMatrix<scalar_t>&
    operator=(const DistributedMatrix<scalar_t>& m);
    DistributedMatrix<scalar_t>&
    operator=(DistributedMatrix<scalar_t>&& m);


    inline const int* desc() const { return desc_; }
    inline int* desc() { return desc_; }
    inline bool active() const { return grid() && grid()->active(); }

    inline const BLACSGrid* grid() const { return grid_; }
    inline const MPIComm& Comm() const { return grid()->Comm(); }
    inline MPI_Comm comm() const { return Comm().comm(); }

    inline int ctxt() const { return grid() ? grid()->ctxt() : -1; }
    inline int ctxt_all() const { return grid() ? grid()->ctxt_all() : -1; }

    virtual int rows() const { return desc_[2]; }
    virtual int cols() const { return desc_[3]; }
    inline int lrows() const { return lrows_; }
    inline int lcols() const { return lcols_; }
    inline int ld() const { return lrows_; }
    inline int MB() const { return desc_[4]; }
    inline int NB() const { return desc_[5]; }
    inline int rowblocks() const { return std::ceil(float(lrows()) / MB()); }
    inline int colblocks() const { return std::ceil(float(lcols()) / NB()); }

    virtual int I() const { return 1; }
    virtual int J() const { return 1; }
    virtual void lranges(int& rlo, int& rhi, int& clo, int& chi) const;

    inline const scalar_t* data() const { return data_; }
    inline scalar_t* data() { return data_; }
    inline const scalar_t& operator()(int r, int c) const
    { return data_[r+ld()*c]; }
    inline scalar_t& operator()(int r, int c) { return data_[r+ld()*c]; }

    inline int prow() const { assert(grid()); return grid()->prow(); }
    inline int pcol() const { assert(grid()); return grid()->pcol(); }
    inline int nprows() const { assert(grid()); return grid()->nprows(); }
    inline int npcols() const { assert(grid()); return grid()->npcols(); }

    inline bool is_master() const { return grid() && prow() == 0 && pcol() == 0; }
    inline int rowl2g(int row) const { assert(grid());
      return indxl2g(row+1, MB(), prow(), 0, nprows()) - I(); }
    inline int coll2g(int col) const { assert(grid());
      return indxl2g(col+1, NB(), pcol(), 0, npcols()) - J(); }
    inline int rowg2l(int row) const { assert(grid());
      return indxg2l(row+I(), MB(), prow(), 0, nprows()) - 1; }
    inline int colg2l(int col) const { assert(grid());
      return indxg2l(col+J(), NB(), pcol(), 0, npcols()) - 1; }
    inline int rowg2p(int row) const { assert(grid());
      return indxg2p(row+I(), MB(), prow(), 0, nprows()); }
    inline int colg2p(int col) const { assert(grid());
      return indxg2p(col+J(), NB(), pcol(), 0, npcols()); }
    inline int rank(int r, int c) const {
      return rowg2p(r) + colg2p(c) * nprows(); }
    inline bool is_local(int r, int c) const { assert(grid());
      return rowg2p(r) == prow() && colg2p(c) == pcol();
    }

    inline bool fixed() const { return MB()==default_MB && NB()==default_NB; }
    inline int rowl2g_fixed(int row) const {
      assert(grid() && fixed());
      return indxl2g(row+1, default_MB, prow(), 0, nprows()) - I(); }
    inline int coll2g_fixed(int col) const {
      assert(grid() && fixed());
      return indxl2g(col+1, default_NB, pcol(), 0, npcols()) - J(); }
    inline int rowg2l_fixed(int row) const {
      assert(grid() && fixed());
      return indxg2l(row+I(), default_MB, prow(), 0, nprows()) - 1; }
    inline int colg2l_fixed(int col) const {
      assert(grid() && fixed());
      return indxg2l(col+J(), default_NB, pcol(), 0, npcols()) - 1; }
    inline int rowg2p_fixed(int row) const {
      assert(grid() && fixed());
      return indxg2p(row+I(), default_MB, prow(), 0, nprows()); }
    inline int colg2p_fixed(int col) const {
      assert(grid() && fixed());
      return indxg2p(col+J(), default_NB, pcol(), 0, npcols()); }
    inline int rank_fixed(int r, int c) const {
      assert(grid() && fixed()); return rowg2p_fixed(r) + colg2p_fixed(c) * nprows(); }
    inline bool is_local_fixed(int r, int c) const {
      assert(grid() && fixed());
      return rowg2p_fixed(r) == prow() && colg2p_fixed(c) == pcol(); }

    // TODO fixed versions??
    inline const scalar_t& global(int r, int c) const
    { assert(is_local(r, c)); return operator()(rowg2l(r),colg2l(c)); }
    inline scalar_t& global(int r, int c)
    { assert(is_local(r, c)); return operator()(rowg2l(r),colg2l(c)); }
    inline scalar_t& global_fixed(int r, int c) {
      assert(is_local(r, c)); assert(fixed());
      return operator()(rowg2l_fixed(r),colg2l_fixed(c)); }
    inline void global(int r, int c, scalar_t v) {
      if (active() && is_local(r, c)) operator()(rowg2l(r),colg2l(c)) = v;  }
    inline scalar_t all_global(int r, int c) const;

    void print() const { print("A"); }
    void print(std::string name, int precision=15) const;
    void print_to_file
    (std::string name, std::string filename,
     int width=8) const;
    void random();
    void random
    (random::RandomGeneratorBase<typename RealType<scalar_t>::
     value_type>& rgen);
    void zero();
    void fill(scalar_t a);
    void eye();
    void shift(scalar_t sigma);
    void clear();
    virtual void resize(std::size_t m, std::size_t n);
    virtual void hconcat(const DistributedMatrix<scalar_t>& b);
    DistributedMatrix<scalar_t> transpose() const;

    void laswp(const std::vector<int>& P, bool fwd);

    DistributedMatrix<scalar_t>
    extract_rows(const std::vector<std::size_t>& Ir) const;
    DistributedMatrix<scalar_t>
    extract_cols(const std::vector<std::size_t>& Ic) const;
    DistributedMatrix<scalar_t> extract
    (const std::vector<std::size_t>& I,
     const std::vector<std::size_t>& J) const;
    DistributedMatrix<scalar_t>& add(const DistributedMatrix<scalar_t>& B);
    DistributedMatrix<scalar_t>& scaled_add
    (scalar_t alpha, const DistributedMatrix<scalar_t>& B);
    typename RealType<scalar_t>::value_type norm() const;
    typename RealType<scalar_t>::value_type normF() const;
    typename RealType<scalar_t>::value_type norm1() const;
    typename RealType<scalar_t>::value_type normI() const;
    virtual std::size_t memory() const
    { return sizeof(scalar_t)*std::size_t(lrows())*std::size_t(lcols()); }
    virtual std::size_t total_memory() const
    { return sizeof(scalar_t)*std::size_t(rows())*std::size_t(cols()); }
    virtual std::size_t nonzeros() const
    { return std::size_t(lrows())*std::size_t(lcols()); }
    virtual std::size_t total_nonzeros() const
    { return std::size_t(rows())*std::size_t(cols()); }

    void scatter(const DenseMatrix<scalar_t>& a);
    DenseMatrix<scalar_t> gather() const;
    DenseMatrix<scalar_t> all_gather() const;

    DenseMatrix<scalar_t> dense_and_clear();
    DenseMatrix<scalar_t> dense() const;
    DenseMatrixWrapper<scalar_t> dense_wrapper();

    std::vector<int> LU();
    DistributedMatrix<scalar_t> solve
    (const DistributedMatrix<scalar_t>& b, const std::vector<int>& piv) const;
    void LQ
    (DistributedMatrix<scalar_t>& L, DistributedMatrix<scalar_t>& Q) const;
    void orthogonalize(scalar_t& r_max, scalar_t& r_min);
    void ID_column
    (DistributedMatrix<scalar_t>& X, std::vector<int>& piv,
     std::vector<std::size_t>& ind, real_t rel_tol, real_t abs_tol);
    void ID_row
    (DistributedMatrix<scalar_t>& X, std::vector<int>& piv,
     std::vector<std::size_t>& ind, real_t rel_tol, real_t abs_tol,
     const BLACSGrid* grid_T);

#ifdef STRUMPACK_PBLAS_BLOCKSIZE
    static const int default_MB = STRUMPACK_PBLAS_BLOCKSIZE;
    static const int default_NB = STRUMPACK_PBLAS_BLOCKSIZE;
#else
    static const int default_MB = 32;
    static const int default_NB = 32;
#endif
  };

  /**
   * copy submatrix of a DistM_t at ia,ja of size m,n into a DenseM_t
   * b at proc dest
   */
  template<typename scalar_t> void copy
  (std::size_t m, std::size_t n, const DistributedMatrix<scalar_t>& a,
   std::size_t ia, std::size_t ja, DenseMatrix<scalar_t>& b,
   int dest, int context_all) {
    if (!m || !n) return;
    int b_desc[9];
    scalapack::descset(b_desc, m, n, m, n, 0, dest, context_all, m);
    scalapack::pgemr2d
      (m, n, a.data(), a.I()+ia, a.J()+ja, a.desc(),
       b.data(), 1, 1, b_desc, context_all);
  }

  template<typename scalar_t> void copy
  (std::size_t m, std::size_t n, const DenseMatrix<scalar_t>& a, int src,
   DistributedMatrix<scalar_t>& b, std::size_t ib, std::size_t jb,
   int context_all) {
    if (!m || !n) return;
    int a_desc[9];
    scalapack::descset
      (a_desc, m, n, m, n, 0, src, context_all, std::max(m, a.ld()));
    scalapack::pgemr2d
      (m, n, a.data(), 1, 1, a_desc, b.data(), b.I()+ib, b.J()+jb,
       b.desc(), context_all);
  }

  /** copy submatrix of a at ia,ja of size m,n into b at position ib,jb */
  template<typename scalar_t> void copy
  (std::size_t m, std::size_t n, const DistributedMatrix<scalar_t>& a,
   std::size_t ia, std::size_t ja, DistributedMatrix<scalar_t>& b,
   std::size_t ib, std::size_t jb, int context_all) {
    if (!m || !n) return;
    assert(!a.active() || (m+ia <= std::size_t(a.rows()) && n+ja <= std::size_t(a.cols())));
    assert(!b.active() || (m+ib <= std::size_t(b.rows()) && n+jb <= std::size_t(b.cols())));
    scalapack::pgemr2d
      (m, n, a.data(), a.I()+ia, a.J()+ja, a.desc(),
       b.data(), b.I()+ib, b.J()+jb, b.desc(), context_all);
  }

  /**
   * Wrapper class does exactly the same as a regular DistributedMatrix,
   * but it is initialized with existing memory, so it does not
   * allocate, own or delete the memory
   */
  template<typename scalar_t>
  class DistributedMatrixWrapper : public DistributedMatrix<scalar_t> {
  private:
    int _rows, _cols, _i, _j;
  public:
    DistributedMatrixWrapper() : DistributedMatrix<scalar_t>(),
      _rows(0), _cols(0), _i(0), _j(0) {}
    DistributedMatrixWrapper(DistributedMatrix<scalar_t>& A);
    DistributedMatrixWrapper
    (std::size_t m, std::size_t n, DistributedMatrix<scalar_t>& A,
     std::size_t i, std::size_t j);
    DistributedMatrixWrapper
    (const BLACSGrid* g, std::size_t m, std::size_t n, scalar_t* A);
    DistributedMatrixWrapper
    (const BLACSGrid* g, std::size_t m, std::size_t n,
     int MB, int NB, scalar_t* A);
    DistributedMatrixWrapper
    (const BLACSGrid* g, int rsrc, int csrc, std::size_t m, std::size_t n,
     DenseMatrix<scalar_t>& A);
    DistributedMatrixWrapper
    (const DistributedMatrixWrapper<scalar_t>& A);
    DistributedMatrixWrapper
    (DistributedMatrixWrapper<scalar_t>&& A);
    virtual ~DistributedMatrixWrapper() { this->data_ = nullptr; }
    DistributedMatrixWrapper<scalar_t>&
    operator=(const DistributedMatrixWrapper<scalar_t>& A);
    DistributedMatrixWrapper<scalar_t>&
    operator=(DistributedMatrixWrapper<scalar_t>&& A);


    int rows() const override { return _rows; }
    int cols() const override { return _cols; }
    int I() const { return _i+1; }
    int J() const { return _j+1; }
    void lranges(int& rlo, int& rhi, int& clo, int& chi) const;

    void resize(std::size_t m, std::size_t n) { assert(1); }
    void hconcat(const DistributedMatrix<scalar_t>& b) { assert(1); }
    void clear()
    { this->data_ = nullptr; DistributedMatrix<scalar_t>::clear(); }
    std::size_t memory() const { return 0; }
    std::size_t total_memory() const { return 0; }
    std::size_t nonzeros() const { return 0; }
    std::size_t total_nonzeros() const { return 0; }

    DenseMatrix<scalar_t> dense_and_clear() = delete;
    DenseMatrixWrapper<scalar_t> dense_wrapper() = delete;
    DistributedMatrixWrapper<scalar_t>&
    operator=(const DistributedMatrix<scalar_t>&) = delete;
    DistributedMatrixWrapper<scalar_t>&
    operator=(DistributedMatrix<scalar_t>&&) = delete;
  };


  template<typename scalar_t> long long int
  LU_flops(const DistributedMatrix<scalar_t>& a) {
    if (!a.is_master()) return 0;
    return (is_complex<scalar_t>() ? 4:1) *
      blas::getrf_flops(a.rows(), a.cols());
  }

  template<typename scalar_t> long long int
  solve_flops(const DistributedMatrix<scalar_t>& b) {
    if (!b.is_master()) return 0;
    return (is_complex<scalar_t>() ? 4:1) *
      blas::getrs_flops(b.rows(), b.cols());
  }

  template<typename scalar_t> long long int
  LQ_flops(const DistributedMatrix<scalar_t>& a) {
    if (!a.is_master()) return 0;
    auto minrc = std::min(a.rows(), a.cols());
    return (is_complex<scalar_t>() ? 4:1) *
      (blas::gelqf_flops(a.rows(), a.cols()) +
       blas::xxglq_flops(a.cols(), a.cols(), minrc));
  }

  template<typename scalar_t> long long int
  ID_row_flops(const DistributedMatrix<scalar_t>& a, int rank) {
    if (!a.is_master()) return 0;
    return (is_complex<scalar_t>() ? 4:1) *
      (blas::geqp3_flops(a.cols(), a.rows())
       + blas::trsm_flops(rank, a.cols() - rank, scalar_t(1.), 'L'));
  }

  template<typename scalar_t> long long int
  trsm_flops(Side s, scalar_t alpha, const DistributedMatrix<scalar_t>& a,
             const DistributedMatrix<scalar_t>& b) {
    if (!a.is_master()) return 0;
    return (is_complex<scalar_t>() ? 4:1) *
      blas::trsm_flops(b.rows(), b.cols(), alpha, char(s));
  }

  template<typename scalar_t> long long int
  gemm_flops(Trans ta, Trans tb, scalar_t alpha,
             const DistributedMatrix<scalar_t>& a,
             const DistributedMatrix<scalar_t>& b, scalar_t beta) {
    if (!a.is_master()) return 0;
    return (is_complex<scalar_t>() ? 4:1) *
      blas::gemm_flops
      ((ta==Trans::N) ? a.rows() : a.cols(),
       (tb==Trans::N) ? b.cols() : b.rows(),
       (ta==Trans::N) ? a.cols() : a.rows(), alpha, beta);
  }

  template<typename scalar_t> long long int
  gemv_flops(Trans ta, const DistributedMatrix<scalar_t>& a,
             scalar_t alpha, scalar_t beta) {
    auto m = (ta==Trans::N) ? a.rows() : a.cols();
    auto n = (ta==Trans::N) ? a.cols() : a.rows();
    return (is_complex<scalar_t>() ? 4:1) *
      ((alpha != scalar_t(0.)) * m * (n * 2 - 1) +
       (alpha != scalar_t(1.) && alpha != scalar_t(0.)) * m +
       (beta != scalar_t(0.) && beta != scalar_t(1.)) * m +
       (alpha != scalar_t(0.) && beta != scalar_t(0.)) * m);
  }

  template<typename scalar_t> long long int
  orthogonalize_flops(const DistributedMatrix<scalar_t>& a) {
    if (!a.is_master()) return 0;
    auto minrc = std::min(a.rows(), a.cols());
    return (is_complex<scalar_t>() ? 4:1) *
      (blas::geqrf_flops(a.rows(), minrc) +
       blas::xxgqr_flops(a.rows(), minrc, minrc));
  }


  template<typename scalar_t>
  std::unique_ptr<const DistributedMatrixWrapper<scalar_t>>
  ConstDistributedMatrixWrapperPtr
  (std::size_t m, std::size_t n, const DistributedMatrix<scalar_t>& D,
   std::size_t i, std::size_t j) {
    return std::unique_ptr<const DistributedMatrixWrapper<scalar_t>>
      (new DistributedMatrixWrapper<scalar_t>
       (m, n, const_cast<DistributedMatrix<scalar_t>&>(D), i, j));
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (DistributedMatrix<scalar_t>& A) :
    DistributedMatrixWrapper<scalar_t>(A.rows(), A.cols(), A, 0, 0) {
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (std::size_t m, std::size_t n, DistributedMatrix<scalar_t>& A,
   std::size_t i, std::size_t j) : _rows(m), _cols(n), _i(i), _j(j) {
    assert(!A.active() || m+i <= std::size_t(A.rows()));
    assert(!A.active() || n+j <= std::size_t(A.cols()));
    assert(m >= 0 && n >= 0 && i >=0 && j >= 0);
    this->data_ = A.data();
    std::copy(A.desc(), A.desc()+9, this->desc_);
    this->lrows_ = A.lrows();   this->lcols_ = A.lcols();
    this->grid_ = A.grid();
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (const BLACSGrid* g, std::size_t m, std::size_t n, scalar_t* A)
    : DistributedMatrixWrapper<scalar_t>
    (g, m, n, DistributedMatrix<scalar_t>::default_MB,
     DistributedMatrix<scalar_t>::default_NB, A) {}


  // TODO do we need this?
  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (const BLACSGrid* g, std::size_t m, std::size_t n,
   int MB, int NB, scalar_t* A)
    : _rows(m), _cols(n), _i(0), _j(0) {
    this->grid_ = g;
    if (this->active()) {
      this->data_ = A;
      if (scalapack::descinit
          (this->desc_, _rows, _cols, MB, NB,
           0, 0, this->ctxt(), std::max(_rows, 1))) {
        std::cerr << "ERROR: Could not create DistributedMatrixWrapper"
                  << " descriptor!" << std::endl;
        abort();
      }
      this->lrows_ = scalapack::numroc
        (this->desc_[2], this->desc_[4],
         this->prow(), this->desc_[6], this->nprows());
      this->lcols_ = scalapack::numroc
        (this->desc_[3], this->desc_[5], this->pcol(),
         this->desc_[7], this->npcols());
    } else {
      this->data_ = nullptr;
      scalapack::descset
        (this->desc_, _rows, _cols, MB, NB, 0, 0, this->ctxt(), 1);
      this->lrows_ = this->lcols_ = 0;
    }
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (const BLACSGrid* g, int rsrc, int csrc, std::size_t m,
   std::size_t n, DenseMatrix<scalar_t>& A)
    : _rows(m), _cols(n), _i(0), _j(0) {
    int MB = std::max(1, _rows);
    int NB = std::max(1, _cols);
    this->grid_ = g;
    if (this->prow() == rsrc && this->pcol() == csrc) {
      this->lrows_ = _rows;
      this->lcols_ = _cols;
      this->data_ = A.data();
      if (scalapack::descinit
          (this->desc_, _rows, _cols, MB, NB,
           rsrc, csrc, this->ctxt(), std::max(_rows, 1))) {
        std::cerr << "ERROR: Could not create DistributedMatrixWrapper"
                  << " descriptor!" << std::endl;
        abort();
      }
    } else {
      this->lrows_ = this->lcols_ = 0;
      this->data_ = nullptr;
      scalapack::descset
        (this->desc_, _rows, _cols, MB, NB, rsrc, csrc, this->ctxt(), 1);
    }
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (const DistributedMatrixWrapper<scalar_t>& A) :
    DistributedMatrix<scalar_t>() {
    *this = A;
  }

  template<typename scalar_t>
  DistributedMatrixWrapper<scalar_t>::DistributedMatrixWrapper
  (DistributedMatrixWrapper<scalar_t>&& A) :
    DistributedMatrixWrapper<scalar_t>(A) {
  }

  template<typename scalar_t> DistributedMatrixWrapper<scalar_t>&
  DistributedMatrixWrapper<scalar_t>::operator=
  (const DistributedMatrixWrapper<scalar_t>& A) {
    this->data_ = A.data_;
    std::copy(A.desc_, A.desc_+9, this->desc_);
    this->lrows_ = A.lrows_;
    this->lcols_ = A.lcols_;
    this->grid_ = A.grid();
    _rows = A._rows;
    _cols = A._cols;
    _i = A._i;
    _j = A._j;
    return *this;
  }

  template<typename scalar_t> DistributedMatrixWrapper<scalar_t>&
  DistributedMatrixWrapper<scalar_t>::operator=
  (DistributedMatrixWrapper<scalar_t>&& A) {
    *this = A;
    return *this;
  }

  template<typename scalar_t> void DistributedMatrixWrapper<scalar_t>::lranges
  (int& rlo, int& rhi, int& clo, int& chi) const {
    scalapack::infog2l
      (I(), J(), this->desc(), this->nprows(), this->npcols(),
       this->prow(), this->pcol(), rlo, clo);
    scalapack::infog2l
      (I()+this->rows(), J()+this->cols(), this->desc(),
       this->nprows(), this->npcols(),
       this->prow(), this->pcol(), rhi, chi);
    rlo--; rhi--; clo--; chi--;
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix()
    : DistributedMatrix(nullptr, 0, 0, default_MB, default_NB) {
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, const DenseMatrix<scalar_t>& m)
    : DistributedMatrix(g, m.rows(), m.cols(), default_MB, default_NB) {
    if (nprows() != 1 || npcols() != 1) {
      std::cout << "ERROR: creating DistM_t from DenseM_t only possible on 1 process!" << std::endl;
      abort();
    }
    for (int c=0; c<lcols_; c++)
      for (int r=0; r<lrows_; r++)
        operator()(r, c) = m(r, c);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, DenseMatrixWrapper<scalar_t>&& m)
    : DistributedMatrix(g, m.rows(), m.cols(), default_MB, default_NB) {
    if (nprows() != 1 || npcols() != 1) {
      std::cout << "ERROR: creating DistM_t from DenseM_t only possible on 1 process!" << std::endl;
      abort();
    }
    for (int c=0; c<lcols_; c++)
      for (int r=0; r<lrows_; r++)
        operator()(r, c) = m(r, c);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, DenseMatrix<scalar_t>&& m) : grid_(g) {
    assert(g->P() == 1);
    lrows_ = m.rows();
    lcols_ = m.cols();
    if (scalapack::descinit
        (desc_, lrows_, lcols_, default_MB, default_MB, 0, 0,
         ctxt(), std::max(lrows_,1))) {
      std::cerr << "ERROR: Could not create DistributedMatrix descriptor!"
                << std::endl;
      abort();
    }
    if (m.ld() == std::size_t(lrows_)) {
      data_ = m.data_;
      m.data_ = nullptr;
    } else {
      data_ = new scalar_t[lrows_*lcols_];
      for (int c=0; c<lcols_; c++)
        for (int r=0; r<lrows_; r++)
          operator()(r, c) = m(r, c);
    }
    delete[] m.data_;
    m.data_ = nullptr;
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, int M, int N, const DistributedMatrix<scalar_t>& m,
   int context_all)
    : DistributedMatrix(g, M, N, default_MB, default_NB) {
    strumpack::copy(M, N, m, 0, 0, *this, 0, 0, context_all);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const DistributedMatrix<scalar_t>& m)
    : grid_(m.grid()), lrows_(m.lrows()), lcols_(m.lcols()) {
    std::copy(m.desc_, m.desc_+9, desc_);
    data_ = new scalar_t[lrows_*lcols_];
    std::copy(m.data_, m.data_+lrows_*lcols_, data_);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (DistributedMatrix<scalar_t>&& m)
    : grid_(m.grid()), lrows_(m.lrows()), lcols_(m.lcols()) {
    std::copy(m.desc(), m.desc()+9, desc_);
    data_ = m.data();
    m.data_ = nullptr;
  }

  template<typename scalar_t>
  DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, int M, int N)
    : DistributedMatrix(g, M, N, default_MB, default_NB) {
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, int M, int N, int MB, int NB) : grid_(g) {
    assert(M >= 0 && N >= 0 && MB >= 0 && NB >= 0);
    MB = std::max(1, MB);
    NB = std::max(1, NB);
    if (!active()) {
      lrows_ = lcols_ = 0;
      data_ = nullptr;
      scalapack::descset
        (desc_, M, N, MB, NB, 0, 0, -1, std::max(lrows_,1));
    } else {
      lrows_ = scalapack::numroc(M, MB, prow(), 0, nprows());
      lcols_ = scalapack::numroc(N, NB, pcol(), 0, npcols());
      data_ = new scalar_t[lrows_*lcols_];
      if (scalapack::descinit
          (desc_, M, N, MB, NB, 0, 0, ctxt(), std::max(lrows_,1))) {
        std::cerr << " ERROR: Could not create DistributedMatrix descriptor!"
                  << std::endl;
        abort();
      }
    }
  }

  template<typename scalar_t>
  DistributedMatrix<scalar_t>::DistributedMatrix
  (const BLACSGrid* g, int desc[9]) : grid_(g) {
    std::copy(desc, desc+9, desc_);
    if (active()) {
      lrows_ = lcols_ = 0;
      data_ = nullptr;
    } else {
      lrows_ = scalapack::numroc(desc_[2], desc_[4], prow(), desc_[6], nprows());
      lcols_ = scalapack::numroc(desc_[3], desc_[5], pcol(), desc_[7], npcols());
      assert(lrows_==desc_[8]);
      if (lrows_ && lcols_) data_ = new scalar_t[lrows_*lcols_];
      else data_ = nullptr;
    }
  }

  template<typename scalar_t>
  DistributedMatrix<scalar_t>::~DistributedMatrix() {
    clear();
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>&
  DistributedMatrix<scalar_t>::operator=
  (const DistributedMatrix<scalar_t>& m) {
    if (lrows_ != m.lrows_ || lcols_ != m.lcols_) {
      lrows_ = m.lrows_;  lcols_ = m.lcols_;
      delete[] data_;
      data_ = new scalar_t[lrows_*lcols_];
    }
    grid_ = m.grid();
    std::copy(m.data_, m.data_+lrows_*lcols_, data_);
    std::copy(m.desc_, m.desc_+9, desc_);
    return *this;
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>&
  DistributedMatrix<scalar_t>::operator=(DistributedMatrix<scalar_t>&& m) {
    grid_ = m.grid();
    lrows_ = m.lrows_;  lcols_ = m.lcols_;
    std::copy(m.desc_, m.desc_+9, desc_);
    delete[] data_;
    data_ = m.data_;
    m.data_ = nullptr;
    return *this;
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::lranges
  (int& rlo, int& rhi, int& clo, int& chi) const {
    rlo = clo = 0;
    rhi = lrows();
    chi = lcols();
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::clear() {
    delete[] data_;
    data_ = nullptr;
    lrows_ = lcols_ = 0;
    scalapack::descset(desc_, 0, 0, MB(), NB(), 0, 0, ctxt(), 1);
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::resize
  (std::size_t m, std::size_t n) {
    DistributedMatrix<scalar_t> tmp(grid(), m, n, MB(), NB());
    for (int c=0; c<std::min(lcols(), tmp.lcols()); c++)
      for (int r=0; r<std::min(lrows(), tmp.lrows()); r++)
        tmp(r, c) = operator()(r, c);
    *this = std::move(tmp);
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::hconcat
  (const DistributedMatrix<scalar_t>& b) {
    assert(rows() == b.rows());
    assert(grid() == b.grid());
    auto my_cols = cols();
    resize(rows(), my_cols+b.cols());
    if (!active()) return;
    copy(rows(), b.cols(), b, 0, 0, *this, 0, my_cols, grid()->ctxt());
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::zero() {
    if (!active()) return;
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; c++)
      for (int r=rlo; r<rhi; r++)
        operator()(r,c) = scalar_t(0.);
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::fill(scalar_t a) {
    if (!active()) return;
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; c++)
      for (int r=rlo; r<rhi; r++)
        operator()(r,c) = a;
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::random() {
    if (!active()) return;
    TIMER_TIME(TaskType::RANDOM_GENERATE, 1, t_gen);
    auto rgen = random::make_default_random_generator<real_t>();
    rgen->seed(prow(), pcol());
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; ++c)
      for (int r=rlo; r<rhi; ++r)
        operator()(r,c) = rgen->get();
    STRUMPACK_FLOPS(rgen->flops_per_prng()*(chi-clo)*(rhi-rlo));
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::random
  (random::RandomGeneratorBase<typename RealType<scalar_t>::
   value_type>& rgen) {
    if (!active()) return;
    TIMER_TIME(TaskType::RANDOM_GENERATE, 1, t_gen);
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; ++c)
      for (int r=rlo; r<rhi; ++r)
        operator()(r,c) = rgen.get();
    STRUMPACK_FLOPS(rgen.flops_per_prng()*(chi-clo)*(rhi-rlo));
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::eye() {
    if (!active()) return;
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; ++c)
      for (int r=rlo; r<rhi; ++r)
        operator()(r,c) = (rowl2g(r) == coll2g(c)) ?
          scalar_t(1.) : scalar_t(0.);
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::shift(scalar_t sigma) {
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; ++c)
      for (int r=rlo; r<rhi; ++r)
        if (rowl2g(r) == coll2g(c))
          operator()(r,c) += sigma;
  }

  /** correct value only on the procs in the ctxt */
  template<typename scalar_t> scalar_t
  DistributedMatrix<scalar_t>::all_global(int r, int c) const {
    if (!active()) return scalar_t(0.);
    scalar_t v;
    if (is_local(r, c)) {
      v = operator()(rowg2l(r), colg2l(c));
      scalapack::gebs2d(ctxt(), 'A', ' ', 1, 1, &v, 1);
    } else
      scalapack::gebr2d(ctxt(), 'A', ' ', 1, 1, &v, 1, rowg2p(r), colg2p(c));
    return v;
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::print(std::string name, int precision) const {
    if (!active()) return;
    auto tmp = gather();
    if (is_master()) tmp.print(name);
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::print_to_file
  (std::string name, std::string filename, int width) const {
    if (!active()) return;
    auto tmp = gather();
    if (is_master()) tmp.print_to_file(name, filename, width);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>
  DistributedMatrix<scalar_t>::transpose() const {
    DistributedMatrix<scalar_t> tmp(grid(), cols(), rows());
    if (!active()) return tmp;
    scalapack::ptranc
      (cols(), rows(), scalar_t(1.), data(), I(), J(),
      desc(), scalar_t(0.), tmp.data(),
      tmp.I(), tmp.J(), tmp.desc());
    return tmp;
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::laswp(const std::vector<int>& P, bool fwd) {
    if (!active()) return;
    int descip[9];
    scalapack::descset
      (descip, rows() + MB()*nprows(), 1, MB(), 1, 0, pcol(),
       ctxt(), MB() + scalapack::numroc
       (rows(), MB(), prow(), 0, nprows()));
    scalapack::plapiv
      (fwd ? 'F' : 'B', 'R', 'C', rows(), cols(), data(), I(), J(), desc(),
       P.data(), 1, 1, descip, NULL);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>
  DistributedMatrix<scalar_t>::extract_rows
  (const std::vector<std::size_t>& Ir) const {
    TIMER_TIME(TaskType::DISTMAT_EXTRACT_ROWS, 1, t_dist_mat_extract_rows);
    DistributedMatrix<scalar_t> tmp(grid(), Ir.size(), cols());
    if (!active()) return tmp;
    std::vector<std::vector<scalar_t>> sbuf(nprows());
    std::vector<std::vector<scalar_t>> rbuf(nprows());
    {
      std::vector<std::size_t> rsizes(nprows());
      std::vector<std::size_t> ssizes(nprows());
      for (std::size_t r=0; r<Ir.size(); r++) {
        auto gr = Ir[r];
        auto owner = rowg2p(gr);
        if (owner != prow()) rsizes[owner] += lcols_;
        else {
          auto dest = rowg2p(r);
          if (dest == prow()) // just copy to tmp
            for (int c=0, tmpr=tmp.rowg2l(r), lr=rowg2l(gr);
                 c<lcols_; c++)
              tmp(tmpr, c) = operator()(lr, c);
          else ssizes[dest] += lcols_;
        }
      }
      for (int p=0; p<nprows(); p++) {
        rbuf[p].resize(rsizes[p]);
        sbuf[p].reserve(ssizes[p]);
      }
    }
    for (std::size_t r=0; r<Ir.size(); r++) {
      auto gr = Ir[r];
      auto owner = rowg2p(gr);
      if (owner == prow()) {
        auto lr = rowg2l(gr);
        auto dest = rowg2p(r);
        if (dest != prow()) {
          for (int c=0; c<lcols_; c++)
            sbuf[dest].push_back(operator()(lr, c));
        }
      }
    }
    auto sreq = new MPI_Request[2*(nprows()-1)];
    auto rreq = sreq + nprows()-1;
    for (int p=0; p<nprows(); p++)
      if (p != prow()) {
        MPI_Isend(sbuf[p].data(), sbuf[p].size(), mpi_type<scalar_t>(),
                  p+pcol()*nprows(), 0, comm(),
                  (p < prow()) ? sreq+p : sreq+p-1);
        MPI_Irecv(rbuf[p].data(), rbuf[p].size(), mpi_type<scalar_t>(),
                  p+pcol()*nprows(), 0, comm(),
                  (p < prow()) ? rreq+p : rreq+p-1);
      }
    MPI_Waitall(nprows()-1, rreq, MPI_STATUSES_IGNORE);
    std::vector<scalar_t*> prbuf(nprows());
    for (int p=0; p<nprows(); p++) prbuf[p] = rbuf[p].data();
    for (std::size_t r=0; r<Ir.size(); r++) {
      auto gr = Ir[r];
      auto owner = rowg2p(gr);
      if (owner == prow()) continue;
      auto dest = rowg2p(r);
      if (dest != prow()) continue;
      auto tmpr = tmp.rowg2l(r);
      for (int c=0; c<lcols_; c++)
        tmp(tmpr, c) = *(prbuf[owner]++);
    }
    MPI_Waitall(nprows()-1, sreq, MPI_STATUSES_IGNORE);
    delete[] sreq;
    return tmp;
  }


  template<typename scalar_t> DistributedMatrix<scalar_t>
  DistributedMatrix<scalar_t>::extract_cols
  (const std::vector<std::size_t>& Jc) const {
    TIMER_TIME(TaskType::DISTMAT_EXTRACT_COLS, 1, t_dist_mat_extract_cols);
    DistributedMatrix<scalar_t> tmp(grid(), rows(), Jc.size());
    if (!active()) return tmp;
    assert(I() == 1 && J() == 1);
    std::vector<std::vector<scalar_t>> sbuf(npcols());
    std::vector<std::vector<scalar_t>> rbuf(npcols());
    {
      std::vector<std::size_t> rsizes(npcols());
      std::vector<std::size_t> ssizes(npcols());
      for (std::size_t c=0; c<Jc.size(); c++) {
        auto gc = Jc[c];
        auto owner = colg2p(gc);
        if (owner != pcol()) rsizes[owner] += lrows_;
        else {
          auto lc = colg2l(gc);
          auto dest = colg2p(c);
          if (dest == pcol()) { // just copy to tmp
            auto tmpc = tmp.colg2l(c);
            for (int r=0; r<lrows_; r++)
              tmp(r, tmpc) = operator()(r, lc);
          } else ssizes[dest] += lrows_;
        }
      }
      for (int p=0; p<npcols(); p++) {
        rbuf[p].resize(rsizes[p]);
        sbuf[p].reserve(ssizes[p]);
      }
    }
    for (std::size_t c=0; c<Jc.size(); c++) {
      auto gc = Jc[c];
      auto owner = colg2p(gc);
      if (owner == pcol()) {
        auto dest = colg2p(c);
        if (dest != pcol())
          for (int r=0, lc=colg2l(gc); r<lrows_; r++)
            sbuf[dest].push_back(operator()(r, lc));
      }
    }
    auto sreq = new MPI_Request[2*(npcols()-1)];
    auto rreq = sreq + npcols()-1;
    for (int p=0; p<npcols(); p++)
      if (p != pcol()) {
        MPI_Isend(sbuf[p].data(), sbuf[p].size(), mpi_type<scalar_t>(),
                  prow()+p*nprows(), 0, comm(),
                  (p < pcol()) ? sreq+p : sreq+p-1);
        MPI_Irecv(rbuf[p].data(), rbuf[p].size(), mpi_type<scalar_t>(),
                  prow()+p*nprows(), 0, comm(),
                  (p < pcol()) ? rreq+p : rreq+p-1);
      }
    MPI_Waitall(npcols()-1, rreq, MPI_STATUSES_IGNORE);
    std::vector<scalar_t*> prbuf(npcols());
    for (int p=0; p<npcols(); p++) prbuf[p] = rbuf[p].data();
    for (std::size_t c=0; c<Jc.size(); c++) {
      auto gc = Jc[c];
      auto owner = colg2p(gc);
      if (owner == pcol()) continue;
      auto dest = colg2p(c);
      if (dest != pcol()) continue;
      auto tmpc = tmp.colg2l(c);
      for (int r=0; r<lrows_; r++)
        tmp(r, tmpc) = *(prbuf[owner]++);
    }
    MPI_Waitall(npcols()-1, sreq, MPI_STATUSES_IGNORE);
    delete[] sreq;
    return tmp;
  }

  // TODO optimize
  template<typename scalar_t> DistributedMatrix<scalar_t>
  DistributedMatrix<scalar_t>::extract
  (const std::vector<std::size_t>& I,
   const std::vector<std::size_t>& J) const {
    TIMER_TIME(TaskType::DISTMAT_EXTRACT, 1, t_dist_mat_extract);
    return extract_rows(I).extract_cols(J);
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>&
  DistributedMatrix<scalar_t>::add
  (const DistributedMatrix<scalar_t>& B) {
    if (!active()) return *this;
    int rlo, rhi, clo, chi, Brlo, Brhi, Bclo, Bchi;
    lranges(rlo, rhi, clo, chi);
    B.lranges(Brlo, Brhi, Bclo, Bchi);
    int lc = chi - clo;
    int lr = rhi - rlo;
    //#pragma omp taskloop grainsize(64) //collapse(2)
    for (int c=0; c<lc; ++c)
      for (int r=0; r<lr; ++r)
        operator()(r+rlo,c+clo) += B(r+Brlo,c+Bclo);
    STRUMPACK_FLOPS((is_complex<scalar_t>()?2:1)*lc*lr);
    return *this;
  }

  template<typename scalar_t> DistributedMatrix<scalar_t>&
  DistributedMatrix<scalar_t>::scaled_add
  (scalar_t alpha, const DistributedMatrix<scalar_t>& B) {
    if (!active()) return *this;
    assert(grid() == B.grid());
    int rlo, rhi, clo, chi, Brlo, Brhi, Bclo, Bchi;
    lranges(rlo, rhi, clo, chi);
    B.lranges(Brlo, Brhi, Bclo, Bchi);
    int lc = chi - clo;
    int lr = rhi - rlo;
    //#pragma omp taskloop grainsize(64) //collapse(2)
    for (int c=0; c<lc; ++c)
      for (int r=0; r<lr; ++r)
        operator()(r+rlo,c+clo) += alpha * B(r+Brlo,c+Bclo);
    STRUMPACK_FLOPS((is_complex<scalar_t>()?8:2)*lc*lr);
    return *this;
  }

  template<typename scalar_t> typename RealType<scalar_t>::value_type
  DistributedMatrix<scalar_t>::norm() const {
    return normF();
  }

  template<typename scalar_t> typename RealType<scalar_t>::value_type
  DistributedMatrix<scalar_t>::norm1() const {
    if (!active()) return real_t(-1.);
    int IACOL = indxg2p(J(), NB(), pcol(), 0, npcols());
    int Nq0 = scalapack::numroc
      (cols()+ ((J()-1)%NB()), NB(), pcol(), IACOL, npcols());
    real_t* work = new real_t[Nq0];
    auto norm = scalapack::plange
      ('1', rows(), cols(), data(), I(), J(), desc(), work);
    delete[] work;
    return norm;
  }

  template<typename scalar_t> typename RealType<scalar_t>::value_type
  DistributedMatrix<scalar_t>::normI() const {
    if (!active()) return real_t(-1.);
    int IAROW = indxg2p(I(), MB(), prow(), 0, nprows());
    int Mp0 = scalapack::numroc
      (rows()+ ((I()-1)%MB()), MB(), prow(), IAROW, nprows());
    real_t* work = new real_t[Mp0];
    auto norm = scalapack::plange
      ('I', rows(), cols(), data(), I(), J(), desc(), work);
    delete[] work;
    return norm;
  }

  template<typename scalar_t> typename RealType<scalar_t>::value_type
  DistributedMatrix<scalar_t>::normF() const {
    if (!active()) return real_t(-1.);
    real_t* work = nullptr;
    return scalapack::plange
      ('F', rows(), cols(), data(), I(), J(), desc(), work);
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::scatter(const DenseMatrix<scalar_t>& a) {
    if (!active()) return;
    int a_desc[9];
    scalapack::descset
      (a_desc, rows(), cols(), rows(), cols(), 0, 0, ctxt(),
       std::max(std::size_t(rows()), a.ld()));
    scalapack::pgemr2d
      (rows(), cols(), a.data(), 1, 1,
       a_desc, data(), I(), J(), desc(), ctxt());
  }

  /** gather to proc 0,0 in ctxt() */
  template<typename scalar_t> DenseMatrix<scalar_t>
  DistributedMatrix<scalar_t>::gather() const {
    DenseMatrix<scalar_t> a;
    if (!active()) return a;
    if (is_master()) a = DenseMatrix<scalar_t>(rows(), cols());
    int a_desc[9];
    scalapack::descset
      (a_desc, rows(), cols(), rows(), cols(), 0, 0, ctxt(), rows());
    scalapack::pgemr2d
      (rows(), cols(), data(), I(), J(), desc(), a.data(), 1, 1, a_desc, ctxt());
    return a;
  }

  /** gather to all process in ctxt_all() */
  template<typename scalar_t> DenseMatrix<scalar_t>
  DistributedMatrix<scalar_t>::all_gather() const {
    DenseMatrix<scalar_t> a(rows(), cols());
    int a_desc[9];
    scalapack::descset
      (a_desc, rows(), cols(), rows(), cols(),
       0, 0, ctxt_all(), rows());
    scalapack::pgemr2d
      (rows(), cols(), data(), I(), J(), desc(),
       a.data(), 1, 1, a_desc, ctxt_all());
    int all_prows, all_pcols, all_prow, all_pcol;
    scalapack::Cblacs_gridinfo
      (ctxt_all(), &all_prows, &all_pcols, &all_prow, &all_pcol);
    if (all_prow==0 && all_pcol==0)
      scalapack::gebs2d(ctxt_all(), 'A', ' ', rows(), cols(), a.data(), a.ld());
    else scalapack::gebr2d
           (ctxt_all(), 'A', ' ', rows(), cols(), a.data(), a.ld(), 0, 0);
    return a;
  }

  template<typename scalar_t> DenseMatrix<scalar_t>
  DistributedMatrix<scalar_t>::dense_and_clear() {
    DenseMatrix<scalar_t> tmp;
    tmp.data_ = data();
    tmp.rows_ = lrows();
    tmp.cols_ = lcols();
    tmp.ld_ = ld();
    this->data_ = nullptr;
    clear();
    return tmp;
  }

  template<typename scalar_t> DenseMatrix<scalar_t>
  DistributedMatrix<scalar_t>::dense() const {
    DenseMatrix<scalar_t> tmp(lrows(), lcols());
    tmp.ld_ = lrows();
    int rlo, rhi, clo, chi;
    lranges(rlo, rhi, clo, chi);
    for (int c=clo; c<chi; c++)
      for (int r=rlo; r<rhi; r++)
        tmp(r-rlo,c-clo) = operator()(r,c);
    return tmp;
  }

  template<typename scalar_t> DenseMatrixWrapper<scalar_t>
  DistributedMatrix<scalar_t>::dense_wrapper() {
    return DenseMatrixWrapper<scalar_t>(lrows(), lcols(), data(), ld());
  }

  template<typename scalar_t> std::vector<int>
  DistributedMatrix<scalar_t>::LU() {
    if (!active()) return std::vector<int>();
    STRUMPACK_FLOPS(LU_flops(*this));
    std::vector<int> ipiv(lrows()+MB());
    int info = scalapack::pgetrf
      (rows(), cols(), data(), I(), J(), desc(), ipiv.data());
    if (info) {
      std::cerr << "ERROR: LU factorization of DistributedMatrix failed"
                << " with info = " << info << std::endl;
      exit(1);
    }
    return ipiv;
  }

  // Solve a system of linear equations with B as right hand side.
  // assumption: the current matrix should have been factored using LU.
  template<typename scalar_t> DistributedMatrix<scalar_t>
  DistributedMatrix<scalar_t>::solve
  (const DistributedMatrix<scalar_t>& b, const std::vector<int>& piv) const {
    if (!active())
      return DistributedMatrix<scalar_t>(b.grid(), b.rows(), b.cols());
    DistributedMatrix<scalar_t> c(b);
    // TODO in place??, add assertions, check dimensions!!
    if (scalapack::pgetrs
        (char(Trans::N), c.rows(), c.cols(), data(),
         I(), J(), desc(), piv.data(),
         c.data(), c.I(), c.J(), c.desc())) {
      std::cerr << "# ERROR: Failure in PGETRS :(" << std::endl; abort();
    }
    STRUMPACK_FLOPS
      (is_master() ?
       ((is_complex<scalar_t>() ? 4:1) *
        blas::getrs_flops(c.rows(), c.cols())) : 0);
    return c;
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::orthogonalize
  (scalar_t& r_max, scalar_t& r_min) {
    if (!active()) return;
    STRUMPACK_FLOPS(orthogonalize_flops(*this));
    TIMER_TIME(TaskType::QR, 1, t_qr);
    auto minmn = std::min(rows(), cols());
    auto N = J() + minmn - 1;
    auto ltau = scalapack::numroc(N, NB(), pcol(), 0, npcols());
    auto tau = new scalar_t[ltau];
    auto info = scalapack::pgeqrf
      (rows(), minmn, data(), I(), J(), desc(), tau);
    if (lrows() && lcols()) {
      real_t Rmax(std::numeric_limits<real_t>::min());
      real_t Rmin(std::numeric_limits<real_t>::max());
      if (fixed()) {
        for (int gi=0; gi<minmn; gi++) {
          if (is_local_fixed(gi, gi)) {
            auto Rii = std::abs(global_fixed(gi,gi));
            Rmax = std::max(Rmax, Rii);
            Rmin = std::min(Rmin, Rii);
          }
        }
      } else {
        for (int gi=0; gi<minmn; gi++) {
          if (is_local(gi, gi)) {
            auto Rii = std::abs(global(gi,gi));
            Rmax = std::max(Rmax, Rii);
            Rmin = std::min(Rmin, Rii);
          }
        }
      }
      r_max = Rmax;
      r_min = Rmin;
    }
    scalapack::gamx2d
      (ctxt(), 'A', ' ', 1, 1, &r_max, 1, NULL, NULL, -1, -1, -1);
    scalapack::gamn2d
      (ctxt(), 'A', ' ', 1, 1, &r_min, 1, NULL, NULL, -1, -1, -1);
    info = scalapack::pxxgqr
      (rows(), minmn, minmn, data(), I(), J(), desc(), tau);
    if (info) {
      std::cerr << "ERROR: Orthogonalization (pxxgqr) failed with info = "
                << info << std::endl;
      abort();
    }
    if (cols() > rows()) {
      DistributedMatrixWrapper<scalar_t>
        tmp(rows(), cols()-rows(), *this, 0, rows());
      tmp.zero();
    }
    delete[] tau;
  }

  template<typename scalar_t> void DistributedMatrix<scalar_t>::LQ
  (DistributedMatrix<scalar_t>& L, DistributedMatrix<scalar_t>& Q) const {
    if (!active()) return;
    STRUMPACK_FLOPS(LQ_flops(*this));
    assert(I()==1 && J()==1);
    DistributedMatrix<scalar_t> tmp(grid(), std::max(rows(), cols()), cols());
    // TODO this is not a pgemr2d, this does not require communication!!
    strumpack::copy(rows(), cols(), *this, 0, 0, tmp, 0, 0, grid()->ctxt());
    // TODO the last argument to numroc, should it be prows/pcols???
    auto tau = new scalar_t
      [scalapack::numroc(I()+std::min(rows(),cols())-1, MB(),
                         prow(), 0, nprows())];
    scalapack::pgelqf
      (rows(), tmp.cols(), tmp.data(), tmp.I(), tmp.J(),
       tmp.desc(), tau);
    L = DistributedMatrix<scalar_t>(grid(), rows(), rows());
    // TODO this is not a pgemr2d, this does not require communication!!
    strumpack::copy(rows(), rows(), tmp, 0, 0, L, 0, 0, grid()->ctxt());
    // TODO check the diagonal elements
    // auto sfmin = blas::lamch<real_t>('S');
    // for (std::size_t i=0; i<std::min(rows(), cols()); i++)
    //   if (std::abs(L(i, i)) < sfmin) {
    //     std::cerr << "WARNING: small diagonal on L from LQ" << std::endl;
    //     break;
    //   }
    scalapack::pxxglq
      (cols(), cols(), std::min(rows(), cols()),
       tmp.data(), tmp.I(), tmp.J(), tmp.desc(), tau);
    delete[] tau;
    if (tmp.rows() == cols()) Q = std::move(tmp);
    else {
      Q = DistributedMatrix<scalar_t>(grid(), cols(), cols());
      // TODO this is not a pgemr2d, this does not require communication!!
      copy(cols(), cols(), tmp, 0, 0, Q, 0, 0, grid()->ctxt());
    }
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::ID_row
  (DistributedMatrix<scalar_t>& X, std::vector<int>& piv,
   std::vector<std::size_t>& ind, real_t rel_tol, real_t abs_tol,
   const BLACSGrid* grid_T) {
    // transpose the BLACS grid and do a local transpose, then call
    // ID_column, then do local transpose of output X_T to get back in
    // the original blacs grid
    if (!active()) return;
    TIMER_TIME(TaskType::HSS_PARHQRINTERPOL, 1, t_hss_par_hqr);
    assert(I()==1 && J()==1);
    DistributedMatrix<scalar_t> this_T(grid_T, cols(), rows());
    blas::omatcopy('T', lrows(), lcols(), data(), ld(),
                   this_T.data(), this_T.ld());
    DistributedMatrix<scalar_t> X_T;
    this_T.ID_column(X_T, piv, ind, rel_tol, abs_tol);
    X = DistributedMatrix<scalar_t>(grid(), X_T.cols(), X_T.rows());
    blas::omatcopy('T', X_T.lrows(), X_T.lcols(), X_T.data(), X_T.ld(),
                   X.data(), X.ld());
    STRUMPACK_FLOPS(ID_row_flops(*this,X.cols()));
  }

  template<typename scalar_t> void
  DistributedMatrix<scalar_t>::ID_column
  (DistributedMatrix<scalar_t>& X, std::vector<int>& piv,
   std::vector<std::size_t>& ind, real_t rel_tol, real_t abs_tol) {
    if (!active()) return;
    // _J: indices of permuted colums (int iso size_t -> ind)
    std::vector<int> _J(cols());
    std::iota(_J.begin(), _J.end(), 1);
    std::vector<int> gpiv(cols()); // gpiv: column permutation
    std::iota(gpiv.begin(), gpiv.end(), 1);
    int rank = 0;
    // Step 1: RRQR
    scalapack::pgeqpfmod
      (rows(), cols(), data(), I(), J(), desc(),
       _J.data(), gpiv.data(), &rank, rel_tol, abs_tol);
    piv.resize(lcols()+NB());
    ind.resize(rank);
    for (int c=0; c<lcols(); c++) piv[c] = gpiv[coll2g(c)];
    for (int c=0; c<rank; c++) ind[c] = _J[c]-1;
    // Step 2: TRSM and permutation:
    //   R1^-1 R = [I R1^-1 R2] = [I X] with R = [R1 R2], R1 r x r
    DistributedMatrixWrapper<scalar_t> R1(rank, rank, *this, 0, 0);
    X = DistributedMatrix<scalar_t>(grid(), rank, cols()-rank);
    copy(rank, cols()-rank, *this, 0, rank, X, 0, 0, grid()->ctxt());
    trsm(Side::L, UpLo::U, Trans::N, Diag::N, scalar_t(1.), R1, X);
  }

  template<typename scalar_t> void gemm
  (Trans ta, Trans tb, scalar_t alpha, const DistributedMatrix<scalar_t>& A,
   const DistributedMatrix<scalar_t>& B,
   scalar_t beta, DistributedMatrix<scalar_t>& C) {
    if (!A.active()) return;
    assert((ta==Trans::N && A.rows()==C.rows()) ||
           (ta!=Trans::N && A.cols()==C.rows()));
    assert((tb==Trans::N && B.cols()==C.cols()) ||
           (tb!=Trans::N && B.rows()==C.cols()));
    assert((ta==Trans::N && tb==Trans::N && A.cols()==B.rows()) ||
           (ta!=Trans::N && tb==Trans::N && A.rows()==B.rows()) ||
           (ta==Trans::N && tb!=Trans::N && A.cols()==B.cols()) ||
           (ta!=Trans::N && tb!=Trans::N && A.rows()==B.cols()));
    assert(A.I()>=1 && A.J()>=1 && B.I()>=1 &&
           B.J()>=1 && C.I()>=1 && C.J()>=1);
    assert(A.ctxt()==B.ctxt() && A.ctxt()==C.ctxt());
#if 1
    scalapack::pgemm
      (char(ta), char(tb), C.rows(), C.cols(),
       (ta==Trans::N) ? A.cols() : A.rows(), alpha,
       A.data(), A.I(), A.J(), A.desc(),
       B.data(), B.I(), B.J(), B.desc(),
       beta, C.data(), C.I(), C.J(), C.desc());
#else
    if (A.I() != 1 || A.J() != 1) {
      DistributedMatrix<scalar_t> localA(A.ctxt(), A.rows(), A.cols());
      strumpack::copy(A.rows(), A.cols(), A, 0, 0, localA, 0, 0, A.ctxt());
      if (B.I() != 1 || B.J() != 1) {
        DistributedMatrix<scalar_t> localB(B.ctxt(), B.rows(), B.cols());
        strumpack::copy(B.rows(), B.cols(), B, 0, 0, localB, 0, 0, B.ctxt());
        if (C.I() != 1 || C.J() != 1) {
          DistributedMatrix<scalar_t> localC(C.ctxt(), C.rows(), C.cols());
          strumpack::copy(C.rows(), C.cols(), C, 0, 0, localC, 0, 0, C.ctxt());
          //std::cout << "Making 3 copies!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             localA.data(), localA.I(), localA.J(), localA.desc(),
             localB.data(), localB.I(), localB.J(), localB.desc(), beta,
             localC.data(), localC.I(), localC.J(), localC.desc());
          strumpack::copy(C.rows(), C.cols(), localC, 0, 0, C, 0, 0, C.ctxt());
        } else {
          //std::cout << "Making 2 copies (A,B)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             localA.data(), localA.I(), localA.J(), localA.desc(),
             localB.data(), localB.I(), localB.J(), localB.desc(), beta,
             C.data(), C.I(), C.J(), C.desc());
        }
      } else {
        if (C.I() || C.J()) {
          DistributedMatrix<scalar_t> localC(C.ctxt(), C.rows(), C.cols());
          strumpack::copy(C.rows(), C.cols(), C, 0, 0, localC, 0, 0, C.ctxt());
          //std::cout << "Making 2 copies (A,C)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             localA.data(), localA.I(), localA.J(), localA.desc(),
             B.data(), B.I(), B.J(), B.desc(), beta,
             localC.data(), localC.I(), localC.J(), localC.desc());
          strumpack::copy(C.rows(), C.cols(), localC, 0, 0, C, 0, 0, C.ctxt());
        } else {
          //std::cout << "Making 1 copy (A)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             localA.data(), localA.I(), localA.J(), localA.desc(),
             B.data(), B.I(), B.J(), B.desc(), beta,
             C.data(), C.I(), C.J(), C.desc());
        }
      }
    } else {
      if (B.I() != 1 || B.J() != 1) {
        DistributedMatrix<scalar_t> localB(B.ctxt(), B.rows(), B.cols());
        strumpack::copy(B.rows(), B.cols(), B, 0, 0, localB, 0, 0, B.ctxt());
        if (C.I() != 1 || C.J() != 1) {
          DistributedMatrix<scalar_t> localC(C.ctxt(), C.rows(), C.cols());
          strumpack::copy(C.rows(), C.cols(), C, 0, 0, localC, 0, 0, C.ctxt());
          //std::cout << "Making 2 copies (B,C)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             A.data(), A.I(), A.J(), A.desc(),
             localB.data(), localB.I(), localB.J(), localB.desc(), beta,
             localC.data(), localC.I(), localC.J(), localC.desc());
          strumpack::copy(C.rows(), C.cols(), localC, 0, 0, C, 0, 0, C.ctxt());
        } else {
          //std::cout << "Making 1 copy (B)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             A.data(), A.I(), A.J(), A.desc(),
             localB.data(), localB.I(), localB.J(), localB.desc(), beta,
             C.data(), C.I(), C.J(), C.desc());
        }
      } else {
        if (C.I() || C.J()) {
          DistributedMatrix<scalar_t> localC(C.ctxt(), C.rows(), C.cols());
          strumpack::copy(C.rows(), C.cols(), C, 0, 0, localC, 0, 0, C.ctxt());
          //std::cout << "Making 1 copy (C)!!" << std::endl;
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             A.data(), A.I(), A.J(), A.desc(),
             B.data(), B.I(), B.J(), B.desc(), beta,
             localC.data(), localC.I(), localC.J(), localC.desc());
          strumpack::copy(C.rows(), C.cols(), localC, 0, 0, C, 0, 0, C.ctxt());
        } else {
          scalapack::pgemm
            (char(ta), char(tb), C.rows(), C.cols(),
             (ta==Trans::N) ? A.cols() : A.rows(), alpha,
             A.data(), A.I(), A.J(), A.desc(),
             B.data(), B.I(), B.J(), B.desc(), beta,
             C.data(), C.I(), C.J(), C.desc());
        }
      }
    }
#endif
    STRUMPACK_FLOPS(gemm_flops(ta, tb, alpha, A, B, beta));
  }

  template<typename scalar_t> void trsm
  (Side s, UpLo u, Trans ta, Diag d, scalar_t alpha,
   const DistributedMatrix<scalar_t>& A, DistributedMatrix<scalar_t>& B) {
    if (!A.active()) return;
    assert(A.rows()==A.cols());
    assert(s!=Side::L || ta!=Trans::N || A.cols()==B.rows());
    assert(s!=Side::L || ta==Trans::N || A.rows()==B.rows());
    assert(s!=Side::R || ta!=Trans::N || A.rows()==B.cols());
    assert(s!=Side::R || ta==Trans::N || A.cols()==B.cols());
    scalapack::ptrsm
      (char(s), char(u), char(ta), char(d), B.rows(), B.cols(),
       alpha, A.data(), A.I(), A.J(), A.desc(),
       B.data(), B.I(), B.J(), B.desc());
    STRUMPACK_FLOPS(trsm_flops(s, alpha, A, B));
  }

  template<typename scalar_t> void trsv
  (UpLo ul, Trans ta, Diag d, const DistributedMatrix<scalar_t>& A,
   DistributedMatrix<scalar_t>& B) {
    if (!A.active()) return;
    assert(B.cols() == 1 && A.rows() == A.cols() && A.cols() == A.rows());
    scalapack::ptrsv
      (char(ul), char(ta), char(d), A.rows(),
       A.data(), A.I(), A.J(), A.desc(),
       B.data(), B.I(), B.J(), B.desc(), 1);
    // TODO also support row vectors by passing different incb?
    STRUMPACK_FLOPS
      (A.is_master() ?
       ((is_complex<scalar_t>()?4:1) * blas::trsv_flops(A.rows())) : 0);
  }

  template<typename scalar_t> void gemv
  (Trans ta, scalar_t alpha, const DistributedMatrix<scalar_t>& A,
   const DistributedMatrix<scalar_t>& X, scalar_t beta,
   DistributedMatrix<scalar_t>& Y) {
    if (!A.active()) return;
    STRUMPACK_FLOPS(gemv_flops(ta, A, alpha, beta));
    assert(X.cols() == 1 && Y.cols() == 1);
    assert(ta != Trans::N || (A.rows() == Y.rows() && A.cols() == X.rows()));
    assert(ta == Trans::N || (A.cols() == Y.rows() && A.rows() == X.rows()));
    scalapack::pgemv
      (char(ta), A.rows(), A.cols(), alpha,
       A.data(), A.I(), A.J(), A.desc(),
       X.data(), X.I(), X.J(), X.desc(), 1,
       beta, Y.data(), Y.I(), Y.J(), Y.desc(), 1);
    // TODO also support row vectors by passing different incb?
  }

  template<typename scalar_t> DistributedMatrix<scalar_t> vconcat
  (int cols, int arows, int brows, const DistributedMatrix<scalar_t>& a,
   const DistributedMatrix<scalar_t>& b,
   const BLACSGrid* gnew, int context_all) {
    DistributedMatrix<scalar_t> tmp(gnew, arows+brows, cols);
    copy(arows, cols, a, 0, 0, tmp, 0, 0, context_all);
    copy(brows, cols, b, 0, 0, tmp, arows, 0, context_all);
    return tmp;
  }




} // end namespace strumpack

#endif // DISTRIBUTED_MATRIX_HPP
