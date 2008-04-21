/// @file
///
/// @brief A component representing an unpolarized gaussian source with the 
//  flat spectrum
/// @details
///     This is an implementation of IComponent for the gaussian source model.
///     The source is assumed unpolarized with a flat spectrum
///     (i.e. spectral index 0).
///
/// @copyright (c) 2007 ASKAP, All Rights Reserved.
/// @author Max Voronkov <maxim.voronkov@csiro.au>

#ifndef UNPOLARIZED_GAUSSIAN_SOURCE_H
#define UNPOLARIZED_GAUSSIAN_SOURCE_H

// own includes
#include <measurementequation/UnpolarizedComponent.h>

// std includes
#include <string>

namespace askap {

namespace synthesis {

/// @brief A component representing an unpolarized gaussian source with the 
/// flat spectrum
/// @details
///     This is an implementation of IComponent for the gaussian source model.
///     The source is assumed unpolarized with a flat spectrum
///     (i.e. spectral index 0).
/// @ingroup measurementequation  
struct UnpolarizedGaussianSource : public UnpolarizedComponent<6> {

  /// @brief construct the point source component
  /// @details 
  /// @param[in] name a name of the component. Will be added to all parameter
  ///            names (e.g. after direction.ra) 
  /// @param[in] flux flux density in Jy
  /// @param[in] ra offset in right ascension w.r.t. the current phase 
  /// centre (in radians)
  /// @param[in] dec offset in declination w.r.t. the current phase
  /// centre (in radians)
  /// @param[in] maj major axis in radians
  /// @param[in] min minor axis in radians
  /// @param[in] pa  position angle in radians
  UnpolarizedGaussianSource(const std::string &name, double flux, double ra, 
           double dec, double maj, double min, double pa);
  
  /// @brief calculate stokes I visibilities for this component
  /// @details This variant of the method calculates just the visibilities
  /// (without derivatives) for a number of frequencies. This method is 
  /// used to in the implementation of the IComponent interface if 
  /// stokes I is requested. Otherwise result is filled with 0.
  /// @param[in] uvw  baseline spacings (in metres)
  /// @param[in] freq vector of frequencies to do calculations for
  /// @param[out] result an output buffer used to store values
  virtual void calculate(const casa::RigidVector<casa::Double, 3> &uvw,
                    const casa::Vector<casa::Double> &freq,
                    std::vector<double> &result) const;
  
  /// @brief calculate stokes I visibilities and derivatives for this component
  /// @details This variant of the method does simultaneous calculations of
  /// the values and derivatives. This method is used to in the implementation
  /// of the IComponent interface if stokes I is requested. Otherwise result
  /// is filled with 0.
  /// @param[in] uvw  baseline spacings (in metres)
  /// @param[in] freq vector of frequencies to do calculations for
  /// @param[out] result an output buffer used to store values
  virtual void calculate(const casa::RigidVector<casa::Double, 3> &uvw,
                    const casa::Vector<casa::Double> &freq,
                    std::vector<casa::AutoDiff<double> > &result) const; 
private:
  
  /// @brief actual calculations
  /// @details templated method for actual calculations. Made private, because
  /// it is used and declared in UnpolarizedGaussianSource.cc only
  /// @param[in] uvw  baseline spacings (in metres)
  /// @param[in] freq vector of frequencies to do calculations for
  /// @param[in] params RigidVector with parameters
  /// @param[out] result an output buffer used to store values
  template<typename T>
  static void calcGaussian(const casa::RigidVector<casa::Double, 3> &uvw,
                    const casa::Vector<casa::Double> &freq,
                    const casa::RigidVector<T, 6> &params,
                    std::vector<T> &result);
};

} // namespace synthesis

} // namespace askap



#endif // #ifndef UNPOLARIZED_GAUSSIAN_SOURCE_H

