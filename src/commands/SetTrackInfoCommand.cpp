/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2018 Audacity Team
   License: wxwidgets

   Dan Horgan
   James Crook

******************************************************************//**

\file SetTrackCommand.cpp
\brief Definitions for SetTrackCommand built up from 
SetTrackBase, SetTrackStatusCommand, SetTrackAudioCommand and
SetTrackVisualsCommand

\class SetTrackBase
\brief Base class for the various SetTrackCommand classes.  
Sbclasses provide the settings that are relevant to them.

\class SetTrackStatusCommand
\brief A SetTrackBase that sets name, selected and focus.

\class SetTrackAudioCommand
\brief A SetTrackBase that sets pan, gain, mute and solo.

\class SetTrackVisualsCommand
\brief A SetTrackBase that sets appearance of a track.

\class SetTrackCommand
\brief A SetTrackBase that combines SetTrackStatusCommand,
SetTrackAudioCommand and SetTrackVisualsCommand.

*//*******************************************************************/

#include "../Audacity.h"
#include "SetTrackInfoCommand.h"
#include "../Project.h"
#include "../Track.h"
#include "../TrackPanel.h"
#include "../WaveTrack.h"
#include "../prefs/WaveformSettings.h"
#include "../ShuttleGui.h"
#include "CommandContext.h"

SetTrackBase::SetTrackBase(){
   mbPromptForTracks = true;
}

bool SetTrackBase::DefineParams( ShuttleParams & S )
{
   return true;
}

void SetTrackBase::PopulateOrExchange(ShuttleGui & S)
{
}

bool SetTrackBase::Apply(const CommandContext & context  )
{
   long i = 0;// track counter
   long j = 0;// channel counter
   TrackListIterator iter(context.GetProject()->GetTracks());
   Track *t = iter.First();
   bIsSecondChannel = false;
   while (t )
   {
      bool bThisTrack =
         t->GetSelected();

      if( bThisTrack ){
         ApplyInner( t );
      }
      bIsSecondChannel = t->GetLinked();
      if( !bIsSecondChannel )
         ++i;
      j++;
      t = iter.Next();
   }
   return true;
}

bool SetTrackStatusCommand::DefineParams( ShuttleParams & S ){ 
   SetTrackBase::DefineParams( S );
   S.OptionalN( bHasTrackName      ).Define(     mTrackName,      wxT("Name"),       wxT("Unnamed") );
   // There is also a select command.  This is an alternative.
   S.OptionalN( bHasSelected       ).Define(     bSelected,       wxT("Selected"),   false );
   S.OptionalN( bHasFocused        ).Define(     bFocused,        wxT("Focused"),    false );
   return true;
};

void SetTrackStatusCommand::PopulateOrExchange(ShuttleGui & S)
{
   SetTrackBase::PopulateOrExchange( S );
   S.StartMultiColumn(3, wxEXPAND);
   {
      S.SetStretchyCol( 2 );
      S.Optional( bHasTrackName   ).TieTextBox(         _("Name:"),          mTrackName );
   }
   S.EndMultiColumn();
   S.StartMultiColumn(2, wxEXPAND);
   {
      S.SetStretchyCol( 1 );
      S.Optional( bHasSelected       ).TieCheckBox( _("Selected"),           bSelected );
      S.Optional( bHasFocused        ).TieCheckBox( _("Focused"),            bFocused);
   }
   S.EndMultiColumn();
}

