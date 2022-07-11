// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExtraEdMode.h"
#include "USDExtraEdModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"
#include "USDExtra.h"


const FEditorModeID FUSDExtraEdMode::EM_USDExtraEdModeId = TEXT("EM_USDExtraEdMode");

FUSDExtraEdMode::FUSDExtraEdMode()
{

}

FUSDExtraEdMode::~FUSDExtraEdMode()
{

}

void FUSDExtraEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FUSDExtraEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FUSDExtraEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	// Call base Exit method to ensure proper cleanup
	FUSDExtraModule& USDModule = FModuleManager::LoadModuleChecked<FUSDExtraModule>("USDExtra");
	USDModule.RemoveUSDDetails();
	FEdMode::Exit();
}

bool FUSDExtraEdMode::UsesToolkits() const
{
	return true;
}

void FUSDExtraEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdMode::AddReferencedObjects(Collector);
}




