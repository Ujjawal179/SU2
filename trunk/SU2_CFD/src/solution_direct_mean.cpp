/*!
 * \file solution_direct_mean.cpp
 * \brief Main subrotuines for solving direct problems (Euler, Navier-Stokes, etc.).
 * \author Aerospace Design Laboratory (Stanford University) <http://su2.stanford.edu>.
 * \version 2.0.1
 *
 * Stanford University Unstructured (SU2) Code
 * Copyright (C) 2012 Aerospace Design Laboratory
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/solution_structure.hpp"

CEulerSolution::CEulerSolution(void) : CSolution() { }

CEulerSolution::CEulerSolution(CGeometry *geometry, CConfig *config, unsigned short iMesh) : CSolution() {
	unsigned long iPoint, index;
	unsigned short iVar, iDim, iMarker;
	unsigned short nZone = geometry->GetnZone();
	bool restart = (config->GetRestart() || config->GetRestart_Flow());
	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();

	int rank = MASTER_NODE;
#ifndef NO_MPI
	rank = MPI::COMM_WORLD.Get_rank();
#endif

	/*--- Set the gamma value ---*/
	Gamma = config->GetGamma();
	Gamma_Minus_One = Gamma - 1.0;

	/*--- Define geometry constants in the solver structure ---*/
	nDim = geometry->GetnDim();	
	if (incompressible) nVar = nDim + 1;
	else nVar = nDim + 2;
	nMarker = config->GetnMarker_All();
	nPoint = geometry->GetnPoint();

	/*--- Allocate the node variables ---*/
	node = new CVariable*[geometry->GetnPoint()];

	/*--- Define some auxiliary vectors related to the residual ---*/
	Residual   = new double[nVar]; Residual_Max = new double[nVar];
	Residual_i = new double[nVar]; Residual_j   = new double[nVar];
	Res_Conv   = new double[nVar]; Res_Visc     = new double[nVar]; 
	Res_Sour = new double[nVar];

	/*--- Define some auxiliary vectors related to the solution ---*/
	Solution   = new double[nVar];
	Solution_i = new double[nVar]; Solution_j = new double[nVar];

	/*--- Define some auxiliary vectors related to the geometry ---*/
	Vector   = new double[nDim];
	Vector_i = new double[nDim]; Vector_j = new double[nDim];

	/*--- Define some auxiliary vectors related to the undivided lapalacian ---*/
	if ((config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) ||
			(config->GetKind_ConvNumScheme_AdjFlow() == SPACE_CENTERED)) {
		p1_Und_Lapl = new double [geometry->GetnPoint()]; 
		p2_Und_Lapl = new double [geometry->GetnPoint()]; 
	}

	if ((config->GetKind_Upwind() == ROE_TURKEL_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_1ST)) {
		Precon_Mat_inv = new double* [nVar];
		for (iVar = 0; iVar < nVar; iVar ++)
			Precon_Mat_inv[iVar] = new double[nVar];
	}

	/*--- Jacobians and vector structures for implicit computations ---*/
	if (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) {

		/*--- Block auxiliar Jacobians ---*/
		Jacobian_i = new double* [nVar]; 
		Jacobian_j = new double* [nVar];
		for (iVar = 0; iVar < nVar; iVar++) {
			Jacobian_i[iVar] = new double [nVar]; 
			Jacobian_j[iVar] = new double [nVar]; 
		}

		/*--- Initialization of the structure for the whole Jacobian ---*/
		if (rank == MASTER_NODE) cout << "Initialize jacobian structure (Euler's equations). MG level: " << iMesh <<"." << endl;
		Initialize_Jacobian_Structure(geometry, config);
		xsol = new double [geometry->GetnPoint()*nVar];
		rhs  = new double [geometry->GetnPoint()*nVar];

	}

	/*--- Computation of gradients by least squares ---*/
	if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) {

		/*--- S matrix := inv(R)*traspose(inv(R)) ---*/
		Smatrix = new double* [nDim]; 
		for (iDim = 0; iDim < nDim; iDim++)
			Smatrix[iDim] = new double [nDim];

		/*--- c vector := transpose(WA)*(Wb) ---*/
		cvector = new double* [nDim+3];
		for (iVar = 0; iVar < nDim+3; iVar++)
			cvector[iVar] = new double [nDim];
	}

	/*--- Forces definition and coefficient in all the markers ---*/
	CPressure = new double* [config->GetnMarker_All()];
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++)
		CPressure[iMarker] = new double [geometry->nVertex[iMarker]];

	/*--- Non dimensional coefficients ---*/
	ForceInviscid    = new double[nDim];
	MomentInviscid   = new double[3];
	CDrag_Inv        = new double[config->GetnMarker_All()];
	CLift_Inv        = new double[config->GetnMarker_All()];
	CSideForce_Inv   = new double[config->GetnMarker_All()];
	CMx_Inv          = new double[config->GetnMarker_All()];
	CMy_Inv          = new double[config->GetnMarker_All()];
	CMz_Inv          = new double[config->GetnMarker_All()];
	CEff_Inv         = new double[config->GetnMarker_All()];
	CFx_Inv          = new double[config->GetnMarker_All()];
	CFy_Inv          = new double[config->GetnMarker_All()];
	CFz_Inv          = new double[config->GetnMarker_All()];

	/*--- Rotational coefficients ---*/
	CMerit_Inv       = new double[config->GetnMarker_All()];
	CT_Inv           = new double[config->GetnMarker_All()];
	CQ_Inv           = new double[config->GetnMarker_All()];

	/*--- Supersonic coefficients ---*/
	CEquivArea_Inv   = new double[config->GetnMarker_All()];
	CNearFieldOF_Inv = new double[config->GetnMarker_All()];

	/*--- Nacelle simulation ---*/
	MassFlow_Rate  = new double[config->GetnMarker_All()];	
	FanFace_Pressure  = new double[config->GetnMarker_All()];
	FanFace_Mach  = new double[config->GetnMarker_All()];

	/*--- Init total coefficients ---*/
	Total_CDrag = 0.0;  Total_CLift = 0.0;      Total_CSideForce = 0.0;
	Total_CMx = 0.0;    Total_CMy = 0.0;        Total_CMz = 0.0;
	Total_CEff = 0.0;   Total_CEquivArea = 0.0; Total_CNearFieldOF = 0.0;
	Total_CFx = 0.0;    Total_CFy = 0.0;        Total_CFz = 0.0;
	Total_CT = 0.0;     Total_CQ = 0.0;         Total_CMerit = 0.0;

	/*--- Read farfield conditions ---*/
	Density_Inf  = config->GetDensity_FreeStreamND();
	Pressure_Inf = config->GetPressure_FreeStreamND();
	Velocity_Inf = config->GetVelocity_FreeStreamND();
	Energy_Inf   = config->GetEnergy_FreeStreamND();
	Mach_Inf     = config->GetMach_FreeStreamND();

	/*--- Initializate fan face pressure, fan face mach number, and mass flow rate ---*/
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
		MassFlow_Rate[iMarker] = 0.0;
		FanFace_Mach[iMarker] = Mach_Inf;
		FanFace_Pressure[iMarker] = Pressure_Inf;
	}

	/*--- Inlet/Outlet boundary conditions, using infinity values ---*/
	Density_Inlet = Density_Inf;		Density_Outlet = Density_Inf;
	Pressure_Inlet = Pressure_Inf;	Pressure_Outlet = Pressure_Inf;
	Energy_Inlet = Energy_Inf;			Energy_Outlet = Energy_Inf;
	Mach_Inlet = Mach_Inf;					Mach_Outlet = Mach_Inf;
	Velocity_Inlet  = new double [nDim]; Velocity_Outlet = new double [nDim];
	for (iDim = 0; iDim < nDim; iDim++) {
		Velocity_Inlet[iDim] = Velocity_Inf[iDim];
		Velocity_Outlet[iDim] = Velocity_Inf[iDim];
	}

	/*--- For a rotating frame, set the velocity due to rotation
	 at each point just once, and store it in the geometry class. ---*/
	if (rotating_frame) {
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++) {
			double RotVel[3], distance[3];
			double *coord = geometry->node[iPoint]->GetCoord();
			double *axis  = config->GetRotAxisOrigin();
			double *omega = config->GetOmega_FreeStreamND();
			double Lref   = config->GetLength_Ref();

			/*--- Calculate non-dim distance fron rotation center ---*/
			distance[0] = (coord[0]-axis[0])/Lref;
			distance[1] = (coord[1]-axis[1])/Lref;
			distance[2] = (coord[2]-axis[2])/Lref;

			/*--- Calculate the angular velocity as omega X r ---*/
			RotVel[0] = omega[1]*(distance[2]) - omega[2]*(distance[1]);
			RotVel[1] = omega[2]*(distance[0]) - omega[0]*(distance[2]);
			RotVel[2] = omega[0]*(distance[1]) - omega[1]*(distance[0]);

			geometry->node[iPoint]->SetRotVel(RotVel);
		}
	}

	/*--- Check for a restart and set up the variables at each node
   appropriately. Coarse multigrid levels will be intitially set to 
   the farfield values bc the solver will immediately interpolate
   the solution from the finest mesh to the coarser levels. ---*/
	if (!restart || geometry->GetFinestMGLevel() == false || nZone > 1) {

		/*--- Restart the solution from infinity ---*/
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++)
			node[iPoint] = new CEulerVariable(Density_Inf, Velocity_Inf, Energy_Inf, nDim, nVar, config);
	}

	else {

		/*--- Restart the solution from file information ---*/
		ifstream restart_file;
		string filename = config->GetSolution_FlowFileName();

		/*--- Append time step for unsteady restart ---*/
		if (config->GetUnsteady_Simulation() && config->GetWrt_Unsteady()) {
			char buffer[50];
			unsigned long flowIter = config->GetnExtIter() - 1;
			filename.erase (filename.end()-4, filename.end());
			if ((int(flowIter) >= 0) && (int(flowIter) < 10)) sprintf (buffer, "_0000%d.dat", int(flowIter));
			if ((int(flowIter) >= 10) && (int(flowIter) < 100)) sprintf (buffer, "_000%d.dat", int(flowIter));
			if ((int(flowIter) >= 100) && (int(flowIter) < 1000)) sprintf (buffer, "_00%d.dat", int(flowIter));
			if ((int(flowIter) >= 1000) && (int(flowIter) < 10000)) sprintf (buffer, "_0%d.dat", int(flowIter));
			if (int(flowIter) >= 10000) sprintf (buffer, "_%d.dat", int(flowIter));
			string UnstExt = string(buffer);
			filename.append(UnstExt);
		}
		restart_file.open(filename.data(), ios::in);

		/*--- In case there is no restart file ---*/
		if (restart_file.fail()) {
			cout << "There is no flow restart file!!" << endl;
			cout << "Press any key to exit..." << endl;
			cin.get(); exit(1);
		}    

		/*--- In case this is a parallel simulation, we need to perform the
     Global2Local index transformation first. ---*/
		long *Global2Local = new long[geometry->GetGlobal_nPointDomain()];

		/*--- First, set all indices to a negative value by default ---*/
		for(iPoint = 0; iPoint < geometry->GetGlobal_nPointDomain(); iPoint++)
			Global2Local[iPoint] = -1;

		/*--- Now fill array with the transform values only for local points ---*/
		for(iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
			Global2Local[geometry->node[iPoint]->GetGlobalIndex()] = iPoint;

		/*--- Read all lines in the restart file ---*/
		long iPoint_Local; unsigned long iPoint_Global = 0; string text_line;
		while (getline (restart_file,text_line)) {
			istringstream point_line(text_line);

			/*--- Retrieve local index. If this node from the restart file lives
       on a different processor, the value of iPoint_Local will be -1. 
       Otherwise, the local index for this node on the current processor 
       will be returned and used to instantiate the vars. ---*/
			iPoint_Local = Global2Local[iPoint_Global];
			if (iPoint_Local >= 0) {
				if (incompressible) {
					if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2];
					if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
				}
				else {
					if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
					if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3] >> Solution[4];
				}
				node[iPoint_Local] = new CEulerVariable(Solution, nDim, nVar, config);
			}
			iPoint_Global++;
		}

		/*--- Instantiate the variable class with an arbitrary solution
     at any halo/periodic nodes. The initial solution can be arbitrary,
     because a send/recv is performed immediately in the solver. ---*/
		for(iPoint = geometry->GetnPointDomain(); iPoint < geometry->GetnPoint(); iPoint++)
			node[iPoint] = new CEulerVariable(Solution, nDim, nVar, config);

		/*--- Close the restart file ---*/
		restart_file.close();

		/*--- Free memory needed for the transformation ---*/
		delete [] Global2Local;
	}

	/*--- Define solver parameters needed for execution of destructor ---*/
	if (config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED || 
			(config->GetKind_ConvNumScheme_AdjFlow() == SPACE_CENTERED)) space_centered = true;
	else space_centered = false;

	if ((config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) ||
			(config->GetKind_TimeIntScheme_AdjFlow() == EULER_IMPLICIT)) euler_implicit = true;
	else euler_implicit = false;

	if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) least_squares = true;
	else least_squares = false;

	if ((config->GetKind_Upwind() == ROE_TURKEL_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_1ST)) roe_turkel = true;
	else roe_turkel = false;


}

CEulerSolution::~CEulerSolution(void) {
	unsigned short iVar, iDim, iMarker;
	unsigned long iPoint;

	for (iPoint = 0; iPoint < nPoint; iPoint++)
		delete node[iPoint];
	delete [] node;

	delete [] Residual;   delete [] Residual_Max; delete [] Res_Sour;
	delete [] Residual_i; delete [] Residual_j;   delete [] Solution_j;
	delete [] Res_Conv;   delete [] Res_Visc;     delete [] Vector_i;     
	delete [] Solution;   delete [] Solution_i;   delete [] Vector_j;
	delete [] Vector;

	if (space_centered) {
		delete [] p1_Und_Lapl;	delete [] p2_Und_Lapl;
	}

	if (euler_implicit){
		for (iVar = 0; iVar < nVar; iVar++) {
			delete [] Jacobian_i[iVar];	delete [] Jacobian_j[iVar];
		}
		delete [] Jacobian_i; delete [] Jacobian_j;
		delete [] xsol; delete [] rhs;
	}

	if (least_squares){
		for (iDim = 0; iDim < nDim; iDim++)
			delete [] Smatrix[iDim];
		delete [] Smatrix;

		for (iVar = 0; iVar < nVar+1; iVar++)
			delete [] cvector[iVar];
		delete [] cvector;
	}

	if (roe_turkel) {
		for (iVar = 0; iVar < nVar; iVar ++)
			delete [] Precon_Mat_inv[iVar];
		delete [] Precon_Mat_inv;
	}

	for (iMarker = 0; iMarker < nMarker; iMarker++)
		delete [] CPressure[iMarker];
	delete [] CPressure;

	delete [] ForceInviscid; delete [] MomentInviscid;   delete [] FanFace_Mach;
	delete [] CDrag_Inv;     delete [] CLift_Inv;        delete [] CSideForce_Inv;
	delete [] CMx_Inv;       delete [] CMy_Inv;          delete [] CMz_Inv;
	delete [] CEff_Inv;      delete [] CEquivArea_Inv;   delete [] CNearFieldOF_Inv;
	delete [] CFx_Inv;       delete [] CFy_Inv;          delete [] CFz_Inv; 
	delete [] CMerit_Inv;    delete [] CT_Inv;           delete [] CQ_Inv;         
	delete [] MassFlow_Rate; delete [] FanFace_Pressure; 

	delete [] Velocity_Inlet; delete [] Velocity_Outlet; 

}

void CEulerSolution::SetInitialCondition(CGeometry **geometry, CSolution ***solution_container, CConfig *config, unsigned long ExtIter) {
	unsigned long iPoint;
	unsigned short iMesh;
	double Density, Pressure, yFreeSurface, PressFreeSurface, 
	Froude, yCoord, Velx, Vely, Velz, RhoVelx, RhoVely, RhoVelz;

	bool gravity = config->GetGravityForce();
	bool incompressible = config->GetIncompressible();
	bool restart = (config->GetRestart() || config->GetRestart_Flow());
	unsigned short nDim = geometry[MESH_0]->GetnDim();

	if (incompressible) {

		for (iMesh = 0; iMesh <= config->GetMGLevels(); iMesh++) {
			for (iPoint = 0; iPoint < geometry[iMesh]->GetnPoint(); iPoint++) {

				solution_container[iMesh][FLOW_SOL]->node[iPoint]->SetDensityInc(1.0);

				/*--- Set initial boundary condition at the first iteration ---*/
				if ((ExtIter == 0) && (!restart)) {
					yFreeSurface = config->GetFreeSurface_Zero();
					PressFreeSurface = solution_container[iMesh][FLOW_SOL]->GetPressure_Inf();
					Density = solution_container[iMesh][FLOW_SOL]->GetDensity_Inf();
					Froude = config->GetFroude();
					yCoord = geometry[iMesh]->node[iPoint]->GetCoord(nDim-1);
					if (gravity) Pressure = PressFreeSurface + Density*((yFreeSurface-yCoord)/(Froude*Froude));
					else Pressure = PressFreeSurface;

					/*--- Update solution with the new pressure ---*/
					solution_container[iMesh][FLOW_SOL]->node[iPoint]->SetSolution(0, Pressure);

					Velx = solution_container[iMesh][FLOW_SOL]->GetVelocity_Inf(0);
					Vely = solution_container[iMesh][FLOW_SOL]->GetVelocity_Inf(1);
					RhoVelx = Velx * Density; RhoVely = Vely * Density;

					solution_container[iMesh][FLOW_SOL]->node[iPoint]->SetSolution(1, RhoVelx);
					solution_container[iMesh][FLOW_SOL]->node[iPoint]->SetSolution(2, RhoVely);

					if (nDim == 3) {
						Velz = solution_container[iMesh][FLOW_SOL]->GetVelocity_Inf(2);
						RhoVelz = Velz * Density;
						solution_container[iMesh][FLOW_SOL]->node[iPoint]->SetSolution(3, RhoVelz);						
					}

					if ((config->GetUnsteady_Simulation() == DT_STEPPING_1ST) || (config->GetUnsteady_Simulation() == DT_STEPPING_2ND))
						if (incompressible) {
							solution_container[iMesh][FLOW_SOL]->node[iPoint]->Set_Solution_time_n();
							solution_container[iMesh][FLOW_SOL]->node[iPoint]->Set_Solution_time_n1();
						}		

				}
			}
		}
	}	
}

void CEulerSolution::Preprocessing(CGeometry *geometry, CSolution **solution_container, CNumerics **solver, CConfig *config, unsigned short iRKStep) {
	unsigned long iPoint;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool incompressible = config->GetIncompressible();
	bool freesurface = (config->GetKind_Solver() == FREE_SURFACE_EULER || config->GetKind_Solver() == FREE_SURFACE_NAVIER_STOKES ||
			config->GetKind_Solver() == FREE_SURFACE_RANS || config->GetKind_Solver() == ADJ_FREE_SURFACE_RANS ||
			config->GetKind_Solver() == ADJ_FREE_SURFACE_EULER || config->GetKind_Solver() == ADJ_FREE_SURFACE_NAVIER_STOKES);
	double Gas_Constant = config->GetGas_Constant();
	double ArtComp_Factor = config->GetArtComp_Factor();

	for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint ++) {

		/*--- Set the primitive variables incompressible (dens, vx, vy, vz, beta)---*/
		/*--- and compressible (temp, vx, vy, vz, press, dens, enthal, sos)---*/
		if (incompressible)
			node[iPoint]->SetPrimVar_Incompressible(Density_Inf, ArtComp_Factor, freesurface);
		else
			node[iPoint]->SetPrimVar_Compressible(Gamma, Gas_Constant);


		/*--- Initialize the convective residual vector ---*/
		node[iPoint]->Set_ResConv_Zero();

		/*--- Initialize the source residual vector ---*/
		node[iPoint]->Set_ResSour_Zero();

		/*--- Initialize the viscous residual vector ---*/
		if ((config->Get_Beta_RKStep(iRKStep) != 0.0) || implicit) 
			node[iPoint]->Set_ResVisc_Zero();
	}

	/*--- Initialize the jacobian matrices ---*/
	if (implicit) Jacobian.SetValZero();

	// Be careful! it breaks the MPI parallelization!
//	bool jouleheating = (config->GetJouleHeating() == YES);
//	if (jouleheating)	geometry->SetGeometryPlanes(config);


}

void CEulerSolution::SetTime_Step(CGeometry *geometry, CSolution **solution_container, CConfig *config, 
		unsigned short iMesh, unsigned long Iteration) {
	double *Normal, Area, Vol, Mean_SoundSpeed, Mean_ProjVel, Mean_BetaInc2, Lambda, Local_Delta_Time, Mean_DensictyInc,
	Global_Delta_Time = 1E6, Global_Delta_UnstTimeND, ProjVel, ProjVel_i, ProjVel_j;
	unsigned long iEdge, iVertex, iPoint, jPoint;
	unsigned short iDim, iMarker;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();
	bool centered = (config->GetKind_ConvNumScheme() == SPACE_CENTERED);
	bool grid_movement = config->GetGrid_Movement();

	Min_Delta_Time = 1.E6; Max_Delta_Time = 0.0;

	/*--- Set maximum inviscid eigenvalue to zero, and compute sound speed ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		node[iPoint]->SetMax_Lambda_Inv(0.0);
		if (centered) node[iPoint]->SetLambda(0.0);
	}

	/*--- Loop interior edges ---*/
	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

		/*--- Point identification, Normal vector and area ---*/
		iPoint = geometry->edge[iEdge]->GetNode(0); 
		jPoint = geometry->edge[iEdge]->GetNode(1);

		Normal = geometry->edge[iEdge]->GetNormal();
		Area = 0.0; 
		for (iDim = 0; iDim < nDim; iDim++) 
			Area += Normal[iDim]*Normal[iDim]; 
		Area = sqrt(Area);

		/*--- Mean Values ---*/
		if (incompressible) {
			Mean_ProjVel = 0.5 * (node[iPoint]->GetProjVelInc(Normal) + node[jPoint]->GetProjVelInc(Normal));
			Mean_BetaInc2 = 0.5 * (node[iPoint]->GetBetaInc2() + node[jPoint]->GetBetaInc2());
			Mean_DensictyInc = 0.5 * (node[iPoint]->GetDensityInc() + node[jPoint]->GetDensityInc());
			Mean_SoundSpeed = sqrt(Mean_ProjVel*Mean_ProjVel + (Mean_BetaInc2/Mean_DensictyInc)*Area*Area);
		}
		else {
			Mean_ProjVel = 0.5 * (node[iPoint]->GetProjVel(Normal) + node[jPoint]->GetProjVel(Normal));
			Mean_SoundSpeed = 0.5 * (node[iPoint]->GetSoundSpeed() + node[jPoint]->GetSoundSpeed()) * Area;
		}

		/*--- Adjustment for a rotating frame ---*/
		if (rotating_frame) {
			double *RotVel_i = geometry->node[iPoint]->GetRotVel();
			double *RotVel_j = geometry->node[jPoint]->GetRotVel();
			ProjVel_i = 0.0; ProjVel_j =0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				ProjVel_i += RotVel_i[iDim]*Normal[iDim];
				ProjVel_j += RotVel_j[iDim]*Normal[iDim];
			}
			Mean_ProjVel -= 0.5 * (ProjVel_i + ProjVel_j);
		}

		/*--- Adjustment for grid movement ---*/
		if (grid_movement) {
			double *GridVel_i = geometry->node[iPoint]->GetGridVel();
			double *GridVel_j = geometry->node[jPoint]->GetGridVel();
			ProjVel_i = 0.0; ProjVel_j =0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				ProjVel_i += GridVel_i[iDim]*Normal[iDim];
				ProjVel_j += GridVel_j[iDim]*Normal[iDim];
			}
			Mean_ProjVel -= 0.5 * (ProjVel_i + ProjVel_j);
		}

		/*--- Inviscid contribution ---*/
		Lambda = fabs(Mean_ProjVel) + Mean_SoundSpeed;
		if (geometry->node[iPoint]->GetDomain()) node[iPoint]->AddMax_Lambda_Inv(Lambda);
		if (geometry->node[jPoint]->GetDomain()) node[jPoint]->AddMax_Lambda_Inv(Lambda);
		if (centered) {
			if (geometry->node[iPoint]->GetDomain()) node[iPoint]->AddLambda(Lambda);
			if (geometry->node[jPoint]->GetDomain()) node[jPoint]->AddLambda(Lambda);
		}

	}

	/*--- Loop boundary edges ---*/
	for (iMarker = 0; iMarker < geometry->GetnMarker(); iMarker++) { 
		for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {

			/*--- Point identification, Normal vector and area ---*/
			iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
			Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
			Area = 0.0; 
			for (iDim = 0; iDim < nDim; iDim++) 
				Area += Normal[iDim]*Normal[iDim]; 
			Area = sqrt(Area);

			/*--- Mean Values ---*/
			if (incompressible) {
				Mean_ProjVel = node[iPoint]->GetProjVelInc(Normal);
				Mean_BetaInc2 = node[iPoint]->GetBetaInc2();
				Mean_DensictyInc = node[iPoint]->GetDensityInc();
				Mean_SoundSpeed = sqrt(Mean_ProjVel*Mean_ProjVel + (Mean_BetaInc2/Mean_DensictyInc)*Area*Area); 
			}
			else {
				Mean_ProjVel = node[iPoint]->GetProjVel(Normal);
				Mean_SoundSpeed = node[iPoint]->GetSoundSpeed() * Area;		
			}

			/*--- Adjustment for a rotating frame ---*/
			if (rotating_frame) {
				double *RotVel = geometry->node[iPoint]->GetRotVel();
				ProjVel = 0.0;
				for (iDim = 0; iDim < nDim; iDim++)
					ProjVel += RotVel[iDim]*Normal[iDim];
				Mean_ProjVel -= ProjVel;
			}

			/*--- Adjustment for grid movement ---*/
			if (grid_movement) {
				double *GridVel = geometry->node[iPoint]->GetGridVel();
				ProjVel = 0.0;
				for (iDim = 0; iDim < nDim; iDim++)
					ProjVel += GridVel[iDim]*Normal[iDim];
				Mean_ProjVel -= ProjVel;
			}

			/*--- Inviscid contribution ---*/
			Lambda = fabs(Mean_ProjVel) + Mean_SoundSpeed;
			if (geometry->node[iPoint]->GetDomain()) {
				node[iPoint]->AddMax_Lambda_Inv(Lambda);
				if (centered) node[iPoint]->AddLambda(Lambda);
			}

		}
	}

	/*--- Each element uses their own speed, steady state simulation ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Vol = geometry->node[iPoint]->GetVolume();
		Local_Delta_Time = config->GetCFL(iMesh)*Vol / (node[iPoint]->GetMax_Lambda_Inv() + EPS);
		/*--- Check if there is any element with only one neighbor... 
		 a CV that is inside another CV ---*/
		if (geometry->node[iPoint]->GetnPoint() == 1) Local_Delta_Time = 0.0;
		Global_Delta_Time = min(Global_Delta_Time, Local_Delta_Time);
		Min_Delta_Time = min(Min_Delta_Time, Local_Delta_Time);
		Max_Delta_Time = max(Max_Delta_Time, Local_Delta_Time);
		node[iPoint]->SetDelta_Time(Local_Delta_Time);
	}

	/*--- Reduce the CFL number in the interfaces, and nearfield ---*/
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++)
		if ((config->GetMarker_All_Boundary(iMarker) == INTERFACE_BOUNDARY) ||
				(config->GetMarker_All_Boundary(iMarker) == NEARFIELD_BOUNDARY))
			for(iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
				iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
				Local_Delta_Time = node[iPoint]->GetDelta_Time()/2.0;
				node[iPoint]->SetDelta_Time(Local_Delta_Time);			
			}

	/*--- For exact time solution use the minimum delta time of the whole mesh ---*/
	if (config->GetUnsteady_Simulation() == TIME_STEPPING) {
#ifndef NO_MPI
		double rbuf_time, sbuf_time;
		sbuf_time = Global_Delta_Time;
		MPI::COMM_WORLD.Reduce(&sbuf_time, &rbuf_time, 1, MPI::DOUBLE, MPI::MIN, MASTER_NODE);
		MPI::COMM_WORLD.Bcast(&rbuf_time, 1, MPI::DOUBLE, MASTER_NODE);
		MPI::COMM_WORLD.Barrier();
		Global_Delta_Time = rbuf_time;
#endif
		for(iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
			node[iPoint]->SetDelta_Time(Global_Delta_Time);
	}

	/*--- Recompute the unsteady time step for the dual time strategy 
	 if the unsteady CFL is diferent from 0 ---*/
	if (((config->GetUnsteady_Simulation() == DT_STEPPING_1ST) || (config->GetUnsteady_Simulation() == DT_STEPPING_2ND)) && 
			(Iteration == 0) && (config->GetUnst_CFL() != 0.0) && (iMesh == MESH_0)) {
		Global_Delta_UnstTimeND = config->GetUnst_CFL()*Global_Delta_Time/config->GetCFL(iMesh);

#ifndef NO_MPI
		double rbuf_time, sbuf_time;
		sbuf_time = Global_Delta_UnstTimeND;
		MPI::COMM_WORLD.Reduce(&sbuf_time, &rbuf_time, 1, MPI::DOUBLE, MPI::MIN, MASTER_NODE);
		MPI::COMM_WORLD.Bcast(&rbuf_time, 1, MPI::DOUBLE, MASTER_NODE);
		MPI::COMM_WORLD.Barrier();
		Global_Delta_UnstTimeND = rbuf_time;
#endif
		config->SetDelta_UnstTimeND(Global_Delta_UnstTimeND);
	}

	/*--- The pseudo local time (explicit integration) cannot be greater than the physical time ---*/
	if ((config->GetUnsteady_Simulation() == DT_STEPPING_1ST) 
			|| (config->GetUnsteady_Simulation() == DT_STEPPING_2ND))
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
			if (!implicit) {
				Local_Delta_Time = min((2.0/3.0)*config->GetDelta_UnstTimeND(), node[iPoint]->GetDelta_Time());
				/*--- Check if there is any element with only one neighbor... 
				 a CV that is inside another CV ---*/
				if (geometry->node[iPoint]->GetnPoint() == 1) Local_Delta_Time = 0.0;
				node[iPoint]->SetDelta_Time(Local_Delta_Time);
			}
		}

}

void CEulerSolution::Centered_Residual(CGeometry *geometry, CSolution **solution_container, CNumerics *solver, 
		CConfig *config, unsigned short iMesh, unsigned short iRKStep) {
	unsigned long iEdge, iPoint, jPoint;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool dissipation = ((config->Get_Beta_RKStep(iRKStep) != 0) || implicit);
	bool high_order_diss = ((config->GetKind_Centered() == JST) && (iMesh == MESH_0));
	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();
	bool grid_movement = config->GetGrid_Movement();

	/*--- Artificial dissipation preprocessing ---*/
	if (dissipation && high_order_diss) {
		SetDissipation_Switch(geometry, solution_container, config); 
		SetUndivided_Laplacian(geometry, config); 
	}

	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

		/*--- Points in edge, set normal vectors, and number of neighbors ---*/
		iPoint = geometry->edge[iEdge]->GetNode(0);
		jPoint = geometry->edge[iEdge]->GetNode(1);
		solver->SetNormal(geometry->edge[iEdge]->GetNormal());
		solver->SetNeighbor(geometry->node[iPoint]->GetnNeighbor(), geometry->node[jPoint]->GetnNeighbor());

		/*--- Set conservative variables w/o reconstruction ---*/
		solver->SetConservative(node[iPoint]->GetSolution(), node[jPoint]->GetSolution());

		if (incompressible) {
			solver->SetDensityInc(node[iPoint]->GetDensityInc(), node[jPoint]->GetDensityInc());
			solver->SetBetaInc2(node[iPoint]->GetBetaInc2(), node[jPoint]->GetBetaInc2());
			solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[jPoint]->GetCoord());
		}
		else {
			solver->SetPressure(node[iPoint]->GetPressure(incompressible), node[jPoint]->GetPressure(incompressible));
			solver->SetSoundSpeed(node[iPoint]->GetSoundSpeed(), node[jPoint]->GetSoundSpeed());
			solver->SetEnthalpy(node[iPoint]->GetEnthalpy(), node[jPoint]->GetEnthalpy());
		}

		solver->SetLambda(node[iPoint]->GetLambda(), node[jPoint]->GetLambda());

		if (dissipation && high_order_diss) {
			solver->SetUndivided_Laplacian(node[iPoint]->GetUnd_Lapl(), node[jPoint]->GetUnd_Lapl());
			solver->SetSensor(node[iPoint]->GetSensor(), node[jPoint]->GetSensor()); 
		}

		/*--- Rotational Frame ---*/
		if (rotating_frame) {
			solver->SetRotVel(geometry->node[iPoint]->GetRotVel(), geometry->node[jPoint]->GetRotVel());
			solver->SetRotFlux(geometry->edge[iEdge]->GetRotFlux());
		}

		/*--- Grid Movement ---*/
		if (grid_movement)
			solver->SetGridVel(geometry->node[iPoint]->GetGridVel(), geometry->node[jPoint]->GetGridVel());

		/*--- Compute residuals ---*/
		solver->SetResidual(Res_Conv, Res_Visc, Jacobian_i, Jacobian_j, config);

		/*--- Update convective and artificial dissipation residuals ---*/
		node[iPoint]->AddRes_Conv(Res_Conv);
		node[jPoint]->SubtractRes_Conv(Res_Conv);
		if (dissipation) {
			node[iPoint]->AddRes_Visc(Res_Visc);
			node[jPoint]->SubtractRes_Visc(Res_Visc); 
		}

		/*--- Set implicit stuff ---*/
		if (implicit) {
			Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
			Jacobian.AddBlock(iPoint,jPoint,Jacobian_j);
			Jacobian.SubtractBlock(jPoint,iPoint,Jacobian_i);
			Jacobian.SubtractBlock(jPoint,jPoint,Jacobian_j); 
		}
	}
}

