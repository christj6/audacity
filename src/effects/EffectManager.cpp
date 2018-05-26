/**********************************************************************

  Audacity: A Digital Audio Editor

  EffectManager.cpp

  Audacity(R) is copyright (c) 1999-2008 Audacity Team.
  License: GPL v2.  See License.txt.

******************************************************************//**

\class EffectManager
\brief EffectManager is the class that handles effects and effect categories.

It maintains a graph of effect categories and subcategories,
registers and unregisters effects and can return filtered lists of
effects.

*//*******************************************************************/

#include "../Audacity.h"

#include <algorithm>
#include <wx/stopwatch.h>
#include <wx/tokenzr.h>

#include "../Experimental.h"
#include "../widgets/ErrorDialog.h"

#include "EffectManager.h"
#include "../commands/Command.h"
#include "../commands/CommandContext.h"


/*******************************************************************************
Creates a singleton and returns reference

 (Thread-safe...no active threading during construction or after destruction)
*******************************************************************************/
EffectManager & EffectManager::Get()
{
   static EffectManager em;
   return em;
}

EffectManager::EffectManager()
{
   mRealtimeLock.Enter();
   mRealtimeActive = false;
   mRealtimeLock.Leave();
   mSkipStateFlag = false;
}

EffectManager::~EffectManager()
{
}

// Here solely for the purpose of Nyquist Workbench until
// a better solution is devised.
const PluginID & EffectManager::RegisterEffect(Effect *f)
{
   const PluginID & ID = PluginManager::Get().RegisterPlugin(f, PluginTypeEffect);

   mEffects[ID] = f;

   return ID;
}

// Here solely for the purpose of Nyquist Workbench until
// a better solution is devised.
void EffectManager::UnregisterEffect(const PluginID & ID)
{
   PluginID id = ID;
   PluginManager::Get().UnregisterPlugin(id);
   mEffects.erase(id);
}

bool EffectManager::DoEffect(const PluginID & ID,
                             wxWindow *parent,
                             double projectRate,
                             TrackList *list,
                             TrackFactory *factory,
                             SelectedRegion *selectedRegion,
                             bool shouldPrompt /* = true */)

{
   this->SetSkipStateFlag(false);
   Effect *effect = GetEffect(ID);
   
   if (!effect)
   {
      return false;
   }

   bool res = effect->DoEffect(parent,
                               projectRate,
                               list,
                               factory,
                               selectedRegion,
                               shouldPrompt);

   return res;
}

bool EffectManager::DoAudacityCommand(const PluginID & ID,
                             const CommandContext &context,
                             wxWindow *parent,
                             bool shouldPrompt /* = true */)

{
   this->SetSkipStateFlag(false);
   AudacityCommand *command = GetAudacityCommand(ID);
   
   if (!command)
   {
      return false;
   }

   bool res = command->DoAudacityCommand(parent, context, shouldPrompt);

   return res;
}

wxString EffectManager::GetCommandName(const PluginID & ID)
{
   return GetCustomTranslation( PluginManager::Get().GetName(ID) );
}

wxString EffectManager::GetEffectFamilyName(const PluginID & ID)
{
   auto effect = GetEffect(ID);
   if (effect)
      return effect->GetFamilyName();
   return {};
}

wxString EffectManager::GetCommandIdentifier(const PluginID & ID)
{
   wxString name = (PluginManager::Get().GetSymbol(ID));

   // Get rid of leading and trailing white space
   name.Trim(true).Trim(false);

   if (name == wxEmptyString)
   {
      return name;
   }

   wxStringTokenizer st(name, wxT(" "));
   wxString id;

   // CamelCase the name
   while (st.HasMoreTokens())
   {
      wxString tok = st.GetNextToken();

      id += tok.Left(1).MakeUpper() + tok.Mid(1).MakeLower();
   }

   return id;
}

wxString EffectManager::GetCommandDescription(const PluginID & ID)
{
   if (GetEffect(ID))
      return wxString::Format(_("Applied effect: %s"), GetCommandName(ID));
   if (GetAudacityCommand(ID))
      return wxString::Format(_("Applied command: %s"), GetCommandName(ID));

   return wxEmptyString;
}

