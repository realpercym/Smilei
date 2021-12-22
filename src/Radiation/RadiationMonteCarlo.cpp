// ----------------------------------------------------------------------------
//! \file RadiationMonteCarlo.cpp
//
//! \brief This class performs the Nonlinear Inverse Compton Scattering
//! on particles using a Monte-Carlo approach.
//
//! The implementation is adapted from the thesis results of M. Lobet
//! See http://www.theses.fr/2015BORD0361
//
// ----------------------------------------------------------------------------

#include "RadiationMonteCarlo.h"

#include <cstring>
#include <fstream>

#include <cmath>

// ---------------------------------------------------------------------------------------------------------------------
//! Constructor for RadiationMonteCarlo
//! Inherit from Radiation
// ---------------------------------------------------------------------------------------------------------------------
RadiationMonteCarlo::RadiationMonteCarlo( Params &params, Species *species, Random * rand  )
    : Radiation( params, species, rand )
{
    radiation_photon_sampling_ = species->radiation_photon_sampling_;
    radiation_photon_gamma_threshold_ = species->radiation_photon_gamma_threshold_;
    inv_radiation_photon_sampling_ = 1. / radiation_photon_sampling_;
}

// ---------------------------------------------------------------------------------------------------------------------
//! Destructor for RadiationMonteCarlo
// ---------------------------------------------------------------------------------------------------------------------
RadiationMonteCarlo::~RadiationMonteCarlo()
{
}