void CEulerSolution::Upwind_Residual(CGeometry *geometry, CSolution **solution_container, CNumerics *solver, 
		CConfig *config, unsigned short iMesh) {
	double Pressure_Old_i, Pressure_Old_j, **Gradient_i, **Gradient_j, Project_Grad_i, Project_Grad_j, 
	*U_i, *U_j, sqvel;
	unsigned long iEdge, iPoint, jPoint;
	unsigned short iDim, iVar;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool high_order_diss = (((config->GetKind_Upwind() == ROE_2ND) || (config->GetKind_Upwind() == AUSM_2ND) 
			|| (config->GetKind_Upwind() == HLLC_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_2ND))
			&& (iMesh == MESH_0));
	bool incompressible = config->GetIncompressible();
	bool rotating_frame = config->GetRotating_Frame();
	bool gravity = config->GetGravityForce();
	bool grid_movement = config->GetGrid_Movement();

	if (high_order_diss) { 
		if (config->GetKind_Gradient_Method() == GREEN_GAUSS) SetSolution_Gradient_GG(geometry); 
		if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) SetSolution_Gradient_LS(geometry, config);
		if (config->GetKind_SlopeLimit() != NONE) SetSolution_Limiter(geometry, config);
	}

	for(iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

		/*--- Points in edge and normal vectors ---*/
		iPoint = geometry->edge[iEdge]->GetNode(0); jPoint = geometry->edge[iEdge]->GetNode(1);
		solver->SetNormal(geometry->edge[iEdge]->GetNormal());

		/*--- Set conservative variables w/o reconstruction ---*/
		U_i = node[iPoint]->GetSolution(); U_j = node[jPoint]->GetSolution();
		solver->SetConservative(U_i, U_j);

		/*--- Set the old value of the pressure, in case the new pressure is negative ---*/
		Pressure_Old_i = node[iPoint]->GetPressure_Old(); Pressure_Old_j = node[jPoint]->GetPressure_Old();
		solver->SetPressure_Old(Pressure_Old_i, Pressure_Old_j);

		/*--- Roe Turkel preconditioning ---*/
		if (roe_turkel) {
			sqvel = 0.0;
			for (iDim = 0; iDim < nDim; iDim ++)
				sqvel += config->GetVelocity_FreeStream()[iDim]*config->GetVelocity_FreeStream()[iDim];
			solver->SetVelocity2_Inf(sqvel);
		}

		/*--- Rotating frame ---*/
		if (rotating_frame) {
			solver->SetRotVel(geometry->node[iPoint]->GetRotVel(), geometry->node[jPoint]->GetRotVel());
			solver->SetRotFlux(geometry->edge[iEdge]->GetRotFlux());
		}

		/*--- Grid Movement ---*/
		if (grid_movement)
			solver->SetGridVel(geometry->node[iPoint]->GetGridVel(), geometry->node[jPoint]->GetGridVel());

		/*--- High order reconstruction using MUSCL strategy ---*/
		if (high_order_diss) {
			
			for (iDim = 0; iDim < nDim; iDim++) {
				Vector_i[iDim] = 0.5*(geometry->node[jPoint]->GetCoord(iDim) - geometry->node[iPoint]->GetCoord(iDim));
				Vector_j[iDim] = 0.5*(geometry->node[iPoint]->GetCoord(iDim) - geometry->node[jPoint]->GetCoord(iDim));
			}

			Gradient_i = node[iPoint]->GetGradient(); 
			Gradient_j = node[jPoint]->GetGradient();

			for (iVar = 0; iVar < nVar; iVar++) {
				Project_Grad_i = 0; Project_Grad_j = 0;
				for (iDim = 0; iDim < nDim; iDim++) {
					Project_Grad_i += Vector_i[iDim]*Gradient_i[iVar][iDim];
					Project_Grad_j += Vector_j[iDim]*Gradient_j[iVar][iDim];
				}
				Solution_i[iVar] = U_i[iVar] + Project_Grad_i; 
				Solution_j[iVar] = U_j[iVar] + Project_Grad_j;
			}

			/*--- Set conservative variables with reconstruction ---*/
			solver->SetConservative(Solution_i, Solution_j);
		}
		else {
			if (incompressible && gravity) {
				double YDistance = 0.5*(geometry->node[jPoint]->GetCoord(nDim-1)-geometry->node[iPoint]->GetCoord(nDim-1));			
				double GradHidrosPress = -node[iPoint]->GetDensityInc()/(config->GetFroude()*config->GetFroude());

				Solution_i[0] = U_i[0] + GradHidrosPress*YDistance;
				Solution_j[0] = U_j[0] - GradHidrosPress*YDistance;

				for (iVar = 1; iVar < nVar; iVar++) {
					Solution_i[iVar] = U_i[iVar];
					Solution_j[iVar] = U_j[iVar];
				}

				/*--- Set conservative variables with reconstruction only for the pressure ---*/
				solver->SetConservative(Solution_i, Solution_j);
			}
		}

		/*--- Set the density and beta w/o reconstruction (incompressible flows) ---*/
		if (incompressible) {
			solver->SetBetaInc2(node[iPoint]->GetBetaInc2(), node[jPoint]->GetBetaInc2());
			solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[jPoint]->GetCoord());
			solver->SetDensityInc(node[iPoint]->GetDensityInc(), node[jPoint]->GetDensityInc());
		}

		/*--- Compute the residual ---*/
		solver->SetResidual(Res_Conv, Jacobian_i, Jacobian_j, config); 

		/*--- Update residual value ---*/
		node[iPoint]->AddRes_Conv(Res_Conv);
		node[jPoint]->SubtractRes_Conv(Res_Conv);

		/*--- Set implicit jacobians ---*/
		if (implicit) {
			Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);
			Jacobian.AddBlock(iPoint, jPoint, Jacobian_j);
			Jacobian.SubtractBlock(jPoint, iPoint, Jacobian_i);
			Jacobian.SubtractBlock(jPoint, jPoint, Jacobian_j);
		}

		/*--- Roe Turkel preconditioning, set the value of beta ---*/
		if (roe_turkel) {
			node[iPoint]->SetPreconditioner_Beta(solver->GetPrecond_Beta());
			node[jPoint]->SetPreconditioner_Beta(solver->GetPrecond_Beta());
		}

	}

}

void CEulerSolution::Source_Residual(CGeometry *geometry, CSolution **solution_container, CNumerics *solver,
		CConfig *config, unsigned short iMesh) {

	unsigned short iVar;
	unsigned long iPoint;
	bool rotating_frame = config->GetRotating_Frame();
	bool axisymmetric = config->GetAxisymmetric();
	bool incompressible = config->GetIncompressible();
	bool gravity = (config->GetGravityForce() == YES);
	bool time_spectral = (config->GetUnsteady_Simulation() == TIME_SPECTRAL);
	bool magnet = (config->GetMagnetic_Force() == YES);
	bool jouleheating = (config->GetJouleHeating() == YES);

	for (iVar = 0; iVar < nVar; iVar++)
		Residual[iVar] = 0;

	if (rotating_frame) {

		/*--- loop over points ---*/
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) { 

			/*--- Set solution  ---*/
			solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

			/*--- Set control volume ---*/
			solver->SetVolume(geometry->node[iPoint]->GetVolume());

			/*--- Set rotational velocity ---*/
			solver->SetRotVel(geometry->node[iPoint]->GetRotVel(), geometry->node[iPoint]->GetRotVel());

			/*--- Compute Residual ---*/
			solver->SetResidual(Residual, Jacobian_i, config);

			/*--- Add Residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

		}
	}

	if (axisymmetric) {

		/*--- loop over points ---*/
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) { 

			/*--- Set solution  ---*/
			solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

			if (incompressible) {
				/*--- Set incompressible density  ---*/
				solver->SetDensityInc(node[iPoint]->GetDensityInc(), node[iPoint]->GetDensityInc());

				/*--- Set beta squared  ---*/
				solver->SetBetaInc2(node[iPoint]->GetBetaInc2(), node[iPoint]->GetBetaInc2());
			}

			/*--- Set control volume ---*/
			solver->SetVolume(geometry->node[iPoint]->GetVolume());

			/*--- Set y coordinate ---*/
			solver->SetCoord(geometry->node[iPoint]->GetCoord(),geometry->node[iPoint]->GetCoord());

			/*--- Compute Source term Residual ---*/
			solver->SetResidual(Residual, config);

			/*--- Add Residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

		}
	}

	if (gravity) {

		/*--- loop over points ---*/
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) { 

			/*--- Set solution  ---*/
			solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

			/*--- Set incompressible density  ---*/
			solver->SetDensityInc(node[iPoint]->GetDensityInc(), node[iPoint]->GetDensityInc());

			/*--- Set control volume ---*/
			solver->SetVolume(geometry->node[iPoint]->GetVolume());

			/*--- Compute Source term Residual ---*/
			solver->SetResidual(Residual, config);

			/*--- Add Residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

		}
	}

	if (time_spectral) {

		double Volume, Source;

		/*--- loop over points ---*/
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {

			/*--- Get control volume ---*/
			Volume = geometry->node[iPoint]->GetVolume();

			/*--- Get stored time spectral source term ---*/
			for (iVar = 0; iVar < nVar; iVar++) {
				Source = node[iPoint]->GetTimeSpectral_Source(iVar);
				Residual[iVar] = Source*Volume;
			}

			/*--- Add Residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

		}
	}

	if (magnet) {
		bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
		for (iVar = 0; iVar < nVar; iVar ++)
			for (unsigned short jVar = 0; jVar < nVar; jVar ++)
				Jacobian_i[iVar][jVar] = 0.0;
		/*--- loop over points ---*/
		for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {

			/*--- Set the coordinate ---*/
			solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[iPoint]->GetCoord());

			/*--- Set solution  ---*/
			solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

			/*--- Set control volume ---*/
			solver->SetVolume(geometry->node[iPoint]->GetVolume());

			/*--- Compute Residual ---*/
			solver->SetResidual(Residual, Jacobian_i, config);

			node[iPoint]->SubtractRes_Sour(Residual);
			if (nDim ==3) node[iPoint]->SetMagneticField(solver->GetMagneticField());
			if (implicit)
				Jacobian.SubtractBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	if (jouleheating) {

		bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
		for (iVar = 0; iVar < nVar; iVar ++)
			for (unsigned short jVar = 0; jVar < nVar; jVar ++)
				Jacobian_i[iVar][jVar] = 0.0;

		double integral = 0.0; unsigned long jPoint;
		vector<double> XCoordList = geometry->GetGeometryPlanes();
		vector<vector<double> > Xcoord_plane = geometry->GetXCoord();
		vector<vector<double> > Ycoord_plane = geometry->GetYCoord();
		vector<vector<unsigned long> > Plane_points = geometry->GetPlanarPoints();

		for (unsigned short iPlane = 0; iPlane < XCoordList.size(); iPlane++) {
			integral= 0.0;

			for (unsigned short iVertex = 1; iVertex < Xcoord_plane[iPlane].size(); iVertex++) {
				iPoint = Plane_points[iPlane][iVertex];
				jPoint = Plane_points[iPlane][iVertex-1];
				//cout << "iPoint = " << iPoint << ", 	 x = " << geometry->node[iPoint]->GetCoord(0) << " y = " << geometry->node[iPoint]->GetCoord(1) << endl;

				/*--- Set solution  ---*/
				solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

				/*--- Set control volume ---*/
				solver->SetVolume(geometry->node[iPoint]->GetVolume());

				/*--- Set the coordinate ---*/
				solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[jPoint]->GetCoord());

				/*--- Set electrical conductivity  ---*/
				solver->SetElec_Cond();

				/*--- Sum over points to get the integral  ---*/
				integral +=solver->GetElec_CondIntegral();
			}

			/*--- Set the integral  ---*/
			solver->SetElec_CondIntegralsqr(integral*integral);
			//cout << " integral = " << integral << endl;

			for (unsigned short iVertex = 0; iVertex < Xcoord_plane[iPlane].size(); iVertex++) {
				iPoint = Plane_points[iPlane][iVertex];

				/*--- Set solution  ---*/
				solver->SetConservative(node[iPoint]->GetSolution(), node[iPoint]->GetSolution());

				/*--- Set control volume ---*/
				solver->SetVolume(geometry->node[iPoint]->GetVolume());

				/*--- Set the coordinate ---*/
				solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[iPoint]->GetCoord());

				/*--- Set electrical conductivity  ---*/
				solver->SetElec_Cond();

				/*--- Compute Residual ---*/
				solver->SetResidual(Residual, Jacobian_i, config);
				//				for (iVar = 0; iVar < nVar; iVar++)
				//					cout << Residual[iVar] << ", ";
				//				cout << endl;
				node[iPoint]->SubtractRes_Sour(Residual);
				if (implicit) Jacobian.SubtractBlock(iPoint, iPoint, Jacobian_i);
			}
		}
	}
}

void CEulerSolution::Source_Template(CGeometry *geometry, CSolution **solution_container, CNumerics *solver,
		CConfig *config, unsigned short iMesh) {

	/* This method should be used to call any new source terms for a particular problem*/
	/* This method calls the new child class in CNumerics, where the new source term should be implemented.  */

	/* Next we describe how to get access to some important quanties for this method */
	/* Access to all points in the current geometric mesh by saying: geometry->GetnPointDomain() */
	/* Get the vector of conservative variables at some point iPoint = node[iPoint]->GetSolution() */
	/* Get the volume (or area in 2D) associated with iPoint = node[iPoint]->GetVolume() */
	/* Get the vector of geometric coordinates of point iPoint = node[iPoint]->GetCoord() */

}


void CEulerSolution::SetUndivided_Laplacian(CGeometry *geometry, CConfig *config) {

	unsigned long iPoint, jPoint, iEdge, Point_Normal = 0, iVertex;
	double Pressure_i = 0, Pressure_j = 0, *Normal;
	unsigned short iVar, iMarker;
	double *Diff = new double[nVar];
	double *U_halo = new double[nVar];
	bool incompressible = config->GetIncompressible();

	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
		node[iPoint]->SetUnd_LaplZero();

	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {
		iPoint = geometry->edge[iEdge]->GetNode(0);
		jPoint = geometry->edge[iEdge]->GetNode(1);

		/*--- Solution differences ---*/
		for (iVar = 0; iVar < nVar; iVar++)
			Diff[iVar] = node[iPoint]->GetSolution(iVar) - node[jPoint]->GetSolution(iVar);

		/*--- Correction for compressible flows which use the enthalpy ---*/
		if (!incompressible) {
			Pressure_i = node[iPoint]->GetPressure(incompressible); Pressure_j = node[jPoint]->GetPressure(incompressible);
			Diff[nVar-1] = (node[iPoint]->GetSolution(nVar-1) + Pressure_i) - (node[jPoint]->GetSolution(nVar-1) + Pressure_j);
		}

		if (geometry->node[iPoint]->GetDomain()) node[iPoint]->SubtractUnd_Lapl(Diff);
		if (geometry->node[jPoint]->GetDomain()) node[jPoint]->AddUnd_Lapl(Diff);

	}

	/*--- Loop over all boundaries and include an extra contribution
        from a halo node. Find the nearest normal, interior point 
        for a boundary node and make a linear approximation. ---*/
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++)
		if (config->GetMarker_All_Boundary(iMarker) != SEND_RECEIVE &&
				config->GetMarker_All_Boundary(iMarker) != INTERFACE_BOUNDARY &&
				config->GetMarker_All_Boundary(iMarker) != NEARFIELD_BOUNDARY )
			for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
				iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
				if (geometry->node[iPoint]->GetDomain()) {
					Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

					Point_Normal = geometry->vertex[iMarker][iVertex]->GetClosest_Neighbor();

					/*--- Interpolate & compute difference in the conserved variables ---*/
					for (iVar = 0; iVar < nVar; iVar++) {
						if (config->GetMarker_All_Boundary(iMarker) == EULER_WALL
								|| config->GetMarker_All_Boundary(iMarker) == NO_SLIP_WALL) {
							U_halo[iVar] = node[Point_Normal]->GetSolution(iVar);
						} else {
							U_halo[iVar] = 2.0*node[iPoint]->GetSolution(iVar) - node[Point_Normal]->GetSolution(iVar);
						}
						Diff[iVar]   = node[iPoint]->GetSolution(iVar) - U_halo[iVar];
					}

					/*--- Correction for compressible flows ---*/
					if (!incompressible) {
						Pressure_i = node[iPoint]->GetPressure(incompressible);
						Pressure_j = 2.0*node[iPoint]->GetPressure(incompressible) - node[Point_Normal]->GetPressure(incompressible);
						Diff[nVar-1] = (node[iPoint]->GetSolution(nVar-1) + Pressure_i) - (U_halo[nVar-1] + Pressure_j);
					}

					/*--- Subtract contribution at the boundary node only ---*/
					if (geometry->node[iPoint]->GetDomain()) node[iPoint]->SubtractUnd_Lapl(Diff);
				}
			}

	delete [] Diff;
	delete [] U_halo;
}

void CEulerSolution::SetDissipation_Switch(CGeometry *geometry, CSolution **solution_container, CConfig *config) {

	unsigned long iEdge, iPoint, jPoint;
	double Pressure_i, Pressure_j;

	bool incompressible = config->GetIncompressible();

	/*--- Reset variables to store the undivided pressure ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		p1_Und_Lapl[iPoint] = 0.0;
		p2_Und_Lapl[iPoint] = 0.0;
	}

	/*--- Evaluate the pressure sensor ---*/
	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

		iPoint = geometry->edge[iEdge]->GetNode(0);
		jPoint = geometry->edge[iEdge]->GetNode(1);

		if (!incompressible) {
			Pressure_i = node[iPoint]->GetPressure(incompressible);
			Pressure_j = node[jPoint]->GetPressure(incompressible);
		}
		else {
			Pressure_i = node[iPoint]->GetDensityInc();
			Pressure_j = node[jPoint]->GetDensityInc();
		}

		if (geometry->node[iPoint]->GetDomain()) p1_Und_Lapl[iPoint] += (Pressure_j - Pressure_i);
		if (geometry->node[jPoint]->GetDomain()) p1_Und_Lapl[jPoint] += (Pressure_i - Pressure_j);

		if (geometry->node[iPoint]->GetDomain()) p2_Und_Lapl[iPoint] += (Pressure_i + Pressure_j);
		if (geometry->node[jPoint]->GetDomain()) p2_Und_Lapl[jPoint] += (Pressure_i + Pressure_j);

	}

	/*--- Set pressure switch for each point ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
		node[iPoint]->SetSensor(fabs(p1_Und_Lapl[iPoint]) / p2_Und_Lapl[iPoint]);

}

void CEulerSolution::Inviscid_Forces(CGeometry *geometry, CConfig *config) {
	unsigned long iVertex, iPoint;
	unsigned short iDim, iMarker, Boundary, Monitoring;
	double Pressure, *Normal = NULL, dist[3], *Coord, Face_Area, PressInviscid;
	double factor, NFPressOF, RefVel2, RefDensity, RefPressure;
	bool rotating_frame = config->GetRotating_Frame();

	double Alpha           = config->GetAoA()*PI_NUMBER/180.0;
	double Beta            = config->GetAoS()*PI_NUMBER/180.0;
	double RefAreaCoeff    = config->GetRefAreaCoeff();
	double RefLengthMoment = config->GetRefLengthMoment();
	double *Origin         = config->GetRefOriginMoment();
	double Pressure_Inf    = config->GetPressure_FreeStreamND();
	double *Velocity_Inf   = config->GetVelocity_FreeStreamND();
	bool incompressible    = config->GetIncompressible();

	/*--- Reference values ---*/
	if (!rotating_frame) {
		/*--- Reference velocity is the velocity at the infinity ---*/
		RefVel2 = 0.0;
		for (iDim = 0; iDim < nDim; iDim++)
			RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
	}
	else {
		/*--- Use the rotational origin for computing moments ---*/
		Origin = config->GetRotAxisOrigin();

		/*--- Reference area is the rotor disk area ---*/
		RefLengthMoment = config->GetRotRadius();
		RefAreaCoeff = PI_NUMBER*RefLengthMoment*RefLengthMoment;

		/*--- Reference velocity is the rotational speed times rotor radius ---*/
		RefVel2 = (config->GetOmegaMag()*RefLengthMoment)*(config->GetOmegaMag()*RefLengthMoment);
	}

	RefDensity  = config->GetDensity_FreeStreamND();
	RefPressure = config->GetPressure_FreeStreamND();
		
	/*-- Initialization ---*/
	Total_CDrag = 0.0; Total_CLift = 0.0;  Total_CSideForce = 0.0;
	Total_CMx = 0.0;   Total_CMy = 0.0;    Total_CMz = 0.0;
	Total_CFx = 0.0;   Total_CFy = 0.0;    Total_CFz = 0.0;
	Total_CEff = 0.0;  Total_CMerit = 0.0; Total_CNearFieldOF = 0.0;
	Total_CT = 0.0;    Total_CQ = 0.0;
	AllBound_CDrag_Inv = 0.0;        AllBound_CLift_Inv = 0.0;  AllBound_CSideForce_Inv = 0.0;
	AllBound_CMx_Inv = 0.0;          AllBound_CMy_Inv = 0.0;    AllBound_CMz_Inv = 0.0;
	AllBound_CFx_Inv = 0.0;          AllBound_CFy_Inv = 0.0;    AllBound_CFz_Inv = 0.0;
	AllBound_CEff_Inv = 0.0;         AllBound_CMerit_Inv = 0.0; AllBound_CQ_Inv = 0.0;
	AllBound_CNearFieldOF_Inv = 0.0; AllBound_CT_Inv = 0.0;

	/*--- Loop over the Euler and Navier-Stokes markers ---*/
	factor = 1.0 / (0.5*RefDensity*RefAreaCoeff*RefVel2);

	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
		Boundary   = config->GetMarker_All_Boundary(iMarker);
		Monitoring = config->GetMarker_All_Monitoring(iMarker);

		if ((Boundary == EULER_WALL) || (Boundary == NO_SLIP_WALL) || (Boundary == NEARFIELD_BOUNDARY)) {

			for (iDim = 0; iDim < nDim; iDim++) ForceInviscid[iDim] = 0.0;
			MomentInviscid[0] = 0.0; MomentInviscid[1] = 0.0; MomentInviscid[2] = 0.0;
			NFPressOF = 0.0; PressInviscid = 0.0;

			for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {
				iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
				Pressure = node[iPoint]->GetPressure(incompressible);

				CPressure[iMarker][iVertex] = (Pressure - RefPressure)*factor*RefAreaCoeff;
				
				/*--- Note that the pressure coefficient is computed at the
				 halo cells (for visualization purposes), but not the forces ---*/
				if ( (geometry->node[iPoint]->GetDomain()) && (Monitoring == YES) ) {
					Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
					Coord = geometry->node[iPoint]->GetCoord();

					/*--- Quadratic objective function for the near field.
           This uses the infinity pressure regardless of Mach number. ---*/
					NFPressOF += 0.5*(Pressure - Pressure_Inf)*(Pressure - Pressure_Inf)*Normal[nDim-1];

					Face_Area = 0.0;
					for (iDim = 0; iDim < nDim; iDim++) {
						/*--- Total force, distance computation, and face area ---*/
						ForceInviscid[iDim] -= Pressure*Normal[iDim]*factor;
						dist[iDim] = Coord[iDim] - Origin[iDim];
						Face_Area += Normal[iDim]*Normal[iDim];
					}
					Face_Area = sqrt(Face_Area);
					PressInviscid += CPressure[iMarker][iVertex]*Face_Area;

					/*--- Moment with respect to the reference axis ---*/
					if (iDim == 3) {
						MomentInviscid[0] -= Pressure*(Normal[2]*dist[1]-Normal[1]*dist[2])*factor/RefLengthMoment;
						MomentInviscid[1] -= Pressure*(Normal[0]*dist[2]-Normal[2]*dist[0])*factor/RefLengthMoment;
					}
					MomentInviscid[2]   -= Pressure*(Normal[1]*dist[0]-Normal[0]*dist[1])*factor/RefLengthMoment;

				}
			}

			/*--- Transform ForceInviscid and MomentInviscid into non-dimensional coefficient ---*/
			if  (Monitoring == YES) {
				if (nDim == 2) {
					if (Boundary != NEARFIELD_BOUNDARY) {
						CDrag_Inv[iMarker]        =  ForceInviscid[0]*cos(Alpha) + ForceInviscid[1]*sin(Alpha);
						CLift_Inv[iMarker]        = -ForceInviscid[0]*sin(Alpha) + ForceInviscid[1]*cos(Alpha);
						CSideForce_Inv[iMarker]   = 0.0;
						CMx_Inv[iMarker]          = 0.0;
						CMy_Inv[iMarker]          = 0.0;
						CMz_Inv[iMarker]          = MomentInviscid[2];
						CEff_Inv[iMarker]         = CLift_Inv[iMarker]/(CDrag_Inv[iMarker]+config->GetCteViscDrag());
						CNearFieldOF_Inv[iMarker] = 0.0;
						CFx_Inv[iMarker]          = ForceInviscid[0];
						CFy_Inv[iMarker]          = ForceInviscid[1];
						CFz_Inv[iMarker]          = 0.0;
						CT_Inv[iMarker]           = -CFz_Inv[iMarker];
						CQ_Inv[iMarker]           = -CMz_Inv[iMarker];
						CMerit_Inv[iMarker]       = 0.0;
					}
					else {
						CDrag_Inv[iMarker] = 0.0; CLift_Inv[iMarker] = 0.0; CSideForce_Inv[iMarker] = 0.0;
						CMx_Inv[iMarker] = 0.0; CMy_Inv[iMarker] = 0.0; CMz_Inv[iMarker] = 0.0;
						CFx_Inv[iMarker] = 0.0; CFy_Inv[iMarker] = 0.0; CFz_Inv[iMarker] = 0.0;
						CEff_Inv[iMarker] = 0.0;
						CNearFieldOF_Inv[iMarker] = NFPressOF;
						CT_Inv[iMarker] = 0.0;
						CQ_Inv[iMarker] = 0.0;
						CMerit_Inv[iMarker] = 0.0;
					}
				}
				if (nDim == 3) {
					if (Boundary != NEARFIELD_BOUNDARY) {
						CDrag_Inv[iMarker] =  ForceInviscid[0]*cos(Alpha)*cos(Beta) + ForceInviscid[1]*sin(Beta) + ForceInviscid[2]*sin(Alpha)*cos(Beta);
						CLift_Inv[iMarker] = -ForceInviscid[0]*sin(Alpha) + ForceInviscid[2]*cos(Alpha);
						CSideForce_Inv[iMarker] = -ForceInviscid[0]*sin(Beta)*cos(Alpha) + ForceInviscid[1]*cos(Beta) - ForceInviscid[2]*sin(Beta)*sin(Alpha);
						CMx_Inv[iMarker] = MomentInviscid[0];
						CMy_Inv[iMarker] = MomentInviscid[1];
						CMz_Inv[iMarker] = MomentInviscid[2];
						CEff_Inv[iMarker] = CLift_Inv[iMarker]/(CDrag_Inv[iMarker]+config->GetCteViscDrag());
						CNearFieldOF_Inv[iMarker] = 0.0;
						CFx_Inv[iMarker] = ForceInviscid[0];
						CFy_Inv[iMarker] = ForceInviscid[1];
						CFz_Inv[iMarker] = ForceInviscid[2];
						CT_Inv[iMarker] = -CFz_Inv[iMarker];
						CQ_Inv[iMarker] = -CMz_Inv[iMarker];
						/*--- For now, define the FM as a simple rotor efficiency ---*/
						//CMerit_Inv[iMarker] = CT_Inv[iMarker]*sqrt(fabs(CT_Inv[iMarker]))/(sqrt(2.0)*CQ_Inv[iMarker]);
						CMerit_Inv[iMarker] = CT_Inv[iMarker]/CQ_Inv[iMarker];
					}
					else {
						CDrag_Inv[iMarker] = 0.0; CLift_Inv[iMarker] = 0.0; CSideForce_Inv[iMarker] = 0.0;
						CMx_Inv[iMarker] = 0.0; CMy_Inv[iMarker] = 0.0; CMz_Inv[iMarker] = 0.0;
						CFx_Inv[iMarker] = 0.0; CFy_Inv[iMarker] = 0.0; CFz_Inv[iMarker] = 0.0;
						CEff_Inv[iMarker] = 0.0;
						CNearFieldOF_Inv[iMarker] = NFPressOF;
						CT_Inv[iMarker] = 0.0;
						CQ_Inv[iMarker] = 0.0;
						CMerit_Inv[iMarker] = 0.0;
					}
				}

				AllBound_CDrag_Inv += CDrag_Inv[iMarker];
				AllBound_CLift_Inv += CLift_Inv[iMarker];
				AllBound_CSideForce_Inv += CSideForce_Inv[iMarker];
				AllBound_CMx_Inv += CMx_Inv[iMarker];
				AllBound_CMy_Inv += CMy_Inv[iMarker];
				AllBound_CMz_Inv += CMz_Inv[iMarker];
				AllBound_CEff_Inv += CEff_Inv[iMarker];
				AllBound_CNearFieldOF_Inv += CNearFieldOF_Inv[iMarker];
				AllBound_CFx_Inv += CFx_Inv[iMarker];
				AllBound_CFy_Inv += CFy_Inv[iMarker];
				AllBound_CFz_Inv += CFz_Inv[iMarker];
				AllBound_CT_Inv += CT_Inv[iMarker];
				AllBound_CQ_Inv += CQ_Inv[iMarker];
				AllBound_CMerit_Inv += CMerit_Inv[iMarker];
			}
		}
	}

	Total_CDrag += AllBound_CDrag_Inv;
	Total_CLift += AllBound_CLift_Inv;
	Total_CSideForce += AllBound_CSideForce_Inv;
	Total_CMx += AllBound_CMx_Inv;
	Total_CMy += AllBound_CMy_Inv;
	Total_CMz += AllBound_CMz_Inv;
	Total_CEff += AllBound_CEff_Inv;
	Total_CNearFieldOF += AllBound_CNearFieldOF_Inv;
	Total_CFx += AllBound_CFx_Inv;
	Total_CFy += AllBound_CFy_Inv;
	Total_CFz += AllBound_CFz_Inv;
	Total_CT += AllBound_CT_Inv;
	Total_CQ += AllBound_CQ_Inv;
	Total_CMerit += AllBound_CMerit_Inv;
}

void CEulerSolution::ExplicitRK_Iteration(CGeometry *geometry, CSolution **solution_container,
		CConfig *config, unsigned short iRKStep) {
	double *Residual, *Res_TruncError, Vol, Delta;
	unsigned short iVar;
	unsigned long iPoint;

	double RK_AlphaCoeff = config->Get_Alpha_RKStep(iRKStep);

	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max(iVar, 0.0);

	/*--- Update the solution ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Vol = geometry->node[iPoint]->GetVolume();
		Delta = node[iPoint]->GetDelta_Time() / (Vol+EPS);
		Res_TruncError = node[iPoint]->GetRes_TruncError();
		Residual = node[iPoint]->GetResidual();
		for (iVar = 0; iVar < nVar; iVar++) {
			node[iPoint]->AddSolution(iVar, -(Residual[iVar]+Res_TruncError[iVar])*Delta*RK_AlphaCoeff);
			AddRes_Max( iVar, Residual[iVar]*Residual[iVar]*Vol );
		}
	}

#ifdef NO_MPI
	/*--- Compute the norm-2 of the residual ---*/
	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max( iVar, sqrt(GetRes_Max(iVar)));
#endif

}

void CEulerSolution::ExplicitEuler_Iteration(CGeometry *geometry, CSolution **solution_container, CConfig *config) {
	double *Residual, *Res_TruncError, Vol, Delta;
	unsigned short iVar;
	unsigned long iPoint;

	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max(iVar, 0.0);

	/*--- Update the solution ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Vol = geometry->node[iPoint]->GetVolume();
		Delta = node[iPoint]->GetDelta_Time() / (Vol + EPS);

		Res_TruncError = node[iPoint]->GetRes_TruncError();
		Residual = node[iPoint]->GetResidual();

		for (iVar = 0; iVar < nVar; iVar++) {
			node[iPoint]->AddSolution(iVar, -(Residual[iVar]+Res_TruncError[iVar])*Delta);
			AddRes_Max( iVar, Residual[iVar]*Residual[iVar]*Vol );
		}

	}


#ifdef NO_MPI
	/*--- Compute the norm-2 of the residual ---*/
	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max( iVar, sqrt(GetRes_Max(iVar)));
#endif

}

