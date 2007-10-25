/// @file
/// 
/// @brief Singular value decomposition acting on GSL matrix/vector
/// @details This file contains a function, which acts as a replacement
/// of the GSL's gsl_linalg_SV_decomp by having the same interface.
/// It uses the SVD code from SVDecompose.h instead of the GSL. 
/// I hope that eventually this file will be dropped, as either the GSL
/// will be fixed or the code will be rewritten to completely avoid using GSL.
///
/// @copyright (c) 2007 CONRAD, All Rights Reserved.
/// @author Max Voronkov <maxim.voronkov@csiro.au>

// own includes
#include <fitting/GSLSVDReplacement.h>
#include <conrad/ConradError.h>
#include <conrad/IndexedLess.h>

// main SVD code, taken from another project
#include <fitting/SVDecompose.h>

// std includes
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <numeric>


namespace conrad {

namespace utility {
/// @brief a helper object function to populate an array of indices
/// @details An instance of this class is initialized with an initial
/// number, which are returned in the first call. All subsequent calls
/// result in an incremented value. (result = initial + (number_of_call-1);)
template<typename T>
struct Counter {
   typedef T result_type;
   /// @brief constructor
   /// @details initialize the counter with the initial value (default is 0)
   /// @param[in] val initial value
   Counter(const T &val = T(0)) : itsValue(val) {}
   
   /// @brief next value
   /// @details return current value and increment it
   T operator()() const { return itsValue++; }
private:
    /// current value
    mutable T itsValue;
};

} // namespace utility

namespace scimath {


/// @brief main method - do SVD (in a symmetric case)
/// @details The routine does decomposition: A=UWV^T
/// @param[in] A a matrix to decompose (gsl_matrix)
/// @param[out] V a matrix with eigenvectors
/// @param[out] S a vector with singular values
void SVDecomp(gsl_matrix *A, gsl_matrix *V, gsl_vector *S)
{
  // this is a test and probably temporary code to replace GSL's svd
  // routine with that stolen from another project (in SVDecompose.h)
  // this adapter method does additional copying (i.e. the goal is
  // quick implementation rather than a performance)
  CONRADDEBUGASSERT(A!=NULL);
  CONRADDEBUGASSERT(V!=NULL);
  CONRADDEBUGASSERT(S!=NULL);
 
  std::vector<double> MatrixABuffer;
  svd::Matrix2D<std::vector<double> > MatrixA(MatrixABuffer,A->size1,A->size2);
  
  std::vector<double> MatrixVBuffer;
  svd::Matrix2D<std::vector<double> > MatrixV(MatrixVBuffer);
  
  std::vector<double> VectorS;
  for (int row=0;row<MatrixA.nrow();++row) {
       for (int col=0;col<MatrixA.ncol();++col) {
            MatrixA(row,col)=gsl_matrix_get(A,row,col);
       }
  }
  try {
     computeSVD(MatrixA,VectorS,MatrixV);
  }
  catch(const std::string &str) {
      CONRADTHROW(ConradError, "SVD failed to converge: "<<str);
  }
  CONRADDEBUGASSERT(MatrixV.nrow() == V->size1);
  CONRADDEBUGASSERT(MatrixV.ncol() == V->size2);
  // we need to sort singular values to get them in the descending order
  // with appropriate permutations of the A and V matrices
  std::vector<size_t> VectorSIndices(VectorS.size());
  std::generate(VectorSIndices.begin(), VectorSIndices.end(), 
                utility::Counter<size_t>());
  std::sort(VectorSIndices.begin(), VectorSIndices.end(), 
            not2(utility::indexedLess(VectorS.begin())));
  
  CONRADDEBUGASSERT(VectorS.size() == VectorSIndices.size());
  for (int row=0;row<MatrixV.nrow();++row) {
       for (int col=0;col<MatrixV.ncol();++col) {
            gsl_matrix_set(V,row,col,MatrixV(row,VectorSIndices[col]));
       }
  }
  CONRADDEBUGASSERT(VectorS.size() == S->size);
  for (int item=0;item<VectorS.size();++item) {
       gsl_vector_set(S,item,VectorS[VectorSIndices[item]]);
  }
  for (int row=0;row<MatrixA.nrow();++row) {
       for (int col=0;col<MatrixA.ncol();++col) {
            gsl_matrix_set(A,row,col,MatrixA(row,VectorSIndices[col]));
       }
  }
}


} // namespace scimath

} // namespace conrad