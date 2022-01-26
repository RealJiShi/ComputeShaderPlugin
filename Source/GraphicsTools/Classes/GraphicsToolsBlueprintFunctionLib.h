#pragma once

#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GraphicsToolsBlueprintFunctionLib.generated.h"

class UTextureRenderTarget2D;

UCLASS(MinimalAPI, meta = (ScriptName = "GraphicsTools"))
class UGraphicsToolsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UGraphicsToolsBlueprintLibrary() {}

	UFUNCTION(BlueprintCallable, Category = "GraphicsTools", meta = (WorldContext = "WorldContextObject"))
	static void DrawCheckerBoard(
		const UObject *WorldContextObject,
		class UTextureRenderTarget2D *OutputRenderTarget
	);
};