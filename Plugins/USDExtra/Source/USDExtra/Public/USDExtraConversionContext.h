// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UnrealUSDWrapper.h"
#include "USDExtraUtils.h"
#include "UsdWrappers/UsdStage.h"
#include "USDExtraConversionContext.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
/**
 * 
 */
UCLASS(meta=(ScriptName="UsdExtraConversionContext"))
class USDEXTRA_API UUSDExtraConversionContext : public UObject
{
	GENERATED_BODY()

	virtual ~UUSDExtraConversionContext();

public:
	/**
	 * Opens or creates a USD stage using `StageRootLayerPath` as root layer, creating the root layer if needed.
	 * All future conversions will fetch prims and get/set USD data to/from this stage.
	 * Note: You must remember to call Cleanup() when done, or else this object will permanently hold a reference to the opened stage!
	 */
	UFUNCTION( BlueprintCallable, Category = "Export context" )
	void SetStageRootLayer( FFilePath StageRootLayerPath );
	
	/**
	 * Sets the current edit target of our internal stage. When calling the conversion functions, prims and attributes
	 * will be authored on this edit target only
	 */
	UFUNCTION( BlueprintCallable, Category = "Export context" )
	void SetEditTarget( FFilePath EditTargetLayerPath );
	
	/**
	* Discards the currently opened stage. This is critical when using this class via scripting: The C++ destructor will
	* not be called when the python object runs out of scope, so we would otherwise keep a strong reference to the stage
	*/
	UFUNCTION( BlueprintCallable, Category = "Export context" )
	void Cleanup();

public:
	UFUNCTION( BlueprintCallable, Category = "Component conversion" )
	bool ConvertSceneComponent( const USceneComponent* Component, const FString& PrimPath );
	
	UFUNCTION( BlueprintCallable, Category = "Component conversion" )
	bool ConvertMeshComponent( const UMeshComponent* Component, const FString& PrimPath );

	UFUNCTION( BlueprintCallable, Category = "Component conversion" )
	bool ConvertBrushComponent( const UBrushComponent* Component, const FString& PrimPath);

	UFUNCTION( BlueprintCallable, Category = "Component conversion" )
	bool ConvertHismComponent( const UHierarchicalInstancedStaticMeshComponent* Component, const FString& PrimPath, float TimeCode = 3.402823466e+38F );
	
	UFUNCTION( BlueprintCallable, Category = "Component conversion" )
	bool ConvertInstancedFoliageActor( const AInstancedFoliageActor* Actor, const FString& PrimPath, float TimeCode = 3.402823466e+38F );

public:
	UFUNCTION( BlueprintCallable )
	UMyActorFolder* GetWorldRootFolder();

	UFUNCTION( BlueprintCallable )
	bool PathIsChildOf(const FString& InPotentialChild, const FString& InParent);

	UFUNCTION( BlueprintCallable )
	TArray<FName> GetWorldFoldersNames();
	
public:
	UPROPERTY(BlueprintReadWrite)
	UWorld* World = nullptr;
	
private:
	UE::FUsdPrim GetPrim( const UE::FUsdStage& Stage, const FString& PrimPath );

	void TryAddActorFolder(UMyActorFolder* ParentFolder, FName FolderNameToAdd);
	
private:
	/** Stage to use when converting components */
	UE::FUsdStage Stage;

	/**
	 * Whether we will erase our current stage from the stage cache when we Cleanup().
	 * This is true if we were the ones that put the stage in the cache in the first place.
	 */
	bool bEraseFromStageCache = false;
};
