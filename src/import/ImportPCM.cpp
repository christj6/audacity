/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportPCM.cpp

  Dominic Mazzoni
  Leland Lucius

*//****************************************************************//**

\class PCMImportFileHandle
\brief An ImportFileHandle for PCM data

*//****************************************************************//**

\class PCMImportPlugin
\brief An ImportPlugin for PCM data

*//*******************************************************************/

#include "../Audacity.h"
#include "ImportPCM.h"
#include "../Internat.h"

#include <wx/wx.h>
#include <wx/string.h>
#include <wx/utils.h>
#include <wx/intl.h>
#include <wx/ffile.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include "sndfile.h"

#include "../ondemand/ODManager.h"
#include "../ondemand/ODComputeSummaryTask.h"

//If OD is enabled, he minimum number of samples a file has to use it.
//Otherwise, we use the older PCMAliasBlockFile method since it should be fast enough.
#define kMinimumODFileSampleSize 44100*30

#ifndef SNDFILE_1
#error Requires libsndfile 1.0 or higher
#endif

#include "../FileFormats.h"
#include "../Prefs.h"
#include "../WaveTrack.h"
#include "ImportPlugin.h"

#include <algorithm>

#define DESC _("WAV, AIFF, and other uncompressed types")

class PCMImportPlugin final : public ImportPlugin
{
public:
   PCMImportPlugin()
   :  ImportPlugin(wxArrayString())
   {
      mExtensions = sf_get_all_extensions();
   }

   ~PCMImportPlugin() { }

   wxString GetPluginStringID() { return wxT("libsndfile"); }
   wxString GetPluginFormatDescription();
   std::unique_ptr<ImportFileHandle> Open(const wxString &Filename) override;
};


class PCMImportFileHandle final : public ImportFileHandle
{
public:
   PCMImportFileHandle(wxString name, SFFile &&file, SF_INFO info);
   ~PCMImportFileHandle();

   wxString GetFileDescription() override;
   ByteCount GetFileUncompressedBytes() override;
   ProgressResult Import(TrackFactory *trackFactory, TrackHolders &outTracks) override;

   wxInt32 GetStreamCount() override { return 1; }

   const wxArrayString &GetStreamInfo() override
   {
      static wxArrayString empty;
      return empty;
   }

   void SetStreamUsage(wxInt32 WXUNUSED(StreamID), bool WXUNUSED(Use)) override
   {}

private:
   SFFile                mFile;
   const SF_INFO         mInfo;
   sampleFormat          mFormat;
};

void GetPCMImportPlugin(ImportPluginList & importPluginList,
                        UnusableImportPluginList & WXUNUSED(unusableImportPluginList))
{
   importPluginList.push_back( make_movable<PCMImportPlugin>() );
}

wxString PCMImportPlugin::GetPluginFormatDescription()
{
    return DESC;
}