void CEulerSolution::ImplicitEuler_Iteration(CGeometry *geometry, CSolution **solution_container, CConfig *config) {
	unsigned short iVar;
	unsigned long iPoint, total_index, IterLinSol = 0;
	double Delta, Res, *local_ResConv, *local_ResVisc, *local_ResSour, *local_Res_TruncError, Vol;

	/*--- Set maximum residual to zero ---*/
	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max(iVar, 0.0);

	/*--- Build implicit system ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {

		/*--- Read the residual ---*/
		local_Res_TruncError = node[iPoint]->GetRes_TruncError();
		local_ResConv = node[iPoint]->GetResConv();
		local_ResVisc = node[iPoint]->GetResVisc();
		local_ResSour = node[iPoint]->GetResSour();

		/*--- Read the volume ---*/
		Vol = geometry->node[iPoint]->GetVolume();

		/*--- Modify matrix diagonal to assure diagonal dominance ---*/
		Delta = Vol / (node[iPoint]->GetDelta_Time() + EPS);

		if (roe_turkel) {
			SetPreconditioner_Turkel(config, iPoint);
			for (iVar = 0; iVar < nVar; iVar ++ ) {
				for (unsigned short jVar = 0; jVar < nVar; jVar ++ ) {
					Precon_Mat_inv[iVar][jVar] = Delta*Precon_Mat_inv[iVar][jVar];
				}
			}
			Jacobian.AddBlock(iPoint, iPoint, Precon_Mat_inv);
		}

		else 		Jacobian.AddVal2Diag(iPoint, Delta);

		/*--- Right hand side of the system (-Residual) and initial guess (x = 0) ---*/
		for (iVar = 0; iVar < nVar; iVar++) {
			total_index = iPoint*nVar+iVar;
			Res = local_ResConv[iVar]+local_ResVisc[iVar]+local_ResSour[iVar];
			rhs[total_index] = -(Res+local_Res_TruncError[iVar]);
			xsol[total_index] = 0.0;
			AddRes_Max( iVar, Res*Res*Vol );
		}
	}

	/*--- Solve the linear system (Stationary iterative methods) ---*/
	if (config->GetKind_Linear_Solver() == SYM_GAUSS_SEIDEL)
		Jacobian.SGSSolution(rhs, xsol, config->GetLinear_Solver_Error(),
				config->GetLinear_Solver_Iter(), false, geometry, config);

	if (config->GetKind_Linear_Solver() == LU_SGS)
		Jacobian.LU_SGSIteration(rhs, xsol, geometry, config);

	/*--- Solve the linear system (Krylov subspace methods) ---*/
	if ((config->GetKind_Linear_Solver() == BCGSTAB) ||
			(config->GetKind_Linear_Solver() == GMRES)) {

		CSysVector rhs_vec(geometry->GetnPoint(), geometry->GetnPointDomain(), nVar, rhs);
		CSysVector sol_vec(geometry->GetnPoint(), geometry->GetnPointDomain(), nVar, xsol);

		CMatrixVectorProduct* mat_vec = new CSparseMatrixVectorProduct(Jacobian);
		CSolutionSendReceive* sol_mpi = new CSparseMatrixSolMPI(Jacobian, geometry, config);

		CPreconditioner* precond = NULL;
		if (config->GetKind_Linear_Solver_Prec() == JACOBI) {
			Jacobian.BuildJacobiPreconditioner();
			precond = new CJacobiPreconditioner(Jacobian);
		}
		else if (config->GetKind_Linear_Solver_Prec() == LINELET) {
			Jacobian.BuildJacobiPreconditioner();
			precond = new CLineletPreconditioner(Jacobian);
		}
		else if (config->GetKind_Linear_Solver_Prec() == NO_PREC)
			precond = new CIdentityPreconditioner();

		CSysSolve system;
		if (config->GetKind_Linear_Solver() == BCGSTAB)
			IterLinSol = system.BCGSTAB(rhs_vec, sol_vec, *mat_vec, *precond, *sol_mpi, config->GetLinear_Solver_Error(),
					config->GetLinear_Solver_Iter(), false);
		else if (config->GetKind_Linear_Solver() == GMRES)
			IterLinSol = system.FlexibleGMRES(rhs_vec, sol_vec, *mat_vec, *precond, *sol_mpi, config->GetLinear_Solver_Error(),
					config->GetLinear_Solver_Iter(), false);

		/*--- The the number of iterations of the linear solver ---*/
		SetIterLinSolver(IterLinSol);

		/*--- Copy the solution to the array ---*/
		sol_vec.CopyToArray(xsol);

		/*--- dealocate memory ---*/
		delete mat_vec;
		delete precond;
		delete sol_mpi;
	}

	/*--- Update solution (system written in terms of increments) ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
		for (iVar = 0; iVar < nVar; iVar++)
			node[iPoint]->AddSolution(iVar,xsol[iPoint*nVar+iVar]);

#ifdef NO_MPI
	/*--- Compute the norm-2 of the residual ---*/
	for (iVar = 0; iVar < nVar; iVar++)
		SetRes_Max(iVar, sqrt(GetRes_Max(iVar)));
#endif

}

void CEulerSolution::SetPrimVar_Gradient_GG(CGeometry *geometry, CConfig *config) {
	unsigned long iPoint, jPoint, iEdge, iVertex;
	unsigned short iDim, iVar, iMarker;
	double *PrimVar_Vertex, *PrimVar_i, *PrimVar_j, PrimVar_Average,
	Partial_Gradient, Partial_Res, *Normal;

	/*--- Primitive variables definition compressible (temperature, vx, vy, vz, P, rho) ---*/
	/*--- Primitive variables definition incompressible (rho, vx, vy, vz) ---*/
	PrimVar_Vertex = new double [nDim+3];
	PrimVar_i = new double [nDim+3];
	PrimVar_j = new double [nDim+3];

	/*--- Set Gradient_Primitive to Zero (nDim+3) ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
		node[iPoint]->SetGradient_PrimitiveZero();

	/*--- Loop interior edges ---*/
	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {
		iPoint = geometry->edge[iEdge]->GetNode(0);
		jPoint = geometry->edge[iEdge]->GetNode(1);

		for (iVar = 0; iVar < nDim+3; iVar++) {
			PrimVar_i[iVar] = node[iPoint]->GetPrimVar(iVar);
			PrimVar_j[iVar] = node[jPoint]->GetPrimVar(iVar);
		}

		Normal = geometry->edge[iEdge]->GetNormal();
		for (iVar = 0; iVar < nDim+3; iVar++) {
			PrimVar_Average =  0.5 * ( PrimVar_i[iVar] + PrimVar_j[iVar] );
			for (iDim = 0; iDim < nDim; iDim++) {
				Partial_Res = PrimVar_Average*Normal[iDim];
				if (geometry->node[iPoint]->GetDomain())
					node[iPoint]->AddGradient_Primitive(iVar, iDim, Partial_Res);
				if (geometry->node[jPoint]->GetDomain())
					node[jPoint]->SubtractGradient_Primitive(iVar, iDim, Partial_Res);
			}
		}
	}

	/*--- Loop boundary edges ---*/
	for (iMarker = 0; iMarker < geometry->GetnMarker(); iMarker++) {
		for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {
			iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
			if (geometry->node[iPoint]->GetDomain()) {

				for (iVar = 0; iVar < nDim+3; iVar++)
					PrimVar_Vertex[iVar] = node[iPoint]->GetPrimVar(iVar);

				Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
				for (iVar = 0; iVar < nDim+3; iVar++)
					for (iDim = 0; iDim < nDim; iDim++) {
						Partial_Res = PrimVar_Vertex[iVar]*Normal[iDim];
						node[iPoint]->SubtractGradient_Primitive(iVar, iDim, Partial_Res);
					}
			}
		}
	}

	/*--- Update gradient value ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		for (iVar = 0; iVar < nDim+3; iVar++) {
			for (iDim = 0; iDim < nDim; iDim++) {
				Partial_Gradient = node[iPoint]->GetGradient_Primitive(iVar,iDim) / (geometry->node[iPoint]->GetVolume()+EPS);
				node[iPoint]->SetGradient_Primitive(iVar, iDim, Partial_Gradient);
			}
		}
	}

	delete [] PrimVar_Vertex;
	delete [] PrimVar_i;
	delete [] PrimVar_j;

}

void CEulerSolution::SetPrimVar_Gradient_LS(CGeometry *geometry, CConfig *config) {
	unsigned short iVar, iDim, jDim, iNeigh;
	unsigned long iPoint, jPoint;
	double *PrimVar_i, *PrimVar_j, *Coord_i, *Coord_j, r11, r12, r13, r22, r23, r23_a,
	r23_b, r33, weight, product;

	/*--- Primitive variables definition (temperature, vx, vy, vz) ---*/
	PrimVar_i = new double [nDim+3];
	PrimVar_j = new double [nDim+3]; 

	/*--- Loop over points of the grid ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Coord_i = geometry->node[iPoint]->GetCoord();

		for (iVar = 0; iVar < nDim+3; iVar++)
			PrimVar_i[iVar] = node[iPoint]->GetPrimVar(iVar);

		/*--- Inizialization of variables ---*/
		for (iVar = 0; iVar < nDim+3; iVar++)
			for (iDim = 0; iDim < nDim; iDim++)
				cvector[iVar][iDim] = 0.0;
		r11 = 0.0; r12 = 0.0; r13 = 0.0; r22 = 0.0; r23 = 0.0; r23_a = 0.0; r23_b = 0.0; r33 = 0.0;

		for (iNeigh = 0; iNeigh < geometry->node[iPoint]->GetnPoint(); iNeigh++) {
			jPoint = geometry->node[iPoint]->GetPoint(iNeigh);
			Coord_j = geometry->node[jPoint]->GetCoord();

			for (iVar = 0; iVar < nDim+3; iVar++)
				PrimVar_j[iVar] = node[jPoint]->GetPrimVar(iVar);

			weight = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				weight += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);

			/*--- Sumations for entries of upper triangular matrix R ---*/
			r11 += (Coord_j[0]-Coord_i[0])*(Coord_j[0]-Coord_i[0])/(weight);
			r12 += (Coord_j[0]-Coord_i[0])*(Coord_j[1]-Coord_i[1])/(weight);
			r22 += (Coord_j[1]-Coord_i[1])*(Coord_j[1]-Coord_i[1])/(weight);
			if (nDim == 3) {
				r13 += (Coord_j[0]-Coord_i[0])*(Coord_j[2]-Coord_i[2])/(weight);
				r23_a += (Coord_j[1]-Coord_i[1])*(Coord_j[2]-Coord_i[2])/(weight);
				r23_b += (Coord_j[0]-Coord_i[0])*(Coord_j[2]-Coord_i[2])/(weight);
				r33 += (Coord_j[2]-Coord_i[2])*(Coord_j[2]-Coord_i[2])/(weight);
			}

			/*--- Entries of c:= transpose(A)*b ---*/
			for (iVar = 0; iVar < nDim+3; iVar++)
				for (iDim = 0; iDim < nDim; iDim++)
					cvector[iVar][iDim] += (Coord_j[iDim]-Coord_i[iDim])*(PrimVar_j[iVar]-PrimVar_i[iVar])/(weight);
		}

		/*--- Entries of upper triangular matrix R ---*/
		r11 = sqrt(r11);
		r12 = r12/(r11);
		r22 = sqrt(r22-r12*r12);
		if (nDim == 3) {
			r13 = r13/(r11);
			r23 = r23_a/(r22) - r23_b*r12/(r11*r22);
			r33 = sqrt(r33-r23*r23-r13*r13);
		}
		/*--- S matrix := inv(R)*traspose(inv(R)) ---*/
		if (nDim == 2) {
			double detR2 = (r11*r22)*(r11*r22);
			Smatrix[0][0] = (r12*r12+r22*r22)/(detR2);
			Smatrix[0][1] = -r11*r12/(detR2);
			Smatrix[1][0] = Smatrix[0][1];
			Smatrix[1][1] = r11*r11/(detR2);
		}
		else {
			double detR2 = (r11*r22*r33)*(r11*r22*r33);
			double z11, z12, z13, z22, z23, z33;
			z11 = r22*r33;
			z12 = -r12*r33;
			z13 = r12*r23-r13*r22;
			z22 = r11*r33;
			z23 = -r11*r23;
			z33 = r11*r22;
			Smatrix[0][0] = (z11*z11+z12*z12+z13*z13)/(detR2);
			Smatrix[0][1] = (z12*z22+z13*z23)/(detR2);
			Smatrix[0][2] = (z13*z33)/(detR2);
			Smatrix[1][0] = Smatrix[0][1];
			Smatrix[1][1] = (z22*z22+z23*z23)/(detR2);
			Smatrix[1][2] = (z23*z33)/(detR2);
			Smatrix[2][0] = Smatrix[0][2];
			Smatrix[2][1] = Smatrix[1][2];
			Smatrix[2][2] = (z33*z33)/(detR2);
		}
		/*--- Computation of the gradient: S*c ---*/
		for (iVar = 0; iVar < nDim+3; iVar++) {
			for (iDim = 0; iDim < nDim; iDim++) {
				product = 0.0;
				for (jDim = 0; jDim < nDim; jDim++)
					product += Smatrix[iDim][jDim]*cvector[iVar][jDim];
				node[iPoint]->SetGradient_Primitive(iVar, iDim, product);
			}
		}
	}

	delete [] PrimVar_i;
	delete [] PrimVar_j;
}

void CEulerSolution::SetPrimVar_Limiter(CGeometry *geometry, CConfig *config) { }

void CEulerSolution::SetPreconditioner_Turkel(CConfig *config, unsigned short iPoint) {
	unsigned short iDim, jDim, iVar, jVar;
	double Beta, local_Mach, Beta2;
	double *U_i;
	double Beta_min = config->GetminTurkelBeta();
	double Beta_max = config->GetmaxTurkelBeta();

	/*--- Variables to calculate the preconditioner parameter Beta ---*/

	local_Mach = sqrt(node[iPoint]->GetVelocity2())/node[iPoint]->GetSoundSpeed();
	Beta 		    = max(Beta_min,min(local_Mach,Beta_max));
	Beta2 		    = Beta*Beta;

	U_i = node[iPoint]->GetSolution();

	double rho, enthalpy, soundspeed, sq_vel;
	rho = U_i[0];
	enthalpy = node[iPoint]->GetEnthalpy();
	soundspeed = node[iPoint]->GetSoundSpeed();
	sq_vel = node[iPoint]->GetVelocity2();

	/*---Calculating the inverse of the preconditioning matrix that multiplies the time derivative  */
	Precon_Mat_inv[0][0] = 0.5*sq_vel;
	Precon_Mat_inv[0][nVar-1] = 1.0;
	for (iDim = 0; iDim < nDim; iDim ++)
		Precon_Mat_inv[0][1+iDim] = -1.0*U_i[iDim+1]/rho;


	for (iDim = 0; iDim < nDim; iDim ++) {
		Precon_Mat_inv[iDim+1][0] = 0.5*sq_vel*U_i[iDim+1]/rho;
		Precon_Mat_inv[iDim+1][nVar-1] = U_i[iDim+1]/rho;
		for (jDim = 0; jDim < nDim; jDim ++) {
			Precon_Mat_inv[iDim+1][1+jDim] = -1.0*U_i[jDim+1]/rho*U_i[iDim+1]/rho;
		}
	}

	Precon_Mat_inv[nVar-1][0] = 0.5*sq_vel*enthalpy;
	Precon_Mat_inv[nVar-1][nVar-1] = enthalpy;
	for (iDim = 0; iDim < nDim; iDim ++)
		Precon_Mat_inv[nVar-1][1+iDim] = -1.0*U_i[iDim+1]/rho*enthalpy;


	for (iVar = 0; iVar < nVar; iVar ++ ) {
		for (jVar = 0; jVar < nVar; jVar ++ ) {
			Precon_Mat_inv[iVar][jVar] = (1.0/(Beta2+EPS) - 1.0) * (Gamma-1.0)/(soundspeed*soundspeed)*Precon_Mat_inv[iVar][jVar];
			if (iVar == jVar)
				Precon_Mat_inv[iVar][iVar] += 1.0;
		}
	}

	delete [] U_i;
}


void CEulerSolution::BC_Euler_Wall(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables ---*/
	unsigned short iDim, iVar, jVar;

	unsigned long iPoint, iVertex;

	double Pressure, *Normal = NULL, *GridVel = NULL;
	double ProjGridVel = 0.0, ProjRotVel = 0.0, Density;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool rotating_frame = config->GetRotating_Frame();
	bool grid_movement  = config->GetGrid_Movement();
	bool incompressible = config->GetIncompressible();

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = -Normal[iDim]/Area;

			/*--- Set the residual using the pressure ---*/
			if (incompressible) Pressure = node[iPoint]->GetSolution(0);
			else Pressure = node[iPoint]->GetPressure(incompressible);
			Residual[0] = 0;
			for (iDim = 0; iDim < nDim; iDim++)
				Residual[iDim+1] = Pressure*UnitaryNormal[iDim]*Area;
			if (!incompressible) Residual[nVar-1] = 0;

			/*--- Adjustment to energy equation for a rotating frame ---*/
			if (rotating_frame) {
				ProjRotVel = -geometry->vertex[val_marker][iVertex]->GetRotFlux();
				Residual[nVar-1] = Pressure*ProjRotVel;
			}

			/*--- Adjustment to energy equation due to grid motion ---*/
			if (grid_movement) {
				ProjGridVel = 0.0;
				GridVel = geometry->node[iPoint]->GetGridVel();
				for (iDim = 0; iDim < nDim; iDim++)
					ProjGridVel += GridVel[iDim]*UnitaryNormal[iDim]*Area;
				Residual[nVar-1] = Pressure*ProjGridVel;
			}

			/*--- Add value to the residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Form Jacobians for implicit computations ---*/
			if (implicit) {
				if (incompressible)  {
					Density = node[iPoint]->GetDensityInc();
					for (iVar = 0; iVar < nVar; iVar++) {
						for (jVar = 0; jVar < nVar; jVar++)
							Jacobian_i[iVar][jVar] = 0.0;
					}
					for (iDim = 0; iDim < nDim; iDim++)
						Jacobian_i[iDim+1][0] = -Normal[iDim];
					Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
				}
				else {
					double a2 = Gamma-1.0;
					double phi = 0.5*a2*node[iPoint]->GetVelocity2();

					for (iVar = 0; iVar < nVar; iVar++) {
						Jacobian_i[0][iVar] = 0.0;
						Jacobian_i[nDim+1][iVar] = 0.0;
					}
					for (iDim = 0; iDim < nDim; iDim++) {
						Jacobian_i[iDim+1][0] = -phi*Normal[iDim];
						for (unsigned short jDim = 0; jDim < nDim; jDim++)
							Jacobian_i[iDim+1][jDim+1] = a2*node[iPoint]->GetVelocity(jDim, incompressible)*Normal[iDim];
						Jacobian_i[iDim+1][nDim+1] = -a2*Normal[iDim];
					}
					if (grid_movement) {
						ProjGridVel = 0.0;
						GridVel = geometry->node[iPoint]->GetGridVel();
						for (iDim = 0; iDim < nDim; iDim++)
							ProjGridVel += GridVel[iDim]*UnitaryNormal[iDim]*Area;
						for (unsigned short jDim = 0; jDim < nDim; jDim++)
							Jacobian_i[nDim+1][jDim+1] = a2*node[iPoint]->GetVelocity(jDim, incompressible)*ProjGridVel;
						Jacobian_i[nDim+1][nDim+1] = -a2*ProjGridVel;
					}
					if (rotating_frame) {
						double Rot_Flux = -geometry->vertex[val_marker][iVertex]->GetRotFlux();
						for (unsigned short jDim = 0; jDim < nDim; jDim++)
							Jacobian_i[nDim+1][jDim+1] = a2*node[iPoint]->GetVelocity(jDim, incompressible)*Rot_Flux;
						Jacobian_i[nDim+1][nDim+1] = -a2*Rot_Flux;
					}
					Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
				}
			}
		}
	}

}

void CEulerSolution::BC_Far_Field(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {
	unsigned short iVar, iDim;
	unsigned long iVertex, iPoint;

	double *U_domain = new double[nVar]; double *U_infty = new double[nVar];

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool rotating_frame = config->GetRotating_Frame();
	bool grid_movement  = config->GetGrid_Movement();
	bool incompressible = config->GetIncompressible();
	bool gravity        = config->GetGravityForce();

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Retrieve solution at the farfield boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			/*--- Construct solution state at infinity (farfield) ---*/
			if (incompressible) {
				double yFreeSurface = config->GetFreeSurface_Zero();
				double PressFreeSurface = GetPressure_Inf();
				double Density = node[iPoint]->GetDensityInc();
				double Froude = config->GetFroude();
				double yCoord = geometry->node[iPoint]->GetCoord(1);
				double Pressure;
				if (gravity) Pressure = PressFreeSurface + Density*(yFreeSurface-yCoord)/(Froude*Froude);
				else Pressure = PressFreeSurface;

				U_infty[0] = Pressure;
				U_infty[1] = GetVelocity_Inf(0)*Density;
				U_infty[2] = GetVelocity_Inf(1)*Density;
				if (nDim == 3) U_infty[3] = GetVelocity_Inf(2)*Density;

			} else {
				U_infty[0] = GetDensity_Inf();
				U_infty[1] = GetDensity_Velocity_Inf(0);
				U_infty[2] = GetDensity_Velocity_Inf(1);
				U_infty[3] = GetDensity_Energy_Inf();
				if (nDim == 3) {
					U_infty[3] = GetDensity_Velocity_Inf(2);
					U_infty[4] = GetDensity_Energy_Inf();
				}
			}

			/*--- Set various quantities in the solver class ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
			for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = -Vector[iDim];
			solver->SetNormal(Vector);
			solver->SetConservative(U_domain, U_infty);
			if (incompressible) {
				solver->SetDensityInc(node[iPoint]->GetDensityInc(),
						node[iPoint]->GetDensityInc());
				solver->SetBetaInc2(node[iPoint]->GetBetaInc2(),
						node[iPoint]->GetBetaInc2());
				double *iCoord = geometry->node[iPoint]->GetCoord();
				solver->SetCoord(iCoord, iCoord);
			}
			if (rotating_frame) {
				solver->SetRotVel(geometry->node[iPoint]->GetRotVel(),
						geometry->node[iPoint]->GetRotVel());
				solver->SetRotFlux(-geometry->vertex[val_marker][iVertex]->GetRotFlux());
			}
			if (grid_movement) {
				solver->SetGridVel(geometry->node[iPoint]->GetGridVel(),
						geometry->node[iPoint]->GetGridVel());
			}

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

			if ((config->GetKind_Upwind() == ROE_TURKEL_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_1ST)) {
				node[iPoint]->SetPreconditioner_Beta(solver->GetPrecond_Beta());
			}

		}
	}

	delete [] U_domain;
	delete [] U_infty;

}

void CEulerSolution::BC_Inlet(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables and initialization. ---*/
	unsigned short iVar, iDim, Kind_Inlet = config->GetKind_Inlet();

	unsigned long iVertex, iPoint, Point_Normal;

	double P_Total, T_Total, Velocity[3];
	double Velocity2, H_Total, Temperature, Riemann;
	double Pressure, Density, Energy, *Flow_Dir, Mach2;
	double SoundSpeed2, SoundSpeed_Total2, Vel_Mag;
	double alpha, aa, bb, cc, dd;
	double Two_Gamma_M1 = 2.0/Gamma_Minus_One;
	double Gas_Constant = config->GetGas_Constant()/config->GetGas_Constant_Ref();
	double *U_domain = new double[nVar]; double *U_inlet = new double[nVar];
	double *Normal = new double[nDim];

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool rotating_frame = config->GetRotating_Frame();
	bool grid_movement  = config->GetGrid_Movement();
	bool incompressible = config->GetIncompressible();

	string Marker_Tag = config->GetMarker_All_Tag(val_marker);

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e., not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
			for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = Normal[iDim]/Area;

			/*--- Current solution at this boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			/*--- Build the fictitious intlet state based on characteristics ---*/
			if (incompressible) {

				/*--- Index of the closest interior node ---*/
				Point_Normal = iPoint; //geometry->vertex[val_marker][iVertex]->GetClosest_Neighbor();

				/*--- Pressure computation using the internal value ---*/
				U_inlet[0] = node[Point_Normal]->GetSolution(0);

				/*--- The velocity is computed from the interior, and normal
         derivative for the density ---*/
				for (iDim = 0; iDim < nDim; iDim++)
					U_inlet[iDim+1] = GetVelocity_Inf(iDim)*node[Point_Normal]->GetDensityInc();

				/*--- Note that the y velocity is recomputed due to the
         free surface effect on the pressure ---*/
				U_inlet[nDim] = node[Point_Normal]->GetSolution(nDim);

			} else {

				/*--- Subsonic inflow: there is one outgoing characteristic (u-c),
         therefore we can specify all but one state variable at the inlet.
         The outgoing Riemann invariant provides the final piece of info.
         Adapted from an original implementation in the Stanford University
         multi-block (SUmb) solver in the routine bcSubsonicInflow.f90
         written by Edwin van der Weide, last modified 04-20-2009. ---*/

				switch (Kind_Inlet) {

				/*--- Total properties have been specified at the inlet. ---*/
				case TOTAL_CONDITIONS:

					/*--- Retrieve the specified total conditions for this inlet. ---*/
					P_Total  = config->GetInlet_Ptotal(Marker_Tag);
					T_Total  = config->GetInlet_Ttotal(Marker_Tag);
					Flow_Dir = config->GetInlet_FlowDir(Marker_Tag);

					/*--- Non-dim. the inputs if necessary. ---*/
					P_Total /= config->GetPressure_Ref();
					T_Total /= config->GetTemperature_Ref();

					/*--- Store primitives and set some variables for clarity. ---*/
					Density = U_domain[0];
					Velocity2 = 0.0;
					for (iDim = 0; iDim < nDim; iDim++) {
						Velocity[iDim] = U_domain[iDim+1]/Density;
						Velocity2 += Velocity[iDim]*Velocity[iDim];
					}
					Energy      = U_domain[nVar-1]/Density;
					Pressure    = Gamma_Minus_One*Density*(Energy-0.5*Velocity2);
					H_Total     = (Gamma*Gas_Constant/Gamma_Minus_One)*T_Total;
					SoundSpeed2 = Gamma*Pressure/Density;

					/*--- Compute the acoustic Riemann invariant that is extrapolated
             from the domain interior. ---*/
					Riemann   = 2.0*sqrt(SoundSpeed2)/Gamma_Minus_One;
					for (iDim = 0; iDim < nDim; iDim++)
						Riemann += Velocity[iDim]*UnitaryNormal[iDim];

					/*--- Total speed of sound ---*/
					SoundSpeed_Total2 = Gamma_Minus_One*(H_Total - (Energy
							+ Pressure/Density)+0.5*Velocity2) + SoundSpeed2;

					/*--- Dot product of normal and flow direction. This should
             be negative due to outward facing boundary normal convention. ---*/
					alpha = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						alpha += UnitaryNormal[iDim]*Flow_Dir[iDim];

					/*--- Coefficients in the quadratic equation for the velocity ---*/
					aa =  1.0 + 0.5*Gamma_Minus_One*alpha*alpha;
					bb = -1.0*Gamma_Minus_One*alpha*Riemann;
					cc =  0.5*Gamma_Minus_One*Riemann*Riemann
							-2.0*SoundSpeed_Total2/Gamma_Minus_One;

					/*--- Solve quadratic equation for velocity magnitude. Value must
             be positive, so the choice of root is clear. ---*/
					dd = bb*bb - 4.0*aa*cc;
					dd = sqrt(max(0.0,dd));
					Vel_Mag   = (-bb + dd)/(2.0*aa);
					Vel_Mag   = max(0.0,Vel_Mag);
					Velocity2 = Vel_Mag*Vel_Mag;

					/*--- Compute speed of sound from total speed of sound eqn. ---*/
					SoundSpeed2 = SoundSpeed_Total2 - 0.5*Gamma_Minus_One*Velocity2;

					/*--- Mach squared (cut between 0-1), use to adapt velocity ---*/
					Mach2 = Velocity2/SoundSpeed2;
					Mach2 = min(1.0,Mach2);
					Velocity2   = Mach2*SoundSpeed2;
					Vel_Mag     = sqrt(Velocity2);
					SoundSpeed2 = SoundSpeed_Total2 - 0.5*Gamma_Minus_One*Velocity2;

					/*--- Compute new velocity vector at the inlet ---*/
					for (iDim = 0; iDim < nDim; iDim++)
						Velocity[iDim] = Vel_Mag*Flow_Dir[iDim];

					/*--- Static temperature from the speed of sound relation ---*/
					Temperature = SoundSpeed2/(Gamma*Gas_Constant);

					/*--- Static pressure using isentropic relation at a point ---*/
					Pressure = P_Total*pow((Temperature/T_Total),Gamma/Gamma_Minus_One);

					/*--- Density at the inlet from the gas law ---*/
					Density = Pressure/(Gas_Constant*Temperature);

					/*--- Using pressure, density, & velocity, compute the energy ---*/
					Energy = Pressure/(Density*Gamma_Minus_One)+0.5*Velocity2;

					/*--- Conservative variables, using the derived quantities ---*/
					U_inlet[0] = Density;
					U_inlet[1] = Velocity[0]*Density;
					U_inlet[2] = Velocity[1]*Density;
					U_inlet[3] = Energy*Density;
					if (nDim == 3) {
						U_inlet[3] = Velocity[2]*Density;
						U_inlet[4] = Energy*Density;
					}

					break;

					/*--- Mass flow has been specified at the inlet. ---*/
				case MASS_FLOW:

					/*--- Retrieve the specified mass flow for the inlet. ---*/
					Density  = config->GetInlet_Ttotal(Marker_Tag);
					Vel_Mag  = config->GetInlet_Ptotal(Marker_Tag);
					Flow_Dir = config->GetInlet_FlowDir(Marker_Tag);

					/*--- Non-dim. the inputs if necessary. ---*/
					Density /= config->GetDensity_Ref();
					Vel_Mag /= config->GetVelocity_Ref();

					/*--- Get primitives from current inlet state. ---*/
					for (iDim = 0; iDim < nDim; iDim++)
						Velocity[iDim] = node[iPoint]->GetVelocity(iDim, incompressible);
					Pressure    = node[iPoint]->GetPressure(incompressible);
					SoundSpeed2 = Gamma*Pressure/U_domain[0];

					/*--- Compute the acoustic Riemann invariant that is extrapolated
             from the domain interior. ---*/
					Riemann = Two_Gamma_M1*sqrt(SoundSpeed2);
					for (iDim = 0; iDim < nDim; iDim++)
						Riemann += Velocity[iDim]*UnitaryNormal[iDim];

					/*--- Speed of sound squared for fictitious inlet state ---*/
					SoundSpeed2 = Riemann;
					for (iDim = 0; iDim < nDim; iDim++)
						SoundSpeed2 -= Vel_Mag*Flow_Dir[iDim]*UnitaryNormal[iDim];

					SoundSpeed2 = max(0.0,0.5*Gamma_Minus_One*SoundSpeed2);
					SoundSpeed2 = SoundSpeed2*SoundSpeed2;

					/*--- Pressure for the fictitious inlet state ---*/
					Pressure = SoundSpeed2*Density/Gamma;

					/*--- Energy for the fictitious inlet state ---*/
					Energy = Pressure/(Density*Gamma_Minus_One)+0.5*Vel_Mag*Vel_Mag;

					/*--- Conservative variables, using the derived quantities ---*/
					U_inlet[0] = Density;
					U_inlet[1] = Vel_Mag*Flow_Dir[0]*Density;
					U_inlet[2] = Vel_Mag*Flow_Dir[1]*Density;
					U_inlet[3] = Energy*Density;
					if (nDim == 3) {
						U_inlet[3] = Vel_Mag*Flow_Dir[2]*Density;
						U_inlet[4] = Energy*Density;
					}

					break;
				}
			}

			/*--- Set various quantities in the solver class ---*/
			solver->SetNormal(Normal);
			solver->SetConservative(U_domain, U_inlet);
			if (incompressible) {
				solver->SetDensityInc(node[iPoint]->GetDensityInc(),
						node[iPoint]->GetDensityInc());
				solver->SetBetaInc2(node[iPoint]->GetBetaInc2(),
						node[iPoint]->GetBetaInc2());
				solver->SetCoord(geometry->node[iPoint]->GetCoord(),
						geometry->node[iPoint]->GetCoord());
			}
			if (rotating_frame) {
				solver->SetRotVel(geometry->node[iPoint]->GetRotVel(),
						geometry->node[iPoint]->GetRotVel());
				solver->SetRotFlux(-geometry->vertex[val_marker][iVertex]->GetRotFlux());
			}
			if (grid_movement)
				solver->SetGridVel(geometry->node[iPoint]->GetGridVel(),
						geometry->node[iPoint]->GetGridVel());

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	/*--- Free locally allocated memory ---*/
	delete [] U_domain;
	delete [] U_inlet;
	delete [] Normal;

}

void CEulerSolution::BC_Supersonic_Inlet(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables and initialization. ---*/
	unsigned short iDim, iVar;

	unsigned long iVertex, iPoint;

	double Density, Pressure, Temperature, Energy, *Velocity, Velocity2;
	double Gas_Constant = config->GetGas_Constant()/config->GetGas_Constant_Ref();
	double *U_inlet = new double[nVar]; double *U_domain = new double[nVar];
	double *Normal = new double[nDim];

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool rotating_frame = config->GetRotating_Frame();
	bool grid_movement  = config->GetGrid_Movement();
	bool incompressible = config->GetIncompressible();

	string Marker_Tag = config->GetMarker_All_Tag(val_marker);

	/*--- Supersonic inlet flow: there are no outgoing characteristics,
   so all flow variables can be imposed at the inlet.
   First, retrieve the specified values for the primitive variables. ---*/
	Temperature = config->GetInlet_Temperature(Marker_Tag);
	Pressure    = config->GetInlet_Pressure(Marker_Tag);
	Velocity    = config->GetInlet_Velocity(Marker_Tag);

	/*--- Density at the inlet from the gas law ---*/
	Density = Pressure/(Gas_Constant*Temperature);

	/*--- Non-dim. the inputs if necessary. ---*/
	Temperature = Temperature/config->GetTemperature_Ref();
	Pressure    = Pressure/config->GetPressure_Ref();
	Density     = Density/config->GetDensity_Ref();
	for (iDim = 0; iDim < nDim; iDim++)
		Velocity[iDim] = Velocity[iDim]/config->GetVelocity_Ref();

	/*--- Compute the energy from the specified state ---*/
	Velocity2 = 0.0;
	for (iDim = 0; iDim < nDim; iDim++)
		Velocity2 += Velocity[iDim]*Velocity[iDim];
	Energy = Pressure/(Density*Gamma_Minus_One)+0.5*Velocity2;

	/*--- Conservative variables, using the derived quantities ---*/
	U_inlet[0] = Density;
	U_inlet[1] = Velocity[0]*Density;
	U_inlet[2] = Velocity[1]*Density;
	U_inlet[3] = Energy*Density;
	if (nDim == 3) {
		U_inlet[3] = Velocity[2]*Density;
		U_inlet[4] = Energy*Density;
	}

	/*--- Loop over all the vertices on this boundary marker ---*/
	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Current solution at this boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
			for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = Normal[iDim]/Area;

			/*--- Set various quantities in the solver class ---*/
			solver->SetNormal(Normal);
			solver->SetConservative(U_domain, U_inlet);
			if (incompressible) {
				solver->SetDensityInc(node[iPoint]->GetDensityInc(),
						node[iPoint]->GetDensityInc());
				solver->SetBetaInc2(node[iPoint]->GetBetaInc2(),
						node[iPoint]->GetBetaInc2());
				solver->SetCoord(geometry->node[iPoint]->GetCoord(),
						geometry->node[iPoint]->GetCoord());
			}
			if (rotating_frame) {
				solver->SetRotVel(geometry->node[iPoint]->GetRotVel(),
						geometry->node[iPoint]->GetRotVel());
				solver->SetRotFlux(-geometry->vertex[val_marker][iVertex]->GetRotFlux());
			}
			if (grid_movement)
				solver->SetGridVel(geometry->node[iPoint]->GetGridVel(),
						geometry->node[iPoint]->GetGridVel());

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	/*--- Free locally allocated memory ---*/
	delete [] U_domain;
	delete [] U_inlet;
	delete [] Normal;

}

void CEulerSolution::BC_Outlet(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables and initialization. ---*/
	unsigned short iVar, iDim;
	unsigned long iVertex, iPoint, Point_Normal;
	double Heaviside, LevelSet, epsilon, Density_Exit;
	double Pressure, P_Exit, Velocity[3], Velocity2, Entropy;
	double Density, Energy, yFreeSurface, PressFreeSurface, Riemann;
	double Froude, yCoord, Vn, SoundSpeed, Mach_Exit, Vn_Exit;
	double *U_domain = new double[nVar]; double *U_outlet = new double[nVar];
	double *Normal = new double[nDim];

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool incompressible = config->GetIncompressible();
	bool rotating_frame = config->GetRotating_Frame();
	bool grid_movement  = config->GetGrid_Movement();
	bool gravity        = config->GetGravityForce();

	string Marker_Tag = config->GetMarker_All_Tag(val_marker);

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e., not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
			for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = Normal[iDim]/Area;

			/*--- Current solution at this boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			if (incompressible) {

				/*--- Index of the closest interior node ---*/
				Point_Normal = iPoint; //geometry->vertex[val_marker][iVertex]->GetClosest_Neighbor();

				/*--- Density computation at the exit ---*/
				Heaviside = 0.0;
				yFreeSurface = config->GetFreeSurface_Zero();
				yCoord = geometry->node[iPoint]->GetCoord(nDim-1);
				LevelSet = yCoord - yFreeSurface;
				epsilon = config->GetFreeSurface_Thickness();
				if (LevelSet < -epsilon) Heaviside = 1.0;
				if (fabs(LevelSet) <= epsilon) Heaviside = 1.0 - (0.5*(1.0+(LevelSet/epsilon)+(1.0/PI_NUMBER)*sin(PI_NUMBER*LevelSet/epsilon)));
				if (LevelSet > epsilon) Heaviside = 0.0;
				Density_Exit = (config->GetRatioDensity() + (1.0 - config->GetRatioDensity())*Heaviside)*config->GetDensity_FreeStreamND();

				/*--- Pressure computation using density at the exit ---*/
				PressFreeSurface = GetPressure_Inf();
				Froude = config->GetFroude();
				if (gravity) U_outlet[0] = PressFreeSurface + Density_Exit*((yFreeSurface-yCoord)/(Froude*Froude));
				else U_outlet[0] = PressFreeSurface;

				/*--- Neumman condition in the interface ---*/
				if ((fabs(LevelSet) <= epsilon) && (gravity)) {
					U_outlet[0] = node[Point_Normal]->GetSolution(0);
					Density_Exit = node[iPoint]->GetDensityInc();
				}
				else {
					Density_Exit = node[iPoint]->GetDensityInc();
				}

				/*--- Velocity interpolation ---*/
				for (iDim = 0; iDim < nDim; iDim++)
					U_outlet[iDim+1] = node[Point_Normal]->GetSolution(iDim+1);

			}
			else {

				/*--- Retrieve the specified back pressure for this outlet. ---*/
				P_Exit = config->GetOutlet_Pressure(Marker_Tag);

				/*--- Non-dim. the inputs if necessary. ---*/
				P_Exit = P_Exit/config->GetPressure_Ref();

				/*--- Check whether the flow is supersonic at the exit. The type
         of boundary update depends on this. ---*/
				Density = U_domain[0];
				Velocity2 = 0.0; Vn = 0.0;
				for (iDim = 0; iDim < nDim; iDim++) {
					Velocity[iDim] = U_domain[iDim+1]/Density;
					Velocity2 += Velocity[iDim]*Velocity[iDim];
					Vn += Velocity[iDim]*UnitaryNormal[iDim];
				}
				Energy     = U_domain[nVar-1]/Density;
				Pressure   = Gamma_Minus_One*Density*(Energy-0.5*Velocity2);
				SoundSpeed = sqrt(Gamma*Pressure/Density);
				Mach_Exit  = sqrt(Velocity2)/SoundSpeed;

				if (Mach_Exit >= 1.0) {

					/*--- Supersonic exit flow: there are no incoming characteristics,
           so no boundary condition is necessary. Set outlet state to current
           state so that upwinding handles the direction of propagation. ---*/
					for (iVar = 0; iVar < nVar; iVar++)
						U_outlet[iVar] = U_domain[iVar];

				} else {

					/*--- Subsonic exit flow: there is one incoming characteristic,
           therefore one variable can be specified (back pressure) and is used
           to update the conservative variables. Compute the entropy and the
           acoustic Riemann variable. These invariants, as well as the
           tangential velocity components, are extrapolated. Adapted from an
           original implementation in the Stanford University multi-block
           (SUmb) solver in the routine bcSubsonicOutflow.f90 by Edwin van
           der Weide, last modified 09-10-2007. ---*/

					Entropy = Pressure*pow(1.0/Density,Gamma);
					Riemann = Vn + 2.0*SoundSpeed/Gamma_Minus_One;

					/*--- Compute the new fictious state at the outlet ---*/
					Density    = pow(P_Exit/Entropy,1.0/Gamma);
					Pressure   = P_Exit;
					SoundSpeed = sqrt(Gamma*P_Exit/Density);
					Vn_Exit    = Riemann - 2.0*SoundSpeed/Gamma_Minus_One;
					Velocity2  = 0.0;
					for (iDim = 0; iDim < nDim; iDim++) {
						Velocity[iDim] = Velocity[iDim] + (Vn_Exit-Vn)*UnitaryNormal[iDim];
						Velocity2 += Velocity[iDim]*Velocity[iDim];
					}
					Energy  = P_Exit/(Density*Gamma_Minus_One) + 0.5*Velocity2;

					/*--- Conservative variables, using the derived quantities ---*/
					U_outlet[0] = Density;
					U_outlet[1] = Velocity[0]*Density;
					U_outlet[2] = Velocity[1]*Density;
					U_outlet[3] = Energy*Density;
					if (nDim == 3) {
						U_outlet[3] = Velocity[2]*Density;
						U_outlet[4] = Energy*Density;
					}
				}
			}

			/*--- Set various quantities in the solver class ---*/
			solver->SetNormal(Normal);
			solver->SetConservative(U_domain, U_outlet);
			if (incompressible) {
				solver->SetDensityInc(node[iPoint]->GetDensityInc(), Density_Exit);
				solver->SetBetaInc2(node[iPoint]->GetBetaInc2(),
						node[iPoint]->GetBetaInc2());
				solver->SetCoord(geometry->node[iPoint]->GetCoord(),
						geometry->node[iPoint]->GetCoord());
			}
			if (rotating_frame) {
				solver->SetRotVel(geometry->node[iPoint]->GetRotVel(),
						geometry->node[iPoint]->GetRotVel());
				solver->SetRotFlux(-geometry->vertex[val_marker][iVertex]->GetRotFlux());
			}
			if (grid_movement)
				solver->SetGridVel(geometry->node[iPoint]->GetGridVel(),
						geometry->node[iPoint]->GetGridVel());

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	/*--- Free locally allocated memory ---*/
	delete [] U_domain;
	delete [] U_outlet;
	delete [] Normal;

}

