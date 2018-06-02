/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportRaw.cpp

  Dominic Mazzoni

*******************************************************************//**

\file ImportRaw.cpp
\brief Functions for guessing audio type and attempting to read from
unknown sample audio data.  Implements ImportRawDialog.

*//****************************************************************//**

\class ImportRawDialog
\brief ImportRawDialog prompts you with options such as endianness
and sample size to help you importing data of an unknown format.

*//*******************************************************************/


#include "../Audacity.h"
#include "ImportRaw.h"

#include "Import.h"

#include "../DirManager.h"
#include "../FileException.h"
#include "../FileFormats.h"
#include "../Internat.h"
#include "../Prefs.h"
#include "../ShuttleGui.h"
#include "../UserException.h"
#include "../WaveTrack.h"

#include <cmath>
#include <cstdio>
#include <stdint.h>
#include <vector>

#include <wx/crt.h>
#include <wx/defs.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/intl.h>
#include <wx/panel.h>
#include <wx/progdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "MultiFormatReader.h"

#include "sndfile.h"

class ImportRawDialog final : public wxDialogWrapper {

  public:
   ImportRawDialog(wxWindow * parent,
                   int encoding, unsigned channels,
                   int offset, double rate);
   ~ImportRawDialog();

   void OnOK(wxCommandEvent & event);
   void OnCancel(wxCommandEvent & event);
   void OnPlay(wxCommandEvent & event);
   void OnChoice(wxCommandEvent & event);

   // in and out
   int mEncoding;
   unsigned mChannels;
   int mOffset;
   double mRate;
   double mPercent;

 private:

   wxButton   *mOK;
   wxChoice   *mEncodingChoice;
   wxChoice   *mEndianChoice;
   wxChoice   *mChannelChoice;
   wxTextCtrl *mOffsetText;
   wxTextCtrl *mPercentText;
   wxTextCtrl *mRateText;

   int         mNumEncodings;
   ArrayOf<int> mEncodingSubtype;

   DECLARE_EVENT_TABLE()
};

//
// ImportRawDialog
//

enum {
   ChoiceID = 9000,
   PlayID
};

BEGIN_EVENT_TABLE(ImportRawDialog, wxDialogWrapper)
   EVT_BUTTON(wxID_OK, ImportRawDialog::OnOK)
   EVT_BUTTON(wxID_CANCEL, ImportRawDialog::OnCancel)
   EVT_BUTTON(PlayID, ImportRawDialog::OnPlay)
   EVT_CHOICE(ChoiceID, ImportRawDialog::OnChoice)
END_EVENT_TABLE()

ImportRawDialog::ImportRawDialog(wxWindow * parent,
                                 int encoding, unsigned channels,
                                 int offset, double rate)
