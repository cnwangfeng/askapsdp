/// @file MsSplitApp.cc
///
/// @copyright (c) 2012 CSIRO
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

// Package level header file
#include "askap_pipelinetasks.h"

// Include own header file
#include "mssplit/MsSplitApp.h"

// System includes
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <limits>
#include <stdint.h>

// ASKAPsoft includes
#include "askap/AskapError.h"
#include "askap/AskapLogging.h"
#include "askap/Application.h"
#include "askap/AskapUtil.h"
#include "askap/StatReporter.h"
#include "askap/Log4cxxLogSink.h"
#include "boost/shared_ptr.hpp"
#include "Common/ParameterSet.h"
#include "casa/OS/File.h"
#include "casa/aips.h"
#include "casa/Arrays/IPosition.h"
#include "casa/Arrays/Slicer.h"
#include "casa/Arrays/Array.h"
#include "casa/Arrays/Vector.h"
#include "casa/Arrays/Cube.h"
#include "casa/Quanta/MVTime.h"
#include "tables/Tables/TableDesc.h"
#include "tables/Tables/SetupNewTab.h"
#include "tables/Tables/IncrementalStMan.h"
#include "tables/Tables/StandardStMan.h"
#include "tables/Tables/TiledShapeStMan.h"
#include "ms/MeasurementSets/MeasurementSet.h"
#include "ms/MeasurementSets/MSColumns.h"

// Local package includes
#include "mssplit/ParsetUtils.h"

ASKAP_LOGGER(logger, ".mssplitapp");

using namespace askap;
using namespace askap::cp::pipelinetasks;
using namespace casa;
using namespace std;

MsSplitApp::MsSplitApp()
    : itsTimeBegin(std::numeric_limits<double>::min()),
    itsTimeEnd(std::numeric_limits<double>::max())
{
}

boost::shared_ptr<casa::MeasurementSet> MsSplitApp::create(
    const std::string& filename, const casa::Bool addSigmaSpec,
    casa::uInt bucketSize, casa::uInt tileNcorr, casa::uInt tileNchan)
{
    if (bucketSize < 8192) bucketSize = 8192;

    if (tileNcorr < 1) tileNcorr = 1;

    if (tileNchan < 1) tileNchan = 1;

    ASKAPLOG_INFO_STR(logger, "Creating dataset " << filename);

    // Make MS with standard columns
    TableDesc msDesc(MS::requiredTableDesc());

    // Add the DATA column.
    MS::addColumnToDesc(msDesc, MS::DATA, 2);

    // Add the SIGMA_SPECTRUM column?
    if (addSigmaSpec) {
        MS::addColumnToDesc(msDesc, MS::SIGMA_SPECTRUM, 2);
    }

    SetupNewTable newMS(filename, msDesc, Table::New);

    // Set the default Storage Manager to be the Incr one
    {
        IncrementalStMan incrStMan("ismdata", bucketSize);
        newMS.bindAll(incrStMan, True);
    }

    // Bind ANTENNA1, and ANTENNA2 to the StandardStMan
    // as they may change sufficiently frequently to make the
    // incremental storage manager inefficient for these columns.
    {
        // NOTE: The addition of the FEED columns here is a bit unusual.
        // While the FEED columns are perfect candidates for the incremental
        // storage manager, for some reason doing so results in a huge
        // increase in I/O to the file (see ticket: 4094 for details).
        StandardStMan ssm("ssmdata", bucketSize);
        newMS.bindColumn(MS::columnName(MS::ANTENNA1), ssm);
        newMS.bindColumn(MS::columnName(MS::ANTENNA2), ssm);
        newMS.bindColumn(MS::columnName(MS::FEED1), ssm);
        newMS.bindColumn(MS::columnName(MS::FEED2), ssm);
        newMS.bindColumn(MS::columnName(MS::UVW), ssm);
    }

    // These columns contain the bulk of the data so save them in a tiled way
    {
        // Get nr of rows in a tile.
        const int bytesPerRow = sizeof(std::complex<float>) * tileNcorr * tileNchan;
        const int nrowTile = std::max(1u, bucketSize / bytesPerRow);
        TiledShapeStMan dataMan("TiledData",
                                IPosition(3, tileNcorr, tileNchan, nrowTile));
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::DATA),
                         dataMan);
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::FLAG),
                         dataMan);
        if (addSigmaSpec) {
            newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::SIGMA_SPECTRUM),
                             dataMan);
        }
    }
    {
        const int bytesPerRow = sizeof(float) * tileNcorr;
        const int nrowTile = std::max(1u, bucketSize / bytesPerRow);
        TiledShapeStMan dataMan("TiledWeight",
                                IPosition(2, tileNcorr, nrowTile));
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::SIGMA),
                         dataMan);
        newMS.bindColumn(MeasurementSet::columnName(MeasurementSet::WEIGHT),
                         dataMan);
    }

    // Now we can create the MeasurementSet and add the (empty) subtables
    boost::shared_ptr<casa::MeasurementSet> ms(new MeasurementSet(newMS, 0));
    ms->createDefaultSubtables(Table::New);
    ms->flush();

    // Set the TableInfo
    {
        TableInfo& info(ms->tableInfo());
        info.setType(TableInfo::type(TableInfo::MEASUREMENTSET));
        info.setSubType(String(""));
        info.readmeAddLine("This is a MeasurementSet Table holding simulated astronomical observations");
    }
    return ms;
}

