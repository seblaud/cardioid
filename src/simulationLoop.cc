#include "simulationLoop.hh"

#include <vector>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <omp.h>

#include "Simulate.hh"
#include "Diffusion.hh"
#include "Reaction.hh"
#include "Stimulus.hh"
#include "Sensor.hh"
#include "HaloExchange.hh"
#include "GridRouter.hh"
#include "ioUtils.h"
#include "writeCells.hh"
#include "checkpointIO.hh"
#include "PerformanceTimers.hh"
#include "fastBarrier.hh"
#include "Threading.hh"

using namespace std;
using namespace PerformanceTimers;


void simulationProlog(Simulate& sim)
{
   // initialize membrane voltage with default value from the reaction model. 
   sim.VmArray_.resize(sim.anatomy_.size());
   sim.reaction_->initializeMembraneVoltage(sim.VmArray_);
  
   for (unsigned ii=sim.anatomy_.nLocal(); ii<sim.anatomy_.size(); ++ii)
      sim.VmArray_[ii] = 0;

   // Load state file, assign corresponding values to membrane voltage and cell model
   if (!sim.stateFilename_.empty())
      readCheckpoint(sim, MPI_COMM_WORLD);
}


void loopIO(const Simulate& sim, const vector<double>& dVmR, vector<double>& dVmD,  const vector<double>& dVmE)
{
   int diffusionID = sim.diffusionGroup_->threadID(); 
   if (diffusionID != 0) return; 
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
      
   static int firstCall=1; 
   int loop = sim.loop_; 

   // SENSORS
   profileFastStart(sensorTimer);
   for (unsigned ii=0; ii<sim.sensor_.size(); ++ii)
   {
       if (loop % sim.sensor_[ii]->evalRate() == 0) sim.sensor_[ii]->eval(sim.time_, loop, sim.VmArray_, dVmR, dVmD, dVmE);
   }
   profileFastStop(sensorTimer);

   profileFastStart(loopIOTimer);
      
   for (unsigned ii=0; ii<sim.sensor_.size(); ++ii)
   {
      if (loop % sim.sensor_[ii]->printRate() == 0) sim.sensor_[ii]->print(sim.time_, loop, sim.VmArray_, dVmR, dVmD, dVmE);
   }
      
   if ( (loop % sim.printRate_ == 0) && myRank == 0)
   {
      if (firstCall) printf("    Loop     Time         Vm(t)        dVm_r(t-h)      dVm_d(t-h)       dVm_e(t-h)\n");
      printf("%8d %8.3f %15.8f %15.8f %15.8f %15.8f\n",loop,sim.time_,sim.VmArray_[0],dVmR[0],dVmD[0],dVmE[0]); 
   }
   if (!firstCall) 
   { 
      if (loop % sim.snapshotRate_ == 0)
      {
         stringstream name;
         name << "snapshot."<<setfill('0')<<setw(12)<<loop;
         string fullname = name.str();
         if (myRank == 0) DirTestCreate(fullname.c_str());
         fullname += "/anatomy";
         writeCells(sim, fullname.c_str());
      }
         
   }
   firstCall=0; 
   profileFastStop(loopIOTimer);
}


