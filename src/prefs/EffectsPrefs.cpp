/**********************************************************************

  Audacity: A Digital Audio Editor

  EffectsPrefs.cpp

  Brian Gunlogson
  Joshua Haberman
  Dominic Mazzoni
  James Crook


*******************************************************************//**

\class EffectsPrefs
\brief A PrefsPanel for general GUI prefernces.

*//*******************************************************************/

#include "../Audacity.h"

#include <wx/choice.h>
#include <wx/defs.h>

#include "../Languages.h"
#include "../Prefs.h"
#include "../ShuttleGui.h"

#include "EffectsPrefs.h"

#include "../Experimental.h"
#include "../Internat.h"

EffectsPrefs::EffectsPrefs(wxWindow * parent, wxWindowID winid)
:  PrefsPanel(parent, winid, _("Effects"))
{
   Populate();
}

EffectsPrefs::~EffectsPrefs()
{
}

void EffectsPrefs::Populate()
{
   //------------------------- Main section --------------------
   // Now construct the GUI itself.
   // Use 'eIsCreatingFromPrefs' so that the GUI is
   // initialised with values from gPrefs.
   ShuttleGui S(this, eIsCreatingFromPrefs);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------
}

void EffectsPrefs::PopulateOrExchange(ShuttleGui & S)
{
   S.SetBorder(2);
   S.StartScroller();

   S.StartStatic(_("Enable Effects"));
   {

#if USE_AUDIO_UNITS
      S.TieCheckBox(_("Audio Unit"),
                    wxT("/AudioUnit/Enable"),
                    true);
#endif
   }
   S.EndStatic();

   S.StartStatic(_("Effect Options"));
   {
      S.StartMultiColumn(2);
      {
         wxArrayString visualgroups;
         wxArrayString prefsgroups;

         visualgroups.Add(_("Sorted by Effect Name"));
         visualgroups.Add(_("Sorted by Publisher and Effect Name"));
         visualgroups.Add(_("Sorted by Type and Effect Name"));
         visualgroups.Add(_("Grouped by Publisher"));
         visualgroups.Add(_("Grouped by Type"));

         prefsgroups.Add(wxT("sortby:name"));
         prefsgroups.Add(wxT("sortby:publisher:name"));
         prefsgroups.Add(wxT("sortby:type:name"));
         prefsgroups.Add(wxT("groupby:publisher"));
         prefsgroups.Add(wxT("groupby:type"));

         wxChoice *c = S.TieChoice(_("S&ort or Group:"),
                                   wxT("/Effects/GroupBy"),
                                   wxT("name"),
                                   visualgroups,
                                   prefsgroups);
         if( c ) c->SetMinSize(c->GetBestSize());

         S.TieNumericTextBox(_("&Maximum effects per group (0 to disable):"),
                             wxT("/Effects/MaxPerGroup"),
#if defined(__WXGTK__)
                             15,
#else
                             0,
#endif
                             5);
      }
      S.EndMultiColumn();
   }
   S.EndStatic();
   S.EndScroller();
}

bool EffectsPrefs::Commit()
{
   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   return true;
}

wxString EffectsPrefs::HelpPageName()
{
   return "Effects_Preferences";
}

PrefsPanel *EffectsPrefsFactory::operator () (wxWindow *parent, wxWindowID winid)
{
   wxASSERT(parent); // to justify safenew
   return safenew EffectsPrefs(parent, winid);
}