// ---------------------------------------------------------------------------------------------------------------------
//! Overloading of the operator (): perform the Discontinuous radiation reaction
//! induced by the nonlinear inverse Compton scattering
//
//! \param particles   particle object containing the particle properties
//! \param photon_species species that will receive emitted photons
//! \param smpi        MPI properties
//! \param RadiationTables Cross-section data tables and useful functions
//                     for nonlinear inverse Compton scattering
//! \param istart      Index of the first particle
//! \param iend        Index of the last particle
//! \param ithread     Thread index
//! \param radiated_energy     overall energy radiated during the call to this method
// ---------------------------------------------------------------------------------------------------------------------
void RadiationMonteCarlo::operator()(
    Particles       &particles,
    Species         *photon_species,
    SmileiMPI       *smpi,
    RadiationTables &RadiationTables,
    double          &radiated_energy,
    int             istart,
    int             iend,
    int             ithread,
    int             ipart_ref)
{
    // _______________________________________________________________
    // Parameters

    std::vector<double> *Epart = &( smpi->dynamics_Epart[ithread] );
    std::vector<double> *Bpart = &( smpi->dynamics_Bpart[ithread] );
    //std::vector<double> *invgf = &(smpi->dynamics_invgf[ithread]);

    int nparts = Epart->size()/3;
    double * __restrict__ Ex = &( ( *Epart )[0*nparts] );
    double * __restrict__ Ey = &( ( *Epart )[1*nparts] );
    double * __restrict__ Ez = &( ( *Epart )[2*nparts] );
    double * __restrict__ Bx = &( ( *Bpart )[0*nparts] );
    double * __restrict__ By = &( ( *Bpart )[1*nparts] );
    double * __restrict__ Bz = &( ( *Bpart )[2*nparts] );

    // Charge divided by the square of the mass
    double charge_over_mass_square;

    const double one_over_mass_square = pow( one_over_mass_, 2. );

    // Number of particles
    const int nbparticles = iend-istart;
    
    // Temporary quantum parameter
    double particle_chi;

    // Temporary Lorentz factor
    double gamma;

    // Radiated energy
    double cont_rad_energy;

    // Temporary double parameter
    double temp;

    // Time to emission
    double emission_time;

    // time spent in the iteration
    double local_it_time;

    // Number of Monte-Carlo iteration
    int mc_it_nb;

    // Momentum shortcut
    double* __restrict__ momentum_x = particles.getPtrMomentum(0);
    double* __restrict__ momentum_y = particles.getPtrMomentum(1);
    double* __restrict__ momentum_z = particles.getPtrMomentum(2);

    // Tables for MC
    double * table_integfochi = &(RadiationTables.integfochi_.table_[0]);
    double * table_min_photon_chi = &(RadiationTables.xi_.min_photon_chi_table_[0]);
    double * table_xi = &(RadiationTables.xi_.table_[0]);

    // Size of tables
    int size_of_Table_integfochi = RadiationTables.integfochi_.size_particle_chi_;
    int size_of_Table_min_photon_chi = RadiationTables.xi_.size_particle_chi_;
    int size_of_Table_xi = RadiationTables.xi_.size_particle_chi_*
                           RadiationTables.xi_.size_photon_chi_;

    // Position shortcut
    double* __restrict__ position_x = particles.getPtrPosition(0);
    double* __restrict__ position_y = NULL;
    double* __restrict__ position_z = NULL;
    if (nDim_>1) {
        position_y = particles.getPtrPosition(1);
        if (nDim_>2) {
            position_z = particles.getPtrPosition(2);
        }
    }

    // Charge shortcut
    short * __restrict__ charge = particles.getPtrCharge();

    // Weight shortcut
    double * __restrict__ weight = particles.getPtrWeight();

    // Optical depth for the Monte-Carlo process
    double * __restrict__ tau = particles.getPtrTau();

    // Quantum parameter
    double * __restrict__ chi = particles.getPtrChi();

    // Parameter to store the local radiated energy
    double radiated_energy_loc = 0;

    //random temporary number
    double random_number; 

    #ifdef _GPU
    unsigned long long seed; // Parameters for CUDA generator
    unsigned long long seq;
    unsigned long long offset;
    curandState_t state_1;
    curandState_t state_2;
    
    seed = 12345ULL;
    seq = 0ULL;
    offset = 0ULL;
    #endif

    // _______________________________________________________________
    // Computation
    #ifdef _GPU
    // Management of the data on GPU though this data region
    int np = iend-istart;
    
    // Initialize initial seed for linear generator
    double initial_seed_1 = rand_->uniform();
    double initial_seed_2 = rand_->uniform();

    // Parameters for linear alleatory number generator
    const int a = 1664525;
    const int c = 1013904223;
    const int m = pow(2,32);

    // Variable to save seed for CUDA generators
    int seed_curand_1;
    int seed_curand_2;
    
    #pragma acc data present(Ex[istart:np],Ey[istart:np],Ez[istart:np],\
            Bx[istart:np],By[istart:np],Bz[istart:np], \
            table_integfochi[0:size_of_Table_integfochi], table_xi[0:size_of_Table_xi], \
            table_min_photon_chi[0:size_of_Table_min_photon_chi]) \
            deviceptr(momentum_x,momentum_y,momentum_z,position_x, \
            position_y,position_z,charge,weight,tau,chi) 
    {
    #endif


    #ifdef _GPU
        #pragma acc parallel \
        present(Ex[istart:np],Ey[istart:np],Ez[istart:np],\
        Bx[istart:np],By[istart:np],Bz[istart:np], \
        table_integfochi[0:size_of_Table_integfochi], table_xi[0:size_of_Table_xi], \
        table_min_photon_chi[0:size_of_Table_min_photon_chi]) \
        deviceptr(momentum_x,momentum_y,momentum_z,charge,weight,particle_chi,tau) 
        {
            #pragma acc loop gang worker vector private(emission_time, local_it_time, mc_it_nb, particle_chi, gamma, state_1, state_2, random_number, seed_curand_1, seed_curand_2) \
        reduction(+:radiated_energy_loc) 
    #endif
    for( int ipart=istart ; ipart<iend; ipart++ ) {
        charge_over_mass_square = ( double )( charge[ipart] )*one_over_mass_square;

        // Init local variables
        emission_time = 0;
        local_it_time = 0;
        mc_it_nb = 0;

        // Monte-Carlo Manager inside the time step
        while( ( local_it_time < dt_ )
                &&( mc_it_nb < max_monte_carlo_iterations_ ) ) {

            // Gamma
            gamma = sqrt( 1.0 + momentum_x[ipart]*momentum_x[ipart]
                          + momentum_y[ipart]*momentum_y[ipart]
                          + momentum_z[ipart]*momentum_z[ipart] );

            if( gamma==1. ){ // does not apply the MC routine for particles with 0 kinetic energy
                break;
            }

            // Computation of the Lorentz invariant quantum parameter
            particle_chi = Radiation::computeParticleChi( charge_over_mass_square,
                           momentum_x[ipart], momentum_y[ipart], momentum_z[ipart],
                           gamma,
                           Ex[ipart-ipart_ref], Ey[ipart-ipart_ref], Ez[ipart-ipart_ref],
                           Bx[ipart-ipart_ref], By[ipart-ipart_ref], Bz[ipart-ipart_ref] );

            // Update the quantum parameter in species
            // chi[ipart] = particle_chi;

            // Discontinuous emission: New emission
            // If tau[ipart] <= 0, this is a new emission
            // We also check that particle_chi > chipa_threshold,
            // else particle_chi is too low to induce a discontinuous emission
            if( ( particle_chi > RadiationTables.getMinimumChiDiscontinuous() )
                    && ( tau[ipart] <= epsilon_tau_ ) ) {
                // New final optical depth to reach for emision
                while( tau[ipart] <= epsilon_tau_ ) {
                    //tau[ipart] = -log( 1.-Rand::uniform() );
                    #ifndef _GPU
                        tau[ipart] = -log( 1.-rand_->uniform() );
                    #else
			            seed_curand_1 = (int) (ipart+1)*(initial_seed_1+1); //Seed for linear generator
                	    seed_curand_1 = (a * seed_curand_1 + c) % m; //Linear generator
               		
			            curand_init(seed_curand_1, seq, offset, &state_1); //Cuda generator initialization
			
			            random_number = curand_uniform(&state_1); //Generating number
                        
			            tau[ipart] = -log( 1.- random_number );
			            initial_seed_1 = random_number;
                    #endif
                }

            }

            // Discontinuous emission: emission under progress
            // If epsilon_tau_ > 0
            if( tau[ipart] > epsilon_tau_ ) {

                // from the cross section
                //temp = 0;
                temp = RadiationTables.computePhotonProductionYield( particle_chi, gamma, 
                                                                        table_integfochi );

                // Time to discontinuous emission
                // If this time is > the remaining iteration time,
                // we have a synchronization
                emission_time = std::min( tau[ipart]/temp, dt_ - local_it_time );

                // Update of the optical depth
                tau[ipart] -= temp*emission_time;

                // If the final optical depth is reached
                if( tau[ipart] <= epsilon_tau_ ) {

                    #ifndef _GPU
                        random_number = rand_->uniform();
                    #else
			            seed_curand_2 = (int) (ipart + 1)*(initial_seed_2 + 1); //Seed for linear generator
              		    seed_curand_2 = (a * seed_curand_2 + c) % m; //Linear generator

        	            curand_init(seed_curand_2, seq, offset, &state_2); //Cuda generator initialization
	
                        random_number = curand_uniform(&state_2); //Generating number
                    #endif

                    // Emission of a photon
                    // Radiated energy is incremented only if the macro-photon is not created
                    radiated_energy_loc += RadiationMonteCarlo::photonEmission( ipart,
                                                         particle_chi, gamma,
                                                         position_x,
                                                         position_y,
                                                         position_z,
                                                         momentum_x,
                                                         momentum_y,
                                                         momentum_z,
                                                         weight,
                                                         random_number,
                                                         table_min_photon_chi,
                                                         table_xi,
                                                         photon_species,
                                                         RadiationTables);                  
                    // Optical depth becomes negative meaning
                    // that a new drawing is possible
                    // at the next Monte-Carlo iteration
                    tau[ipart] = -1.;
                }

                // Incrementation of the Monte-Carlo iteration counter
                mc_it_nb ++;
                // Update of the local time
                local_it_time += emission_time;

            }

            // Continuous emission
            // particle_chi needs to be below the discontinuous threshold
            // particle_chi needs to be above the continuous threshold
            // No discontiuous emission is in progress:
            // tau[ipart] <= epsilon_tau_
            else if( ( particle_chi <= RadiationTables.getMinimumChiDiscontinuous() )
                     && ( tau[ipart] <= epsilon_tau_ )
                     && ( particle_chi > RadiationTables.getMinimumChiContinuous() )
                     && ( gamma > 1. ) ) {

                // Remaining time of the iteration
                emission_time = dt_ - local_it_time;

                // Radiated energy during emission_time
                cont_rad_energy =
                    RadiationTables.getRidgersCorrectedRadiatedEnergy( particle_chi,
                            emission_time );

                // Effect on the momentum
                temp = cont_rad_energy*gamma/( gamma*gamma-1. );
                momentum_x[ipart] -= temp*momentum_x[ipart];
                momentum_y[ipart] -= temp*momentum_y[ipart];
                momentum_z[ipart] -= temp*momentum_z[ipart];

                // Incrementation of the radiated energy cumulative parameter
                radiated_energy_loc += weight[ipart]*( gamma - sqrt( 1.0
                                                    + momentum_x[ipart]*momentum_x[ipart]
                                                    + momentum_y[ipart]*momentum_y[ipart]
                                                    + momentum_z[ipart]*momentum_z[ipart] ) );

                // End for this particle
                local_it_time = dt_;
            }
            // No emission since particle_chi is too low
            else { // if (particle_chi < RadiationTables.getMinimumChiContinuous())
                local_it_time = dt_;
            }

        }

    }

    #ifdef _GPU
    } // end acc parallel
    #endif

    // Update the patch radiated energy
    radiated_energy += radiated_energy_loc;
    //std::cerr << " " << radiated_energy << std::endl;
    // ____________________________________________________
    // Update of the quantum parameter chi

    #ifndef _GPU
        #pragma omp simd
    #else
        int np = iend-istart;
        #pragma acc parallel present(Ex[istart:np],Ey[istart:np],Ez[istart:np],\
        Bx[istart:np],By[istart:np],Bz[istart:np]) \
        deviceptr(momentum_x,momentum_y,momentum_z, charge,weight,chi) 
    {

        #pragma acc loop gang worker vector
    #endif
    for( int ipart=istart ; ipart<iend; ipart++ ) {
        charge_over_mass_square = ( double )( charge[ipart] )*one_over_mass_square;

        // Gamma
        gamma = sqrt( 1.0 + momentum_x[ipart]*momentum_x[ipart]
                      + momentum_y[ipart]*momentum_y[ipart]
                      + momentum_z[ipart]*momentum_z[ipart] );

        // Computation of the Lorentz invariant quantum parameter
        chi[ipart] = Radiation::computeParticleChi( charge_over_mass_square,
                     momentum_x[ipart], momentum_y[ipart], momentum_z[ipart],
                     gamma,
                     Ex[ipart-ipart_ref], Ey[ipart-ipart_ref], Ez[ipart-ipart_ref],
                     Bx[ipart-ipart_ref], By[ipart-ipart_ref], Bz[ipart-ipart_ref] );

    }

    #ifdef _GPU
    }
    #endif

    #ifdef _GPU
    }   // end acc data
    #endif

}

