// Fill out your copyright notice in the Description page of Project Settings.


#include "CombatComponent.h"
#include "Blaster/Weapon/Weapon.h"
#include "Blaster/Character/BlasterCharacter.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Blaster/PlayerController/BlasterPlayerController.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Math/Vector2D.h"

// Sets default values for this component's properties
UCombatComponent::UCombatComponent()
{
	
	PrimaryComponentTick.bCanEverTick = true;

	BaseWalkSpeed = 600.f;
	AimWalkSpeed = 450.f;
	
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	//Replicating this Equippedweapon component because the BP Anim Instance will need it to know to use a certain pose.
	DOREPLIFETIME(UCombatComponent, EquippedWeapon);
	DOREPLIFETIME(UCombatComponent, bAiming);
}

// Called when the game starts
void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
		//GetFollowCamera() is a Getter Function we Created to Return the Camera Component (BlasterCharacter.h)
		if( Character->GetFollowCamera())
		{
			// Returns the Camera Field of View and Stores it in a Variable
			DefaultFOV = Character->GetFollowCamera()->FieldOfView;
			CurrentFOV = DefaultFOV;
		}
	}
	
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if(Character && Character->IsLocallyControlled())
	{
		FHitResult HitResult;
		TraceUnderCrosshairs(HitResult);
		HitTarget = HitResult.ImpactPoint.Size() > 0 ? HitResult.ImpactPoint : HitResult.Location ;
		FString SomeString = HitTarget.ToString();
		
		UE_LOG(LogTemp, Error, TEXT("HitResult.ImpactPoint: %s"), *SomeString);
		UE_LOG(LogTemp, Error, TEXT("HitResult.Location: %s"), *HitResult.Location.ToString());
		UE_LOG(LogTemp, Error, TEXT("HitResult.ImpactPoint.Size: %f"), HitTarget.Size());
		
		SetHUDCrosshairs(DeltaTime);
		InterpFOV(DeltaTime);
	}
}

void UCombatComponent::SetHUDCrosshairs(float DeltaTime)
{
	//needs to set the HUD package in BlasterHUD based on the weapons selected Crosshair Textures
	if(Character == nullptr || Character->Controller == nullptr) return;

	//A Good compact way to set the controller variable.
	Controller = Controller == nullptr ? Cast<ABlasterPlayerController>(Character->Controller): Controller;

	if(Controller)
	{
		HUD = HUD == nullptr ? Cast<ABlasterHUD>(Controller->GetHUD()) : HUD;
		if(HUD)
		{
			
			if(EquippedWeapon)
			{
				
				HUDPackage.CrosshairsCenter = EquippedWeapon->CrosshairsCenter;
				HUDPackage.CrosshairsTop = EquippedWeapon->CrosshairsTop;
				HUDPackage.CrosshairsBottom = EquippedWeapon->CrosshairsBottom;
				HUDPackage.CrosshairsLeft = EquippedWeapon->CrosshairsLeft;
				HUDPackage.CrosshairsRight = EquippedWeapon->CrosshairsRight;
			}
			else
			{
				HUDPackage.CrosshairsCenter = nullptr;
				HUDPackage.CrosshairsTop = nullptr;
				HUDPackage.CrosshairsBottom = nullptr;
				HUDPackage.CrosshairsLeft = nullptr;
				HUDPackage.CrosshairsRight = nullptr;
			}

			//Calculate Crosshair Spread
			//[0.600] -> [0,1]
			FVector2d WalkSpeedRange(0.f, Character->GetCharacterMovement()->MaxWalkSpeed);
			FVector2d VelocityMultiplyerRange(0.f, 1.f);
			FVector Velocity = Character->GetVelocity();
			// We just 0 out the Z Variable
			Velocity.Z = 0;
			CrosshairVelocityFactor = FMath::GetMappedRangeValueClamped(WalkSpeedRange, VelocityMultiplyerRange, Velocity.Size());

			//if character is in the air
			if(Character->GetCharacterMovement()->IsFalling())
			{
				CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 2.25f, DeltaTime, 2.25f);
			}
			else
			{
				CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 0.f, DeltaTime, 30.f);
			}

			if(bAiming)
			{
				CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor, 0.58f, DeltaTime, 30.f );
			}
			else
			{
				CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor, 0.f, DeltaTime, 30.f );
			}
			
			CrosshairShootingFactor = FMath::FInterpTo(CrosshairShootingFactor, 0.f, DeltaTime, 20.f);
			
			HUDPackage.CrosshairSpread =
				(0.5f +
				CrosshairVelocityFactor +
					CrosshairInAirFactor -
						CrosshairAimFactor +
							CrosshairShootingFactor) *
								CrosshairEnemyFactor;
			
			HUD->SetHUDPackage(HUDPackage);
		}
		
	}
}

void UCombatComponent::InterpFOV(float DeltaTime)
{
	if(EquippedWeapon == nullptr) return;
	//Setting FOV Variable based on whether or not we are aiming.
	if(bAiming)
	{
		CurrentFOV = FMath::FInterpTo(CurrentFOV, EquippedWeapon->GetZoomedFOV(), DeltaTime, EquippedWeapon->GetZoomedInterpSpeed());
	}
	else
	{
		CurrentFOV = FMath::FInterpTo(CurrentFOV, DefaultFOV, DeltaTime, ZoomInterpSpeed);
	}

	//Setting the Camera FOV to our Stored FOV Value each frame.
	if(Character && Character->GetFollowCamera())
	{
		Character->GetFollowCamera()->SetFieldOfView(CurrentFOV);
	}
}