void MsSplitApp::copyAntenna(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSAntennaColumns& sc = srcMsc.antenna();

    MSColumns destMsc(dest);
    MSAntennaColumns& dc = destMsc.antenna();

    // Add new rows to the destination and copy the data
    dest.antenna().addRow(sc.nrow());

    dc.name().putColumn(sc.name());
    dc.station().putColumn(sc.station());
    dc.type().putColumn(sc.type());
    dc.mount().putColumn(sc.mount());
    dc.position().putColumn(sc.position());
    dc.dishDiameter().putColumn(sc.dishDiameter());
    dc.flagRow().putColumn(sc.flagRow());
}

void MsSplitApp::copyDataDescription(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSDataDescColumns& sc = srcMsc.dataDescription();

    MSColumns destMsc(dest);
    MSDataDescColumns& dc = destMsc.dataDescription();

    // Add new rows to the destination and copy the data
    dest.dataDescription().addRow(sc.nrow());

    dc.flagRow().putColumn(sc.flagRow());
    dc.spectralWindowId().putColumn(sc.spectralWindowId());
    dc.polarizationId().putColumn(sc.polarizationId());
}

void MsSplitApp::copyFeed(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSFeedColumns& sc = srcMsc.feed();

    MSColumns destMsc(dest);
    MSFeedColumns& dc = destMsc.feed();

    // Add new rows to the destination and copy the data
    dest.feed().addRow(sc.nrow());

    dc.antennaId().putColumn(sc.antennaId());
    dc.feedId().putColumn(sc.feedId());
    dc.spectralWindowId().putColumn(sc.spectralWindowId());
    dc.beamId().putColumn(sc.beamId());
    dc.numReceptors().putColumn(sc.numReceptors());
    dc.position().putColumn(sc.position());
    dc.beamOffset().putColumn(sc.beamOffset());
    dc.polarizationType().putColumn(sc.polarizationType());
    dc.polResponse().putColumn(sc.polResponse());
    dc.receptorAngle().putColumn(sc.receptorAngle());
    dc.time().putColumn(sc.time());
    dc.interval().putColumn(sc.interval());
}

void MsSplitApp::copyField(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSFieldColumns& sc = srcMsc.field();

    MSColumns destMsc(dest);
    MSFieldColumns& dc = destMsc.field();

    // Add new rows to the destination and copy the data
    dest.field().addRow(sc.nrow());

    dc.name().putColumn(sc.name());
    dc.code().putColumn(sc.code());
    dc.time().putColumn(sc.time());
    dc.numPoly().putColumn(sc.numPoly());
    dc.sourceId().putColumn(sc.sourceId());
    dc.delayDir().putColumn(sc.delayDir());
    dc.phaseDir().putColumn(sc.phaseDir());
    dc.referenceDir().putColumn(sc.referenceDir());
}

void MsSplitApp::copyObservation(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSObservationColumns& sc = srcMsc.observation();

    MSColumns destMsc(dest);
    MSObservationColumns& dc = destMsc.observation();

    // Add new rows to the destination and copy the data
    dest.observation().addRow(sc.nrow());

    dc.timeRange().putColumn(sc.timeRange());
    //dc.log().putColumn(sc.log());
    //dc.schedule().putColumn(sc.schedule());
    dc.flagRow().putColumn(sc.flagRow());
    dc.observer().putColumn(sc.observer());
    dc.telescopeName().putColumn(sc.telescopeName());
    dc.project().putColumn(sc.project());
    dc.releaseDate().putColumn(sc.releaseDate());
    dc.scheduleType().putColumn(sc.scheduleType());
}

