/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectrumPrefs.h

  Dominic Mazzoni
  James Crook

**********************************************************************/
/*
  Salvo Ventura
  November 2006

  Added selection box for windowType

  All params are saved in config file.
*/


#ifndef __AUDACITY_SPECTRUM_PREFS__
#define __AUDACITY_SPECTRUM_PREFS__

#include <wx/defs.h>
#include <wx/string.h>
#include <wx/window.h>

#include "../Experimental.h"
#include "../WaveTrack.h"

#include "PrefsPanel.h"
#include "SpectrogramSettings.h"

class wxChoice;
class wxCheckBox;
class wxTextCtrl;
struct FFTParam;
class ShuttleGui;
class SpectrogramSettings;
class WaveTrack;

class SpectrumPrefs final : public PrefsPanel
{
 public:
   SpectrumPrefs(wxWindow * parent, wxWindowID winid, WaveTrack *wt);
   virtual ~SpectrumPrefs();
   void Preview() override;
   bool Commit() override;
   void PopulateOrExchange(ShuttleGui & S) override;
   void Rollback();
   bool ShowsPreviewButton() override;
   bool Validate() override;
   wxString HelpPageName() override;

 private:
   void Populate();

   void OnControl(wxCommandEvent &event);
   void OnWindowSize(wxCommandEvent &event);
   void OnDefaults(wxCommandEvent&);
   void OnAlgorithm(wxCommandEvent &);
   DECLARE_EVENT_TABLE()

   WaveTrack *const mWt;
   bool mDefaulted, mOrigDefaulted;

   wxTextCtrl *mMinFreq;
   wxTextCtrl *mMaxFreq;
   wxTextCtrl *mGain;
   wxTextCtrl *mRange;
   wxTextCtrl *mFrequencyGain;

   wxArrayString mSizeChoices;

   wxArrayString mTypeChoices;
   wxArrayString mScaleChoices;

   wxChoice *mAlgorithmChoice;
   wxArrayString mAlgorithmChoices;

   wxCheckBox *mDefaultsCheckbox;

   SpectrogramSettings mTempSettings, mOrigSettings;

   WaveTrack::WaveTrackDisplay mOrigDisplay;
   float mOrigMin, mOrigMax;

   bool mPopulating;
   bool mCommitted{};
};

class SpectrumPrefsFactory final : public PrefsPanelFactory
{
public:
   explicit SpectrumPrefsFactory(WaveTrack *wt = 0);
   PrefsPanel *operator () (wxWindow *parent, wxWindowID winid) override;

private:
   WaveTrack *const mWt;
};
#endif