wxString EffectManager::GetCommandUrl(const PluginID & ID)
{
   Effect* pEff = GetEffect(ID);
   if( pEff )
      return pEff->ManualPage();
   AudacityCommand * pCom = GetAudacityCommand(ID);
   if( pCom )
      return pCom->ManualPage();

   return wxEmptyString;
}

wxString EffectManager::GetCommandTip(const PluginID & ID)
{
   Effect* pEff = GetEffect(ID);
   if( pEff )
      return pEff->GetDescription();
   AudacityCommand * pCom = GetAudacityCommand(ID);
   if( pCom )
      return pCom->GetDescription();

   return wxEmptyString;
}


void EffectManager::GetCommandDefinition(const PluginID & ID, const CommandContext & context, int flags)
{
   ParamsInterface *command;
   command = GetEffect(ID);
   if( !command )
      command = GetAudacityCommand( ID );
   if( !command )
      return;

   ShuttleParams NullShuttle;
   // Test if it defines any parameters at all.
   bool bHasParams = command->DefineParams( NullShuttle );
   if( (flags ==0) && !bHasParams )
      return;

   // This is capturing the output context into the shuttle.
   ShuttleGetDefinition S(  *context.pOutput.get()->mStatusTarget.get() );
   S.StartStruct();
   S.AddItem( GetCommandIdentifier( ID ), "id" );
   S.AddItem( GetCommandName( ID ), "name" );
   if( bHasParams ){
      S.StartField( "params" );
      S.StartArray();
      command->DefineParams( S );
      S.EndArray();
      S.EndField();
   }
   S.AddItem( GetCommandUrl( ID ), "url" );
   S.AddItem( GetCommandTip( ID ), "tip" );
   S.EndStruct();
}



bool EffectManager::IsHidden(const PluginID & ID)
{
   Effect *effect = GetEffect(ID);

   if (effect)
   {
      return effect->IsHidden();
   }

   return false;
}

void EffectManager::SetSkipStateFlag(bool flag)
{
   mSkipStateFlag = flag;
}

bool EffectManager::GetSkipStateFlag()
{
   return mSkipStateFlag;
}

bool EffectManager::SupportsAutomation(const PluginID & ID)
{
   const PluginDescriptor *plug =  PluginManager::Get().GetPlugin(ID);
   if (plug)
   {
      return plug->IsEffectAutomatable();
   }

   return false;
}

wxString EffectManager::GetEffectParameters(const PluginID & ID)
{
   Effect *effect = GetEffect(ID);
   
   if (effect)
   {
      wxString parms;

      effect->GetAutomationParameters(parms);

      return parms;
   }

   AudacityCommand *command = GetAudacityCommand(ID);
   
   if (command)
   {
      wxString parms;

      command->GetAutomationParameters(parms);

      return parms;
   }
   return wxEmptyString;
}

bool EffectManager::SetEffectParameters(const PluginID & ID, const wxString & params)
{
   Effect *effect = GetEffect(ID);
   
   if (effect)
   {
      CommandParameters eap(params);

      if (eap.HasEntry(wxT("Use Preset")))
      {
         return effect->SetAutomationParameters(eap.Read(wxT("Use Preset")));
      }

      return effect->SetAutomationParameters(params);
   }
   AudacityCommand *command = GetAudacityCommand(ID);
   
   if (command)
   {
      // Set defaults (if not initialised) before setting values.
      command->Init(); 
      CommandParameters eap(params);

      if (eap.HasEntry(wxT("Use Preset")))
      {
         return command->SetAutomationParameters(eap.Read(wxT("Use Preset")));
      }

      return command->SetAutomationParameters(params);
   }
   return false;
}

bool EffectManager::PromptUser(const PluginID & ID, wxWindow *parent)
{
   bool result = false;
   Effect *effect = GetEffect(ID);

   if (effect)
   {
      result = effect->PromptUser(parent);
      return result;
   }

   AudacityCommand *command = GetAudacityCommand(ID);

   if (command)
   {
      result = command->PromptUser(parent);
      return result;
   }

   return result;
}

bool EffectManager::HasPresets(const PluginID & ID)
{
   Effect *effect = GetEffect(ID);

   if (!effect)
   {
      return false;
   }

   return effect->GetUserPresets().GetCount() > 0 ||
          effect->GetFactoryPresets().GetCount() > 0;
}

