// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "USDStageOptions.h"
#include "USDExtraExportOptions.generated.h"

/**
 * 
 */
UCLASS(Config = Editor, Blueprintable, HideCategories=Hidden )
class USDEXTRA_API UUSDExtraExportOptions : public UObject
{
	GENERATED_BODY()

public:
	/** Basic options about the stage to export */
	UPROPERTY( EditAnywhere, config, BlueprintReadWrite, Category = "Stage options", meta = ( ShowOnlyInnerProperties ) )
	FUsdStageOptions StageOptions;
	
	/** StartTimeCode to be used for all exported layers */
	UPROPERTY(BlueprintReadWrite)
	float StartTimeCode;

	/** EndTimeCode to be used for all exported layers */
	UPROPERTY(BlueprintReadWrite)
	float EndTimeCode;
	
	/** Asset to export */
	UPROPERTY(BlueprintReadWrite)
	UWorld* World = nullptr;

	/** File to export as */
	UPROPERTY(BlueprintReadWrite)
	FString FileName;

	/** Whether to export only the selected actors, and assets used by them */
	UPROPERTY(BlueprintReadWrite)
	bool bSelectionOnly = false;

	/** Whether to bake UE materials and add material bindings to the baked assets */
	UPROPERTY(BlueprintReadWrite)
	bool bBakeMaterials;

	/** Resolution to use when baking materials into textures */
	UPROPERTY(BlueprintReadWrite)
	FIntPoint BakeResolution = FIntPoint( 512, 512 );

	/** Whether to remove the 'unrealMaterial' attribute after binding the corresponding baked material */
	UPROPERTY(BlueprintReadWrite)
	bool bRemoveUnrealMaterials;
	
	/** If true, the actual static/skeletal mesh data is exported in "payload" files, and referenced via the payload composition arc */
	UPROPERTY( BlueprintReadWrite)
	bool bUsePayload;

	/** USD format to use for exported payload files */
	UPROPERTY( BlueprintReadWrite)
	FString PayloadFormat;

	/** Whether to use UE actor folders as empty prims */
	UPROPERTY( BlueprintReadWrite)
	bool bExportActorFolders = true;
	
	/** If true, will export sub-levels as separate layers (referenced as sublayers). If false, will collapse all sub-levels in a single exported root layer */
	UPROPERTY( BlueprintReadWrite)
	bool bExportSublayers;
	
	/** Names of levels that should be ignored when collecting actors to export (e.g. "Persistent Level", "Level1", "MySubLevel", etc.) */
	UPROPERTY(BlueprintReadWrite)
	TSet<FString> LevelsToIgnore;
};