void MsSplitApp::copyPointing(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSPointingColumns& sc = srcMsc.pointing();

    MSColumns destMsc(dest);
    MSPointingColumns& dc = destMsc.pointing();

    // Add new rows to the destination and copy the data
    dest.pointing().addRow(sc.nrow());

    // Create and copy the POLANGLE column, if it exists.
    // This non-standard column captures the third (role) axis position.
    // casabrowser row order changes when it's copy at the end, so do it here.
    if (source.pointing().actualTableDesc().isColumn("POLANGLE")) {
        const MSPointing& srcPointing = source.pointing();
        const casa::ROScalarColumn<casa::Float> srcPolAngleCol(srcPointing, "POLANGLE");
        MSPointing& destPointing = dest.pointing();
        destPointing.addColumn(srcPointing.actualTableDesc().columnDesc("POLANGLE"));
        casa::ScalarColumn<casa::Float> destPolAngleCol(destPointing, "POLANGLE");
        destPolAngleCol.putColumn(srcPolAngleCol);
    }

    // Copy required columns

    // These two copies were disabled due to a problem with them hanging.
    // This not longer seems to be a problem, so the copies are re-enabled.
    dc.direction().putColumn(sc.direction());
    dc.target().putColumn(sc.target());

    dc.antennaId().putColumn(sc.antennaId());
    dc.interval().putColumn(sc.interval());
    dc.name().putColumn(sc.name());
    dc.numPoly().putColumn(sc.numPoly());
    dc.time().putColumn(sc.time());
    dc.timeOrigin().putColumn(sc.timeOrigin());
    dc.tracking().putColumn(sc.tracking());

}

void MsSplitApp::copyPolarization(const casa::MeasurementSet& source, casa::MeasurementSet& dest)
{
    const ROMSColumns srcMsc(source);
    const ROMSPolarizationColumns& sc = srcMsc.polarization();

    MSColumns destMsc(dest);
    MSPolarizationColumns& dc = destMsc.polarization();

    // Add new rows to the destination and copy the data
    dest.polarization().addRow(sc.nrow());

    dc.flagRow().putColumn(sc.flagRow());
    dc.numCorr().putColumn(sc.numCorr());
    dc.corrType().putColumn(sc.corrType());
    dc.corrProduct().putColumn(sc.corrProduct());
}

casa::Int MsSplitApp::findSpectralWindowId(const casa::MeasurementSet& ms)
{
    const ROMSColumns msc(ms);
    const casa::uInt nrows = msc.nrow();
    ASKAPCHECK(nrows > 0, "No rows in main table");
    const casa::ROMSDataDescColumns& ddc = msc.dataDescription();

    casa::Int r0 = -1; // Row zero SpWindow id

    for (casa::uInt row = 0; row < nrows; ++row) {
        const casa::Int dataDescId = msc.dataDescId()(row);
        const casa::Int spwId = ddc.spectralWindowId()(dataDescId);

        if (row == 0) {
            r0 = spwId;
        } else {
            ASKAPCHECK(spwId == r0, "All rows must be of the same spectral window");
        }
    }

    return r0;
}

