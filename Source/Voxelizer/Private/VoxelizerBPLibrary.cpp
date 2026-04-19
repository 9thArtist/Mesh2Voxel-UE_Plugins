// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelizerBPLibrary.h"
#include "Voxelizer.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Async/ParallelFor.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#include "DrawDebugHelpers.h"

UVoxelizerBPLibrary::UVoxelizerBPLibrary(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{

}

bool UVoxelizerBPLibrary::VoxelizeLevelInBounds(UObject* WorldContextObject, AActor* BoundaryActor, float VoxelSizeInMeters, FVoxelGridData& OutVoxelData)
{
    // 1. Get world context
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (!World) return false;

    // 2. Unit conversion: meters → UE units (centimeters)
    const float VoxelSize = VoxelSizeInMeters * 100.0f;
    if (VoxelSize <= 0.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("Voxelizer: Voxel size must be greater than 0!"));
        return false;
    }

    // 3. Auto find VoxelBox_StaticMesh in the level
    if (!BoundaryActor)
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel().Contains(TEXT("VoxelBox_StaticMesh")))
            {
                BoundaryActor = *It;
                break;
            }
        }
    }

    if (!BoundaryActor)
    {
        UE_LOG(LogTemp, Error, TEXT("Voxelizer: Can't find Boundary Actor (VoxelBox_StaticMesh)!"));
        return false;
    }

    // 4. Get bounding box from VoxelBox_StaticMesh (only for defining voxel space bounds)
    const FBox BoundingBox = BoundaryActor->GetComponentsBoundingBox(true);
    const FVector Min = BoundingBox.Min;
    const FVector Max = BoundingBox.Max;

    // 5. Calculate voxel grid dimensions (match bounds + voxel size)
    const int32 SizeX = FMath::CeilToInt((Max.X - Min.X) / VoxelSize);
    const int32 SizeY = FMath::CeilToInt((Max.Y - Min.Y) / VoxelSize);
    const int32 SizeZ = FMath::CeilToInt((Max.Z - Min.Z) / VoxelSize);
    const int64 TotalVoxels = (int64)SizeX * SizeY * SizeZ;

    // Safety limit: prevent memory overflow
    //if (TotalVoxels > 64000000)
    //{
    //    UE_LOG(LogTemp, Error, TEXT("Voxelizer: Voxel count too big (%lld). Reduce boundary or increase voxel size!"), TotalVoxels);
    //    return false;
    //}

    // 6. Initialize voxel array: [all default to 0 (free)]
    TArray<bool> VoxelOccupancy;
    VoxelOccupancy.Init(false, TotalVoxels);

    // 7. Collision query setup: ignore VoxelBox itself, only detect other objects
    FCollisionShape VoxelShape = FCollisionShape::MakeBox(FVector(VoxelSize * 0.5f));
    FCollisionQueryParams QueryParams(SCENE_QUAT_STAT(VoxelizeLevel), false);
    QueryParams.AddIgnoredActor(BoundaryActor); // Core: exclude VoxelBox itself
    //const ECollisionChannel TraceChannel = ECC_Visibility;
    const ECollisionChannel TraceChannel = ECC_WorldStatic;

    // 8. Multithreaded voxel detection: mark as 1 if occupied (overwrite default 0)
    ParallelFor(TotalVoxels, [&](int64 Index)
        {
            // Convert 1D index to 3D grid coordinates
            const int32 Z = Index / (SizeX * SizeY);
            const int32 Y = (Index / SizeX) % SizeY;
            const int32 X = Index % SizeX;

            // Calculate voxel center world position
            const FVector VoxelCenter = Min + FVector(
                X * VoxelSize + VoxelSize * 0.5f,
                Y * VoxelSize + VoxelSize * 0.5f,
                Z * VoxelSize + VoxelSize * 0.5f
            );

            // Check: if current voxel is occupied by any object
            const bool bIsOccupied = World->OverlapAnyTestByChannel(
                VoxelCenter,
                FQuat::Identity,
                TraceChannel,
                VoxelShape,
                QueryParams
            );

            // Assign: 1 if occupied, else keep default 0
            VoxelOccupancy[Index] = bIsOccupied;
        });

    // 9. Initialize blueprint output data
    OutVoxelData.GridDimensions = FIntVector(SizeX, SizeY, SizeZ);
    OutVoxelData.WorldMinBounds = Min;
    OutVoxelData.VoxelSize = VoxelSize;
    OutVoxelData.OccupancyArray = VoxelOccupancy;

    // 10. Save dual files
    SaveVoxelDataFiles(VoxelOccupancy, SizeX, SizeY, SizeZ, Min, VoxelSize);

    UE_LOG(LogTemp, Warning, TEXT("Voxelizer: Success! Total Voxels: %lld"), TotalVoxels);
    return true;
}