void CEulerSolution::BC_NacelleInflow(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables and initialization. ---*/
	unsigned short iVar, iDim;
	unsigned long iVertex, iPoint;
	double Pressure, P_Fan, Velocity[3], Velocity2, Entropy, Target_FanFace_Mach = 0.0, LocalMassFlow_Rate;
	double Density, Energy, Riemann, Area, UnitaryNormal[3], TotalArea, Mach, Press_Mean;
	double Vn, SoundSpeed, Mach_Exit, Vn_Exit, P_Fan_inc, P_Fan_old;
	double *U_domain = new double[nVar]; double *U_outlet = new double[nVar];
	double *Normal = new double[nDim];
	double DampingFactor = 0.1;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

	string Marker_Tag = config->GetMarker_All_Tag(val_marker);

	/*--- Compute the numerical fan face Mach number,
	 and the total area of the inflow ---*/
	TotalArea = 0.0; MassFlow_Rate[val_marker] = 0.0;
	FanFace_Mach[val_marker] = 0.0; Press_Mean = 0.0;
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		if (geometry->node[iPoint]->GetDomain()) {
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);

			/*--- Compute the area and the velocity ---*/
			Density = node[iPoint]->GetSolution(0);
			Velocity2 = 0.0; Area = 0.0; LocalMassFlow_Rate = 0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				Area += Normal[iDim]*Normal[iDim];
				Velocity[iDim] = node[iPoint]->GetSolution(iDim+1)/Density;
				Velocity2 += Velocity[iDim]*Velocity[iDim];
				LocalMassFlow_Rate -= Normal[iDim]*node[iPoint]->GetSolution(1+iDim);
			}

			Area       = sqrt (Area);
			Energy     = node[iPoint]->GetSolution(nVar-1)/Density;
			Pressure   = Gamma_Minus_One*Density*(Energy-0.5*Velocity2);
			SoundSpeed = sqrt(Gamma*Pressure/Density);
			Mach       = sqrt(Velocity2)/SoundSpeed;

			MassFlow_Rate[val_marker] += LocalMassFlow_Rate;
			Press_Mean += Pressure*Area;

			/*--- Compute the FanFace_Mach and the total area ---*/
			FanFace_Mach[val_marker] += Mach*Area;
			TotalArea += Area;
		}
	}

	FanFace_Mach[val_marker] /= TotalArea;
	Press_Mean /= TotalArea;

	if (config->GetExtIter() == 0) FanFace_Pressure[val_marker] = Press_Mean;

	/*--- Retrieve the specified target fan face mach in the nacelle. ---*/
	Target_FanFace_Mach = config->GetFanFace_Mach(Marker_Tag);

	/*--- Retrieve the old fan face pressure in the nacelle. ---*/
	P_Fan_old = FanFace_Pressure[val_marker];

	/*--- Compute the Pressure increment ---*/
	P_Fan_inc = ((FanFace_Mach[val_marker]/Target_FanFace_Mach) - 1.0) * config->GetPressure_FreeStreamND();

	/*--- Estimate the new fan face pressure ---*/
	P_Fan = (1.0 - DampingFactor)*P_Fan_old + DampingFactor * (P_Fan_old + P_Fan_inc);

	/*--- Update pressure ---*/
	FanFace_Pressure[val_marker] = P_Fan;

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
			for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];

			Area = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = Normal[iDim]/Area;

			/*--- Current solution at this boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			/*--- Check whether the flow is supersonic at the exit. The type
			 of boundary update depends on this. ---*/
			Density = U_domain[0];
			Velocity2 = 0.0; Vn = 0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				Velocity[iDim] = U_domain[iDim+1]/Density;
				Velocity2 += Velocity[iDim]*Velocity[iDim];
				Vn += Velocity[iDim]*UnitaryNormal[iDim];
			}
			Energy     = U_domain[nVar-1]/Density;
			Pressure   = Gamma_Minus_One*Density*(Energy-0.5*Velocity2);
			SoundSpeed = sqrt(Gamma*Pressure/Density);
			Mach_Exit  = sqrt(Velocity2)/SoundSpeed;

			/*--- Subsonic exit flow: there is one incoming characteristic,
			 therefore one variable can be specified (back pressure) and is used
			 to update the conservative variables.

			 Compute the entropy and the acoustic variable. These
			 riemann invariants, as well as the tangential velocity components,
			 are extrapolated. ---*/
			Entropy = Pressure*pow(1.0/Density,Gamma);
			Riemann = Vn + 2.0*SoundSpeed/Gamma_Minus_One;

			/*--- Compute the new fictious state at the outlet ---*/
			Density    = pow(P_Fan/Entropy,1.0/Gamma);
			Pressure   = P_Fan;
			SoundSpeed = sqrt(Gamma*P_Fan/Density);
			Vn_Exit    = Riemann - 2.0*SoundSpeed/Gamma_Minus_One;
			Velocity2  = 0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				Velocity[iDim] = Velocity[iDim] + (Vn_Exit-Vn)*UnitaryNormal[iDim];
				Velocity2 += Velocity[iDim]*Velocity[iDim];
			}
			Energy  = P_Fan/(Density*Gamma_Minus_One) + 0.5*Velocity2;

			/*--- Conservative variables, using the derived quantities ---*/
			U_outlet[0] = Density;
			U_outlet[1] = Velocity[0]*Density;
			U_outlet[2] = Velocity[1]*Density;
			U_outlet[3] = Energy*Density;
			if (nDim == 3) {
				U_outlet[3] = Velocity[2]*Density;
				U_outlet[4] = Energy*Density;
			}

			/*--- Set various quantities in the solver class ---*/
			solver->SetNormal(Normal);
			solver->SetConservative(U_domain, U_outlet);

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	delete [] U_domain;
	delete [] U_outlet;
	delete [] Normal;

}

void CEulerSolution::BC_NacelleExhaust(CGeometry *geometry, CSolution **solution_container,
		CNumerics *solver, CConfig *config, unsigned short val_marker) {

	/*--- Local variables and initialization. ---*/
	unsigned short iVar, iDim;
	unsigned long iVertex, iPoint;
	double P_Total, T_Total, Velocity[3];
	double Velocity2, H_Total, Temperature, Riemann, LocalMassFlow_Rate;
	double Pressure, Density, Energy, Mach2;
	double SoundSpeed2, SoundSpeed_Total2, Vel_Mag;
	double alpha, aa, bb, cc, dd;
	double Gas_Constant = config->GetGas_Constant()/config->GetGas_Constant_Ref();
	double *U_domain = new double[nVar]; double *U_inlet = new double[nVar];
	double *Normal = new double[nDim];
	double *Flow_Dir = new double[nDim];

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

	string Marker_Tag = config->GetMarker_All_Tag(val_marker);

	/*--- Compute the mass flow rate ---*/
	MassFlow_Rate[val_marker] = 0.0;
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		if (geometry->node[iPoint]->GetDomain()) {
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);

			/*--- Compute the mass flow rate ---*/
			LocalMassFlow_Rate = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				LocalMassFlow_Rate += Normal[iDim]*node[iPoint]->GetSolution(1+iDim);

			MassFlow_Rate[val_marker] += LocalMassFlow_Rate;

		}
	}

	/*--- Loop over all the vertices on this boundary marker ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- Check if the node belongs to the domain (i.e, not a halo node) ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Normal vector for this vertex (negate for outward convention) ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Normal);
			for (iDim = 0; iDim < nDim; iDim++) Normal[iDim] = -Normal[iDim];

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = Normal[iDim]/Area;

			/*--- Current solution at this boundary node ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar] = node[iPoint]->GetSolution(iVar);

			/*--- Subsonic inflow: there is one outgoing characteristic (u-c),
			 therefore we can specify all but one state variable at the inlet.
			 The outgoing Riemann invariant provides the final piece of info. ---*/

			/*--- Retrieve the specified total conditions for this inlet. ---*/
			P_Total  = config->GetNozzle_Ptotal(Marker_Tag);
			T_Total  = config->GetNozzle_Ttotal(Marker_Tag);

			/*--- Non-dim. the inputs if necessary. ---*/
			P_Total /= config->GetPressure_Ref();
			T_Total /= config->GetTemperature_Ref();

			/*--- Store primitives and set some variables for clarity. ---*/
			Density = U_domain[0];
			Velocity2 = 0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				Velocity[iDim] = U_domain[iDim+1]/Density;
				Velocity2 += Velocity[iDim]*Velocity[iDim];
			}
			Energy      = U_domain[nVar-1]/Density;
			Pressure    = Gamma_Minus_One*Density*(Energy-0.5*Velocity2);
			H_Total     = (Gamma*Gas_Constant/Gamma_Minus_One)*T_Total;
			SoundSpeed2 = Gamma*Pressure/Density;

			/*--- Compute the acoustic Riemann invariant that is extrapolated
			 from the domain interior. ---*/
			Riemann   = 2.0*sqrt(SoundSpeed2)/Gamma_Minus_One;
			for (iDim = 0; iDim < nDim; iDim++)
				Riemann += Velocity[iDim]*UnitaryNormal[iDim];

			/*--- Total speed of sound ---*/
			SoundSpeed_Total2 = Gamma_Minus_One*(H_Total - (Energy
					+ Pressure/Density)+0.5*Velocity2) + SoundSpeed2;

			/*--- The flow direction is defined by the surface normal ---*/
			for (iDim = 0; iDim < nDim; iDim++)
				Flow_Dir[iDim] = -UnitaryNormal[iDim];

			/*--- Dot product of normal and flow direction. This should
			 be negative due to outward facing boundary normal convention. ---*/
			alpha = 0.0;
			for (iDim = 0; iDim < nDim; iDim++)
				alpha += UnitaryNormal[iDim]*Flow_Dir[iDim];

			/*--- Coefficients in the quadratic equation for the velocity ---*/
			aa =  1.0 + 0.5*Gamma_Minus_One*alpha*alpha;
			bb = -1.0*Gamma_Minus_One*alpha*Riemann;
			cc =  0.5*Gamma_Minus_One*Riemann*Riemann
					-2.0*SoundSpeed_Total2/Gamma_Minus_One;

			/*--- Solve quadratic equation for velocity magnitude. Value must
			 be positive, so the choice of root is clear. ---*/
			dd = bb*bb - 4.0*aa*cc;
			dd = sqrt(max(0.0,dd));
			Vel_Mag   = (-bb + dd)/(2.0*aa);
			Vel_Mag   = max(0.0,Vel_Mag);
			Velocity2 = Vel_Mag*Vel_Mag;

			/*--- Compute speed of sound from total speed of sound eqn. ---*/
			SoundSpeed2 = SoundSpeed_Total2 - 0.5*Gamma_Minus_One*Velocity2;

			/*--- Mach squared (cut between 0-1), use to adapt velocity ---*/
			Mach2 = Velocity2/SoundSpeed2;
			Mach2 = min(1.0,Mach2);
			Velocity2   = Mach2*SoundSpeed2;
			Vel_Mag     = sqrt(Velocity2);
			SoundSpeed2 = SoundSpeed_Total2 - 0.5*Gamma_Minus_One*Velocity2;

			/*--- Compute new velocity vector at the inlet ---*/
			for (iDim = 0; iDim < nDim; iDim++)
				Velocity[iDim] = Vel_Mag*Flow_Dir[iDim];

			/*--- Static temperature from the speed of sound relation ---*/
			Temperature = SoundSpeed2/(Gamma*Gas_Constant);

			/*--- Static pressure using isentropic relation at a point ---*/
			Pressure = P_Total*pow((Temperature/T_Total),Gamma/Gamma_Minus_One);

			/*--- Density at the inlet from the gas law ---*/
			Density = Pressure/(Gas_Constant*Temperature);

			/*--- Using pressure, density, & velocity, compute the energy ---*/
			Energy = Pressure/(Density*Gamma_Minus_One)+0.5*Velocity2;

			/*--- Conservative variables, using the derived quantities ---*/
			U_inlet[0] = Density;
			U_inlet[1] = Velocity[0]*Density;
			U_inlet[2] = Velocity[1]*Density;
			U_inlet[3] = Energy*Density;
			if (nDim == 3) {
				U_inlet[3] = Velocity[2]*Density;
				U_inlet[4] = Energy*Density;
			}

			/*--- Set various quantities in the solver class ---*/
			solver->SetNormal(Normal);
			solver->SetConservative(U_domain, U_inlet);

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- Jacobian contribution for implicit integration ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}

	delete [] U_domain;
	delete [] U_inlet;
	delete [] Normal;
	delete [] Flow_Dir;

}

void CEulerSolution::BC_Sym_Plane(CGeometry *geometry, CSolution **solution_container, CNumerics *solver, CConfig *config, unsigned short val_marker) {
	unsigned long iPoint, iVertex;
	unsigned short iDim, iVar, jVar;
	double Pressure, *Normal, Density;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool incompressible = config->GetIncompressible();

	/*--- Loop over all the vertices ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- If the node belong to the domain ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Compute the projected residual ---*/
			Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

			double Area = 0.0; double UnitaryNormal[3];
			for (iDim = 0; iDim < nDim; iDim++)
				Area += Normal[iDim]*Normal[iDim];
			Area = sqrt (Area);

			for (iDim = 0; iDim < nDim; iDim++)
				UnitaryNormal[iDim] = -Normal[iDim]/Area;

			if (incompressible) Pressure = node[iPoint]->GetSolution(0);
			else Pressure = node[iPoint]->GetPressure(incompressible);

			/*--- Set the residual using the pressure ---*/
			Residual[0] = 0;
			for (iDim = 0; iDim < nDim; iDim++)
				Residual[iDim+1] = Pressure*UnitaryNormal[iDim]*Area;
			if (!incompressible) Residual[nVar-1] = 0;

			/*--- Add value to the residual ---*/
			node[iPoint]->AddRes_Conv(Residual);

			/*--- In case we are doing a implicit computation ---*/
			if (implicit) {
				if (incompressible)  {
					Density = node[iPoint]->GetDensityInc();
					for (iVar = 0; iVar < nVar; iVar++) {
						for (jVar = 0; jVar < nVar; jVar++)
							Jacobian_i[iVar][jVar] = 0.0;
					}
					for (iDim = 0; iDim < nDim; iDim++)
						Jacobian_i[iDim+1][0] = -Normal[iDim];
					Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
				}
				else {
					double a2 = Gamma-1.0;
					double phi = 0.5*a2*node[iPoint]->GetVelocity2();

					for (iVar = 0; iVar < nVar; iVar++) {
						Jacobian_i[0][iVar] = 0.0;
						Jacobian_i[nDim+1][iVar] = 0.0;
					}
					for (iDim = 0; iDim < nDim; iDim++) {
						Jacobian_i[iDim+1][0] = -phi*Normal[iDim];
						for (unsigned short jDim = 0; jDim < nDim; jDim++)
							Jacobian_i[iDim+1][jDim+1] = a2*node[iPoint]->GetVelocity(jDim, incompressible)*Normal[iDim];
						Jacobian_i[iDim+1][nDim+1] = -a2*Normal[iDim];
					}
					Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
				}
			}
		}
	}
}

void CEulerSolution::BC_Interface_Boundary(CGeometry *geometry, CSolution **solution_container, CNumerics *solver,
		CConfig *config, unsigned short val_marker) {
	unsigned long iVertex, iPoint, jPoint;
	unsigned short iVar, iDim;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

#ifdef NO_MPI

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetDonorPoint();

			if (iPoint != jPoint) {

				/*--- Store the solution for both points ---*/
				for (iVar = 0; iVar < nVar; iVar++) {
					Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
					Solution_j[iVar] = node[jPoint]->GetSolution(iVar);
				}

				/*--- Set Conservative Variables ---*/
				solver->SetConservative(Solution_i, Solution_j);

				/*--- Set the normal vector ---*/
				geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
				for (iDim = 0; iDim < nDim; iDim++)
					Vector[iDim] = -Vector[iDim];
				solver->SetNormal(Vector);

				/*--- Add Residuals and Jacobians ---*/
				solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
				node[iPoint]->AddRes_Conv(Residual);
				if (implicit) Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);

			}
		}
	}

#else

	int rank = MPI::COMM_WORLD.Get_rank(), jProcessor;
	double *Conserv_Var;
	bool compute;

	double *Buffer_Send_U = new double [nVar];
	double *Buffer_Receive_U = new double [nVar];

	/*--- Do the send process, by the moment we are sending each
	 node individually, this must be changed ---*/
	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			if ((iPoint == jPoint) && (jProcessor == rank)) compute = false;
			else compute = true;

			/*--- We only send the information that belong to other boundary ---*/
			if ((jProcessor != rank) && compute) {
				Conserv_Var = node[iPoint]->GetSolution();
				for (iVar = 0; iVar < nVar; iVar++)
					Buffer_Send_U[iVar] = Conserv_Var[iVar];
				MPI::COMM_WORLD.Bsend(Buffer_Send_U, nVar, MPI::DOUBLE, jProcessor, iPoint);
			}
		}
	}

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			if ((iPoint == jPoint) && (jProcessor == rank)) compute = false;
			else compute = true;

			if (compute) {
				/*--- We only receive the information that belong to other boundary ---*/
				if (jProcessor != rank)
					MPI::COMM_WORLD.Recv(Buffer_Receive_U, nVar, MPI::DOUBLE, jProcessor, jPoint);
				else {
					for (iVar = 0; iVar < nVar; iVar++)
						Buffer_Receive_U[iVar] = node[jPoint]->GetSolution(iVar);
				}

				/*--- Store the solution for both points ---*/
				for (iVar = 0; iVar < nVar; iVar++) {
					Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
					Solution_j[iVar] = Buffer_Receive_U[iVar];
				}

				solver->SetConservative(Solution_i, Solution_j);

				geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
				for (iDim = 0; iDim < nDim; iDim++)
					Vector[iDim] = -Vector[iDim];
				solver->SetNormal(Vector);

				solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
				node[iPoint]->AddRes_Conv(Residual);
				if (implicit) Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);
			}
		}
	}

	delete[] Buffer_Send_U;
	delete[] Buffer_Receive_U;

#endif

}

void CEulerSolution::BC_NearField_Boundary(CGeometry *geometry, CSolution **solution_container, CNumerics *solver,
		CConfig *config, unsigned short val_marker) {
	unsigned long iVertex, iPoint, jPoint;
	unsigned short iVar, iDim;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

#ifdef NO_MPI

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetDonorPoint();

			if (iPoint != jPoint) {

				/*--- Store the solution for both points ---*/
				for (iVar = 0; iVar < nVar; iVar++) {
					Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
					Solution_j[iVar] = node[jPoint]->GetSolution(iVar);
				}

				/*--- Set Conservative Variables ---*/
				solver->SetConservative(Solution_i, Solution_j);

				/*--- Set the normal vector ---*/
				geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
				for (iDim = 0; iDim < nDim; iDim++)
					Vector[iDim] = -Vector[iDim];
				solver->SetNormal(Vector);

				/*--- Add Residuals and Jacobians ---*/
				solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
				node[iPoint]->AddRes_Conv(Residual);
				if (implicit) Jacobian.AddBlock(iPoint,iPoint,Jacobian_i);
			}
		}
	}

#else

	int rank = MPI::COMM_WORLD.Get_rank(), jProcessor;
	double *Conserv_Var;
	bool compute;

	double *Buffer_Send_U = new double [nVar];
	double *Buffer_Receive_U = new double [nVar];

	/*--- Do the send process, by the moment we are sending each
	 node individually, this must be changed ---*/
	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			if ((iPoint == jPoint) && (jProcessor == rank)) compute = false;
			else compute = true;

			/*--- We only send the information that belong to other boundary, -1 processor
			 means that the boundary condition is not applied ---*/
			if ((jProcessor != rank) && compute) {
				Conserv_Var = node[iPoint]->GetSolution();
				for (iVar = 0; iVar < nVar; iVar++)
					Buffer_Send_U[iVar] = Conserv_Var[iVar];
				MPI::COMM_WORLD.Bsend(Buffer_Send_U, nVar, MPI::DOUBLE, jProcessor, iPoint);
			}
		}
	}

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			if ((iPoint == jPoint) && (jProcessor == rank)) compute = false;
			else compute = true;

			if (compute) {
				/*--- We only receive the information that belong to other boundary ---*/
				if (jProcessor != rank)
					MPI::COMM_WORLD.Recv(Buffer_Receive_U, nVar, MPI::DOUBLE, jProcessor, jPoint);
				else {
					for (iVar = 0; iVar < nVar; iVar++)
						Buffer_Receive_U[iVar] = node[jPoint]->GetSolution(iVar);
				}

				/*--- Store the solution for both points ---*/
				for (iVar = 0; iVar < nVar; iVar++) {
					Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
					Solution_j[iVar] = Buffer_Receive_U[iVar];
				}

				/*--- Set Conservative Variables ---*/
				solver->SetConservative(Solution_i, Solution_j);

				/*--- Set Normal ---*/
				geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
				for (iDim = 0; iDim < nDim; iDim++)
					Vector[iDim] = -Vector[iDim];
				solver->SetNormal(Vector);

				/*--- Add Residuals and Jacobians ---*/
				solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
				node[iPoint]->AddRes_Conv(Residual);
				if (implicit) Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);
			}
		}
	}

	delete[] Buffer_Send_U;
	delete[] Buffer_Receive_U;

#endif

}

void CEulerSolution::BC_Dirichlet(CGeometry *geometry, CSolution **solution_container,
		CConfig *config, unsigned short val_marker) {
	unsigned long iVertex, iPoint, jPoint, total_index;
	unsigned short iVar;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);

#ifdef NO_MPI

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetDonorPoint();

			/*--- Store the solution for both points ---*/
			for (iVar = 0; iVar < nVar; iVar++) {
				Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
				Solution_j[iVar] = node[jPoint]->GetSolution(iVar);
			}

			/*--- Set the residual, truncation error and solution value ---*/
			node[iPoint]->SetSolution_Old(Solution_j);
			node[iPoint]->SetSolution(Solution_j);
			node[iPoint]->Set_ResConv_Zero();
			node[iPoint]->Set_ResSour_Zero();
			node[iPoint]->Set_ResVisc_Zero();
			node[iPoint]->SetRes_TruncErrorZero();

			/*--- Change rows of the Jacobian (includes 1 in the diagonal) ---*/
			if (implicit)
				for (iVar = 0; iVar < nVar; iVar++) {
					total_index = iPoint*nVar+iVar;
					Jacobian.DeleteValsRowi(total_index);
				}

		}
	}

