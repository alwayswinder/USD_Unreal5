// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"

#include "USDExtra.generated.h"


class ALandscape;

UCLASS(BlueprintType)
class AUSDDetails : public AActor
{
	GENERATED_BODY()
public:
	AUSDDetails(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land")
	ALandscape* Landscape;
};

class FMyCustomUSDDetails : public IDetailCustomization
{
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)override;
private:
	TSharedPtr<IPropertyHandle> USDProperty;
};

class FUSDExtraModule : public IModuleInterface
{
public:
	UObject* GetUSDDetails();
	void RemoveUSDDetails();

public:
	AActor* USDDetails;
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
