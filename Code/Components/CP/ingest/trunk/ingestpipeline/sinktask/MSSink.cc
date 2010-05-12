/// @file MSSink.cc
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

// Include own header file first
#include "MSSink.h"

// Include package level header file
#include "askap_cpingest.h"

// ASKAPsoft includes
#include "askap/AskapLogging.h"
#include "askap/AskapError.h"
#include "boost/scoped_ptr.hpp"

// Casecore includes
#include "casa/aips.h"
#include "casa/Quanta.h"
#include "casa/Arrays/Vector.h"
#include "casa/Arrays/Matrix.h"
#include "casa/Arrays/Cube.h"
#include "tables/Tables/TableDesc.h"
#include "tables/Tables/SetupNewTab.h"
#include "tables/Tables/IncrementalStMan.h"
#include "tables/Tables/StandardStMan.h"
#include "tables/Tables/TiledShapeStMan.h"
#include "ms/MeasurementSets/MeasurementSet.h"
#include "ms/MeasurementSets/MSColumns.h"

// Local package includes
#include "ingestpipeline/datadef/VisChunk.h"
#include "ingestutils/AntennaPositions.h"

ASKAP_LOGGER(logger, ".MSSink");

using namespace askap;
using namespace askap::cp;
using namespace casa;

//////////////////////////////////
// Public methods
//////////////////////////////////

MSSink::MSSink(const LOFAR::ParameterSet& parset) :
    itsParset(parset.makeSubset("cp.ingest.ms_sink."))
{
    ASKAPLOG_DEBUG_STR(logger, "Constructor");
    create();
    initAntennas();
    initFeeds();
    initSpws();
    itsMs->addRow();
}

MSSink::~MSSink()
{
    ASKAPLOG_DEBUG_STR(logger, "Destructor");
}

void MSSink::process(VisChunk::ShPtr chunk)
{
    ASKAPLOG_DEBUG_STR(logger, "process()");
}

//////////////////////////////////
// Private methods
//////////////////////////////////

void MSSink::create(void)
{
    // Get configuration first to ensure all parameters are present
    casa::uInt bucketSize = itsParset.getUint32("stman.bucketsize", 1024 * 1024);
    casa::uInt tileNcorr = itsParset.getUint32("stman.tilencorr", 4);
    casa::uInt tileNchan = itsParset.getUint32("stman.tilenchan", 1);
    const std::string filename = itsParset.getString("filename");

    if (bucketSize < 8192) {
        bucketSize = 8192;
    }
    if (tileNcorr < 1) {
        tileNcorr = 1;
    }
    if (tileNchan < 1) {
        tileNchan = 1;
    }

    ASKAPLOG_DEBUG_STR(logger, "Creating dataset " << filename);

    // Make MS with standard columns
    TableDesc msDesc(MS::requiredTableDesc());

    // Add the DATA column.
    MS::addColumnToDesc(msDesc, MS::DATA, 2);

    SetupNewTable newMS(filename, msDesc, Table::New);

    // Set the default Storage Manager to be the Incr one
    {
        IncrementalStMan incrStMan("ismdata", bucketSize);
        newMS.bindAll(incrStMan, True);
    }

    // Bind ANTENNA1, and ANTENNA2 to the standardStMan 
    // as they may change sufficiently frequently to make the
    // incremental storage manager inefficient for these columns.

    {
        StandardStMan ssm("ssmdata", bucketSize);
        newMS.bindColumn(MS::columnName(MS::ANTENNA1), ssm);
        newMS.bindColumn(MS::columnName(MS::ANTENNA2), ssm);
        newMS.bindColumn(MS::columnName(MS::UVW), ssm);
    }

    // These columns contain the bulk of the data so save them in a tiled way
    {
        // Get nr of rows in a tile.
        const int nrowTile = std::max(1u, bucketSize / (8*tileNcorr*tileNchan));
        TiledShapeStMan dataMan("TiledData",
                IPosition(3, tileNcorr, tileNchan, nrowTile));
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::DATA),
                dataMan);
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::FLAG),
                dataMan);
    }
    {
        const int nrowTile = std::max(1u, bucketSize / (4*8));
        TiledShapeStMan dataMan("TiledWeight",
                IPosition(2, 4, nrowTile));
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::SIGMA),
                dataMan);
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::WEIGHT),
                dataMan);
    }

    // Now we can create the MeasurementSet and add the (empty) subtables
    itsMs.reset(new MeasurementSet(newMS, 0));
    itsMs->createDefaultSubtables(Table::New);
    itsMs->flush();

    // Set the TableInfo
    {
        TableInfo& info(itsMs->tableInfo());
        info.setType(TableInfo::type(TableInfo::MEASUREMENTSET));
        info.setSubType(String("simulator"));
        info.readmeAddLine("This is a MeasurementSet Table holding simulated astronomical observations");
    }
}

void MSSink::initAntennas(void)
{
    const LOFAR::ParameterSet antSubset(itsParset.makeSubset("antennas."));

    // Get station name
    const std::string station = antSubset.getString("station", "");
    // Get antenna names and number
    const casa::Vector<std::string> names(antSubset.getStringVector("names"));
    const casa::uInt nAnt = names.size();
    ASKAPCHECK(nAnt > 0, "No antennas defined in parset file");

    // Get antenna positions
    AntennaPositions antPos(antSubset);
    casa::Matrix<double> antXYZ = antPos.getPositionMatrix();

    // Get antenna diameter
    const casa::Double diameter = asQuantity(antSubset.getString("diameter",
                "12m")).getValue("m");
    ASKAPCHECK(diameter > 0.0, "Antenna diameter not positive");

    // Get mount type
    const std::string mount = antSubset.getString("mount", "equatorial");
    ASKAPCHECK((mount == "equatorial") || (mount == "alt-az"), "Antenna mount type unknown");

    // Write the rows to the measurement set
    MSColumns msc(*itsMs);
    MSAntennaColumns& antc = msc.antenna();
    ASKAPCHECK(antc.nrow() == 0, "Antenna table already has data");

    MSAntenna& ant = itsMs->antenna();
    ant.addRow(nAnt);

    antc.type().fillColumn("GROUND-BASED");
    antc.station().fillColumn(station);
    antc.mount().fillColumn(mount);
    antc.flagRow().fillColumn(false);
    antc.dishDiameter().fillColumn(diameter);
    antc.position().putColumn(antXYZ);
    for (unsigned int row = 0; row < nAnt; ++row) {
        antc.name().put(row, names(row));
    }
}

void MSSink::initFeeds(void)
{
}

void MSSink::initSpws(void)
{
}

casa::Quantity MSSink::asQuantity(const std::string& str)
{
    casa::Quantity q;
    casa::Quantity::read(q, str);
    return q;
}

