// Copyright Epic Games / Modular Combat System. All Rights Reserved.

#include "AnimNotifies/AnimNotify_ComboTransition.h"
#include "Components/SkeletalMeshComponent.h"
#include "CombatComponent.h"

void UAnimNotify_ComboTransition::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	if (!MeshComp || !MeshComp->GetOwner()) return;

	if (UCombatComponent* CombatComp = MeshComp->GetOwner()->FindComponentByClass<UCombatComponent>())
	{
		CombatComp->NotifyComboTransitionPoint(Animation);
	}
}
