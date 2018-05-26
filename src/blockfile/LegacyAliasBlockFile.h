/**********************************************************************

  Audacity: A Digital Audio Editor

  LegacyAliasBlockFile.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_LEGACYALIASBLOCKFILE__
#define __AUDACITY_LEGACYALIASBLOCKFILE__

#include "../BlockFile.h"
#include "PCMAliasBlockFile.h"

/// An AliasBlockFile that references uncompressed data in an existing file
class LegacyAliasBlockFile final : public PCMAliasBlockFile
{
 public:

   // Constructor / Destructor

   /// Constructs a LegacyAliasBlockFile, writing the summary to disk
   LegacyAliasBlockFile(wxFileNameWrapper &&fileName,
                        wxFileNameWrapper &&aliasedFileName,
                        sampleCount aliasStart,
                        size_t aliasLen,
                        int aliasChannel,
                        size_t summaryLen,
                        bool noRMS);
   virtual ~LegacyAliasBlockFile();

   BlockFilePtr Copy(wxFileNameWrapper &&fileName) override;
   void Recover() override;
};

#endif
