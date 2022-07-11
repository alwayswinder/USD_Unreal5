#pragma once

#include "CoreMinimal.h"
#include "USDPrimConversion.h"
//#include "USDPrimResolver.h"
//#include "USDImporter.h"
#include "USDExtraUtils.generated.h"


UENUM(BlueprintType)
enum class EUnrealPrimType : uint8
{
	None,
	Folder,
	Scene,
	StaticMesh,
	SkeletalMesh,
	HISM,
	InstancedFoliage,
	BSP
};

UENUM(BlueprintType)
enum class EUnrealPrimUsage : uint8
{
	Folder,
	Actor,
	Component,
	Data
};

UENUM(BlueprintType)
enum class EUnrealConversionMethod : uint8
{
	Spawn,
	Modify,
	Ignore
};

USTRUCT(BlueprintType)
struct FUSDExtraToUnrealInfo
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite)
	EUnrealPrimType PrimType = EUnrealPrimType::None;
	
	UPROPERTY(BlueprintReadWrite)
	EUnrealPrimUsage PrimUsage = EUnrealPrimUsage::Data;
	
	UPROPERTY(BlueprintReadWrite)
	EUnrealConversionMethod ConversionMethod = EUnrealConversionMethod::Ignore;
	
	UPROPERTY(BlueprintReadWrite)
	FName InstanceReference = NAME_None;
	
	UPROPERTY(BlueprintReadWrite)
	UClass* ClassReference = nullptr;
	
	UPROPERTY(BlueprintReadWrite)
	UObject* AssetReference = nullptr;
	
	UPROPERTY(BlueprintReadWrite)
	UMaterialInterface* MaterialReference = nullptr;
	
	UPROPERTY(BlueprintReadWrite)
	FName ActorFolderPath = NAME_None;

	UPROPERTY(BlueprintReadWrite)
	TEnumAsByte<EBrushType> BSPBrushType = Brush_Default;
};

UCLASS(Blueprintable)
class UMyActorFolder : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite)
	FName FolderName = NAME_None;
	
	UPROPERTY(BlueprintReadWrite)
	TMap<FName, UMyActorFolder*> ChildrenFolders;
};

UCLASS(Blueprintable)
class USDEXTRA_API UUSDExtraUtils : public UObject
{
	GENERATED_BODY()
	
public:
	static void ImportUSDToLevel(UWorld* World, FString FilePath);
	static void ExportLevelToUSD(UWorld* World, FString FilePath);
	static bool Test();

	static UStaticMesh* CreateStaticMeshFromBrush(UObject* Outer, FName Name, ABrush* Brush, const UModel* Model);
	static void CreateModelFromStaticMesh(UBrushComponent* BrushComponent, const UStaticMesh* StaticMesh);
	static void GetMeshDescriptionFromBrush(::ABrush* Brush, const ::UModel* Model, ::FMeshDescription& MeshDescription, TArray<FStaticMaterial>& OutMaterials);
	static void BSPValidateBrush( UModel* Brush, bool ForceValidate, bool DoStatusUpdate );

	UFUNCTION(BlueprintCallable)
	static void PrintBrushPolyInfo(AActor* InActor); 
	
private:
	static TArray<ULevel*> StreamInRequiredLevels( UWorld* World, const TSet<FString>& LevelsToIgnore );
	static void StreamOutLevels( const TArray<ULevel*>& LevelsToStreamOut );

	static TMap<FName, USceneComponent*> CollectWorldContent(UWorld* World);

};

#if USE_USD_SDK
namespace UnrealToUSDExtra
{
	bool ConvertSceneComponent(const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim);
	bool ConvertMeshComponent(const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, pxr::UsdPrim& UsdPrim);
	bool ConvertBrushComponent(const pxr::UsdStageRefPtr& Stage, const UBrushComponent* BrushComponent, pxr::UsdPrim& UsdPrim);
	bool ConvertHierarchicalInstancedStaticMeshComponent( const UHierarchicalInstancedStaticMeshComponent* HISMComponent, pxr::UsdPrim& UsdPrim, double TimeCode = UsdUtils::GetDefaultTimeCode() );
	bool ConvertInstancedFoliageActor( const AInstancedFoliageActor& Actor, pxr::UsdPrim& UsdPrim, double TimeCode );

	bool AddUSDExtraAttributesForSceneComponent(const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim);
	bool AddUSDExtraAttributesForMeshComponent(const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, const pxr::UsdPrim& UsdPrim);
	bool AddUSDExtraAttributesForHISMComponent(const UHierarchicalInstancedStaticMeshComponent* HISMComponent, const pxr::UsdPrim& UsdPrim);
	bool AddUSDExtraAttributesForFoliageComponent(const AInstancedFoliageActor& FoliageActor, pxr::UsdPrim& UsdPrim);
}

namespace USDExtraToUnreal
{
	bool ConvertFolder(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, TMap<FName, USceneComponent*> &WorldContent, UWorld* World);
	bool ConvertActor(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, USceneComponent* ParentComponent, TMap<FName, USceneComponent*> &WorldContent, UWorld* World);
	bool ConvertComponent(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, AActor* OwnerActor, USceneComponent* ParentComponent, TMap<FName, USceneComponent*> &WorldContent, UWorld* World);
	
	bool ConvertMeshPrim(FUSDExtraToUnrealInfo PrimInfo, UMeshComponent* MeshComponent);
	bool ConvertBSPPrim(FUSDExtraToUnrealInfo PrimInfo, const pxr::UsdPrim& UsdPrim, UBrushComponent* BrushComponent);
	bool ConvertXformPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, USceneComponent& SceneComponent, UWorld* World);
	bool ConvertPointInstancerPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, UHierarchicalInstancedStaticMeshComponent* HISMComponent);
	bool ConvertPointInstancerPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, AInstancedFoliageActor* FoliageActor, TMap<FName, USceneComponent*> WorldContent);
	
	FUSDExtraToUnrealInfo GatherPrimConversionInfo(pxr::UsdPrim& UsdPrim);
}

namespace USDExtraIdentifiers
{
	extern const pxr::TfToken UnrealPrimType;
	extern const pxr::TfToken UnrealPrimUsage;
	extern const pxr::TfToken UnrealConversionMethod;
	extern const pxr::TfToken UnrealInstanceReference;
	extern const pxr::TfToken UnrealClassReference;
	extern const pxr::TfToken UnrealAssetReference;
	extern const pxr::TfToken UnrealMaterialReference;
	extern const pxr::TfToken UnrealActorFolderPath;
	extern const pxr::TfToken UnrealBSPBrushType;
	extern const pxr::TfToken UnrealBaseComponentReferences;
	extern const pxr::TfToken UnrealBaseComponentIndices;
}

namespace USDExtraTokensType
{
	extern const pxr::TfToken USDActorFolder;
	extern const pxr::TfToken USDScene;
	extern const pxr::TfToken USDStaticMesh;
	extern const pxr::TfToken USDSkeletalMesh;
	extern const pxr::TfToken USDInstancedStaticMesh;

	extern const pxr::TfToken Folder;
	extern const pxr::TfToken Actor;
	extern const pxr::TfToken Component;
	extern const pxr::TfToken Data;

	extern const pxr::TfToken Spawn;
	extern const pxr::TfToken Modify;
	extern const pxr::TfToken Ignore;

	extern const pxr::TfToken PrimTypeBSP;
	
	extern const pxr::TfToken Add;
	extern const pxr::TfToken Subtract;
	extern const pxr::TfToken Default;
	extern const pxr::TfToken Max;
}
#endif