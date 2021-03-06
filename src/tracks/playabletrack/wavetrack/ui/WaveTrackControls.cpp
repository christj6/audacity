/**********************************************************************

Audacity: A Digital Audio Editor

WaveTrackControls.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#include "../../../../Audacity.h"
#include "../../../../Experimental.h"
#include "WaveTrackControls.h"

#include "../../../../AudioIO.h"
#include "../../../../HitTestResult.h"
#include "../../../../Project.h"
#include "../../../../RefreshCode.h"
#include "../../../../WaveTrack.h"
#include "../../../../ShuttleGui.h"
#include "../../../../TrackPanel.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../widgets/PopupMenuTable.h"
#include "../../../../ondemand/ODManager.h"
#include "../../../../prefs/PrefsDialog.h"
#include "../../../../prefs/TracksBehaviorsPrefs.h"
#include "../../../../prefs/WaveformPrefs.h"
#include "../../../../widgets/ErrorDialog.h"

#include <wx/combobox.h>

namespace
{
   /// Puts a check mark at a given position in a menu.
   template<typename Pred>
   void SetMenuChecks(wxMenu & menu, const Pred &pred)
   {
      for (auto &item : menu.GetMenuItems())
      {
         if (item->IsCheckable()) {
            auto id = item->GetId();
            menu.Check(id, pred(id));
         }
      }
   }
}

WaveTrackControls::~WaveTrackControls()
{
}


std::vector<UIHandlePtr> WaveTrackControls::HitTest
(const TrackPanelMouseState & st,
 const AudacityProject *pProject)
{
   // Hits are mutually exclusive, results single
   const wxMouseState &state = st.state;
   if (state.ButtonIsDown(wxMOUSE_BTN_LEFT)) {
      auto track = FindTrack();
      std::vector<UIHandlePtr> results;
      auto result = [&]{
         UIHandlePtr result;

         return result;
      }();
      if (result) {
         results.push_back(result);
         return results;
      }
   }

   return TrackControls::HitTest(st, pProject);
}

enum {
   OnRate8ID = 30000,      // <---
   OnRate11ID,             //    |
   OnRate16ID,             //    |
   OnRate22ID,             //    |
   OnRate44ID,             //    |
   OnRate48ID,             //    | Leave these in order
   OnRate88ID,             //    |
   OnRate96ID,             //    |
   OnRate176ID,            //    |
   OnRate192ID,            //    |
   OnRate352ID,            //    |
   OnRate384ID,            //    |
   OnRateOtherID,          //    |
   //    |
   On16BitID,              //    |
   On24BitID,              //    |
   OnFloatID,              // <---

   OnWaveformID,
   OnWaveformDBID,

   OnChannelLeftID,
   OnChannelRightID,
   OnChannelMonoID,

   OnWaveColorID,
   OnInstrument1ID,
   OnInstrument2ID,
   OnInstrument3ID,
   OnInstrument4ID,

   OnSwapChannelsID,

   ChannelMenuID,
};


//=============================================================================
// Table class for a sub-menu
class WaveColorMenuTable : public PopupMenuTable
{
   WaveColorMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(WaveColorMenuTable);

public:
   static WaveColorMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   TrackControls::InitMenuData *mpData;

   int IdOfWaveColor(int WaveColor);
   void OnWaveColorChange(wxCommandEvent & event);
};

WaveColorMenuTable &WaveColorMenuTable::Instance()
{
   static WaveColorMenuTable instance;
   return instance;
}

void WaveColorMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<TrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   auto WaveColorId = IdOfWaveColor( pTrack->GetWaveColorIndex());
   SetMenuChecks(*pMenu, [=](int id){ return id == WaveColorId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = project->IsAudioActive();
   for (int i = OnInstrument1ID; i <= OnInstrument4ID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

const wxString GetWaveColorStr(int colorIndex)
{
   return wxString::Format( _("Instrument %i"), colorIndex+1 );
}


BEGIN_POPUP_MENU(WaveColorMenuTable)
   POPUP_MENU_RADIO_ITEM(OnInstrument1ID,
      GetWaveColorStr(0), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument2ID,
      GetWaveColorStr(1), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument3ID,
      GetWaveColorStr(2), OnWaveColorChange)
   POPUP_MENU_RADIO_ITEM(OnInstrument4ID,
      GetWaveColorStr(3), OnWaveColorChange)
END_POPUP_MENU()

/// Converts a WaveColor enumeration to a wxWidgets menu item Id.
int WaveColorMenuTable::IdOfWaveColor(int WaveColor)
{  return OnInstrument1ID + WaveColor;}

/// Handles the selection from the WaveColor submenu of the
/// track menu.
void WaveColorMenuTable::OnWaveColorChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnInstrument1ID && id <= OnInstrument4ID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack && pTrack->GetKind() == Track::Wave);

   int newWaveColor = id - OnInstrument1ID;

   AudacityProject *const project = ::GetActiveProject();
//   TrackList *const tracks = project->GetTracks();

   pTrack->SetWaveColorIndex(newWaveColor);

   // Assume partner is wave or null
   const auto partner = static_cast<WaveTrack*>(pTrack->GetLink());
   if (partner)
      partner->SetWaveColorIndex(newWaveColor);

   project->PushState(wxString::Format(_("Changed '%s' to %s"),
      pTrack->GetName(),
      GetWaveColorStr(newWaveColor)),
      _("WaveColor Change"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}




//=============================================================================
// Table class for a sub-menu
class FormatMenuTable : public PopupMenuTable
{
   FormatMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(FormatMenuTable);

public:
   static FormatMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   TrackControls::InitMenuData *mpData;

   int IdOfFormat(int format);

   void OnFormatChange(wxCommandEvent & event);
};

FormatMenuTable &FormatMenuTable::Instance()
{
   static FormatMenuTable instance;
   return instance;
}

void FormatMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<TrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   auto formatId = IdOfFormat(pTrack->GetSampleFormat());
   SetMenuChecks(*pMenu, [=](int id){ return id == formatId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = project->IsAudioActive();
   for (int i = On16BitID; i <= OnFloatID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

BEGIN_POPUP_MENU(FormatMenuTable)
   POPUP_MENU_RADIO_ITEM(On16BitID,
      GetSampleFormatStr(int16Sample), OnFormatChange)
   POPUP_MENU_RADIO_ITEM(On24BitID,
      GetSampleFormatStr(int24Sample), OnFormatChange)
   POPUP_MENU_RADIO_ITEM(OnFloatID,
      GetSampleFormatStr(floatSample), OnFormatChange)
END_POPUP_MENU()

/// Converts a format enumeration to a wxWidgets menu item Id.
int FormatMenuTable::IdOfFormat(int format)
{
   switch (format) {
   case int16Sample:
      return On16BitID;
   case int24Sample:
      return On24BitID;
   case floatSample:
      return OnFloatID;
   default:
      // ERROR -- should not happen
      wxASSERT(false);
      break;
   }
   return OnFloatID;// Compiler food.
}

/// Handles the selection from the Format submenu of the
/// track menu.
void FormatMenuTable::OnFormatChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= On16BitID && id <= OnFloatID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack && pTrack->GetKind() == Track::Wave);

   sampleFormat newFormat = int16Sample;

   switch (id) {
   case On16BitID:
      newFormat = int16Sample;
      break;
   case On24BitID:
      newFormat = int24Sample;
      break;
   case OnFloatID:
      newFormat = floatSample;
      break;
   default:
      // ERROR -- should not happen
      wxASSERT(false);
      break;
   }
   if (newFormat == pTrack->GetSampleFormat())
      return; // Nothing to do.

   AudacityProject *const project = ::GetActiveProject();
   pTrack->ConvertToSampleFormat(newFormat);

   // Assume partner is wave or null
   const auto partner = static_cast<WaveTrack*>(pTrack->GetLink());
   if (partner)
      partner->ConvertToSampleFormat(newFormat);

   /* i18n-hint: The strings name a track and a format */
   project->PushState(wxString::Format(_("Changed '%s' to %s"),
      pTrack->GetName(),
      GetSampleFormatStr(newFormat)),
      _("Format Change"));

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}


