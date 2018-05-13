/**********************************************************************

  Audacity: A Digital Audio Editor

  SpectrumPrefs.cpp

  Dominic Mazzoni
  James Crook

*******************************************************************//**

\class SpectrumPrefs
\brief A PrefsPanel for spectrum settings.

*//*******************************************************************/

#include "../Audacity.h"
#include "SpectrumPrefs.h"

#include <wx/defs.h>
#include <wx/intl.h>
#include <wx/checkbox.h>

#include "../FFT.h"
#include "../Project.h"
#include "../ShuttleGui.h"
#include "../WaveTrack.h"

#include "../TrackPanel.h"

#include <algorithm>

#include "../Experimental.h"
#include "../widgets/ErrorDialog.h"

SpectrumPrefs::SpectrumPrefs(wxWindow * parent, wxWindowID winid, WaveTrack *wt)
:  PrefsPanel(parent, winid, wt ? _("Spectrogram Settings") : _("Spectrograms"))
, mWt(wt)
, mPopulating(false)
{
   if (mWt) {
      SpectrogramSettings &settings = wt->GetSpectrogramSettings();
      mOrigDefaulted = mDefaulted = (&SpectrogramSettings::defaults() == &settings);
      mTempSettings = mOrigSettings = settings;
      wt->GetSpectrumBounds(&mOrigMin, &mOrigMax);
      mTempSettings.maxFreq = mOrigMax;
      mTempSettings.minFreq = mOrigMin;
      mOrigDisplay = mWt->GetDisplay();
   }
   else  {
      mTempSettings = mOrigSettings = SpectrogramSettings::defaults();
      mOrigDefaulted = mDefaulted = false;
   }

   mTempSettings.ConvertToEnumeratedWindowSizes();
   Populate();
}

SpectrumPrefs::~SpectrumPrefs()
{
   if (!mCommitted)
      Rollback();
}

enum {
   ID_WINDOW_SIZE = 10001,

   ID_WINDOW_TYPE,
   ID_PADDING_SIZE,
   ID_SCALE,
   ID_ALGORITHM,
   ID_MINIMUM,
   ID_MAXIMUM,
   ID_GAIN,
   ID_RANGE,
   ID_FREQUENCY_GAIN,
   ID_GRAYSCALE,
   ID_SPECTRAL_SELECTION,

   ID_DEFAULTS,
};

void SpectrumPrefs::Populate()
{
   mSizeChoices.Add(_("8 - most wideband"));
   mSizeChoices.Add(wxT("16"));
   mSizeChoices.Add(wxT("32"));
   mSizeChoices.Add(wxT("64"));
   mSizeChoices.Add(wxT("128"));
   mSizeChoices.Add(wxT("256"));
   mSizeChoices.Add(wxT("512"));
   mSizeChoices.Add(_("1024 - default"));
   mSizeChoices.Add(wxT("2048"));
   mSizeChoices.Add(wxT("4096"));
   mSizeChoices.Add(wxT("8192"));
   mSizeChoices.Add(wxT("16384"));
   mSizeChoices.Add(_("32768 - most narrowband"));
   wxASSERT(mSizeChoices.size() == SpectrogramSettings::NumWindowSizes);

   for (int i = 0; i < NumWindowFuncs(); i++) {
      mTypeChoices.Add(WindowFuncName(i));
   }

   mScaleChoices = SpectrogramSettings::GetScaleNames();

   mAlgorithmChoices = SpectrogramSettings::GetAlgorithmNames();

   //------------------------- Main section --------------------
   // Now construct the GUI itself.
   ShuttleGui S(this, eIsCreatingFromPrefs);
   PopulateOrExchange(S);
   // ----------------------- End of main section --------------
}

