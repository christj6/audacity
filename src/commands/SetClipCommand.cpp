/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2018 Audacity Team
   License: wxwidgets

   James Crook

******************************************************************//**

\file SetClipCommand.cpp
\brief Definitions for SetClipCommand

\class SetClipCommand
\brief Command that sets clip information

*//*******************************************************************/

#include "../Audacity.h"
#include "SetClipCommand.h"
#include "../Project.h"
#include "../Track.h"
#include "../TrackPanel.h"
#include "../WaveTrack.h"
#include "../ShuttleGui.h"
#include "CommandContext.h"

SetClipCommand::SetClipCommand()
{
}

enum kColours
{
   kColour0,
   kColour1,
   kColour2,
   kColour3,
   nColours
};

static const wxString kColourStrings[nColours] =
{
   XO("Color0"),
   XO("Color1"),
   XO("Color2"),
   XO("Color3"),
};


bool SetClipCommand::DefineParams( ShuttleParams & S ){ 
   wxArrayString colours( nColours, kColourStrings );
   S.OptionalY( bHasContainsTime   ).Define(     mContainsTime,   wxT("At"),         0.0, 0.0, 100000.0 );
   S.OptionalN( bHasColour         ).DefineEnum( mColour,         wxT("Color"),      kColour0, colours );
   // Allowing a negative start time is not a mistake.
   // It will be used in demonstrating time before zero.
   S.OptionalN( bHasT0             ).Define(     mT0,             wxT("Start"),      0.0, -5.0, 1000000.0);
   return true;
};

void SetClipCommand::PopulateOrExchange(ShuttleGui & S)
{
   auto colours = LocalizedStrings( kColourStrings, nColours );

   S.AddSpace(0, 5);

   S.StartMultiColumn(3, wxALIGN_CENTER);
   {
      S.Optional( bHasContainsTime).TieNumericTextBox(  _("At:"),            mContainsTime );
      S.Optional( bHasColour      ).TieChoice(          _("Colour:"),        mColour, &colours );
      S.Optional( bHasT0          ).TieNumericTextBox(  _("Start:"),         mT0 );
   }
   S.EndMultiColumn();
}

bool SetClipCommand::ApplyInner( Track * t )
{
   if( t->GetKind() != Track::Wave) 
      return true;
   
   WaveTrack *waveTrack = static_cast<WaveTrack*>(t);
   wxASSERT( waveTrack );
   WaveClipPointers ptrs( waveTrack->SortedClipArray());
   for(auto it = ptrs.begin(); (it != ptrs.end()); it++ ){
      WaveClip * pClip = *it;
      bool bFound = 
         !bHasContainsTime || (
            ( pClip->GetStartTime() <= mContainsTime ) &&
            ( pClip->GetEndTime() >= mContainsTime )
         );
      if( bFound )
      {
         // Inside this IF is where we actually apply the command

         if( bHasColour )
            pClip->SetColourIndex(mColour);
         // No validation of overlap yet.  We assume the user is sensible!
         if( bHasT0 )
            pClip->SetOffset(mT0);
         // \todo Use SetClip to move a clip between tracks too.

      }
   }
   return true;
}
