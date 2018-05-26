/**********************************************************************

  Audacity: A Digital Audio Editor

  SilentBlockFile.cpp

  Joshua Haberman

**********************************************************************/

#include "../Audacity.h"
#include "SilentBlockFile.h"
#include "../FileFormats.h"

SilentBlockFile::SilentBlockFile(size_t sampleLen):
BlockFile{ wxFileNameWrapper{}, sampleLen }
{
   mMin = 0.;
   mMax = 0.;
   mRMS = 0.;
}

SilentBlockFile::~SilentBlockFile()
{
}

bool SilentBlockFile::ReadSummary(ArrayOf<char> &data)
{
   data.reinit( mSummaryInfo.totalSummaryBytes );
   memset(data.get(), 0, mSummaryInfo.totalSummaryBytes);
   return true;
}

size_t SilentBlockFile::ReadData(samplePtr data, sampleFormat format,
                              size_t WXUNUSED(start), size_t len, bool) const
{
   ClearSamples(data, format, 0, len);

   return len;
}

void SilentBlockFile::SaveXML(XMLWriter &xmlFile)
// may throw
{
   xmlFile.StartTag(wxT("silentblockfile"));

   xmlFile.WriteAttr(wxT("len"), mLen);

   xmlFile.EndTag(wxT("silentblockfile"));
}

/// Create a copy of this BlockFile
BlockFilePtr SilentBlockFile::Copy(wxFileNameWrapper &&)
{
   auto newBlockFile = make_blockfile<SilentBlockFile>(mLen);

   return newBlockFile;
}

auto SilentBlockFile::GetSpaceUsage() const -> DiskByteCount
{
   return 0;
}

