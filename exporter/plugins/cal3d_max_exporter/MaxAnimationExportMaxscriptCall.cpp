//
// Copyright (C) 2004 Mekensleep
//
// Mekensleep
// 24 rue vieille du temple
// 75004 Paris
//       licensing@mekensleep.com
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//

//----------------------------------------------------------------------------//
// Exporter.cpp                                                               //
//----------------------------------------------------------------------------//
// This program is free software; you can redistribute it and/or modify it    //
// under the terms of the GNU General Public License as published by the Free //
// Software Foundation; either version 2 of the License, or (at your option)  //
// any later version.                                                         //
//----------------------------------------------------------------------------//
//----------------------------------------------------------------------------//
// Includes                                                                   //
//----------------------------------------------------------------------------//

#include "StdAfx.h"
#include "Exporter.h"
#include "MaxInterface.h"
#include "SkeletonCandidate.h"
#include "BoneCandidate.h"
#include "BaseNode.h"
#include "SkeletonExportSheet.h"
#include "AnimationExportSheet.h"
#include "MeshExportSheet.h"
#include "MaterialExportSheet.h"
#include "MeshCandidate.h"
#include "SubmeshCandidate.h"
#include "VertexCandidate.h"
#include "MaterialLibraryCandidate.h"
#include "MaterialCandidate.h"
#include "MaxAnimationExport.h"


