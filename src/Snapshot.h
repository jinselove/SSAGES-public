#pragma once
#include <vector>
#include <memory>
#include <boost/mpi.hpp>
#include "json/json.h"
#include "JSON/Serializable.h"
#include "types.h"

namespace SSAGES
{
	inline double roundf(double x)
	{
    	return ( x >= 0 ) ? floor( x + 0.5 ) : ceil( x - 0.5 );
  	}

	//! Class containing a snapshot of the current simulation in time.
	/*!
	 * \ingroup core
	 *
	 * This contains information on the particle positions, velocities, etc...
	 * and additional information on the state of the system.
	 */
	class Snapshot : public Serializable
	{
	private:
		//! Local snapshot (walker) communicator.
		boost::mpi::communicator _comm;

		unsigned _wid; //!< Walker ID.

		std::string _ID; //!< ID string

		Matrix3 _H; //!< Parrinello-Rahman box H-matrix.
		Matrix3 _Hinv; //!< Parinello-Rahman box inverse.

		std::vector<Vector3> _positions; //!< Positions
		std::vector<Vector3> _images; //!< Unwrapped positions
		std::vector<Vector3> _velocities; //!< Velocities
		std::vector<Vector3> _forces; //!< Forces
		std::vector<double> _masses; //!< Masses
		Label _atomids; //!< List of Atom IDs
		Label _types; //!< List of Atom types

		int _iteration; //!< Iteration of Simulation
		double _temperature; //!< Current temperature
		double _pressure; //!< System pressure
		double _energy; //!< Average per-particle energy
		double _kb; //!< Kb from the MD driver

		bool _changed; //!< \c TRUE is Simulation state changed

	public:
		//! Constructor
		/*!
		 * \param comm MPI communicator
		 * \param wid Walker ID
		 *
		 * Initialize a snapshot with MPI communicator and a
		 * correpsonding walker ID.
		 */
		Snapshot(boost::mpi::communicator& comm, unsigned wid) :
		_comm(comm), _wid(wid), _H(), _Hinv(), _positions(0), _velocities(0), 
		_forces(0), _atomids(0), _types(0), 
		_iteration(0), _temperature(0), _pressure(0), 
		_energy(0), _kb(0)
		{}

		//! Get the current iteration
		/*!
		 * \return Current Iteration
		 */
		int GetIteration() const {return _iteration; }

		//! Get current temperature
		/*!
		 * \return System temperature
		 */
		double GetTemperature() const {return _temperature; }

		//! Get system pressure
		/*!
		 * \return Current system pressure
		 */
		double GetPressure() const { return _pressure; }

		//! Get per-particle energy
		/*!
		 * \return Current average per-particle energy
		 */
		double GetEnergy() const { return _energy; }

		//! Get system H-matrix
		/*! 
		 * \return Parrinello-Rahman H-matrix of simulation box
		 */
		const Matrix3& GetHMatrix() const { return _H; }

		//! Get system volume
		/*!
		 * \return Volume of the current simulation box
		 */
		double GetVolume() const { return _H.determinant(); }

		//! Get system Kb
		/*!
		 * \return Kb of the current simulation box
		 */
		double GetKb() const { return _kb; }
		
		//! Get communicator for group (walker).
		/*!
		 * \return Communicator used for MetaDynamics.
		 *
		 * Access the communicator containing the set of processors that belong
		 * to a single simulation box.
		 */
		const boost::mpi::communicator& GetCommunicator() const
		{
			return _comm;
		}

		//! Get walker ID.
		/*!
		 * \return ID of the Walker
		 */
		unsigned GetWalkerID() const { return _wid; }

		//! Set the iteration
		/*!
		 * \param iteration New value for the iteration
		 */
		void SetIteration(int iteration) 
		{
			_iteration = iteration; 
			_changed = true; 
		}

		//! Change the temperature
		/*!
		 * \param temperature New value for the temperature
		 */
		void SetTemperature(double temperature) 
		{ 
			_temperature = temperature;
			_changed = true;
		}

		//! Change the pressure
		/*!
		 * \param pressure New value for the pressure
		 */
		void SetPressure(double pressure) 
		{ 
			_pressure = pressure;
			_changed = true;
		}

		//! Change the energy
		/*!
		 * \param energy New value for the energy
		 */
		void SetEnergy(double energy) 
		{
			_energy = energy;
			_changed = true;
		}

		//! Change the Box H-matrix. 
		/*!
		 * \param hmat New H-matrix for the system
		*/
		void SetHMatrix(const Matrix3& hmat)
		{
			_H = hmat;
			_Hinv = hmat.inverse();
			_changed = true;
		}

		//! Change the kb
		/*!
		 * \param kb New value for the kb
		 */
		void SetKb(double kb) 
		{
			_kb = kb;
			_changed = true;
		}
		