void MsSplitApp::splitSpectralWindow(const casa::MeasurementSet& source,
        casa::MeasurementSet& dest,
        const uint32_t startChan,
        const uint32_t endChan,
        const uint32_t width,
        const casa::Int spwId)
{
    MSColumns destCols(dest);
    const ROMSColumns srcCols(source);

    MSSpWindowColumns& dc = destCols.spectralWindow();
    const ROMSSpWindowColumns& sc = srcCols.spectralWindow();
    const casa::Int srow = spwId;
    const casa::Int drow = dc.nrow();

    dest.spectralWindow().addRow();

    // 1: Copy over the simple cells (i.e. those not needing splitting/averaging)
    dc.measFreqRef().put(drow, sc.measFreqRef()(srow));
    dc.refFrequency().put(drow, sc.refFrequency()(srow));
    dc.flagRow().put(drow, sc.flagRow()(srow));
    dc.freqGroup().put(drow, sc.freqGroup()(srow));
    dc.freqGroupName().put(drow, sc.freqGroupName()(srow));
    dc.ifConvChain().put(drow, sc.ifConvChain()(srow));
    dc.name().put(drow, sc.name()(srow));
    dc.netSideband().put(drow, sc.netSideband()(srow));

    // 2: Now process each source measurement set, building up the arrays
    const uInt nChanIn = endChan - startChan + 1;
    const uInt nChanOut = nChanIn / width;
    vector<double> chanFreq;
    vector<double> chanWidth;
    vector<double> effectiveBW;
    vector<double> resolution;
    chanFreq.resize(nChanOut);
    chanWidth.resize(nChanOut);
    effectiveBW.resize(nChanOut);
    resolution.resize(nChanOut);
    double totalBandwidth = 0.0;

    for (uInt destChan = 0; destChan < nChanOut; ++destChan) {
        chanFreq[destChan] = 0.0;
        chanWidth[destChan] = 0.0;
        effectiveBW[destChan] = 0.0;
        resolution[destChan] = 0.0;

        // The offset for the first input channel for this destination channel
        const uInt chanOffset = startChan - 1 + (destChan * width);

        for (uInt i = chanOffset; i < chanOffset + width; ++i) {
            chanFreq[destChan] += sc.chanFreq()(srow)(casa::IPosition(1, i));
            chanWidth[destChan] += sc.chanWidth()(srow)(casa::IPosition(1, i));
            effectiveBW[destChan] += sc.effectiveBW()(srow)(casa::IPosition(1, i));
            resolution[destChan] += sc.resolution()(srow)(casa::IPosition(1, i));
            totalBandwidth += sc.chanWidth()(srow)(casa::IPosition(1, i));
        }

        // Finally average chanFreq
        chanFreq[destChan] = chanFreq[destChan] / width;
    }

    // 3: Add those splitting/averaging cells
    dc.numChan().put(drow, nChanOut);
    dc.chanFreq().put(drow, casa::Vector<double>(chanFreq));
    dc.chanWidth().put(drow, casa::Vector<double>(chanWidth));
    dc.effectiveBW().put(drow, casa::Vector<double>(effectiveBW));
    dc.resolution().put(drow, casa::Vector<double>(resolution));
    dc.totalBandwidth().put(drow, totalBandwidth);
}

bool MsSplitApp::rowFiltersExist() const
{
    return !itsBeams.empty() || !itsScans.empty() || !itsFieldIds.empty()
        || itsTimeBegin > std::numeric_limits<double>::min()
        || itsTimeEnd < std::numeric_limits<double>::max();
}

bool MsSplitApp::rowIsFiltered(uint32_t scanid, uint32_t fieldid,
                               uint32_t feed1, uint32_t feed2,
                               double time) const
{
    // Include all rows if no filters exist
    if (!rowFiltersExist()) return false;

    if (time < itsTimeBegin || time > itsTimeEnd) return true;

    if (!itsScans.empty() && itsScans.find(scanid) == itsScans.end()) return true;

    if (!itsFieldIds.empty() && itsFieldIds.find(fieldid) == itsFieldIds.end()) return true;

    if (!itsBeams.empty() &&
            itsBeams.find(feed1) == itsBeams.end() &&
            itsBeams.find(feed2) == itsBeams.end()) {
        return true;
    }

    return false;
}

