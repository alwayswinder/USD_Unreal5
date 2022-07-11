// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExtraEdModeToolkit.h"
#include "USDExtraEdMode.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"
#include "USDExtraUtils.h"
#include "Widgets\Input\SFilePathPicker.h"
#include "EditorDirectories.h"
#include "EditorStyleSet.h"
#include "USDExtraSettings.h"
#include "Misc\FileHelper.h"
#include "USDExtra.h"

#define LOCTEXT_NAMESPACE "FUSDExtraEdModeToolkit"



FUSDExtraEdModeToolkit::FUSDExtraEdModeToolkit()
{
	UWorld* World = GetEditorMode()->GetWorld();
	check(World)

	const UUSDExtraSettings* USDExtraSettings = GetDefault<UUSDExtraSettings>();
	if (USDExtraSettings->USDRepository.Path.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("No Valid USD Repository, check the project setting"));
		return;
	}
	FilePath = USDExtraSettings->USDRepository.Path;
	FolderPath = FilePath;
	FilePath += "/" + World->GetName();
	FilePath += UEnum::GetDisplayValueAsText(USDExtraSettings->USDFormat).ToString();
	
	if (!FPaths::FileExists(FilePath))
	{
		const TCHAR* TestText = TEXT("Test");
		FFileHelper::SaveStringToFile(TestText, *FilePath);
	}
}

void FUSDExtraEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	//FUSDExtraModule& USDModule = FModuleManager::LoadModuleChecked<FUSDExtraModule>("USDExtra");
	//UObject *USDDetails = USDModule.GetUSDDetails();
	//FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	//FDetailsViewArgs Args(false, false, false, FDetailsViewArgs::HideNameArea, false);

	//Args.bShowActorLabel = false;
	//DetailsWidget = PropertyModule.CreateDetailView(Args);
	//DetailsWidget->SetObject(USDDetails);

	SAssignNew(ToolkitWidget, SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SFilePathPicker)
				.BrowseButtonImage(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.BrowseButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.BrowseButtonToolTip(LOCTEXT("FileButtonToolTipText", "Choose a USD file..."))
				.BrowseDirectory(FolderPath)
				.BrowseTitle(LOCTEXT("BrowseButtonTitle", "Choose a USD file"))
				.FileTypeFilter(TEXT("USD File (*.usda)|*.usda|(*.usd)|*.usd"))
				.FilePath(this, &FUSDExtraEdModeToolkit::GetFilePath)
				.OnPathPicked(this, &FUSDExtraEdModeToolkit::FilePathPicked)
			]
			+ SVerticalBox::Slot()
			[
				SNew(SButton)
				.OnClicked(this,&FUSDExtraEdModeToolkit::OnImport)
				[
					SNew(STextBlock)
					.Text(FText::FromString("Import Scene"))
				]
			]
			+ SVerticalBox::Slot()
			[
				SNew(SButton)
				.OnClicked(this,&FUSDExtraEdModeToolkit::OnExport)
				[
					SNew(STextBlock)
					.Text(FText::FromString("Export Scene"))
				]
			]
		];
		/*+SVerticalBox::Slot()
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			.Style(FEditorStyle::Get(), "FoliageEditMode.Splitter")
			+ SSplitter::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(0, 2)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString("USDReference"))
				]

				+ SVerticalBox::Slot()
				[
					DetailsWidget.ToSharedRef()
				]
			]
		];*/
		
	FModeToolkit::Init(InitToolkitHost);
}

FName FUSDExtraEdModeToolkit::GetToolkitFName() const
{
	return FName("USDExtraEdMode");
}

FText FUSDExtraEdModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("USDExtraEdModeToolkit", "DisplayName", "USDExtraEdMode Tool");
}

FReply FUSDExtraEdModeToolkit::OnImport() const
{
	UWorld* World = GetEditorMode()->GetWorld();
	check(World)

	GEditor->GetSelectedActors()->Modify();
	GEditor->SelectNone(true, true, false);

	UUSDExtraUtils::ImportUSDToLevel(World, FilePath);
	
	UE_LOG(LogTemp, Log, TEXT("USDExtraImport"));

	return FReply::Handled();
}

FReply FUSDExtraEdModeToolkit::OnExport() const
{
	UWorld* World = GetEditorMode()->GetWorld();
	check(World)

	UUSDExtraUtils::ExportLevelToUSD(World, FilePath);

	UE_LOG(LogTemp, Log, TEXT("USDExtraExport"));

	return FReply::Handled();
}

FString FUSDExtraEdModeToolkit::GetFilePath() const
{
	return FilePath;
}

void FUSDExtraEdModeToolkit::FilePathPicked(const FString& PickedPath)
{
	FilePath = PickedPath;
}

class FEdMode* FUSDExtraEdModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FUSDExtraEdMode::EM_USDExtraEdModeId);
}

#undef LOCTEXT_NAMESPACE
