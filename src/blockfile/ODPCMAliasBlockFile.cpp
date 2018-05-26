/**********************************************************************

  Audacity: A Digital Audio Editor

  ODPCMAliasBlockFile.cpp

  Created by Michael Chinen (mchinen)
  Audacity(R) is copyright (c) 1999-2008 Audacity Team.
  License: GPL v2.  See License.txt.

******************************************************************//**

\class ODPCMAliasBlockFile
\brief ODPCMAliasBlockFile is a special type of PCMAliasBlockFile that does not necessarily have summary data available
The summary is eventually computed and written to a file in a background thread.

*//*******************************************************************/

#include "../Audacity.h"
#include "ODPCMAliasBlockFile.h"

#include <float.h>

#include <wx/file.h>
#include <wx/utils.h>
#include <wx/wxchar.h>
#include <wx/log.h>
#include <wx/thread.h>
#include <sndfile.h>

#include "../AudacityApp.h"
#include "PCMAliasBlockFile.h"
#include "../FileFormats.h"
#include "../Internat.h"

#include "../ondemand/ODManager.h"
#include "../AudioIO.h"

#include "NotYetAvailableException.h"

//#include <errno.h>

extern AudioIO *gAudioIO;

const int aheaderTagLen = 20;
char aheaderTag[aheaderTagLen + 1] = "AudacityBlockFile112";


ODPCMAliasBlockFile::ODPCMAliasBlockFile(
      wxFileNameWrapper &&fileName,
      wxFileNameWrapper &&aliasedFileName,
      sampleCount aliasStart,
      size_t aliasLen, int aliasChannel)
: PCMAliasBlockFile { std::move(fileName), std::move(aliasedFileName),
                      aliasStart, aliasLen, aliasChannel, false }
{
   mSummaryAvailable = mSummaryBeingComputed = mHasBeenSaved = false;
}

///summaryAvailable should be true if the file has been written already.
ODPCMAliasBlockFile::ODPCMAliasBlockFile(
      wxFileNameWrapper &&existingSummaryFileName,
      wxFileNameWrapper &&aliasedFileName,
      sampleCount aliasStart,
      size_t aliasLen, int aliasChannel,
      float min, float max, float rms, bool summaryAvailable)
: PCMAliasBlockFile(std::move(existingSummaryFileName), std::move(aliasedFileName),
                    aliasStart, aliasLen,
                    aliasChannel, min, max, rms)
{
   mSummaryAvailable=summaryAvailable;
   mSummaryBeingComputed=mHasBeenSaved=false;
 }

ODPCMAliasBlockFile::~ODPCMAliasBlockFile()
{
}



//Check to see if we have the file for these calls.
auto ODPCMAliasBlockFile::GetSpaceUsage() const -> DiskByteCount
{
   if(IsSummaryAvailable())
   {
      DiskByteCount ret;
      mFileNameMutex.Lock();
      wxFFile summaryFile(mFileName.GetFullPath());
      ret= summaryFile.Length();
      mFileNameMutex.Unlock();
      return ret;
   }
   else
   {
      return 0;
   }
}

/// Locks the blockfile only if it has a file that exists.  This needs to be done
/// so that the unsaved ODPCMAliasBlockfiles are deleted upon exit
void ODPCMAliasBlockFile::Lock()
{
   if(IsSummaryAvailable()&&mHasBeenSaved)
      PCMAliasBlockFile::Lock();
}

//when the file closes, it locks the blockfiles, but only conditionally.
// It calls this so we can check if it has been saved before.
void ODPCMAliasBlockFile::CloseLock()
{
   if(mHasBeenSaved)
      PCMAliasBlockFile::Lock();
}


/// unlocks the blockfile only if it has a file that exists.  This needs to be done
/// so that the unsaved ODPCMAliasBlockfiles are deleted upon exit
void ODPCMAliasBlockFile::Unlock()
{
   if(IsSummaryAvailable() && IsLocked())
      PCMAliasBlockFile::Unlock();
}


