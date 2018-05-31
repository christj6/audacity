/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportExportPrefs.cpp

  Joshua Haberman
  Dominic Mazzoni
  James Crook

*******************************************************************//**

\class ImportExportPrefs
\brief A PrefsPanel used to select import and export options.

*//*******************************************************************/

#include "../Audacity.h"

#include <wx/defs.h>

#include "../Prefs.h"
#include "../ShuttleGui.h"

#include "ImportExportPrefs.h"
#include "../Internat.h"

ImportExportPrefs::ImportExportPrefs(wxWindow * parent, wxWindowID winid)
:   PrefsPanel(parent, winid, _("Import / Export"))
{
   Populate();
}

ImportExportPrefs::~ImportExportPrefs()
{
}

/// Creates the dialog and its contents.
void ImportExportPrefs::Populate()
{
   //------------------------- Main section --------------------
   // Now construct the GUI itself.
   // Use 'eIsCreatingFromPrefs' so that the GUI is
   // initialised with values from gPrefs.
   ShuttleGui S(this, eIsCreatingFromPrefs);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------
}

void ImportExportPrefs::PopulateOrExchange(ShuttleGui & S)
{
   S.SetBorder(2);
   S.StartScroller();

   S.StartStatic(_("When importing audio files"));
   {
      S.StartRadioButtonGroup(wxT("/FileFormats/CopyOrEditUncompressedData"), wxT("copy"));
      {
         S.TieRadioButton(_("&Copy uncompressed files into the project (safer)"),
                          wxT("copy"));
         S.TieRadioButton(_("&Read uncompressed files from original location (faster)"),
                          wxT("edit"));
      }
      S.EndRadioButtonGroup();

      S.TieCheckBox(_("&Normalize all tracks in project"),
                    wxT("/AudioFiles/NormalizeOnLoad"),
                    false);
   }
   S.EndStatic();

   S.StartStatic(_("When exporting tracks to an audio file"));
   {
      S.StartRadioButtonGroup(wxT("/FileFormats/ExportDownMix"), true);
      {
         S.TieRadioButton(_("&Mix down to Stereo or Mono"),
                          true);
         S.TieRadioButton(_("&Use custom mix"),
                          false);
      }
      S.EndRadioButtonGroup();
   }
   S.EndStatic();
   S.EndScroller();
}

bool ImportExportPrefs::Commit()
{
   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   return true;
}

wxString ImportExportPrefs::HelpPageName()
{
   return "Import_-_Export_Preferences";
}

PrefsPanel *ImportExportPrefsFactory::operator () (wxWindow *parent, wxWindowID winid)
{
   wxASSERT(parent); // to justify safenew
   return safenew ImportExportPrefs(parent, winid);
}