bool SetTrackStatusCommand::ApplyInner(const CommandContext & context, Track * t )
{
   // You can get some intriguing effects by setting R and L channels to 
   // different values.
   if( bHasTrackName )
      t->SetName(mTrackName);

   // In stereo tracks, both channels need selecting/deselecting.
   if( bHasSelected )
      t->SetSelected(bSelected);

   // These ones don't make sense on the second channel of a stereo track.
   if( !bIsSecondChannel ){
      if( bHasFocused )
      {
         TrackPanel *panel = context.GetProject()->GetTrackPanel();
         if( bFocused)
            panel->SetFocusedTrack( t );
         else if( t== panel->GetFocusedTrack() )
            panel->SetFocusedTrack( nullptr );
      }
   }
   return true;
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


enum kDisplayTypes
{
   kWaveform,
   nDisplayTypes
};

static const wxString kDisplayTypeStrings[nDisplayTypes] =
{
   XO("Waveform"),
};

enum kScaleTypes
{
   kLinear,
   kDb,
   nScaleTypes
};

static const wxString kScaleTypeStrings[nScaleTypes] =
{
   XO("Linear"),
   XO("dB"),
};

enum kZoomTypes
{
   kReset,
   kTimes2,
   kHalfWave,
   nZoomTypes
};

static const wxString kZoomTypeStrings[nZoomTypes] =
{
   XO("Reset"),
   XO("Times2"),
   XO("HalfWave"),
};

bool SetTrackVisualsCommand::DefineParams( ShuttleParams & S ){ 
   wxArrayString colours(  nColours,      kColourStrings );
   wxArrayString displays( nDisplayTypes, kDisplayTypeStrings );
   wxArrayString scales(   nScaleTypes,   kScaleTypeStrings );
   wxArrayString vzooms(   nZoomTypes,    kZoomTypeStrings );

   SetTrackBase::DefineParams( S );
   S.OptionalN( bHasHeight         ).Define(     mHeight,         wxT("Height"),     120, 44, 700 );
   S.OptionalN( bHasDisplayType    ).DefineEnum( mDisplayType,    wxT("Display"),    kWaveform, displays );
   S.OptionalN( bHasScaleType      ).DefineEnum( mScaleType,      wxT("Scale"),      kLinear,   scales );
   S.OptionalN( bHasColour         ).DefineEnum( mColour,         wxT("Color"),      kColour0,  colours );
   S.OptionalN( bHasVZoom          ).DefineEnum( mVZoom,          wxT("VZoom"),      kReset,    vzooms );

   S.OptionalN( bHasUseSpecPrefs   ).Define(     bUseSpecPrefs,   wxT("SpecPrefs"),  false );
   S.OptionalN( bHasSpectralSelect ).Define(     bSpectralSelect, wxT("SpectralSel"),true );
   S.OptionalN( bHasGrayScale      ).Define(     bGrayScale,      wxT("GrayScale"),  false );

   return true;
};

void SetTrackVisualsCommand::PopulateOrExchange(ShuttleGui & S)
{
   auto colours = LocalizedStrings(  kColourStrings, nColours );
   auto displays = LocalizedStrings( kDisplayTypeStrings, nDisplayTypes );
   auto scales = LocalizedStrings( kScaleTypeStrings, nScaleTypes );
   auto vzooms = LocalizedStrings( kZoomTypeStrings, nZoomTypes );

   SetTrackBase::PopulateOrExchange( S );
   S.StartMultiColumn(3, wxEXPAND);
   {
      S.SetStretchyCol( 2 );
      S.Optional( bHasHeight      ).TieNumericTextBox(  _("Height:"),        mHeight );
      S.Optional( bHasColour      ).TieChoice(          _("Colour:"),        mColour,      &colours );
      S.Optional( bHasDisplayType ).TieChoice(          _("Display:"),       mDisplayType, &displays );
      S.Optional( bHasScaleType   ).TieChoice(          _("Scale:"),         mScaleType,   &scales );
      S.Optional( bHasVZoom       ).TieChoice(          _("VZoom:"),         mVZoom,       &vzooms );
   }
   S.EndMultiColumn();
   S.StartMultiColumn(2, wxEXPAND);
   {
      S.SetStretchyCol( 1 );
      S.Optional( bHasUseSpecPrefs   ).TieCheckBox( _("Use Spectral Prefs"), bUseSpecPrefs );
      S.Optional( bHasSpectralSelect ).TieCheckBox( _("Spectral Select"),    bSpectralSelect);
      S.Optional( bHasGrayScale      ).TieCheckBox( _("Gray Scale"),         bGrayScale );
   }
   S.EndMultiColumn();
}

bool SetTrackVisualsCommand::ApplyInner( Track * t )
{
   auto wt = dynamic_cast<WaveTrack *>(t);

   // You can get some intriguing effects by setting R and L channels to 
   // different values.
   if( wt && bHasColour )
      wt->SetWaveColorIndex( mColour );
   if( t && bHasHeight )
      t->SetHeight( mHeight );

   if( wt && bHasDisplayType  )
      wt->SetDisplay(WaveTrack::WaveTrackDisplayValues::Waveform);

   if( wt && bHasScaleType )
      wt->GetIndependentWaveformSettings().scaleType = 
         (mScaleType==kLinear) ? 
            WaveformSettings::stLinear
            : WaveformSettings::stLogarithmic;

   if( wt && bHasVZoom ){
      switch( mVZoom ){
         default:
         case kReset: wt->SetDisplayBounds(-1,1); break;
         case kTimes2: wt->SetDisplayBounds(-2,2); break;
         case kHalfWave: wt->SetDisplayBounds(0,1); break;
      }
   }

   return true;
}


SetTrackCommand::SetTrackCommand()
{
   mSetStatus.mbPromptForTracks = false;
   mSetVisuals.mbPromptForTracks = false;
}