void simulationLoop(Simulate& sim)
{

   vector<double> dVmDiffusion(sim.anatomy_.nLocal(), 0.0);
   vector<double> dVmReaction(sim.anatomy_.nLocal(), 0.0);
   vector<double> dVmExternal(sim.anatomy_.nLocal(), 0.0);
   vector<double> iStim(sim.anatomy_.nLocal(), 0.0);
   simulationProlog(sim);
#ifdef SPI   
   spi_HaloExchange<double> voltageExchange(sim.sendMap_, (sim.commTable_));
#else
   mpi_HaloExchange<double> voltageExchange(sim.sendMap_, (sim.commTable_));
#endif

#if defined(SPI) && defined(TRACESPI)
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
   cout << "Rank[" << myRank << "]: numOfNeighborToSend=" << sim.commTable_->_sendTask.size() << " numOfNeighborToRecv=" << sim.commTable_->_recvTask.size() << " numOfBytesToSend=" << sim.commTable_->_sendOffset[sim.commTable_->_sendTask.size()]*sizeof(double) << " numOfBytesToRecv=" << sim.commTable_->_recvOffset[sim.commTable_->_recvTask.size()]*sizeof(double) << endl;
#endif

   while ( sim.loop_<sim.maxLoop_ )
   {
      int nLocal = sim.anatomy_.nLocal();
    
      profileFastStart(imbalanceTimer);
      voltageExchange.barrier();
      profileFastStop(imbalanceTimer);

      profileFastStart(haloTimer);
      voltageExchange.execute(sim.VmArray_, nLocal);
      voltageExchange.complete();
      profileFastStop(haloTimer);

      for (unsigned ii=0; ii<nLocal; ++ii) dVmExternal[ii] = 0;
    

      // DIFFUSION
      profileFastStart(diffusionTimer);
      sim.diffusion_->calc(sim.VmArray_, dVmDiffusion, voltageExchange.get_recv_buf_(), nLocal);
      //sim.diffusion_->calc_simd(sim.VmArray_, dVmDiffusion, voltageExchange.get_recv_buf_(), nLocal);
      profileFastStop(diffusionTimer);

      // code to limit or set iStimArray goes here.
      profileFastStart(stimulusTimer);
      for (unsigned ii=0; ii<sim.stimulus_.size(); ++ii) sim.stimulus_[ii]->stim(sim.time_, dVmDiffusion, dVmExternal);
      for (unsigned ii=0; ii<nLocal; ++ii) iStim[ii] = -(dVmDiffusion[ii] + dVmExternal[ii]);
      profileFastStop(stimulusTimer);

      
      // REACTION
      profileFastStart(reactionTimer);
      sim.reaction_->calc(sim.dt_, sim.VmArray_, iStim, dVmReaction);
      profileFastStop(reactionTimer);

      profileFastStart(integratorTimer);
      for (unsigned ii=0; ii<nLocal; ++ii)
      {
         double dVm = dVmReaction[ii] + dVmDiffusion[ii] + dVmExternal[ii] ;
         sim.VmArray_[ii] += sim.dt_*dVm;
      }
      sim.time_ += sim.dt_;
      ++sim.loop_;
      profileFastStop(integratorTimer);
      if (sim.checkpointRate_ > 0 && sim.loop_ % sim.checkpointRate_ == 0) writeCheckpoint(sim, MPI_COMM_WORLD);
      loopIO(sim,dVmReaction,dVmDiffusion,dVmExternal);

   }
}


/** One stop shopping for all of the data that we would rather create
 *  before the parallel section, either because we want the data to
 *  be shared across threads, or because it is just easier to create
 *  it in a single thread environment.  This data is all gathered
 *  into a struct so that it can be easily passed to the diffusion
 *  and reaction loops without giant argument lists.
 *
 *  You might argue that all of this data could be just as well
 *  stored in the Simulation class.  We choose not to that because
 *  - It would turn the Simulation class into quite a kitchen sink
 *  - It would make it even harder to construct the Simulation class
 *    properly
 *  - The data is only needed in the simulation loop.
 */
struct SimLoopData
{
   SimLoopData(const Simulate& sim)
   : voltageExchange(sim.sendMap_, (sim.commTable_))
   {
      haloBarrier = L2_BarrierWithSync_InitShared();
      reactionBarrier = L2_BarrierWithSync_InitShared();
      diffusionBarrier= L2_BarrierWithSync_InitShared();
      reactionWaitOnNonGateBarrier= L2_BarrierWithSync_InitShared();
      int nLocal = sim.anatomy_.nLocal();
      dVmExternal.resize(nLocal, 0.0);
      dVmDiffusion.resize(nLocal, 0.0);
      dVmReactionCpy.resize(nLocal, 0.0);
      dVmReaction.resize(nLocal, 0.0); 
   }

   ~SimLoopData()
   {
      free(reactionWaitOnNonGateBarrier);
      free(diffusionBarrier);
      free(reactionBarrier);
   }
   
   
   L2_Barrier_t* haloBarrier;
   L2_Barrier_t* reactionBarrier;
   L2_Barrier_t* diffusionBarrier;
   L2_Barrier_t* reactionWaitOnNonGateBarrier;
   
   vector<double> dVmReaction; 
   vector<double> dVmExternal;
   vector<double> dVmDiffusion;
   vector<double> dVmReactionCpy;
   #ifdef SPI   
   spi_HaloExchange<double> voltageExchange;
   #else
   mpi_HaloExchange<double> voltageExchange;
   #endif
  
};