void SpectrumPrefs::PopulateOrExchange(ShuttleGui & S)
{
   mPopulating = true;
   S.SetBorder(2);
   S.StartScroller(); {

   mDefaultsCheckbox = 0;
   if (mWt) {
      /* i18n-hint: use is a verb */
      mDefaultsCheckbox = S.Id(ID_DEFAULTS).TieCheckBox(_("&Use Preferences"), mDefaulted);
   }

   S.StartStatic(_("Scale"));
   {
      S.StartTwoColumn();
      {
         S.Id(ID_SCALE).TieChoice(_("S&cale") + wxString(wxT(":")),
            mTempSettings.scaleType,
            &mScaleChoices);

         mMinFreq =
            S.Id(ID_MINIMUM).TieNumericTextBox(_("Mi&nimum Frequency (Hz):"),
            mTempSettings.minFreq,
            12);

         mMaxFreq =
            S.Id(ID_MAXIMUM).TieNumericTextBox(_("Ma&ximum Frequency (Hz):"),
            mTempSettings.maxFreq,
            12);
      }
      S.EndTwoColumn();
   }
   S.EndStatic();

   S.StartStatic(_("Colors"));
   {
      S.StartTwoColumn();
      {
         mGain =
            S.Id(ID_GAIN).TieNumericTextBox(_("&Gain (dB):"),
            mTempSettings.gain,
            8);

         mRange =
            S.Id(ID_RANGE).TieNumericTextBox(_("&Range (dB):"),
            mTempSettings.range,
            8);

         mFrequencyGain =
            S.Id(ID_FREQUENCY_GAIN).TieNumericTextBox(_("Frequency g&ain (dB/dec):"),
            mTempSettings.frequencyGain,
            4);
      }

      S.Id(ID_GRAYSCALE).TieCheckBox(_("Gra&yscale"),
         mTempSettings.isGrayscale);

      S.EndTwoColumn();
   }
   S.EndStatic();

   S.StartStatic(_("Algorithm"));
   {
      S.StartMultiColumn(2);
      {
         mAlgorithmChoice =
            S.Id(ID_ALGORITHM).TieChoice(_("A&lgorithm") + wxString(wxT(":")),
            mTempSettings.algorithm,
            &mAlgorithmChoices);

         S.Id(ID_WINDOW_SIZE).TieChoice(_("Window &size:"),
            mTempSettings.windowSize,
            &mSizeChoices);

         S.Id(ID_WINDOW_TYPE).TieChoice(_("Window &type:"),
            mTempSettings.windowType,
            &mTypeChoices);
      }
      S.EndMultiColumn();
   }
   S.EndStatic();

#ifndef SPECTRAL_SELECTION_GLOBAL_SWITCH
   S.Id(ID_SPECTRAL_SELECTION).TieCheckBox(_("Ena&ble Spectral Selection"),
      mTempSettings.spectralSelection);
#endif

#ifdef SPECTRAL_SELECTION_GLOBAL_SWITCH
   S.StartStatic(_("Global settings"));
   {
      S.TieCheckBox(_("Ena&ble spectral selection"),
         SpectrogramSettings::Globals::Get().spectralSelection);
   }
   S.EndStatic();
#endif

   } S.EndScroller();
   
   // Enabling and disabling belongs outside this function.
   if( S.GetMode() != eIsGettingMetadata )
      EnableDisableSTFTOnlyControls();

   mPopulating = false;
}

bool SpectrumPrefs::Validate()
{
   // Do checking for whole numbers

   // ToDo: use wxIntegerValidator<unsigned> when available

   long maxFreq;
   if (!mMaxFreq->GetValue().ToLong(&maxFreq)) {
      AudacityMessageBox(_("The maximum frequency must be an integer"));
      return false;
   }

   long minFreq;
   if (!mMinFreq->GetValue().ToLong(&minFreq)) {
      AudacityMessageBox(_("The minimum frequency must be an integer"));
      return false;
   }

   long gain;
   if (!mGain->GetValue().ToLong(&gain)) {
      AudacityMessageBox(_("The gain must be an integer"));
      return false;
   }

   long range;
   if (!mRange->GetValue().ToLong(&range)) {
      AudacityMessageBox(_("The range must be a positive integer"));
      return false;
   }

   long frequencygain;
   if (!mFrequencyGain->GetValue().ToLong(&frequencygain)) {
      AudacityMessageBox(_("The frequency gain must be an integer"));
      return false;
   }

   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);

   // Delegate range checking to SpectrogramSettings class
   mTempSettings.ConvertToActualWindowSizes();
   const bool result = mTempSettings.Validate(false);
   mTempSettings.ConvertToEnumeratedWindowSizes();
   return result;
}