#else

	int rank = MPI::COMM_WORLD.Get_rank(), jProcessor;
	double *Conserv_Var;

	double *Buffer_Send_U = new double [nVar];
	double *Buffer_Receive_U = new double [nVar];

	/*--- Do the send process, by the moment we are sending each
	 node individually, this must be changed ---*/
	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			/*--- We only send the information that belong to other boundary ---*/
			if (jProcessor != rank) {
				Conserv_Var = node[iPoint]->GetSolution();
				for (iVar = 0; iVar < nVar; iVar++)
					Buffer_Send_U[iVar] = Conserv_Var[iVar];
				MPI::COMM_WORLD.Bsend(Buffer_Send_U, nVar, MPI::DOUBLE, jProcessor, iPoint);
			}
		}
	}

	for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Find the associate pair to the original node ---*/
			jPoint = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[0];
			jProcessor = geometry->vertex[val_marker][iVertex]->GetPeriodicPointDomain()[1];

			/*--- We only receive the information that belong to other boundary ---*/
			if (jProcessor != rank)
				MPI::COMM_WORLD.Recv(Buffer_Receive_U, nVar, MPI::DOUBLE, jProcessor, jPoint);
			else {
				for (iVar = 0; iVar < nVar; iVar++)
					Buffer_Receive_U[iVar] = node[jPoint]->GetSolution(iVar);
			}

			/*--- Store the solution for both points ---*/
			for (iVar = 0; iVar < nVar; iVar++) {
				Solution_i[iVar] = node[iPoint]->GetSolution(iVar);
				Solution_j[iVar] = Buffer_Receive_U[iVar];
			}

			/*--- Set the residual, truncation error and solution value ---*/
			node[iPoint]->SetSolution_Old(Solution_j);
			node[iPoint]->SetSolution(Solution_j);
			node[iPoint]->Set_ResConv_Zero();
			node[iPoint]->Set_ResSour_Zero();
			node[iPoint]->Set_ResVisc_Zero();
			node[iPoint]->SetRes_TruncErrorZero();

			/*--- Change rows of the Jacobian (includes 1 in the diagonal) ---*/
			if (implicit)
				for (iVar = 0; iVar < nVar; iVar++) {
					total_index = iPoint*nVar+iVar;
					Jacobian.DeleteValsRowi(total_index);
				}

		}
	}

	delete[] Buffer_Send_U;
	delete[] Buffer_Receive_U;

#endif

}

void CEulerSolution::BC_Custom(CGeometry *geometry, CSolution **solution_container, CNumerics *solver, CConfig *config, unsigned short val_marker) {
	unsigned long iVertex, iPoint;
	unsigned short iVar, iDim;
	double *U_domain, press, tempe, velo[3], b, rho, L, V, psi, Vpsi, theta, xx, yy;
	U_domain = new double[nVar];

	/*-------- Set Problem Parameters for Ringleb flow -------------*/
	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	unsigned short imax = 500;
	double tol = 1.e-10;
	double Vmin = 0.;
	double Vmax = 2.;
	double Vp = 0.5*( Vmin + Vmax );

	/*--- Loop over all the vertices ---*/
	for (iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
		iPoint = geometry->vertex[val_marker][iVertex]->GetNode();

		/*--- If the node belong to the domain ---*/
		if (geometry->node[iPoint]->GetDomain()) {

			/*--- Copy the solution on the boundary ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				Solution[iVar] = node[iPoint]->GetSolution(iVar);

			xx = geometry->node[iPoint]->GetCoord(0);
			yy = geometry->node[iPoint]->GetCoord(1);

			/*--- Iterate to find solution (Adapted from Matsuzaka) ---*/
			for(unsigned short i = 1; i <= imax; i++) {
				b  = sqrt(1. - 0.2*Vp*Vp);
				rho = pow(b,5);
				L  = 1/b + 1/(3*pow(b,3)) + 1/(5*pow(b,5)) - 0.5*log((1+b)/(1-b));
				V   = sqrt(1/sqrt(fabs((xx-0.5*L)*(xx-0.5*L)+yy*yy) )/2/rho);
				if (fabs(V-Vp) < tol) break;
				if (i == imax)
					cout<<"did not converge... "<<fabs(V-Vp)<<" "<<xx<<" "<<yy<<endl;
				Vp = V;
			}

			b = sqrt(1. - 0.2*V*V);
			rho = pow(b,5);
			press = rho*b*b/Gamma;
			L = 1/b + 1/(3*pow(b,3)) + 1/(5*pow(b,5))-0.5*log((1+b)/(1-b));
			psi = sqrt(0.5/V/V-(xx-0.5*L)*rho);    Vpsi= sqrt(0.5-V*V*(xx-0.5*L)*rho);
			if ( fabs( Vpsi - 1. ) < tol  ) theta = 0.5*PI_NUMBER;
			else theta = asin( psi*V );
			velo[0] = -V*cos(theta); velo[1] = -V*sin(theta); velo[2] = 0.;
			tempe = press/rho;

			/*--- Load up the solution vector ---*/
			Solution[0] = rho;
			for (iVar = 1; iVar< nVar-1; iVar++)
				Solution[iVar] = rho*velo[iVar-1];
			Solution[nVar-1] = press/(Gamma-1.)+0.5*rho*(velo[0]*velo[0]+velo[1]*velo[1]);

			/*--- Weak boundary imposition ---*/
			for (iVar = 0; iVar < nVar; iVar++)
				U_domain[iVar]= node[iPoint]->GetSolution(iVar);

			/*--- Set the normal vector ---*/
			geometry->vertex[val_marker][iVertex]->GetNormal(Vector);
			for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = -Vector[iDim];
			solver->SetNormal(Vector);

			/*--- Compute the residual using an upwind scheme ---*/
			solver->SetConservative(U_domain, Solution);
			solver->SetResidual(Residual, Jacobian_i, Jacobian_j, config);
			node[iPoint]->AddRes_Conv(Residual);

			/*--- In case we are doing a implicit computation ---*/
			if (implicit)
				Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);

		}
	}
}

#ifndef NEW_MPI

void CEulerSolution::MPI_Send_Receive(CGeometry ***geometry, CSolution ****solution_container,
																			CConfig **config, unsigned short iMGLevel, unsigned short iZone) {
	
	unsigned short iVar, iDim, iMarker, iPeriodic_Index;
	unsigned long iVertex, iPoint, nVertex, nBuffer_Vector, nBuffer_VectorGrad, nBuffer_Scalar;
	double rotMatrix[3][3], *angles, theta, cosTheta, sinTheta, phi, cosPhi, sinPhi, psi, cosPsi, 
	sinPsi, *newSolution = NULL, **newGradient = NULL, *Buffer_Receive_U = NULL, 
	*Buffer_Receive_BetaInc2 = NULL, *Buffer_Receive_DensityInc = NULL,
	*Buffer_Receive_UGrad = NULL, *Buffer_Receive_Limit = NULL, *Buffer_Receive_Undivided_Laplacian = NULL, 
	*Buffer_Receive_Sensor = NULL, *Buffer_Receive_Lambda = NULL, *Buffer_Receive_LaminarViscosity = NULL, 
	*Buffer_Receive_EddyViscosity = NULL, *Buffer_Receive_VGrad = NULL;
	unsigned short *Buffer_Receive_Neighbor = NULL;
	short SendRecv;
	int send_to, receive_from;
	bool viscous = false, upwind = false, centered = false;
	bool incompressible = config[iZone]->GetIncompressible();
	
#ifdef NO_MPI
	double N_0, N_1, N_2;
	unsigned long Point_0, Point_1, Point_2, donorPoint, donorElem;
	unsigned short donorZone, iMark, donorMarker, donor_marker;
#else
	double *Buffer_Send_U = NULL, *Buffer_Send_BetaInc2 = NULL, *Buffer_Send_DensityInc = NULL, *Buffer_Send_LaminarViscosity = NULL, 
	*Buffer_Send_EddyViscosity = NULL, *Buffer_Send_VGrad = NULL, *Buffer_Send_UGrad = NULL, *Buffer_Send_Limit = NULL, *Buffer_Send_Undivided_Laplacian = NULL,
	*Buffer_Send_Sensor = NULL, *Buffer_Send_Lambda = NULL;
	unsigned short *Buffer_Send_Neighbor = NULL;
#endif
	
	newSolution = new double[nVar];
	newGradient = new double* [nVar];
	for (iVar=0; iVar< nVar; iVar++)
		newGradient[iVar] = new double[3];
	
	if ((config[iZone]->GetKind_Solver() == NAVIER_STOKES) || (config[iZone]->GetKind_Solver() == ADJ_NAVIER_STOKES)) viscous = true;
	if ((config[iZone]->GetKind_Solver() == RANS) || (config[iZone]->GetKind_Solver() == ADJ_RANS)) viscous = true;
	if (config[iZone]->GetKind_ConvNumScheme() == SPACE_UPWIND) upwind = true;
	if (config[iZone]->GetKind_ConvNumScheme() == SPACE_CENTERED) centered = true;
	
	/*--- Send-Receive boundary conditions ---*/
	for (iMarker = 0; iMarker < config[iZone]->GetnMarker_All(); iMarker++) {
		
		if (config[iZone]->GetMarker_All_Boundary(iMarker) == SEND_RECEIVE) {
			
			SendRecv = config[iZone]->GetMarker_All_SendRecv(iMarker);
			nVertex = geometry[iZone][iMGLevel]->nVertex[iMarker];
			
			nBuffer_VectorGrad	= nVertex*nVar*nDim;
			nBuffer_Vector			= nVertex*nVar;
			nBuffer_Scalar			= nVertex;
			
			send_to = SendRecv-1;
			receive_from = abs(SendRecv)-1;
			
#ifndef NO_MPI
			
			/*--- Send information using MPI  ---*/
			if (SendRecv > 0) {
				
				/*--- Allocate upwind variables ---*/
				if (upwind) {
					Buffer_Send_U = new double[nBuffer_Vector];
					if (iMGLevel == MESH_0) {
						Buffer_Send_UGrad = new double[nBuffer_VectorGrad];
						Buffer_Send_Limit = new double[nBuffer_Vector];
					}
				}
				
				/*--- Allocate centered variables ---*/
				if (centered) {
					Buffer_Send_U = new double[nBuffer_Vector];
					Buffer_Send_Lambda = new double [nBuffer_Scalar];
					Buffer_Send_Neighbor = new unsigned short [nBuffer_Scalar];
					if (iMGLevel == MESH_0) {
						Buffer_Send_Undivided_Laplacian = new double [nBuffer_Vector];
						Buffer_Send_Sensor = new double [nBuffer_Scalar];
					}
				}
				
				/*--- Allocate incompressible variables ---*/
				if (incompressible) {
					Buffer_Send_BetaInc2 = new double [nBuffer_Scalar];
					Buffer_Send_DensityInc = new double [nBuffer_Scalar];
				}
				
				/*--- Allocate viscous variables ---*/
				if (viscous) {
					Buffer_Send_LaminarViscosity = new double[nBuffer_Scalar];
					Buffer_Send_EddyViscosity = new double[nBuffer_Scalar];
					Buffer_Send_VGrad = new double[nBuffer_VectorGrad];
				}
				
				for (iVertex = 0; iVertex < nVertex; iVertex++) {
					
					iPoint = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetNode();
					
					/*--- Copy upwind data ---*/
					if (upwind) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Send_U[iVar*nVertex+iVertex] = node[iPoint]->GetSolution(iVar);
							if (iMGLevel == MESH_0) {
								Buffer_Send_Limit[iVar*nVertex+iVertex] = node[iPoint]->GetLimiter(iVar);
								for (iDim = 0; iDim < nDim; iDim++)
									Buffer_Send_UGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = node[iPoint]->GetGradient(iVar,iDim);
							}
						}
					}
					
					/*--- Copy centered data ---*/
					if (centered) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Send_U[iVar*nVertex+iVertex] = node[iPoint]->GetSolution(iVar);
							if (iMGLevel == MESH_0)
								Buffer_Send_Undivided_Laplacian[iVar*nVertex+iVertex] = node[iPoint]->GetUnd_Lapl(iVar);							
						}
						if (iMGLevel == MESH_0) Buffer_Send_Sensor[iVertex] = node[iPoint]->GetSensor();
						Buffer_Send_Lambda[iVertex] = node[iPoint]->GetLambda();
						Buffer_Send_Neighbor[iVertex] = geometry[iZone][iMGLevel]->node[iPoint]->GetnPoint();
					}
					
					/*--- Copy incompressible data ---*/
					if (incompressible) {
						Buffer_Send_BetaInc2[iVertex] = node[iPoint]->GetBetaInc2();
						Buffer_Send_DensityInc[iVertex] = node[iPoint]->GetDensityInc();
					}
					
					/*--- Copy viscous data ---*/
					if (viscous) {
						Buffer_Send_LaminarViscosity[iVertex] = node[iPoint]->GetLaminarViscosity();
						Buffer_Send_EddyViscosity[iVertex] = node[iPoint]->GetEddyViscosity();
						for (iVar = 0; iVar < nVar; iVar++)
							for (iDim = 0; iDim < nDim; iDim++)
								Buffer_Send_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = node[iPoint]->GetGradient_Primitive(iVar,iDim);							
					}
					
				}
				
				/*--- Send the buffer, and deallocate information using MPI ---*/
				if (upwind) {
					MPI::COMM_WORLD.Bsend(Buffer_Send_U, nBuffer_Vector, MPI::DOUBLE, send_to, 0); delete [] Buffer_Send_U;
					if (iMGLevel == MESH_0) {
						MPI::COMM_WORLD.Bsend(Buffer_Send_UGrad, nBuffer_VectorGrad, MPI::DOUBLE, send_to, 1); delete [] Buffer_Send_UGrad;
						MPI::COMM_WORLD.Bsend(Buffer_Send_Limit, nBuffer_Vector, MPI::DOUBLE, send_to, 2); delete [] Buffer_Send_Limit;
					}
				}
				
				if (centered) {
					MPI::COMM_WORLD.Bsend(Buffer_Send_U, nBuffer_Vector, MPI::DOUBLE, send_to, 0); delete [] Buffer_Send_U;
					MPI::COMM_WORLD.Bsend(Buffer_Send_Lambda, nBuffer_Scalar, MPI::DOUBLE, send_to, 1); delete [] Buffer_Send_Lambda;
					MPI::COMM_WORLD.Bsend(Buffer_Send_Neighbor, nBuffer_Scalar, MPI::UNSIGNED_SHORT, send_to, 2); delete [] Buffer_Send_Neighbor;
					if (iMGLevel == MESH_0) {
						MPI::COMM_WORLD.Bsend(Buffer_Send_Undivided_Laplacian, nBuffer_Vector, MPI::DOUBLE, send_to, 3); delete [] Buffer_Send_Undivided_Laplacian;
						MPI::COMM_WORLD.Bsend(Buffer_Send_Sensor, nBuffer_Scalar, MPI::DOUBLE, send_to, 4); delete [] Buffer_Send_Sensor;
					}
				}
				
				if (incompressible) {
					MPI::COMM_WORLD.Bsend(Buffer_Send_BetaInc2, nBuffer_Scalar, MPI::DOUBLE, send_to, 5); delete [] Buffer_Send_BetaInc2;
					MPI::COMM_WORLD.Bsend(Buffer_Send_DensityInc, nBuffer_Scalar, MPI::DOUBLE, send_to, 6); delete [] Buffer_Send_DensityInc;
				}
				
				if (viscous) {
					MPI::COMM_WORLD.Bsend(Buffer_Send_LaminarViscosity, nBuffer_Scalar, MPI::DOUBLE, send_to, 7); delete [] Buffer_Send_LaminarViscosity;
					MPI::COMM_WORLD.Bsend(Buffer_Send_EddyViscosity, nBuffer_Scalar, MPI::DOUBLE, send_to, 8); delete [] Buffer_Send_EddyViscosity;
					MPI::COMM_WORLD.Bsend(Buffer_Send_VGrad, nBuffer_VectorGrad, MPI::DOUBLE, send_to, 9); delete []  Buffer_Send_VGrad;
				}
				
			}
			