void diffusionLoop(Simulate& sim,
                   SimLoopData& loopData,
                   L2_BarrierHandle_t& reactionHandle,
                   L2_BarrierHandle_t& diffusionHandle)
{
   int tid = sim.diffusionGroup_->threadID();
   L2_BarrierHandle_t haloBarrierHandle;
   L2_BarrierWithSync_InitInThread(loopData.haloBarrier, &haloBarrierHandle);
   
   while ( sim.loop_ < sim.maxLoop_ )
   {
      profileFastStart(diffusionLoopTimer);
      int nLocal = sim.anatomy_.nLocal();
    
      if (tid == 0)
      {
         profileFastStart(diffusionImbalanceTimer);
         loopData.voltageExchange.barrier();
         profileFastStop(diffusionImbalanceTimer);
         
         profileFastStart(haloTimer);
         profileFastStart(haloTimerExecute);
         loopData.voltageExchange.execute(sim.VmArray_, nLocal);
         profileFastStop(haloTimerExecute);
         profileFastStart(haloTimerComplete);
         loopData.voltageExchange.complete();
         profileFastStop(haloTimerComplete);
         profileFastStop(haloTimer);

         for (unsigned ii=0; ii<nLocal; ++ii)
            loopData.dVmDiffusion[ii] = 0;
      }
      
      // Need a barrier for the completion of the halo exchange.
      L2_BarrierWithSync_Barrier(loopData.haloBarrier, &haloBarrierHandle,
                                 sim.diffusionGroup_->nThreads());
      
      // DIFFUSION
      profileFastStart(diffusionTimer);
      //sim.diffusion_->calc(sim.VmArray_, loopData.dVmDiffusion,
      //                     loopData.voltageExchange.get_recv_buf_(), nLocal);
      sim.diffusion_->calc_simd(sim.VmArray_, loopData.dVmDiffusion,
                           loopData.voltageExchange.get_recv_buf_(), nLocal);
      profileFastStop(diffusionTimer);
      
      L2_BarrierWithSync_Barrier(loopData.haloBarrier, &haloBarrierHandle,
                                 sim.diffusionGroup_->nThreads());
      if (tid == 0)
      {
         for (unsigned ii=0; ii<nLocal; ++ii)
            loopData.dVmExternal[ii] = 0;
         profileFastStart(stimulusTimer);
         for (unsigned ii=0; ii<sim.stimulus_.size(); ++ii)
            sim.stimulus_[ii]->stim(sim.time_, loopData.dVmDiffusion,
                                    loopData.dVmExternal);
         profileFastStop(stimulusTimer);
      }
      
      profileFastStart(diffusionWaitTimer); 
      L2_BarrierWithSync_WaitAndReset(loopData.reactionBarrier,
                                      &reactionHandle,
                                      sim.reactionGroup_->nThreads());
      profileFastStop(diffusionWaitTimer); 
      profileFastStart(diffusionStallTimer); 


      profileFastStart(dummyTimer);
      profileFastStop(dummyTimer);
      
      if (tid == 0)
      {
         loopData.dVmReactionCpy = loopData.dVmReaction; 
         profileFastStart(integratorTimer);
         for (unsigned ii=0; ii<nLocal; ++ii)
         {
            double dVm = loopData.dVmReaction[ii] + loopData.dVmDiffusion[ii]
               + loopData.dVmExternal[ii];
            sim.VmArray_[ii] += sim.dt_*dVm;
         }
         sim.time_ += sim.dt_;
         ++sim.loop_;
         profileFastStop(integratorTimer);
      }
      
      L2_BarrierWithSync_Arrive(loopData.diffusionBarrier, &diffusionHandle,
                                sim.diffusionGroup_->nThreads());
      // wait and reset to make this a barrier.  Can't let any thread
      // past here until loop_ has been incremented.
      L2_BarrierWithSync_WaitAndReset(loopData.diffusionBarrier,
                                      &diffusionHandle,
                                      sim.diffusionGroup_->nThreads());
      profileFastStop(diffusionStallTimer); 

      if (tid == 0)
      {
         if (sim.checkpointRate_ > 0 && sim.loop_ % sim.checkpointRate_ == 0)
            writeCheckpoint(sim, MPI_COMM_WORLD);
         loopIO(sim, loopData.dVmReactionCpy, loopData.dVmDiffusion,
                loopData.dVmExternal);
      }
      profileFastStop(diffusionLoopTimer);
   }
}