wxString EffectManager::GetPreset(const PluginID & ID, const wxString & params, wxWindow * parent)
{
   Effect *effect = GetEffect(ID);

   if (!effect)
   {
      return wxEmptyString;
   }

   CommandParameters eap(params);

   wxString preset;
   if (eap.HasEntry(wxT("Use Preset")))
   {
      preset = eap.Read(wxT("Use Preset"));
   }

   preset = effect->GetPreset(parent, preset);
   if (preset.IsEmpty())
   {
      return preset;
   }

   eap.DeleteAll();
   
   eap.Write(wxT("Use Preset"), preset);
   eap.GetParameters(preset);

   return preset;
}

void EffectManager::SetBatchProcessing(const PluginID & ID, bool start)
{
   Effect *effect = GetEffect(ID);
   if (effect)
   {
      effect->SetBatchProcessing(start);
      return;
   }

   AudacityCommand *command = GetAudacityCommand(ID);
   if (command)
   {
      command->SetBatchProcessing(start);
      return;
   }

}

Effect *EffectManager::GetEffect(const PluginID & ID)
{
   // Must have a "valid" ID
   if (ID.IsEmpty())
   {
      return NULL;
   }

   // If it is actually a command then refuse it (as an effect).
   if( mCommands.find( ID ) != mCommands.end() )
      return NULL;

   // TODO: This is temporary and should be redone when all effects are converted
   if (mEffects.find(ID) == mEffects.end())
   {
      // This will instantiate the effect client if it hasn't already been done
      EffectDefinitionInterface *ident = dynamic_cast<EffectDefinitionInterface *>(PluginManager::Get().GetInstance(ID));
      if (ident)
      {
         auto effect = dynamic_cast<Effect *>(ident);
         if (effect && effect->Startup(NULL))
         {
            mEffects[ID] = effect;
            return effect;
         }
      }

      auto effect = std::make_shared<Effect>(); // TODO: use make_unique and store in std::unordered_map
      if (effect)
      {
         EffectClientInterface *client = dynamic_cast<EffectClientInterface *>(ident);
         if (client && effect->Startup(client))
         {
            auto pEffect = effect.get();
            mEffects[ID] = pEffect;
            mHostEffects[ID] = std::move(effect);
            return pEffect;
         }
      }

      auto command = dynamic_cast<AudacityCommand *>(PluginManager::Get().GetInstance(ID));
      if( !command )
         AudacityMessageBox(wxString::Format(_("Attempting to initialize the following effect failed:\n\n%s\n\nMore information may be available in Help->Show Log"),
                                    GetCommandName(ID)),
                   _("Effect failed to initialize"));

      return NULL;
   }

   return mEffects[ID];
}

AudacityCommand *EffectManager::GetAudacityCommand(const PluginID & ID)
{
   // Must have a "valid" ID
   if (ID.IsEmpty())
   {
      return NULL;
   }

   // TODO: This is temporary and should be redone when all effects are converted
   if (mCommands.find(ID) == mCommands.end())
   {

      // This will instantiate the effect client if it hasn't already been done
      auto command = dynamic_cast<AudacityCommand *>(PluginManager::Get().GetInstance(ID));
      if (command )//&& command->Startup(NULL))
      {
         command->Init();
         mCommands[ID] = command;
         return command;
      }

      AudacityMessageBox(wxString::Format(_("Attempting to initialize the following command failed:\n\n%s\n\nMore information may be available in Help->Show Log"),
                                    GetCommandName(ID)),
                   _("Command failed to initialize"));

      return NULL;
   }

   return mCommands[ID];
}


const PluginID & EffectManager::GetEffectByIdentifier(const wxString & strTarget)
{
   static PluginID empty;
   if (strTarget == wxEmptyString) // set GetCommandIdentifier to wxT("") to not show an effect in Batch mode
   {
      return empty;
   }

   PluginManager & pm = PluginManager::Get();
   // Effects OR Generic commands...
   const PluginDescriptor *plug = pm.GetFirstPlugin(PluginTypeEffect | PluginTypeAudacityCommand);
   while (plug)
   {
      if (GetCommandIdentifier(plug->GetID()).IsSameAs(strTarget, false))
      {
         return plug->GetID();
      }
      plug = pm.GetNextPlugin(PluginTypeEffect | PluginTypeAudacityCommand);
   }
   return empty;;
}

