// ----------------------------------------------------------------------------
//! \file MergingFactory.h
//
//! \brief Header for the class MergingFactory that
//! manages the different particle merging algorithms.
//
//! Creation - 01/2019 - Mathieu Lobet
//
// ----------------------------------------------------------------------------

#ifndef MERGINGFACTORY_H
#define MERGINGFACTORY_H

#include "Merging.h"
#include "MergingVranic.h"

//  ----------------------------------------------------------------------------
//! Class MergingFactory
//  ----------------------------------------------------------------------------

class MergingFactory
{
public:

    //  ------------------------------------------------------------------------
    //! Create appropriate merging method for the species `species`
    //! \param species Species object
    //! \param params Parameters
    //  ------------------------------------------------------------------------
    static Merging *create( Params &params, Species *species )
    {
        Merging *Merge = NULL;
        
        // assign the correct Radiation model to Radiate
        if( species->merging_method_ == "vranic" ) {
            Merge = new MergingVranic( params, species );
        }
        
        return Merge;
    }
};

#endif