:  wxDialogWrapper(parent, wxID_ANY, _("Import Raw Data"),
            wxDefaultPosition, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    mEncoding(encoding),
    mChannels(channels),
    mOffset(offset),
    mRate(rate)
{
   wxASSERT(channels >= 1);

   SetName(GetTitle());

   ShuttleGui S(this, eIsCreating);
   wxArrayString encodings;
   wxArrayString endians;
   wxArrayString chans;
   int num;
   int selection;
   int endian;
   int i;

   num = sf_num_encodings();
   mNumEncodings = 0;
   mEncodingSubtype.reinit(static_cast<size_t>(num));

   selection = 0;
   for (i=0; i<num; i++) {
      SF_INFO info;

      memset(&info, 0, sizeof(SF_INFO));

      int subtype = sf_encoding_index_to_subtype(i);
      info.format = SF_FORMAT_RAW + SF_ENDIAN_LITTLE + subtype;
      info.channels = 1;
      info.samplerate = 44100;

      if (sf_format_check(&info)) {
         mEncodingSubtype[mNumEncodings] = subtype;
         encodings.Add(sf_encoding_index_name(i));

         if ((mEncoding & SF_FORMAT_SUBMASK) == subtype)
            selection = mNumEncodings;

         mNumEncodings++;
      }
   }

   /* i18n-hint: Refers to byte-order.  Don't translate "endianness" if you don't
       know the correct technical word. */
   endians.Add(_("No endianness"));
   /* i18n-hint: Refers to byte-order.  Don't translate this if you don't
    know the correct technical word. */
   endians.Add(_("Little-endian"));
   /* i18n-hint: Refers to byte-order.  Don't translate this if you don't
      know the correct technical word. */
   endians.Add(_("Big-endian"));
   /* i18n-hint: Refers to byte-order.  Don't translate "endianness" if you don't
      know the correct technical word. */
   endians.Add(_("Default endianness"));

   switch (mEncoding & (SF_FORMAT_ENDMASK))
   {
      default:
      case SF_ENDIAN_FILE:
         endian = 0;
         break;
      case SF_ENDIAN_LITTLE:
         endian = 1;
         break;
      case SF_ENDIAN_BIG:
         endian = 2;
         break;
      case SF_ENDIAN_CPU:
         endian = 3;
         break;
   }

   chans.Add(_("1 Channel (Mono)"));
   chans.Add(_("2 Channels (Stereo)"));
   for (i=2; i<16; i++) {
      chans.Add(wxString::Format(_("%d Channels"), i + 1));
   }

   S.StartVerticalLay(false);
   {
      S.SetBorder(5);
      S.StartTwoColumn();
      {
         mEncodingChoice = S.Id(ChoiceID).AddChoice(_("Encoding:"),
                                                    encodings[selection],
                                                    &encodings);
         mEndianChoice = S.Id(ChoiceID).AddChoice(_("Byte order:"),
                                                  endians[endian],
                                                  &endians);
         mChannelChoice = S.Id(ChoiceID).AddChoice(_("Channels:"),
                                                   chans[mChannels-1],
                                                   &chans);
      }
      S.EndTwoColumn();

      S.SetBorder(5);
      S.StartMultiColumn(3);
      {
         // Offset text
         /* i18n-hint: (noun)*/
         mOffsetText = S.AddTextBox(_("Start offset:"),
                                    wxString::Format(wxT("%d"), mOffset),
                                    12);
         S.AddUnits(_("bytes"));

         // Percent text
         mPercentText = S.AddTextBox(_("Amount to import:"),
                                     wxT("100"),
                                     12);
         S.AddUnits(_("%"));

         // Rate text
         /* i18n-hint: (noun)*/
         mRateText = S.AddTextBox(_("Sample rate:"),
                                  wxString::Format(wxT("%d"), (int)mRate),
                                  12);
         /* i18n-hint: This is the abbreviation for "Hertz", or
            cycles per second. */
         S.AddUnits(_("Hz"));
      }
      S.EndMultiColumn();

      //
      // Preview Pane goes here
      //

      S.AddStandardButtons();
      // Find the OK button, and change its text to 'Import'.
      // We MUST set mOK because it is used later.
      mOK = (wxButton *)wxWindow::FindWindowById(wxID_OK, this);
      mOK->SetLabel(_("&Import"));
   }
   S.EndVerticalLay();

   Fit();
   SetSizeHints(GetSize());

   Centre(wxBOTH);
}

ImportRawDialog::~ImportRawDialog()
{
}

void ImportRawDialog::OnOK(wxCommandEvent & WXUNUSED(event))
{
   long l;

   mEncoding = mEncodingSubtype[mEncodingChoice->GetSelection()];
   mEncoding += (mEndianChoice->GetSelection() * 0x10000000);
   mChannels = mChannelChoice->GetSelection() + 1;
   mOffsetText->GetValue().ToLong(&l);
   mOffset = l;
   mPercentText->GetValue().ToDouble(&mPercent);
   mRateText->GetValue().ToDouble(&mRate);

   if (mChannels < 1 || mChannels > 16)
      mChannels = 1;
   if (mOffset < 0)
      mOffset = 0;
   if (mPercent < 0.0)
      mPercent = 0.0;
   if (mPercent > 100.0)
      mPercent = 100.0;
   if (mRate < 100.0)
      mRate = 100.0;
   if (mRate > 100000.0)
      mRate = 100000.0;

   EndModal(true);
}

void ImportRawDialog::OnCancel(wxCommandEvent & WXUNUSED(event))
{
   EndModal(false);
}

void ImportRawDialog::OnPlay(wxCommandEvent & WXUNUSED(event))
{
}

void ImportRawDialog::OnChoice(wxCommandEvent & WXUNUSED(event))
{
   SF_INFO info;

   memset(&info, 0, sizeof(SF_INFO));

   mEncoding = mEncodingSubtype[mEncodingChoice->GetSelection()];
   mEncoding += (mEndianChoice->GetSelection() * 0x10000000);

   info.format = mEncoding | SF_FORMAT_RAW;
   info.channels = mChannelChoice->GetSelection() + 1;
   info.samplerate = 44100;

   //mOK = (wxButton *)wxWindow::FindWindowById(wxID_OK, this);
   if (sf_format_check(&info)) {
      mOK->Enable(true);
      return;
   }

   // Try it with 1-channel
   info.channels = 1;
   if (sf_format_check(&info)) {
      mChannelChoice->SetSelection(0);
      mOK->Enable(true);
      return;
   }

   // Otherwise, this is an unsupported format
   mOK->Enable(false);
}
