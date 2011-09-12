/// @file
/// @brief implementation of the calibration solution accessor returning cached values
/// @details This class is very similar to CachedCalSolutionAccessor and perhaps should have
/// used that name. It supports all calibration products (i.e. gains, bandpasses and leakages) 
/// and stores them in a compact structure like casa::Cube suitable for table-based implementation
/// (unlike CachedCalSolutionAccessor which uses named parameters). The downside of this approach is 
/// that maximum number of antennas and beams should be known in advance (or an expensive re-shape 
/// operation should be implemented, which is not done at the moment). Note, that the actual 
/// resizing of the cache is done in the method which fills the cache (i.e. methods of solution
/// source), rather than inside this class. This class is intended to be 
/// used in the table-based implementation of the calibration solution interface.
///
/// @copyright (c) 2011 CSIRO
/// Australia Telescope National Facility (ATNF)
/// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
/// PO Box 76, Epping NSW 1710, Australia
/// atnf-enquiries@csiro.au
///
/// This file is part of the ASKAP software distribution.
///
/// The ASKAP software distribution is free software: you can redistribute it
/// and/or modify it under the terms of the GNU General Public License as
/// published by the Free Software Foundation; either version 2 of the License,
/// or (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
///
/// @author Max Voronkov <Maxim.Voronkov@csiro.au>

#include <calibaccess/MemCalSolutionAccessor.h>
#include <askap/AskapError.h>

