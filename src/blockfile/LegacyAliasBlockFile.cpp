/**********************************************************************

  Audacity: A Digital Audio Editor

  LegacyAliasBlockFile.cpp

  Dominic Mazzoni

**********************************************************************/

#include "../Audacity.h"
#include "LegacyAliasBlockFile.h"

#include <wx/utils.h>
#include <wx/wxchar.h>

#include <sndfile.h>

#include "LegacyBlockFile.h"
#include "../FileFormats.h"
#include "../Internat.h"

LegacyAliasBlockFile::LegacyAliasBlockFile(wxFileNameWrapper &&fileName,
                                           wxFileNameWrapper &&aliasedFileName,
                                           sampleCount aliasStart,
                                           size_t aliasLen,
                                           int aliasChannel,
                                           size_t summaryLen,
                                           bool noRMS)
: PCMAliasBlockFile{ std::move(fileName), std::move(aliasedFileName), aliasStart, aliasLen,
                     aliasChannel, 0.0, 0.0, 0.0 }
{
   sampleFormat format;

   if (noRMS)
      format = int16Sample;
   else
      format = floatSample;

   ComputeLegacySummaryInfo(mFileName,
                            summaryLen, format,
                            &mSummaryInfo, noRMS, FALSE,
                            &mMin, &mMax, &mRMS);
}

LegacyAliasBlockFile::~LegacyAliasBlockFile()
{
}

/// Construct a NEW LegacyAliasBlockFile based on this one, but writing
/// the summary data to a NEW file.
///
/// @param newFileName The filename to copy the summary data to.
BlockFilePtr LegacyAliasBlockFile::Copy(wxFileNameWrapper &&newFileName)
{
   auto newBlockFile = make_blockfile<LegacyAliasBlockFile>
      (std::move(newFileName), wxFileNameWrapper{ mAliasedFileName },
       mAliasStart, mLen, mAliasChannel,
       mSummaryInfo.totalSummaryBytes, mSummaryInfo.fields < 3);

   return newBlockFile;
}

void LegacyAliasBlockFile::SaveXML(XMLWriter &xmlFile)
// may throw
{
   xmlFile.StartTag(wxT("legacyblockfile"));

   xmlFile.WriteAttr(wxT("alias"), 1);
   xmlFile.WriteAttr(wxT("name"), mFileName.GetFullName());
   xmlFile.WriteAttr(wxT("aliaspath"), mAliasedFileName.GetFullPath());
   xmlFile.WriteAttr(wxT("aliasstart"),
                     mAliasStart.as_long_long() );
   xmlFile.WriteAttr(wxT("aliaslen"), mLen);
   xmlFile.WriteAttr(wxT("aliaschannel"), mAliasChannel);
   xmlFile.WriteAttr(wxT("summarylen"), mSummaryInfo.totalSummaryBytes);
   if (mSummaryInfo.fields < 3)
      xmlFile.WriteAttr(wxT("norms"), 1);

   xmlFile.EndTag(wxT("legacyblockfile"));
}

// regenerates the summary info, doesn't deal with missing alias files
void LegacyAliasBlockFile::Recover(){
   WriteSummary();
}
