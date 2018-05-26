/**********************************************************************

  Audacity: A Digital Audio Editor

  XMLTagHandler.cpp

  Dominic Mazzoni
  Vaughan Johnson


*//****************************************************************//**

\class XMLTagHandler
\brief This class is an interface which should be implemented by
  classes which wish to be able to load and save themselves
  using XML files.

\class XMLValueChecker
\brief XMLValueChecker implements static bool methods for checking
  input values from XML files.

*//*******************************************************************/

#include "../Audacity.h"
#include "XMLTagHandler.h"

#include "../MemoryX.h"
#include "../Internat.h"

#ifdef _WIN32
   #include <windows.h>
   #include <wx/msw/winundef.h>
#endif

#include <wx/defs.h>
#include <wx/arrstr.h>
#include <wx/filename.h>

#include "../SampleFormat.h"
#include "../Track.h"

bool XMLValueChecker::IsGoodString(const wxString & str)
{
   size_t len = str.Length();
   int nullIndex = str.Find('\0', false);
   if ((len <= PLATFORM_MAX_PATH) && // Shouldn't be any reason for longer strings, except intentional file corruption.
         (nullIndex == -1)) // No null characters except terminator.
      return true;
   else
      return false; // good place for a breakpoint
}

// "Good" means the name is well-formed and names an existing file or folder.
bool XMLValueChecker::IsGoodFileName(const wxString & strFileName, const wxString & strDirName /* = "" */)
{
   // Test strFileName.
   if (!IsGoodFileString(strFileName) ||
         (strDirName.Length() + 1 + strFileName.Length() > PLATFORM_MAX_PATH))
      return false;

   // Test the corresponding wxFileName.
   wxFileName fileName(strDirName, strFileName);
   return (fileName.IsOk() && fileName.FileExists());
}

bool XMLValueChecker::IsGoodFileString(const wxString &str)
{
   return (IsGoodString(str) &&
            !str.IsEmpty() &&

            // FILENAME_MAX is 260 in MSVC, but inconsistent across platforms,
            // sometimes huge, but we use 260 for all platforms.
            (str.Length() <= 260) &&

            (str.Find(wxFileName::GetPathSeparator()) == -1)); // No path separator characters.
}

bool XMLValueChecker::IsGoodSubdirName(const wxString & strSubdirName, const wxString & strDirName /* = "" */)
{
   // Test strSubdirName.
   // Note this prevents path separators, and relative path to parents (strDirName),
   // so fixes vulnerability #3 in the NGS report for UmixIt,
   // where an attacker could craft an AUP file with relative pathnames to get to system files, for example.
   if (!IsGoodFileString(strSubdirName) ||
         (strSubdirName == wxT(".")) || (strSubdirName == wxT("..")) ||
         (strDirName.Length() + 1 + strSubdirName.Length() > PLATFORM_MAX_PATH))
      return false;

   // Test the corresponding wxFileName.
   wxFileName fileName(strDirName, strSubdirName);
   return (fileName.IsOk() && fileName.DirExists());
}

bool XMLValueChecker::IsGoodPathName(const wxString & strPathName)
{
   // Test the corresponding wxFileName.
   wxFileName fileName(strPathName);
   return XMLValueChecker::IsGoodFileName(fileName.GetFullName(), fileName.GetPath(wxPATH_GET_VOLUME));
}

bool XMLValueChecker::IsGoodPathString(const wxString &str)
{
   return (IsGoodString(str) &&
            !str.IsEmpty() &&
            (str.Length() <= PLATFORM_MAX_PATH));
}


bool XMLValueChecker::IsGoodIntForRange(const wxString & strInt, const wxString & strMAXABS)
{
   if (!IsGoodString(strInt))
      return false;

   // Check that the value won't overflow.
   // Must lie between -Range and +Range-1
   // We're strict about disallowing spaces and commas, and requiring minus sign to be first 
   // char for negative. No + sign for positive numbers.  It's disallowed, not optional.

   const size_t lenMAXABS = strMAXABS.Length();
   const size_t lenStrInt = strInt.Length();

   if( lenStrInt < 1 )
      return false;
   size_t offset = (strInt[0] == '-') ?1:0;
   if( lenStrInt <= offset )
      return false;// string too short, no digits in it.

   if (lenStrInt > (lenMAXABS + offset))
      return false;

   unsigned int i;
   for (i = offset; i < lenStrInt; i++)
      if (strInt[i] < '0' || strInt[i] > '9' )
          return false; // not a digit

   // All chars were digits.
   if( lenStrInt < (lenMAXABS + offset) )
      return true; // too few digits to overflow.

   // Numerical part is same length as strMAXABS
   for (i = 0; i < lenMAXABS; i++)
      if (strInt[i+offset] < strMAXABS[i])
         return true; // number is small enough
      else if (strInt[i+offset] > strMAXABS[i])
         return false; // number is too big.

   // Digits were textually equal to strMAXABS
   // That's OK if negative, but not OK if positive.
   return (strInt[0] == '-');
}


bool XMLValueChecker::IsGoodInt(const wxString & strInt)
{
   // Signed long: -2,147,483,648 to +2,147,483,647, i.e., -2^31 to 2^31-1
   return IsGoodIntForRange( strInt, "2147483648" );
}

bool XMLValueChecker::IsGoodInt64(const wxString & strInt)
{
   // Signed 64-bit:  -9,223,372,036,854,775,808 to +9,223,372,036,854,775,807, i.e., -2^63 to 2^63-1
   return IsGoodIntForRange( strInt, "9223372036854775808" );
}

bool XMLValueChecker::IsValidChannel(const int nValue)
{
   return (nValue >= Track::LeftChannel) && (nValue <= Track::MonoChannel);
}

bool XMLValueChecker::IsValidSampleFormat(const int nValue)
{
   return (nValue == int16Sample) || (nValue == int24Sample) || (nValue == floatSample);
}

void XMLTagHandler::ReadXMLEndTag(const char *tag)
{
}

void XMLTagHandler::ReadXMLContent(const char *s, int len)
{
   HandleXMLContent(wxString(s, wxConvUTF8, len));
}
