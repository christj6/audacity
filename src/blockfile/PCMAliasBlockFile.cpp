/**********************************************************************

  Audacity: A Digital Audio Editor

  PCMAliasBlockFile.cpp

  Joshua Haberman

**********************************************************************/

#include "../Audacity.h"
#include "PCMAliasBlockFile.h"

#include <wx/file.h>
#include <wx/utils.h>
#include <wx/wxchar.h>
#include <wx/log.h>

#include <sndfile.h>

#include "../AudacityApp.h"
#include "../FileFormats.h"
#include "../Internat.h"
#include "../MemoryX.h"

#include "../ondemand/ODManager.h"
#include "../AudioIO.h"

extern AudioIO *gAudioIO;

PCMAliasBlockFile::PCMAliasBlockFile(
      wxFileNameWrapper &&fileName,
      wxFileNameWrapper &&aliasedFileName,
      sampleCount aliasStart,
      size_t aliasLen, int aliasChannel)
: AliasBlockFile{ std::move(fileName), std::move(aliasedFileName),
                  aliasStart, aliasLen, aliasChannel }
{
   AliasBlockFile::WriteSummary();
}

PCMAliasBlockFile::PCMAliasBlockFile(
      wxFileNameWrapper&& fileName,
      wxFileNameWrapper&& aliasedFileName,
      sampleCount aliasStart,
      size_t aliasLen, int aliasChannel,bool writeSummary)
: AliasBlockFile{ std::move(fileName), std::move(aliasedFileName),
                  aliasStart, aliasLen, aliasChannel }
{
   if(writeSummary)
      AliasBlockFile::WriteSummary();
}

PCMAliasBlockFile::PCMAliasBlockFile(
      wxFileNameWrapper &&existingSummaryFileName,
      wxFileNameWrapper &&aliasedFileName,
      sampleCount aliasStart,
      size_t aliasLen, int aliasChannel,
      float min, float max, float rms)
: AliasBlockFile{ std::move(existingSummaryFileName), std::move(aliasedFileName),
                  aliasStart, aliasLen,
                  aliasChannel, min, max, rms }
{
}

PCMAliasBlockFile::~PCMAliasBlockFile()
{
}

/// Reads the specified data from the aliased file, using libsndfile,
/// and converts it to the given sample format.
///
/// @param data   The buffer to read the sample data into.
/// @param format The format to convert the data into
/// @param start  The offset within the block to begin reading
/// @param len    The number of samples to read
size_t PCMAliasBlockFile::ReadData(samplePtr data, sampleFormat format,
                                size_t start, size_t len, bool mayThrow) const
{
   if(!mAliasedFileName.IsOk()){ // intentionally silenced
      memset(data, 0, SAMPLE_SIZE(format) * len);
      return len;
   }

   return CommonReadData( mayThrow,
      mAliasedFileName, mSilentAliasLog, this, mAliasStart, mAliasChannel,
      data, format, start, len);
}

/// Construct a NEW PCMAliasBlockFile based on this one, but writing
/// the summary data to a NEW file.
///
/// @param newFileName The filename to copy the summary data to.
BlockFilePtr PCMAliasBlockFile::Copy(wxFileNameWrapper &&newFileName)
{
   auto newBlockFile = make_blockfile<PCMAliasBlockFile>
      (std::move(newFileName), wxFileNameWrapper{mAliasedFileName},
       mAliasStart, mLen, mAliasChannel, mMin, mMax, mRMS);

   return newBlockFile;
}

void PCMAliasBlockFile::Recover(void)
{
   WriteSummary();
}

