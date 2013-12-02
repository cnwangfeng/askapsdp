/// @file FrtCommunicator.cc
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
/// @author Max Voronkov <maxim.voronkov@csiro.au>

// Local package includes
#include "ingestpipeline/phasetracktask/FrtCommunicator.h"

// Include package level header file
#include "askap_cpingest.h"

// ASKAPsoft includes
#include "askap/AskapLogging.h"
#include "askap/AskapError.h"
#include "askap/AskapUtil.h"

// std includes
#include <string>
#include <map>

// casa includes
#include <casa/BasicSL/String.h>

ASKAP_LOGGER(logger, ".FrtCommunicator");

namespace askap {
namespace cp {
namespace ingest {

/// @brief Constructor.
/// @param[in] parset the configuration parameter set.
/// @param[in] config configuration
FrtCommunicator::FrtCommunicator(const LOFAR::ParameterSet& parset, const Configuration& config) :
       itsCyclesToWait(parset.getUint32("cycles2skip",5u))
{
   const std::vector<Antenna> antennas = config.antennas();
   const size_t nAnt = antennas.size();
   itsAntennaStatuses = std::vector<AntennaFlagStatus>(nAnt, ANT_UNINITIALISED);
   itsAntennaRequestIDs.resize(nAnt);
   itsRequestCompletedTimes.resize(nAnt);
   itsAntennaNames.resize(nAnt);
   itsRequestedDRxDelays.resize(nAnt);
   itsRequestedFRPhaseRates.resize(nAnt);
   itsRequestedFRPhaseSlopes.resize(nAnt);
   itsRequestedFRPhaseOffsets.resize(nAnt);
   for (size_t i = 0; i < nAnt; ++i) {
        itsAntennaNames[i] = casa::downcase(antennas.at(i).name());
   }
   
   const std::string locatorHost = parset.getString("ice.locator_host");
   const std::string locatorPort = parset.getString("ice.locator_port");
   const std::string topicManager = parset.getString("icestorm.topicmanager");
   const std::string outtopic = parset.getString("icestorm.outtopic");
   const std::string intopic = parset.getString("icestorm.intopic");
   const std::string adapterName = "FrtCommunicator";
   const int bufSize = 24;
   
   ASKAPLOG_INFO_STR(logger, "Fringe rotator communicator constructor is setup for "<<nAnt<<
                             " antennas, ice topics: "<<outtopic<<" and "<<intopic);

   itsOutPort.reset(new icewrapper::FrtMetadataOutputPort(locatorHost, locatorPort, topicManager, outtopic));
   itsInPort.reset(new FrtMetadataSource(locatorHost, locatorPort, topicManager, intopic, adapterName, bufSize));
   ASKAPDEBUGASSERT(itsOutPort);
   ASKAPDEBUGASSERT(itsInPort);   
}

/// @brief get requested DRx delay
/// @param[in] ant antenna index 
/// @return DRx delay setting
int FrtCommunicator::requestedDRxDelay(const casa::uInt ant) const
{
  ASKAPASSERT(ant < itsRequestedDRxDelays.size());
  return itsRequestedDRxDelays[ant];
}
    
/// @brief get requested FR phase rate
/// @param[in] ant antenna index 
/// @return FR phase rate (in hardware units)
int FrtCommunicator::requestedFRPhaseRate(const casa::uInt ant) const
{
  ASKAPASSERT(ant < itsRequestedFRPhaseRates.size());
  return itsRequestedFRPhaseRates[ant];
}


/// @brief get requested FR phase frequency slope
/// @param[in] ant antenna index 
/// @return FR phase frequency slope (in hardware units)
int FrtCommunicator::requestedFRPhaseSlope(const casa::uInt ant) const
{
  ASKAPASSERT(ant < itsRequestedFRPhaseSlopes.size());
  return itsRequestedFRPhaseSlopes[ant];
}


/// @brief get requested FR phase offset
/// @param[in] ant antenna index
/// @return FR phase offset (in hardware units)
int FrtCommunicator::requestedFRPhaseOffset(const casa::uInt ant) const
{
  ASKAPASSERT(ant < itsRequestedFRPhaseOffsets.size());
  return itsRequestedFRPhaseOffsets[ant];
}

/// @brief test if antenna produces valid data
/// @param[in] ant antenna index
/// @return true, if the given antenna produces valid data
bool FrtCommunicator::isValid(const casa::uInt ant) const
{
  ASKAPASSERT(ant < itsAntennaStatuses.size());
  return itsAntennaStatuses[ant] == ANT_VALID;
}

/// @brief signal of the new time stamp
/// @details Without asynchronous thread, the current implementation relies on this method
/// being called every cycle. It manages time outs and flags/unflags antennas as necessary.
void FrtCommunicator::newTimeStamp(const casa::MVEpoch &epoch)
{
  itsCurrentEpoch = epoch;
  // first check any requests waiting for completion
  const double timeOut = 5.* itsCyclesToWait;
  for (size_t ant = 0; ant < itsAntennaStatuses.size(); ++ant) {
       if (itsAntennaStatuses[ant] == ANT_BEING_UPDATED) {
           const casa::MVEpoch timeSince = epoch - itsRequestCompletedTimes[ant];
           if (timeSince.getTime("s").getValue() >= timeOut) {
               ASKAPLOG_INFO_STR(logger, "Requested changes to FR parameters are now expected to be in place for "<<itsAntennaNames[ant]<<
                                 ", unflagging the antenna");
               itsAntennaStatuses[ant] == ANT_VALID;
           }
       }
  } 
  
  // now check whether there are any new reply messages in the queue waiting to be analysed
  for (boost::shared_ptr<std::map<std::string,int> > reply = itsInPort->next(0); reply; reply = itsInPort->next(0)) {
      ASKAPDEBUGASSERT(reply); 
      const std::map<std::string,int>::const_iterator ci = reply->find("id");
      if (ci != reply->end()) {
          const int reqID = ci->second;
          for (size_t ant = 0; ant<itsAntennaRequestIDs.size(); ++ant) {
               if (itsAntennaRequestIDs[ant] == reqID) {
                   itsAntennaRequestIDs[ant] = -1;          
                   if (itsCyclesToWait > 0) {
                       ASKAPLOG_INFO_STR(logger, "Requested changes to FR parameters have been applied for "<<itsAntennaNames[ant]<<
                                     " waiting "<<itsCyclesToWait<<" cycles before unflagging it");
                       itsAntennaStatuses[ant] = ANT_BEING_UPDATED;
                       itsRequestCompletedTimes[ant] = epoch;
                   } else {
                       // do not wait
                       ASKAPLOG_INFO_STR(logger, "Requested changes to FR parameters are now expected to be in place for "<<itsAntennaNames[ant]<<
                                 ", unflagging the antenna");
                       itsAntennaStatuses[ant] = ANT_VALID;
                   }
                   break;
               }
          }          
      } else {      
          ASKAPLOG_WARN_STR(logger, "id key is missing in the frt reply message");
      }
  }
} 

/// @brief request DRx delay
/// @param[in] ant antenna index
/// @param[in] int delay setting (in the units required by hardware)
void FrtCommunicator::requestDRxDelay(const casa::uInt ant, const int delay)
{
   ASKAPASSERT(ant < itsAntennaNames.size());
   std::map<std::string, int> msg;
   msg[itsAntennaNames[ant]+".drx_delay"] = delay;
   const int id = tagMessage(msg);
   itsRequestedDRxDelays[ant] = delay;
   itsAntennaRequestIDs[ant] = id;
   itsAntennaStatuses[ant] = ANT_DRX_REQUESTED;
   // send the message
   itsOutPort->send(msg);
}

/// @brief helper method to tag a message with time-based ID
/// @details We need to be able to track which requests are completed and when. It is done
/// by passing an ID string which is buffered per antenna. When reply is received, the post-processing
/// actions are finalised. This method forms an ID based on current epoch, tags the message and
/// returns the id
/// @param[in] msg message to update
/// @return assigned ID (same as msg["id"])
int FrtCommunicator::tagMessage(std::map<std::string, int> &msg) const
{
  const long timeInSeconds = static_cast<long>(itsCurrentEpoch.getTime("s").getValue());
  const int tag = static_cast<int>(timeInSeconds & 0xffffffff);
  msg["id"] = tag;
  return tag;
}



} // namespace ingest 
} // namespace cp 
} // namespace askap 


