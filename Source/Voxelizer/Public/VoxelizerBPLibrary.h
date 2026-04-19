// Copyright Epic, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VoxelizerBPLibrary.generated.h"

/*
*	Function library class.
*	Each function in it is expected to be static and represents blueprint node that can be called in any blueprint.
*
*	When declaring function you can define metadata for the node. Key function specifiers will be BlueprintPure and BlueprintCallable.
*	BlueprintPure - means the function does not affect the owning object in any way and thus creates a node without Exec pins.
*	BlueprintCallable - makes a function which can be executed in Blueprints - Thus it has Exec pins.
*	DisplayName - full name of the node, shown when you mouse over the node and in the blueprint drop down menu.
*				Its lets you name the node using characters not allowed in C++ function names.
*	CompactNodeTitle - the word(s) that appear on the node.
*	Keywords -	the list of keywords that helps you to find the node when you search for it using the Blueprint drop down menu.
*				Good example is "Print String" node which you can find also by using keyword "log".
*	Category -	the category your node will be under in the Blueprint drop down menu.
*/

// Struct for returning voxelization results in Blueprints
USTRUCT(BlueprintType)
struct FVoxelGridData
{
    GENERATED_BODY()

    // Number of voxels along X, Y, Z axes
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
    FIntVector GridDimensions;

    // World-space minimum bounds of the voxel grid (origin point)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
    FVector WorldMinBounds;

    // Flattened 1D voxel array (true = occupied)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
    TArray<bool> OccupancyArray;

    // Voxel resolution (units: cm)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
    float VoxelSize;
};

UCLASS()
class UVoxelizerBPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_UCLASS_BODY()

    /**
     * Voxelize the level within the specified bounds
     * @param WorldContextObject World context object
     * @param BoundaryActor Actor used to define the bounds (if empty, will auto-search for VoxelBox_StaticMesh)
     * @param VoxelSizeInMeters Voxel resolution (units: meters, e.g. 0.5)
     * @param OutVoxelData Output voxel data
     * @return True if voxelization succeeded
     */
public:
    UFUNCTION(BlueprintCallable, Category = "Voxelizer", meta = (WorldContext = "WorldContextObject"))
    static bool VoxelizeLevelInBounds(UObject* WorldContextObject, AActor* BoundaryActor, float VoxelSizeInMeters, FVoxelGridData& OutVoxelData);
public:
    UFUNCTION(BlueprintCallable, Category = "Voxelizer")
    static void DrawVoxelDataFromFile(
        const FString& FilePath,
        float VoxelSizeInMeters,
        bool bDrawFreeVoxels = false
    );

private:
    // ==============================================
    // Save voxel data files (refactored)
    // Output: Full voxels (0+1) + Occupied voxels only (1)
    // ==============================================
    static void SaveVoxelDataFiles(
        const TArray<bool>& OccupancyArray,
        int32 SizeX, int32 SizeY, int32 SizeZ,
        const FVector& MinBounds,
        float VoxelSize
    );
};