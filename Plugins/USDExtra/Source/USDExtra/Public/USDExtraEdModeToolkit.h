// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"



class FUSDExtraEdModeToolkit : public FModeToolkit
{
public:

	FUSDExtraEdModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

private:
	FReply OnImport() const;
	
	FReply OnExport() const;

	FString GetFilePath() const;
	void FilePathPicked(const FString& PickedPath);

private:
	FString FilePath;
	FString FolderPath;


	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<class IDetailsView> DetailsWidget;
};
