/*=========================================================================

  Program:   ParaView
  Module:    vtkPVCaveRenderModule.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include <sys/stat.h>
#include "vtkPVCaveRenderModule.h"
#include "vtkToolkits.h"
#include "vtkObjectFactory.h"
#include "vtkRenderWindow.h"
#include "vtkPVApplication.h"
#include "vtkPVProcessModule.h"
#include "vtkPVMultiDisplayPartDisplay.h"
#include "vtkPVLODPartDisplayInformation.h"
#include "vtkCollection.h"
#include "vtkRenderer.h"
#include "vtkTimerLog.h"
#include "vtkClientServerStream.h"
#include "vtkCaveRenderManager.h"
#include "vtkMPIMToNSocketConnection.h"

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVCaveRenderModule);
vtkCxxRevisionMacro(vtkPVCaveRenderModule, "1.1");



//***************************************************************************
//===========================================================================

//----------------------------------------------------------------------------
vtkPVCaveRenderModule::vtkPVCaveRenderModule()
{
}

//----------------------------------------------------------------------------
vtkPVCaveRenderModule::~vtkPVCaveRenderModule()
{

}


//----------------------------------------------------------------------------
void vtkPVCaveRenderModule::SetPVApplication(vtkPVApplication *pvApp)
{
  this->Superclass::SetPVApplication(pvApp);
  if (pvApp == NULL)
    {
    return;
    }
  vtkPVProcessModule* pm = pvApp->GetProcessModule();
  int numDisplays;

  // We had trouble with SGI/aliasing with compositing.
  if (this->RenderWindow->IsA("vtkOpenGLRenderWindow") &&
      (pvApp->GetProcessModule()->GetNumberOfPartitions() > 1))
    {
    pm->GetStream() << vtkClientServerStream::Invoke
                    << this->RenderWindowID 
                    << "SetMultiSamples" << 0 
                    << vtkClientServerStream::End;
    }

  this->Composite = NULL;
  this->CompositeID = pm->NewStreamObject("vtkCaveRenderManager");
  pm->SendStreamToClientAndRenderServer();

  if (pvApp->GetClientMode())
    {
    pm->GetStream()
      << vtkClientServerStream::Invoke
      << this->CompositeID << "SetClientFlag" << 1
      << vtkClientServerStream::End;
    pm->SendStreamToClient();

    pm->GetStream()
      << vtkClientServerStream::Invoke
      << pm->GetProcessModuleID() << "GetRenderServerSocketController"
      << vtkClientServerStream::End;
    pm->GetStream()
      << vtkClientServerStream::Invoke
      << this->CompositeID << "SetSocketController"
      << vtkClientServerStream::LastResult
      << vtkClientServerStream::End;
    pm->SendStreamToClientAndRenderServer();

    // Timing of this set is important for server.
    // It has to be after the SocketController has been set,
    // but before the Desplays are defined.
    pm->GetStream()
      << vtkClientServerStream::Invoke
      << this->CompositeID << "InitializeRMIs"
      << vtkClientServerStream::End;
    pm->SendStreamToClientAndRenderServer();

    // Setup the tiles.
    // We need a better way to retreive the number of processes
    numDisplays = pm->GetNumberOfPartitions();
    vtkMPIMToNSocketConnection* m2n = NULL;
    if (pm->GetMPIMToNSocketConnectionID().ID)
      {
      m2n = vtkMPIMToNSocketConnection::SafeDownCast(
        pm->GetObjectFromID(pm->GetMPIMToNSocketConnectionID()));
      }   
   if (m2n)
      {
      numDisplays = m2n->GetNumberOfConnections();
      }    

    this->LoadConfigurationFile(numDisplays);
    }
  else
    { // Timing of this set is important for server.
    pm->GetStream()
      << vtkClientServerStream::Invoke
      << this->CompositeID << "InitializeRMIs"
      << vtkClientServerStream::End;
    pm->SendStreamToClientAndRenderServer();
    }

  pm->GetStream() 
    << vtkClientServerStream::Invoke
    <<  this->CompositeID 
    << "SetRenderWindow"
    << this->RenderWindowID
    << vtkClientServerStream::End;
  pm->SendStreamToClientAndRenderServer();
}

//----------------------------------------------------------------------------
void vtkPVCaveRenderModule::LoadConfigurationFile(int numDisplays)
{
  int idx;
  const char* fileName = this->PVApplication->GetCaveConfigurationFileName();
  ifstream *File;
  if(!fileName)
    {
    vtkErrorMacro("Missing configuration file.");
    return;
    } 
  
  // Open the new file
  struct stat fs;
  if ( !stat( fileName, &fs) )
    {
#ifdef _WIN32
    File = new ifstream(fileName, ios::in | ios::binary);
#else
    File = new ifstream(fileName, ios::in);
#endif
    }
  if (! File)
    {
    vtkErrorMacro(<< "Initialize: Could not open file " << fileName);
    return;
    }

  if (File->fail())
    {
    File->close();
    delete File;
    vtkErrorMacro(<< "Initialize: Could not open file " << fileName);
    return;
    }

  vtkPVApplication* pvApp = this->PVApplication;
  vtkPVProcessModule* pm = pvApp->GetProcessModule();
  vtkCaveRenderManager* crm = 
    vtkCaveRenderManager::SafeDownCast(pm->GetObjectFromID(this->CompositeID));

  for (idx = 0; idx < numDisplays; ++idx)
    { // Just a test case.  Configuration file later.
    double o[3];
    double x[3];
    double y[3];

    *File >> o[0];
    *File >> o[1];
    *File >> o[2];

    *File >> x[0];
    *File >> x[1];
    *File >> x[2];

    *File >> y[0];
    *File >> y[1];
    *File >> y[2];

    if (File->fail())
      {
      File->close();
      delete File;
      vtkErrorMacro("Unexpected end of configuration file.");
      return;
      }

    crm->DefineDisplay(idx, o, x, y);
    }
  File->close();
  delete File;
}

//----------------------------------------------------------------------------
vtkPVPartDisplay* vtkPVCaveRenderModule::CreatePartDisplay()
{
  vtkPVMultiDisplayPartDisplay* pd = vtkPVMultiDisplayPartDisplay::New();

  return pd;
}


//----------------------------------------------------------------------------
void vtkPVCaveRenderModule::StillRender()
{
  unsigned long totalMemory = 0;
  vtkObject* object;
  vtkPVCompositePartDisplay* pDisp;

  // Change the collection flags and update.
  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVCompositePartDisplay::SafeDownCast(object);
    if (pDisp && pDisp->GetVisibility())
      {
      pDisp->SetCollectionDecision(1);
      pDisp->SetLODCollectionDecision(1);
      pDisp->Update();
      }
    }

  // Still Render can get called some funky ways.
  // Interactive renders get called through the PVInteractorStyles
  // which cal ResetCameraClippingRange on the Renderer.
  // We could convert them to call a method on the module directly ...
  this->Renderer->ResetCameraClippingRange();

  this->RenderWindow->SetDesiredUpdateRate(0.002);
  // this->GetPVWindow()->GetInteractor()->GetStillUpdateRate());

  this->GetPVApplication()->SetGlobalLODFlag(0);


  vtkTimerLog::MarkStartEvent("Still Render");
  this->RenderWindow->Render();
  vtkTimerLog::MarkEndEvent("Still Render");
}


//----------------------------------------------------------------------------
void vtkPVCaveRenderModule::InteractiveRender()
{
  vtkObject* object;
  vtkPVCompositePartDisplay* pDisp;
  vtkPVLODPartDisplayInformation* info;
  unsigned long totalGeoMemory = 0;
  unsigned long totalLODMemory = 0;

  // Compute memory totals.
  this->Displays->InitTraversal();
  while ( (object = this->Displays->GetNextItemAsObject()) )
    {
    pDisp = vtkPVCompositePartDisplay::SafeDownCast(object);
    if (pDisp && pDisp->GetVisibility())
      {
      pDisp->SetCollectionDecision(1);
      pDisp->SetLODCollectionDecision(1);
      // This updates if required (collection disabled).
      info = pDisp->GetInformation();
      totalGeoMemory += info->GetGeometryMemorySize();
      totalLODMemory += info->GetLODGeometryMemorySize();
      }
    }

  // Make LOD decision.
  if ((float)(totalGeoMemory)/1000.0 < this->GetLODThreshold())
    {
    this->GetPVApplication()->SetGlobalLODFlag(0);
    }
  else
    {
    this->GetPVApplication()->SetGlobalLODFlag(1);
    }

  // This might be used for Reduction factor.
  this->RenderWindow->SetDesiredUpdateRate(5.0);
  // this->GetPVWindow()->GetInteractor()->GetStillUpdateRate());

  // Still Render can get called some funky ways.
  // Interactive renders get called through the PVInteractorStyles
  // which cal ResetCameraClippingRange on the Renderer.
  // We could convert them to call a method on the module directly ...
  // Reset camera clipping range has to be called after
  // LOD flag is set, other wise, the wrong bounds could be used.
  this->Renderer->ResetCameraClippingRange();
  
  vtkTimerLog::MarkStartEvent("Interactive Render");
  this->RenderWindow->Render();
  vtkTimerLog::MarkEndEvent("Interactive Render");
}


//----------------------------------------------------------------------------
void vtkPVCaveRenderModule::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

