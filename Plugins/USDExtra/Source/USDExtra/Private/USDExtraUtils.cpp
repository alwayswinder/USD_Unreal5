#include "USDExtraUtils.h"

#include "BSPOps.h"
#include "EditorActorFolders.h"
#include "EditorBuildUtils.h"
#include "UnrealUSDWrapper.h"
#include "USDTypesConversion.h"
#include "EditorLevelUtils.h"
#include "EngineUtils.h"
#include "InstancedFoliageActor.h"
#include "USDExtraSettings.h"
#include "IPythonScriptPlugin.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "USDExtraExportOptions.h"
#include "USDGeomMeshConversion.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "USDLog.h"
#include "Components/BrushComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshResources.h"
#include "Engine/Polys.h"
#include "UsdWrappers/UsdTyped.h"
#include "FoliageHelper.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Components\ModelComponent.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/path.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usdSkel/root.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/pointInstancer.h"
	#include "pxr/usd/usd/primRange.h"
#include "USDIncludesEnd.h"

typedef TFunction<bool(const UPrimitiveComponent*)> FFoliageTraceFilterFunc;

struct FFoliagePaintingGeometryFilter
{
	bool bAllowLandscape;
	bool bAllowStaticMesh;
	bool bAllowBSP;
	bool bAllowFoliage;
	bool bAllowTranslucent;

	FFoliagePaintingGeometryFilter()
		: bAllowLandscape(true)
		, bAllowStaticMesh(false)
		, bAllowBSP(false)
		, bAllowFoliage(false)
		, bAllowTranslucent(false)
	{
	}

	bool operator() (const UPrimitiveComponent* Component) const
	{
		if (Component)
		{
			bool bFoliageOwned = Component->GetOwner() && FFoliageHelper::IsOwnedByFoliage(Component->GetOwner());

			// Whitelist
			bool bAllowed =
				(bAllowLandscape && Component->IsA(ULandscapeHeightfieldCollisionComponent::StaticClass())) ||
				(bAllowStaticMesh && Component->IsA(UStaticMeshComponent::StaticClass()) && !Component->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) && !bFoliageOwned) ||
				(bAllowBSP && (Component->IsA(UBrushComponent::StaticClass()) || Component->IsA(UModelComponent::StaticClass()))) ||
				(bAllowFoliage && (Component->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) || bFoliageOwned));

			// Blacklist
			bAllowed &=
				(bAllowTranslucent || !(Component->GetMaterial(0) && IsTranslucentBlendMode(Component->GetMaterial(0)->GetBlendMode())));

			return bAllowed;
		}

		return false;
	}
};
namespace USDExtraIdentifiers
{
	const pxr::TfToken UnrealPrimType = pxr::TfToken("unrealPrimType");
	const pxr::TfToken UnrealPrimUsage = pxr::TfToken("unrealPrimUsage");
	const pxr::TfToken UnrealConversionMethod = pxr::TfToken("unrealConversionMethod");
	const pxr::TfToken UnrealInstanceReference = pxr::TfToken("unrealInstanceReference");
	const pxr::TfToken UnrealClassReference = pxr::TfToken("unrealClassReference");
	const pxr::TfToken UnrealAssetReference = pxr::TfToken("unrealAssetReference");
	const pxr::TfToken UnrealMaterialReference = pxr::TfToken("unrealMaterial");
	const pxr::TfToken UnrealActorFolderPath = pxr::TfToken("unrealActorFolderPath");
	const pxr::TfToken UnrealBSPBrushType = pxr::TfToken("unrealBSPBrushType");
	const pxr::TfToken UnrealBaseComponentReferences = pxr::TfToken("unrealBaseComponentReferences");
	const pxr::TfToken UnrealBaseComponentIndices = pxr::TfToken("unrealBaseComponentIndices");
}

namespace USDExtraTokensType
{
	const pxr::TfToken USDActorFolder = pxr::TfToken("Scope");
	const pxr::TfToken USDScene = pxr::TfToken("Xform");
	const pxr::TfToken USDStaticMesh = pxr::TfToken("Mesh");
	const pxr::TfToken USDSkeletalMesh = pxr::TfToken("SkelRoot");
	const pxr::TfToken USDInstancedStaticMesh = pxr::TfToken("PointInstancer");

	const pxr::TfToken Folder = pxr::TfToken("folder");
	const pxr::TfToken Actor = pxr::TfToken("actor");
	const pxr::TfToken Component = pxr::TfToken("component");
	const pxr::TfToken Data = pxr::TfToken("data");

	const pxr::TfToken Spawn = pxr::TfToken("spawn");
	const pxr::TfToken Modify = pxr::TfToken("modify");
	const pxr::TfToken Ignore = pxr::TfToken("ignore");

	const pxr::TfToken PrimTypeBSP = pxr::TfToken("BSP");
	const pxr::TfToken Add = pxr::TfToken("add");
	const pxr::TfToken Subtract = pxr::TfToken("subtract");
	const pxr::TfToken Default = pxr::TfToken("default");
	const pxr::TfToken Max = pxr::TfToken("max");
}

inline bool FVerticesEqual(const FVector& V1, const FVector& V2)
{
	if(FMath::Abs(V1.X - V2.X) > THRESH_POINTS_ARE_SAME * 4.0f)
	{
		return false;
	}

	if(FMath::Abs(V1.Y - V2.Y) > THRESH_POINTS_ARE_SAME * 4.0f)
	{
		return false;
	}

	if(FMath::Abs(V1.Z - V2.Z) > THRESH_POINTS_ARE_SAME * 4.0f)
	{
		return false;
	}

	return true;
}

inline FTransform ConvertAxes( const bool bZUp, const FTransform Transform )
{
	FVector Translation = Transform.GetTranslation();
	FQuat Rotation = Transform.GetRotation();
	FVector Scale = Transform.GetScale3D();

	if ( bZUp )
	{
		Translation.Y = -Translation.Y;
		Rotation.X = -Rotation.X;
		Rotation.Z = -Rotation.Z;
	}
	else
	{
		Swap( Translation.Y, Translation.Z );

		Rotation = Rotation.Inverse();
		Swap( Rotation.Y, Rotation.Z );

		Swap( Scale.Y, Scale.Z );
	}

	return FTransform( Rotation, Translation, Scale );
}

inline void TexCoordsToVectors(const FVector& V0, const FVector2D& InUV0,
								const FVector& V1, const FVector2D& InUV1,
								const FVector& V2, const FVector2D& InUV2,
								FVector* InBaseResult, FVector* InUResult, FVector* InVResult )
{
	{
		const FVector2D UVOrigin = FVector2D::ZeroVector;
		const FVector2D C = UVOrigin - InUV2;
		const FVector2D A = InUV0 - InUV2;
		const FVector2D B = InUV1 - InUV2;

		const float W0 = (C.X * B.Y - C.Y * B.X) / (A.X * B.Y - A.Y * B.X);
		const float W1 = (C.X * A.Y - C.Y * A.X) / (B.X * A.Y - B.Y * A.X);
		const float W2 = 1 - W0 - W1;
		*InBaseResult = V0 * W0 + V1 * W1 + V2 * W2;
	}

	{
		const FVector2D TextureU = FVector2D(1.0f, 0.0f);
		const FVector2D C = TextureU - InUV2;
		const FVector2D A = InUV0 - InUV2;
		const FVector2D B = InUV1 - InUV2;

		const float W0 = (C.X * B.Y - C.Y * B.X) / (A.X * B.Y - A.Y * B.X);
		const float W1 = (C.X * A.Y - C.Y * A.X) / (B.X * A.Y - B.Y * A.X);
		const float W2 = 1 - W0 - W1;
		*InUResult = V0 * W0 + V1 * W1 + V2 * W2;
		*InUResult = *InUResult - *InBaseResult;
	}
	
	{
		const FVector2D TextureV = FVector2D(0.0f, 1.0f);
		const FVector2D C = TextureV - InUV2;
		const FVector2D A = InUV0 - InUV2;
		const FVector2D B = InUV1 - InUV2;

		const float W0 = (C.X * B.Y - C.Y * B.X) / (A.X * B.Y - A.Y * B.X);
		const float W1 = (C.X * A.Y - C.Y * A.X) / (B.X * A.Y - B.Y * A.X);
		const float W2 = 1 - W0 - W1;
		*InVResult = V0 * W0 + V1 * W1 + V2 * W2;
		*InVResult = *InVResult - *InBaseResult;
	}
}