#endif
			
			/*--- Receive information  ---*/
			if (SendRecv < 0) {
				
				/*--- Allocate upwind variables ---*/
				if (upwind) {
					Buffer_Receive_U = new double [nBuffer_Vector];
					if (iMGLevel == MESH_0) {
						Buffer_Receive_UGrad = new double [nBuffer_VectorGrad];
						Buffer_Receive_Limit = new double [nBuffer_Vector];
					}
				}
				
				/*--- Allocate centered variables ---*/
				if (centered) {
					Buffer_Receive_U = new double [nBuffer_Vector];
					Buffer_Receive_Lambda = new double [nBuffer_Scalar];
					Buffer_Receive_Neighbor = new unsigned short [nBuffer_Scalar];
					if (iMGLevel == MESH_0) {
						Buffer_Receive_Undivided_Laplacian = new double [nBuffer_Vector];
						Buffer_Receive_Sensor = new double [nBuffer_Scalar];
					}
				}
				
				/*--- Allocate incompressible variables ---*/
				if (incompressible) {
					Buffer_Receive_BetaInc2 = new double [nBuffer_Scalar];
					Buffer_Receive_DensityInc = new double [nBuffer_Scalar];
				}
				
				/*--- Allocate viscous variables ---*/
				if (viscous) {
					Buffer_Receive_LaminarViscosity = new double [nBuffer_Scalar];
					Buffer_Receive_EddyViscosity = new double [nBuffer_Scalar];
					Buffer_Receive_VGrad = new double [nBuffer_VectorGrad];
				}
				
#ifdef NO_MPI	
				
				/*--- Allow for periodic boundaries to use SEND_RECEIVE in serial.
				 Serial computations will only use the BC in receive mode, as
				 the proc will always be sending information to itself. ---*/
				
				/*--- Retrieve the donor boundary marker ---*/
				donor_marker = 1;
				donorZone    = 0;
				donorMarker  = 1;
				
				for (iMark = 0; iMark < config[iZone]->GetnMarker_All(); iMark++)
					if (config[iZone]->GetMarker_All_SendRecv(iMark)-1 == receive_from) donor_marker = iMark;
				
				/*--- Get the information from the donor point directly. This is a
				 serial computation with access to all nodes. Note that there is an
				 implicit ordering in the list. ---*/
				for (iVertex = 0; iVertex < nVertex; iVertex++) {
					
					donorPoint = geometry[iZone][iMGLevel]->vertex[donor_marker][iVertex]->GetNode();
					donorZone = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetMatching_Zone();
					
					/*--- This is a multizone simulation and we need to use the
					 results of the search & interpolation routine in case of
					 sliding interfaces. ---*/
					if (donorZone != iZone) {
						
						donorElem = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetDonorElem();
						
						N_0 = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetBasisFunction(0);
						N_1 = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetBasisFunction(1);
						N_2 = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetBasisFunction(2);
						
						Point_0 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(0);
						Point_1 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(1);
						Point_2 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(2);
						
						/*--- Interpolation and storage of values from this element. ---*/
						if (upwind) {
							for (iVar = 0; iVar < nVar; iVar++) {
								Buffer_Receive_U[iVar*nVertex+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSolution(iVar)
								+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSolution(iVar)
								+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSolution(iVar);
								/*--- Gradient and limiters... ---*/
								if (iMGLevel == MESH_0) {
									
								}
							}	
							
						}
						
						if (centered) {
							for (iVar = 0; iVar < nVar; iVar++) {
								Buffer_Receive_U[iVar*nVertex+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSolution(iVar)
								+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSolution(iVar)
								+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSolution(iVar);
								if (iMGLevel == MESH_0)
									Buffer_Receive_Undivided_Laplacian[iVar*nVertex+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetUnd_Lapl(iVar)
									+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetUnd_Lapl(iVar)
									+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetUnd_Lapl(iVar);
							}
							if (iMGLevel == MESH_0)
								Buffer_Receive_Sensor[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSensor()
								+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSensor()
								+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSensor();
							
							Buffer_Receive_Lambda[iVertex]  = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetLambda()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetLambda()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetLambda();
							
							/*--- Just use the neighbors from the first node for now... ---*/
							Buffer_Receive_Neighbor[iVertex] = geometry[donorZone][iMGLevel]->node[Point_0]->GetnPoint();
						}
						
						if (incompressible) {
							Buffer_Receive_BetaInc2[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetBetaInc2()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetBetaInc2()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetBetaInc2();
							Buffer_Receive_DensityInc[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetDensityInc()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetDensityInc()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetDensityInc();
						}
						
						if (viscous) {
							Buffer_Receive_LaminarViscosity[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetLaminarViscosity()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetLaminarViscosity()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetLaminarViscosity();
							Buffer_Receive_EddyViscosity[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetEddyViscosity()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetEddyViscosity()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetEddyViscosity();
							
							for (iVar = 0; iVar < nVar; iVar++) {
								for (iDim = 0; iDim < nDim; iDim++) {
									Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex]  = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetGradient_Primitive(iVar, iDim);
									Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] += N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetGradient_Primitive(iVar, iDim);
									Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] += N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetGradient_Primitive(iVar, iDim);
								}
							}
							
						}						
					}
					
					else {
						
						/*--- For now, search for donor marker for every receive point. Probably
						 a more efficient way to do this in the future. ---*/
						for (unsigned short iMark = 0; iMark < config[donorZone]->GetnMarker_All(); iMark++)
							if (config[donorZone]->GetMarker_All_SendRecv(iMark)-1 == receive_from) donorMarker = iMark;
						
						/*--- Index of the donor point. ---*/
						donorPoint = geometry[donorZone][iMGLevel]->vertex[donorMarker][iVertex]->GetNode();
						
						/*--- Copy upwind data ---*/
						if (upwind) {
							for (iVar = 0; iVar < nVar; iVar++) {
								Buffer_Receive_U[iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetSolution(iVar);
								if (iMGLevel == MESH_0) {
									Buffer_Receive_Limit[iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLimiter(iVar);
									for (iDim = 0; iDim < nDim; iDim++)
										Buffer_Receive_UGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetGradient(iVar,iDim);
								}
							}
						}
						
						/*--- Copy centered data ---*/
						if (centered) {
							for (iVar = 0; iVar < nVar; iVar++) {
								Buffer_Receive_U[iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetSolution(iVar);
								if (iMGLevel == MESH_0)
									Buffer_Receive_Undivided_Laplacian[iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetUnd_Lapl()[iVar];							
							}
							Buffer_Receive_Lambda[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLambda();
							Buffer_Receive_Neighbor[iVertex] = geometry[donorZone][iMGLevel]->node[donorPoint]->GetnPoint();
						}
						
						/*--- Copy incompressible data ---*/
						if (incompressible) {
							Buffer_Receive_BetaInc2[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetBetaInc2();
							Buffer_Receive_DensityInc[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetDensityInc();
						}
						
						/*--- Copy viscous data ---*/
						if (viscous) {
							Buffer_Receive_LaminarViscosity[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLaminarViscosity();
							Buffer_Receive_EddyViscosity[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetEddyViscosity();
							for (iVar = 0; iVar < nVar; iVar++)
								for (iDim = 0; iDim < nDim; iDim++)
									Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetGradient_Primitive(iVar,iDim);
						}
					}
				}
				
#else
				/*--- Receive the information using MPI---*/
				if (upwind) {
					MPI::COMM_WORLD.Recv(Buffer_Receive_U, nBuffer_Vector, MPI::DOUBLE, receive_from, 0);
					if (iMGLevel == MESH_0) {
						MPI::COMM_WORLD.Recv(Buffer_Receive_UGrad, nBuffer_VectorGrad, MPI::DOUBLE, receive_from, 1);
						MPI::COMM_WORLD.Recv(Buffer_Receive_Limit, nBuffer_Vector, MPI::DOUBLE, receive_from, 2);
					}
				}
				
				if (centered) {
					MPI::COMM_WORLD.Recv(Buffer_Receive_U, nBuffer_Vector, MPI::DOUBLE, receive_from, 0);
					MPI::COMM_WORLD.Recv(Buffer_Receive_Lambda, nBuffer_Scalar, MPI::DOUBLE, receive_from, 1);
					MPI::COMM_WORLD.Recv(Buffer_Receive_Neighbor, nBuffer_Scalar, MPI::UNSIGNED_SHORT, receive_from, 2);
					if (iMGLevel == MESH_0) {
						MPI::COMM_WORLD.Recv(Buffer_Receive_Undivided_Laplacian, nBuffer_Vector, MPI::DOUBLE, receive_from, 3);
						MPI::COMM_WORLD.Recv(Buffer_Receive_Sensor, nBuffer_Scalar, MPI::DOUBLE, receive_from, 4);
					}
				}
				
				if (incompressible) {
					MPI::COMM_WORLD.Recv(Buffer_Receive_BetaInc2, nBuffer_Scalar, MPI::DOUBLE, receive_from, 5);
					MPI::COMM_WORLD.Recv(Buffer_Receive_DensityInc, nBuffer_Scalar, MPI::DOUBLE, receive_from, 6);
				}
				
				if (viscous) {
					MPI::COMM_WORLD.Recv(Buffer_Receive_LaminarViscosity, nBuffer_Scalar, MPI::DOUBLE, receive_from, 7);
					MPI::COMM_WORLD.Recv(Buffer_Receive_EddyViscosity, nBuffer_Scalar, MPI::DOUBLE, receive_from, 8);
					MPI::COMM_WORLD.Recv(Buffer_Receive_VGrad, nBuffer_VectorGrad, MPI::DOUBLE, receive_from, 9);
				}
#endif
				
				/*--- Do the coordinate transformation ---*/
				for (iVertex = 0; iVertex < nVertex; iVertex++) {
					
					/*--- Find point and its type of transformation ---*/
					iPoint = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetNode();
					iPeriodic_Index = geometry[iZone][iMGLevel]->vertex[iMarker][iVertex]->GetRotation_Type();
					
					/*--- Retrieve the supplied periodic information. ---*/
					angles = config[iZone]->GetPeriodicRotation(iPeriodic_Index);
					
					/*--- Store angles separately for clarity. ---*/
					theta    = angles[0];   phi    = angles[1]; psi    = angles[2];
					cosTheta = cos(theta);  cosPhi = cos(phi);  cosPsi = cos(psi);
					sinTheta = sin(theta);  sinPhi = sin(phi);  sinPsi = sin(psi);
					
					/*--- Compute the rotation matrix. Note that the implicit
					 ordering is rotation about the x-axis, y-axis,
					 then z-axis. Note that this is the transpose of the matrix
					 used during the preprocessing stage. ---*/
					rotMatrix[0][0] = cosPhi*cosPsi; rotMatrix[1][0] = sinTheta*sinPhi*cosPsi - cosTheta*sinPsi; rotMatrix[2][0] = cosTheta*sinPhi*cosPsi + sinTheta*sinPsi;
					rotMatrix[0][1] = cosPhi*sinPsi; rotMatrix[1][1] = sinTheta*sinPhi*sinPsi + cosTheta*cosPsi; rotMatrix[2][1] = cosTheta*sinPhi*sinPsi - sinTheta*cosPsi;
					rotMatrix[0][2] = -sinPhi; rotMatrix[1][2] = sinTheta*cosPhi; rotMatrix[2][2] = cosTheta*cosPhi;
					
					/*--- Copy conserved variables before performing transformation. ---*/
					for (iVar = 0; iVar < nVar; iVar++) 
						newSolution[iVar] = Buffer_Receive_U[iVar*nVertex+iVertex];
					
					/*--- Rotate the momentum components. ---*/
					if (nDim == 2) {
						newSolution[1] = rotMatrix[0][0]*Buffer_Receive_U[1*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_U[2*nVertex+iVertex];
						newSolution[2] = rotMatrix[1][0]*Buffer_Receive_U[1*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_U[2*nVertex+iVertex];
					}
					else {
						newSolution[1] = rotMatrix[0][0]*Buffer_Receive_U[1*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_U[2*nVertex+iVertex] + rotMatrix[0][2]*Buffer_Receive_U[3*nVertex+iVertex];
						newSolution[2] = rotMatrix[1][0]*Buffer_Receive_U[1*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_U[2*nVertex+iVertex] + rotMatrix[1][2]*Buffer_Receive_U[3*nVertex+iVertex];
						newSolution[3] = rotMatrix[2][0]*Buffer_Receive_U[1*nVertex+iVertex] + rotMatrix[2][1]*Buffer_Receive_U[2*nVertex+iVertex] + rotMatrix[2][2]*Buffer_Receive_U[3*nVertex+iVertex];
					}
					
					/*--- Copy transformed conserved variables back into buffer. ---*/
					for (iVar = 0; iVar < nVar; iVar++) 
						Buffer_Receive_U[iVar*nVertex+iVertex] = newSolution[iVar];
					
					/*--- Also transform the gradient for upwinding if this is the fine mesh ---*/
					if ((upwind) && (iMGLevel == MESH_0)) {
						
						for (iVar = 0; iVar < nVar; iVar++) 
							for (iDim = 0; iDim < nDim; iDim++)
								newGradient[iVar][iDim] = Buffer_Receive_UGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex];
						
						/*--- Need to rotate the gradients for all conserved variables. ---*/
						for (iVar = 0; iVar < nVar; iVar++) {
							if (nDim == 2) {
								newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_UGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_UGrad[1*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_UGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_UGrad[1*nVar*nVertex+iVar*nVertex+iVertex];
							}
							else {
								newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_UGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_UGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][2]*Buffer_Receive_UGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_UGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_UGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][2]*Buffer_Receive_UGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][2] = rotMatrix[2][0]*Buffer_Receive_UGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[2][1]*Buffer_Receive_UGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[2][2]*Buffer_Receive_UGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
							}
						}
						
						/*--- Copy transformed gradients back into buffer. ---*/
						for (iVar = 0; iVar < nVar; iVar++) 
							for (iDim = 0; iDim < nDim; iDim++)
								Buffer_Receive_UGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = newGradient[iVar][iDim];
						
					}
					
					/*--- Also transform the gradient for viscous terms ---*/
					if (viscous) {
						
						for (iVar = 0; iVar < nVar; iVar++)
							for (iDim = 0; iDim < nDim; iDim++)
								newGradient[iVar][iDim] = Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex];
						
						/*--- Need to rotate the gradients for all conserved variables. ---*/
						for (iVar = 0; iVar < nVar; iVar++) {
							if (nDim == 2) {
								newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_VGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_VGrad[1*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_VGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_VGrad[1*nVar*nVertex+iVar*nVertex+iVertex];
							}
							else {
								newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_VGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][1]*Buffer_Receive_VGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[0][2]*Buffer_Receive_VGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_VGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][1]*Buffer_Receive_VGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[1][2]*Buffer_Receive_VGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
								newGradient[iVar][2] = rotMatrix[2][0]*Buffer_Receive_VGrad[0*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[2][1]*Buffer_Receive_VGrad[1*nVar*nVertex+iVar*nVertex+iVertex] + rotMatrix[2][2]*Buffer_Receive_VGrad[2*nVar*nVertex+iVar*nVertex+iVertex];
							}
						}
						
						/*--- Copy transformed gradients back into buffer. ---*/
						for (iVar = 0; iVar < nVar; iVar++)
							for (iDim = 0; iDim < nDim; iDim++)
								Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex] = newGradient[iVar][iDim];
						
					}
					
					
					/*--- Upwind method. Store the received information ---*/
					if (upwind) {
						for (iVar = 0; iVar < nVar; iVar++) {
							node[iPoint]->SetSolution(iVar, Buffer_Receive_U[iVar*nVertex+iVertex]);
							if (iMGLevel == MESH_0) {
								node[iPoint]->SetLimiter(iVar, Buffer_Receive_Limit[iVar*nVertex+iVertex]);
								for (iDim = 0; iDim < nDim; iDim++)
									node[iPoint]->SetGradient(iVar, iDim, Buffer_Receive_UGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex]);
							}
						}
					}
					
					/*--- Centered method. Store the received information ---*/
					if (centered) {
						for (iVar = 0; iVar < nVar; iVar++) {
							node[iPoint]->SetSolution(iVar, Buffer_Receive_U[iVar*nVertex+iVertex]);
							if (iMGLevel == MESH_0) 
								node[iPoint]->SetUndivided_Laplacian(iVar, Buffer_Receive_Undivided_Laplacian[iVar*nVertex+iVertex]);
						}
						if (iMGLevel == MESH_0) node[iPoint]->SetSensor(Buffer_Receive_Sensor[iVertex]);
						node[iPoint]->SetLambda(Buffer_Receive_Lambda[iVertex]);
						geometry[iZone][iMGLevel]->node[iPoint]->SetnNeighbor(Buffer_Receive_Neighbor[iVertex]);
					}
					
					/*--- Incompressible method. Store the received information ---*/
					if (incompressible) {
						node[iPoint]->SetBetaInc2(Buffer_Receive_BetaInc2[iVertex]);
						node[iPoint]->SetDensityInc(Buffer_Receive_DensityInc[iVertex]);
					}
					
					/*--- Viscous method. Store the received information ---*/
					if (viscous) {
						node[iPoint]->SetLaminarViscosity(Buffer_Receive_LaminarViscosity[iVertex]);
						node[iPoint]->SetEddyViscosity(Buffer_Receive_EddyViscosity[iVertex]);
						for (iVar = 0; iVar < nVar; iVar++)
							for (iDim = 0; iDim < nDim; iDim++)
								node[iPoint]->SetGradient_Primitive(iVar, iDim, Buffer_Receive_VGrad[iDim*nVar*nVertex+iVar*nVertex+iVertex]);
					}
					
				}
				
				if (upwind) {
					delete [] Buffer_Receive_U;
					if (iMGLevel == MESH_0) {
						delete [] Buffer_Receive_UGrad;
						delete [] Buffer_Receive_Limit;
					}
				}
				
				if (centered) {
					delete [] Buffer_Receive_U;
					delete [] Buffer_Receive_Lambda;
					delete [] Buffer_Receive_Neighbor;
					if (iMGLevel == MESH_0) {
						delete [] Buffer_Receive_Undivided_Laplacian;
						delete [] Buffer_Receive_Sensor;
					}
				}
				
				if (incompressible) {
					delete [] Buffer_Receive_BetaInc2;
					delete [] Buffer_Receive_DensityInc;
				}
				
				if (viscous) {
					delete [] Buffer_Receive_LaminarViscosity;
					delete [] Buffer_Receive_EddyViscosity;
					delete [] Buffer_Receive_VGrad;
				}
				
			}
		}
	}
	
	delete [] newSolution;
	for (iVar = 0; iVar < nVar; iVar++)
		delete [] newGradient[iVar];
	delete [] newGradient;
	
}
 
#else

void CEulerSolution::MPI_Send_Receive(CGeometry ***geometry, CSolution ****solution_container,
																			CConfig **config, unsigned short iMGLevel, unsigned short iZone) {
	
	unsigned short iVar, iDim, iMarker, iPeriodic_Index, MarkerS, MarkerR;
	unsigned long iVertex, iPoint, nVertexS, nVertexR, nBufferS_Vector, nBufferS_VectorGrad, nBufferS_Scalar, 
	nBufferR_Vector, nBufferR_VectorGrad, nBufferR_Scalar;
	double rotMatrix[3][3], *angles, theta, cosTheta, sinTheta, phi, cosPhi, sinPhi, psi, cosPsi, 
	sinPsi, *newSolution = NULL, **newGradient = NULL, *Buffer_Receive_U = NULL, 
	*Buffer_Receive_BetaInc2 = NULL, *Buffer_Receive_DensityInc = NULL,
	*Buffer_Receive_UGrad = NULL, *Buffer_Receive_Limit = NULL, *Buffer_Receive_Undivided_Laplacian = NULL, 
	*Buffer_Receive_Sensor = NULL, *Buffer_Receive_Lambda = NULL, *Buffer_Receive_LaminarViscosity = NULL, 
	*Buffer_Receive_EddyViscosity = NULL, *Buffer_Receive_VGrad = NULL;
	unsigned short *Buffer_Receive_Neighbor = NULL;
	int send_to, receive_from;
	bool viscous = false, upwind = false, centered = false;
	bool incompressible = config[iZone]->GetIncompressible();
	
#ifdef NO_MPI
	double N_0, N_1, N_2;
	unsigned long Point_0, Point_1, Point_2, donorPoint, donorElem;
	unsigned short donorZone, iMark, donorMarker, donor_marker;
#endif
	
	double *Buffer_Send_U = NULL, *Buffer_Send_BetaInc2 = NULL, *Buffer_Send_DensityInc = NULL, *Buffer_Send_LaminarViscosity = NULL, 
	*Buffer_Send_EddyViscosity = NULL, *Buffer_Send_VGrad = NULL, *Buffer_Send_UGrad = NULL, *Buffer_Send_Limit = NULL, *Buffer_Send_Undivided_Laplacian = NULL,
	*Buffer_Send_Sensor = NULL, *Buffer_Send_Lambda = NULL;
	unsigned short *Buffer_Send_Neighbor = NULL;
	
	newSolution = new double[nVar];
	newGradient = new double* [nVar];
	for (iVar=0; iVar< nVar; iVar++)
		newGradient[iVar] = new double[3];
	
	if ((config[iZone]->GetKind_Solver() == NAVIER_STOKES) || (config[iZone]->GetKind_Solver() == ADJ_NAVIER_STOKES)) viscous = true;
	if ((config[iZone]->GetKind_Solver() == RANS) || (config[iZone]->GetKind_Solver() == ADJ_RANS)) viscous = true;
	if (config[iZone]->GetKind_ConvNumScheme() == SPACE_UPWIND) upwind = true;
	if (config[iZone]->GetKind_ConvNumScheme() == SPACE_CENTERED) centered = true;
	
	/*--- Send-Receive boundary conditions ---*/
	for (iMarker = 0; iMarker < config[iZone]->GetnMarker_All(); iMarker++) {
		
		if ((config[iZone]->GetMarker_All_Boundary(iMarker) == SEND_RECEIVE) && 
				(config[iZone]->GetMarker_All_SendRecv(iMarker) > 0)) {
			
			MarkerS = iMarker;				// Marker of the boundary to be sended
			MarkerR = iMarker+1;			// Marker of the boundary to be received
			
			send_to = config[iZone]->GetMarker_All_SendRecv(MarkerS)-1;
			receive_from = abs(config[iZone]->GetMarker_All_SendRecv(MarkerR))-1;
			
			nVertexS = geometry[iZone][iMGLevel]->nVertex[MarkerS];
			nVertexR = geometry[iZone][iMGLevel]->nVertex[MarkerR];
			
			nBufferS_VectorGrad	= nVertexS*nVar*nDim;		nBufferR_VectorGrad	= nVertexR*nVar*nDim;
			nBufferS_Vector			= nVertexS*nVar;				nBufferR_Vector			= nVertexR*nVar;
			nBufferS_Scalar			= nVertexS;							nBufferR_Scalar			= nVertexR;

			/*--- Allocate upwind variables (send and receive) ---*/
			if (upwind) {
				Buffer_Send_U = new double[nBufferS_Vector];
				Buffer_Receive_U = new double [nBufferR_Vector];
				if (iMGLevel == MESH_0) {
					Buffer_Send_UGrad = new double[nBufferS_VectorGrad];
					Buffer_Receive_UGrad = new double [nBufferR_VectorGrad];
					Buffer_Send_Limit = new double[nBufferS_Vector];
					Buffer_Receive_Limit = new double [nBufferR_Vector];
				}					
			}
			
			/*--- Allocate centered variables (send and receive) ---*/
			if (centered) {
				Buffer_Send_U = new double[nBufferS_Vector];
				Buffer_Receive_U = new double [nBufferR_Vector];
				Buffer_Send_Lambda = new double [nBufferS_Scalar];
				Buffer_Receive_Lambda = new double [nBufferR_Scalar];
				Buffer_Send_Neighbor = new unsigned short [nBufferS_Scalar];
				Buffer_Receive_Neighbor = new unsigned short [nBufferR_Scalar];	
				if (iMGLevel == MESH_0) {
					Buffer_Send_Undivided_Laplacian = new double [nBufferS_Vector];
					Buffer_Receive_Undivided_Laplacian = new double [nBufferR_Vector];
					Buffer_Send_Sensor = new double [nBufferS_Scalar];
					Buffer_Receive_Sensor = new double [nBufferR_Scalar];
				}
			}
			
			/*--- Allocate incompressible variables (send and receive) ---*/
			if (incompressible) {
				Buffer_Send_BetaInc2 = new double [nBufferS_Scalar];
				Buffer_Receive_BetaInc2 = new double [nBufferR_Scalar];
				Buffer_Send_DensityInc = new double [nBufferS_Scalar];
				Buffer_Receive_DensityInc = new double [nBufferR_Scalar];
			}
			
			/*--- Allocate viscous variables (send and receive) ---*/
			if (viscous) {
				Buffer_Send_LaminarViscosity = new double[nBufferS_Scalar];
				Buffer_Receive_LaminarViscosity = new double [nBufferR_Scalar];
				Buffer_Send_EddyViscosity = new double[nBufferS_Scalar];
				Buffer_Receive_EddyViscosity = new double [nBufferR_Scalar];
				Buffer_Send_VGrad = new double[nBufferS_VectorGrad];
				Buffer_Receive_VGrad = new double [nBufferR_VectorGrad];
			}
			
#ifndef NO_MPI

			/*--- Copy the solution that should be sended ---*/
			for (iVertex = 0; iVertex < nVertexS; iVertex++) {
				
				iPoint = geometry[iZone][iMGLevel]->vertex[MarkerS][iVertex]->GetNode();
				
				/*--- Copy upwind data ---*/
				if (upwind) {
					for (iVar = 0; iVar < nVar; iVar++) {
						Buffer_Send_U[iVar*nVertexS+iVertex] = node[iPoint]->GetSolution(iVar);
						if (iMGLevel == MESH_0) {
							Buffer_Send_Limit[iVar*nVertexS+iVertex] = node[iPoint]->GetLimiter(iVar);
							for (iDim = 0; iDim < nDim; iDim++)
								Buffer_Send_UGrad[iDim*nVar*nVertexS+iVar*nVertexS+iVertex] = node[iPoint]->GetGradient(iVar,iDim);
						}
					}
				}
				
				/*--- Copy centered data ---*/
				if (centered) {
					for (iVar = 0; iVar < nVar; iVar++) {
						Buffer_Send_U[iVar*nVertexS+iVertex] = node[iPoint]->GetSolution(iVar);
						if (iMGLevel == MESH_0)
							Buffer_Send_Undivided_Laplacian[iVar*nVertexS+iVertex] = node[iPoint]->GetUnd_Lapl(iVar);							
					}
					if (iMGLevel == MESH_0) Buffer_Send_Sensor[iVertex] = node[iPoint]->GetSensor();
					Buffer_Send_Lambda[iVertex] = node[iPoint]->GetLambda();
					Buffer_Send_Neighbor[iVertex] = geometry[iZone][iMGLevel]->node[iPoint]->GetnPoint();
				}
				
				/*--- Copy incompressible data ---*/
				if (incompressible) {
					Buffer_Send_BetaInc2[iVertex] = node[iPoint]->GetBetaInc2();
					Buffer_Send_DensityInc[iVertex] = node[iPoint]->GetDensityInc();
				}
				
				/*--- Copy viscous data ---*/
				if (viscous) {
					Buffer_Send_LaminarViscosity[iVertex] = node[iPoint]->GetLaminarViscosity();
					Buffer_Send_EddyViscosity[iVertex] = node[iPoint]->GetEddyViscosity();
					for (iVar = 0; iVar < nVar; iVar++)
						for (iDim = 0; iDim < nDim; iDim++)
							Buffer_Send_VGrad[iDim*nVar*nVertexS+iVar*nVertexS+iVertex] = node[iPoint]->GetGradient_Primitive(iVar,iDim);							
				}
			}
			
			
			/*--- Send the buffer, and deallocate information using MPI ---*/
			if (upwind) {
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_U, nBufferS_Vector, MPI::DOUBLE, send_to, 0,
																 Buffer_Receive_U, nBufferR_Vector, MPI::DOUBLE, receive_from, 0); delete [] Buffer_Send_U;
				if (iMGLevel == MESH_0) {
					MPI::COMM_WORLD.Sendrecv(Buffer_Send_UGrad, nBufferS_VectorGrad, MPI::DOUBLE, send_to, 1,
																	 Buffer_Receive_UGrad, nBufferR_VectorGrad, MPI::DOUBLE, receive_from, 1); delete [] Buffer_Send_UGrad;
					MPI::COMM_WORLD.Sendrecv(Buffer_Send_Limit, nBufferS_Vector, MPI::DOUBLE, send_to, 2,
																	 Buffer_Receive_Limit, nBufferR_Vector, MPI::DOUBLE, receive_from, 2); delete [] Buffer_Send_Limit;						
				}
			}
			
			if (centered) {
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_U, nBufferS_Vector, MPI::DOUBLE, send_to, 0,
																 Buffer_Receive_U, nBufferR_Vector, MPI::DOUBLE, receive_from, 0); delete [] Buffer_Send_U;
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_Lambda, nBufferS_Scalar, MPI::DOUBLE, send_to, 1,
																 Buffer_Receive_Lambda, nBufferR_Scalar, MPI::DOUBLE, receive_from, 1); delete [] Buffer_Send_Lambda;
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_Neighbor, nBufferS_Scalar, MPI::UNSIGNED_SHORT, send_to, 2,
																 Buffer_Receive_Neighbor, nBufferR_Scalar, MPI::UNSIGNED_SHORT, receive_from, 2); delete [] Buffer_Send_Neighbor;
				if (iMGLevel == MESH_0) {
					MPI::COMM_WORLD.Sendrecv(Buffer_Send_Undivided_Laplacian, nBufferS_Vector, MPI::DOUBLE, send_to, 3,
																	 Buffer_Receive_Undivided_Laplacian, nBufferR_Vector, MPI::DOUBLE, receive_from, 3); delete [] Buffer_Send_Undivided_Laplacian;
					MPI::COMM_WORLD.Sendrecv(Buffer_Send_Sensor, nBufferS_Scalar, MPI::DOUBLE, send_to, 4,
																	 Buffer_Receive_Sensor, nBufferR_Scalar, MPI::DOUBLE, receive_from, 4); delete [] Buffer_Send_Sensor;
				}
			}
			
			if (incompressible) {
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_BetaInc2, nBufferS_Scalar, MPI::DOUBLE, send_to, 5,
																 Buffer_Receive_BetaInc2, nBufferR_Scalar, MPI::DOUBLE, receive_from, 5); delete [] Buffer_Send_BetaInc2;
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_DensityInc, nBufferS_Scalar, MPI::DOUBLE, send_to, 6,
																 Buffer_Receive_DensityInc, nBufferR_Scalar, MPI::DOUBLE, receive_from, 6); delete [] Buffer_Send_DensityInc;
			}
			
			if (viscous) {
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_LaminarViscosity, nBufferS_Scalar, MPI::DOUBLE, send_to, 7,
																 Buffer_Receive_LaminarViscosity, nBufferR_Scalar, MPI::DOUBLE, receive_from, 7); delete [] Buffer_Send_LaminarViscosity;
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_EddyViscosity, nBufferS_Scalar, MPI::DOUBLE, send_to, 8,
																 Buffer_Receive_EddyViscosity, nBufferR_Scalar, MPI::DOUBLE, receive_from, 8); delete [] Buffer_Send_VGrad;
				MPI::COMM_WORLD.Sendrecv(Buffer_Send_VGrad, nBufferS_VectorGrad, MPI::DOUBLE, send_to, 9,
																 Buffer_Receive_VGrad, nBufferR_VectorGrad, MPI::DOUBLE, receive_from, 9); delete [] Buffer_Send_EddyViscosity;
			}
			
			
#else

			/*--- Allow for periodic boundaries to use SEND_RECEIVE in serial.
			 Serial computations will only use the BC in receive mode, as
			 the proc will always be sending information to itself. ---*/
			
			/*--- Retrieve the donor boundary marker ---*/
			donor_marker = 1;
			donorZone    = 0;
			donorMarker  = 1;
			
			for (iMark = 0; iMark < config[iZone]->GetnMarker_All(); iMark++)
				if (config[iZone]->GetMarker_All_SendRecv(iMark)-1 == receive_from) donor_marker = iMark;
			
			/*--- Get the information from the donor point directly. This is a
			 serial computation with access to all nodes. Note that there is an
			 implicit ordering in the list. ---*/
			for (iVertex = 0; iVertex < nVertexR; iVertex++) {
				
				donorPoint = geometry[iZone][iMGLevel]->vertex[donor_marker][iVertex]->GetNode();
				donorZone = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetMatching_Zone();
				
				/*--- This is a multizone simulation and we need to use the
				 results of the search & interpolation routine in case of
				 sliding interfaces. ---*/
				if (donorZone != iZone) {
					
					donorElem = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetDonorElem();
					
					N_0 = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetBasisFunction(0);
					N_1 = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetBasisFunction(1);
					N_2 = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetBasisFunction(2);
					
					Point_0 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(0);
					Point_1 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(1);
					Point_2 = geometry[donorZone][iMGLevel]->elem[donorElem]->GetNode(2);
					
					/*--- Interpolation and storage of values from this element. ---*/
					if (upwind) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Receive_U[iVar*nVertexR+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSolution(iVar)
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSolution(iVar)
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSolution(iVar);
							/*--- Gradient and limiters... ---*/
							if (iMGLevel == MESH_0) {
								
							}
						}	
						
					}
					
					if (centered) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Receive_U[iVar*nVertexR+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSolution(iVar)
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSolution(iVar)
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSolution(iVar);
							if (iMGLevel == MESH_0)
								Buffer_Receive_Undivided_Laplacian[iVar*nVertexR+iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetUnd_Lapl(iVar)
								+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetUnd_Lapl(iVar)
								+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetUnd_Lapl(iVar);
						}
						if (iMGLevel == MESH_0)
							Buffer_Receive_Sensor[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetSensor()
							+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetSensor()
							+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetSensor();
						
						Buffer_Receive_Lambda[iVertex]  = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetLambda()
						+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetLambda()
						+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetLambda();
						
						/*--- Just use the neighbors from the first node for now... ---*/
						Buffer_Receive_Neighbor[iVertex] = geometry[donorZone][iMGLevel]->node[Point_0]->GetnPoint();
					}
					
					if (incompressible) {
						Buffer_Receive_BetaInc2[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetBetaInc2()
						+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetBetaInc2()
						+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetBetaInc2();
						Buffer_Receive_DensityInc[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetDensityInc()
						+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetDensityInc()
						+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetDensityInc();
					}
					
					if (viscous) {
						Buffer_Receive_LaminarViscosity[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetLaminarViscosity()
						+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetLaminarViscosity()
						+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetLaminarViscosity();
						Buffer_Receive_EddyViscosity[iVertex] = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetEddyViscosity()
						+ N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetEddyViscosity()
						+ N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetEddyViscosity();
						
						for (iVar = 0; iVar < nVar; iVar++) {
							for (iDim = 0; iDim < nDim; iDim++) {
								Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex]  = N_0*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_0]->GetGradient_Primitive(iVar, iDim);
								Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] += N_1*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_1]->GetGradient_Primitive(iVar, iDim);
								Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] += N_2*solution_container[donorZone][iMGLevel][FLOW_SOL]->node[Point_2]->GetGradient_Primitive(iVar, iDim);
							}
						}
						
					}						
				}
				
				else {
					
					/*--- For now, search for donor marker for every receive point. Probably
					 a more efficient way to do this in the future. ---*/
					for (iMark = 0; iMark < config[donorZone]->GetnMarker_All(); iMark++)
						if (config[donorZone]->GetMarker_All_SendRecv(iMark)-1 == receive_from) donorMarker = iMark;
					
					/*--- Index of the donor point. ---*/
					donorPoint = geometry[donorZone][iMGLevel]->vertex[donorMarker][iVertex]->GetNode();
					
					/*--- Copy upwind data ---*/
					if (upwind) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Receive_U[iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetSolution(iVar);
							if (iMGLevel == MESH_0) {
								Buffer_Receive_Limit[iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLimiter(iVar);
								for (iDim = 0; iDim < nDim; iDim++)
									Buffer_Receive_UGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetGradient(iVar,iDim);
							}
						}
					}
					
					/*--- Copy centered data ---*/
					if (centered) {
						for (iVar = 0; iVar < nVar; iVar++) {
							Buffer_Receive_U[iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetSolution(iVar);
							if (iMGLevel == MESH_0)
								Buffer_Receive_Undivided_Laplacian[iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetUnd_Lapl()[iVar];							
						}
						Buffer_Receive_Lambda[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLambda();
						Buffer_Receive_Neighbor[iVertex] = geometry[donorZone][iMGLevel]->node[donorPoint]->GetnPoint();
					}
					
					/*--- Copy incompressible data ---*/
					if (incompressible) {
						Buffer_Receive_BetaInc2[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetBetaInc2();
						Buffer_Receive_DensityInc[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetDensityInc();
					}
					
					/*--- Copy viscous data ---*/
					if (viscous) {
						Buffer_Receive_LaminarViscosity[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetLaminarViscosity();
						Buffer_Receive_EddyViscosity[iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetEddyViscosity();
						for (iVar = 0; iVar < nVar; iVar++)
							for (iDim = 0; iDim < nDim; iDim++)
								Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] = solution_container[donorZone][iMGLevel][FLOW_SOL]->node[donorPoint]->GetGradient_Primitive(iVar,iDim);
					}
				}
			}
			
#endif
			
			/*--- Do the coordinate transformation using the received information ---*/
			for (iVertex = 0; iVertex < nVertexR; iVertex++) {
				
				/*--- Find point and its type of transformation ---*/
				iPoint = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetNode();
				iPeriodic_Index = geometry[iZone][iMGLevel]->vertex[MarkerR][iVertex]->GetRotation_Type();
				
				/*--- Retrieve the supplied periodic information. ---*/
				angles = config[iZone]->GetPeriodicRotation(iPeriodic_Index);
				
				/*--- Store angles separately for clarity. ---*/
				theta    = angles[0];   phi    = angles[1]; psi    = angles[2];
				cosTheta = cos(theta);  cosPhi = cos(phi);  cosPsi = cos(psi);
				sinTheta = sin(theta);  sinPhi = sin(phi);  sinPsi = sin(psi);
				
				/*--- Compute the rotation matrix. Note that the implicit
				 ordering is rotation about the x-axis, y-axis,
				 then z-axis. Note that this is the transpose of the matrix
				 used during the preprocessing stage. ---*/
				rotMatrix[0][0] = cosPhi*cosPsi; rotMatrix[1][0] = sinTheta*sinPhi*cosPsi - cosTheta*sinPsi; rotMatrix[2][0] = cosTheta*sinPhi*cosPsi + sinTheta*sinPsi;
				rotMatrix[0][1] = cosPhi*sinPsi; rotMatrix[1][1] = sinTheta*sinPhi*sinPsi + cosTheta*cosPsi; rotMatrix[2][1] = cosTheta*sinPhi*sinPsi - sinTheta*cosPsi;
				rotMatrix[0][2] = -sinPhi; rotMatrix[1][2] = sinTheta*cosPhi; rotMatrix[2][2] = cosTheta*cosPhi;
				
				/*--- Copy conserved variables before performing transformation. ---*/
				for (iVar = 0; iVar < nVar; iVar++) 
					newSolution[iVar] = Buffer_Receive_U[iVar*nVertexR+iVertex];
				
				/*--- Rotate the momentum components. ---*/
				if (nDim == 2) {
					newSolution[1] = rotMatrix[0][0]*Buffer_Receive_U[1*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_U[2*nVertexR+iVertex];
					newSolution[2] = rotMatrix[1][0]*Buffer_Receive_U[1*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_U[2*nVertexR+iVertex];
				}
				else {
					newSolution[1] = rotMatrix[0][0]*Buffer_Receive_U[1*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_U[2*nVertexR+iVertex] + rotMatrix[0][2]*Buffer_Receive_U[3*nVertexR+iVertex];
					newSolution[2] = rotMatrix[1][0]*Buffer_Receive_U[1*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_U[2*nVertexR+iVertex] + rotMatrix[1][2]*Buffer_Receive_U[3*nVertexR+iVertex];
					newSolution[3] = rotMatrix[2][0]*Buffer_Receive_U[1*nVertexR+iVertex] + rotMatrix[2][1]*Buffer_Receive_U[2*nVertexR+iVertex] + rotMatrix[2][2]*Buffer_Receive_U[3*nVertexR+iVertex];
				}
				
				/*--- Copy transformed conserved variables back into buffer. ---*/
				for (iVar = 0; iVar < nVar; iVar++) 
					Buffer_Receive_U[iVar*nVertexR+iVertex] = newSolution[iVar];
				
				/*--- Also transform the gradient for upwinding if this is the fine mesh ---*/
				if ((upwind) && (iMGLevel == MESH_0)) {
					
					for (iVar = 0; iVar < nVar; iVar++) 
						for (iDim = 0; iDim < nDim; iDim++)
							newGradient[iVar][iDim] = Buffer_Receive_UGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex];
					
					/*--- Need to rotate the gradients for all conserved variables. ---*/
					for (iVar = 0; iVar < nVar; iVar++) {
						if (nDim == 2) {
							newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_UGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_UGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_UGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_UGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex];
						}
						else {
							newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_UGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_UGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][2]*Buffer_Receive_UGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_UGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_UGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][2]*Buffer_Receive_UGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][2] = rotMatrix[2][0]*Buffer_Receive_UGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[2][1]*Buffer_Receive_UGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[2][2]*Buffer_Receive_UGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
						}
					}
					
					/*--- Copy transformed gradients back into buffer. ---*/
					for (iVar = 0; iVar < nVar; iVar++) 
						for (iDim = 0; iDim < nDim; iDim++)
							Buffer_Receive_UGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] = newGradient[iVar][iDim];
					
				}
				
				/*--- Also transform the gradient for viscous terms ---*/
				if (viscous) {
					
					for (iVar = 0; iVar < nVar; iVar++)
						for (iDim = 0; iDim < nDim; iDim++)
							newGradient[iVar][iDim] = Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex];
					
					/*--- Need to rotate the gradients for all conserved variables. ---*/
					for (iVar = 0; iVar < nVar; iVar++) {
						if (nDim == 2) {
							newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_VGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_VGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_VGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_VGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex];
						}
						else {
							newGradient[iVar][0] = rotMatrix[0][0]*Buffer_Receive_VGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][1]*Buffer_Receive_VGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[0][2]*Buffer_Receive_VGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][1] = rotMatrix[1][0]*Buffer_Receive_VGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][1]*Buffer_Receive_VGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[1][2]*Buffer_Receive_VGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
							newGradient[iVar][2] = rotMatrix[2][0]*Buffer_Receive_VGrad[0*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[2][1]*Buffer_Receive_VGrad[1*nVar*nVertexR+iVar*nVertexR+iVertex] + rotMatrix[2][2]*Buffer_Receive_VGrad[2*nVar*nVertexR+iVar*nVertexR+iVertex];
						}
					}
					
					/*--- Copy transformed gradients back into buffer. ---*/
					for (iVar = 0; iVar < nVar; iVar++)
						for (iDim = 0; iDim < nDim; iDim++)
							Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex] = newGradient[iVar][iDim];
					
				}
				
				
				/*--- Upwind method. Store the received information ---*/
				if (upwind) {
					for (iVar = 0; iVar < nVar; iVar++) {
						node[iPoint]->SetSolution(iVar, Buffer_Receive_U[iVar*nVertexR+iVertex]);
						if (iMGLevel == MESH_0) {
							node[iPoint]->SetLimiter(iVar, Buffer_Receive_Limit[iVar*nVertexR+iVertex]);
							for (iDim = 0; iDim < nDim; iDim++)
								node[iPoint]->SetGradient(iVar, iDim, Buffer_Receive_UGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex]);
						}
					}
				}
				
				/*--- Centered method. Store the received information ---*/
				if (centered) {
					for (iVar = 0; iVar < nVar; iVar++) {
						node[iPoint]->SetSolution(iVar, Buffer_Receive_U[iVar*nVertexR+iVertex]);
						if (iMGLevel == MESH_0) 
							node[iPoint]->SetUndivided_Laplacian(iVar, Buffer_Receive_Undivided_Laplacian[iVar*nVertexR+iVertex]);
					}
					if (iMGLevel == MESH_0) node[iPoint]->SetSensor(Buffer_Receive_Sensor[iVertex]);
					node[iPoint]->SetLambda(Buffer_Receive_Lambda[iVertex]);
					geometry[iZone][iMGLevel]->node[iPoint]->SetnNeighbor(Buffer_Receive_Neighbor[iVertex]);
				}
				
				/*--- Incompressible method. Store the received information ---*/
				if (incompressible) {
					node[iPoint]->SetBetaInc2(Buffer_Receive_BetaInc2[iVertex]);
					node[iPoint]->SetDensityInc(Buffer_Receive_DensityInc[iVertex]);
				}
				
				/*--- Viscous method. Store the received information ---*/
				if (viscous) {
					node[iPoint]->SetLaminarViscosity(Buffer_Receive_LaminarViscosity[iVertex]);
					node[iPoint]->SetEddyViscosity(Buffer_Receive_EddyViscosity[iVertex]);
					for (iVar = 0; iVar < nVar; iVar++)
						for (iDim = 0; iDim < nDim; iDim++)
							node[iPoint]->SetGradient_Primitive(iVar, iDim, Buffer_Receive_VGrad[iDim*nVar*nVertexR+iVar*nVertexR+iVertex]);
				}
				
			}
			
			if (upwind) {
				delete [] Buffer_Receive_U;
				if (iMGLevel == MESH_0) {
					delete [] Buffer_Receive_UGrad;
					delete [] Buffer_Receive_Limit;
				}
			}
			
			if (centered) {
				delete [] Buffer_Receive_U;
				delete [] Buffer_Receive_Lambda;
				delete [] Buffer_Receive_Neighbor;
				if (iMGLevel == MESH_0) {
					delete [] Buffer_Receive_Undivided_Laplacian;
					delete [] Buffer_Receive_Sensor;
				}
			}
			
			if (incompressible) {
				delete [] Buffer_Receive_BetaInc2;
				delete [] Buffer_Receive_DensityInc;
			}
			
			if (viscous) {
				delete [] Buffer_Receive_LaminarViscosity;
				delete [] Buffer_Receive_EddyViscosity;
				delete [] Buffer_Receive_VGrad;
			}
			
		}
	}
	
	delete [] newSolution;
	for (iVar = 0; iVar < nVar; iVar++)
		delete [] newGradient[iVar];
	delete [] newGradient;
	
}

#endif

void CEulerSolution::SetResidual_DualTime(CGeometry *geometry, CSolution **solution_container, CConfig *config,
		unsigned short iRKStep, unsigned short iMesh, unsigned short RunTime_EqSystem) {
	unsigned short iVar, jVar;
	unsigned long iPoint;
	double *U_time_nM1, *U_time_n, *U_time_nP1;
	double Volume_nM1, Volume_n, Volume_nP1, TimeStep;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool FlowEq = (RunTime_EqSystem == RUNTIME_FLOW_SYS);
	bool AdjEq = (RunTime_EqSystem == RUNTIME_ADJFLOW_SYS);
	bool incompressible = config->GetIncompressible();
	bool Grid_Movement = config->GetGrid_Movement();

	/*--- loop over points ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {

		/*--- Solution at time n-1, n and n+1 ---*/
		U_time_nM1 = node[iPoint]->GetSolution_time_n1();
		U_time_n   = node[iPoint]->GetSolution_time_n();
		U_time_nP1 = node[iPoint]->GetSolution();

		/*--- Volume at time n-1 and n ---*/
		if (Grid_Movement) {
			Volume_nM1 = geometry->node[iPoint]->GetVolume_nM1();
			Volume_n = geometry->node[iPoint]->GetVolume_n();
			Volume_nP1 = geometry->node[iPoint]->GetVolume();
		}
		else {
			Volume_nM1 = geometry->node[iPoint]->GetVolume();
			Volume_n = geometry->node[iPoint]->GetVolume();
			Volume_nP1 = geometry->node[iPoint]->GetVolume();
		}

		/*--- Time Step ---*/
		TimeStep = config->GetDelta_UnstTimeND();

		/*--- Compute Residual ---*/
		for(iVar = 0; iVar < nVar; iVar++) {
			if (config->GetUnsteady_Simulation() == DT_STEPPING_1ST)
				Residual[iVar] = ( U_time_nP1[iVar]*Volume_nP1 - U_time_n[iVar]*Volume_n ) / TimeStep;
			if (config->GetUnsteady_Simulation() == DT_STEPPING_2ND)
				Residual[iVar] = ( 3.0*U_time_nP1[iVar]*Volume_nP1 - 4.0*U_time_n[iVar]*Volume_n
						+  1.0*U_time_nM1[iVar]*Volume_nM1 ) / (2.0*TimeStep);
		}

		if (incompressible && (FlowEq || AdjEq)) Residual[0] = 0.0;

		/*--- Add Residual ---*/
		node[iPoint]->AddRes_Conv(Residual);

		if (implicit) {
			for (iVar = 0; iVar < nVar; iVar++) {
				for (jVar = 0; jVar < nVar; jVar++)
					Jacobian_i[iVar][jVar] = 0.0;

				if (config->GetUnsteady_Simulation() == DT_STEPPING_1ST)
					Jacobian_i[iVar][iVar] = Volume_nP1 / TimeStep;
				if (config->GetUnsteady_Simulation() == DT_STEPPING_2ND)
					Jacobian_i[iVar][iVar] = (Volume_nP1*3.0)/(2.0*TimeStep);
			}
			if (incompressible && (FlowEq || AdjEq)) Jacobian_i[0][0] = 0.0;
			Jacobian.AddBlock(iPoint, iPoint, Jacobian_i);
		}
	}
}

