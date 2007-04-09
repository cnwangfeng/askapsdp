/// @file
///
/// MEParamsRep: represent a set of parameters for an imaging equation.
///
/// @copyright (c) 2007 CONRAD, All Rights Reserved.
/// @author Tim Cornwell <tim.cornwell@csiro.au>
///
#ifndef MEPARAMSREP_H_
#define MEPARAMSREP_H_

#include <map>
#include <string>
#include <ostream>

using std::string;
using std::map;

namespace conrad {
namespace synthesis
{

template <class T>
class MEParamsRep {
public:

	MEParamsRep();
	
	MEParamsRep& operator=(const MEParamsRep& other);
	
	MEParamsRep(const MEParamsRep& other);
			
	/// Add an Parameter
	/// @param name Name of param to be added
	/// @param ip Param to be added
	void add(const string& name, const T& ip);

	/// Update an Parameter
	/// @param name Name of param to be updated
	/// @param ip Param to be updated
	void update(const string& name, const T& ip);
	
	/// Fix a parameter
	void fix(const string& name);

	/// Free a parameter
	void free(const string& name);

    /// Is this parameter free?
	bool isFree(const string& name);
	
	// Return number of values
	const uint size() const;
	
	/// Return the parameter with this name
	/// @param name Name of param
	const T& value(const string& name) const;		
	T& value(const string& name);
	
	bool has(const string& name) const;

	/// Is this set congruent with another?
	bool isCongruent(const MEParamsRep& other) const;
	
	/// Reset to empty
	void reset();
private:
	mutable map<string, T> itsValues;
	mutable map<string, bool> itsFree;
};

}
}

#endif /*MEPARAMSBASE_H_*/
