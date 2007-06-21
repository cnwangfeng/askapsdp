/// @file TableConstDataIterator.cc
///
/// @brief Implementation of IConstDataIterator in the table-based case
/// @details
/// TableConstDataIterator: Allow read-only iteration across preselected data. Each 
/// iteration step is represented by the IConstDataAccessor interface.
/// This is an implementation in the table-based case.
/// 
/// @copyright (c) 2007 CONRAD, All Rights Reserved.
/// @author Max Voronkov <maxim.voronkov@csiro.au>
///

/// casa includes
#include <tables/Tables/ArrayColumn.h>

/// own includes
#include <dataaccess/TableConstDataIterator.h>
#include <conrad/ConradError.h>
#include <dataaccess/DataAccessError.h>


using namespace casa;
using namespace conrad;
using namespace synthesis;

/// @param[in] ms the measurement set to use (as a reference to table)
/// @param[in] sel shared pointer to selector
/// @param[in] conv shared pointer to converter
/// @param[in] maxChunkSize maximum number of rows per accessor
TableConstDataIterator::TableConstDataIterator(const casa::Table &ms,
            const boost::shared_ptr<ITableDataSelectorImpl const> &sel,
            const boost::shared_ptr<IDataConverterImpl const> &conv,
	    casa::uInt maxChunkSize) :
	   itsMS(ms), itsSelector(sel), itsConverter(conv),
	   itsMaxChunkSize(maxChunkSize), itsAccessor(*this)
{ 
  init();
}

/// Restart the iteration from the beginning
void TableConstDataIterator::init()
{ 
  itsCurrentTopRow=0;  
  const casa::TableExprNode &exprNode =
              itsSelector->getTableSelector(itsConverter);
  if (exprNode.isNull()) {
      itsTabIterator=casa::TableIterator(itsMS,"TIME",
	   casa::TableIterator::DontCare,casa::TableIterator::NoSort);
  } else {
      itsTabIterator=casa::TableIterator(itsMS(itsSelector->
                               getTableSelector(itsConverter)),"TIME",
	   casa::TableIterator::DontCare,casa::TableIterator::NoSort);
  }  
  setUpIteration();
}

/// operator* delivers a reference to data accessor (current chunk)
/// @return a reference to the current chunk
const IConstDataAccessor& TableConstDataIterator::operator*() const
{
  return itsAccessor;
}
      
/// Checks whether there are more data available.
/// @return True if there are more data available
casa::Bool TableConstDataIterator::hasMore() const throw()
{
  if (!itsTabIterator.pastEnd()) {
      return true;
  }
  if (itsCurrentTopRow+itsNumberOfRows<itsCurrentIteration.nrow()) {
      return true;
  }   
  return false;
}
      
/// advance the iterator one step further 
/// @return True if there are more data (so constructions like 
///         while(it.next()) {} are possible)
casa::Bool TableConstDataIterator::next()
{
  itsCurrentTopRow+=itsNumberOfRows;
  if (itsCurrentTopRow>=itsCurrentIteration.nrow()) {
      // need to advance table iterator further
      itsTabIterator.next();
      if (!itsTabIterator.pastEnd()) {
          setUpIteration();
      }
      itsCurrentTopRow=0;
  } else {
      uInt remainder=itsCurrentIteration.nrow()-itsCurrentTopRow;
      itsNumberOfRows=remainder<=itsMaxChunkSize ?
                      remainder : itsMaxChunkSize;
      // number of channels/pols are expected to be the same as for the
      // first iteration
      itsAccessor.invalidateAllCaches();
  }  
  return hasMore();
}

/// setup accessor for a new iteration of the table iterator
void TableConstDataIterator::setUpIteration()
{
  itsCurrentIteration=itsTabIterator.table();  
  itsAccessor.invalidateAllCaches();
  itsNumberOfRows=itsCurrentIteration.nrow()<=itsMaxChunkSize ?
                  itsCurrentIteration.nrow() : itsMaxChunkSize;
  // retreive the number of channels and polarizations from the table
  if (itsNumberOfRows) {
      ROArrayColumn<Complex> visCol(itsCurrentIteration,"DATA");
      const casa::IPosition &shape=visCol.shape(0);
      CONRADASSERT(shape.size() && (shape.size()<3));
      itsNumberOfPols=shape[0];
      itsNumberOfChannels=shape.size()>1?shape[1]:1;      
  } else {
      itsNumberOfChannels=0;
      itsNumberOfPols=0;  
  }  
}


/// populate the buffer of visibilities with the values of current
/// iteration
/// @param[inout] vis a reference to the nRow x nChannel x nPol buffer
///            cube to fill with the complex visibility data
void TableConstDataIterator::fillVisibility(casa::Cube<casa::Complex> &vis) const
{
  vis.resize(itsNumberOfRows,itsNumberOfChannels,itsNumberOfPols);
  ROArrayColumn<Complex> visCol(itsCurrentIteration,"DATA");
  // temporary buffer and position in this buffer, declared outside the loop
  IPosition curPos(2,itsNumberOfPols,itsNumberOfChannels);
  Array<Complex> buf(curPos);
  for (uInt row=0;row<itsNumberOfRows;++row) {
       const casa::IPosition &shape=visCol.shape(row);
       CONRADASSERT(shape.size() && (shape.size()<3));
       const casa::uInt thisRowNumberOfPols=shape[0];
       const casa::uInt thisRowNumberOfChannels=shape.size()>1?shape[1]:1;
       if (thisRowNumberOfPols!=itsNumberOfPols) {
           CONRADTHROW(DataAccessError,"Number of polarizations is not "
	               "conformant for row "<<row);           	       
       }
       if (thisRowNumberOfChannels!=itsNumberOfChannels) {
           CONRADTHROW(DataAccessError,"Number of channels is not "
	               "conformant for row "<<row);           	       
       }
       // for now just copy. In the future we will pass this array through
       // the transformation which will do averaging, selection,
       // polarization conversion

       // extract data record for this row, no resizing
       visCol.get(row+itsCurrentTopRow,buf,False); 
       
       for (uInt chan=0;chan<itsNumberOfChannels;++chan) {
            curPos[1]=chan;
            for (uInt pol=0;pol<itsNumberOfPols;++pol) {
	         curPos[0]=pol;
	         vis(row,chan,pol)=buf(curPos);
	    }
       }
  }
}

/// populate the buffer with uvw
/// @param[in] uvw a reference to vector of rigid vectors (3 elemets,
///            u,v and w for each row) to fill
void TableConstDataIterator::fillUVW(casa::Vector<casa::RigidVector<casa::Double, 3> >&uvw) const
{
  uvw.resize(itsNumberOfRows);

  ROArrayColumn<Double> uvwCol(itsCurrentIteration,"UVW");
  // temporary buffer and position in it
  IPosition curPos(1,3);
  Array<Double> buf(curPos);
  for (uInt row=0;row<itsNumberOfRows;++row) {
       const casa::IPosition &shape=uvwCol.shape(row);
       CONRADASSERT(shape.size()==1);
       CONRADASSERT(shape[0]==3);
       // extract data record for this row, no resizing     
       uvwCol.get(row+itsCurrentTopRow,buf,False);
       RigidVector<Double, 3> &thisRowUVW=uvw(row);
       for (curPos[0]=0;curPos[0]<3;++curPos[0]) {
            thisRowUVW(curPos[0])=buf(curPos);
       }
  }
}