void MsSplitApp::splitMainTable(const casa::MeasurementSet& source,
                                casa::MeasurementSet& dest,
                                const uint32_t startChan,
                                const uint32_t endChan,
                                const uint32_t width)
{
    // Pre-conditions
    ASKAPDEBUGASSERT(endChan >= startChan);
    ASKAPDEBUGASSERT((endChan - startChan + 1) % width == 0);

    const ROMSColumns sc(source);
    MSColumns dc(dest);

    // Add rows upfront if no row based filters exist
    const casa::uInt nRows = sc.nrow();
    if (!rowFiltersExist()) dest.addRow(nRows);

    // Work out how many channels are to be actual input and which output
    // and how many polarisations are involved.
    const uInt nChanIn = endChan - startChan + 1;
    const uInt nChanOut = nChanIn / width;
    const uInt nPol = sc.data()(0).shape()(0);
    ASKAPDEBUGASSERT(nPol > 0);

    // Test to see whether SIGMA_SPECTRUM has been added
    casa::Bool haveInSigmaSpec = source.isColumn(MS::SIGMA_SPECTRUM);
    casa::Bool haveOutSigmaSpec = dest.isColumn(MS::SIGMA_SPECTRUM);
    if (haveInSigmaSpec) {
        ASKAPLOG_INFO_STR(logger, "Reading and using the spectra of sigma values");
    }
    if (haveOutSigmaSpec) {
        ASKAPLOG_INFO_STR(logger, "Calculating and storing spectra of sigma values");
    }

    // Decide how many rows to process simultaneously. This needs to fit within
    // a reasonable amount of memory, because all visibilities will be read
    // in for possible averaging. Assumes 32MB working space.
    std::size_t inDataSize = sizeof(casa::Complex) + sizeof(casa::Bool);
    std::size_t outDataSize = inDataSize;
    if (haveInSigmaSpec) {
        inDataSize += sizeof(casa::Float);
    }
    if (haveOutSigmaSpec) {
        outDataSize += sizeof(casa::Float);
    }
    uInt maxSimultaneousRows = (32 * 1024 * 1024) / nPol /
            (nChanIn * inDataSize) / (nChanOut * outDataSize);
    if (maxSimultaneousRows<1) maxSimultaneousRows = 1;

    // However, if there is row-based filtering only one row can be copied
    // at a time.
    if (rowFiltersExist()) maxSimultaneousRows = 1;

    // Set a 64MB maximum cache size for the large columns
    const casa::uInt cacheSize = 64 * 1024 * 1024;
    sc.data().setMaximumCacheSize(cacheSize);
    dc.data().setMaximumCacheSize(cacheSize);
    sc.flag().setMaximumCacheSize(cacheSize);
    dc.flag().setMaximumCacheSize(cacheSize);
    if (haveInSigmaSpec) {
        sc.sigmaSpectrum().setMaximumCacheSize(cacheSize);
    }
    if (haveOutSigmaSpec) {
        dc.sigmaSpectrum().setMaximumCacheSize(cacheSize);
    }

    uInt progressCounter = 0; // Used for progress reporting
    const uInt PROGRESS_INTERVAL_IN_ROWS = nRows / 100;

    // Row in destination table may differ from source table if row based
    // filtering is used
    uInt dstRow = 0;
    uInt row = 0;
    while (row < nRows) {
        // Number of rows to process for this iteration of the loop; either
        // maxSimultaneousRows or the remaining rows.
        const uInt nRowsThisIteration = min(maxSimultaneousRows, nRows - row);
        const Slicer srcrowslicer(IPosition(1, row), IPosition(1, nRowsThisIteration),
                Slicer::endIsLength);
        Slicer dstrowslicer = srcrowslicer;

        // Report progress at intervals and on completion
        progressCounter += nRowsThisIteration;
        if (progressCounter >= PROGRESS_INTERVAL_IN_ROWS ||
                (row >= nRows - 1)) {
            ASKAPLOG_INFO_STR(logger,  "Processed row " << row + 1 << " of " << nRows);
            progressCounter = 0;
        }
        
        // Debugging for chunk copying only
        if (nRowsThisIteration > 1) {
            ASKAPLOG_DEBUG_STR(logger,  "Processing " << nRowsThisIteration
                    << " rows this iteration");
        }

        // Skip this row if it is filtered out
        if (rowIsFiltered(sc.scanNumber()(row),
                    sc.fieldId()(row),
                    sc.feed1()(row),
                    sc.feed2()(row),
                    sc.time()(row))) {
            row += nRowsThisIteration;
            continue;
        }

        // Rows have been pre-added if no row based filtering is done 
        if (rowFiltersExist()) {
            dest.addRow();
            dstrowslicer = Slicer(IPosition(1, dstRow), IPosition(1, nRowsThisIteration),
                    Slicer::endIsLength);
        }

        // Copy over the simple cells (i.e. those not needing averaging/merging)
        dc.scanNumber().putColumnRange(dstrowslicer, sc.scanNumber().getColumnRange(srcrowslicer));
        dc.fieldId().putColumnRange(dstrowslicer, sc.fieldId().getColumnRange(srcrowslicer));
        dc.dataDescId().putColumnRange(dstrowslicer, sc.dataDescId().getColumnRange(srcrowslicer));
        dc.time().putColumnRange(dstrowslicer, sc.time().getColumnRange(srcrowslicer));
        dc.timeCentroid().putColumnRange(dstrowslicer, sc.timeCentroid().getColumnRange(srcrowslicer));
        dc.arrayId().putColumnRange(dstrowslicer, sc.arrayId().getColumnRange(srcrowslicer));
        dc.processorId().putColumnRange(dstrowslicer, sc.processorId().getColumnRange(srcrowslicer));
        dc.exposure().putColumnRange(dstrowslicer, sc.exposure().getColumnRange(srcrowslicer));
        dc.interval().putColumnRange(dstrowslicer, sc.interval().getColumnRange(srcrowslicer));
        dc.observationId().putColumnRange(dstrowslicer, sc.observationId().getColumnRange(srcrowslicer));
        dc.antenna1().putColumnRange(dstrowslicer, sc.antenna1().getColumnRange(srcrowslicer));
        dc.antenna2().putColumnRange(dstrowslicer, sc.antenna2().getColumnRange(srcrowslicer));
        dc.feed1().putColumnRange(dstrowslicer, sc.feed1().getColumnRange(srcrowslicer));
        dc.feed2().putColumnRange(dstrowslicer, sc.feed2().getColumnRange(srcrowslicer));
        dc.uvw().putColumnRange(dstrowslicer, sc.uvw().getColumnRange(srcrowslicer));
        dc.flagRow().putColumnRange(dstrowslicer, sc.flagRow().getColumnRange(srcrowslicer));
        dc.weight().putColumnRange(dstrowslicer, sc.weight().getColumnRange(srcrowslicer));
        dc.sigma().putColumnRange(dstrowslicer, sc.sigma().getColumnRange(srcrowslicer)/sqrt(width));

        // Set the shape of the destination arrays
        for (uInt i = dstRow; i < dstRow + nRowsThisIteration; ++i) {
            dc.data().setShape(i, IPosition(2, nPol, nChanOut));
            dc.flag().setShape(i, IPosition(2, nPol, nChanOut));
            if (haveOutSigmaSpec) {
                dc.sigmaSpectrum().setShape(i, IPosition(2, nPol, nChanOut));
            }
        }

        //  Average (if applicable) then write data into the output MS
        const Slicer srcarrslicer(IPosition(2, 0, startChan - 1),
                                  IPosition(2, nPol, nChanIn), Slicer::endIsLength);
        const Slicer destarrslicer(IPosition(2, 0, 0),
                                   IPosition(2, nPol, nChanOut), Slicer::endIsLength);

        if (width == 1) {
            dc.data().putColumnRange(dstrowslicer, destarrslicer,
                sc.data().getColumnRange(srcrowslicer, srcarrslicer));
            dc.flag().putColumnRange(dstrowslicer, destarrslicer,
                sc.flag().getColumnRange(srcrowslicer, srcarrslicer));
            if (haveInSigmaSpec && haveOutSigmaSpec) {
                dc.sigmaSpectrum().putColumnRange(dstrowslicer, destarrslicer,
                    sc.sigmaSpectrum().getColumnRange(srcrowslicer, srcarrslicer));
            }
        } else {
            // Get (read) the input data/flag/sigma
            const casa::Cube<casa::Complex> indata = sc.data().getColumnRange(srcrowslicer, srcarrslicer);
            const casa::Cube<casa::Bool> inflag = sc.flag().getColumnRange(srcrowslicer, srcarrslicer);
            // This is only needed if generating sigmaSpectra, but that should be the
            // case with width>1, and this avoids testing in the tight loops below
            casa::Cube<casa::Float> insigma;
            if (haveInSigmaSpec) {
                insigma = sc.sigmaSpectrum().getColumnRange(srcrowslicer, srcarrslicer);
            } else {
                // There's only 1 sigma per pol & row, so spread over channels
                insigma = casa::Cube<casa::Float>(indata.shape());
                casa::IPosition arrayShape(3, nPol,1,nRowsThisIteration);
                casa::Array<casa::Float> sigmaArray =
                    sc.sigma().getColumnRange(srcrowslicer).reform(arrayShape);
                for (uInt i = 0; i < nChanIn; ++i) {
                    const Slicer blockSlicer(IPosition(3, 0,i,0),
                                             arrayShape, Slicer::endIsLength);
                    insigma(blockSlicer) = sigmaArray;
                }
            }

            // Create the output data/flag/sigma
            casa::Cube<casa::Complex> outdata(nPol, nChanOut, nRowsThisIteration);
            casa::Cube<casa::Bool> outflag(nPol, nChanOut, nRowsThisIteration);
            // This is only needed if generating sigmaSpectra, but that should be the
            // case with width>1, and this avoids testing in the tight loops below
            casa::Cube<casa::Float> outsigma(nPol, nChanOut, nRowsThisIteration); 

            // Average data and combine flag information
            for (uInt pol = 0; pol < nPol; ++pol) {
                for (uInt destChan = 0; destChan < nChanOut; ++destChan) {
                    for (uInt r = 0; r < nRowsThisIteration; ++r) {
                        casa::Complex sum(0.0, 0.0);
                        casa::Float varsum = 0.0; 
                        casa::uInt sumcount = 0; 

                        // Starting at the appropriate offset into the source data, average "width"
                        // channels together
                        for (uInt i = (destChan * width); i < (destChan * width) + width; ++i) {
                            ASKAPDEBUGASSERT(i < nChanIn);
                            if (inflag(pol, i, r)) continue;
                            sum += indata(pol, i, r);
                            varsum += insigma(pol, i, r) * insigma(pol, i, r);
                            sumcount++;
                        }

                        // Now the input channels have been averaged, write the data to
                        // the output cubes
                        if (sumcount > 0) { 
                            outdata(pol, destChan, r) = casa::Complex(sum.real() / sumcount,
                                                                      sum.imag() / sumcount);
                            outflag(pol, destChan, r) = false;
                            outsigma(pol, destChan, r) = sqrt(varsum) / sumcount;
                        } else {
                            outflag(pol, destChan, r) = true;
                        }

                    }
                }
            }

            // Put (write) the output data/flag
            dc.data().putColumnRange(dstrowslicer, destarrslicer, outdata);
            dc.flag().putColumnRange(dstrowslicer, destarrslicer, outflag);
            if (haveOutSigmaSpec) {
                dc.sigmaSpectrum().putColumnRange(dstrowslicer, destarrslicer, outsigma);
            }
        }

        row += nRowsThisIteration;
        dstRow += nRowsThisIteration;
    }
}

