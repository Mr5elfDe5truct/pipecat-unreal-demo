//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

// Subtitles at the bottom of the screen: what the user and the bot (the orb)
// say, and the connection's status.
class SPipecatSubtitles : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPipecatSubtitles) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetStatus(const FString& Text);
	void SetUserText(const FString& Text);
	void SetBotText(const FString& Text);

private:
	TSharedPtr<STextBlock> Status;
	TSharedPtr<STextBlock> User;
	TSharedPtr<STextBlock> Bot;
};
