/**********************************************************************

  Audacity: A Digital Audio Editor

  XMLTagHandler.h

  Dominic Mazzoni
  Vaughan Johnson

  The XMLTagHandler class is an interface which should be implemented by
  classes which wish to be able to load and save themselves
  using XML files.

  The XMLValueChecker class implements static bool methods for checking
  input values from XML files.

**********************************************************************/
#ifndef __AUDACITY_XML_TAG_HANDLER__
#define __AUDACITY_XML_TAG_HANDLER__

#include "../Audacity.h"
#include <wx/string.h>
#include <stdio.h>

#include "XMLWriter.h"
class XMLValueChecker
{
public:
   // "Good" means well-formed and for the file-related functions, names an existing file or folder.
   // These are used in HandleXMLTag and BuildFomXML methods to check the input for
   // security vulnerabilites, per the NGS report for UmixIt.
   static bool IsGoodString(const wxString & str);

   static bool IsGoodFileName(const wxString & strFileName, const wxString & strDirName = wxEmptyString);
   static bool IsGoodFileString(const wxString &str);
   static bool IsGoodSubdirName(const wxString & strSubdirName, const wxString & strDirName = wxEmptyString);
   static bool IsGoodPathName(const wxString & strPathName);
   static bool IsGoodPathString(const wxString &str);

   /** @brief Check that the supplied string can be converted to a long (32bit)
	* integer.
	*
	* Note that because wxString::ToLong does additional testing, IsGoodInt doesn't
	* duplicate that testing, so use wxString::ToLong after IsGoodInt, not just
	* atoi.
	* @param strInt The string to test
	* @return true if the string is convertable, false if not
	*/
   static bool IsGoodInt(const wxString & strInt);
   /** @brief Check that the supplied string can be converted to a 64bit
	* integer.
	*
	* Note that because wxString::ToLongLong does additional testing, IsGoodInt64
	* doesn't duplicate that testing, so use wxString::ToLongLong after IsGoodInt64
	* not just atoll.
	* @param strInt The string to test
	* @return true if the string is convertable, false if not
	*/
   static bool IsGoodInt64(const wxString & strInt);
   static bool IsGoodIntForRange(const wxString & strInt, const wxString & strMAXABS);

   static bool IsValidChannel(const int nValue);
   static bool IsValidSampleFormat(const int nValue); // true if nValue is one sampleFormat enum values
};


class AUDACITY_DLL_API XMLTagHandler /* not final */ {
 public:
   XMLTagHandler(){};
   virtual ~XMLTagHandler(){};
   //
   // Methods to override
   //
};

#endif // define __AUDACITY_XML_TAG_HANDLER__

