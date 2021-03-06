/// @file TypedValueMapConstMapper.h
///
/// @copyright (c) 2010 CSIRO
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
/// @author Ben Humphreys <ben.humphreys@csiro.au>

#ifndef ASKAP_CP_ICEWRAPPER_TYPEDVALUEMAPCONSTMAPPER_H
#define ASKAP_CP_ICEWRAPPER_TYPEDVALUEMAPCONSTMAPPER_H

// System includes
#include <string>
#include <vector>

// ASKAPsoft includes
#include "casa/aips.h"
#include "measures/Measures/MDirection.h"

// CP ice interfaces
#include "TypedValues.h"

namespace askap {
namespace cp {
namespace icewrapper {

/// @brief Used to map between an askap::interfaces::TypedValueMap instance
/// and native Casa types.
///
/// This class provides read-only access to the TypedValueMap. If read/write
/// access is required the TypedValueMapMapper may be used.
/// 
/// @ingroup tosmetadata
class TypedValueMapConstMapper {
    public:
        /// @brief Constructor
        /// @param[in] map  the TypedValueMap this mapper maps from/to
        TypedValueMapConstMapper(const askap::interfaces::TypedValueMap& map);

        /// @brief Destructor.
        virtual ~TypedValueMapConstMapper() {};
        
        virtual casa::Int getInt(const std::string& key) const;
        virtual casa::Long getLong(const std::string& key) const;
        virtual casa::String getString(const std::string& key) const;
        virtual casa::Bool getBool(const std::string& key) const;
        virtual casa::Float getFloat(const std::string& key) const;
        virtual casa::Double getDouble(const std::string& key) const;
        virtual casa::Complex getFloatComplex(const std::string& key) const;
        virtual casa::DComplex getDoubleComplex(const std::string& key) const;
        virtual casa::MDirection getDirection(const std::string& key) const;

        virtual std::vector<casa::Int> getIntSeq(const std::string& key) const;
        virtual std::vector<casa::Long> getLongSeq(const std::string& key) const;
        virtual std::vector<casa::String> getStringSeq(const std::string& key) const;
        virtual std::vector<casa::Bool> getBoolSeq(const std::string& key) const;
        virtual std::vector<casa::Float> getFloatSeq(const std::string& key) const;
        virtual std::vector<casa::Double> getDoubleSeq(const std::string& key) const;
        virtual std::vector<casa::Complex> getFloatComplexSeq(const std::string& key) const;
        virtual std::vector<casa::DComplex> getDoubleComplexSeq(const std::string& key) const;
        virtual std::vector<casa::MDirection> getDirectionSeq(const std::string& key) const;

    private:
        // Template params are:
        /// @brief Parse the value of element identified by "key" and return in
        /// the appropriate type.
        ///
        /// @tparam T        Native (or casa) type
        /// @tparam TVType   Enum value from TypedValueType enum
        /// @tparam TVPtr    TypedValue pointer type
        ///
        /// @return the value for map element identified by the parameter "key".
        template <class T, askap::interfaces::TypedValueType TVType, class TVPtr>
        T get(const std::string& key) const;

        /// @brief Convert a TypedValue (Slice) direction type to a
        /// casa::MDirection
        casa::MDirection convertDirection(const askap::interfaces::Direction& dir) const;

        /// @brief The TypedValueMap this mapper maps from/to.
        const askap::interfaces::TypedValueMap itsConstMap;
};

}
}
}

#endif