int MsSplitApp::split(const std::string& invis, const std::string& outvis,
                      const uint32_t startChan,
                      const uint32_t endChan,
                      const uint32_t width,
                      const LOFAR::ParameterSet& parset)
{
    ASKAPLOG_INFO_STR(logger,  "Splitting out channel range " << startChan << " to "
                          << endChan << " (inclusive)");

    if (width > 1) {
        ASKAPLOG_INFO_STR(logger,  "Averaging " << width << " channels to form 1");
    } else {
        ASKAPLOG_INFO_STR(logger,  "No averaging");
    }

    // Verify split parameters
    const uInt nChanIn = endChan - startChan + 1;

    if ((width < 1) || (nChanIn % width != 0)) {
        ASKAPLOG_ERROR_STR(logger, "Width must equally divide the channel range");
        return 1;
    }

    // Open the input measurement set
    const casa::MeasurementSet in(invis);

    // Verify split parameters that require input MS info
    const casa::uInt totChanIn = ROScalarColumn<casa::Int>(in.spectralWindow(),"NUM_CHAN")(0);
    if ((startChan<1) || (endChan > totChanIn)) {
        ASKAPLOG_ERROR_STR(logger,
            "Input channel range is inconsistent with input spectra: ["<<
            startChan<<","<<endChan<<"] is outside [1,"<<totChanIn<<"]");
        return 1;
    }

    // Create the output measurement set
    if (casa::File(outvis).exists()) {
        ASKAPLOG_ERROR_STR(logger, "File or table " << outvis << " already exists!");
        return 1;
    }

    // Add a sigma spectrum to the output measurement set?
    casa::Bool addSigmaSpec = false;
    if ((width > 1) || in.isColumn(MS::SIGMA_SPECTRUM)) {
        addSigmaSpec = true;
    }

    const casa::uInt bucketSize = parset.getUint32("stman.bucketsize", 64 * 1024);
    const casa::uInt tileNcorr = parset.getUint32("stman.tilencorr", 4);
    const casa::uInt tileNchan = parset.getUint32("stman.tilenchan", 1);

    boost::shared_ptr<casa::MeasurementSet>
        out(create(outvis, addSigmaSpec, bucketSize, tileNcorr, tileNchan));

    // Copy ANTENNA
    ASKAPLOG_INFO_STR(logger,  "Copying ANTENNA table");
    copyAntenna(in, *out);

    // Copy DATA_DESCRIPTION
    ASKAPLOG_INFO_STR(logger,  "Copying DATA_DESCRIPTION table");
    copyDataDescription(in, *out);

    // Copy FEED
    ASKAPLOG_INFO_STR(logger,  "Copying FEED table");
    copyFeed(in, *out);

    // Copy FIELD
    ASKAPLOG_INFO_STR(logger,  "Copying FIELD table");
    copyField(in, *out);

    // Copy OBSERVATION
    ASKAPLOG_INFO_STR(logger,  "Copying OBSERVATION table");
    copyObservation(in, *out);

    // Copy POINTING
    ASKAPLOG_INFO_STR(logger,  "Copying POINTING table");
    copyPointing(in, *out);

    // Copy POLARIZATION
    ASKAPLOG_INFO_STR(logger,  "Copying POLARIZATION table");
    copyPolarization(in, *out);

    // Get the spectral window id (must be common for all main table rows)
    const casa::Int spwId = findSpectralWindowId(in);

    // Split SPECTRAL_WINDOW
    ASKAPLOG_INFO_STR(logger,  "Splitting SPECTRAL_WINDOW table");
    splitSpectralWindow(in, *out, startChan, endChan, width, spwId);

    // Split main table
    ASKAPLOG_INFO_STR(logger,  "Splitting main table");
    splitMainTable(in, *out, startChan, endChan, width);

    return 0;
}

