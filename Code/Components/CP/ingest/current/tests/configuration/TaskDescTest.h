/// @file TaskDescTest.cc
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
/// @author Ben Humphreys <ben.humphreys@csiro.au>

// CPPUnit includes
#include <cppunit/extensions/HelperMacros.h>

// Support classes
#include <string>

// Classes to test
#include "configuration/TaskDesc.h"

using namespace std;

namespace askap {
namespace cp {
namespace ingest {

class TaskDescTest : public CppUnit::TestFixture {
        CPPUNIT_TEST_SUITE(TaskDescTest);
        CPPUNIT_TEST(testAll);
        CPPUNIT_TEST_SUITE_END();

    public:
        void setUp() {
        };

        void tearDown() {
        }

        void testAll() {
            const string name = "CalcUVWTask";
            const TaskDesc::Type type = TaskDesc::CalcUVWTask;
            LOFAR::ParameterSet parset;
            parset.add("a_key", "a_value");

            TaskDesc instance(name, type, parset);
            CPPUNIT_ASSERT_EQUAL(name, instance.name());
            CPPUNIT_ASSERT_EQUAL(type, instance.type());
            CPPUNIT_ASSERT_EQUAL(parset.size(), instance.parset().size());
        };
};

}   // End namespace ingest
}   // End namespace cp
}   // End namespace askap