/// Gets extreme values for the specified region
auto ODPCMAliasBlockFile::GetMinMaxRMS(
   size_t start, size_t len, bool mayThrow) const -> MinMaxRMS
{
   if(IsSummaryAvailable())
   {
      return PCMAliasBlockFile::GetMinMaxRMS(start, len, mayThrow);
   }
   else
   {
      if (mayThrow)
         throw NotYetAvailableException{ GetAliasedFileName() };

      //fake values.  These values are used usually for normalization and amplifying, so we want
      //the max to be maximal and the min to be minimal
      return {
         -JUST_BELOW_MAX_AUDIO,
         JUST_BELOW_MAX_AUDIO,
         0.707f //sin with amp of 1 rms
      };
   }
}

/// Gets extreme values for the entire block
auto ODPCMAliasBlockFile::GetMinMaxRMS(bool mayThrow) const -> MinMaxRMS
{
  if(IsSummaryAvailable())
   {
      return PCMAliasBlockFile::GetMinMaxRMS(mayThrow);
   }
   else
   {
      if (mayThrow)
         throw NotYetAvailableException{ GetAliasedFileName() };

      //fake values.  These values are used usually for normalization and amplifying, so we want
      //the max to be maximal and the min to be minimal
      return {
         -JUST_BELOW_MAX_AUDIO,
         JUST_BELOW_MAX_AUDIO,
         0.707f //sin with amp of 1 rms
      };
   }
}

/// Returns the 256 byte summary data block.
/// Fill with zeroes and return false if data are unavailable for any reason.
bool ODPCMAliasBlockFile::Read256(float *buffer, size_t start, size_t len)
{
   if(IsSummaryAvailable())
   {
      return PCMAliasBlockFile::Read256(buffer,start,len);
   }
   else
   {
      //return nothing.
      ClearSamples((samplePtr)buffer, floatSample, 0, len);
      return false;
   }
}

/// Returns the 64K summary data block.
/// Fill with zeroes and return false if data are unavailable for any reason.
bool ODPCMAliasBlockFile::Read64K(float *buffer, size_t start, size_t len)
{
   if(IsSummaryAvailable())
   {
      return PCMAliasBlockFile::Read64K(buffer,start,len);
   }
   else
   {
      //return nothing - it hasn't been calculated yet
      ClearSamples((samplePtr)buffer, floatSample, 0, len);
      return false;
   }
}

/// If the summary has been computed,
/// Construct a NEW PCMAliasBlockFile based on this one.
/// otherwise construct an ODPCMAliasBlockFile that still needs to be computed.
/// @param newFileName The filename to copy the summary data to.
BlockFilePtr ODPCMAliasBlockFile::Copy(wxFileNameWrapper &&newFileName)
{
   BlockFilePtr newBlockFile;

   //mAliasedFile can change so we lock readdatamutex, which is responsible for it.
   auto locker = LockForRead();
   //If the file has been written AND it has been saved, we create a PCM alias blockfile because for
   //all intents and purposes, it is the same.
   //However, if it hasn't been saved yet, we shouldn't create one because the default behavior of the
   //PCMAliasBlockFile is to lock on exit, and this will cause orphaned blockfiles..
   if(IsSummaryAvailable() && mHasBeenSaved)
   {
      newBlockFile  = make_blockfile<PCMAliasBlockFile>
         (std::move(newFileName), wxFileNameWrapper{mAliasedFileName},
          mAliasStart, mLen, mAliasChannel, mMin, mMax, mRMS);

   }
   else
   {
      //Summary File might exist in this case, but it might not.
      newBlockFile  = make_blockfile<ODPCMAliasBlockFile>
         (std::move(newFileName), wxFileNameWrapper{mAliasedFileName},
          mAliasStart, mLen, mAliasChannel, mMin, mMax, mRMS,
          IsSummaryAvailable());
      //The client code will need to schedule this blockfile for OD summarizing if it is going to a NEW track.
   }

   return newBlockFile;
}

