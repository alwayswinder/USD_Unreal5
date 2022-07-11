// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "USDExtraSettings.generated.h"

UENUM(BlueprintType)
enum EUSDFormat
{
	USD = 0 UMETA(DisplayName = ".usd"),
	USDA = 1 UMETA(DisplayName = ".usda"),
	USDC = 2 UMETA(DisplayName = ".usdc")
};

UCLASS(config = Editor, defaultconfig)
class USDEXTRA_API UUSDExtraSettings : public UDeveloperSettings
{
	GENERATED_BODY()

	UUSDExtraSettings();

public:
	// UDeveloperSettings interface.
	virtual FText GetSectionText() const override;
	// End of UDeveloperSettings interface.
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, config)
	FDirectoryPath USDRepository;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config)
	TEnumAsByte<EUSDFormat> USDFormat = EUSDFormat::USDA;
};
