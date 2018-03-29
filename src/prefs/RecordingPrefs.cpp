/**********************************************************************

  Audacity: A Digital Audio Editor

  RecordingPrefs.cpp

  Joshua Haberman
  Dominic Mazzoni
  James Crook

*******************************************************************//**

\class RecordingPrefs
\brief A PrefsPanel used to select recording options.

  Presents interface for user to update the various recording options
  like playthrough, latency correction, and others.

*//********************************************************************/

#include "../Audacity.h"
#include "RecordingPrefs.h"

#include <wx/defs.h>
#include <wx/textctrl.h>
#include <algorithm>

#include "../AudioIO.h"
#include "../prefs/GUISettings.h"
#include "../Prefs.h"
#include "../ShuttleGui.h"

#include "../Experimental.h"
#include "../Internat.h"

#include "../widgets/Warning.h"

using std::min;

enum {
   UseCustomTrackNameID = 1000,
};

BEGIN_EVENT_TABLE(RecordingPrefs, PrefsPanel)
   EVT_CHECKBOX(UseCustomTrackNameID, RecordingPrefs::OnToggleCustomName)
END_EVENT_TABLE()

RecordingPrefs::RecordingPrefs(wxWindow * parent, wxWindowID winid)
:  PrefsPanel(parent, winid, _("Recording"))
{
   gPrefs->Read(wxT("/GUI/TrackNames/RecordingNameCustom"), &mUseCustomTrackName, false);
   mOldNameChoice = mUseCustomTrackName;
   Populate();
}

RecordingPrefs::~RecordingPrefs()
{
}

void RecordingPrefs::Populate()
{
   //------------------------- Main section --------------------
   // Now construct the GUI itself.
   // Use 'eIsCreatingFromPrefs' so that the GUI is
   // initialised with values from gPrefs.
   ShuttleGui S(this, eIsCreatingFromPrefs);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------
}

void RecordingPrefs::PopulateOrExchange(ShuttleGui & S)
{
   S.SetBorder(2);
   S.StartScroller();

   S.StartStatic(_("Options"));
   {
      // Start wording of options with a verb, if possible.
      S.TieCheckBox(_("Play &other tracks while recording (overdub)"),
                    wxT("/AudioIO/Duplex"),
                    true);

//#if defined(__WXMAC__)
// Bug 388.  Feature not supported on any Mac Hardware.
      S.TieCheckBox(_("Use &software to play other tracks"),
                    wxT("/AudioIO/SWPlaythrough"),
                    false);
#if !defined(__WXMAC__)
      //S.AddUnits(wxString(wxT("     ")) + _("(uncheck when recording computer playback)"));
#endif

       S.TieCheckBox(_("Record on a new track"),
                    wxT("/GUI/PreferNewTrackRecord"),
                    false);

/* i18n-hint: Dropout is a loss of a short sequence audio sample data from the recording */
       S.TieCheckBox(_("Detect dropouts"),
                     WarningDialogKey(wxT("DropoutDetected")),
                     true);


   }
   S.EndStatic();

   S.StartStatic(_("Sound Activated Recording"));
   {
      S.TieCheckBox(_("&Enable"),
                    wxT("/AudioIO/SoundActivatedRecord"),
                    false);

      S.StartMultiColumn(2, wxEXPAND);
      {
         S.SetStretchyCol(1);

         int dBRange = gPrefs->Read(ENV_DB_KEY, ENV_DB_RANGE);
         S.TieSlider(_("Le&vel (dB):"),
                     wxT("/AudioIO/SilenceLevel"),
                     -50,
                     0,
                     -dBRange);
      }
      S.EndMultiColumn();
   }
   S.EndStatic();

   S.StartStatic(_("Name newly recorded tracks"));
   {
      // Nested multicolumns to indent by 'With:' width, in a way that works if 
      // translated.
      // This extra step is worth doing to get the check boxes lined up nicely.
      S.StartMultiColumn( 2 );
      {
         S.AddFixedText(_("With:")) ;
         S.StartMultiColumn(3);
         {
            S.Id(UseCustomTrackNameID).TieCheckBox(_("Custom Track &Name"),
                                            wxT("/GUI/TrackNames/RecordingNameCustom"),
                                            mUseCustomTrackName ? true : false);

            mToggleCustomName = S.TieTextBox( {},
                                              wxT("/GUI/TrackNames/RecodingTrackName"),
                                             _("Recorded_Audio"),
                                             30);
            if( mToggleCustomName ) {
               mToggleCustomName->SetName(_("Custom name text"));
               mToggleCustomName->Enable(mUseCustomTrackName);
            }
         }
         S.EndMultiColumn();

         S.AddFixedText(  {} );
         S.StartMultiColumn(3);
         {
            S.TieCheckBox(_("&Track Number"),
                          wxT("/GUI/TrackNames/TrackNumber"),
                          false);

            S.TieCheckBox(_("System &Date"),
                          wxT("/GUI/TrackNames/DateStamp"),
                          false);

            S.TieCheckBox(_("System T&ime"),
                          wxT("/GUI/TrackNames/TimeStamp"),
                          false);
         }
         S.EndMultiColumn();
      }
      S.EndMultiColumn();
   }
   S.EndStatic();
   S.EndScroller();
}

bool RecordingPrefs::Commit()
{
   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   double latencyDuration = DEFAULT_LATENCY_DURATION;
   gPrefs->Read(wxT("/AudioIO/LatencyDuration"), &latencyDuration);
   if (latencyDuration < 0) {
      gPrefs->Write(wxT("/AudioIO/LatencyDuration"), DEFAULT_LATENCY_DURATION);
   }
   return true;
}

void RecordingPrefs::OnToggleCustomName(wxCommandEvent & /* Evt */)
{
   mUseCustomTrackName = !mUseCustomTrackName;
   mToggleCustomName->Enable(mUseCustomTrackName);
}

wxString RecordingPrefs::HelpPageName()
{
   return "Recording_Preferences";
}

PrefsPanel *RecordingPrefsFactory::operator () (wxWindow *parent, wxWindowID winid)
{
   wxASSERT(parent); // to justify safenew
   return safenew RecordingPrefs(parent, winid);
}