void MsSplitApp::configureTimeFilter(const std::string& key, const std::string& msg,
                                 double& var)
{
    if (config().isDefined(key)) {
        const string ts = config().getString(key);
        casa::Quantity tq;
        if(!casa::MVTime::read(tq, ts)) {
            ASKAPTHROW(AskapError, "Unable to convert " << ts << " to MVTime");
        }

        const casa::MVTime t(tq);
        var = t.second();
        ASKAPLOG_INFO_STR(logger, msg << ts << " (" << var << " sec)");
    }
}

std::vector<uint32_t> MsSplitApp::configureFieldNameFilter(
                          const std::vector<std::string>& names,
                          const std::string invis)
{
    std::vector<uint32_t> fieldIds;
    if (!names.empty()) {
        const casa::MeasurementSet in(invis);
        const ROMSColumns srcMsc(in);
        const ROMSFieldColumns& sc = srcMsc.field();
        const casa::Vector<casa::String> fieldNames = sc.name().getColumn();
        // Step through each field and find IDs for the filter.
        // Could set fieldIds in the following loop, but this seems easier.
        for (uInt i = 0; i < sc.nrow(); ++i) {
            if (find(names.begin(), names.end(), (std::string)fieldNames[i]) !=
                     names.end()) {
                fieldIds.push_back(i);
            }
        }
        // print a warning for any missing fields
        for (uInt i = 0; i < names.size(); ++i) {
            if (find(fieldNames.begin(), fieldNames.end(),
                       (casa::String)names[i]) == fieldNames.end()) {
                ASKAPLOG_WARN_STR(logger, "  cannot find field name " <<
                    names[i] << " in ms "<< invis);
            }
        }
    }
    if (fieldIds.empty()) {
        ASKAPTHROW(AskapError, "Cannot find any of the field names " <<
            names << " in ms "<< invis);
    }
    return fieldIds;
}

