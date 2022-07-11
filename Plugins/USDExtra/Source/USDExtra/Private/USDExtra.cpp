// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExtra.h"
#include "USDExtraEdMode.h"
#include "USDMemory.h"
#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "FUSDExtraModule"

AUSDDetails::AUSDDetails(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{

}
void FMyCustomUSDDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideCategory("Tick");
	DetailBuilder.HideCategory("Replication");
	DetailBuilder.HideCategory("Collision");
	DetailBuilder.HideCategory("Input");
	DetailBuilder.HideCategory("Actor");
	DetailBuilder.HideCategory("LOD");
	DetailBuilder.HideCategory("Cooking");
	DetailBuilder.HideCategory("Rendering");
}


void FUSDExtraModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FEditorModeRegistry::Get().RegisterMode<FUSDExtraEdMode>(FUSDExtraEdMode::EM_USDExtraEdModeId, LOCTEXT("USDExtraEdModeName", "USD Extra"), FSlateIcon(), true);
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.RegisterCustomClassLayout
	(	
		"USDDetails",
		FOnGetDetailCustomizationInstance::CreateLambda([] {return MakeShareable(new FMyCustomUSDDetails); })
	);
}

void FUSDExtraModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FEditorModeRegistry::Get().UnregisterMode(FUSDExtraEdMode::EM_USDExtraEdModeId);
}
UObject* FUSDExtraModule::GetUSDDetails()
{
	if (!IsValid(USDDetails))
	{
		USDDetails = GWorld->SpawnActor<AUSDDetails>();
	}
	return USDDetails;
}
void FUSDExtraModule::RemoveUSDDetails()
{
	if (IsValid(USDDetails))
	{
		GWorld->DestroyActor(USDDetails);
	}
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE_USD(FUSDExtraModule, USDExtra)