void UUSDExtraUtils::ImportUSDToLevel(UWorld* World, FString FilePath)
{
	TMap<FName, USceneComponent*> WorldContent = CollectWorldContent(World);
	
	FScopedUsdAllocs Allocs;
	
	UE::FUsdStage USDStage = UnrealUSDWrapper::OpenStage(*FilePath, EUsdInitialLoadSet::LoadAll);
	check(USDStage)
	pxr::UsdStageRefPtr& StageRef = USDStage;
	
	TArray<pxr::UsdPrim> VisitedPrims;
	pxr::UsdPrim FoliageActorPrim;
	FUSDExtraToUnrealInfo FoliageActorPrimInfo;
	pxr::UsdPrimSiblingRange PrimRange = StageRef->GetDefaultPrim().GetChildren();
	for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		TUsdStore<pxr::UsdPrim> UsdPrim = *PrimRangeIt;
		
		if (VisitedPrims.Contains(UsdPrim.Get()))
		{
			continue;
		}

		FUSDExtraToUnrealInfo PrimInfo = USDExtraToUnreal::GatherPrimConversionInfo(UsdPrim.Get());

		if (PrimInfo.PrimType == EUnrealPrimType::Folder)
		{
			USDExtraToUnreal::ConvertFolder(StageRef, UsdPrim.Get(), PrimInfo, VisitedPrims, WorldContent, World);
			continue;
		}

		if (PrimInfo.ConversionMethod == EUnrealConversionMethod::Ignore)
		{
			continue;
		}
		
		if (PrimInfo.PrimUsage == EUnrealPrimUsage::Actor)
		{
			if (PrimInfo.ClassReference != AInstancedFoliageActor::StaticClass())
			{
				USDExtraToUnreal::ConvertActor(StageRef, UsdPrim.Get(), PrimInfo, VisitedPrims, nullptr, WorldContent, World);
			}
			else
			{
				FoliageActorPrim = UsdPrim.Get();
				FoliageActorPrimInfo = PrimInfo;
			}
		}
	}

	if (FoliageActorPrim)
	{
		USDExtraToUnreal::ConvertActor(StageRef, FoliageActorPrim, FoliageActorPrimInfo, VisitedPrims, nullptr, WorldContent, World);
	}

	FEditorBuildUtils::EditorBuild( World, FBuildOptions::BuildVisibleGeometry );

	UnrealUSDWrapper::EraseStageFromCache(USDStage);
}

void UUSDExtraUtils::ExportLevelToUSD(UWorld* World, FString FilePath)
{
	UUSDExtraExportOptions* ExportOptions = GetMutableDefault<UUSDExtraExportOptions>();
	ExportOptions->StageOptions.UpAxis = EUsdUpAxis::YAxis;
	ExportOptions->StageOptions.MetersPerUnit = 1.0f;
	ExportOptions->World = World;
	ExportOptions->FileName = FilePath;
	
	if ( IPythonScriptPlugin::Get()->IsPythonAvailable() )
	{
		IPythonScriptPlugin::Get()->ExecPythonCommand( TEXT( "import usd_extra_export_scripts; usd_extra_export_scripts.export_with_cdo_options()" ) );
	}

	ExportOptions->World = nullptr;
	ExportOptions->FileName = "";
}

bool UUSDExtraUtils::Test()
{
	const FString PathName = "TestTest";
	//UnrealToUsd::ConvertString(*PathName);
	auto a = UnrealToUsd::ConvertString(*PathName);
	auto b = a.Get();

	UE_LOG(LogTemp, Warning, TEXT("LogTest:%s"), *PathName);
	return true;
}

TArray<ULevel*> UUSDExtraUtils::StreamInRequiredLevels(UWorld* InWorld, const TSet<FString>& LevelsToIgnore)
{
	TArray<ULevel*> Result;
	if ( !InWorld )
	{
		return Result;
	}

	// Make sure all streamed levels are loaded so we can query their visibility
	constexpr bool bForce = true;
	InWorld->LoadSecondaryLevels( bForce );

	if ( ULevel* PersistentLevel = InWorld->PersistentLevel )
	{
		const FString LevelName = TEXT( "Persistent Level" );
		if ( !PersistentLevel->bIsVisible && !LevelsToIgnore.Contains( LevelName ) )
		{
			Result.Add( PersistentLevel );
		}
	}

	for ( ULevelStreaming* StreamingLevel : InWorld->GetStreamingLevels() )
	{
		if ( StreamingLevel )
		{
			if ( ULevel* Level = StreamingLevel->GetLoadedLevel() )
			{
				const FString LevelName = Level->GetTypedOuter<UWorld>()->GetName();
				if ( !Level->bIsVisible && !LevelsToIgnore.Contains( LevelName ) )
				{
					Result.Add( Level );
				}
			}
		}
	}

	TArray<bool> ShouldBeVisible;
	ShouldBeVisible.SetNumUninitialized(Result.Num());
	for ( bool& bVal : ShouldBeVisible )
	{
		bVal = true;
	}

	constexpr bool bForceLayersVisible = true;
	EditorLevelUtils::SetLevelsVisibility( Result, ShouldBeVisible, bForceLayersVisible, ELevelVisibilityDirtyMode::DontModify );

	return Result;
}

void UUSDExtraUtils::StreamOutLevels(const TArray<ULevel*>& LevelsToStreamOut)
{
	TArray<bool> ShouldBeVisible;
	ShouldBeVisible.SetNumZeroed( LevelsToStreamOut.Num() );

	constexpr bool bForceLayersVisible = false;
	EditorLevelUtils::SetLevelsVisibility( LevelsToStreamOut, ShouldBeVisible, bForceLayersVisible, ELevelVisibilityDirtyMode::DontModify );
}

TMap<FName, USceneComponent*> UUSDExtraUtils::CollectWorldContent(UWorld* World)
{
	TMap<FName, USceneComponent*> Result;

	for(FActorIterator It(World); It; ++It)
	{
		const AActor* Actor = *It;
		TInlineComponentArray<USceneComponent*> Components(Actor);
		for (USceneComponent* Component : Components)
		{
			if (Actor->GetRootComponent() == Component)
			{
				FName Key = FName(Actor->GetPathName());
				Result.Add(Key, Component);
			}
			FName Key = FName(Component->GetPathName());
			Result.Add(Key, Component);
		}
	}
	return Result;
}

UStaticMesh* UUSDExtraUtils::CreateStaticMeshFromBrush(UObject* Outer, FName Name, ABrush* Brush, const UModel* Model)
{
	FScopedSlowTask SlowTask(0.0f, NSLOCTEXT("UnrealEd", "CreatingStaticMeshE", "Creating static mesh..."));
	SlowTask.MakeDialog();

	// Create the UStaticMesh object.
	FStaticMeshComponentRecreateRenderStateContext RecreateRenderStateContext(FindObject<UStaticMesh>(Outer, *Name.ToString()));
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Outer, Name, RF_Public | RF_Standalone);

	// Add one LOD for the base mesh
	StaticMesh->AddSourceModel();
	const int32 LodIndex = StaticMesh->GetNumSourceModels() - 1;
	FMeshDescription* MeshDescription = StaticMesh->CreateMeshDescription(LodIndex);

	// Fill out the mesh description and materials from the brush geometry
	TArray<FStaticMaterial> Materials;
	//GetBrushMesh(Brush, Model, *MeshDescription, Materials);
	GetMeshDescriptionFromBrush(Brush, Model, *MeshDescription, Materials);

	// Commit mesh description and materials list to static mesh
	StaticMesh->CommitMeshDescription(LodIndex);
	StaticMesh->SetStaticMaterials(Materials);

	// Set up the SectionInfoMap to enable collision
	const int32 NumSections = StaticMesh->GetStaticMaterials().Num();
	for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
	{
		FMeshSectionInfo Info = StaticMesh->GetSectionInfoMap().Get(0, SectionIdx);
		Info.MaterialIndex = SectionIdx;
		Info.bEnableCollision = true;
		StaticMesh->GetSectionInfoMap().Set(0, SectionIdx, Info);
		StaticMesh->GetOriginalSectionInfoMap().Set(0, SectionIdx, Info);
	}

	//Set the Imported version before calling the build
	StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;

	StaticMesh->Build();
	// ReSharper disable once CppExpressionWithoutSideEffects
	StaticMesh->MarkPackageDirty();

	return StaticMesh;

}