std::unique_ptr<ImportFileHandle> PCMImportPlugin::Open(const wxString &filename)
{
   SF_INFO info;
   wxFile f;   // will be closed when it goes out of scope
   SFFile file;

   memset(&info, 0, sizeof(info));


#ifdef __WXGTK__
   if (filename.Lower().EndsWith(wxT("mp3"))) {
      // There is a bug in libsndfile where mp3s with duplicated metadata tags
      // will crash libsndfile and thus audacity.
      // We have patched the lib-src version of libsndfile, but
      // for linux the user can build against the system libsndfile which
      // still has this bug.
      // This happens in sf_open_fd, which is the very first point of
      // interaction with libsndfile, so the only workaround is to hardcode
      // ImportPCM to not handle .mp3.  Of couse, this will still fail for mp3s
      // that are mislabeled with a .wav or other extension.
      // So, in the future we may want to write a simple parser to detect mp3s here.
      return NULL;
   }
#endif


   if (f.Open(filename)) {
      // Even though there is an sf_open() that takes a filename, use the one that
      // takes a file descriptor since wxWidgets can open a file with a Unicode name and
      // libsndfile can't (under Windows).
      file.reset(SFCall<SNDFILE*>(sf_open_fd, f.fd(), SFM_READ, &info, TRUE));
   }

   // The file descriptor is now owned by "file", so we must tell "f" to leave
   // it alone.  The file descriptor is closed by the destructor of file even if an error
   // occurs.
   f.Detach();

   if (!file) {
      // TODO: Handle error
      //char str[1000];
      //sf_error_str((SNDFILE *)NULL, str, 1000);

      return nullptr;
   } else if (file &&
              (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG) {
      // mchinen 15.1.2012 - disallowing libsndfile to handle
      // ogg files because seeking is broken at this date (very slow,
      // seeks from beginning of file each seek).
      // This was said by Erik (libsndfile maintainer).
      // Note that this won't apply to our local libsndfile, so only
      // linux builds that use --with-libsndfile=system are affected,
      // as our local libsndfile doesn't do OGG.
      // In particular ubuntu 10.10 and 11.04 are known to be affected
      // When the bug is fixed, we can check version to avoid only
      // the broken builds.

      return nullptr;
   }

   // Success, so now transfer the duty to close the file from "file".
   return std::make_unique<PCMImportFileHandle>(filename, std::move(file), info);
}

PCMImportFileHandle::PCMImportFileHandle(wxString name,
                                         SFFile &&file, SF_INFO info)
:  ImportFileHandle(name),
   mFile(std::move(file)),
   mInfo(info)
{
   wxASSERT(info.channels >= 0);

   //
   // Figure out the format to use.
   //
   // In general, go with the user's preferences.  However, if
   // the file is higher-quality, go with a format which preserves
   // the quality of the original file.
   //

   mFormat = (sampleFormat)
      gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleFormat"), floatSample);

   if (mFormat != floatSample &&
       sf_subtype_more_than_16_bits(mInfo.format))
      mFormat = floatSample;
}

wxString PCMImportFileHandle::GetFileDescription()
{
   return SFCall<wxString>(sf_header_name, mInfo.format);
}

auto PCMImportFileHandle::GetFileUncompressedBytes() -> ByteCount
{
   return mInfo.frames * mInfo.channels * SAMPLE_SIZE(mFormat);
}

// returns "copy" or "edit" (aliased) as the user selects.
// if the cancel button is hit then "cancel" is returned.
static wxString AskCopyOrEdit()
{
   wxString oldCopyPref = gPrefs->Read(wxT("/FileFormats/CopyOrEditUncompressedData"), wxT("copy"));
   bool firstTimeAsk    = gPrefs->Read(wxT("/Warnings/CopyOrEditUncompressedDataFirstAsk"), true)?true:false;
   bool oldAskPref      = gPrefs->Read(wxT("/Warnings/CopyOrEditUncompressedDataAsk"), true)?true:false;

   // The first time the user is asked we force it to 'copy'.
   // This effectively does a one-time change to the preferences.
   if (firstTimeAsk) {
      if (oldCopyPref != wxT("copy")) {
         gPrefs->Write(wxT("/FileFormats/CopyOrEditUncompressedData"), wxT("copy"));
         oldCopyPref = wxT("copy");
      }
      gPrefs->Write(wxT("/Warnings/CopyOrEditUncompressedDataFirstAsk"), (long) false);
      gPrefs->Flush();
   }

   // check the current preferences for whether or not we should ask the user about this.
   if (oldAskPref) {
      wxString newCopyPref = wxT("copy");
      wxDialogWrapper dialog(nullptr, -1, wxString(_("Warning")));
      dialog.SetName(dialog.GetTitle());

      wxBoxSizer *vbox;
      dialog.SetSizer(vbox = safenew wxBoxSizer(wxVERTICAL));

      wxString clause1 = _(
"When importing uncompressed audio files you can either copy them into the project,"
" or read them directly from their current location (without copying).\n\n"
      );

      wxString clause2 = oldCopyPref == wxT("copy")
         ? _("Your current preference is set to copy in.\n\n")
         : _("Your current preference is set to read directly.\n\n")
      ;

      wxString clause3 = _(
"Reading the files directly allows you to play or edit them almost immediately.  "
"This is less safe than copying in, because you must retain the files with their "
"original names in their original locations.\n"
"Help > Diagnostics > Check Dependencies will show the original names and locations of any files "
"that you are reading directly.\n\n"
"How do you want to import the current file(s)?"
      );

      wxStaticText *message =
         safenew wxStaticText(&dialog, -1, clause1 + clause2 + clause3);

      message->Wrap(500);
      message->SetName(message->GetLabel());

      vbox->Add(message, 1, wxALL | wxEXPAND, 10);

      wxStaticBox *box = safenew wxStaticBox(&dialog, -1, _("Choose an import method"));
      box->SetName(box->GetLabel());

      wxRadioButton *aliasRadio;
      wxRadioButton *copyRadio;
      wxCheckBox *dontAskNextTimeBox;

      {
         auto boxsizer = std::make_unique<wxStaticBoxSizer>(box, wxVERTICAL);

         copyRadio = safenew wxRadioButton(&dialog, -1, _("Make a &copy of the files before editing (safer)"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
         boxsizer->Add(copyRadio, 0, wxALL);
         copyRadio->SetName(wxStripMenuCodes(copyRadio->GetLabel()));

         aliasRadio = safenew wxRadioButton(&dialog, -1, _("Read the files &directly from the original (faster)"));
         boxsizer->Add(aliasRadio, 0, wxALL);
         aliasRadio->SetName(wxStripMenuCodes(aliasRadio->GetLabel()));

         dontAskNextTimeBox = safenew wxCheckBox(&dialog, -1, _("Don't &warn again and always use my choice above"));
         boxsizer->Add(dontAskNextTimeBox, 0, wxALL);
         vbox->Add(boxsizer.release(), 0, wxALL, 10);
      }

      dontAskNextTimeBox->SetName(wxStripMenuCodes(dontAskNextTimeBox->GetLabel()));


      wxRadioButton *prefsRadio = oldCopyPref == wxT("copy") ? copyRadio : aliasRadio;
      prefsRadio->SetValue(true);

      wxSizer *buttonSizer = dialog.CreateButtonSizer(wxOK | wxCANCEL);
      vbox->Add(buttonSizer, 0, wxALL | wxEXPAND, 10);

      dialog.SetSize(dialog.GetBestSize());
      dialog.Layout();
      dialog.Center();

      if (dialog.ShowModal() == wxID_OK) {
         if (aliasRadio->GetValue()) {
            newCopyPref = wxT("edit");
         }
         if (dontAskNextTimeBox->IsChecked()) {
            gPrefs->Write(wxT("/Warnings/CopyOrEditUncompressedDataAsk"), (long) false);
            gPrefs->Flush();
         }
      } else {
         return wxT("cancel");
      }

      // if the preference changed, save it.
      if (newCopyPref != oldCopyPref) {
         gPrefs->Write(wxT("/FileFormats/CopyOrEditUncompressedData"), newCopyPref);
         gPrefs->Flush();
      }
      oldCopyPref = newCopyPref;
   }
   return oldCopyPref;
}

ProgressResult PCMImportFileHandle::Import(TrackFactory *trackFactory,
                                TrackHolders &outTracks)
{
   outTracks.clear();

   wxASSERT(mFile.get());

   // Get the preference / warn the user about aliased files.
   wxString copyEdit = AskCopyOrEdit();

   if (copyEdit == wxT("cancel"))
      return ProgressResult::Cancelled;

   // Fall back to "copy" if it doesn't match anything else, since it is safer
   bool doEdit = false;
   if (copyEdit.IsSameAs(wxT("edit"), false))
      doEdit = true;


   CreateProgress();

   TrackHolders channels(mInfo.channels);

   auto iter = channels.begin();
   for (int c = 0; c < mInfo.channels; ++iter, ++c) {
      *iter = trackFactory->NewWaveTrack(mFormat, mInfo.samplerate);

      if (mInfo.channels > 1)
         switch (c) {
         case 0:
            iter->get()->SetChannel(Track::LeftChannel);
            break;
         case 1:
            iter->get()->SetChannel(Track::RightChannel);
            break;
         default:
            iter->get()->SetChannel(Track::MonoChannel);
         }
   }

   if (mInfo.channels == 2) {
      channels.begin()->get()->SetLinked(true);
   }

   auto fileTotalFrames =
      (sampleCount)mInfo.frames; // convert from sf_count_t
   auto maxBlockSize = channels.begin()->get()->GetMaxBlockSize();
   auto updateResult = ProgressResult::Cancelled;

   // If the format is not seekable, we must use 'copy' mode,
   // because 'edit' mode depends on the ability to seek to an
   // arbitrary location in the file.
   if (!mInfo.seekable)
      doEdit = false;

   if (doEdit) {
      // If this mode has been selected, we form the tracks as
      // aliases to the files we're editing, i.e. ("foo.wav", 12000-18000)
      // instead of actually making fresh copies of the samples.

      // lets use OD only if the file is longer than 30 seconds.  Otherwise, why wake up extra threads.
      //todo: make this a user pref.
      bool useOD =fileTotalFrames>kMinimumODFileSampleSize;
      int updateCounter = 0;

      for (decltype(fileTotalFrames) i = 0; i < fileTotalFrames; i += maxBlockSize) {

         const auto blockLen =
            limitSampleBufferSize( maxBlockSize, fileTotalFrames - i );

         auto iter = channels.begin();
         for (int c = 0; c < mInfo.channels; ++iter, ++c)
            iter->get()->AppendAlias(mFilename, i, blockLen, c,useOD);

         if (++updateCounter == 50) {
            updateResult = mProgress->Update(
               i.as_long_long(),
               fileTotalFrames.as_long_long()
            );
            updateCounter = 0;
            if (updateResult != ProgressResult::Success)
               break;
         }
      }

      // One last update for completion
      updateResult = mProgress->Update(
         fileTotalFrames.as_long_long(),
         fileTotalFrames.as_long_long()
      );

      if(useOD)
      {
         auto computeTask = make_movable<ODComputeSummaryTask>();
         bool moreThanStereo = mInfo.channels>2;
         for (const auto &channel : channels)
         {
            computeTask->AddWaveTrack(channel.get());
            if(moreThanStereo)
            {
               //if we have 3 more channels, they get imported on seperate tracks, so we add individual tasks for each.
               ODManager::Instance()->AddNewTask(std::move(computeTask));
               computeTask = make_movable<ODComputeSummaryTask>();
            }
         }
         //if we have a linked track, we add ONE task.
         if(!moreThanStereo)
            ODManager::Instance()->AddNewTask(std::move(computeTask));
      }
   }
   else {
      // Otherwise, we're in the "copy" mode, where we read in the actual
      // samples from the file and store our own local copy of the
      // samples in the tracks.

      // PRL:  guard against excessive memory buffer allocation in case of many channels
      using type = decltype(maxBlockSize);
      if (mInfo.channels < 1)
         return ProgressResult::Failed;
      auto maxBlock = std::min(maxBlockSize,
         std::numeric_limits<type>::max() /
            (mInfo.channels * SAMPLE_SIZE(mFormat))
      );
      if (maxBlock < 1)
         return ProgressResult::Failed;

      SampleBuffer srcbuffer, buffer;
      wxASSERT(mInfo.channels >= 0);
      while (NULL == srcbuffer.Allocate(maxBlock * mInfo.channels, mFormat).ptr() ||
             NULL == buffer.Allocate(maxBlock, mFormat).ptr())
      {
         maxBlock /= 2;
         if (maxBlock < 1)
            return ProgressResult::Failed;
      }

      decltype(fileTotalFrames) framescompleted = 0;

      long block;
      do {
         block = maxBlock;

         if (mFormat == int16Sample)
            block = SFCall<sf_count_t>(sf_readf_short, mFile.get(), (short *)srcbuffer.ptr(), block);
         //import 24 bit int as float and have the append function convert it.  This is how PCMAliasBlockFile works too.
         else
            block = SFCall<sf_count_t>(sf_readf_float, mFile.get(), (float *)srcbuffer.ptr(), block);

         if(block < 0 || block > (long)maxBlock) {
            wxASSERT(false);
            block = maxBlock;
         }

         if (block) {
            auto iter = channels.begin();
            for(int c=0; c<mInfo.channels; ++iter, ++c) {
               if (mFormat==int16Sample) {
                  for(int j=0; j<block; j++)
                     ((short *)buffer.ptr())[j] =
                        ((short *)srcbuffer.ptr())[mInfo.channels*j+c];
               }
               else {
                  for(int j=0; j<block; j++)
                     ((float *)buffer.ptr())[j] =
                        ((float *)srcbuffer.ptr())[mInfo.channels*j+c];
               }

               iter->get()->Append(buffer.ptr(), (mFormat == int16Sample)?int16Sample:floatSample, block);
            }
            framescompleted += block;
         }

         updateResult = mProgress->Update(
            framescompleted.as_long_long(),
            fileTotalFrames.as_long_long()
         );
         if (updateResult != ProgressResult::Success)
            break;

      } while (block > 0);
   }

   if (updateResult == ProgressResult::Failed || updateResult == ProgressResult::Cancelled) {
      return updateResult;
   }

   for(const auto &channel : channels) {
      channel->Flush();
   }
   outTracks.swap(channels);

   return updateResult;
}

PCMImportFileHandle::~PCMImportFileHandle()
{
}
