#include "USDExtraSettings.h"

#define LOCTEXT_NAMESPACE "USDExtraPlugin"

UUSDExtraSettings::UUSDExtraSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("USDExtra");
}

#if WITH_EDITOR
FText UUSDExtraSettings::GetSectionText() const
{
	return LOCTEXT("UserSettingsDisplayName", "USDExtra");
}
#endif

#undef LOCTEXT_NAMESPACE