void UUSDExtraUtils::CreateModelFromStaticMesh(UBrushComponent* BrushComponent, const UStaticMesh* StaticMesh)
{
	FScopedUnrealAllocs UnrealAllocs;

	check(BrushComponent)
	check(StaticMesh)

	UModel* Model = BrushComponent->Brush;
	if (!Model)
	{
		ABrush* OwnerBrush = Cast<ABrush>(BrushComponent->GetOwner());
		check(OwnerBrush)

		Model = NewObject<UModel>(OwnerBrush);
		Model->Initialize(nullptr, true);
		Model->Polys = NewObject<UPolys>(Model);
		OwnerBrush->Brush = Model;
		BrushComponent->Brush = Model;
	}
	check(Model)
	
	FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
	FStaticMeshAttributes StaticMeshAttributes( *MeshDescription );
	const TVertexAttributesRef< FVector > MeshDescriptionVertexPositions = StaticMeshAttributes.GetVertexPositions();
	const TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs();

	Model->Polys->Element.Empty();

	FTriangleArray& TriangleArray = MeshDescription->Triangles();
	
	if(TriangleArray.Num())
	{
		for (const FTriangleID& TriangleID : TriangleArray.GetElementIDs())
		{
			FMeshTriangle MeshTriangle = TriangleArray[TriangleID];

			const FVertexInstanceID VI0 = MeshTriangle.GetVertexInstanceID(0);
			const FVertexInstanceID VI1 = MeshTriangle.GetVertexInstanceID(1);
			const FVertexInstanceID VI2 = MeshTriangle.GetVertexInstanceID(2);

			const FVertexID V0 = MeshDescription->GetVertexInstanceVertex(VI0);
			const FVertexID V1 = MeshDescription->GetVertexInstanceVertex(VI1);
			const FVertexID V2 = MeshDescription->GetVertexInstanceVertex(VI2);
			
			FPoly* Polygon	= new(Model->Polys->Element) FPoly;
						
			Polygon->Init();
			Polygon->iLink = Polygon - Model->Polys->Element.GetData();
			//Polygon->Material = StaticMesh->LODModels[0].Elements[Triangle.MaterialIndex].Material;
			Polygon->PolyFlags = PF_DefaultFlags;
			//Polygon->SmoothingMask = Triangle.SmoothingMask;

			//new(Polygon->Vertices) FVector(ActorToWorld.TransformPosition(Triangle.Vertices[2]));
			//new(Polygon->Vertices) FVector(ActorToWorld.TransformPosition(Triangle.Vertices[1]));
			//new(Polygon->Vertices) FVector(ActorToWorld.TransformPosition(Triangle.Vertices[0]));

			new(Polygon->Vertices) FVector(MeshDescriptionVertexPositions[V2]);
			new(Polygon->Vertices) FVector(MeshDescriptionVertexPositions[V1]);
			new(Polygon->Vertices) FVector(MeshDescriptionVertexPositions[V0]);
			
			TexCoordsToVectors(Polygon->Vertices[2],FVector2D(VertexInstanceUVs.Get(VI0, 0).X * UModel::GetGlobalBSPTexelScale(),VertexInstanceUVs.Get(VI0, 0).Y * UModel::GetGlobalBSPTexelScale()),
								Polygon->Vertices[1],FVector2D(VertexInstanceUVs.Get(VI1, 0).X * UModel::GetGlobalBSPTexelScale(),VertexInstanceUVs.Get(VI1, 0).Y * UModel::GetGlobalBSPTexelScale()),
								Polygon->Vertices[0],FVector2D(VertexInstanceUVs.Get(VI2, 0).X * UModel::GetGlobalBSPTexelScale(),VertexInstanceUVs.Get(VI2, 0).Y * UModel::GetGlobalBSPTexelScale()),
								&Polygon->Base,&Polygon->TextureU,&Polygon->TextureV);

			Polygon->Finalize(nullptr,0);
		}
	}

	Model->Linked = true;
	BSPValidateBrush(Model,false,true);
}

void UUSDExtraUtils::GetMeshDescriptionFromBrush(::ABrush* Brush, const ::UModel* Model, ::FMeshDescription& MeshDescription, TArray<FStaticMaterial>& OutMaterials)
{
	const TVertexAttributesRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	const TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	const TEdgeAttributesRef<bool> EdgeHardnesses = MeshDescription.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	const TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	
	//Make sure we have one UVChannel
	VertexInstanceUVs.SetNumIndices(1);
	
	// Calculate the local to world transform for the source brush.
	// Change ToMatrixWithScale() to ToMatrixNoScale, preventing apply the scale twice.
	const FMatrix	ActorToWorld = Brush ? Brush->ActorToWorld().ToMatrixNoScale() : FMatrix::Identity;
	const FVector4	PostSub = Brush ? FVector4(Brush->GetActorLocation()) : FVector4(0, 0, 0, 0);

	TMap<uint32, FEdgeID> RemapEdgeID;
	const int32 NumPolys = Model->Polys->Element.Num();
	//Create Fill the vertex position
	for (int32 PolygonIndex = 0; PolygonIndex < NumPolys; ++PolygonIndex)
	{
		FPoly& Polygon = Model->Polys->Element[PolygonIndex];

		// Find a material index for this polygon.
		UMaterialInterface*	Material = Polygon.Material;
		if (Material == nullptr)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		OutMaterials.AddUnique(FStaticMaterial(Material, Material->GetFName(), Material->GetFName()));
		FPolygonGroupID CurrentPolygonGroupID = FPolygonGroupID::Invalid;
		for (const FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
		{
			if (Material->GetFName() == PolygonGroupImportedMaterialSlotNames[PolygonGroupID])
			{
				CurrentPolygonGroupID = PolygonGroupID;
				break;
			}
		}
		if (CurrentPolygonGroupID == FPolygonGroupID::Invalid)
		{
			CurrentPolygonGroupID = MeshDescription.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[CurrentPolygonGroupID] = Material->GetFName();
		}

		// Cache the texture coordinate system for this polygon.
		FVector	TextureBase = Polygon.Base - (Brush ? Brush->GetPivotOffset() : FVector::ZeroVector),
			TextureX = Polygon.TextureU / UModel::GetGlobalBSPTexelScale(),
			TextureY = Polygon.TextureV / UModel::GetGlobalBSPTexelScale();
		// For each vertex after the first two vertices...
		for (int32 VertexIndex = 2; VertexIndex < Polygon.Vertices.Num(); VertexIndex++)
		{
			// ReSharper disable once CppCompileTimeConstantCanBeReplacedWithBooleanConstant
			constexpr bool ReverseVertices = 0;
			FVector Positions[3];
			Positions[ReverseVertices ? 0 : 2] = ActorToWorld.TransformPosition(Polygon.Vertices[0]) - PostSub;
			Positions[1] = ActorToWorld.TransformPosition(Polygon.Vertices[VertexIndex - 1]) - PostSub;
			Positions[ReverseVertices ? 2 : 0] = ActorToWorld.TransformPosition(Polygon.Vertices[VertexIndex]) - PostSub;
			FVertexID VertexID[3] = { FVertexID::Invalid, FVertexID::Invalid, FVertexID::Invalid };
			for (const FVertexID IterVertexID : MeshDescription.Vertices().GetElementIDs())
			{
				if (FVerticesEqual(Positions[0], VertexPositions[IterVertexID]))
				{
					VertexID[0] = IterVertexID;
				}
				if (FVerticesEqual(Positions[1], VertexPositions[IterVertexID]))
				{
					VertexID[1] = IterVertexID;
				}
				if (FVerticesEqual(Positions[2], VertexPositions[IterVertexID]))
				{
					VertexID[2] = IterVertexID;
				}
			}

			//Create the vertex instances
			TArray<FVertexInstanceID> VertexInstanceIDs;
			VertexInstanceIDs.SetNum(3);

			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				if (VertexID[CornerIndex] == FVertexID::Invalid)
				{
					VertexID[CornerIndex] = MeshDescription.CreateVertex();
					VertexPositions[VertexID[CornerIndex]] = Positions[CornerIndex];
				}
				VertexInstanceIDs[CornerIndex] = MeshDescription.CreateVertexInstance(VertexID[CornerIndex]);
				VertexInstanceUVs.Set(VertexInstanceIDs[CornerIndex], 0, FVector2D(
					(Positions[CornerIndex] - TextureBase) | TextureX,
					(Positions[CornerIndex] - TextureBase) | TextureY));
			}

			// Create a polygon with the 3 vertex instances
			
			TArray<FEdgeID> NewEdgeIDs;
			MeshDescription.CreatePolygon(CurrentPolygonGroupID, VertexInstanceIDs, &NewEdgeIDs);
			for (const FEdgeID& NewEdgeID : NewEdgeIDs)
			{
				//All edge are hard for BSP
				EdgeHardnesses[NewEdgeID] = true;
			}
		}
	}
}

void UUSDExtraUtils::BSPValidateBrush(UModel* Brush, bool ForceValidate, bool DoStatusUpdate)
{
	check(Brush != nullptr);
	Brush->Modify();
	if( ForceValidate || !Brush->Linked )
	{
		Brush->Linked = true;
		for( int32 i=0; i<Brush->Polys->Element.Num(); i++ )
		{
			Brush->Polys->Element[i].iLink = i;
		}
		int32 n=0;
		for( int32 i=0; i<Brush->Polys->Element.Num(); i++ )
		{
			FPoly* EdPoly = &Brush->Polys->Element[i];
			if( EdPoly->iLink==i )
			{
				for( int32 j=i+1; j<Brush->Polys->Element.Num(); j++ )
				{
					FPoly* OtherPoly = &Brush->Polys->Element[j];
					if
					(	OtherPoly->iLink == j
					&&	OtherPoly->Material == EdPoly->Material
					&&	OtherPoly->TextureU == EdPoly->TextureU
					&&	OtherPoly->TextureV == EdPoly->TextureV
					&&	OtherPoly->PolyFlags == EdPoly->PolyFlags
					&&	(OtherPoly->Normal | EdPoly->Normal)>0.9999 )
					{
						const float Dist = FVector::PointPlaneDist( OtherPoly->Vertices[0], EdPoly->Vertices[0], EdPoly->Normal );
						if( Dist>-0.001 && Dist<0.001 )
						{
							OtherPoly->iLink = i;
							n++;
						}
					}
				}
			}
		}
		// 		UE_LOG(LogBSPOps, Log,  TEXT("BspValidateBrush linked %i of %i polys"), n, Brush->Polys->Element.Num() );
	}

	// Build bounds.
	Brush->BuildBound();
}