void SpectrumPrefs::Rollback()
{
   const auto partner =
      mWt ?
            // Assume linked track is wave or null
            static_cast<WaveTrack*>(mWt->GetLink())
          : nullptr;

   if (mWt) {
      if (mOrigDefaulted) {
         mWt->SetSpectrogramSettings({});
         mWt->SetSpectrumBounds(-1, -1);
         if (partner) {
            partner->SetSpectrogramSettings({});
            partner->SetSpectrumBounds(-1, -1);
         }
      }
      else {
         SpectrogramSettings *pSettings =
            &mWt->GetIndependentSpectrogramSettings();
         mWt->SetSpectrumBounds(mOrigMin, mOrigMax);
         *pSettings = mOrigSettings;
         if (partner) {
            pSettings = &partner->GetIndependentSpectrogramSettings();
            partner->SetSpectrumBounds(mOrigMin, mOrigMax);
            *pSettings = mOrigSettings;
         }
      }
   }

   if (!mWt || mOrigDefaulted) {
      SpectrogramSettings *const pSettings = &SpectrogramSettings::defaults();
      *pSettings = mOrigSettings;
   }

   const bool isOpenPage = this->IsShown();
   if (mWt && isOpenPage) {
      mWt->SetDisplay(mOrigDisplay);
      if (partner)
         partner->SetDisplay(mOrigDisplay);
   }

   if (isOpenPage) {
      TrackPanel *const tp = ::GetActiveProject()->GetTrackPanel();
      tp->UpdateVRulers();
      tp->Refresh(false);
   }
}

void SpectrumPrefs::Preview()
{
   if (!Validate())
      return;

   const bool isOpenPage = this->IsShown();

   const auto partner =
      mWt ?
            // Assume linked track is wave or null
            static_cast<WaveTrack*>(mWt->GetLink())
          : nullptr;

   ShuttleGui S(this, eIsSavingToPrefs);
   PopulateOrExchange(S);


   mTempSettings.ConvertToActualWindowSizes();

   if (mWt) {
      if (mDefaulted) {
         mWt->SetSpectrogramSettings({});
         // ... and so that the vertical scale also defaults:
         mWt->SetSpectrumBounds(-1, -1);
         if (partner) {
            partner->SetSpectrogramSettings({});
            partner->SetSpectrumBounds(-1, -1);
         }
      }
      else {
         SpectrogramSettings *pSettings =
            &mWt->GetIndependentSpectrogramSettings();
         mWt->SetSpectrumBounds(mTempSettings.minFreq, mTempSettings.maxFreq);
         *pSettings = mTempSettings;
         if (partner) {
            pSettings = &partner->GetIndependentSpectrogramSettings();
            partner->SetSpectrumBounds(mTempSettings.minFreq, mTempSettings.maxFreq);
            *pSettings = mTempSettings;
         }
      }
   }

   if (!mWt || mDefaulted) {
      SpectrogramSettings *const pSettings = &SpectrogramSettings::defaults();
      *pSettings = mTempSettings;
   }
   mTempSettings.ConvertToEnumeratedWindowSizes();

   if (mWt && isOpenPage) {
      mWt->SetDisplay(WaveTrack::Spectrum);
      if (partner)
         partner->SetDisplay(WaveTrack::Spectrum);
   }

   if (isOpenPage) {
      TrackPanel *const tp = ::GetActiveProject()->GetTrackPanel();
      tp->UpdateVRulers();
      tp->Refresh(false);
   }
}

