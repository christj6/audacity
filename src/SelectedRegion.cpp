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