void UUSDExtraUtils::PrintBrushPolyInfo(AActor* InActor)
{
	ABrush* BrushActor = Cast<ABrush>(InActor);

	if (!BrushActor)
	{
		UE_LOG(LogUsd, Warning, TEXT("No Valid Brush"));
		return;
	}

	const UModel* Model = BrushActor->GetBrushComponent()->Brush;
	
	for( int32 i=0; i<Model->Polys->Element.Num(); i++ )
	{
		const FPoly* Poly = &Model->Polys->Element[i];

		FVector PolyBase = Poly->Base;
		FVector PolyTextureU = Poly->TextureU;
		FVector PolyTextureV = Poly->TextureV;

		UE_LOG(LogUsd, Log, TEXT("Poly Base: %s ; TextureU: %s ; TextureV: %s"), *PolyBase.ToString(), *PolyTextureU.ToString(), *PolyTextureV.ToString());
	}
}

bool USDExtraToUnreal::ConvertFolder(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, TMap<FName, USceneComponent*> &WorldContent, UWorld* World)
{
	FScopedUsdAllocs Allocs;

	// Deal with target prim as Folder.
	UE_LOG(LogUsd, Log, TEXT("Convert Folder: %s"), *PrimInfo.ActorFolderPath.ToString());
	VisitedPrims.Add(UsdPrim);

	// Traverse children prims.
	pxr::UsdPrimSiblingRange PrimRange = UsdPrim.GetChildren();
	for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		TUsdStore<pxr::UsdPrim> ChildUsdPrim = *PrimRangeIt;

		if (VisitedPrims.Contains(ChildUsdPrim.Get()))
		{
			continue;
		}

		FUSDExtraToUnrealInfo ChildPrimInfo = USDExtraToUnreal::GatherPrimConversionInfo(ChildUsdPrim.Get());
		
		if (ChildPrimInfo.PrimType == EUnrealPrimType::Folder)
		{
			ChildPrimInfo.ActorFolderPath = FName(PrimInfo.ActorFolderPath.ToString() + "/" + ChildPrimInfo.ActorFolderPath.ToString());
			ConvertFolder(Stage, ChildUsdPrim.Get(), ChildPrimInfo, VisitedPrims, WorldContent, World);
			continue;
		}

		if (ChildPrimInfo.ConversionMethod == EUnrealConversionMethod::Ignore)
		{
			VisitedPrims.Add(ChildUsdPrim.Get());
			continue;
		}

		if (ChildPrimInfo.PrimUsage == EUnrealPrimUsage::Actor)
		{
			ChildPrimInfo.ActorFolderPath = PrimInfo.ActorFolderPath;
			ConvertActor(Stage, ChildUsdPrim.Get(), ChildPrimInfo, VisitedPrims, nullptr, WorldContent, World);
		}
	}

	return true;
}

bool USDExtraToUnreal::ConvertActor(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, USceneComponent* ParentComponent, TMap<FName, USceneComponent*> &WorldContent, UWorld* World)
{
	AActor* Actor = nullptr;
	USceneComponent* RootComponent = nullptr;

	FScopedUsdAllocs Allocs;

	// Deal with target prim as Actor.
	UE_LOG(LogUsd, Log, TEXT("%s Actor: %s"), *StaticEnum<EUnrealConversionMethod>()->GetValueAsString(PrimInfo.ConversionMethod), *PrimInfo.InstanceReference.ToString());

	if (PrimInfo.InstanceReference.IsEqual(""))
	{
		FString primName(UsdPrim.GetName().GetString().c_str());
		primName = World->GetActiveLevelCollection()->GetPersistentLevel()->GetFullName()+ '.'+ primName;
		primName.RemoveFromStart("Level ");

		PrimInfo.InstanceReference = FName(*primName);
	}

	if (!WorldContent.Find(PrimInfo.InstanceReference))
	{	
		if (PrimInfo.ClassReference)
		{
			if (PrimInfo.ClassReference == AInstancedFoliageActor::StaticClass())
			{
				Actor =	AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
			}
			else
			{
				Actor = World->SpawnActor(PrimInfo.ClassReference);
			}
			FString ActorPath;
			FString ActorLabel;
			PrimInfo.InstanceReference.ToString().Split(".", &ActorPath, &ActorLabel, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Actor->SetActorLabel(ActorLabel, true);
			RootComponent = Actor->GetRootComponent();
			WorldContent.Add(PrimInfo.InstanceReference, RootComponent);
			TArray<USceneComponent*> SceneComponents;
			Actor->GetComponents(SceneComponents);
			for (USceneComponent* SceneComponent : SceneComponents)
			{
				FString SceneComponentPath = PrimInfo.InstanceReference.ToString() + "." + SceneComponent->GetName();
				WorldContent.Add(FName(SceneComponentPath), SceneComponent);
			}
			PrimInfo.ConversionMethod = EUnrealConversionMethod::Modify;
		}
	}
	else
	{
		if (WorldContent.Find(PrimInfo.InstanceReference))
		{
			RootComponent = *WorldContent.Find(PrimInfo.InstanceReference);
			if (RootComponent)
			{
				Actor = RootComponent->GetOwner();
			}
		}
		else if (PrimInfo.ClassReference == AInstancedFoliageActor::StaticClass())
		{
			Actor = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
			RootComponent = Actor->GetRootComponent();
			WorldContent.Add(PrimInfo.InstanceReference, RootComponent);
		}
	}
			
	if (Actor && RootComponent)
	{
		Actor->SetFolderPath(PrimInfo.ActorFolderPath);
		ConvertComponent(Stage, UsdPrim, PrimInfo, VisitedPrims, Actor, ParentComponent, WorldContent, World);
	}
	else
	{
		const pxr::UsdPrimRange PrimRange(UsdPrim);
		for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
		{
			TUsdStore<pxr::UsdPrim> ChildUsdPrim = *PrimRangeIt;
			VisitedPrims.Add(ChildUsdPrim.Get());
		}
		
		UE_LOG(LogUsd, Warning, TEXT("Failed to convert Actor: %s"), *PrimInfo.InstanceReference.ToString());
		return false;
	}
	return true;
}

bool USDExtraToUnreal::ConvertComponent(const pxr::UsdStageRefPtr& Stage, pxr::UsdPrim& UsdPrim, FUSDExtraToUnrealInfo PrimInfo, TArray<pxr::UsdPrim> &VisitedPrims, AActor* OwnerActor, USceneComponent* ParentComponent, TMap<FName, USceneComponent*> &WorldContent, UWorld* World)
{
	USceneComponent* SceneComponent = nullptr;
	
	FScopedUsdAllocs Allocs;

	// Deal with target prim conversion as Component.
	UE_LOG(LogUsd, Log, TEXT("%s Component: %s"), *StaticEnum<EUnrealConversionMethod>()->GetValueAsString(PrimInfo.ConversionMethod), *PrimInfo.InstanceReference.ToString());

	if (!OwnerActor)
	{
		const pxr::UsdPrimRange PrimRange(UsdPrim);
		for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
		{
			TUsdStore<pxr::UsdPrim> ChildUsdPrim = *PrimRangeIt;
			VisitedPrims.Add(ChildUsdPrim.Get());
		}
		
		UE_LOG(LogUsd, Warning, TEXT("Need Valid Owner Actor for Component: %s"), *PrimInfo.InstanceReference.ToString());
		return false;
	}

	FName ComponentPath = PrimInfo.PrimUsage == EUnrealPrimUsage::Actor ? PrimInfo.InstanceReference : FName(OwnerActor->GetPathName() + "." + PrimInfo.InstanceReference.ToString());
	if (PrimInfo.ConversionMethod == EUnrealConversionMethod::Spawn && !WorldContent.Find(ComponentPath))
	{
		if (PrimInfo.ClassReference->IsChildOf(USceneComponent::StaticClass()))
		{
			UObject* NewSceneComponent = NewObject<UObject>(OwnerActor, PrimInfo.ClassReference, PrimInfo.InstanceReference);
			SceneComponent = Cast<USceneComponent>(NewSceneComponent);
			if (SceneComponent)
			{
				OwnerActor->AddInstanceComponent(SceneComponent);
				SceneComponent->RegisterComponent();
				WorldContent.Add(FName(SceneComponent->GetPathName()), SceneComponent);
			}
		}
	}
	else
	{
		if (WorldContent.Find(ComponentPath))
		{
			SceneComponent = *WorldContent.Find(ComponentPath);
		}
	}

	if (!SceneComponent)
	{
		const pxr::UsdPrimRange PrimRange(UsdPrim);
		for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
		{
			TUsdStore<pxr::UsdPrim> ChildUsdPrim = *PrimRangeIt;
			VisitedPrims.Add(ChildUsdPrim.Get());
		}
		
		UE_LOG(LogUsd, Warning, TEXT("Failed to convert Component: %s"), *PrimInfo.InstanceReference.ToString());
		return false;
	}
	
	if (ParentComponent)
	{
		SceneComponent->AttachToComponent(ParentComponent, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}

	switch (PrimInfo.PrimType)
	{
	case EUnrealPrimType::StaticMesh:
	case EUnrealPrimType::SkeletalMesh:
		ConvertMeshPrim(PrimInfo, Cast<UMeshComponent>(SceneComponent));
		break;
	case EUnrealPrimType::HISM:
		ConvertPointInstancerPrim(Stage, UsdPrim, Cast<UHierarchicalInstancedStaticMeshComponent>(SceneComponent));
		break;
	case EUnrealPrimType::InstancedFoliage:
		ConvertPointInstancerPrim(Stage, UsdPrim, Cast<AInstancedFoliageActor>(OwnerActor), WorldContent);
		break;
	case EUnrealPrimType::BSP:
		ConvertBSPPrim(PrimInfo, UsdPrim, Cast<UBrushComponent>(SceneComponent));
		break;
	default:
		break;
	}

	ConvertXformPrim(Stage,UsdPrim,*SceneComponent,World);

	VisitedPrims.Add(UsdPrim);

	// Traverse children prims
	pxr::UsdPrimSiblingRange PrimRange = UsdPrim.GetChildren();
	for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		TUsdStore<pxr::UsdPrim> ChildUsdPrim = *PrimRangeIt;

		if (VisitedPrims.Contains(ChildUsdPrim.Get()))
		{
			continue;
		}

		FUSDExtraToUnrealInfo ChildPrimInfo = GatherPrimConversionInfo(ChildUsdPrim.Get());
		if (ChildPrimInfo.ConversionMethod == EUnrealConversionMethod::Ignore)
		{
			VisitedPrims.Add(ChildUsdPrim.Get());
			continue;
		}

		if (ChildPrimInfo.PrimUsage == EUnrealPrimUsage::Actor)
		{
			ChildPrimInfo.ActorFolderPath = PrimInfo.ActorFolderPath;
			ConvertActor(Stage, ChildUsdPrim.Get(), ChildPrimInfo, VisitedPrims, SceneComponent,WorldContent,World);
			continue;
		}

		if (ChildPrimInfo.PrimUsage == EUnrealPrimUsage::Component)
		{
			ConvertComponent(Stage, ChildUsdPrim.Get(), ChildPrimInfo, VisitedPrims, OwnerActor, SceneComponent, WorldContent, World);
		}
	}
	
	return true;
}

bool USDExtraToUnreal::ConvertMeshPrim(FUSDExtraToUnrealInfo PrimInfo, UMeshComponent* MeshComponent)
{
	if (PrimInfo.AssetReference)
	{
		if (PrimInfo.PrimType == EUnrealPrimType::StaticMesh)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
			UStaticMesh* StaticMeshAsset = Cast<UStaticMesh>(PrimInfo.AssetReference);
			
			if (StaticMeshComponent && StaticMeshAsset)
			{
				StaticMeshComponent->SetStaticMesh(StaticMeshAsset);
				if (PrimInfo.MaterialReference)
				{
					StaticMeshComponent->SetMaterial(0, PrimInfo.MaterialReference);
				}
			}
		}
		else if (PrimInfo.PrimType == EUnrealPrimType::SkeletalMesh)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent);
			USkeletalMesh* SkeletalMeshAsset = Cast<USkeletalMesh>(PrimInfo.AssetReference);
			
			if (SkeletalMeshComponent && SkeletalMeshAsset)
			{
				SkeletalMeshComponent->SetSkeletalMesh(SkeletalMeshAsset);
				if (PrimInfo.MaterialReference)
				{
					SkeletalMeshComponent->SetMaterial(0, PrimInfo.MaterialReference);
				}
			}
		}
	}
	
	return true;
}