bool SpectrumPrefs::Commit()
{
   if (!Validate())
      return false;

   mCommitted = true;
   SpectrogramSettings::Globals::Get().SavePrefs(); // always
   if (!mWt || mDefaulted) {
      SpectrogramSettings *const pSettings = &SpectrogramSettings::defaults();
      pSettings->SavePrefs();
   }

   return true;
}

bool SpectrumPrefs::ShowsPreviewButton()
{
   return true;
}

void SpectrumPrefs::OnControl(wxCommandEvent&)
{
   // Common routine for most controls
   // If any per-track setting is changed, break the association with defaults
   // Skip this, and View Settings... will be able to change defaults instead
   // when the checkbox is on, as in the original design.

   if (mDefaultsCheckbox && !mPopulating) {
      mDefaulted = false;
      mDefaultsCheckbox->SetValue(false);
   }
}

void SpectrumPrefs::OnWindowSize(wxCommandEvent &evt)
{
   // Restrict choice of zero padding, so that product of window
   // size and padding may not exceed the largest window size.
   wxChoice *const pWindowSizeControl =
      static_cast<wxChoice*>(wxWindow::FindWindowById(ID_WINDOW_SIZE, this));

   // Do the common part
   OnControl(evt);
}

void SpectrumPrefs::OnDefaults(wxCommandEvent &)
{
   if (mDefaultsCheckbox->IsChecked()) {
      mTempSettings = SpectrogramSettings::defaults();
      mTempSettings.ConvertToEnumeratedWindowSizes();
      mDefaulted = true;
      ShuttleGui S(this, eIsSettingToDialog);
      PopulateOrExchange(S);
   }
}

void SpectrumPrefs::OnAlgorithm(wxCommandEvent &evt)
{
   EnableDisableSTFTOnlyControls();
   OnControl(evt);
}

void SpectrumPrefs::EnableDisableSTFTOnlyControls()
{
   // Enable or disable other controls that are applicable only to STFT.
   const bool STFT =
      (mAlgorithmChoice->GetSelection() != SpectrogramSettings::algPitchEAC);
   mGain->Enable(STFT);
   mRange->Enable(STFT);
   mFrequencyGain->Enable(STFT);
}

wxString SpectrumPrefs::HelpPageName()
{
   return "Spectrograms_Preferences";
}

BEGIN_EVENT_TABLE(SpectrumPrefs, PrefsPanel)
   EVT_CHOICE(ID_WINDOW_SIZE, SpectrumPrefs::OnWindowSize)
   EVT_CHECKBOX(ID_DEFAULTS, SpectrumPrefs::OnDefaults)
   EVT_CHOICE(ID_ALGORITHM, SpectrumPrefs::OnAlgorithm)

   // Several controls with common routine that unchecks the default box
   EVT_CHOICE(ID_WINDOW_TYPE, SpectrumPrefs::OnControl)
   EVT_CHOICE(ID_PADDING_SIZE, SpectrumPrefs::OnControl)
   EVT_CHOICE(ID_SCALE, SpectrumPrefs::OnControl)
   EVT_TEXT(ID_MINIMUM, SpectrumPrefs::OnControl)
   EVT_TEXT(ID_MAXIMUM, SpectrumPrefs::OnControl)
   EVT_TEXT(ID_GAIN, SpectrumPrefs::OnControl)
   EVT_TEXT(ID_RANGE, SpectrumPrefs::OnControl)
   EVT_TEXT(ID_FREQUENCY_GAIN, SpectrumPrefs::OnControl)
   EVT_CHECKBOX(ID_GRAYSCALE, SpectrumPrefs::OnControl)
   EVT_CHECKBOX(ID_SPECTRAL_SELECTION, SpectrumPrefs::OnControl)

END_EVENT_TABLE()

SpectrumPrefsFactory::SpectrumPrefsFactory(WaveTrack *wt)
: mWt(wt)
{
}

PrefsPanel *SpectrumPrefsFactory::operator () (wxWindow *parent, wxWindowID winid)
{
   wxASSERT(parent); // to justify safenew
   return safenew SpectrumPrefs(parent, winid, mWt);
}
