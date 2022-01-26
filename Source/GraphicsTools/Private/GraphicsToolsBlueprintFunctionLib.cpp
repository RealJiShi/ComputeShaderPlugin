#include "GraphicsToolsBlueprintFunctionLib.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "Runtime/Engine/Classes/Engine/World.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "SceneInterface.h"
#include "ShaderParameterUtils.h"
#include "Logging/MessageLog.h"
#include "Internationalization/Internationalization.h"
#include "Runtime/RenderCore/Public/RenderTargetPool.h"
#include "Runtime/RenderCore/Public/RenderGraphUtils.h"
#include "Runtime/RenderCore/Public/RenderGraph.h"

#define LOCTEXT_NAMESPACE "GraphicToolsPlugin"

#define USE_RDG 1

class FCheckerBoardComputeShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCheckerBoardComputeShader);
	SHADER_USE_PARAMETER_STRUCT(FCheckerBoardComputeShader, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
#if USE_RDG
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<FVector4>, OutputTexture)
#else
		SHADER_PARAMETER_UAV(RWTexture2D<FVector4>, OutputTexture)
#endif
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FShaderPermutationParameters&, FShaderCompilerEnvironment&)
	{

	}
};
IMPLEMENT_SHADER_TYPE(, FCheckerBoardComputeShader, TEXT("/Plugins/GraphicsTools/Private/CheckerBoard.usf"), TEXT("MainCS"), SF_Compute);

static void DrawCheckerBoard_RenderThread(
	FRHICommandListImmediate &RHICmdList,
	FTextureRenderTargetResource* TextureRenderTargetResource,
	ERHIFeatureLevel::Type FeatureLevel
)
{
	check(IsInRenderingThread());

	FTexture2DRHIRef RenderTargetTexture = TextureRenderTargetResource->GetRenderTargetTexture();

#if USE_RDG
	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(RenderTargetTexture->GetSizeXY(), PF_FloatRGBA, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV));
	FRDGTextureRef RDGRenderTarget = GraphBuilder.CreateTexture(Desc, TEXT("RDGRenderTarget"));
	FCheckerBoardComputeShader::FParameters* Parameters = GraphBuilder.AllocParameters<FCheckerBoardComputeShader::FParameters>();
	FRDGTextureUAVDesc UAVDesc(RDGRenderTarget);
	Parameters->OutputTexture = GraphBuilder.CreateUAV(UAVDesc);
	TShaderMapRef<FCheckerBoardComputeShader>ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	uint32 GGroupSize = 32;
	uint32 GroupSizeX = FMath::DivideAndRoundUp((uint32)RenderTargetTexture->GetSizeX(), GGroupSize);
	uint32 GroupSizeY = FMath::DivideAndRoundUp((uint32)RenderTargetTexture->GetSizeY(), GGroupSize);
	FIntVector GroupCounts = FIntVector(GroupSizeX, GroupSizeY, 1);
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RDGCompute"),
		Parameters,
		ERDGPassFlags::Compute,
		[Parameters, ComputeShader, GroupCounts](FRHICommandList& RHICmdList) {
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCounts);
		}
	);
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget;
	GraphBuilder.QueueTextureExtraction(RDGRenderTarget, &PooledRenderTarget);
	GraphBuilder.Execute();
	RHICmdList.CopyToResolveTarget(PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture, RenderTargetTexture, FResolveParams());
#else
	FPooledRenderTargetDesc RenderTargetDesc =
		FPooledRenderTargetDesc::Create2DDesc(
			RenderTargetTexture->GetSizeXY(),
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_UAV,
			false);

	static TRefCountPtr<IPooledRenderTarget> ComputeShaderOutput;
	if (!ComputeShaderOutput.IsValid())
	{
		GRenderTargetPool.FindFreeElement(RHICmdList, RenderTargetDesc, ComputeShaderOutput, TEXT("ShaderPlugin_ComputeShaderOutput"));
	}
	RHICmdList.Transition(FRHITransitionInfo(ComputeShaderOutput->GetRenderTargetItem().UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	FCheckerBoardComputeShader::FParameters PassParameters;
	PassParameters.OutputTexture = ComputeShaderOutput->GetRenderTargetItem().UAV;

	TShaderMapRef<FCheckerBoardComputeShader>ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	uint32 GGroupSize = 32;
	uint32 GroupSizeX = FMath::DivideAndRoundUp((uint32)RenderTargetTexture->GetSizeX(), GGroupSize);
	uint32 GroupSizeY = FMath::DivideAndRoundUp((uint32)RenderTargetTexture->GetSizeY(), GGroupSize);
	FIntVector GroupCounts = FIntVector(GroupSizeX, GroupSizeY, 1);
	FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCounts);

	RHICmdList.CopyToResolveTarget(ComputeShaderOutput->GetTargetableRHI(), RenderTargetTexture, FResolveParams());
#endif
}

void UGraphicsToolsBlueprintLibrary::DrawCheckerBoard(const UObject* WorldContextObject, class UTextureRenderTarget2D* OutputRenderTarget)
{
	check(IsInGameThread());

	if (!OutputRenderTarget)
	{
		FMessageLog("Blueprint").Warning(
			LOCTEXT("UGraphicToolsBlueprintLibrary::DrawCheckerBoard",
				"DrawUVDisplacementToRenderTarget: Output render target is required."));
		return;
	}

	FTextureRenderTargetResource* TextureRenderTargetResource = OutputRenderTarget->GameThread_GetRenderTargetResource();
	ERHIFeatureLevel::Type FeatureLevel = WorldContextObject->GetWorld()->Scene->GetFeatureLevel();

	ENQUEUE_RENDER_COMMAND(CaptureCommand)
	(
		[TextureRenderTargetResource, FeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			DrawCheckerBoard_RenderThread(RHICmdList, TextureRenderTargetResource, FeatureLevel);
		}
	);
}

#undef LOCTEXT_NAMESPACE