bool USDExtraToUnreal::ConvertBSPPrim(FUSDExtraToUnrealInfo PrimInfo, const pxr::UsdPrim& UsdPrim, UBrushComponent* BrushComponent)
{
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(GetTransientPackage());

	if (!StaticMesh->IsSourceModelValid(0))
	{
		// Add one LOD
		StaticMesh->AddSourceModel();
	}
	
	FMeshDescription* MeshDescription = StaticMesh->CreateMeshDescription(0);

	UsdUtils::FUsdPrimMaterialAssignmentInfo MaterialInfo;
	UsdToUnreal::ConvertGeomMesh( UE::FUsdTyped( UsdPrim ), *MeshDescription, MaterialInfo );
	
	StaticMesh->CommitMeshDescription(0);

	//Set the Imported version before calling the build
	StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;

	StaticMesh->Build();
	// ReSharper disable once CppExpressionWithoutSideEffects
	StaticMesh->MarkPackageDirty();
	
	// Test if the mesh info generated successfully
	//AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(BrushComponent->GetWorld()->SpawnActor(AStaticMeshActor::StaticClass()));
	//MeshActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
	//MeshActor->SetActorTransform(BrushComponent->GetComponentToWorld());
	
	UUSDExtraUtils::CreateModelFromStaticMesh(BrushComponent, StaticMesh);
	//MeshActor->Destroy();

	ABrush* BrushActor = Cast<ABrush>(BrushComponent->GetOwner());
	BrushActor->BrushType = PrimInfo.BSPBrushType;
	
	return true;
}

bool USDExtraToUnreal::ConvertXformPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, USceneComponent& SceneComponent, UWorld* World)
{
	if (UsdToUnreal::ConvertXformable(Stage,pxr::UsdGeomXformable(UsdPrim),SceneComponent,0.0f))
	{
		SceneComponent.Modify();

		UE_LOG(LogUsd, Log, TEXT("Convert XformPrim for %s"), *SceneComponent.GetName());

		return true;
	}
	return false;
}

bool USDExtraToUnreal::ConvertPointInstancerPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, UHierarchicalInstancedStaticMeshComponent* HISMComponent)
{
	if (!HISMComponent || !UsdPrim)
	{
		return false;
	}
	
	HISMComponent->Modify();
	HISMComponent->ClearInstances();
	
	FScopedUsdAllocs UsdAllocs;
	
	pxr::UsdPrim HISMInstance = UsdPrim.GetChild(pxr::TfToken("HISMInstance"));
	if (!HISMInstance)
	{
		return false;
	}
	
	pxr::UsdGeomPointInstancer PointInstancer(HISMInstance);
	if (!PointInstancer)
	{
		return false;
	}
	
	const pxr::UsdPrim Prototypes = HISMInstance.GetChild(pxr::TfToken("Prototypes"));
	if (!Prototypes)
	{
		return false;
	}

	TArray<TUsdStore<pxr::UsdPrim>> MeshPrototypes = UsdUtils::GetAllPrimsOfType(Prototypes, pxr::TfType::Find< pxr::UsdGeomMesh >());

	if (MeshPrototypes.IsValidIndex(0))
	{
		pxr::UsdPrim Mesh = MeshPrototypes[0].Get();
		const FUSDExtraToUnrealInfo MeshInfo = GatherPrimConversionInfo(Mesh);

		if (MeshInfo.AssetReference)
		{
			UStaticMesh* MeshAsset = Cast<UStaticMesh>(MeshInfo.AssetReference);
			if (MeshAsset)
			{
				HISMComponent->SetStaticMesh(MeshAsset);
				if (MeshInfo.MaterialReference)
				{
					HISMComponent->SetMaterial(0, MeshInfo.MaterialReference);
				}
				pxr::VtMatrix4dArray UsdInstanceTransforms;

				if ( !PointInstancer.ComputeInstanceTransformsAtTime(
						&UsdInstanceTransforms,
						0.0f,
						0.0f ) )
				{
					return false;
				}

				FUsdStageInfo StageInfo( Stage );

				FScopedUnrealAllocs UnrealAllocs;
				
				for ( pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms )
				{
					FTransform InstanceTransform = UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );
					HISMComponent->AddInstance( InstanceTransform );
				}

				HISMComponent->BuildTreeIfOutdated(true, true);

				return true;
			}
		}
	}
	
	return false;
}