void UCombatComponent::SetAiming(bool bIsAiming)
{
	//this function handles setting aiming to true or false and also has a callback to the RPC to push that information to the server and then to all clients.
	//if we dont set the variable here first, the client would have to wait for the callback (RPC) to go to the server and then replicate back down to the client.
	//for cosmetic actions like animations, this is ok.
	bAiming = bIsAiming;

	//if character is not the server, does not have authority, run the RPC to pass the information to the server and replicate the action
	ServerSetAiming(bIsAiming);

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = bIsAiming ? AimWalkSpeed : BaseWalkSpeed;

	}


}

void UCombatComponent::ServerSetAiming_Implementation(bool bIsAiming)
{
	bAiming = bIsAiming;

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = bIsAiming ? AimWalkSpeed : BaseWalkSpeed;

	}
}

void UCombatComponent::ServerFire_Implementation(const FVector_NetQuantize& TraceHitTarget)
{
	//This is for clients to pass the fire RPC to the server. And if this is called on the server it only invokes on the server.
	//This is the functionality of a Server RPC
	MulticastFire(TraceHitTarget);
}

void UCombatComponent::MulticastFire_Implementation(const FVector_NetQuantize& TraceHitTarget)
{
	// Using Mutlicast RPC from the server, replicated the calls on all of the clients.
	if(EquippedWeapon == nullptr) return;
	if(Character)
	{
		Character->PlayFireMontage(bAiming);
		EquippedWeapon->Fire(TraceHitTarget);
	}
}

void UCombatComponent::StartFireTimer()
{
	if(EquippedWeapon == nullptr || Character == nullptr) return;
	Character->GetWorldTimerManager().SetTimer(FireTimer,
		this,
		&UCombatComponent::FireTimerFinished,
		EquippedWeapon->FireDelay);
}

void UCombatComponent::FireTimerFinished()
{
	if(EquippedWeapon == nullptr || Character == nullptr) return;
	bCanFire = true;
	if(bFireButtonPressed && EquippedWeapon->bAutomatic)
	{
		Fire();
	}
}

void UCombatComponent::Fire()
{
	if(bCanFire && EquippedWeapon)
	{
		bCanFire = false;
		CrosshairShootingFactor = 0.2f;
	
		ServerFire(HitTarget);
		StartFireTimer();
	}
	
}

void UCombatComponent::FireButtonPressed(bool bPressed)
{
	//Called locally on the server or client
	bFireButtonPressed = bPressed;
	Fire();
}

void UCombatComponent::TraceUnderCrosshairs(FHitResult& TraceHitResult)
{
	// We get the viewport size in order to find the center of the viewport or the screen
	// that the player is using.
	FVector2d ViewportSize;
	//Returns X & Y Components for the size of our viewport.
	// We are storing it in our local variable ViewportSize
	if(GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	
	FVector2d CrosshairLocation(ViewportSize.X/2.f, ViewportSize.Y/2.f);
	FVector CrosshairWorldPosition;
	FVector CrosshairWorldDirection;

	//returns a bool is projecion is successful
	bool bScreenToWorld = UGameplayStatics::DeprojectScreenToWorld(
		UGameplayStatics::GetPlayerController(this, 0),
		CrosshairLocation,
		CrosshairWorldPosition,
		CrosshairWorldDirection
	);

	if(bScreenToWorld)
	{
		//the 3D world postision corresponding to the location at the center of our screen
		FVector Start = CrosshairWorldPosition;

		//Moving Start of Trace to in front of Character
		if(Character)
		{
			float DistanceToCharacter = (Character->GetActorLocation() - Start).Size();
			Start += CrosshairWorldDirection * (DistanceToCharacter + 20.f);
			
		}
		
		// The Start location pushed out in the World Direction
		FVector End = Start + CrosshairWorldDirection * TRACE_LENGTH;
		UE_LOG(LogTemp, Warning, TEXT("End of Vector: %s"), *End.ToString());

		
		//Traces straight out and logs hit result based on visibility channel
		GetWorld()->LineTraceSingleByChannel(TraceHitResult, Start, End, ECollisionChannel::ECC_Visibility);

		
		//Set Crosshair color to read when on Enemy or White as Default and CrosshairSpread
		if(TraceHitResult.GetActor() && TraceHitResult.GetActor()->Implements<UInteractWithCrosshairsInterface>())
		{
			HUDPackage.CrosshairColor = FLinearColor::Red;
			bAiming ? CrosshairEnemyFactor = 0.085f : CrosshairEnemyFactor = 0.1f;
		}
		else
		{
			HUDPackage.CrosshairColor = FLinearColor::White;
			CrosshairEnemyFactor = 1.f;
		}
	}
} 

void UCombatComponent::EquipWeapon(AWeapon* WeaponToEquip)
{
	if (Character == nullptr || WeaponToEquip == nullptr) return;

	//Equipping weapon.
	EquippedWeapon = WeaponToEquip;
	EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);
	
	//attach to character hand socket
	const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(FName("RightHandSocket"));
	if (HandSocket)
	{
		HandSocket->AttachActor(EquippedWeapon, Character->GetMesh());
	}

	//set weapons owner to the character
	EquippedWeapon->SetOwner(Character);

	Character->GetCharacterMovement()->bOrientRotationToMovement = false;
	Character->bUseControllerRotationYaw = true;
}

void UCombatComponent::OnRep_EquippedWeapon()
{
	if (EquippedWeapon && Character)
	{
		Character->GetCharacterMovement()->bOrientRotationToMovement = false;
		Character->bUseControllerRotationYaw = true;
	}
}