int MsSplitApp::run(int argc, char* argv[])
{
    StatReporter stats;

    // Get the required parameters to split
    const string invis = config().getString("vis");
    const string outvis = config().getString("outputvis");

    // Read channel selection parameters
    const pair<uint32_t, uint32_t> range = ParsetUtils::parseIntRange(config(), "channel");
    const uint32_t width = config().getUint32("width", 1);

    // Read beam selection parameters
    if (config().isDefined("beams")) {
        const vector<uint32_t> v = config().getUint32Vector("beams", true);
        itsBeams.insert(v.begin(), v.end());
        ASKAPLOG_INFO_STR(logger, "Including ONLY beams: " << v);
    }

    // Read scan id selection parameters
    if (config().isDefined("scans")) {
        const vector<uint32_t> v = config().getUint32Vector("scans", true);
        itsScans.insert(v.begin(), v.end());
        ASKAPLOG_INFO_STR(logger, "Including ONLY scan numbers: " << v);
    }

    // Read field name selection parameters
    if (config().isDefined("fieldnames")) {
        const vector<string> names = config().getStringVector("fieldnames", true);
        ASKAPLOG_INFO_STR(logger, "Including ONLY fields with names: " << names);
        const vector<uint32_t> v = configureFieldNameFilter(names,invis);
        itsFieldIds.insert(v.begin(), v.end());
        ASKAPLOG_INFO_STR(logger, "  fields: " << v);
    }

    // Read time range selection parameters
    configureTimeFilter("timebegin", "Excluding rows with time less than: ", itsTimeBegin);
    configureTimeFilter("timeend", "Excluding rows with time greater than: ", itsTimeEnd);

    const int error = split(invis, outvis, range.first, range.second, width, config());
    stats.logSummary();
    return error;
}