bool USDExtraToUnreal::ConvertPointInstancerPrim(const pxr::UsdStageRefPtr& Stage, const pxr::UsdPrim& UsdPrim, AInstancedFoliageActor* FoliageActor, TMap<FName, USceneComponent*> WorldContent)
{
	if (!FoliageActor || !UsdPrim)
	{
		return false;
	}

	FoliageActor->Modify();
	TMap<UFoliageType*, FFoliageInfo*> InstancesFoliageType = FoliageActor->GetAllInstancesFoliageType();
	TArray<UFoliageType*> FoliageTypes;
	InstancesFoliageType.GetKeys(FoliageTypes);
	FoliageActor->RemoveFoliageType(FoliageTypes.GetData(), FoliageTypes.Num());	
	
	FScopedUsdAllocs UsdAllocs;
	
	pxr::UsdGeomPointInstancer PointInstancer( UsdPrim );
	if ( !PointInstancer )
	{
		return false;
	}
	
	const pxr::UsdPrim Prototypes = UsdPrim.GetChild(pxr::TfToken("Prototypes"));
	if (!Prototypes)
	{
		return false;
	}

	TArray<TUsdStore<pxr::UsdPrim>> MeshPrototypes = UsdUtils::GetAllPrimsOfType(Prototypes, pxr::TfType::Find< pxr::UsdGeomMesh >());


	for (int32 ProtoIndex = 0; ProtoIndex < MeshPrototypes.Num(); ++ProtoIndex)
	{
		pxr::UsdPrim MeshPrim = MeshPrototypes[ProtoIndex].Get();
		
		const FUSDExtraToUnrealInfo MeshInfo = GatherPrimConversionInfo(MeshPrim);
		if (MeshInfo.AssetReference)
		{
			UStaticMesh* MeshAsset = Cast<UStaticMesh>(MeshInfo.AssetReference);
			if (MeshAsset)
			{
				UFoliageType_InstancedStaticMesh* MeshSetting = nullptr;
				if (MeshInfo.MaterialReference)
				{
					MeshSetting = NewObject<UFoliageType_InstancedStaticMesh>(GetTransientPackage());
					MeshSetting->Mesh = MeshAsset;
					MeshSetting->OverrideMaterials.Add(MeshInfo.MaterialReference);
				}
				
				UFoliageType* NewFoliageType;
				FFoliageInfo* NewFoliageInfo = FoliageActor->AddMesh(MeshAsset, &NewFoliageType, MeshSetting);
				
				pxr::VtArray< int > ProtoIndices = UsdUtils::GetUsdValue< pxr::VtArray< int > >( PointInstancer.GetProtoIndicesAttr(), 0.0f );
				pxr::VtMatrix4dArray UsdInstanceTransforms;
				if ( !PointInstancer.ComputeInstanceTransformsAtTime(
					&UsdInstanceTransforms,
					0.0f,
					0.0f ))
				{
					continue;
				}

				// Deal with base components
				pxr::VtArray<int> BaseComponentIndices = UsdUtils::GetUsdValue<pxr::VtArray<int>>(UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealBaseComponentIndices),UsdUtils::GetDefaultTimeCode());
				pxr::UsdAttribute BaseComponentReferencesAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealBaseComponentReferences);
				pxr::VtArray<std::string> BaseComponentReferences;
				BaseComponentReferencesAttr.Get<pxr::VtArray<std::string>>(&BaseComponentReferences);
				
				TArray<UActorComponent*> BaseComponents;
				for (std::string& BaseComponentReference : BaseComponentReferences)
				{
					FName ComponentPathName = FName(UsdToUnreal::ConvertString(BaseComponentReference));
					UE_LOG(LogUsd, Error, TEXT("Base Component Path Name: %s"), *ComponentPathName.ToString());
					
					if (WorldContent.Find(ComponentPathName))
					{
						USceneComponent* SceneComponent = *WorldContent.Find(ComponentPathName);
						BaseComponents.Add(SceneComponent);
					}
					else
					{
						BaseComponents.Add(FoliageActor->GetRootComponent());
					}
				}
				
				FUsdStageInfo StageInfo( Stage );

				int32 Index = 0;

				FScopedUnrealAllocs UnrealAllocs;
				
				for ( pxr::GfMatrix4d& UsdMatrix : UsdInstanceTransforms )
				{
					if ( ProtoIndices[ Index ] == ProtoIndex )
					{
						FTransform InstanceTransform = UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );
						FFoliageInstance FoliageInstance;
						FoliageInstance.Location = InstanceTransform.GetLocation();
						FoliageInstance.Rotation = InstanceTransform.GetRotation().Rotator();
						FoliageInstance.PreAlignRotation = InstanceTransform.GetRotation().Rotator();
						FoliageInstance.DrawScale3D = InstanceTransform.GetScale3D();

						FVector start = FoliageInstance.Location + FVector(0, 0, 500);
						FVector end = FoliageInstance.Location + FVector(0, 0, -500);

						//FDesiredFoliageInstance* DesiredInstance = FDesiredFoliageInstance(start, end);
						FFoliagePaintingGeometryFilter OverrideGeometryFilter;

						FHitResult Hit;
						static FName NAME_AddFoliageInstances = FName(TEXT("AddFoliageInstances"));
						if (AInstancedFoliageActor::FoliageTrace(GWorld, Hit, FDesiredFoliageInstance(start, end),
							NAME_AddFoliageInstances, true, OverrideGeometryFilter))
						{
							float HitWeight = 1.f;

							UPrimitiveComponent* InstanceBase = Hit.GetComponent();

							if (InstanceBase)
							{
								FoliageInstance.BaseComponent = InstanceBase;
							}
						}
						else
						{
							FoliageInstance.BaseComponent = BaseComponents[BaseComponentIndices[Index]];
						}

						NewFoliageInfo->AddInstance(FoliageActor, NewFoliageType, FoliageInstance);
					}

					++Index;
				}

				NewFoliageInfo->Refresh(FoliageActor, true, false);
			}
		}
	}
	
	return true;
}

FUSDExtraToUnrealInfo USDExtraToUnreal::GatherPrimConversionInfo(pxr::UsdPrim& UsdPrim)
{
	FUSDExtraToUnrealInfo USDExtraToUnrealInfo;

	FScopedUsdAllocs Allocs;

	if (const pxr::UsdAttribute PrimUsageAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealPrimUsage))
	{
		pxr::TfToken PrimUsage;
		PrimUsageAttr.Get<pxr::TfToken>(&PrimUsage);
		USDExtraToUnrealInfo.PrimUsage = PrimUsage == USDExtraTokensType::Actor ? EUnrealPrimUsage::Actor : EUnrealPrimUsage::Component;
	}
	
	if (const pxr::UsdAttribute ConversionMethodAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealConversionMethod))
	{
		pxr::TfToken ConversionMethod;
		ConversionMethodAttr.Get<pxr::TfToken>(&ConversionMethod);
		if (ConversionMethod == USDExtraTokensType::Ignore)
		{
			USDExtraToUnrealInfo.ConversionMethod = EUnrealConversionMethod::Ignore;
		}
		else
		{
			USDExtraToUnrealInfo.ConversionMethod = ConversionMethod == USDExtraTokensType::Spawn ? EUnrealConversionMethod::Spawn : EUnrealConversionMethod::Modify;
		}
	}
	
	if (const pxr::UsdAttribute InstanceReferenceAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealInstanceReference))
	{
		std::string InstanceReference;
		InstanceReferenceAttr.Get<std::string>(&InstanceReference);
		USDExtraToUnrealInfo.InstanceReference = FName(UsdToUnreal::ConvertString(InstanceReference));
	}
	
	if (const pxr::UsdAttribute ClassReferenceAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealClassReference))
	{
		std::string ClassReference;
		ClassReferenceAttr.Get<std::string>(&ClassReference);
		FString ClassPath = UsdToUnreal::ConvertString(ClassReference);
		USDExtraToUnrealInfo.ClassReference = StaticLoadClass(UObject::StaticClass(),nullptr, *ClassPath);
	}
	
	if (const pxr::UsdAttribute AssetReferenceAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealAssetReference))
	{
		std::string AssetReference;
		AssetReferenceAttr.Get<std::string>(&AssetReference);
		FString AssetPath = UsdToUnreal::ConvertString(AssetReference);
		if (AssetPath != "None")
		{
			USDExtraToUnrealInfo.AssetReference = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		}
	}

	if (const pxr::UsdAttribute MaterialReferenceAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealMaterialReference))
	{
		std::string MaterialReference;
		MaterialReferenceAttr.Get<std::string>(&MaterialReference);
		FString MaterialPath = UsdToUnreal::ConvertString(MaterialReference);
		if (MaterialPath != "None")
		{
			USDExtraToUnrealInfo.MaterialReference = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
		}
	}
	
	if (const pxr::UsdAttribute ActorFolderPathAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealActorFolderPath))
	{
		std::string ActorFolderPath;
		ActorFolderPathAttr.Get<std::string>(&ActorFolderPath);
		USDExtraToUnrealInfo.ActorFolderPath = FName(UsdToUnreal::ConvertString(ActorFolderPath));
	}

	pxr::TfToken PrimTypeName = UsdPrim.GetPrimTypeInfo().GetTypeName();
	if (PrimTypeName == USDExtraTokensType::USDActorFolder)
	{
		if (const pxr::UsdAttribute UnrealPrimUsageAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealPrimUsage))
		{
			pxr::TfToken UnrealPrimUsage;
			UnrealPrimUsageAttr.Get<pxr::TfToken>(&UnrealPrimUsage);
			if (UnrealPrimUsage == USDExtraTokensType::Folder)
			{
				USDExtraToUnrealInfo.PrimType = EUnrealPrimType::Folder;
			}
		}
	}
	else if (PrimTypeName == USDExtraTokensType::USDScene)
	{
		USDExtraToUnrealInfo.PrimType = EUnrealPrimType::Scene;

		if (USDExtraToUnrealInfo.ClassReference == UHierarchicalInstancedStaticMeshComponent::StaticClass())
		{
			USDExtraToUnrealInfo.PrimType = EUnrealPrimType::HISM;
		}
	}
	else if (PrimTypeName == USDExtraTokensType::USDStaticMesh)
	{
		USDExtraToUnrealInfo.PrimType = EUnrealPrimType::StaticMesh;

		if (const pxr::UsdAttribute UnrealPrimTypeAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealPrimType))
		{
			pxr::TfToken UnrealPrimType;
			UnrealPrimTypeAttr.Get<pxr::TfToken>(&UnrealPrimType);
			if (UnrealPrimType == USDExtraTokensType::PrimTypeBSP)
			{
				USDExtraToUnrealInfo.PrimType = EUnrealPrimType::BSP;

				if (const pxr::UsdAttribute UnrealBSPBrushTypeAttr = UsdPrim.GetAttribute(USDExtraIdentifiers::UnrealBSPBrushType))
				{
					pxr::TfToken UnrealBSPBrushType;
					UnrealBSPBrushTypeAttr.Get<pxr::TfToken>(&UnrealBSPBrushType);
					if (UnrealBSPBrushType == USDExtraTokensType::Add)
					{
						USDExtraToUnrealInfo.BSPBrushType = Brush_Add;
					}
					else if (UnrealBSPBrushType == USDExtraTokensType::Subtract)
					{
						USDExtraToUnrealInfo.BSPBrushType = Brush_Subtract;
					}
					else if (UnrealBSPBrushType == USDExtraTokensType::Max)
					{
						USDExtraToUnrealInfo.BSPBrushType = Brush_MAX;
					}
				}
			}
		}
	}
	else if (PrimTypeName == USDExtraTokensType::USDSkeletalMesh)
	{
		USDExtraToUnrealInfo.PrimType = EUnrealPrimType::SkeletalMesh;
	}
	else if (PrimTypeName == USDExtraTokensType::USDInstancedStaticMesh)
	{
		if (USDExtraToUnrealInfo.ClassReference == AInstancedFoliageActor::StaticClass())
		{
			USDExtraToUnrealInfo.PrimType = EUnrealPrimType::InstancedFoliage;
		}
	}
	
	return USDExtraToUnrealInfo;
}