void ODPCMAliasBlockFile::Recover(void)
{
   if(IsSummaryAvailable())
   {
      WriteSummary();
   }
}

bool ODPCMAliasBlockFile::IsSummaryAvailable() const
{
   bool retval;
   mSummaryAvailableMutex.Lock();
   retval= mSummaryAvailable;
   mSummaryAvailableMutex.Unlock();
   return retval;
}

///Calls write summary, and makes sure it is only done once in a thread-safe fashion.
void ODPCMAliasBlockFile::DoWriteSummary()
{
   ODLocker locker { &mWriteSummaryMutex };
   if(!IsSummaryAvailable())
      WriteSummary();
}

///sets the file name the summary info will be saved in.  threadsafe.
void ODPCMAliasBlockFile::SetFileName(wxFileNameWrapper &&name)
{
   mFileNameMutex.Lock();
   mFileName = std::move(name);
   mFileNameMutex.Unlock();
}

///sets the file name the summary info will be saved in.  threadsafe.
auto ODPCMAliasBlockFile::GetFileName() const -> GetFileNameResult
{
   return { mFileName, ODLocker{ &mFileNameMutex } };
}

/// Write the summary to disk, using the derived ReadData() to get the data
void ODPCMAliasBlockFile::WriteSummary()
{
   // To build the summary data, call ReadData (implemented by the
   // derived classes) to get the sample data
   // Call this first, so that in case of exceptions from ReadData, there is
   // no NEW output file
   SampleBuffer sampleData(mLen, floatSample);
   this->ReadData(sampleData.ptr(), floatSample, 0, mLen, true);

   ArrayOf< char > fileNameChar;
   FILE *summaryFile{};
   {
      //the mFileName path may change, for example, when the project is saved.
      //(it moves from /tmp/ to wherever it is saved to.
      ODLocker locker { &mFileNameMutex };

      //wxFFile is not thread-safe - if any error occurs in opening the file,
      // it posts a wxlog message which WILL crash
      // Audacity because it goes into the wx GUI.
      // For this reason I left the wxFFile method commented out. (mchinen)
      //    wxFFile summaryFile(mFileName.GetFullPath(), wxT("wb"));

      // ...and we use fopen instead.
      wxString sFullPath = mFileName.GetFullPath();
      fileNameChar.reinit( strlen(sFullPath.mb_str(wxConvFile)) + 1 );
      strcpy(fileNameChar.get(), sFullPath.mb_str(wxConvFile));
      summaryFile = fopen(fileNameChar.get(), "wb");
   }

   // JKC ANSWER-ME: Whay is IsOpened() commented out?
   if (!summaryFile){//.IsOpened() ){

      // Never silence the Log w.r.t write errors; they always count
      //however, this is going to be called from a non-main thread,
      //and wxLog calls are not thread safe.
      wxPrintf("Unable to write summary data to file: %s", fileNameChar.get());

      throw FileException{
         FileException::Cause::Read, wxFileName{ fileNameChar.get() } };
   }

   ArrayOf<char> cleanup;
   void *summaryData = CalcSummary(sampleData.ptr(), mLen,
                                            floatSample, cleanup);

   //summaryFile.Write(summaryData, mSummaryInfo.totalSummaryBytes);
   fwrite(summaryData, 1, mSummaryInfo.totalSummaryBytes, summaryFile);
   fclose(summaryFile);


    //     wxPrintf("write successful. filename: %s\n", fileNameChar);

   mSummaryAvailableMutex.Lock();
   mSummaryAvailable=true;
   mSummaryAvailableMutex.Unlock();
}