void CEulerSolution::SetFlow_Displacement(CGeometry **flow_geometry, CVolumetricMovement *flow_grid_movement,
		CConfig *flow_config, CConfig *fea_config, CGeometry **fea_geometry, CSolution ***fea_solution) {
	unsigned short iMarker, iDim;
	unsigned long iVertex, iPoint;
	double *Coord, VarCoord[3];

#ifdef NO_MPI
	unsigned long iPoint_Donor;
	double *CoordDonor, *DisplacementDonor;

	for (iMarker = 0; iMarker < flow_config->GetnMarker_All(); iMarker++) {
		if (flow_config->GetMarker_All_Moving(iMarker) == YES) {
			for(iVertex = 0; iVertex < flow_geometry[MESH_0]->nVertex[iMarker]; iVertex++) {
				iPoint = flow_geometry[MESH_0]->vertex[iMarker][iVertex]->GetNode();
				iPoint_Donor = flow_geometry[MESH_0]->vertex[iMarker][iVertex]->GetDonorPoint();
				Coord = flow_geometry[MESH_0]->node[iPoint]->GetCoord();
				CoordDonor = fea_geometry[MESH_0]->node[iPoint_Donor]->GetCoord();
				DisplacementDonor = fea_solution[MESH_0][FEA_SOL]->node[iPoint_Donor]->GetSolution();

				for (iDim = 0; iDim < nDim; iDim++)
					VarCoord[iDim] = (CoordDonor[iDim]+DisplacementDonor[iDim])-Coord[iDim];

				flow_geometry[MESH_0]->vertex[iMarker][iVertex]->SetVarCoord(VarCoord);
			}
		}
	}
	flow_grid_movement->SpringMethod(flow_geometry[MESH_0], flow_config, true);

#else

	int rank = MPI::COMM_WORLD.Get_rank(), jProcessor;
	double *Buffer_Send_Coord = new double [nDim];
	double *Buffer_Receive_Coord = new double [nDim];
	unsigned long jPoint;

	/*--- Do the send process, by the moment we are sending each
	 node individually, this must be changed ---*/
	for (iMarker = 0; iMarker < fea_config->GetnMarker_All(); iMarker++) {
		if (fea_config->GetMarker_All_Boundary(iMarker) == LOAD_BOUNDARY) {
			for(iVertex = 0; iVertex < fea_geometry[MESH_0]->nVertex[iMarker]; iVertex++) {
				iPoint = fea_geometry[MESH_0]->vertex[iMarker][iVertex]->GetNode();

				if (fea_geometry[MESH_0]->node[iPoint]->GetDomain()) {

					/*--- Find the associate pair to the original node (index and processor) ---*/
					jPoint = fea_geometry[MESH_0]->vertex[iMarker][iVertex]->GetPeriodicPointDomain()[0];
					jProcessor = fea_geometry[MESH_0]->vertex[iMarker][iVertex]->GetPeriodicPointDomain()[1];

					/*--- We only send the pressure that belong to other boundary ---*/
					if (jProcessor != rank) {
						for (iDim = 0; iDim < nDim; iDim++)
							Buffer_Send_Coord[iDim] = fea_geometry[MESH_0]->node[iPoint]->GetCoord(iDim);

						MPI::COMM_WORLD.Bsend(Buffer_Send_Coord, nDim, MPI::DOUBLE, jProcessor, iPoint);
					}

				}
			}
		}
	}

	/*--- Now the loop is over the fea points ---*/
	for (iMarker = 0; iMarker < flow_config->GetnMarker_All(); iMarker++) {
		if ((flow_config->GetMarker_All_Boundary(iMarker) == EULER_WALL) &&
				(flow_config->GetMarker_All_Moving(iMarker) == YES)) {
			for(iVertex = 0; iVertex < flow_geometry[MESH_0]->nVertex[iMarker]; iVertex++) {
				iPoint = flow_geometry[MESH_0]->vertex[iMarker][iVertex]->GetNode();
				if (flow_geometry[MESH_0]->node[iPoint]->GetDomain()) {

					/*--- Find the associate pair to the original node ---*/
					jPoint = flow_geometry[MESH_0]->vertex[iMarker][iVertex]->GetPeriodicPointDomain()[0];
					jProcessor = flow_geometry[MESH_0]->vertex[iMarker][iVertex]->GetPeriodicPointDomain()[1];

					/*--- We only receive the information that belong to other boundary ---*/
					if (jProcessor != rank)
						MPI::COMM_WORLD.Recv(Buffer_Receive_Coord, nDim, MPI::DOUBLE, jProcessor, jPoint);
					else {
						for (iDim = 0; iDim < nDim; iDim++)
							Buffer_Send_Coord[iDim] = fea_geometry[MESH_0]->node[jPoint]->GetCoord(iDim);
					}

					/*--- Store the solution for both points ---*/
					Coord = flow_geometry[MESH_0]->node[iPoint]->GetCoord();

					for (iDim = 0; iDim < nDim; iDim++)
						VarCoord[iDim] = Buffer_Send_Coord[iDim]-Coord[iDim];

					flow_geometry[MESH_0]->vertex[iMarker][iVertex]->SetVarCoord(VarCoord);


				}
			}
		}
	}
	delete[] Buffer_Send_Coord;
	delete[] Buffer_Receive_Coord;

#endif

}

void CEulerSolution::GetRestart(CGeometry *geometry, CConfig *config, unsigned short val_iZone) {

	int rank = MASTER_NODE;
#ifndef NO_MPI
	rank = MPI::COMM_WORLD.Get_rank();
#endif

	/*--- Restart the solution from file information ---*/
	string restart_filename = config->GetSolution_FlowFileName();
	unsigned long iPoint, index, nFlowIter, adjIter, flowIter;
	char buffer[50];
	string UnstExt, text_line;
	ifstream restart_file;
	bool incompressible = config->GetIncompressible();
	bool grid_movement = config->GetGrid_Movement();
	unsigned short nZone = geometry->GetnZone();

	/*--- Multi-zone restart files. ---*/
	if (nZone > 1 && !(config->GetUnsteady_Simulation() == TIME_SPECTRAL)) {
		restart_filename.erase(restart_filename.end()-4, restart_filename.end());
		sprintf (buffer, "_%d.dat", int(val_iZone));
		UnstExt = string(buffer);
		restart_filename.append(UnstExt);
	}

	/*--- For the unsteady adjoint, we integrate backwards through
   physical time, so load in the direct solution files in reverse. ---*/
	if (config->GetUnsteady_Simulation() == TIME_SPECTRAL) {
		flowIter = val_iZone;
		restart_filename.erase(restart_filename.end()-4, restart_filename.end());
		if (int(val_iZone) < 10) sprintf (buffer, "_0000%d.dat", int(val_iZone));
		if ((int(val_iZone) >= 10) && (int(val_iZone) < 100)) sprintf (buffer, "_000%d.dat", int(val_iZone));
		if ((int(val_iZone) >= 100) && (int(val_iZone) < 1000)) sprintf (buffer, "_00%d.dat", int(val_iZone));
		if ((int(val_iZone) >= 1000) && (int(val_iZone) < 10000)) sprintf (buffer, "_0%d.dat", int(val_iZone));
		if (int(val_iZone) >= 10000) sprintf (buffer, "_%d.dat", int(val_iZone));
		UnstExt = string(buffer);
		restart_filename.append(UnstExt);
	} else if (config->GetUnsteady_Simulation() && config->GetWrt_Unsteady()) {
		nFlowIter = config->GetnExtIter() - 1;
		adjIter   = config->GetExtIter();
		flowIter  = nFlowIter - adjIter;
		restart_filename.erase (restart_filename.end()-4, restart_filename.end());
		if ((int(flowIter) >= 0) && (int(flowIter) < 10)) sprintf (buffer, "_0000%d.dat", int(flowIter));
		if ((int(flowIter) >= 10) && (int(flowIter) < 100)) sprintf (buffer, "_000%d.dat", int(flowIter));
		if ((int(flowIter) >= 100) && (int(flowIter) < 1000)) sprintf (buffer, "_00%d.dat", int(flowIter));
		if ((int(flowIter) >= 1000) && (int(flowIter) < 10000)) sprintf (buffer, "_0%d.dat", int(flowIter));
		if (int(flowIter) >= 10000) sprintf (buffer, "_%d.dat", int(flowIter));
		UnstExt = string(buffer);
		restart_filename.append(UnstExt);
	} else {
		flowIter = config->GetExtIter();
		restart_filename.erase (restart_filename.end()-4, restart_filename.end());
		if ((int(flowIter) >= 0) && (int(flowIter) < 10)) sprintf (buffer, "_0000%d.dat", int(flowIter));
		if ((int(flowIter) >= 10) && (int(flowIter) < 100)) sprintf (buffer, "_000%d.dat", int(flowIter));
		if ((int(flowIter) >= 100) && (int(flowIter) < 1000)) sprintf (buffer, "_00%d.dat", int(flowIter));
		if ((int(flowIter) >= 1000) && (int(flowIter) < 10000)) sprintf (buffer, "_0%d.dat", int(flowIter));
		if (int(flowIter) >= 10000) sprintf (buffer, "_%d.dat", int(flowIter));
		UnstExt = string(buffer);
		restart_filename.append(UnstExt);
	}

	/*--- Open the flow solution from the restart file ---*/
	if (rank == MASTER_NODE && val_iZone == ZONE_0)
		cout << "Reading in the direct flow solution from iteration " << flowIter << "." << endl;
	restart_file.open(restart_filename.data(), ios::in);

	/*--- In case there is no file ---*/
	if (restart_file.fail()) {
		cout << "There is no flow restart file!!" << endl;
		cout << "Press any key to exit..." << endl;
		cin.get(); exit(1);
	}

	/*--- Store the previous solution (needed for aeroacoustic adjoint) ---*/
	if (config->GetExtIter() > 0)
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++)
			node[iPoint]->Set_Solution_time_n();

	/*--- In case this is a parallel simulation, we need to perform the
   Global2Local index transformation first. ---*/
	long *Global2Local = NULL;
	Global2Local = new long[geometry->GetGlobal_nPointDomain()];
	/*--- First, set all indices to a negative value by default ---*/
	for(iPoint = 0; iPoint < geometry->GetGlobal_nPointDomain(); iPoint++) {
		Global2Local[iPoint] = -1;
	}

	/*--- Now fill array with the transform values only for local points ---*/
	for(iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Global2Local[geometry->node[iPoint]->GetGlobalIndex()] = iPoint;
	}

	/*--- Read all lines in the restart file ---*/
	long iPoint_Local = 0; unsigned long iPoint_Global = 0;
	while (getline (restart_file,text_line)) {
		istringstream point_line(text_line);

		/*--- Retrieve local index. If this node from the restart file lives
     on a different processor, the value of iPoint_Local will be -1, as 
     initialized above. Otherwise, the local index for this node on the 
     current processor will be returned and used to instantiate the vars. ---*/
		iPoint_Local = Global2Local[iPoint_Global];
		if (iPoint_Local >= 0) {

			if (incompressible) {
				if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2];
				if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
			}
			else {
				if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
				if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3] >> Solution[4];
			}

			node[iPoint_Local]->SetSolution(Solution);

			/*--- If necessary, read in the grid velocities for the unsteady adjoint ---*/
			if (config->GetUnsteady_Simulation() && config->GetWrt_Unsteady() && grid_movement) {
				double Volume, GridVel[3];
				if (nDim == 2) point_line >> Volume >> GridVel[0] >> GridVel[1];
				if (nDim == 3) point_line >> Volume >> GridVel[0] >> GridVel[1] >> GridVel[2];
				if (iPoint_Local >= 0)
					for (unsigned short iDim = 0; iDim < geometry->GetnDim(); iDim++)
						geometry->node[iPoint_Local]->SetGridVel(iDim, GridVel[iDim]);
			}

		}
		iPoint_Global++;
	}

	/*--- Set an average grid velocity at any halo nodes for the unsteady adjoint ---*/
	if (config->GetUnsteady_Simulation() && config->GetWrt_Unsteady() && grid_movement) {
		unsigned long jPoint;
		unsigned short nNeighbors;
		double AvgVel[3], *GridVel;
		for(iPoint = geometry->GetnPointDomain(); iPoint < geometry->GetnPoint(); iPoint++) {
			AvgVel[0] = 0.0; AvgVel[1] = 0.0; AvgVel[2] = 0.0; nNeighbors = 0;
			/*--- Find & store any neighbor points to the sliding boundary in the donor zone (jZone). ---*/
			for (unsigned short iNeighbor = 0; iNeighbor < geometry->node[iPoint]->GetnPoint(); iNeighbor++) {
				jPoint = geometry->node[iPoint]->GetPoint(iNeighbor);
				if (geometry->node[jPoint]->GetDomain()) {
					GridVel = geometry->node[jPoint]->GetGridVel();
					for (unsigned short iDim = 0; iDim < geometry->GetnDim(); iDim++) {
						AvgVel[iDim] += GridVel[iDim];
						nNeighbors++;
					}
				}
			}
			for (unsigned short iDim = 0; iDim < geometry->GetnDim(); iDim++)
				geometry->node[iPoint]->SetGridVel(iDim, AvgVel[iDim]/(double)nNeighbors);
		}
	}

	/*--- Close the restart file ---*/
	restart_file.close();

	/*--- Free memory needed for the transformation ---*/
	delete [] Global2Local;


}

CNSSolution::CNSSolution(void) : CEulerSolution() { }

CNSSolution::CNSSolution(CGeometry *geometry, CConfig *config, unsigned short iMesh) : CEulerSolution() {
	unsigned long iPoint, index;
	unsigned short iVar, iDim, iMarker;
	unsigned short nZone = geometry->GetnZone();
	bool restart = (config->GetRestart() || config->GetRestart_Flow());
	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();

	int rank = MASTER_NODE;
#ifndef NO_MPI
	rank = MPI::COMM_WORLD.Get_rank();
#endif

	/*--- Set the gamma value ---*/
	Gamma = config->GetGamma();
	Gamma_Minus_One = Gamma - 1.0;

	/*--- Define geometry constants in the solver structure ---*/
	nDim = geometry->GetnDim();
	if (incompressible) nVar = nDim + 1;
	else nVar = nDim + 2;
	nMarker = config->GetnMarker_All();
	nPoint = geometry->GetnPoint();

	/*--- Allocate the node variables ---*/
	node = new CVariable*[geometry->GetnPoint()];

	/*--- Define some auxiliar vector related with the residual ---*/
	Residual = new double[nVar]; Residual_Max = new double[nVar];
	Residual_i = new double[nVar]; Residual_j = new double[nVar];
	Res_Conv = new double[nVar]; Res_Visc = new double[nVar];
	Res_Sour = new double[nVar];

	/*--- Define some auxiliar vector related with the solution ---*/
	Solution = new double[nVar];
	Solution_i = new double[nVar]; Solution_j = new double[nVar];

	/*--- Define some auxiliar vector related with the primitive variables ---*/
	PrimVar = new double[nVar];
	PrimVar_i = new double[nVar]; PrimVar_j = new double[nVar];

	/*--- Define some auxiliar vector related with the geometry ---*/
	Vector = new double[nDim];
	Vector_i = new double[nDim]; Vector_j = new double[nDim];

	/*--- Define some auxiliar vector related with the undivided lapalacian computation ---*/
	if ((config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) ||
			(config->GetKind_ConvNumScheme_AdjFlow() == SPACE_CENTERED)) {
		p1_Und_Lapl = new double [geometry->GetnPoint()];
		p2_Und_Lapl = new double [geometry->GetnPoint()];
	}

	if ((config->GetKind_Upwind() == ROE_TURKEL_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_1ST)) {
		Precon_Mat_inv = new double* [nVar];
		for (iVar = 0; iVar < nVar; iVar ++)
			Precon_Mat_inv[iVar] = new double[nVar];
	}


	/*--- Jacobians and vector structures for implicit computations ---*/
	if (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) {

		/*--- Point to point Jacobians ---*/
		Jacobian_i = new double* [nVar];
		Jacobian_j = new double* [nVar];
		for (iVar = 0; iVar < nVar; iVar++) {
			Jacobian_i[iVar] = new double [nVar];
			Jacobian_j[iVar] = new double [nVar];
		}
		/*--- Initialization of the structure of the whole Jacobian ---*/
		if (rank == MASTER_NODE) cout << "Initialize jacobian structure (Navier-Stokes' equations). MG level: " << iMesh <<"." << endl;
		Initialize_Jacobian_Structure(geometry, config);
		xsol = new double [geometry->GetnPoint()*nVar];
		rhs  = new double [geometry->GetnPoint()*nVar];
	}

	/*--- Computation of gradients by least squares ---*/
	if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) {

		/*--- S matrix := inv(R)*traspose(inv(R)) ---*/
		Smatrix = new double* [nDim];
		for (iDim = 0; iDim < nDim; iDim++)
			Smatrix[iDim] = new double [nDim];

		/*--- c vector := transpose(WA)*(Wb) ---*/
		cvector = new double* [nDim+3];
		for (iVar = 0; iVar < nDim+3; iVar++)
			cvector[iVar] = new double [nDim];
	}

	/*--- Inviscid forces definition and coefficient in all the markers ---*/
	CPressure = new double* [config->GetnMarker_All()];
	for (iMarker=0; iMarker<config->GetnMarker_All(); iMarker++)
		CPressure[iMarker] = new double [geometry->nVertex[iMarker]];

	/*--- Forces definition and coefficient in all the markers ---*/
	CHeatTransfer = new double* [config->GetnMarker_All()];
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++)
		CHeatTransfer[iMarker] = new double [geometry->nVertex[iMarker]];

	/*--- Viscous forces definition and coefficient in all the markers ---*/
	CSkinFriction = new double* [config->GetnMarker_All()];
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++)
		CSkinFriction[iMarker] = new double [geometry->nVertex[iMarker]];

	/*--- Non dimensional coefficients ---*/
	ForceInviscid = new double[nDim];
	MomentInviscid = new double[3];
	CDrag_Inv = new double[config->GetnMarker_All()];
	CLift_Inv = new double[config->GetnMarker_All()];
	CSideForce_Inv = new double[config->GetnMarker_All()];
	CMx_Inv = new double[config->GetnMarker_All()];
	CMy_Inv = new double[config->GetnMarker_All()];
	CMz_Inv = new double[config->GetnMarker_All()];
	CEff_Inv = new double[config->GetnMarker_All()];
	CFx_Inv = new double[config->GetnMarker_All()];
	CFy_Inv = new double[config->GetnMarker_All()];
	CFz_Inv = new double[config->GetnMarker_All()];

	/*--- Rotational coefficients ---*/
	CMerit_Inv = new double[config->GetnMarker_All()];
	CT_Inv = new double[config->GetnMarker_All()];
	CQ_Inv = new double[config->GetnMarker_All()];

	/*--- Supersonic coefficients ---*/
	CEquivArea_Inv = new double[config->GetnMarker_All()];
	CNearFieldOF_Inv = new double[config->GetnMarker_All()];

	/*--- Nacelle simulation ---*/
	MassFlow_Rate = new double[config->GetnMarker_All()];
	FanFace_Pressure = new double[config->GetnMarker_All()];
	FanFace_Mach = new double[config->GetnMarker_All()];

	/*--- Init total coefficients ---*/
	Total_CDrag = 0.0;	Total_CLift = 0.0;			Total_CSideForce = 0.0;
	Total_CMx = 0.0;		Total_CMy = 0.0;				Total_CMz = 0.0;
	Total_CEff = 0.0;		Total_CEquivArea = 0.0; Total_CNearFieldOF = 0.0;
	Total_CFx = 0.0;		Total_CFy = 0.0;				Total_CFz = 0.0; Total_CMerit = 0.0;
	Total_CT = 0.0;			Total_CQ = 0.0;

	ForceViscous = new double[nDim];
	MomentViscous = new double[nDim];
	CDrag_Visc = new double[config->GetnMarker_All()];
	CLift_Visc = new double[config->GetnMarker_All()];
	CMx_Visc = new double[config->GetnMarker_All()];
	CMy_Visc = new double[config->GetnMarker_All()];
	CMz_Visc = new double[config->GetnMarker_All()];
	CEff_Visc = new double[config->GetnMarker_All()];
	CFx_Visc = new double[config->GetnMarker_All()];
	CFy_Visc = new double[config->GetnMarker_All()];
	CFz_Visc = new double[config->GetnMarker_All()];
	CMerit_Visc = new double[config->GetnMarker_All()];
	CT_Visc = new double[config->GetnMarker_All()];
	CQ_Visc = new double[config->GetnMarker_All()];

	/*--- Read farfield conditions from config ---*/
	Density_Inf   = config->GetDensity_FreeStreamND();
	Pressure_Inf  = config->GetPressure_FreeStreamND();
	Velocity_Inf  = config->GetVelocity_FreeStreamND();
	Energy_Inf    = config->GetEnergy_FreeStreamND();
	Viscosity_Inf = config->GetViscosity_FreeStreamND();
	Mach_Inf      = config->GetMach_FreeStreamND();
	Prandtl_Lam   = config->GetPrandtl_Lam();
	Prandtl_Turb  = config->GetPrandtl_Turb();

	/*--- Initializate Fan Face Pressure ---*/
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
		MassFlow_Rate[iMarker] = 0.0;
		FanFace_Pressure[iMarker] = Pressure_Inf;
		FanFace_Mach[iMarker] = Mach_Inf;
	}

	/*--- Inlet/Outlet boundary conditions, using infinity values ---*/
	Density_Inlet = Density_Inf;		Density_Outlet = Density_Inf;
	Pressure_Inlet = Pressure_Inf;	Pressure_Outlet = Pressure_Inf;
	Energy_Inlet = Energy_Inf;			Energy_Outlet = Energy_Inf;
	Mach_Inlet = Mach_Inf;					Mach_Outlet = Mach_Inf;
	Velocity_Inlet  = new double [nDim]; Velocity_Outlet = new double [nDim];
	for (iDim = 0; iDim < nDim; iDim++) {
		Velocity_Inlet[iDim] = Velocity_Inf[iDim];
		Velocity_Outlet[iDim] = Velocity_Inf[iDim];
	}

	/*--- For a rotating frame, set the velocity due to rotation
	 at each point just once, and store it in the geometry class. ---*/
	if (rotating_frame) {
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++) {
			double RotVel[3], distance[3];
			double *coord = geometry->node[iPoint]->GetCoord();
			double *axis  = config->GetRotAxisOrigin();
			double *omega = config->GetOmega_FreeStreamND();
			double Lref   = config->GetLength_Ref();

			/*--- Calculate non-dim distance fron rotation center ---*/
			distance[0] = (coord[0]-axis[0])/Lref;
			distance[1] = (coord[1]-axis[1])/Lref;
			distance[2] = (coord[2]-axis[2])/Lref;

			/*--- Calculate the angular velocity as omega X r ---*/
			RotVel[0] = omega[1]*(distance[2]) - omega[2]*(distance[1]);
			RotVel[1] = omega[2]*(distance[0]) - omega[0]*(distance[2]);
			RotVel[2] = omega[0]*(distance[1]) - omega[1]*(distance[0]);

			geometry->node[iPoint]->SetRotVel(RotVel);
		}
	}

	/*--- Restart the solution from file information ---*/
	if (!restart || geometry->GetFinestMGLevel() == false || nZone > 1) {

		/*--- Restart the solution from infinity ---*/
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++)
			node[iPoint] = new CNSVariable(Density_Inf, Velocity_Inf, Energy_Inf, nDim, nVar, config);
	}

	else {

		/*--- Restart the solution from file information ---*/
		ifstream restart_file;
		string filename = config->GetSolution_FlowFileName();

		/*--- Append time step for unsteady restart ---*/
		if (config->GetUnsteady_Simulation() && config->GetWrt_Unsteady()) {
			char buffer[50];
			unsigned long flowIter = config->GetnExtIter() - 1;
			filename.erase (filename.end()-4, filename.end());
			if ((int(flowIter) >= 0) && (int(flowIter) < 10)) sprintf (buffer, "_0000%d.dat", int(flowIter));
			if ((int(flowIter) >= 10) && (int(flowIter) < 100)) sprintf (buffer, "_000%d.dat", int(flowIter));
			if ((int(flowIter) >= 100) && (int(flowIter) < 1000)) sprintf (buffer, "_00%d.dat", int(flowIter));
			if ((int(flowIter) >= 1000) && (int(flowIter) < 10000)) sprintf (buffer, "_0%d.dat", int(flowIter));
			if (int(flowIter) >= 10000) sprintf (buffer, "_%d.dat", int(flowIter));
			string UnstExt = string(buffer);
			filename.append(UnstExt);
		}
		restart_file.open(filename.data(), ios::in);

		/*--- In case there is no file ---*/
		if (restart_file.fail()) {
			cout << "There is no flow restart file!!" << endl;
			cout << "Press any key to exit..." << endl;
			cin.get(); exit(1);
		}

		/*--- In case this is a parallel simulation, we need to perform the
     Global2Local index transformation first. ---*/
		long *Global2Local = new long[geometry->GetGlobal_nPointDomain()];

		/*--- First, set all indices to a negative value by default ---*/
		for(iPoint = 0; iPoint < geometry->GetGlobal_nPointDomain(); iPoint++)
			Global2Local[iPoint] = -1;

		/*--- Now fill array with the transform values only for local points ---*/
		for(iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
			Global2Local[geometry->node[iPoint]->GetGlobalIndex()] = iPoint;

		/*--- Read all lines in the restart file ---*/
		long iPoint_Local; unsigned long iPoint_Global = 0; string text_line;
		while (getline (restart_file,text_line)) {
			istringstream point_line(text_line);

			/*--- Retrieve local index. If this node from the restart file lives
       on a different processor, the value of iPoint_Local will be -1. 
       Otherwise, the local index for this node on the current processor 
       will be returned and used to instantiate the vars. ---*/
			iPoint_Local = Global2Local[iPoint_Global];
			if (iPoint_Local >= 0) {
				if (incompressible) {
					if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2];
					if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
				}
				else {
					if (nDim == 2) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3];
					if (nDim == 3) point_line >> index >> Solution[0] >> Solution[1] >> Solution[2] >> Solution[3] >> Solution[4];
				}
				node[iPoint_Local] = new CNSVariable(Solution, nDim, nVar, config);
			}
			iPoint_Global++;
		}

		/*--- Instantiate the variable class with an arbitrary solution
     at any halo/periodic nodes. The initial solution can be arbitrary,
     because a send/recv is performed immediately in the solver. ---*/
		for(iPoint = geometry->GetnPointDomain(); iPoint < geometry->GetnPoint(); iPoint++)
			node[iPoint] = new CNSVariable(Solution, nDim, nVar, config);

		/*--- Close the restart file ---*/
		restart_file.close();

		/*--- Free memory needed for the transformation ---*/
		delete [] Global2Local;
	}

	/*--- For incompressible solver set the initial values for the density and viscosity,
	 unless a freesurface problem, this must be constant during the computation ---*/
	if (incompressible) {
		for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint++) {
			node[iPoint]->SetDensityInc(Density_Inf);
			node[iPoint]->SetLaminarViscosityInc(Viscosity_Inf);
		}
	}

	/*--- Define solver parameters needed for execution of destructor ---*/
	if (config->GetKind_ConvNumScheme_Flow() == SPACE_CENTERED ||
			(config->GetKind_ConvNumScheme_AdjFlow() == SPACE_CENTERED))
		space_centered = true;
	else space_centered = false;

	if ((config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT) ||
			(config->GetKind_TimeIntScheme_AdjFlow() == EULER_IMPLICIT)) euler_implicit = true;
	else euler_implicit = false;

	if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) least_squares = true;
	else least_squares = false;

	if ((config->GetKind_Upwind() == ROE_TURKEL_2ND) || (config->GetKind_Upwind() == ROE_TURKEL_1ST)) roe_turkel = true;
	else roe_turkel = false;
}

CNSSolution::~CNSSolution(void) {
	unsigned short iVar, iDim, iMarker;
	unsigned long iPoint;

	for (iPoint = 0; iPoint < nPoint; iPoint++)
		delete node[iPoint];
	delete [] node;

	delete [] Residual;   delete [] Residual_Max; delete [] Res_Sour;
	delete [] Residual_i; delete [] Residual_j;   delete [] Solution_j;
	delete [] Res_Conv;   delete [] Res_Visc;     delete [] Vector_i;
	delete [] Solution;   delete [] Solution_i;   delete [] Vector_j;
	delete [] Vector;			delete [] PrimVar;			delete [] PrimVar_i;
	delete [] PrimVar_j;

	if (space_centered) {
		delete [] p1_Und_Lapl;	delete [] p2_Und_Lapl;
	}

	if (euler_implicit){
		for (iVar = 0; iVar < nVar; iVar++) {
			delete [] Jacobian_i[iVar];	delete [] Jacobian_j[iVar];
		}
		delete [] Jacobian_i; delete [] Jacobian_j;
		delete [] xsol; delete [] rhs;
	}

	if (least_squares){
		for (iDim = 0; iDim < nDim; iDim++)
			delete [] Smatrix[iDim];
		delete [] Smatrix;

		for (iVar = 0; iVar < nVar+1; iVar++)
			delete [] cvector[iVar];
		delete [] cvector;
	}

	if (roe_turkel) {
		for (iVar = 0; iVar < nVar; iVar ++)
			delete [] Precon_Mat_inv[iVar];
		delete [] Precon_Mat_inv;
	}

	for (iMarker = 0; iMarker < nMarker; iMarker++) {
		delete [] CPressure[iMarker];
		delete [] CHeatTransfer[iMarker];
		delete [] CSkinFriction[iMarker];
	}
	delete [] CPressure;
	delete [] CHeatTransfer;
	delete [] CSkinFriction;

	delete [] ForceInviscid;	delete [] MomentInviscid;   delete [] FanFace_Mach;
	delete [] CDrag_Inv;			delete [] CLift_Inv;        delete [] CSideForce_Inv;
	delete [] CMx_Inv;				delete [] CMy_Inv;          delete [] CMz_Inv;
	delete [] CEff_Inv;				delete [] CEquivArea_Inv;   delete [] CNearFieldOF_Inv;
	delete [] CFx_Inv;				delete [] CFy_Inv;          delete [] CFz_Inv;
	delete [] CMerit_Inv;			delete [] CT_Inv;           delete [] CQ_Inv;
	delete [] MassFlow_Rate;	delete [] FanFace_Pressure; delete [] CLift_Visc;
	delete [] ForceViscous;		delete [] MomentViscous;		delete [] CDrag_Visc;
	delete [] CMx_Visc;				delete [] CMy_Visc;					delete [] CMz_Visc;
	delete [] CFx_Visc;				delete [] CFy_Visc;					delete [] CFz_Visc;
	delete [] CEff_Visc;			delete [] CMerit_Visc;			delete [] CT_Visc;
	delete [] CQ_Visc;

	delete [] Velocity_Inlet;		delete [] Velocity_Outlet;

}

void CNSSolution::Preprocessing(CGeometry *geometry, CSolution **solution_container, CNumerics **solver, CConfig *config, unsigned short iRKStep) {
	unsigned long iPoint;
	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool freesurface = ((config->GetKind_Solver() == FREE_SURFACE_NAVIER_STOKES) || (config->GetKind_Solver() == ADJ_FREE_SURFACE_NAVIER_STOKES));
	bool incompressible = config->GetIncompressible();
	double Gas_Constant = config->GetGas_Constant();
	double ArtComp_Factor = config->GetArtComp_Factor();
	unsigned short turb_model = config->GetKind_Turb_Model();
	bool tkeNeeded = (turb_model == SST);
	double turb_ke = 0.0;

	for (iPoint = 0; iPoint < geometry->GetnPoint(); iPoint ++) {

		if (tkeNeeded) turb_ke = solution_container[TURB_SOL]->node[iPoint]->GetSolution(0);

		/*--- Set the primitive variables incompressible (dens, vx, vy, vz, beta)---*/
		/*--- and compressible (temp, vx, vy, vz, press, dens, enthal, sos)---*/
		if (incompressible)
			node[iPoint]->SetPrimVar_Incompressible(Density_Inf, Viscosity_Inf, ArtComp_Factor, turb_ke, freesurface);
		else
			node[iPoint]->SetPrimVar_Compressible(Gamma, Gas_Constant, turb_ke);

		/*--- Set the value of the eddy viscosity ---*/
		if (turb_model != NONE)
			node[iPoint]->SetEddyViscosity(config->GetKind_Turb_Model(), solution_container[TURB_SOL]->node[iPoint]);
		else
			node[iPoint]->SetEddyViscosity(NONE, NULL);

		/*--- Initialize the convective, source and viscous residual vector ---*/
		node[iPoint]->Set_ResConv_Zero();
		node[iPoint]->Set_ResSour_Zero();
		if ((config->Get_Beta_RKStep(iRKStep) != 0) || implicit)
			node[iPoint]->Set_ResVisc_Zero();
	}

	/*--- Initialize the jacobian matrices ---*/
	if (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT)
		Jacobian.SetValZero();
}