bool UnrealToUSDExtra::ConvertSceneComponent(const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim)
{
	if (UnrealToUsd::ConvertSceneComponent(Stage, SceneComponent, UsdPrim))
	{
		return AddUSDExtraAttributesForSceneComponent(Stage, SceneComponent, UsdPrim);
	}
	return false;
}

bool UnrealToUSDExtra::ConvertMeshComponent(const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, pxr::UsdPrim& UsdPrim)
{
	if (UnrealToUsd::ConvertMeshComponent(Stage, MeshComponent, UsdPrim))
	{
		return AddUSDExtraAttributesForMeshComponent(Stage, MeshComponent, UsdPrim);
	}

	return false;
}

bool UnrealToUSDExtra::ConvertBrushComponent(const pxr::UsdStageRefPtr& Stage, const UBrushComponent* BrushComponent, pxr::UsdPrim& UsdPrim)
{
	ABrush* BrushActor = Cast<ABrush>(BrushComponent->GetOwner());
	if (!BrushActor)
	{
		return false;
	}
	
	const FName MeshName = FName(BrushComponent->GetName() + "_StaticMesh");
	UStaticMesh* StaticMesh = UUSDExtraUtils::CreateStaticMeshFromBrush(GetTransientPackage(), MeshName,BrushActor,BrushComponent->Brush);
	if (StaticMesh)
	{
		if (!UnrealToUsd::ConvertStaticMesh(StaticMesh, UsdPrim))
		{
			return false;
		}
		// Test if the mesh info generated successfully
		// const AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(BrushComponent->GetWorld()->SpawnActor(AStaticMeshActor::StaticClass()));
		// MeshActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
	}

	if (const pxr::UsdAttribute UnrealPrimUsageAttr = UsdPrim.CreateAttribute(USDExtraIdentifiers::UnrealPrimType, pxr::SdfValueTypeNames->Token))
	{
		const pxr::TfToken UnrealPrimUsage = USDExtraTokensType::PrimTypeBSP;
		// ReSharper disable once CppExpressionWithoutSideEffects
		UnrealPrimUsageAttr.Set(UnrealPrimUsage);
	}

	if (const pxr::UsdAttribute UnrealBSPBrushTypeAttr = UsdPrim.CreateAttribute(USDExtraIdentifiers::UnrealBSPBrushType, pxr::SdfValueTypeNames->Token))
	{
		pxr::TfToken UnrealBSPBrushType;

		switch (BrushActor->BrushType)
		{
		case Brush_Add:
			UnrealBSPBrushType = USDExtraTokensType::Add;
			break;
		case Brush_Subtract:
			UnrealBSPBrushType = USDExtraTokensType::Subtract;
			break;
		case Brush_MAX:
			UnrealBSPBrushType = USDExtraTokensType::Max;
			break;
		case Brush_Default:
		default:
			UnrealBSPBrushType = USDExtraTokensType::Default;
			break;
		}
		
		// ReSharper disable once CppExpressionWithoutSideEffects
		UnrealBSPBrushTypeAttr.Set(UnrealBSPBrushType);
	}
	return true;
}

bool UnrealToUSDExtra::ConvertHierarchicalInstancedStaticMeshComponent(const UHierarchicalInstancedStaticMeshComponent* HISMComponent, pxr::UsdPrim& UsdPrim, double TimeCode)
{
	if (UnrealToUsd::ConvertHierarchicalInstancedStaticMeshComponent(HISMComponent, UsdPrim, TimeCode))
	{
		return AddUSDExtraAttributesForHISMComponent(HISMComponent, UsdPrim);
	}
	return false;
}

