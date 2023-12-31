// Copyright Epic Games, Inc. All Rights Reserved.

#include "Wallrun_C_plusplusCharacter.h"
#include "Wallrun_C_plusplusProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "XRMotionControllerBase.h" // for FXRMotionControllerBase::RightHandSourceId

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AWallrun_C_plusplusCharacter

AWallrun_C_plusplusCharacter::AWallrun_C_plusplusCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(false);			// otherwise won't be visible in the multiplayer
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	// Note: The ProjectileClass and the skeletal mesh/anim blueprints for Mesh1P, FP_Gun, and VR_Gun 
	// are set in the derived blueprint asset named MyCharacter to avoid direct content references in C++.

}

void AWallrun_C_plusplusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bIsWallRunning)
	{
		UpdateWallRun();
	}
	CameraTiltTimeline.TickTimeline(DeltaSeconds);

}

void AWallrun_C_plusplusCharacter::Jump()
{
	if (bIsWallRunning)
	{
		FVector JumpDirection = FVector::ZeroVector;
		if (CurrentWallRunSide == EWallRunSide::Right)
		{
			JumpDirection = FVector::CrossProduct(CurrentWallRunDirection, FVector::UpVector).GetSafeNormal();
		}
		else
		{
			JumpDirection = FVector::CrossProduct(FVector::UpVector,CurrentWallRunDirection).GetSafeNormal();
		}

		JumpDirection += FVector::UpVector;

		LaunchCharacter(GetCharacterMovement()->JumpZVelocity * JumpDirection.GetSafeNormal(), false, true);

		StopWallRun();

	}
	else
	{
		Super::Jump();
	}
}

void AWallrun_C_plusplusCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	GetCapsuleComponent()->OnComponentHit.AddDynamic(this, &AWallrun_C_plusplusCharacter::OnPlayerCapsuleHit);
	GetCharacterMovement()->SetPlaneConstraintEnabled(true);

	if (IsValid(CameraTiltCurve))
	{
		FOnTimelineFloat TimeLineCallBack;
		TimeLineCallBack.BindUFunction(this, FName("UpdateCameraTilt"));
		CameraTiltTimeline.AddInterpFloat(CameraTiltCurve, TimeLineCallBack);


	}

}

//////////////////////////////////////////////////////////////////////////
// Input

void AWallrun_C_plusplusCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AWallrun_C_plusplusCharacter::OnFire);



	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AWallrun_C_plusplusCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AWallrun_C_plusplusCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AWallrun_C_plusplusCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AWallrun_C_plusplusCharacter::LookUpAtRate);
}

void AWallrun_C_plusplusCharacter::OnPlayerCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{

	FVector HitNormal = Hit.ImpactNormal;

	if (bIsWallRunning)
	{
		return;
	}

	if (!IsSurfaceWallRunable(HitNormal))
	{
		return;
	}

	if (!GetCharacterMovement()->IsFalling())
	{
		return;
	}

	EWallRunSide Side = EWallRunSide::None;
	FVector Direction = FVector::ZeroVector;
	GetWallRunSideAndDirection(HitNormal, Side, Direction);

	if (!AreRequiredKeysDown(Side))
	{
		return;
	}

	StrartWallRun(Side, Direction);
}

void AWallrun_C_plusplusCharacter::GetWallRunSideAndDirection(const FVector& HitNormal, EWallRunSide& OutSide, FVector& OutDirection)const
{
	if (FVector::DotProduct(HitNormal, GetActorRightVector()) > 0)
	{
		OutSide = EWallRunSide::Left;
		OutDirection = FVector::CrossProduct(HitNormal, FVector::UpVector).GetSafeNormal();
	}
	else
	{
		OutSide = EWallRunSide::Right;
		OutDirection = FVector::CrossProduct(FVector::UpVector, HitNormal).GetSafeNormal();
	}
}


bool AWallrun_C_plusplusCharacter::IsSurfaceWallRunable(const FVector& SurfaceNormal)const
{
	if (SurfaceNormal.Z > GetCharacterMovement()->GetWalkableFloorZ() || SurfaceNormal.Z < -0.005f)
	{
		return false;
	}

	return true;
}

bool AWallrun_C_plusplusCharacter::AreRequiredKeysDown(EWallRunSide Side)const
{
	if(ForwardAxis < 0.1f)
	{
		return false;
	}
	if (Side == EWallRunSide::Right && RightAxis < -0.1f)
	{
		return false;
	}

	if (Side == EWallRunSide::Left && RightAxis > 0.1f)
	{
		return false;
	}
	else
	{
		return true;
	}
}

