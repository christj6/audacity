/**********************************************************************

Audacity: A Digital Audio Editor

SelectedRegion.cpp

Paul Licameli

*******************************************************************/

#include "Internat.h"
#include "SelectedRegion.h"

#include "Experimental.h"
const wxChar *SelectedRegion::sDefaultT0Name = wxT("selStart");
const wxChar *SelectedRegion::sDefaultT1Name = wxT("selEnd");

namespace {
const wxChar *sDefaultF0Name = wxT("selLow");
const wxChar *sDefaultF1Name = wxT("selHigh");
}

bool SelectedRegion::HandleXMLAttribute
(const wxChar *attr, const wxChar *value,
 const wxChar *legacyT0Name, const wxChar *legacyT1Name)
{
   typedef bool (SelectedRegion::*Setter)(double, bool);
   Setter setter = 0;
   if (!wxStrcmp(attr, legacyT0Name))
      setter = &SelectedRegion::setT0;
   else if (!wxStrcmp(attr, legacyT1Name))
      setter = &SelectedRegion::setT1;
   else
      return false;

   double dblValue;
   if (!Internat::CompatibleToDouble(value, &dblValue))
      return false;

   // False means don't flip time or frequency boundaries
   (void)(this->*setter)(dblValue, false);
   return true;
}
