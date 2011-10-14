#include <iostream>
#include <iomanip>
#include <string>
#include <cassert>
#include <fstream>
#include <vector>
#include <map>
#include <mpi.h>

#include "Simulate.hh"
#include "Heart.hh"
#include "TT04Model.hh"
#include "SimpleInputParser.hh"
#include "ProcessGrid3D.hh"
#include "DataGrid3D.hh"
#include "Timer.hh"
#include "GDLoadBalancer.hh"
#include "AnatomyReader.hh"
#include "mpiUtils.h"
#include "KoradiTest.hh"

#include "writeCells.hh"
#include "initializeAnatomy.hh"
#include "assignCellsToTasks.hh"
#include "heap.h"
#include "object_cc.hh"

using namespace std;

void parseCommandLineAndReadInputFile(int argc, char** argv, int rank);



// Sorry about this.
MPI_Comm COMM_LOCAL = MPI_COMM_WORLD;

int main(int argc, char** argv)
{
  int npes, mype;
  MPI_Init(&argc,&argv);
  MPI_Comm_size(MPI_COMM_WORLD, &npes);
  MPI_Comm_rank(MPI_COMM_WORLD, &mype);  
  heap_start(100);

  parseCommandLineAndReadInputFile(argc, argv, mype);

  Simulate sim;
  OBJECT* simObj = object_find("simulate", "SIMULATE");
  string nameTmp;

  objectGet(simObj, "anatomy", nameTmp, "anatomy");
  initializeAnatomy(sim, nameTmp, MPI_COMM_WORLD);
  
  objectGet(simObj, "decomposition", nameTmp, "decomposition");
  assignCellsToTasks(sim, nameTmp, MPI_COMM_WORLD);



//  buildHaloExchange(sim, MPI_COMM_WORLD);
//   prepareCellModels();
//   precompute();
//   simulationLoop();
  

  
  
  // create integrator object, run time steps
#if 0
  { // limit scope


     
     double*** Vm;
     diffusion*** diffIntra;
     cell* cells;

     for (param.tcurrent=param.tstart;
	  param.tcurrent<param.tend; param.tcurrent++)
     {
	
	// REACTION
	for (int iCell=0; iCell<nTissue; ++iCell)
	{
	   iStimArray[iCell] =
	      boundaryFDLaplacianSaleheen98SumPhi(
		 Vm, diffIntra,
		 cells[iCell].x, cells[iCell].y, cells[iCell].z);
	}
	
	// DIFFUSION
	for (int iCell=0; iCell<nTissue; ++iCell)
	{
	   iStimArray[iCell] *= param.diffusionscale;
	   
	   // code to limit or set iStimArray goes here.
	   
	   VmArray[iCell] = pemIBMArray[iCell]->Calc(
	      param.dt, VmArray[iCell], IstimArray[iCell]);
	}
     }
  } //limit scope
  
#endif
  
  
  
  
  if (mype == 0)
  {
     
     cout.setf(ios::fixed,ios::floatfield);
     for ( map<std::string,Timer>::iterator i = sim.tmap_.begin(); i != sim.tmap_.end(); i++ )
     {
	double time = (*i).second.real();
	cout << "timing name=" << setw(15) << (*i).first << ":   time=" << setprecision(6) << setw(12) << time << " sec" << endl;
     }
     
  }
  MPI_Finalize();

  return 0;
}


void parseCommandLineAndReadInputFile(int argc, char** argv, int rank)
{
   // get input file name from command line argument
   if (argc != 2 && argc != 1)
   {
      if (rank == 0)
	 cout << "Usage:  bigHeart [input file]" << endl;
      exit(-1);
   }

   // parse input file
   string inputfile("object.data");
   if (argc > 1)
   {
      string argfile(argv[1]);
      inputfile = argfile;
      cout << "argc = " << argc << endl;
   }

   object_compilefile(inputfile.c_str());
   

}