//=============================================================================
// Table class for a sub-menu
class RateMenuTable : public PopupMenuTable
{
   RateMenuTable() : mpData(NULL) {}
   DECLARE_POPUP_MENU(RateMenuTable);

public:
   static RateMenuTable &Instance();

private:
   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = NULL;
   }

   TrackControls::InitMenuData *mpData;

   int IdOfRate(int rate);
   void SetRate(WaveTrack * pTrack, double rate);

   void OnRateChange(wxCommandEvent & event);
   void OnRateOther(wxCommandEvent & event);
};

RateMenuTable &RateMenuTable::Instance()
{
   static RateMenuTable instance;
   return instance;
}

void RateMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<TrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   const auto rateId = IdOfRate((int)pTrack->GetRate());
   SetMenuChecks(*pMenu, [=](int id){ return id == rateId; });

   AudacityProject *const project = ::GetActiveProject();
   bool unsafe = project->IsAudioActive();
   for (int i = OnRate8ID; i <= OnRateOtherID; i++) {
      pMenu->Enable(i, !unsafe);
   }
}

BEGIN_POPUP_MENU(RateMenuTable)
   POPUP_MENU_RADIO_ITEM(OnRate8ID, _("8000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate11ID, _("11025 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate16ID, _("16000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate22ID, _("22050 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate44ID, _("44100 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate48ID, _("48000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate88ID, _("88200 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate96ID, _("96000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate176ID, _("176400 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate192ID, _("192000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate352ID, _("352800 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRate384ID, _("384000 Hz"), OnRateChange)
   POPUP_MENU_RADIO_ITEM(OnRateOtherID, _("&Other..."), OnRateOther)
END_POPUP_MENU()

const int nRates = 12;

///  gRates MUST CORRESPOND DIRECTLY TO THE RATES AS LISTED IN THE MENU!!
///  IN THE SAME ORDER!!
static int gRates[nRates] = { 8000, 11025, 16000, 22050, 44100, 48000, 88200, 96000,
176400, 192000, 352800, 384000 };

/// Converts a sampling rate to a wxWidgets menu item id
int RateMenuTable::IdOfRate(int rate)
{
   for (int i = 0; i<nRates; i++) {
      if (gRates[i] == rate)
         return i + OnRate8ID;
   }
   return OnRateOtherID;
}

/// Sets the sample rate for a track, and if it is linked to
/// another track, that one as well.
void RateMenuTable::SetRate(WaveTrack * pTrack, double rate)
{
   AudacityProject *const project = ::GetActiveProject();
   pTrack->SetRate(rate);
   // Assume linked track is wave or null
   const auto partner = static_cast<WaveTrack*>(pTrack->GetLink());
   if (partner)
      partner->SetRate(rate);
   // Separate conversion of "rate" enables changing the decimals without affecting i18n
   wxString rateString = wxString::Format(wxT("%.3f"), rate);
   /* i18n-hint: The string names a track */
   project->PushState(wxString::Format(_("Changed '%s' to %s Hz"),
      pTrack->GetName(), rateString),
      _("Rate Change"));
}

/// This method handles the selection from the Rate
/// submenu of the track menu, except for "Other" (/see OnRateOther).
void RateMenuTable::OnRateChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnRate8ID && id <= OnRate384ID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack->GetKind() == Track::Wave);

   SetRate(pTrack, gRates[id - OnRate8ID]);

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

void RateMenuTable::OnRateOther(wxCommandEvent &)
{
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack && pTrack->GetKind() == Track::Wave);

   int newRate;

   /// \todo Remove artificial constants!!
   /// \todo Make a real dialog box out of this!!
   while (true)
   {
      wxDialogWrapper dlg(mpData->pParent, wxID_ANY, wxString(_("Set Rate")));
      dlg.SetName(dlg.GetTitle());
      ShuttleGui S(&dlg, eIsCreating);
      wxString rate;
      wxArrayString rates;
      wxComboBox *cb;

      rate.Printf(wxT("%ld"), lrint(pTrack->GetRate()));

      rates.Add(wxT("8000"));
      rates.Add(wxT("11025"));
      rates.Add(wxT("16000"));
      rates.Add(wxT("22050"));
      rates.Add(wxT("44100"));
      rates.Add(wxT("48000"));
      rates.Add(wxT("88200"));
      rates.Add(wxT("96000"));
      rates.Add(wxT("176400"));
      rates.Add(wxT("192000"));
      rates.Add(wxT("352800"));
      rates.Add(wxT("384000"));

      S.StartVerticalLay(true);
      {
         S.SetBorder(10);
         S.StartHorizontalLay(wxEXPAND, false);
         {
            cb = S.AddCombo(_("New sample rate (Hz):"),
               rate,
               &rates);
#if defined(__WXMAC__)
            // As of wxMac-2.8.12, setting manually is required
            // to handle rates not in the list.  See: Bug #427
            cb->SetValue(rate);
#endif
         }
         S.EndHorizontalLay();
         S.AddStandardButtons();
      }
      S.EndVerticalLay();

      dlg.SetClientSize(dlg.GetSizer()->CalcMin());
      dlg.Center();

      if (dlg.ShowModal() != wxID_OK)
      {
         return;  // user cancelled dialog
      }

      long lrate;
      if (cb->GetValue().ToLong(&lrate) && lrate >= 1 && lrate <= 1000000)
      {
         newRate = (int)lrate;
         break;
      }

      AudacityMessageBox(_("The entered value is invalid"), _("Error"),
         wxICON_ERROR, mpData->pParent);
   }

   SetRate(pTrack, newRate);

   using namespace RefreshCode;
   mpData->result = RefreshAll | FixScrollbars;
}

//=============================================================================
// Class defining common command handlers for mono and stereo tracks
class WaveTrackMenuTable : public PopupMenuTable
{
public:
   static WaveTrackMenuTable &Instance( Track * pTrack);
   Track * mpTrack;

protected:
   WaveTrackMenuTable() : mpData(NULL) {mpTrack=NULL;}

   void InitMenu(Menu *pMenu, void *pUserData) override;

   void DestroyMenu() override
   {
      mpData = nullptr;
   }

   DECLARE_POPUP_MENU(WaveTrackMenuTable);

   TrackControls::InitMenuData *mpData;

   void OnSetDisplay(wxCommandEvent & event);
   void OnChannelChange(wxCommandEvent & event);
   void SplitStereo();
   void OnSwapChannels(wxCommandEvent & event);
};

WaveTrackMenuTable &WaveTrackMenuTable::Instance( Track * pTrack )
{
   static WaveTrackMenuTable instance;
   wxCommandEvent evt;
   // Clear it out so we force a repopulate
   instance.Invalidate( evt );
   // Ensure we know how to poulate.
   // Messy, but the design does not seem to offer an alternative.
   // We won't use pTrack after populate.
   instance.mpTrack = pTrack;
   return instance;
}

void WaveTrackMenuTable::InitMenu(Menu *pMenu, void *pUserData)
{
   mpData = static_cast<TrackControls::InitMenuData*>(pUserData);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);

   std::vector<int> checkedIds;

   const int display = pTrack->GetDisplay();
   if (display == WaveTrack::Waveform)
   {
	   if (pTrack->GetWaveformSettings().isLinear())
	   {
		   checkedIds.push_back(OnWaveformID);
	   }
	   else
	   {
		   checkedIds.push_back(OnWaveformDBID);
	   }
   }

   const bool isMono = !pTrack->GetLink();
   if ( isMono )
   {
      mpData = static_cast<TrackControls::InitMenuData*>(pUserData);
      WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);

      if (isMono) {
         int itemId;
         switch (pTrack->GetChannel()) {
            case Track::LeftChannel:
               itemId = OnChannelLeftID;
               break;
            case Track::RightChannel:
               itemId = OnChannelRightID;
               break;
            default:
               itemId = OnChannelMonoID;
               break;
         }
         checkedIds.push_back(itemId);
      }
   }

   SetMenuChecks(*pMenu, [&](int id){
      auto end = checkedIds.end();
      return end != std::find(checkedIds.begin(), end, id);
   });


   pMenu->Enable(OnSwapChannelsID, !isMono);
}

BEGIN_POPUP_MENU(WaveTrackMenuTable)
   POPUP_MENU_SEPARATOR()

   POPUP_MENU_RADIO_ITEM(OnWaveformID, _("Wa&veform"), OnSetDisplay)
   POPUP_MENU_RADIO_ITEM(OnWaveformDBID, _("&Waveform (dB)"), OnSetDisplay)
   POPUP_MENU_SEPARATOR()

   POPUP_MENU_ITEM(OnSwapChannelsID, _("Swap Stereo &Channels"), OnSwapChannels)

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpTrack);
   if( pTrack && pTrack->GetDisplay() == WaveTrack::Waveform  ){
      POPUP_MENU_SEPARATOR()
      POPUP_MENU_SUB_MENU(OnWaveColorID, _("&Wave Color"), WaveColorMenuTable)
   }

   POPUP_MENU_SEPARATOR()
   POPUP_MENU_SUB_MENU(0, _("&Format"), FormatMenuTable)
   POPUP_MENU_SEPARATOR()
   POPUP_MENU_SUB_MENU(0, _("Rat&e"), RateMenuTable)
END_POPUP_MENU()


///  Set the Display mode based on the menu choice in the Track Menu.
void WaveTrackMenuTable::OnSetDisplay(wxCommandEvent & event)
{
   int idInt = event.GetId();
   wxASSERT(idInt >= OnWaveformID && idInt <= OnWaveformDBID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack && pTrack->GetKind() == Track::Wave);

   bool linear = false;
   WaveTrack::WaveTrackDisplay id;
   switch (idInt) {
   default:
   case OnWaveformID:
      linear = true, id = WaveTrack::Waveform; break;
   case OnWaveformDBID:
      id = WaveTrack::Waveform; break;
   }

   const bool wrongType = pTrack->GetDisplay() != id;
   const bool wrongScale =
      (id == WaveTrack::Waveform &&
      pTrack->GetWaveformSettings().isLinear() != linear);
   if (wrongType || wrongScale) {
      pTrack->SetLastScaleType();
      pTrack->SetDisplay(WaveTrack::WaveTrackDisplay(id));
      if (wrongScale)
         pTrack->GetIndependentWaveformSettings().scaleType = linear
         ? WaveformSettings::stLinear
         : WaveformSettings::stLogarithmic;

      // Assume partner is wave or null
      auto partner = static_cast<WaveTrack *>(pTrack->GetLink());
      if (partner) {
         partner->SetLastScaleType();
         partner->SetDisplay(WaveTrack::WaveTrackDisplay(id));
         if (wrongScale)
            partner->GetIndependentWaveformSettings().scaleType = linear
            ? WaveformSettings::stLinear
            : WaveformSettings::stLogarithmic;
      }

      AudacityProject *const project = ::GetActiveProject();
      project->ModifyState();

      using namespace RefreshCode;
      mpData->result = RefreshAll | UpdateVRuler;
   }
}

void WaveTrackMenuTable::OnChannelChange(wxCommandEvent & event)
{
   int id = event.GetId();
   wxASSERT(id >= OnChannelLeftID && id <= OnChannelMonoID);
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack);
   int channel;
   wxString channelmsg;
   switch (id) {
   default:
   case OnChannelMonoID:
      channel = Track::MonoChannel;
      channelmsg = _("Mono");
      break;
   case OnChannelLeftID:
      channel = Track::LeftChannel;
      channelmsg = _("Left Channel");
      break;
   case OnChannelRightID:
      channel = Track::RightChannel;
      channelmsg = _("Right Channel");
      break;
   }
   pTrack->SetChannel(channel);
   AudacityProject *const project = ::GetActiveProject();
   /* i18n-hint: The strings name a track and a channel choice (mono, left, or right) */
   project->PushState(wxString::Format(_("Changed '%s' to %s"),
      pTrack->GetName(),
      channelmsg),
      _("Channel"));
   mpData->result = RefreshCode::RefreshAll;
}

/// Split a stereo track into two tracks...
void WaveTrackMenuTable::SplitStereo()
{
   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   wxASSERT(pTrack);
   pTrack->SetChannel(Track::MonoChannel);

   // Assume partner is present, and is wave
   const auto partner = static_cast<WaveTrack*>(pTrack->GetLink());
   wxASSERT(partner);
   if (!partner)
      return;

   if (partner)
   {
      // Keep original stereo track name.
      partner->SetName(pTrack->GetName());
      partner->SetChannel(Track::MonoChannel);

      //On Demand - have each channel add its own.
      if (ODManager::IsInstanceCreated() && partner->GetKind() == Track::Wave)
         ODManager::Instance()->MakeWaveTrackIndependent(partner);
   }

   pTrack->SetLinked(false);
   //make sure neither track is smaller than its minimum height
   if (pTrack->GetHeight() < pTrack->GetMinimizedHeight())
      pTrack->SetHeight(pTrack->GetMinimizedHeight());
   if (partner)
   {
      if (partner->GetHeight() < partner->GetMinimizedHeight())
         partner->SetHeight(partner->GetMinimizedHeight());

      // Make tracks the same height
      if (pTrack->GetHeight() != partner->GetHeight())
      {
         pTrack->SetHeight((pTrack->GetHeight() + partner->GetHeight()) / 2.0);
         partner->SetHeight(pTrack->GetHeight());
      }
   }

   mpData->result = RefreshCode::RefreshAll;
}

/// Swap the left and right channels of a stero track...
void WaveTrackMenuTable::OnSwapChannels(wxCommandEvent &)
{
   AudacityProject *const project = ::GetActiveProject();

   WaveTrack *const pTrack = static_cast<WaveTrack*>(mpData->pTrack);
   // Assume partner is wave or null
   const auto partner = static_cast<WaveTrack*>(pTrack->GetLink());
   Track *const focused = project->GetTrackPanel()->GetFocusedTrack();
   const bool hasFocus =
      (focused == pTrack || focused == partner);

   SplitStereo();
   pTrack->SetChannel(Track::RightChannel);
   partner->SetChannel(Track::LeftChannel);

   TrackList *const tracks = project->GetTracks();
   (tracks->MoveUp(partner));
   partner->SetLinked(true);

   if (hasFocus)
      project->GetTrackPanel()->SetFocusedTrack(partner);

   /* i18n-hint: The string names a track  */
   project->PushState(wxString::Format(_("Swapped Channels in '%s'"),
      pTrack->GetName()),
      _("Swap Channels"));

   mpData->result = RefreshCode::RefreshAll;
}

//=============================================================================
PopupMenuTable *WaveTrackControls::GetMenuExtension(Track * pTrack)
{

   WaveTrackMenuTable & result = WaveTrackMenuTable::Instance( pTrack );
   return &result;
}