/// A thread-safe version of CalcSummary.  BlockFile::CalcSummary
/// uses a static summary array across the class, which we can't use.
/// Get a buffer containing a summary block describing this sample
/// data.  This must be called by derived classes when they
/// are constructed, to allow them to construct their summary data,
/// after which they should write that data to their disk file.
///
/// This method also has the side effect of setting the mMin, mMax,
/// and mRMS members of this class.
///
/// Unlike BlockFile's implementation You SHOULD DELETE the returned buffer.
/// this is protected so it shouldn't be hard to deal with - just override
/// all BlockFile methods that use this method.
///
/// @param buffer A buffer containing the sample data to be analyzed
/// @param len    The length of the sample data
/// @param format The format of the sample data.
void *ODPCMAliasBlockFile::CalcSummary(samplePtr buffer, size_t len,
                             sampleFormat format, ArrayOf<char> &cleanup)
{
   cleanup.reinit(mSummaryInfo.totalSummaryBytes);
   char* localFullSummary = cleanup.get();

   memcpy(localFullSummary, aheaderTag, aheaderTagLen);

   float *summary64K = (float *)(localFullSummary + mSummaryInfo.offset64K);
   float *summary256 = (float *)(localFullSummary + mSummaryInfo.offset256);

   Floats floats;
   float *fbuffer;

   //mchinen: think we can hack this - don't allocate and copy if we don't need to.,
   if(format==floatSample)
   {
      fbuffer = (float*)buffer;
   }
   else
   {
      floats.reinit(len);
      fbuffer = floats.get();
      CopySamples(buffer, format,
               (samplePtr)fbuffer, floatSample, len);
   }

   BlockFile::CalcSummaryFromBuffer(fbuffer, len, summary256, summary64K);

   return localFullSummary;
}




/// Reads the specified data from the aliased file, using libsndfile,
/// and converts it to the given sample format.
/// Copied from PCMAliasBlockFIle but wxLog calls taken out for thread safety
///
/// @param data   The buffer to read the sample data into.
/// @param format The format to convert the data into
/// @param start  The offset within the block to begin reading
/// @param len    The number of samples to read
size_t ODPCMAliasBlockFile::ReadData(samplePtr data, sampleFormat format,
                                size_t start, size_t len, bool mayThrow) const
{

   auto locker = LockForRead();

   if(!mAliasedFileName.IsOk()){ // intentionally silenced
      memset(data,0,SAMPLE_SIZE(format)*len);
      return len;
   }

   return CommonReadData( mayThrow,
      mAliasedFileName, mSilentAliasLog, this, mAliasStart, mAliasChannel,
      data, format, start, len);
}

/// Read the summary of this alias block from disk.  Since the audio data
/// is elsewhere, this consists of reading the entire summary file.
/// Fill with zeroes and return false if data are unavailable for any reason.
///
/// @param *data The buffer where the summary data will be stored.  It must
///              be at least mSummaryInfo.totalSummaryBytes long.
bool ODPCMAliasBlockFile::ReadSummary(ArrayOf<char> &data)
{
   data.reinit( mSummaryInfo.totalSummaryBytes );

   ODLocker locker{ &mFileNameMutex };
   wxFFile summaryFile(mFileName.GetFullPath(), wxT("rb"));

   if( !summaryFile.IsOpened() ) {

      // NEW model; we need to return valid data
      memset(data.get(), 0, mSummaryInfo.totalSummaryBytes);

      // we silence the logging for this operation in this object
      // after first occurrence of error; it's already reported and
      // spewing at the user will complicate the user's ability to
      // deal
      mSilentLog = TRUE;

      return false;
   }
   else
      mSilentLog = FALSE; // worked properly, any future error is NEW

   auto read = summaryFile.Read(data.get(), mSummaryInfo.totalSummaryBytes);

   if (read != mSummaryInfo.totalSummaryBytes) {
      memset(data.get(), 0, mSummaryInfo.totalSummaryBytes);
      return false;
   }
   
   FixSummary(data.get());

   return true;
}

/// Prevents a read on other threads.
void ODPCMAliasBlockFile::LockRead() const
{
   mReadDataMutex.Lock();
}
/// Allows reading on other threads.
void ODPCMAliasBlockFile::UnlockRead() const
{
   mReadDataMutex.Unlock();
}


