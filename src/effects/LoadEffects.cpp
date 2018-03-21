/**********************************************************************

  Audacity: A Digital Audio Editor

  LoadEffects.cpp

  Dominic Mazzoni

**************************************************************************//**
\class BuiltinEffectsModule
\brief Internal module to auto register all built in effects.  
*****************************************************************************/

#include "../Audacity.h"
#include "../Prefs.h"

#include "LoadEffects.h"
#include "../MemoryX.h"

#include "EffectManager.h"

#include "AutoDuck.h"
#include "ChangeSpeed.h"
#include "ClickRemoval.h"
#include "DtmfGen.h"
#include "Paulstretch.h"
#include "Invert.h"
#include "Noise.h"
#include "Phaser.h"
#include "Repair.h"
#include "Repeat.h"
#include "StereoToMono.h"
#include "ToneGen.h"
#include "TruncSilence.h"

#include "FindClipping.h"

#include "../Experimental.h"

//
// Define the list of effects that will be autoregistered and how to instantiate each
//
#define EFFECT_LIST \
   EFFECT( CHIRP,             EffectToneGen, (true) )      \
   EFFECT( DTMFTONES,         EffectDtmf, () )             \
   EFFECT( NOISE,             EffectNoise, () )            \
   EFFECT( TONE,              EffectToneGen, (false) )     \
   EFFECT( CHANGESPEED,       EffectChangeSpeed, () )      \
   EFFECT( CLICKREMOVAL,      EffectClickRemoval, () )     \
   EFFECT( INVERT,            EffectInvert, () )           \
   EFFECT( PHASER,            EffectPhaser, () )           \
   EFFECT( REPAIR,            EffectRepair, () )           \
   EFFECT( REPEAT,            EffectRepeat, () )           \
   EFFECT( STEREOTOMONO,      EffectStereoToMono, () )     \
   EFFECT( TRUNCATESILENCE,   EffectTruncSilence, () )     \
   EFFECT( FINDCLIPPING,      EffectFindClipping, () )     \
   EFFECT( AUTODUCK,          EffectAutoDuck, () )         \
   EFFECT( PAULSTRETCH,       EffectPaulstretch, () )      

//
// Define the list of effects that do not get autoregistered
//
#define EXCLUDE_LIST \
   CLASSICFILTER_EFFECT

//
// Define the EFFECT() macro to generate enum names
//
#define EFFECT(n, i, args) ENUM_ ## n,

//
// Create the enum for the list of effects (will be used in a switch statement)
//
enum
{
   EFFECT_LIST
   EXCLUDE_LIST
};

//
// Redefine EFFECT() to add the effect's name to an array
//
#undef EFFECT
#define EFFECT(n, i, args) n ## _PLUGIN_SYMBOL,

//
// Create the effect name array
//
static const wxChar *kEffectNames[] =
{
   EFFECT_LIST
};

//
// Redefine EFFECT() to generate a case statement for the lookup switch
//
#undef EFFECT
#define EFFECT(n, i, args) case ENUM_ ## n: return std::make_unique<i> args;

// ============================================================================
// Module registration entry point
//
// This is the symbol that Audacity looks for when the module is built as a
// dynamic library.
//
// When the module is builtin to Audacity, we use the same function, but it is
// declared static so as not to clash with other builtin modules.
// ============================================================================
DECLARE_MODULE_ENTRY(AudacityModule)
{
   // Create and register the importer
   // Trust the module manager not to leak this
   return safenew BuiltinEffectsModule(moduleManager, path);
}

// ============================================================================
// Register this as a builtin module
// ============================================================================
DECLARE_BUILTIN_MODULE(BuiltinsEffectBuiltin);

///////////////////////////////////////////////////////////////////////////////
//
// BuiltinEffectsModule
//
///////////////////////////////////////////////////////////////////////////////

BuiltinEffectsModule::BuiltinEffectsModule(ModuleManagerInterface *moduleManager,
                                           const wxString *path)
{
   mModMan = moduleManager;
   if (path)
   {
      mPath = *path;
   }
}

BuiltinEffectsModule::~BuiltinEffectsModule()
{
   mPath.Clear();
}

// ============================================================================
// IdentInterface implementation
// ============================================================================

wxString BuiltinEffectsModule::GetPath()
{
   return mPath;
}

wxString BuiltinEffectsModule::GetSymbol()
{
   return XO("Builtin Effects");
}

wxString BuiltinEffectsModule::GetName()
{
   return XO("Builtin Effects");
}

wxString BuiltinEffectsModule::GetVendor()
{
   return XO("The Audacity Team");
}

wxString BuiltinEffectsModule::GetVersion()
{
   // This "may" be different if this were to be maintained as a separate DLL
   return AUDACITY_VERSION_STRING;
}

wxString BuiltinEffectsModule::GetDescription()
{
   return _("Provides builtin effects to Audacity");
}

// ============================================================================
// ModuleInterface implementation
// ============================================================================

bool BuiltinEffectsModule::Initialize()
{
   return true;
}

void BuiltinEffectsModule::Terminate()
{
   // Nothing to do here
   return;
}

bool BuiltinEffectsModule::AutoRegisterPlugins(PluginManagerInterface & pm)
{
   wxString ignoredErrMsg;
   for (size_t i = 0; i < WXSIZEOF(kEffectNames); i++)
   {
      wxString path(wxString(BUILTIN_EFFECT_PREFIX) + kEffectNames[i]);

      if (!pm.IsPluginRegistered(path))
      {
         // No checking of error ?
         DiscoverPluginsAtPath(path, ignoredErrMsg,
            PluginManagerInterface::DefaultRegistrationCallback);
      }
   }

   // We still want to be called during the normal registration process
   return false;
}

wxArrayString BuiltinEffectsModule::FindPluginPaths(PluginManagerInterface & WXUNUSED(pm))
{
   return mNames;
}

unsigned BuiltinEffectsModule::DiscoverPluginsAtPath(
   const wxString & path, wxString &errMsg,
   const RegistrationCallback &callback)
{
   errMsg.clear();
   auto effect = Instantiate(path);
   if (effect)
   {
      if (callback)
         callback(this, effect.get());
      return 1;
   }

   errMsg = _("Unknown built-in effect name");
   return 0;
}

bool BuiltinEffectsModule::IsPluginValid(const wxString & path, bool bFast)
{
   // bFast is unused as checking in the list is fast.
   static_cast<void>(bFast);
   return mNames.Index(path) != wxNOT_FOUND;
}

IdentInterface *BuiltinEffectsModule::CreateInstance(const wxString & path)
{
   // Acquires a resource for the application.
   // Safety of this depends on complementary calls to DeleteInstance on the module manager side.
   return Instantiate(path).release();
}

void BuiltinEffectsModule::DeleteInstance(IdentInterface *instance)
{
   // Releases the resource.
   std::unique_ptr < Effect > {
      dynamic_cast<Effect *>(instance)
   };
}

// ============================================================================
// BuiltinEffectsModule implementation
// ============================================================================

std::unique_ptr<Effect> BuiltinEffectsModule::Instantiate(const wxString & path)
{
   wxASSERT(path.StartsWith(BUILTIN_EFFECT_PREFIX));
   wxASSERT(mNames.Index(path) != wxNOT_FOUND);

   switch (mNames.Index(path))
   {
      EFFECT_LIST;
   }

   return nullptr;
}