void AWallrun_C_plusplusCharacter::StrartWallRun(EWallRunSide Side, const FVector& Direction)
{

	GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Green, TEXT("WallRun Started!"));
	BeginCameraTilt();

	bIsWallRunning = true;

	CurrentWallRunSide = Side;
	CurrentWallRunDirection = Direction;

	GetCharacterMovement()->SetPlaneConstraintNormal(FVector::UpVector);

	GetWorld()->GetTimerManager().SetTimer(WallRunTimer, this, &AWallrun_C_plusplusCharacter::StopWallRun, MaxWallRunTime, false);

}

void AWallrun_C_plusplusCharacter::StopWallRun()
{
	GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Red, TEXT("WallRun Ended!"));

	EndCameraTilt();

	bIsWallRunning = false;



	GetCharacterMovement()->SetPlaneConstraintNormal(FVector::ZeroVector);

}

void AWallrun_C_plusplusCharacter::UpdateWallRun()
{
	if (!AreRequiredKeysDown(CurrentWallRunSide))
	{
		StopWallRun();
		return;
	}

	FHitResult HitResult;

	FVector LineTraceDirection = CurrentWallRunSide == EWallRunSide::Right ? GetActorRightVector() : -GetActorRightVector();

	float LineTraceLength = 200.0f;
	FVector StartPosition = GetActorLocation();

	FVector EndPosition = StartPosition + LineTraceLength * LineTraceDirection;
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(HitResult, StartPosition, EndPosition, ECC_Visibility, QueryParams))
	{ 
		FVector HitNormal = HitResult.ImpactNormal; 
		EWallRunSide Side = EWallRunSide::None;
		FVector Direction = FVector::ZeroVector;
		GetWallRunSideAndDirection(HitResult.ImpactNormal, Side, Direction);

		if (Side != CurrentWallRunSide)
		{
			StopWallRun();
		}
		else
		{
			CurrentWallRunDirection = Direction;
			GetCharacterMovement()->Velocity = GetCharacterMovement()->GetMaxSpeed() * CurrentWallRunDirection;
		}
	}
	else
	{
		StopWallRun();
	}


}

void AWallrun_C_plusplusCharacter::UpdateCameraTilt(float Value)
{
	FRotator CurrentControlRotation = GetControlRotation();
	CurrentControlRotation.Roll = CurrentWallRunSide == EWallRunSide::Left ? Value : -Value;
	GetController()->SetControlRotation(CurrentControlRotation);

}

void AWallrun_C_plusplusCharacter::OnFire()
{
	// try and fire a projectile
	if (ProjectileClass != nullptr)
	{
		UWorld* const World = GetWorld();
		if (World != nullptr)
		{
		
				const FRotator SpawnRotation = GetControlRotation();
				// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
				const FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);

				//Set Spawn Collision Handling Override
				FActorSpawnParameters ActorSpawnParams;
				ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

				// spawn the projectile at the muzzle
				World->SpawnActor<AWallrun_C_plusplusProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
			
		}
	}

	// try and play the sound if specified
	if (FireSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	// try and play a firing animation if specified
	if (FireAnimation != nullptr)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
		if (AnimInstance != nullptr)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}
}


//Commenting this section out to be consistent with FPS BP template.
//This allows the user to turn without using the right virtual joystick

//void AWallrun_C_plusplusCharacter::TouchUpdate(const ETouchIndex::Type FingerIndex, const FVector Location)
//{
//	if ((TouchItem.bIsPressed == true) && (TouchItem.FingerIndex == FingerIndex))
//	{
//		if (TouchItem.bIsPressed)
//		{
//			if (GetWorld() != nullptr)
//			{
//				UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
//				if (ViewportClient != nullptr)
//				{
//					FVector MoveDelta = Location - TouchItem.Location;
//					FVector2D ScreenSize;
//					ViewportClient->GetViewportSize(ScreenSize);
//					FVector2D ScaledDelta = FVector2D(MoveDelta.X, MoveDelta.Y) / ScreenSize;
//					if (FMath::Abs(ScaledDelta.X) >= 4.0 / ScreenSize.X)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.X * BaseTurnRate;
//						AddControllerYawInput(Value);
//					}
//					if (FMath::Abs(ScaledDelta.Y) >= 4.0 / ScreenSize.Y)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.Y * BaseTurnRate;
//						AddControllerPitchInput(Value);
//					}
//					TouchItem.Location = Location;
//				}
//				TouchItem.Location = Location;
//			}
//		}
//	}
//}

void AWallrun_C_plusplusCharacter::MoveForward(float Value)
{
	ForwardAxis = Value;
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AWallrun_C_plusplusCharacter::MoveRight(float Value)
{
	RightAxis = Value;
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AWallrun_C_plusplusCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AWallrun_C_plusplusCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