namespace askap {

namespace accessors {

/// @brief obtain gains (J-Jones)
/// @details This method retrieves parallel-hand gains for both 
/// polarisations (corresponding to XX and YY). If no gains are defined
/// for a particular index, gains of 1. with invalid flags set are
/// returned.
/// @param[in] index ant/beam index 
/// @return JonesJTerm object with gains and validity flags
JonesJTerm MemCalSolutionAccessor::gain(const JonesIndex &index) const
{
  const std::pair<casa::Complex, casa::Bool> g1 = extractFromCube(itsGains, 0, index);
  const std::pair<casa::Complex, casa::Bool> g2 = extractFromCube(itsGains, 1, index);
  return JonesJTerm(g1.first, g1.second, g2.first, g2.second);
}
   
/// @brief obtain leakage (D-Jones)
/// @details This method retrieves cross-hand elements of the 
/// Jones matrix (polarisation leakages). There are two values
/// (corresponding to XY and YX) returned (as members of JonesDTerm 
/// class). If no leakages are defined for a particular index,
/// zero leakages are returned with invalid flags set. 
/// @param[in] index ant/beam index
/// @return JonesDTerm object with leakages and validity flags
JonesDTerm MemCalSolutionAccessor::leakage(const JonesIndex &index) const
{
  const std::pair<casa::Complex, casa::Bool> d12 = extractFromCube(itsLeakages, 0, index);
  const std::pair<casa::Complex, casa::Bool> d21 = extractFromCube(itsGains, 1, index);
  return JonesDTerm(d12.first, d12.second, d21.first, d21.second);  
}
   
/// @brief obtain bandpass (frequency dependent J-Jones)
/// @details This method retrieves parallel-hand spectral
/// channel-dependent gain (also known as bandpass) for a
/// given channel and antenna/beam. The actual implementation
/// does not necessarily store these channel-dependent gains
/// in an array. It could also implement interpolation or 
/// sample a polynomial fit at the given channel (and 
/// parameters of the polynomial could be in the database). If
/// no bandpass is defined (at all or for this particular channel),
/// gains of 1.0 are returned (with invalid flag is set).
/// @param[in] index ant/beam index
/// @param[in] chan spectral channel of interest
/// @return JonesJTerm object with gains and validity flags
JonesJTerm MemCalSolutionAccessor::bandpass(const JonesIndex &index, const casa::uInt chan) const
{ 
  const std::pair<casa::Complex, casa::Bool> g1 = extractFromCube(itsBandpasses, 2 * chan, index);
  const std::pair<casa::Complex, casa::Bool> g2 = extractFromCube(itsBandpasses, 2 * chan + 1, index);
  return JonesJTerm(g1.first, g1.second, g2.first, g2.second);
}

/// @brief set gains (J-Jones)
/// @details This method writes parallel-hand gains for both 
/// polarisations (corresponding to XX and YY)
/// @param[in] index ant/beam index 
/// @param[in] gains JonesJTerm object with gains and validity flags
void MemCalSolutionAccessor::setGain(const JonesIndex &index, const JonesJTerm &gains)
{
  setInCube(itsGains, std::pair<casa::Complex, casa::Bool>(gains.g1(),gains.g1IsValid()), 0, index);
  setInCube(itsGains, std::pair<casa::Complex, casa::Bool>(gains.g2(),gains.g2IsValid()), 1, index);  
}
   
/// @brief set leakages (D-Jones)
/// @details This method writes cross-pol leakages  
/// (corresponding to XY and YX)
/// @param[in] index ant/beam index 
/// @param[in] leakages JonesDTerm object with leakages and validity flags
void MemCalSolutionAccessor::setLeakage(const JonesIndex &index, const JonesDTerm &leakages)
{
  setInCube(itsLeakages, std::pair<casa::Complex, casa::Bool>(leakages.d12(),leakages.d12IsValid()), 0, index);
  setInCube(itsLeakages, std::pair<casa::Complex, casa::Bool>(leakages.d21(),leakages.d21IsValid()), 1, index);
}
   
/// @brief set gains for a single bandpass channel
/// @details This method writes parallel-hand gains corresponding to a single
/// spectral channel (i.e. one bandpass element).
/// @param[in] index ant/beam index 
/// @param[in] bp JonesJTerm object with gains for the given channel and validity flags
/// @param[in] chan spectral channel
/// @note We may add later variants of this method assuming that the bandpass is
/// approximated somehow, e.g. by a polynomial. For simplicity, for now we deal with 
/// gains set explicitly for each channel.
void MemCalSolutionAccessor::setBandpass(const JonesIndex &index, const JonesJTerm &bp, const casa::uInt chan)
{
  setInCube(itsBandpasses, std::pair<casa::Complex, casa::Bool>(bp.g1(),bp.g1IsValid()), chan * 2, index);
  setInCube(itsBandpasses, std::pair<casa::Complex, casa::Bool>(bp.g2(),bp.g2IsValid()), chan * 2 + 1, index);  
} 

/// @details helper method to extract value and validity flag for a given ant/beam pair
/// @param[in] cube const reference to a cube
/// @param[in] row polarisation/channel index (row of the cube)
/// @param[in] index ant/beam index
/// @return value/validity flag pair
std::pair<casa::Complex, casa::Bool> MemCalSolutionAccessor::extractFromCube(const casa::Cube<std::pair<casa::Complex, casa::Bool> > &cube,
                   const casa::uInt row, const JonesIndex &index)
{
  ASKAPDEBUGASSERT(row < cube.nrow());
  const casa::Short ant = index.antenna();
  const casa::Short beam = index.beam();
  
  ASKAPCHECK((ant >= 0) && (casa::uInt(ant) < cube.ncolumn()), "Requested antenna index "<<ant<<" is outside the shape of the cache: "<<cube.shape());
  ASKAPCHECK((beam >= 0) && (casa::uInt(beam) < cube.nplane()), "Requested beam index "<<beam<<" is outside the shape of the cache: "<<cube.shape());
  return cube(row,casa::uInt(ant),casa::uInt(beam));  
}                   

/// @details helper method to set the value and validity flag for a given ant/beam pair
/// @param[in] cube non-const reference to a cube
/// @param[in] val const reference to the value/validity flag pair
/// @param[in] row polarisation/channel index (row of the cube)
/// @param[in] index ant/beam index
void MemCalSolutionAccessor::setInCube(casa::Cube<std::pair<casa::Complex, casa::Bool> > &cube,
                   const std::pair<casa::Complex, casa::Bool> &val,
                   const casa::uInt row, const JonesIndex &index)
{
  ASKAPDEBUGASSERT(row < cube.nrow());
  const casa::Short ant = index.antenna();
  const casa::Short beam = index.beam();
  
  ASKAPCHECK((ant >= 0) && (casa::uInt(ant) < cube.ncolumn()), "Requested antenna index "<<ant<<" is outside the shape of the cache: "<<cube.shape());
  ASKAPCHECK((beam >= 0) && (casa::uInt(beam) < cube.nplane()), "Requested beam index "<<beam<<" is outside the shape of the cache: "<<cube.shape());
  cube(row,casa::uInt(ant),casa::uInt(beam)) = val;
}

} // namespace accessors

} // namespace askap