bool CMaxInterface::ExportAnimationFromMaxscriptCall(const std::string& strFilename, void* _AnimExportParams)
{
	if (!_AnimExportParams)
	{
		theExporter.SetLastError("_AnimExportParams pointer is null.", __FILE__, __LINE__);
		return false;
	}

	AnimExportParams*	param = reinterpret_cast<AnimExportParams*>(_AnimExportParams);


	// build a skeleton candidate
	CSkeletonCandidate skeletonCandidate;

	//Remove user interface
	/*// show export wizard sheet
	CAnimationExportSheet sheet("Cal3D Animation Export", GetMainWnd());
	sheet.SetSkeletonCandidate(&skeletonCandidate);
	sheet.SetAnimationTime(GetStartFrame(), GetEndFrame(), GetCurrentFrame(), GetFps());
	sheet.SetWizardMode();
	if(sheet.DoModal() != ID_WIZFINISH) return true;
	*/

	//Following block replaces the user interface interactions
	{
		// create the skeleton candidate from the skeleton file
		if(! skeletonCandidate.CreateFromSkeletonFile(param->m_skeletonfilepath))
		{
			AfxMessageBox(theExporter.GetLastError().c_str(), MB_OK | MB_ICONEXCLAMATION);
			return false;
		}	

		//Set all bones in our array of nodes selected
		std::vector<CBoneCandidate *>& vectorBoneCandidate = skeletonCandidate.GetVectorBoneCandidate();

		int NumElemInTabMaxscript = param->m_tabbones.Count();

		// Select bone candidates that are in our array
		int idx = 0;
		const int numelems = vectorBoneCandidate.size();
		for (idx = 0;idx<numelems;idx++)
		{
			CBoneCandidate * bonecandidate = vectorBoneCandidate[idx];
			if (! bonecandidate)return false;

			//Deselect it
			bonecandidate->SetSelected(false);
			
			int j;
			for (j=0;j<NumElemInTabMaxscript;j++)
			{
				std::string bcname		= bonecandidate->GetNode()->GetName();
				std::string	bonename	= param->m_tabbones[j]->GetName();

				if (bcname == bonename)
				{
					//This bone candidate is in the array passed by maxscript, so select it.
					bonecandidate->SetSelected(true);
					break;
				}
			}
		}
	}

	// get the number of selected bone candidates
	int selectedCount;
	selectedCount = skeletonCandidate.GetSelectedCount();
	if(selectedCount == 0)
	{
		theExporter.SetLastError("No bones selected to export.", __FILE__, __LINE__);
		return false;
	}

	// create the core animation instance
	CalCoreAnimation coreAnimation;

	// set the duration of the animation
	float duration;
	bool backward = false;
	duration = (float)(param->m_endframe - param->m_startframe) / (float)GetFps();
	if ( duration < 0 )
	{
		duration = -duration;
		backward = true;
	}
	coreAnimation.setDuration(duration > 0 ? duration : FLT_EPSILON);

	// get bone candidate vector
	std::vector<CBoneCandidate *>& vectorBoneCandidate = skeletonCandidate.GetVectorBoneCandidate();

	size_t boneCandidateId;
	for(boneCandidateId = 0; boneCandidateId < vectorBoneCandidate.size(); boneCandidateId++)
	{
		// get the bone candidate
		CBoneCandidate *pBoneCandidate;
		pBoneCandidate = vectorBoneCandidate[boneCandidateId];

		// only create tracks for the selected bone candidates
		if(pBoneCandidate->IsSelected())
		{
			// allocate new core track instance
			CalCoreTrack *pCoreTrack;
			pCoreTrack = new CalCoreTrack();
			if(pCoreTrack == 0)
			{
				theExporter.SetLastError("Memory allocation failed.", __FILE__, __LINE__);
				StopProgressInfo();
				return false;
			}

			// set the core bone id
			pCoreTrack->setCoreBoneId(boneCandidateId);

			// add the core track to the core animation instance
			coreAnimation.addCoreTrack(pCoreTrack);
		}
	}

	// start the progress info
	StartProgressInfo("Exporting to animation file...");

	// calculate the end frame
	int endFrame;
	endFrame = (int)(duration * (float)param->m_framerate + 0.5f);
	// m_framerate actually is not frame rate but sampling rate,
	// i.e. how often key-frames are taken

	// calculate the displaced frame
    int displacedFrame = 0;
    if ( endFrame > 0 )
    {
	    displacedFrame = (int)(((float)param->m_frameoffset / (float)GetFps())
                               * (float)param->m_framerate + 0.5f) % endFrame;
    }

	// calculate the possible wrap frame
    int wrapFrame;
    wrapFrame = (displacedFrame > 0) ? 1 : 0;
    float wrapTime;
    wrapTime = 0.0f;

    /*CString str;
    str.Format("m_framerate = %f; GetFps() = %f; endFrame = %d", (float)param->m_framerate, (float)GetFps(), endFrame);
    AfxMessageBox( str );*/

    int frame;
    int outputFrame;
    for(frame = 0,  outputFrame = 0; frame <= (endFrame + wrapFrame); frame++)
	{
		// update the progress info
		SetProgressInfo(int(100.0f * (float)frame / (float)(endFrame + wrapFrame + 1)));

		// calculate the time in seconds
		float time;
		time = (float)param->m_startframe / (float)GetFps() +
            (float)(backward ? -displacedFrame : displacedFrame) / (float)param->m_framerate;
 
/* DEBUG
   CString str;
   str.Format("frame=%d, endframe=%d, disframe=%d, ouputFrame=%d (%f), time=%f\n", frame, endFrame, displacedFrame, outputFrame, (float)outputFrame / (float)param->m_framerate + wrapTime, time);
   OutputDebugString(str);
*/

		for(boneCandidateId = 0; boneCandidateId < vectorBoneCandidate.size(); boneCandidateId++)
		{
			// get the bone candidate
			CBoneCandidate *pBoneCandidate;
			pBoneCandidate = vectorBoneCandidate[boneCandidateId];

			// only export keyframes for the selected bone candidates
			if(pBoneCandidate->IsSelected())
			{
				// allocate new core keyframe instance
				CalCoreKeyframe *pCoreKeyframe;
				pCoreKeyframe = new CalCoreKeyframe();
				if(pCoreKeyframe == 0)
				{
					theExporter.SetLastError("Memory allocation failed.", __FILE__, __LINE__);
					StopProgressInfo();
					return false;
				}

				// set the frame time
				pCoreKeyframe->setTime((float)outputFrame / (float)param->m_framerate + wrapTime);

				// get the translation and the rotation of the bone candidate
				CalVector translation;
				CalQuaternion rotation;
				skeletonCandidate.GetTranslationAndRotation(boneCandidateId, time, translation, rotation);

				// set the translation and rotation
				pCoreKeyframe->setTranslation(translation);
				pCoreKeyframe->setRotation(rotation);

				// get the core track for this bone candidate
				CalCoreTrack *pCoreTrack;
				pCoreTrack = coreAnimation.getCoreTrack(pBoneCandidate->GetId());
				if(pCoreTrack == 0)
				{
					theExporter.SetLastError(CalError::getLastErrorText(), __FILE__, __LINE__);
					delete pCoreKeyframe;
					StopProgressInfo();
					return false;
				}

				// add this core keyframe to the core track
				pCoreTrack->addCoreKeyframe(pCoreKeyframe);
			}
		}

        // calculate the next displaced frame and its frame time
        if(wrapFrame > 0)
        {
            if(displacedFrame == endFrame)
            {
                wrapTime = 0.0001f;
                displacedFrame = 0;
            }
            else
            {
                wrapTime = 0.0f;
                outputFrame++;
                displacedFrame++;
            }
        }
        else
        {
            outputFrame++;
            displacedFrame++;
        }
	}

	// stop the progress info
	StopProgressInfo();

	// save core animation to the file
	if(!CalSaver::saveCoreAnimation(strFilename, &coreAnimation))
	{
		theExporter.SetLastError(CalError::getLastErrorText(), __FILE__, __LINE__);
		return false;
	}

	return true;
}