bool UnrealToUSDExtra::ConvertInstancedFoliageActor(const AInstancedFoliageActor& Actor, pxr::UsdPrim& UsdPrim, double TimeCode)
{
#if WITH_EDITOR
	using namespace pxr;

	FScopedUsdAllocs Allocs;

	UsdGeomPointInstancer PointInstancer{ UsdPrim };
	if ( !PointInstancer )
	{
		return false;
	}

	UsdStageRefPtr Stage = UsdPrim.GetStage();
	FUsdStageInfo StageInfo{ Stage };

	// Add records for base components
	// ReSharper disable once CppTooWideScope
	VtArray<std::string> UnrealBaseComponentReferences;
	VtArray<int> UnrealBaseComponentIndices;
	
	VtArray<int> ProtoIndices;
	VtArray<GfVec3f> Positions;
	VtArray<GfQuath> Orientations;
	VtArray<GfVec3f> Scales;

	TArray<UActorComponent*> BaseComponents;
	BaseComponents.Add(nullptr);
	
	int PrototypeIndex = 0;
	for ( const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliagePair : Actor.FoliageInfos )
	{
		const FFoliageInfo& Info = FoliagePair.Value.Get();

		//for (const TPair<FFoliageInstanceBaseId, FFoliageInstanceBaseInfo>& FoliageInstancePair : Actor.InstanceBaseCache.InstanceBaseMap)
		
		for ( const TPair<FFoliageInstanceBaseId, TSet<int32>>& Pair : Info.ComponentHash )
		{
			const FFoliageInstanceBaseId BaseId = Pair.Key;
			const TSet<int32>& InstanceSet = Pair.Value;
			
			const int32 NumInstances = InstanceSet.Num();
			ProtoIndices.reserve( ProtoIndices.size() + NumInstances );
			Positions.reserve( Positions.size() + NumInstances );
			Orientations.reserve( Orientations.size() + NumInstances );
			Scales.reserve( Scales.size() + NumInstances );
			UnrealBaseComponentIndices.reserve(UnrealBaseComponentIndices.size() + NumInstances);

			int UnrealBaseComponentIndex = 0;
			if (auto BaseInfo = Actor.InstanceBaseCache.InstanceBaseMap.Find(BaseId))
			{
				if (BaseComponents.Contains(BaseInfo->BasePtr.Get()))
				{
					UnrealBaseComponentIndex = BaseComponents.IndexOfByKey(BaseInfo->BasePtr.Get());
				}
				else
				{
					UnrealBaseComponentIndex = BaseComponents.Add(BaseInfo->BasePtr.Get());
				}
			}
			
			for ( int32 InstanceIndex : InstanceSet )
			{
				const FFoliageInstancePlacementInfo* Instance = &Info.Instances[ InstanceIndex ];

				// Convert axes
				FTransform UETransform{ Instance->Rotation, Instance->Location, Instance->DrawScale3D };
				FTransform USDTransform = ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, UETransform );

				FVector Translation = USDTransform.GetTranslation();
				FQuat Rotation = USDTransform.GetRotation();
				FVector Scale = USDTransform.GetScale3D();

				// Compensate metersPerUnit
				constexpr float UEMetersPerUnit = 0.01f;
				if ( !FMath::IsNearlyEqual( UEMetersPerUnit, StageInfo.MetersPerUnit ) )
				{
					Translation *= ( UEMetersPerUnit / StageInfo.MetersPerUnit );
				}

				ProtoIndices.push_back( PrototypeIndex );
				Positions.push_back( GfVec3f( Translation.X, Translation.Y, Translation.Z ) );
				Orientations.push_back( GfQuath( Rotation.W, Rotation.X, Rotation.Y, Rotation.Z ) );
				Scales.push_back( GfVec3f( Scale.X, Scale.Y, Scale.Z ) );
				UnrealBaseComponentIndices.push_back(UnrealBaseComponentIndex);
			}
		}

		++PrototypeIndex;
	}

	const pxr::UsdTimeCode UsdTimeCode( TimeCode );

	if ( UsdAttribute Attr = PointInstancer.CreateProtoIndicesAttr() )
	{
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set( ProtoIndices, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreatePositionsAttr() )
	{
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set( Positions, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateOrientationsAttr() )
	{
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set( Orientations, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateScalesAttr() )
	{
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set( Scales, UsdTimeCode );
	}

	if (UsdAttribute Attr = UsdPrim.CreateAttribute(USDExtraIdentifiers::UnrealBaseComponentReferences, SdfValueTypeNames->StringArray))
	{
		UnrealBaseComponentReferences.reserve(BaseComponents.Num());

		for (UActorComponent* ActorComponent : BaseComponents)
		{
			if (ActorComponent)
			{
				UnrealBaseComponentReferences.push_back(UnrealToUsd::ConvertString(*ActorComponent->GetPathName()).Get());
			}
			else
			{
				UnrealBaseComponentReferences.push_back(UnrealToUsd::ConvertString(*FString("None")).Get());
			}
		}
		
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set(UnrealBaseComponentReferences, UsdTimeCode);
	}

	if (UsdAttribute Attr = UsdPrim.CreateAttribute(USDExtraIdentifiers::UnrealBaseComponentIndices, SdfValueTypeNames->IntArray))
	{
		// ReSharper disable once CppExpressionWithoutSideEffects
		Attr.Set(UnrealBaseComponentIndices, UsdTimeCode);
	}
	
	return AddUSDExtraAttributesForFoliageComponent(Actor, UsdPrim);;
#else
	return false;
#endif // WITH_EDITOR
	

}

bool UnrealToUSDExtra::AddUSDExtraAttributesForSceneComponent(const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim)
{
	AActor* OwnerActor = SceneComponent->GetOwner();
	USceneComponent* RootComponent = OwnerActor->GetRootComponent();

	FScopedUsdAllocs Allocs;

	pxr::SdfPath OverridePrimPath = UsdPrim.GetPath();
	pxr::UsdPrim OverridePrim = Stage->OverridePrim( OverridePrimPath );
	pxr::TfToken UnrealPrimUsage;

	if ( pxr::UsdAttribute UnrealPrimUsageAttr = OverridePrim.CreateAttribute( USDExtraIdentifiers::UnrealPrimUsage, pxr::SdfValueTypeNames->Token ) )
	{
		UnrealPrimUsage = SceneComponent == RootComponent ? USDExtraTokensType::Actor : USDExtraTokensType::Component;
		// ReSharper disable once CppExpressionWithoutSideEffects
		UnrealPrimUsageAttr.Set(UnrealPrimUsage);
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Create Prim Usage Attribute Failed for %s"), *SceneComponent->GetName());
		return false;
	}

	if ( pxr::UsdAttribute UnrealConversionMethodAttr = OverridePrim.CreateAttribute( USDExtraIdentifiers::UnrealConversionMethod, pxr::SdfValueTypeNames->Token ) )
	{
		pxr::TfToken UnrealConversionMethod;
		UnrealConversionMethod = USDExtraTokensType::Spawn;
		// ReSharper disable once CppExpressionWithoutSideEffects
		UnrealConversionMethodAttr.Set(UnrealConversionMethod);
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Create Conversion Method Attribute Failed for %s"), *SceneComponent->GetName());
		return false;
	}

	if ( pxr::UsdAttribute UnrealInstanceReferenceAttr = OverridePrim.CreateAttribute( USDExtraIdentifiers::UnrealInstanceReference, pxr::SdfValueTypeNames->String ) )
	{
		FString UnrealInstanceReference;
		if (UnrealPrimUsage == USDExtraTokensType::Actor)
		{
			UnrealInstanceReference = OwnerActor->GetPathName();
		}
		else if (UnrealPrimUsage == USDExtraTokensType::Component)
		{
			UnrealInstanceReference = SceneComponent->GetName();
		}
		
		UnrealInstanceReferenceAttr.Set(UnrealToUsd::ConvertString(*UnrealInstanceReference).Get());
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Create Instance Reference Attribute Failed for %s"), *SceneComponent->GetName());
		return false;
	}

	if ( pxr::UsdAttribute UnrealClassReferenceAttr = OverridePrim.CreateAttribute( USDExtraIdentifiers::UnrealClassReference, pxr::SdfValueTypeNames->String ) )
	{
		FString UnrealAssetReference;
		if (UnrealPrimUsage == USDExtraTokensType::Actor)
    	{
			UnrealAssetReference = OwnerActor->GetClass()->GetPathName();
    	}
    	else if (UnrealPrimUsage == USDExtraTokensType::Component)
    	{
			UnrealAssetReference = SceneComponent->GetClass()->GetPathName();
    	}
		
		UnrealClassReferenceAttr.Set(UnrealToUsd::ConvertString(*UnrealAssetReference).Get());
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Create Class Reference Attribute Failed for %s"), *SceneComponent->GetName());
		return false;
	}

	if (Cast<UBrushComponent>(SceneComponent))
	{
		if (pxr::UsdAttribute UnrealVisibilityAttr = OverridePrim.GetAttribute(pxr::TfToken("visibility")))
		{
			pxr::TfToken Visibility = SceneComponent->GetOwner()->IsHiddenEd() ? pxr::TfToken("invisible") : pxr::TfToken("inherited");
			// ReSharper disable once CppExpressionWithoutSideEffects
			UnrealVisibilityAttr.Set(Visibility);
		}
	}

	return true;
}

bool UnrealToUSDExtra::AddUSDExtraAttributesForMeshComponent(const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, const pxr::UsdPrim& UsdPrim)
{
	FScopedUsdAllocs Allocs;

	const pxr::SdfPath OverridePrimPath = UsdPrim.GetPath();
	const pxr::UsdPrim OverridePrim = Stage->OverridePrim( OverridePrimPath );

	const pxr::TfToken PrimTypeName = OverridePrim.GetPrimTypeInfo().GetTypeName();
	
	if (PrimTypeName != USDExtraTokensType::USDStaticMesh && PrimTypeName != USDExtraTokensType::USDSkeletalMesh)
	{
		return true;		
	}

	return true;
	
	// We think all the mesh components would remain the existed mesh assets by default
	// when exporting, so this attribute should be created by other DCCs rather than unreal
	// to tell us if the mesh asset should be override.
	/**
	if (const pxr::UsdAttribute UnrealAssetReferenceAttr = OverridePrim.CreateAttribute( USDExtraIdentifiers::UnrealAssetReference, pxr::SdfValueTypeNames->String ) )
	{
		FString UnrealAssetReference;
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<const UStaticMeshComponent>(MeshComponent))
		{
			UnrealAssetReference = StaticMeshComponent->GetStaticMesh()->GetPathName();
		}
		else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<const USkeletalMeshComponent>(MeshComponent))
		{
			UnrealAssetReference = SkeletalMeshComponent->SkeletalMesh->GetPathName();
		}
	
		UnrealAssetReferenceAttr.Set(UnrealToUsd::ConvertString(*UnrealAssetReference).Get());
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Create Asset Reference Attribute Failed for %s"), *MeshComponent->GetName());
		return false;
	}
	**/
}

bool UnrealToUSDExtra::AddUSDExtraAttributesForHISMComponent(const UHierarchicalInstancedStaticMeshComponent* HISMComponent, const pxr::UsdPrim& UsdPrim)
{
	FScopedUsdAllocs Allocs;

	const pxr::UsdPrim Prototypes = UsdPrim.GetChild(pxr::TfToken("Prototypes"));

	const pxr::UsdPrimSiblingRange PrimRange = Prototypes.GetChildren();
	for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		TUsdStore<pxr::UsdPrim> Instance = *PrimRangeIt;

		if (Instance.Get().GetPrimTypeInfo().GetTypeName() == USDExtraTokensType::USDStaticMesh)
		{
			if (const pxr::UsdAttribute UnrealAssetReferenceAttr = Instance.Get().CreateAttribute( USDExtraIdentifiers::UnrealAssetReference, pxr::SdfValueTypeNames->String ))
			{
				const FString UnrealAssetReference = HISMComponent->GetStaticMesh()->GetPathName();
				UnrealAssetReferenceAttr.Set(UnrealToUsd::ConvertString(*UnrealAssetReference).Get());
			}
		}
	}

	return true;
}

bool UnrealToUSDExtra::AddUSDExtraAttributesForFoliageComponent(const AInstancedFoliageActor& FoliageActor, pxr::UsdPrim& UsdPrim)
{
	FScopedUsdAllocs Allocs;

	TArray<UFoliageType*> FoliageTypes;
	FoliageActor.FoliageInfos.GetKeys(FoliageTypes);

	const pxr::UsdPrim Prototypes = UsdPrim.GetChild(pxr::TfToken("Prototypes"));

	int32 Index = 0;
	const pxr::UsdPrimSiblingRange PrimRange = Prototypes.GetChildren();
	for ( pxr::UsdPrimSiblingRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		TUsdStore<pxr::UsdPrim> Instance = *PrimRangeIt;

		if (Instance.Get().GetPrimTypeInfo().GetTypeName() == USDExtraTokensType::USDStaticMesh)
		{
			if (const pxr::UsdAttribute UnrealAssetReferenceAttr = Instance.Get().CreateAttribute( USDExtraIdentifiers::UnrealAssetReference, pxr::SdfValueTypeNames->String ))
			{
				const FString UnrealAssetReference = FoliageTypes[Index]->GetSource()->GetPathName();
				UnrealAssetReferenceAttr.Set(UnrealToUsd::ConvertString(*UnrealAssetReference).Get());
			}
		}
		
		Index++;
	}
	
	return true;
}

#endif

