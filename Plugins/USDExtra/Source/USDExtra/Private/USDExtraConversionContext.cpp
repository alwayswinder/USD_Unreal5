// Fill out your copyright notice in the Description page of Project Settings.


#include "USDExtraConversionContext.h"

#include "EditorActorFolders.h"
#include "UnrealUSDWrapper.h"
#include "UsdWrappers/UsdPrim.h"
#include "USDLog.h"
#include "UsdWrappers/SdfPath.h"
#include "USDExtraUtils.h"
#include "UsdWrappers/SdfLayer.h"

UUSDExtraConversionContext::~UUSDExtraConversionContext()
{
	Cleanup();
}

void UUSDExtraConversionContext::SetStageRootLayer(const FFilePath StageRootLayerPath)
{
	Cleanup();

	const TArray< UE::FUsdStage > PreviouslyOpenedStages = UnrealUSDWrapper::GetAllStagesFromCache();

	Stage = UnrealUSDWrapper::OpenStage( *StageRootLayerPath.FilePath, EUsdInitialLoadSet::LoadAll );
	bEraseFromStageCache = !PreviouslyOpenedStages.Contains( Stage );
}

void UUSDExtraConversionContext::SetEditTarget(FFilePath EditTargetLayerPath)
{
	if ( Stage )
	{
		if (const UE::FSdfLayer EditTargetLayer = UE::FSdfLayer::FindOrOpen( *EditTargetLayerPath.FilePath ) )
		{
			Stage.SetEditTarget( EditTargetLayer );
		}
		else
		{
			UE_LOG( LogUsd, Error, TEXT( "Failed to find or open USD layer with filepath '%s'!" ), *EditTargetLayerPath.FilePath );
		}
	}
	else
	{
		UE_LOG( LogUsd, Error, TEXT( "There is no stage currently open!" ) );
	}
}

void UUSDExtraConversionContext::Cleanup()
{
	if ( Stage )
	{
		if ( bEraseFromStageCache )
		{
			UnrealUSDWrapper::EraseStageFromCache( Stage );
		}

		Stage = UE::FUsdStage();
	}
}

bool UUSDExtraConversionContext::ConvertSceneComponent(const USceneComponent* Component, const FString& PrimPath)
{
#if USE_USD_SDK
	UE::FUsdPrim Prim = GetPrim( Stage, PrimPath );
	if ( !Prim || !Component )
	{
		return false;
	}

	return UnrealToUSDExtra::ConvertSceneComponent( Stage, Component, Prim );
#else
	return false;
#endif // USE_USD_SDK
}

bool UUSDExtraConversionContext::ConvertMeshComponent(const UMeshComponent* Component, const FString& PrimPath)
{
#if USE_USD_SDK
	UE::FUsdPrim Prim = GetPrim( Stage, PrimPath );
	if ( !Prim || !Component )
	{
		return false;
	}

	return UnrealToUSDExtra::ConvertMeshComponent( Stage, Component, Prim );
#else
	return false;
#endif // USE_USD_SDK
}

bool UUSDExtraConversionContext::ConvertBrushComponent(const UBrushComponent* Component, const FString& PrimPath)
{
#if USE_USD_SDK
	UE::FUsdPrim Prim = GetPrim( Stage, PrimPath );
	if ( !Prim || !Component )
	{
		return false;
	}

	return UnrealToUSDExtra::ConvertBrushComponent( Stage, Component, Prim );
#else
	return false;
#endif // USE_USD_SDK
}

bool UUSDExtraConversionContext::ConvertHismComponent(const UHierarchicalInstancedStaticMeshComponent* Component, const FString& PrimPath, float TimeCode)
{
#if USE_USD_SDK
	UE::FUsdPrim Prim = GetPrim( Stage, PrimPath );
	if ( !Prim || !Component )
	{
		return false;
	}

	return UnrealToUSDExtra::ConvertHierarchicalInstancedStaticMeshComponent( Component, Prim, TimeCode == FLT_MAX ? UsdUtils::GetDefaultTimeCode() : TimeCode );
#else
	return false;
#endif // USE_USD_SDK
}