void CNSSolution::SetTime_Step(CGeometry *geometry, CSolution **solution_container, CConfig *config, unsigned short iMesh, unsigned long Iteration) {
	double Mean_BetaInc2, *Normal, Area, Vol, Mean_SoundSpeed, Mean_ProjVel, Lambda, Local_Delta_Time, Local_Delta_Time_Visc,
	Global_Delta_Time = 1E6, Mean_LaminarVisc, Mean_EddyVisc, Mean_Density, Lambda_1, Lambda_2, K_v = 0.25, Global_Delta_UnstTimeND;
	unsigned long iEdge, iVertex, iPoint = 0, jPoint = 0;
	unsigned short iDim, iMarker;
	double ProjVel, ProjVel_i, ProjVel_j;

	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();
	bool centered = (config->GetKind_ConvNumScheme() == SPACE_CENTERED);
	bool grid_movement = config->GetGrid_Movement();
	double turb_model = config->GetKind_Turb_Model();

	Min_Delta_Time = 1.E6; Max_Delta_Time = 0.0;

	/*--- Set maximum inviscid eigenvalue to zero, and compute sound speed and viscosity ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		node[iPoint]->SetMax_Lambda_Inv(0.0);
		node[iPoint]->SetMax_Lambda_Visc(0.0);
		if (centered) node[iPoint]->SetLambda(0.0);

		/*--- Set the value of the eddy viscosity ---*/
		if (turb_model != NONE)
			node[iPoint]->SetEddyViscosity(config->GetKind_Turb_Model(), solution_container[TURB_SOL]->node[iPoint]);
		else
			node[iPoint]->SetEddyViscosity(NONE, NULL);
	}

	/*--- Loop interior edges ---*/
	for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

		/*--- Point identification, Normal vector and area ---*/
		iPoint = geometry->edge[iEdge]->GetNode(0);
		jPoint = geometry->edge[iEdge]->GetNode(1);
		Normal = geometry->edge[iEdge]->GetNormal();
		Area = 0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);

		/*--- Mean Values ---*/
		if (incompressible) {
			Mean_ProjVel = 0.5 * (node[iPoint]->GetProjVelInc(Normal) + node[jPoint]->GetProjVelInc(Normal));
			Mean_BetaInc2 = 0.5 * (node[iPoint]->GetBetaInc2() + node[jPoint]->GetBetaInc2());
			Mean_SoundSpeed = sqrt(Mean_ProjVel*Mean_ProjVel + Mean_BetaInc2*Area*Area);
		}
		else {
			Mean_ProjVel = 0.5 * (node[iPoint]->GetProjVel(Normal) + node[jPoint]->GetProjVel(Normal));
			Mean_SoundSpeed = 0.5 * (node[iPoint]->GetSoundSpeed() + node[jPoint]->GetSoundSpeed()) * Area;
		}

		/*--- Contribution due to a rotating frame ---*/
		if (rotating_frame) {
			double *RotVel_i = geometry->node[iPoint]->GetRotVel();
			double *RotVel_j = geometry->node[jPoint]->GetRotVel();
			ProjVel_i = 0.0; ProjVel_j =0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				ProjVel_i += RotVel_i[iDim]*Normal[iDim];
				ProjVel_j += RotVel_j[iDim]*Normal[iDim];
			}
			Mean_ProjVel -= 0.5 * (ProjVel_i + ProjVel_j) ;
		}

		/*--- Contribution due to grid movement ---*/
		if (grid_movement) {
			double *GridVel_i = geometry->node[iPoint]->GetGridVel();
			double *GridVel_j = geometry->node[jPoint]->GetGridVel();
			ProjVel_i = 0.0; ProjVel_j =0.0;
			for (iDim = 0; iDim < nDim; iDim++) {
				ProjVel_i += GridVel_i[iDim]*Normal[iDim];
				ProjVel_j += GridVel_j[iDim]*Normal[iDim];
			}
			Mean_ProjVel -= 0.5 * (ProjVel_i + ProjVel_j) ;
		}

		/*--- Inviscid contribution ---*/
		Lambda = fabs(Mean_ProjVel) + Mean_SoundSpeed ;
		if (geometry->node[iPoint]->GetDomain()) node[iPoint]->AddMax_Lambda_Inv(Lambda);
		if (geometry->node[jPoint]->GetDomain()) node[jPoint]->AddMax_Lambda_Inv(Lambda);
		if (centered) {
			if (geometry->node[iPoint]->GetDomain()) node[iPoint]->AddLambda(Lambda);
			if (geometry->node[jPoint]->GetDomain()) node[jPoint]->AddLambda(Lambda);
		}

		/*--- Viscous contribution ---*/
		if (incompressible) {
			Mean_LaminarVisc = 0.5*(node[iPoint]->GetLaminarViscosityInc() + node[jPoint]->GetLaminarViscosityInc());
			Mean_EddyVisc    = 0.5*(node[iPoint]->GetEddyViscosity() + node[jPoint]->GetEddyViscosity());
			Mean_Density     = 0.5*(node[iPoint]->GetDensityInc() + node[jPoint]->GetDensityInc());
		}
		else {
			Mean_LaminarVisc = 0.5*(node[iPoint]->GetLaminarViscosity() + node[jPoint]->GetLaminarViscosity());
			Mean_EddyVisc    = 0.5*(node[iPoint]->GetEddyViscosity() + node[jPoint]->GetEddyViscosity());
			Mean_Density     = 0.5*(node[iPoint]->GetSolution(0) + node[jPoint]->GetSolution(0));
		}

		Lambda_1 = (4.0/3.0)*(Mean_LaminarVisc + Mean_EddyVisc);
		Lambda_2 = (1.0 + (Prandtl_Lam/Prandtl_Turb)*(Mean_EddyVisc/Mean_LaminarVisc))*(Gamma*Mean_LaminarVisc/Prandtl_Lam);
		Lambda = (Lambda_1 + Lambda_2)*Area*Area/Mean_Density;

		node[iPoint]->AddMax_Lambda_Visc(Lambda);
		node[jPoint]->AddMax_Lambda_Visc(Lambda);
	}

	/*--- Loop boundary edges ---*/
	for (iMarker = 0; iMarker < geometry->GetnMarker(); iMarker++) {
		for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {

			/*--- Point identification, Normal vector and area ---*/
			iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
			Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
			Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);

			/*--- Mean Values ---*/
			if (incompressible) {
				Mean_ProjVel = node[iPoint]->GetProjVelInc(Normal);
				Mean_BetaInc2 = node[iPoint]->GetBetaInc2();
				Mean_SoundSpeed = sqrt(Mean_ProjVel*Mean_ProjVel + Mean_BetaInc2*Area*Area);
			}
			else {
				Mean_ProjVel = node[iPoint]->GetProjVel(Normal);
				Mean_SoundSpeed = node[iPoint]->GetSoundSpeed() * Area;
			}

			/*--- Contribution due to a rotating frame ---*/
			if (rotating_frame) {
				double *RotVel = geometry->node[iPoint]->GetRotVel();
				ProjVel = 0.0;
				for (iDim = 0; iDim < nDim; iDim++)
					ProjVel += RotVel[iDim]*Normal[iDim];
				Mean_ProjVel -= ProjVel;
			}

			/*--- Contribution due to grid movement ---*/
			if (grid_movement) {
				double *GridVel = geometry->node[iPoint]->GetGridVel();
				ProjVel = 0.0;
				for (iDim = 0; iDim < nDim; iDim++)
					ProjVel += GridVel[iDim]*Normal[iDim];
				Mean_ProjVel -= ProjVel;
			}

			/*--- Inviscid contribution ---*/
			Lambda = fabs(Mean_ProjVel) + Mean_SoundSpeed;
			if (geometry->node[iPoint]->GetDomain()) {
				node[iPoint]->AddMax_Lambda_Inv(Lambda);
				if (centered) node[iPoint]->AddLambda(Lambda);
			}

			/*--- Viscous contribution ---*/
			if (incompressible) {
				Mean_LaminarVisc = 0.5*(node[iPoint]->GetLaminarViscosityInc() + node[jPoint]->GetLaminarViscosityInc());
				Mean_EddyVisc    = 0.5*(node[iPoint]->GetEddyViscosity() + node[jPoint]->GetEddyViscosity());
				Mean_Density     = 0.5*(node[iPoint]->GetDensityInc() + node[jPoint]->GetDensityInc());
			}
			else {
				Mean_LaminarVisc = node[iPoint]->GetLaminarViscosity();
				Mean_EddyVisc    = node[iPoint]->GetEddyViscosity();
				Mean_Density     = node[iPoint]->GetSolution(0);
			}

			Lambda_1 = (4.0/3.0)*(Mean_LaminarVisc + Mean_EddyVisc);
			Lambda_2 = (1.0 + (Prandtl_Lam/Prandtl_Turb)*(Mean_EddyVisc/Mean_LaminarVisc))*(Gamma*Mean_LaminarVisc/Prandtl_Lam);
			Lambda = (Lambda_1 + Lambda_2)*Area*Area/Mean_Density;
			node[iPoint]->AddMax_Lambda_Visc(Lambda);
		}
	}

	/*--- Each element uses their own speed ---*/
	for (iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++) {
		Vol = geometry->node[iPoint]->GetVolume();
		Local_Delta_Time = config->GetCFL(iMesh)*Vol / (node[iPoint]->GetMax_Lambda_Inv()+EPS);
		Local_Delta_Time_Visc = config->GetCFL(iMesh)*K_v*Vol*Vol/ (node[iPoint]->GetMax_Lambda_Visc()+EPS);
		Local_Delta_Time = min(Local_Delta_Time, Local_Delta_Time_Visc);

		/*--- Check if there is any element with only one neighbor...
		 a CV that is inside another CV ---*/
		if (geometry->node[iPoint]->GetnPoint() == 1) Local_Delta_Time = 0.0;

		Global_Delta_Time = min(Global_Delta_Time, Local_Delta_Time);
		Min_Delta_Time = min(Min_Delta_Time, Local_Delta_Time);
		Max_Delta_Time = max(Max_Delta_Time, Local_Delta_Time);
		node[iPoint]->SetDelta_Time(Local_Delta_Time);
	}

	/*--- For exact time solution use the minimum delta time of the whole mesh ---*/
	if (config->GetUnsteady_Simulation() == TIME_STEPPING) {
#ifndef NO_MPI
		double rbuf_time, sbuf_time;
		sbuf_time = Global_Delta_Time;
		MPI::COMM_WORLD.Reduce(&sbuf_time, &rbuf_time, 1, MPI::DOUBLE, MPI::MIN, MASTER_NODE);
		MPI::COMM_WORLD.Bcast(&rbuf_time, 1, MPI::DOUBLE, MASTER_NODE);
		MPI::COMM_WORLD.Barrier();
		Global_Delta_Time = rbuf_time;
#endif
		for(iPoint = 0; iPoint < geometry->GetnPointDomain(); iPoint++)
			node[iPoint]->SetDelta_Time(Global_Delta_Time);
	}

	/*--- Recompute the unsteady time step for the dual time stratey
	 if the unsteady CFL is diferent from 0 ---*/
	if (((config->GetUnsteady_Simulation() == DT_STEPPING_1ST) || (config->GetUnsteady_Simulation() == DT_STEPPING_2ND)) &&
			(Iteration == 0) && (config->GetUnst_CFL() != 0.0) && (iMesh == MESH_0)) {
		Global_Delta_UnstTimeND = config->GetUnst_CFL()*Global_Delta_Time/config->GetCFL(iMesh);
#ifndef NO_MPI
		double rbuf_time, sbuf_time;
		sbuf_time = Global_Delta_UnstTimeND;
		MPI::COMM_WORLD.Reduce(&sbuf_time, &rbuf_time, 1, MPI::DOUBLE, MPI::MIN, MASTER_NODE);
		MPI::COMM_WORLD.Bcast(&rbuf_time, 1, MPI::DOUBLE, MASTER_NODE);
		MPI::COMM_WORLD.Barrier();
		Global_Delta_UnstTimeND = rbuf_time;
#endif
		config->SetDelta_UnstTimeND(Global_Delta_UnstTimeND);
	}

}

void CNSSolution::Viscous_Residual(CGeometry *geometry, CSolution **solution_container, CNumerics *solver,
		CConfig *config, unsigned short iMesh, unsigned short iRKStep) {
	unsigned long iPoint, jPoint, iEdge;

	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool incompressible = config->GetIncompressible();

	if ((config->Get_Beta_RKStep(iRKStep) != 0) || implicit) {

		/*--- Compute gradients (not in the preprocessing, because we want to be sure that we have
		 the gradient of the primitive varibles, not the conservative) ---*/
		if (config->GetKind_Gradient_Method() == GREEN_GAUSS) SetPrimVar_Gradient_GG(geometry, config);
		if (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES) SetPrimVar_Gradient_LS(geometry, config);

		for (iEdge = 0; iEdge < geometry->GetnEdge(); iEdge++) {

			/*--- Points, coordinates and normal vector in edge ---*/
			iPoint = geometry->edge[iEdge]->GetNode(0);
			jPoint = geometry->edge[iEdge]->GetNode(1);
			solver->SetCoord(geometry->node[iPoint]->GetCoord(), geometry->node[jPoint]->GetCoord());
			solver->SetNormal(geometry->edge[iEdge]->GetNormal());

			/*--- Primitive variables, and gradient ---*/
			solver->SetPrimitive(node[iPoint]->GetPrimVar(), node[jPoint]->GetPrimVar());
			solver->SetPrimVarGradient(node[iPoint]->GetGradient_Primitive(), node[jPoint]->GetGradient_Primitive());

			/*--- Laminar and eddy viscosity ---*/
			if (incompressible) {
				solver->SetDensityInc(node[iPoint]->GetDensityInc(), node[jPoint]->GetDensityInc());
				solver->SetLaminarViscosity(node[iPoint]->GetLaminarViscosityInc(), node[jPoint]->GetLaminarViscosityInc());
			}
			else
				solver->SetLaminarViscosity(node[iPoint]->GetLaminarViscosity(), node[jPoint]->GetLaminarViscosity());

			solver->SetEddyViscosity(node[iPoint]->GetEddyViscosity(), node[jPoint]->GetEddyViscosity());

			/*--- Turbulent kinetic energy ---*/
			if (config->GetKind_Turb_Model() == SST)
				solver->SetTurbKineticEnergy(solution_container[TURB_SOL]->node[iPoint]->GetSolution(0),
						solution_container[TURB_SOL]->node[jPoint]->GetSolution(0));

			/*--- Compute and update residual ---*/
			solver->SetResidual(Res_Visc, Jacobian_i, Jacobian_j, config);

			node[iPoint]->SubtractRes_Visc(Res_Visc);
			node[jPoint]->AddRes_Visc(Res_Visc);

			/*--- Implicit part ---*/
			if (implicit) {
				Jacobian.SubtractBlock(iPoint,iPoint,Jacobian_i);
				Jacobian.SubtractBlock(iPoint,jPoint,Jacobian_j);
				Jacobian.AddBlock(jPoint,iPoint,Jacobian_i);
				Jacobian.AddBlock(jPoint,jPoint,Jacobian_j);
			}
		}
	}
}

void CNSSolution::Viscous_Forces(CGeometry *geometry, CConfig *config) {
	unsigned long iVertex, iPoint;
	unsigned short Boundary, Monitoring, iMarker, iDim, jDim;
	double **Tau, Delta, Viscosity, **Grad_PrimVar, div_vel, *Normal, *TauElem;
	double dist[3], *Coord, *UnitaryNormal, *TauTangent, Area, WallShearStress, TauNormal;
	double factor, RefVel2, RefDensity;
	double cp, heat_flux_factor, GradTemperature;

	double Alpha        = config->GetAoA()*PI_NUMBER/180.0;
	double Beta         = config->GetAoS()*PI_NUMBER/180.0;
	double RefAreaCoeff = config->GetRefAreaCoeff();
	double RefLengthMoment = config->GetRefLengthMoment();
	double *Origin      = config->GetRefOriginMoment();
	double Gas_Constant = config->GetGas_Constant();
	bool rotating_frame = config->GetRotating_Frame();
	bool incompressible = config->GetIncompressible();

	cp = (Gamma / Gamma_Minus_One) * Gas_Constant;

	/*--- If this is a rotating frame problem, use the specified speed
        for computing the force coefficients. Otherwise, use the 
        freestream values which is the standard convention. ---*/

	if (!rotating_frame) {
		double *Velocity_Inf = config->GetVelocity_FreeStreamND();
		RefVel2 = 0.0;
		for (iDim = 0; iDim < nDim; iDim++)
			RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
	}
	else {
		/*--- Use the rotational origin for computing moments ---*/
		Origin = config->GetRotAxisOrigin();
		/*--- Reference area is the rotor disk area ---*/
		RefLengthMoment = config->GetRotRadius();
		RefAreaCoeff = PI_NUMBER*RefLengthMoment*RefLengthMoment;
		/* --- Reference velocity is the rotational speed times rotor radius ---*/
		RefVel2     = (config->GetOmegaMag()*RefLengthMoment)*(config->GetOmegaMag()*RefLengthMoment);
	}

	RefDensity  = config->GetDensity_FreeStreamND();

	factor = 1.0 / (0.5*RefDensity*RefAreaCoeff*RefVel2);

	/*-- Initialization --*/
	AllBound_CDrag_Visc = 0.0; AllBound_CLift_Visc = 0.0;
	AllBound_CMx_Visc = 0.0; AllBound_CMy_Visc = 0.0; AllBound_CMz_Visc = 0.0;
	AllBound_CFx_Visc = 0.0; AllBound_CFy_Visc = 0.0; AllBound_CFz_Visc = 0.0;
	AllBound_CEff_Visc = 0.0; AllBound_CMerit_Visc = 0.0;
	AllBound_CT_Visc = 0.0; AllBound_CQ_Visc = 0.0;

	/*--- Vector and variables initialization ---*/
	UnitaryNormal      = new double [nDim];
	TauElem    = new double [nDim];
	TauTangent = new double [nDim];
	Tau        = new double* [nDim];
	for (iDim = 0; iDim < nDim; iDim++)
		Tau[iDim]   = new double [nDim];

	/*--- Loop over the Navier-Stokes markers ---*/
	for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
		Boundary = config->GetMarker_All_Boundary(iMarker);
		Monitoring = config->GetMarker_All_Monitoring(iMarker);

		if (Boundary == NO_SLIP_WALL) {

			for (iDim = 0; iDim < nDim; iDim++) ForceViscous[iDim] = 0.0;
			MomentViscous[0] = 0.0; MomentViscous[1] = 0.0; MomentViscous[2] = 0.0;

			for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
				iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
				
				if (geometry->node[iPoint]->GetDomain()) {
					
					Coord = geometry->node[iPoint]->GetCoord();
					for (iDim = 0; iDim < nDim; iDim++) dist[iDim] = Coord[iDim] - Origin[iDim];

					if (incompressible) Viscosity = node[iPoint]->GetLaminarViscosityInc();
					else Viscosity = node[iPoint]->GetLaminarViscosity();

					Grad_PrimVar = node[iPoint]->GetGradient_Primitive();

					div_vel = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						div_vel += Grad_PrimVar[iDim+1][iDim];

					for (iDim = 0; iDim < nDim; iDim++)
						for (jDim = 0 ; jDim < nDim; jDim++) {
							Delta = 0.0; if (iDim == jDim) Delta = 1.0;
							Tau[iDim][jDim] = Viscosity*(Grad_PrimVar[jDim+1][iDim] +
									Grad_PrimVar[iDim+1][jDim]) - TWO3*Viscosity*div_vel*Delta;
						}

					Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
					Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);
					for (iDim = 0; iDim < nDim; iDim++) UnitaryNormal[iDim] = Normal[iDim]/Area;

					for (iDim = 0; iDim < nDim; iDim++) {
						TauElem[iDim] = 0.0;
						for (jDim = 0; jDim < nDim; jDim++)
							TauElem[iDim] += Tau[iDim][jDim]*UnitaryNormal[jDim] ;
					}

					/*--- Compute viscous forces, and moment ---*/
					if (Monitoring == YES) {
						for (iDim = 0; iDim < nDim; iDim++)
							ForceViscous[iDim] += TauElem[iDim]*Area*factor;
						
						if (iDim == 3) MomentViscous[0] += (ForceViscous[2]*dist[1] - ForceViscous[1]*dist[2])/RefLengthMoment;
						if (iDim == 3) MomentViscous[1] += (ForceViscous[0]*dist[2] - ForceViscous[2]*dist[0])/RefLengthMoment;
						MomentViscous[2] += (ForceViscous[1]*dist[0] - ForceViscous[0]*dist[1])/RefLengthMoment;
					}
					
					/*--- Compute wall shear stress, skin friction coefficient, and heat flux on the wall ---*/
					TauNormal = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						TauNormal += TauElem[iDim] * UnitaryNormal[iDim];

					for (iDim = 0; iDim < nDim; iDim++)
						TauTangent[iDim] = TauElem[iDim] - TauNormal * UnitaryNormal[iDim];

					WallShearStress = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						WallShearStress += TauTangent[iDim]*TauTangent[iDim];

					/*--- Note that the wall Shear Stress is just mu(delta u/delta y)---*/
					CSkinFriction[iMarker][iVertex] = sqrt(WallShearStress) / (0.5*RefDensity*RefVel2);

					GradTemperature = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						GradTemperature +=  Grad_PrimVar[0][iDim]*(-Normal[iDim]);
					
					heat_flux_factor = (cp * Viscosity/PRANDTL);
					CHeatTransfer[iMarker][iVertex] = heat_flux_factor*GradTemperature/(0.5*RefDensity*RefVel2);
					
				}
				
			}

			/*--- Transform ForceInviscid into CLift and CDrag ---*/
			if  (Monitoring == YES) {
				if (nDim == 2) {
					CDrag_Visc[iMarker] =  ForceViscous[0]*cos(Alpha) + ForceViscous[1]*sin(Alpha);
					CLift_Visc[iMarker] = -ForceViscous[0]*sin(Alpha) + ForceViscous[1]*cos(Alpha);
					CMx_Visc[iMarker] = 0.0;
					CMy_Visc[iMarker] = 0.0;
					CMz_Visc[iMarker] = MomentViscous[2];
					CEff_Visc[iMarker] = CLift_Visc[iMarker]/CDrag_Visc[iMarker];
					CFx_Visc[iMarker] = ForceViscous[0];
					CFy_Visc[iMarker] = ForceViscous[1];
					CFz_Visc[iMarker] = 0.0;
					CT_Visc[iMarker] = -CFx_Visc[iMarker];
					CQ_Visc[iMarker] = 0.0;
					CMerit_Visc[iMarker] = 0.0;
				}
				if (nDim == 3) {
					CDrag_Visc[iMarker] =  ForceViscous[0]*cos(Alpha)*cos(Beta) + ForceViscous[1]*sin(Beta) + ForceViscous[2]*sin(Alpha)*cos(Beta);
					CLift_Visc[iMarker] = -ForceViscous[0]*sin(Alpha) + ForceViscous[2]*cos(Alpha);
					CMx_Visc[iMarker] = MomentViscous[0];
					CMy_Visc[iMarker] = MomentViscous[1];
					CMz_Visc[iMarker] = MomentViscous[2];
					CEff_Visc[iMarker] = CLift_Visc[iMarker]/CDrag_Visc[iMarker];
					CFx_Visc[iMarker] = ForceViscous[0];
					CFy_Visc[iMarker] = ForceViscous[1];
					CFz_Visc[iMarker] = ForceViscous[2];
					CT_Visc[iMarker] = -CFx_Visc[iMarker];
					CQ_Visc[iMarker] = -CMx_Visc[iMarker];
					CMerit_Visc[iMarker] = CT_Visc[iMarker]/CQ_Visc[iMarker];
				}

				AllBound_CDrag_Visc += CDrag_Visc[iMarker];
				AllBound_CLift_Visc += CLift_Visc[iMarker];
				AllBound_CMx_Visc += CMx_Visc[iMarker];
				AllBound_CMy_Visc += CMy_Visc[iMarker];
				AllBound_CMz_Visc += CMz_Visc[iMarker];
				AllBound_CEff_Visc += CEff_Visc[iMarker];
				AllBound_CFx_Visc += CFx_Visc[iMarker];
				AllBound_CFy_Visc += CFy_Visc[iMarker];
				AllBound_CFz_Visc += CFz_Visc[iMarker];
				AllBound_CT_Visc += CT_Visc[iMarker];
				AllBound_CQ_Visc += CQ_Visc[iMarker];
				AllBound_CMerit_Visc += CMerit_Visc[iMarker];
			}
		}
	}

	Total_CDrag += AllBound_CDrag_Visc;
	Total_CLift += AllBound_CLift_Visc;
	Total_CMx += AllBound_CMx_Visc;
	Total_CMy += AllBound_CMy_Visc;
	Total_CMz += AllBound_CMz_Visc;
	Total_CEff = Total_CLift/Total_CDrag;
	Total_CFx += AllBound_CFx_Visc;
	Total_CFy += AllBound_CFy_Visc;
	Total_CFz += AllBound_CFz_Visc;
	Total_CT += AllBound_CT_Visc;
	Total_CQ += AllBound_CQ_Visc;
	Total_CMerit += AllBound_CMerit_Visc;

	for (iDim = 0; iDim < nDim; iDim++)
		delete [] Tau[iDim];
	delete [] Tau;
	delete [] UnitaryNormal;
	delete [] TauTangent;
	delete [] TauElem;
}

void CNSSolution::BC_NS_Wall(CGeometry *geometry, CSolution **solution_container, CNumerics *solver, CConfig *config, unsigned short val_marker) {
	unsigned long iVertex, iPoint, total_index, Point_Normal;
	unsigned short iVar, iDim, jVar;
	bool implicit = (config->GetKind_TimeIntScheme_Flow() == EULER_IMPLICIT);
	bool adiabatic = config->GetAdiabaticWall();
	bool incompressible = config->GetIncompressible();
	bool grid_movement = config->GetGrid_Movement();
	bool rotating_frame = config->GetRotating_Frame();
	double Laminar_Viscosity, total_viscosity, Gas_Constant, cp,cpoR;
	double heat_flux_factor, factor, phi_rho, phi_p, rhoovisc;
	double *Normal, Density, *Coord_i, *Coord_j, dist_ij, *Grid_Vel, *Rot_Vel;
	double *U_i, *U_j;
	double  sq_vel, Pressure, Temperature, Temperature_Gradient;
	double Twall, RefVel3, RefDensity, theta, phi;


	Gas_Constant = config->GetGas_Constant();
	cp = (Gamma / Gamma_Minus_One) * Gas_Constant;
	cpoR = cp/Gas_Constant; // cp over R

	Twall = 300.0;//Pressure_Inf/(Gas_Constant*Density_Inf);
	RefVel3 = 0.0;
	for (iDim = 0; iDim < nDim; iDim++)
		RefVel3  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
	RefDensity  = Density_Inf;



	if (adiabatic) {
		for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
			iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
			if (geometry->node[iPoint]->GetDomain()) {

				/*--- Store the corrected velocity at the wall which will
         be zero (v = 0), unless there is grid motion (v = u_wall)---*/
				if (rotating_frame) {
					Rot_Vel = geometry->node[iPoint]->GetRotVel();
					for (iDim = 0; iDim < nDim; iDim++)
						Vector[iDim] = Rot_Vel[iDim];
				} else if (grid_movement) {
					Grid_Vel = geometry->node[iPoint]->GetGridVel();
					for (iDim = 0; iDim < nDim; iDim++)
						Vector[iDim] = Grid_Vel[iDim];
				} else {
					for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
				}

				/*--- Set the residual, truncation error and velocity value ---*/
				node[iPoint]->SetVelocity_Old(Vector, incompressible);
				node[iPoint]->SetVel_ResConv_Zero();
				node[iPoint]->SetVel_ResSour_Zero();
				node[iPoint]->SetVel_ResVisc_Zero();
				node[iPoint]->SetVelRes_TruncErrorZero();

				/*--- Only change velocity-rows of the Jacobian (includes 1 in the diagonal) ---*/
				if (implicit)
					for (iVar = 1; iVar <= nDim; iVar++) {
						total_index = iPoint*nVar+iVar;
						Jacobian.DeleteValsRowi(total_index);
					}
			}
		}
	}
	else {
		for(iVertex = 0; iVertex < geometry->nVertex[val_marker]; iVertex++) {
			iPoint = geometry->vertex[val_marker][iVertex]->GetNode();
			if (geometry->node[iPoint]->GetDomain()) {

				/*--- Compute the projected residual ---*/
				Normal = geometry->vertex[val_marker][iVertex]->GetNormal();

				double Area = 0.0; double UnitaryNormal[3];
				for (iDim = 0; iDim < nDim; iDim++)
					Area += Normal[iDim]*Normal[iDim];
				Area = sqrt (Area);

				for (iDim = 0; iDim < nDim; iDim++)
					UnitaryNormal[iDim] = -Normal[iDim]/Area;

				/*--- Compute closest normal neighbor ---*/
				Point_Normal = geometry->vertex[val_marker][iVertex]->GetClosest_Neighbor();

				/*--- Store the corrected velocity at the wall which will
         be zero (v = 0), unless there is grid motion (v - u_wall = 0)---*/
				if (rotating_frame) {
					Rot_Vel = geometry->node[iPoint]->GetRotVel();
					for (iDim = 0; iDim < nDim; iDim++)
						Vector[iDim] = Rot_Vel[iDim];
				} else if (grid_movement) {
					Grid_Vel = geometry->node[iPoint]->GetGridVel();
					for (iDim = 0; iDim < nDim; iDim++)
						Vector[iDim] = Grid_Vel[iDim];
				} else {
					for (iDim = 0; iDim < nDim; iDim++) Vector[iDim] = 0.0;
				}

				/*--- Set the residual, truncation error and velocity value ---*/
				node[iPoint]->SetVelocity_Old(Vector, incompressible);
				node[iPoint]->SetVel_ResConv_Zero();
				node[iPoint]->SetVel_ResSour_Zero();
				node[iPoint]->SetVel_ResVisc_Zero();
				node[iPoint]->SetVelRes_TruncErrorZero();

				for (iVar = 0; iVar < nVar; iVar ++)
					Res_Visc[iVar] = 0.0;


				//	GradPrimVar = node[iPoint]->GetGradient_Primitive();

				dist_ij = 0;
				Coord_i = geometry->node[iPoint]->GetCoord();
				Coord_j = geometry->node[Point_Normal]->GetCoord();

				for (iDim = 0; iDim < nDim; iDim++)
					dist_ij += (Coord_j[iDim]-Coord_i[iDim])*(Coord_j[iDim]-Coord_i[iDim]);
				dist_ij = sqrt(dist_ij);

				U_i = node[iPoint]->GetSolution() ;
				U_j = node[Point_Normal]->GetSolution();

				CHeatTransfer[val_marker][iVertex] = 0.0;

				sq_vel = 0.0;
				for (iDim = 0; iDim < nDim; iDim ++)
					sq_vel += pow(U_j[iDim+1]/U_j[0],2);

				Pressure = (Gamma-1)*(U_j[nDim+1] - 0.5*U_j[0]*sq_vel);
				Temperature = Pressure/(Gas_Constant*U_j[0]);
				Temperature_Gradient = (Twall - Temperature)/dist_ij;

				Laminar_Viscosity = node[iPoint]->GetLaminarViscosity() ;
				total_viscosity = Laminar_Viscosity ;
				heat_flux_factor = cp * Laminar_Viscosity/PRANDTL;

				Res_Visc[nDim+1] = heat_flux_factor * Temperature_Gradient*Area;

				CHeatTransfer[val_marker][iVertex] = -heat_flux_factor * Temperature_Gradient/(0.5*RefDensity*RefVel3);

				node[iPoint]->SubtractRes_Visc(Res_Visc);  // SIGN CHECK

				/*--- Only change velocity-rows of the Jacobian (includes 1 in the diagonal) ---*/
				if (implicit) {
					Density =  U_i[0];
					sq_vel = 0.0;
					for (iDim = 0; iDim< nDim; iDim ++)
						sq_vel   += pow((U_i[iDim+1]/U_i[0]),2);

					Pressure = Gamma_Minus_One*(U_i[nDim+1]-0.5*sq_vel*U_i[0]);
					theta = 0.0;
					for (iDim = 0; iDim < nDim; iDim++)
						theta += UnitaryNormal[iDim]*UnitaryNormal[iDim];

					phi = 0.5*(Gamma-1.0)*sq_vel;

					factor = total_viscosity/(Density*dist_ij)*Area;
					phi_rho = -cpoR*heat_flux_factor*Pressure/(Density*Density);
					phi_p = cpoR*heat_flux_factor/Density;
					rhoovisc = Density/total_viscosity; // rho over viscosity

					for (iVar = 0; iVar < nVar; iVar ++)
						for (jVar = 0; jVar < nVar; jVar ++)
							Jacobian_i[iVar][jVar] = 0.0;

					Jacobian_i[nDim+1][0] = -factor*(rhoovisc*theta*(phi_rho+phi*phi_p));
					Jacobian_i[nDim+1][nDim+1] = -factor*((Gamma-1)*rhoovisc*theta*phi_p);

					Jacobian.SubtractBlock(iPoint, iPoint, Jacobian_i);
				}
			}
		}
	}
}