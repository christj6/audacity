/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2009 Audacity Team
   License: wxWidgets

   Dan Horgan

******************************************************************//**

\file CommandDirectory.cpp
\brief A dictionary of supported scripting commands, including 
functions to look up a command by name.

*//*******************************************************************/

#include "../Audacity.h"
#include "CommandDirectory.h"
#include "CommandMisc.h"

#include "HelpCommand.h"
#include "MessageCommand.h"
#include "BatchEvalCommand.h"

std::unique_ptr<CommandDirectory> CommandDirectory::mInstance;

CommandDirectory::CommandDirectory()
{
   // Create the command map.
   // First we have commands which return information
   AddCommand(make_movable<BatchEvalCommandType>());
}

CommandDirectory::~CommandDirectory()
{
}

OldStyleCommandType *CommandDirectory::LookUp(const wxString &cmdName) const
{
   CommandMap::const_iterator iter = mCmdMap.find(cmdName);
   if (iter == mCmdMap.end())
   {
      return NULL;
   }
   return iter->second.get();
}

void CommandDirectory::AddCommand(movable_ptr<OldStyleCommandType> &&type)
{
   wxASSERT(type != NULL);
   wxString cmdName = type->GetName();
   wxASSERT_MSG(mCmdMap.find(cmdName) == mCmdMap.end()
         , wxT("A command named ") + cmdName
         + wxT(" already exists."));

   mCmdMap[cmdName] = std::move(type);
}

CommandDirectory *CommandDirectory::Get()
{
   if (!mInstance)
      mInstance.reset(safenew CommandDirectory());
   return mInstance.get();
}