// ---------------------------------------------------------------------------------------------------------------------
//! Perform the photon emission (creation of a super-photon
//! and slow down of the emitting particle)
//! \param ipart              particle index
//! \param particle_chi              particle quantum parameter
//! \param particle_gamma            particle gamma factor
//! \param position           particle position
//! \param momentum           particle momentum
//! \param RadiationTables    Cross-section data tables and useful functions
//                        for nonlinear inverse Compton scattering
// ---------------------------------------------------------------------------------------------------------------------
double RadiationMonteCarlo::photonEmission( int ipart,
        double &particle_chi,
        double &particle_gamma,
        double * position_x,
        double * position_y,
        double * position_z,
        double * momentum_x,
        double * momentum_y,
        double * momentum_z,
        double *weight,
        double random_number,
        double * table_min_photon_chi,
        double * table_xi,
        Species *photon_species,
        RadiationTables &RadiationTables)
{
    // ____________________________________________________
    // Parameters
    double photon_chi;      // Photon quantum parameter
    double gammaph;    // Photon gamma factor
    double inv_old_norm_p;
    double radiated_energy = 0;
    double xi;
    //double new_norm_p;

    // Get the photon quantum parameter from the table xip
    // photon_chi = RadiationTables.computeRandomPhotonChi( particle_chi );
    //photon_chi = 0;
    photon_chi = RadiationTables.computeRandomPhotonChiWithInterpolation( particle_chi, random_number, 
                                                            table_min_photon_chi, table_xi);
    //std::cerr << " " << photon_chi <<std::endl;
    // compute the photon gamma factor
    gammaph = photon_chi/particle_chi*( particle_gamma-1.0 );
    // ____________________________________________________
    // Creation of the new photon

    // ____________________________________________________
    // Update of the particle properties
    // direction d'emission // direction de l'electron (1/gamma << 1)
    // With momentum conservation

    inv_old_norm_p = gammaph/std::sqrt( particle_gamma*particle_gamma - 1.0 );
    momentum_x[ipart] -= momentum_x[ipart]*inv_old_norm_p;
    momentum_y[ipart] -= momentum_y[ipart]*inv_old_norm_p;
    momentum_z[ipart] -= momentum_z[ipart]*inv_old_norm_p;

    // With energy conservation
    /*inv_old_norm_p = 1./sqrt(particle_gamma*particle_gamma - 1.0);
    particle_gamma -= gammaph;
    new_norm_p = sqrt(particle_gamma*particle_gamma - 1.0);
    px *= new_norm_p * inv_old_norm_p;
    py *= new_norm_p * inv_old_norm_p;
    pz *= new_norm_p * inv_old_norm_p;*/

    // Creation of macro-photons if requested
    // Check that the photon_species is defined and the threshold on the energy
    if( photon_species
           && ( gammaph >= radiation_photon_gamma_threshold_ ) ) {
        
        /* ---------------------------------------------------------------------
        // First method: emission of a single photon

        // Creation of the new photon in the temporary array new_photons_
        new_photons_.createParticle();

        int idNew = new_photons_.size() - 1;

        for (int i=0; i<n_dimensions_; i++) {
            new_photons_.position(i,idNew)=position[i][ipart];
        }

        inv_old_norm_p = 1./sqrt(momentum_x[ipart]*momentum_x[ipart]
                                + momentum_y[ipart]*momentum_y[ipart]
                                + momentum_z[ipart]*momentum_z[ipart]);

        for (unsigned int i=0; i<3; i++) {
            new_photons_.momentum(i,idNew) =
            gammaph*momentum[i][ipart]*inv_old_norm_p;
        }

        new_photons_.weight(idNew)=weight[ipart];
        new_photons_.charge(idNew)=0;
        --------------------------------------------------------------------- */

        // Second method: emission of several photons for statistics following
        // the parameter radiation_photon_sampling_

        #ifndef _GPU
        // Creation of new photons in the temporary array new_photons_
        new_photons_.createParticles( radiation_photon_sampling_ );

        // Final size
        int npart = new_photons_.size();

        // Inverse of the momentum norm
        inv_old_norm_p = 1./sqrt( momentum_x[ipart]*momentum_x[ipart]
                                  + momentum_y[ipart]*momentum_y[ipart]
                                  + momentum_z[ipart]*momentum_z[ipart] );

        // For all new photons...
        for( int idNew=npart-radiation_photon_sampling_; idNew<npart; idNew++ ) {


            new_photons_.position( 0, idNew )=position_x[ipart];
            if (nDim_>1) {
                new_photons_.position( 1, idNew )=position_y[ipart];
                if (nDim_>2) {
                    new_photons_.position( 2, idNew )=position_z[ipart];
                }
            }

            new_photons_.momentum( 0, idNew ) =
                gammaph*momentum_x[ipart]*inv_old_norm_p;
            new_photons_.momentum( 1, idNew ) =
                gammaph*momentum_y[ipart]*inv_old_norm_p;
            new_photons_.momentum( 2, idNew ) =
                gammaph*momentum_z[ipart]*inv_old_norm_p;


            new_photons_.weight( idNew )=weight[ipart]*inv_radiation_photon_sampling_;
            new_photons_.charge( idNew )=0;

            if( new_photons_.isQuantumParameter ) {
                new_photons_.chi( idNew ) = photon_chi;
            }

            if( new_photons_.isMonteCarlo ) {
                new_photons_.tau( idNew ) = -1.;
            }

        }
        #endif

    }
    // Addition of the emitted energy in the cumulating parameter
    // for the scalar diagnostics
    else {
        gammaph = particle_gamma - sqrt( 1.0 + momentum_x[ipart]*momentum_x[ipart]
                                         + momentum_y[ipart]*momentum_y[ipart]
                                         + momentum_z[ipart]*momentum_z[ipart] );
        radiated_energy += weight[ipart]*gammaph;
    }

    return radiated_energy;
}