bool UUSDExtraConversionContext::ConvertInstancedFoliageActor(const AInstancedFoliageActor* Actor, const FString& PrimPath, float TimeCode)
{
#if USE_USD_SDK
	UE::FUsdPrim Prim = GetPrim( Stage, PrimPath );
	if ( !Prim || !Actor )
	{
		return false;
	}

	return UnrealToUSDExtra::ConvertInstancedFoliageActor( *Actor, Prim, TimeCode == FLT_MAX ? UsdUtils::GetDefaultTimeCode() : TimeCode );
#else
	return false;
#endif // USE_USD_SDK
}

UMyActorFolder* UUSDExtraConversionContext::GetWorldRootFolder()
{
	if (!World)
	{
		UE_LOG( LogUsd, Error, TEXT( "No Valid World for Folders." ) );
		return nullptr;
	}

	UMyActorFolder* RootFolder = NewObject<UMyActorFolder>(GetTransientPackage());
	RootFolder->FolderName = FName("Root");

	FActorFolders::Get().ForEachFolder(*World, [this, &World, &OutItems](const FFolder& Folder)
	{
		UE_LOG(LogUsd, Error, TEXT("UE Folder: %s"), Folder.GetLeafName());
		TryAddActorFolder(RootFolder, Folder.GetLeafName());
	});
	return RootFolder;
}

bool UUSDExtraConversionContext::PathIsChildOf(const FString& InPotentialChild, const FString& InParent)
{
	if (!World)
	{
		UE_LOG( LogUsd, Error, TEXT( "No Valid World for Folders." ) );
		return false;
	}
	//return FFolder::IsChildOf(InPotentialChild, InParent);
	return false;
}

TArray<FName> UUSDExtraConversionContext::GetWorldFoldersNames()
{
	TArray<FName> FolderNames;
	
	if (!World)
	{
		UE_LOG( LogUsd, Error, TEXT( "No Valid World for Folders." ) );
		return FolderNames;
	}

	/*FActorFolders ActorFolders = FActorFolders::Get();
	const TMap<FName, FActorFolderProps> FolderProperties = ActorFolders.GetFolderPropertiesForWorld(*World);
	FolderProperties.GetKeys(FolderNames);*/
	return FolderNames;
}

UE::FUsdPrim UUSDExtraConversionContext::GetPrim(const UE::FUsdStage& InStage, const FString& PrimPath)
{
	if ( !InStage )
	{
		UE_LOG( LogUsd, Error, TEXT( "Export context has no stage set! Call SetStage with a root layer filepath first." ) );
		return {};
	}
    
	return Stage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) );
}

void UUSDExtraConversionContext::TryAddActorFolder(UMyActorFolder* ParentFolder, FName FolderNameToAdd)
{
	TArray<FName> ChildrenOfParentFolder;
	ParentFolder->ChildrenFolders.GetKeys(ChildrenOfParentFolder);

	FString LeftString;
	FString RightString;
	
	if(FolderNameToAdd.ToString().Split("/", &LeftString, &RightString))
	{
		const FName FolderNameToTest(LeftString);

		if (!ChildrenOfParentFolder.Contains(FolderNameToTest))
		{
			UMyActorFolder* NewActorFolder = NewObject<UMyActorFolder>(GetTransientPackage());
			NewActorFolder->FolderName = FolderNameToTest;
			ParentFolder->ChildrenFolders.Add(FolderNameToTest, NewActorFolder);
		}

		TryAddActorFolder(*ParentFolder->ChildrenFolders.Find(FolderNameToTest), FName(RightString));
	}
	else if (!ChildrenOfParentFolder.Contains(FolderNameToAdd))
	{
		UMyActorFolder* NewActorFolder = NewObject<UMyActorFolder>(GetTransientPackage());
		NewActorFolder->FolderName = FolderNameToAdd;
		ParentFolder->ChildrenFolders.Add(FolderNameToAdd, NewActorFolder);
	}
}