// Save function: output two standard format files
void UVoxelizerBPLibrary::SaveVoxelDataFiles(
    const TArray<bool>& OccupancyArray,
    int32 SizeX, int32 SizeY, int32 SizeZ,
    const FVector& MinBounds,
    float VoxelSize)
{
    // Create save directory
    const FString SaveDir = FPaths::ProjectSavedDir() / TEXT("Voxelizer");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*SaveDir))
    {
        PlatformFile.CreateDirectory(*SaveDir);
    }

    // ==============================================
    // File 1: FullVoxelData.txt → output all voxels (0 + 1)
    // ==============================================
    const FString FullFilePath = SaveDir / TEXT("FullVoxelData.txt");
    FString FullContent;
    FullContent += FString::Printf(TEXT("// Full Voxel Data (All voxels)\n"));
    FullContent += FString::Printf(TEXT("// Format: X Y Z F | F=0(Free), F=1(Occupied)\n"));
    FullContent += FString::Printf(TEXT("// Voxel Size: %.2f cm | Grid: X=%d Y=%d Z=%d\n\n"), VoxelSize, SizeX, SizeY, SizeZ);

    // ==============================================
    // File 2: OnlyOccupied.txt → only output occupied voxels (Only 1)
    // ==============================================
    const FString OccupiedFilePath = SaveDir / TEXT("OnlyOccupied.txt");
    FString OccupiedContent;
    OccupiedContent += FString::Printf(TEXT("// Only Occupied Voxels\n"));
    OccupiedContent += FString::Printf(TEXT("// Format: X Y Z F | F=1(Occupied)\n"));
    OccupiedContent += FString::Printf(TEXT("// Voxel Size: %.2f cm | Grid: X=%d Y=%d Z=%d\n\n"), VoxelSize, SizeX, SizeY, SizeZ);

    // Iterate all voxels and write to files
    const int64 TotalVoxels = (int64)SizeX * SizeY * SizeZ;
    for (int64 Index = 0; Index < TotalVoxels; Index++)
    {
        // Calculate coordinates
        const int32 Z = Index / (SizeX * SizeY);
        const int32 Y = (Index / SizeX) % SizeY;
        const int32 X = Index % SizeX;

        const FVector WorldCenter = MinBounds + FVector(
            X * VoxelSize + VoxelSize * 0.5f,
            Y * VoxelSize + VoxelSize * 0.5f,
            Z * VoxelSize + VoxelSize * 0.5f
        );

        // State value: 0=free, 1=occupied
        const int32 State = OccupancyArray[Index] ? 1 : 0;
        const FString Line = FString::Printf(TEXT("%.2f %.2f %.2f %d\n"), WorldCenter.X, WorldCenter.Y, WorldCenter.Z, State);

        // Write to full data file (write all voxels)
        FullContent += Line;

        // Only write to occupied data file (only 1)
        if (State == 1)
        {
            OccupiedContent += Line;
        }
    }

    // Save files to local disk
    FFileHelper::SaveStringToFile(FullContent, *FullFilePath);
    FFileHelper::SaveStringToFile(OccupiedContent, *OccupiedFilePath);

    UE_LOG(LogTemp, Log, TEXT("Voxelizer: Files saved to: %s"), *SaveDir);
}

// Visualize voxel representation from grid conversion using wireframe
void UVoxelizerBPLibrary::DrawVoxelDataFromFile(
    const FString& FilePath,
    float VoxelSizeInMeters,
    bool bDrawFreeVoxels)
{
    // Convert meters to UE centimeters
    const float VoxelSize = VoxelSizeInMeters * 100.0f;
    const float HalfSize = VoxelSize * 0.5f;

    // Read all lines from file
    TArray<FString> FileLines;
    if (!FFileHelper::LoadFileToStringArray(FileLines, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("DrawVoxel: Failed to open file %s"), *FilePath);
        return;
    }

    // Iterate each line
    for (const FString& Line : FileLines)
    {
        FString TrimmedLine = Line.TrimStartAndEnd();

        // Skip comment lines and empty lines
        if (TrimmedLine.IsEmpty() || TrimmedLine.StartsWith(TEXT("//")))
            continue;

        // Split X Y Z F
        TArray<FString> Parts;
        TrimmedLine.ParseIntoArray(Parts, TEXT(" "), true);

        if (Parts.Num() < 4)
            continue;

        // Parse values
        float X, Y, Z;
        int32 F;

        // Parse values (correct method: single parameter, direct assignment)
        X = FCString::Atof(*Parts[0]);
        Y = FCString::Atof(*Parts[1]);
        Z = FCString::Atof(*Parts[2]);
        F = FCString::Atoi(*Parts[3]);

        // Skip invalid lines that failed parsing (skip when default 0 to avoid dirty data)
        if (X == 0.0f && Y == 0.0f && Z == 0.0f && F == 0 && Parts[0] != TEXT("0"))
        {
            continue;
        }

        FVector Center(X, Y, Z);

        // Occupied = red solid box
        if (F == 1)
        {
            DrawDebugBox(
                GWorld,
                Center,
                FVector(HalfSize),
                FQuat::Identity,
                FColor::Red,
                true,            // Persistent
                -1,              // Infinite duration
                0,
                1.0f             // Line width
            );
        }
        // Free = green wireframe (toggleable)
        else if (F == 0 && bDrawFreeVoxels)
        {
            DrawDebugBox(
                GWorld,
                Center,
                FVector(HalfSize),
                FQuat::Identity,
                FColor::Green,
                true,
                -1,
                0,
                0.5f
            );
        }
    }

    UE_LOG(LogTemp, Log, TEXT("DrawVoxel: Finished drawing voxels from file %s"), *FilePath);
}