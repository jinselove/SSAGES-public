/**
 * This file is part of
 * SSAGES - Suite for Advanced Generalized Ensemble Simulations
 *
 * Copyright 2016 Emre Sevgen <sesevgen@uchicago.edu>
 *                Hythem Sidky <hsidky@nd.edu>
 *
 * SSAGES is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SSAGES is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SSAGES.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "ABF.h"
#include <math.h>
#include <algorithm>
#include <iostream>
#include <cassert>


// "Adaptive biasing force method for scalar and vector free energy calculations"
// Darve, Rodriguez-Gomez, Pohorille
// J. Chem. Phys. (2008)

namespace mpi = boost::mpi;
namespace SSAGES
{
	// Function to return histogram coordinates, given CV values. Returns -1 if any CV value is outside bounds.
	// Regardless of number of CVs, the histogram is a one dimensional object.
	// Therefore, this function works in two parts:
	// 1) Mapping from CV coordinates to an N dimensional fictitious object where N is the number of CVs.
	// -> This fictitious object is (Number of CV bins) large in each dimension.
	// -> Example: If you have two CVs, X and Y, and you bin X into three bins and Y into five bins, the fictitious object is a 2 dimensional 3x5 matrix. 
	
	// 2) Mapping from N dimensions to one dimension.
	// -> Simply constructed by writing out all members of the fictitious object in a line. This is a vector of length equal to the product of number of bins in each dimension.
	// -> For the above example, this is 3.5 = 15 members. First 5 members are Y = [1 2 3 4 5] bins with X = 1. Next 5 members are Y = [1 2 3 4 5] with X = 2 ....


	int ABF::histCoords(const CVList& cvs)
	{
		// Histogram details: This is a 2 Dimensional object set up as the following:
		// Histdetails is a vector of three vectors, each of those three vectors are (Number of CVs) long.
		// First of these vectors hold the lower bound for the CVs in order.
		// Second vector holds the upper bound for the CVs in order.
		// Third vector holds number of histogram bins for the CVs in order.
	
		// The first mapping starts here.
		for(size_t i = 0; i < histdetails_.size(); ++i)
		{
			// Check if CV is in bounds for each CV.
			if ((cvs[i]->GetValue()<histdetails_[i][0]) || (cvs[i]->GetValue()>histdetails_[i][1]))
				return -1;
		}

		// This vector holds the CV coordinates in the fictitious object.
		std::vector<int> coords(cvs.size());
		
		// Loop over each CV dimension.
		for(size_t i = 0; i < histdetails_.size(); ++i)
		{
			// Loop over all bins in each CV dimensions
			for(int j = 0; j < histdetails_[i][2] ; ++j)
			{
				if((histdetails_[i][0] + j*((histdetails_[i][1]-histdetails_[i][0])/histdetails_[i][2]) < cvs[i]->GetValue()) && 
				   (histdetails_[i][0] + (j+1)*((histdetails_[i][1]-histdetails_[i][0])/histdetails_[i][2]) > cvs[i]->GetValue()))
				{
					coords[i] = j;
				}
			}
		} //First mapping ends here. 

		// Second mapping starts here.
		int finalcoord = 0;
		int indexer = 1;
		// Loop over each CV dimension.
		for(size_t i = 0; i < histdetails_.size(); ++i)
		{
			indexer = 1;
			for(size_t j = i+1; j < histdetails_.size(); ++j)
				indexer = indexer*histdetails_[j][2];

			finalcoord += coords[i]*indexer;
		}

		return finalcoord;			
	}
	
	// Pre-simulation hook.
	void ABF::PreSimulation(Snapshot* snapshot, const CVList& cvs)
	{
		mpiid_ = snapshot->GetWalkerID();
		char file[1024];
		sprintf(file, "node-%04d.log",mpiid_);
	 	walkerout_.open(file);

		if(mpiid_ == 0)
		 	worldout_.open(filename_.c_str());

		// Convenience. Number of CVs.
		ncv_ = cvs.size();

		// Size and initialize Fold_
		Fold_.setZero(ncv_);
		
		// Size and initialize Fworld_ and Nworld_		
		auto nel = 1;
		for(auto i = 0; i < ncv_; ++i)
			nel *= histdetails_[i][2];

		Fworld_.setZero(nel*ncv_);
		Nworld_.resize(nel, 0);

		// If F or N are empty, size appropriately. 
		if(_F.size() == 0) _F.setZero(nel*ncv_);
		if(_N.size() == 0) _N.resize(nel, 0);

		// Initialize biases.
		biases_.resize(snapshot->GetPositions().size(), Vector3{0, 0, 0});
		
		// Initialize w \dot p's for finite difference. 
		wdotp1_.setZero(ncv_);
		wdotp2_.setZero(ncv_);
		
	}

	//! Post-integration hook.
	/*!
	 * Post-integration hook is where processes carried out every timestep happen.
	 * First, information from current snapshot is retrieved and stored in variables as necessary.
	 * Then, coordinates in CV space are determined.
	 * Then, for each CV, the time derivative of w.p is calculated.
	 * Then, information is printed out if its enabled.
	 * Finally, bias is applied from current estimate of generalized force.
	 */
	void ABF::PostIntegration(Snapshot* snapshot, const CVList& cvs)
	{
		++iteration_;

		// Gather information.
		auto& vels = snapshot->GetVelocities();
		auto& mass = snapshot->GetMasses();
		auto& forces = snapshot->GetForces();
		auto n = snapshot->GetNumAtoms();

		//! Coord holds where we are in CV space in current timestep.
		int coord = histCoords(cvs);

		//! Eigen::MatrixXd to hold the CV gradient.
		Eigen::MatrixXd J(ncv_, 3*n);

		// Fill J. Each column represents grad(CV) with flattened Cartesian elements. 
		for(int i = 0; i < ncv_; ++i)
		{
			auto& grad = cvs[i]->GetGradient();
			for(size_t j = 0; j < n; ++j)
				J.block<1, 3>(i,3*j) = grad[j];
		}
		
		//* Calculate W using Darve's approach (http://mc.stanford.edu/cgi-bin/images/0/06/Darve_2008.pdf).
		Eigen::MatrixXd Jmass = J.transpose();
		
		if(massweigh_)
		{
			for(size_t i = 0; i < forces.size(); ++i)
				Jmass.block(3*i, 0, 3, ncv_) = Jmass.block(3*i, 0, 3, ncv_)/mass[i];
		}
							
		Eigen::MatrixXd Minv = J*Jmass;
		MPI_Allreduce(MPI_IN_PLACE, Minv.data(), Minv.size(), MPI_DOUBLE, MPI_SUM, comm_);

		Eigen::MatrixXd Wt = Minv.inverse()*Jmass.transpose();	

		// Fill momenta.
		Eigen::VectorXd momenta(3*vels.size());
		for(size_t i = 0; i < vels.size(); ++i)
			momenta.segment<3>(3*i) = mass[i]*vels[i];

		// Compute dot(w,p).
		Eigen::VectorXd wdotp = Wt*momenta;

		// Reduce dot product across processors.
		MPI_Allreduce(MPI_IN_PLACE, wdotp.data(), wdotp.size(), MPI_DOUBLE, MPI_SUM, comm_);

		// Compute d(wdotp)/dt second order backwards finite difference. 
		// Adding old force removes bias. 
		Eigen::VectorXd dwdotpdt = unitconv_*(1.5*wdotp - 2.0*wdotp1_ + 0.5*wdotp2_)/timestep_ + Fold_;

		// If we are in bounds, sum force into running total.
		if(coord != -1)
		{
			_F.segment(ncv_*coord, ncv_) += dwdotpdt;
			++_N[coord];
		}

		// Reduce data across processors.
		MPI_Allreduce(_F.data(), Fworld_.data(), _F.size(), MPI_DOUBLE, MPI_SUM, world_);
		MPI_Allreduce(_N.data(), Nworld_.data(), _N.size(), MPI_INT, MPI_SUM, world_);

		// If we are in bounds, store the old summed force.
		if(coord != -1)
			Fold_ = Fworld_.segment(ncv_*coord, ncv_)/std::max(min_, Nworld_[coord]);
	
		// Update finite difference time derivatives.
		wdotp2_ = wdotp1_;
		wdotp1_ = wdotp;		

		// Write out data to file.
		if(iteration_ % FBackupInterv_ == 0)
			WriteData();
		
		// Calculate the bias from averaged F at current CV coordinates
		CalcBiasForce(snapshot, cvs, coord);		
		
		// Update the forces in snapshot by adding in the force bias from each
		// CV to each atom based on the gradient of the CV.
		for (size_t j = 0; j < forces.size(); ++j)
			forces[j] += biases_[j];	
	}

	// Post-simulation hook.
	void ABF::PostSimulation(Snapshot*, const CVList&)
	{
		WriteData();
		worldout_.close();		
		walkerout_.close();
	}

	void ABF::CalcBiasForce(const Snapshot* snapshot, const CVList& cvs, int coord)
	{
		// Reset the bias.
		biases_.resize(snapshot->GetNumAtoms(), Vector3{0,0,0});
		for(auto& b : biases_)
			b.setZero();
		
		// Compute bias if within bounds
		if(coord != -1)
		{
			for(int i = 0; i < ncv_; ++i)
			{
				auto& grad = cvs[i]->GetGradient();
				for(size_t j = 0; j < biases_.size(); ++j)
					biases_[j] -= Fworld_[ncv_*coord+i]*grad[j]/std::max(min_, Nworld_[coord]);
			}
		}
		else
		{
			for(int i = 0; i < ncv_; ++i)
			{
				double cvVal = cvs[i]->GetValue();
				auto k = 0.;
				auto x0 = 0.;
				
				if(isperiodic_[i])
					{
					double periodsize = periodicboundaries_[i][1]-periodicboundaries_[i][0];
					double cvRestrMidpoint = (restraint_[i][1]+restraint_[i][0])/2;
					while((cvVal-cvRestrMidpoint) > periodsize/2)
						cvVal -= periodsize;
					while((cvVal-cvRestrMidpoint) < -periodsize/2)
						cvVal += periodsize;
					}

				if(cvVal < restraint_[i][0] && restraint_[i][2] > 0)
				{
					k = restraint_[i][2];
					x0 = restraint_[i][0];
				}
				else if (cvVal > restraint_[i][1] && restraint_[i][2] > 0)
				{
					k = restraint_[i][2];
					x0 = restraint_[i][1];
				}

				auto& grad = cvs[i]->GetGradient();
				for(size_t j = 0; j < biases_.size(); ++j)
					biases_[j] -= (cvVal - x0)*k*grad[j];
			}	
		}
	}

	void ABF::WriteData()
	{		
		if(mpiid_ != 0)
			return;

		worldout_ << std::endl;
		worldout_ << "Iteration: " << iteration_ << std::endl;			
		worldout_ << "Printing out the current Adaptive Biasing Vector Field." << std::endl;
		worldout_ << "First (Nr of CVs) columns are the coordinates, the next (Nr of CVs) columns are components of the Adaptive Force vector at that point." << std::endl;
		worldout_ << "The columns are " << _N.size() << " long, mapping out a surface of ";
		
		for(size_t i = 0; i < histdetails_.size()-1; ++i)
			worldout_ << histdetails_[i][2] << " by ";
		
		worldout_ << histdetails_[histdetails_.size()-1][2] 
		          << " points in " << histdetails_.size() 
		          << " dimensions." <<std::endl;

		int modulo = 1;
		int index = 0;
		for(size_t i = 0; i < Nworld_.size(); ++i)
		{
			index = i;
			worldout_ << std::endl;
			for(size_t j=0 ; j < histdetails_.size(); ++j)
			{
				modulo = 1;
				for(size_t k=j+1 ; k <histdetails_.size(); ++k)
					modulo = modulo * histdetails_[k][2];

				worldout_ << (floor(index/modulo)+0.5)*((histdetails_[j][1]-histdetails_[j][0])/histdetails_[j][2]) + histdetails_[j][0] << " ";
				index = index % modulo;
			}

			for(int j = 0; j < ncv_; ++j)
				worldout_ << Fworld_[ncv_*i+j]/std::max(Nworld_[i],min_) << " ";
		}

		worldout_ << std::endl;
		worldout_ << std::endl;
	}
}



