		//! Access the particle positions
		/*!
		 * \return List of particle positions
		 */
		const std::vector<Vector3>& GetPositions() const { return _positions; }

		/*! \copydoc Snapshot::GetPositions() const */
		std::vector<Vector3>& GetPositions() 
		{ 
			_changed = true;
			return _positions; 
		}

		//! Access the particles image flags
		/*!
		 * \return List of particle image flags
		 */
		const std::vector<Vector3>& GetImageFlags() const { return _images; }

		//! \copydoc Snapshot::GetImageFlags() const
		std::vector<Vector3>& GetImageFlags() 
		{ 
			_changed = true;
			return _images; 
		}

		//! Access the particle velocities
		/*!
		 * \return List of particle velocities
		 */
		const std::vector<Vector3>& GetVelocities() const { return _velocities; }

		/*! \copydoc Snapshot::GetVelocities() const */
		std::vector<Vector3>& GetVelocities() 
		{
			_changed = true;
			return _velocities; 
		}

		//! Access the per-particle forces
		/*!
		 * \return List of per-particle forces
		 */
		const std::vector<Vector3>& GetForces() const { return _forces; }

		/*! \copydoc Snapshot::GetForces() const */
		std::vector<Vector3>& GetForces() 
		{
			_changed = true; 
			return _forces; 
		}

		//! Const access to the particle masses
		/*!
		 * \return List of Masses
		 *
		 * Note that the Masses can be either stored as per-atom or per-type
		 * depending on the Lammps Atom type used.
		 */
		const std::vector<double>& GetMasses() const { return _masses; }

		/*! \copydoc Snapshot::GetMasses() const */
		std::vector<double>& GetMasses() 
		{
			_changed = true; 
			return _masses; 
		}

		//! Scale a vector into fractional coordinates
		/*! 
		 * \param v Vector of interest
		 * \return Scaled vector in fractional coordinates
		 */
		Vector3 ScaleVector(const Vector3& v) const
		{
			return _Hinv*v;
		}

		//! Apply minimum image to a vector according to periodic boundary conditions
		/*!
		 * \param v Vector of interest
		 */
		void ApplyMinimumImage(Vector3* v) const
		{
			Vector3 scaled = ScaleVector(*v);

			for(int i = 0; i < 3; ++i)
				scaled[i] -= roundf(scaled[i]);

			*v = _H*scaled;
		}

		//! Access the atom IDs
		/*!
		 * \return List of atom IDs
		 */
		const Label& GetAtomIDs() const { return _atomids; }

		/*! \copydoc Snapshot::GetAtomIDs() const */
		Label& GetAtomIDs()
		{
			_changed = true;
			return _atomids;
		}
		
		//! Access the atom types
		/*!
		 * \return List of atom types
		 */
		const Label& GetAtomTypes() const { return _types; }

		/*! \copydoc Snapshot::GetAtomTypes() const */
		Label& GetAtomTypes() 
		{
			_changed = true; 
			return _types; 
		}

		//! Access the snapshot ID
		/*!
		 * \return Snapshot ID
		 */
		const std::string& GetSnapshotID() const { return _ID; }

		/*! \copydoc Snapshot::GetSnapshotID() const */
		std::string& GetSnapshotID() 
		{
			_changed = true; 
			return _ID; 
		}

		//! Query if Snapshot was modified
		/*!
		 * \return \c True if Snapshot was modified, else return \c False
		 */
		bool HasChanged() const { return _changed; }

		//! Set the "changed" flag of the Snapshot
		/*!
		 * \param state State to which the "changed" flag is set
		 */
		void Changed(bool state) { _changed = state; }

		//! Serialize the Snapshot
		/*!
		 * \param json Json value to write serialized state to
		 */
		void Serialize(Json::Value& json) const override
		{
			json["walker id"] = _wid;
			json["id"] = _ID;

			for(unsigned int i = 0; i < _atomids.size(); i++)
			{
				json["Atom"][i]["ID"] = _atomids[i];
				json["Atom"][i]["type"] = _types[i];
				json["Atom"][i]["mass"] = _masses[i];
				for(int j = 0; j<3; j++)
				{
					json["Atom"][i]["positions"][j] = _positions[i][j];
					json["Atom"][i]["velocities"][j] = _velocities[i][j];
					json["Atom"][i]["forces"][j] = _forces[i][j];
				}
			}
			
			json["iteration"] = _iteration; 
			json["temperature"] = _temperature; 
			json["pressure"] = _pressure; 
			json["energy"] = _energy;
			json["kb"] = _kb;

			for(int i = 0; i < 3; ++i)
				for(int j = 0; j < 3; ++j)
					json["hmat"][i][j] = _H(i,j);
		}
		
		//! Destructor
		~Snapshot(){}
	};
}
