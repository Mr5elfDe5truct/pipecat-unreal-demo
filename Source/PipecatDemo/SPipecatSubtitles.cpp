//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "SPipecatSubtitles.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPipecatSubtitles::Construct(const FArguments& InArgs)
{
	// Lines are as wide as their text, up to most of the screen.
	auto Line = [](TSharedPtr<STextBlock>& Text, int32 Size, const FLinearColor& Color) {
		return SAssignNew(Text, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
			.ColorAndOpacity(Color)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.Justification(ETextJustify::Center)
			.WrapTextAt(1200.0f);
	};

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(40.0f, 0.0f, 40.0f, 60.0f))
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
		.Padding(FMargin(24.0f, 14.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				Line(Status, 14, FLinearColor(0.45f, 0.85f, 1.0f))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				Line(User, 18, FLinearColor(0.75f, 0.75f, 0.75f))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				Line(Bot, 24, FLinearColor::White)
			]
		]
	];
}

void SPipecatSubtitles::SetStatus(const FString& Text)
{
	Status->SetText(FText::FromString(Text));
}

void SPipecatSubtitles::SetUserText(const FString& Text)
{
	User->SetText(Text.IsEmpty() ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("User: %s"), *Text)));
}

void SPipecatSubtitles::SetBotText(const FString& Text)
{
	Bot->SetText(Text.IsEmpty() ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("Orb: %s"), *Text)));
}