void reactionLoop(Simulate& sim, SimLoopData& loopData, L2_BarrierHandle_t& reactionHandle, L2_BarrierHandle_t& diffusionHandle)
{
   vector<double>& dVmReaction = loopData.dVmReaction;
   L2_BarrierHandle_t reactionWaitOnNonGateHandle;
   L2_BarrierWithSync_InitInThread(loopData.reactionWaitOnNonGateBarrier, &reactionWaitOnNonGateHandle);
   while ( sim.loop_<sim.maxLoop_ )
   {
      profileFastStart(reactionLoopTimer);
      int nLocal = sim.anatomy_.nLocal();
      
      profileFastStart(reactionTimer);
      sim.reaction_->updateNonGate(sim.dt_, sim.VmArray_, dVmReaction);
      L2_BarrierWithSync_Barrier(loopData.reactionWaitOnNonGateBarrier, &reactionWaitOnNonGateHandle, sim.reactionGroup_->nThreads());
      sim.reaction_->updateGate(sim.dt_, sim.VmArray_);
      profileFastStop(reactionTimer);
      L2_BarrierWithSync_Arrive(loopData.reactionBarrier, &reactionHandle, sim.reactionGroup_->nThreads());
      L2_BarrierWithSync_Reset(loopData.reactionBarrier, &reactionHandle, sim.reactionGroup_->nThreads());
      profileFastStart(dummyTimer);
      profileFastStop(dummyTimer);
      profileFastStart(reactionWaitTimer);
      L2_BarrierWithSync_WaitAndReset(loopData.diffusionBarrier, &diffusionHandle, sim.diffusionGroup_->nThreads());
      profileFastStop(reactionWaitTimer);
      profileFastStop(reactionLoopTimer);
   }
}

void nullReactionLoop(Simulate& sim, SimLoopData& loopData, L2_BarrierHandle_t& reactionHandle, L2_BarrierHandle_t& diffusionHandle)
{
   while ( sim.loop_<=sim.maxLoop_ )
   {
      L2_BarrierWithSync_Arrive(loopData.reactionBarrier, &reactionHandle, sim.reactionGroup_->nThreads());
      L2_BarrierWithSync_Reset(loopData.reactionBarrier, &reactionHandle, sim.reactionGroup_->nThreads());
      L2_BarrierWithSync_WaitAndReset(loopData.diffusionBarrier, &diffusionHandle, sim.diffusionGroup_->nThreads());
   }
}



void simulationLoopParallelDiffusionReaction(Simulate& sim)
{
   SimLoopData loopData(sim);

   simulationProlog(sim);

#if defined(SPI) && defined(TRACESPI)
   int myRank;
   MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
   cout << "Rank[" << myRank << "]: numOfNeighborToSend=" << sim.commTable_->_sendTask.size() << " numOfNeighborToRecv=" << sim.commTable_->_recvTask.size() << " numOfBytesToSend=" << sim.commTable_->_sendOffset[sim.commTable_->_sendTask.size()]*sizeof(double) << " numOfBytesToRecv=" << sim.commTable_->_recvOffset[sim.commTable_->_recvTask.size()]*sizeof(double) << endl;
#endif

   #pragma omp parallel
   {
      int ompTid = omp_get_thread_num();
      
      L2_BarrierHandle_t reactionHandle;
      L2_BarrierHandle_t diffusionHandle;
      L2_BarrierWithSync_InitInThread(loopData.reactionBarrier, &reactionHandle);
      L2_BarrierWithSync_InitInThread(loopData.diffusionBarrier, &diffusionHandle);
      
      #pragma omp barrier
      profileFastStart(parallelDiffReacTimer);
      if ( sim.tinfo_.threadingMap_[ompTid] == sim.diffusionGroup_) 
      {
         diffusionLoop(sim, loopData, reactionHandle, diffusionHandle);
      }
      if ( sim.tinfo_.threadingMap_[ompTid] == sim.reactionGroup_) 
      {
          reactionLoop(sim, loopData, reactionHandle, diffusionHandle);
      } 
      profileFastStop(parallelDiffReacTimer);
   }
}
