/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003-2025 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "TurnBasedCombatManager.h"

#include "ie_stats.h"

#include "DisplayMessage.h"
#include "Game.h"
#include "Interface.h"
#include "Map.h"

#include "GUI/GameControl.h"
#include "Scriptable/Actor.h"

#include <algorithm>

namespace GemRB {

// =============================================================================
// Slot Access Methods
// =============================================================================

InitiativeSlot* TurnBasedCombatManager::GetCurrentTurnBasedSlot()
{
	if (currentTurnBasedList < 0 || currentTurnBasedList >= MAX_ATTACK_LISTS) {
		return nullptr;
	}

	if (initiatives[currentTurnBasedList].empty()) {
		return nullptr;
	}

	size_t slot = static_cast<size_t>(currentTurnBasedSlot);
	if (slot >= initiatives[currentTurnBasedList].size()) {
		slot = initiatives[currentTurnBasedList].size() - 1;
	}

	return &initiatives[currentTurnBasedList][slot];
}

const InitiativeSlot* TurnBasedCombatManager::GetCurrentTurnBasedSlot() const
{
	if (currentTurnBasedList < 0 || currentTurnBasedList >= MAX_ATTACK_LISTS) {
		return nullptr;
	}

	if (initiatives[currentTurnBasedList].empty()) {
		return nullptr;
	}

	size_t slot = static_cast<size_t>(currentTurnBasedSlot);
	if (slot >= initiatives[currentTurnBasedList].size()) {
		slot = initiatives[currentTurnBasedList].size() - 1;
	}

	return &initiatives[currentTurnBasedList][slot];
}

InitiativeSlot* TurnBasedCombatManager::GetTurnBasedSlot(Actor* actor)
{
	if (!actor->InInitiativeList()) {
		actor->MoveToInitiativeList();
	}

	for (int list = 0; list < MAX_ATTACK_LISTS; list++) {
		for (auto& slot : initiatives[list]) {
			if (slot.actor == actor) {
				return &slot;
			}
		}
	}

	return nullptr;
}

InitiativeSlot* TurnBasedCombatManager::GetTurnBasedSlotWithAttack(Actor* actor)
{
	if (actor->AuraCooldown) {
		return nullptr;
	}
	for (int list = 0; list < MAX_ATTACK_LISTS; list++) {
		for (auto& slot : initiatives[list]) {
			if (slot.actor == actor && slot.haveaction) {
				return &slot;
			}
		}
	}
	return nullptr;
}

// =============================================================================
// Turn Management
// =============================================================================

void TurnBasedCombatManager::InitTurnBasedSlot()
{
	InitiativeSlot* slot = GetCurrentTurnBasedSlot();
	currentTurnBasedActor = slot ? slot->actor : nullptr;
	lastTurnBasedTarget = 0;

	if (slot && !slot->delayaction) {
		if (currentTurnBasedList == 0) {
			slot->movesleft = 1.0f;
			slot->havefreeaction = true;
		} else {
			for (size_t idx = 0; idx < initiatives[currentTurnBasedList - 1].size(); idx++) {
				if (initiatives[currentTurnBasedList - 1][idx].actor == currentTurnBasedActor) {
					slot->movesleft = initiatives[currentTurnBasedList - 1][idx].movesleft;
					break;
				}
			}
		}
	}

	if (currentTurnBasedActor->IsPC() && currentTurnBasedActor->GetStance() != IE_ANI_CAST) {
		currentTurnBasedActor->ClearPath(true);
		currentTurnBasedActor->ClearActions();
	}
	currentTurnBasedActor->lastInit = core->GetGame()->GetGameTimeReal();

	GameControl* gc = core->GetGameControl();
	if (currentTurnBasedActor->GetCurrentArea()->IsVisible(currentTurnBasedActor->Pos)) {
		gc->MoveViewportTo(currentTurnBasedActor->Pos, true);
	}
}

void TurnBasedCombatManager::FirstRoundStart()
{
	currentTurnBasedSlot = 0;
	currentTurnBasedList = 0;
	opportunity = 0;
	roundTurnBased++;

	if (roundTurnBased == 1) {
		timeTurnBased = core->GetGame()->GetGameTimeReal();
	} else {
		for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
			initiatives[0][idx].initiative = initiatives[0][idx].actor->CalculateInitiative();
		}
	}

	std::sort(initiatives[0].begin(), initiatives[0].end(), [](const InitiativeSlot& a, const InitiativeSlot& b) {
		return a.initiative < b.initiative;
	});

	for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
		initiatives[0][idx].actor->InitRound(timeTurnBased);
		initiatives[0][idx].haveaction = true;
		initiatives[0][idx].havefreeaction = true;
		initiatives[0][idx].delayaction = false;
		initiatives[0][idx].movesleft = 1.0f;
		// Stop all movement at combat start
		if (roundTurnBased == 1) {
			initiatives[0][idx].actor->ClearPath(true);
			initiatives[0][idx].actor->ClearActions();
		}
	}

	for (size_t attacks = 1; attacks < MAX_ATTACK_LISTS; attacks++) {
		initiatives[attacks].clear();
		for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
			if (initiatives[0][idx].actor->attackcount >= attacks + 1) {
				initiatives[attacks].push_back(initiatives[0][idx]);
				initiatives[attacks].back().haveaction = true;
				initiatives[attacks].back().havefreeaction = false;
				initiatives[attacks].back().delayaction = false;
			}
		}
	}

	InitTurnBasedSlot();

	String rollLog = fmt::format(u">>> ROUND: {} <<<", roundTurnBased);
	displaymsg->DisplayString(std::move(rollLog), GUIColors::GOLD, 0);

	core->GetGame()->SelectActor(nullptr, false, SELECT_REPLACE);
	if (initiatives[0][0].actor->IsPartyMember()) {
		core->GetGame()->SelectActor(initiatives[0][0].actor, true, SELECT_REPLACE);
	}
}

void TurnBasedCombatManager::EndTurn()
{
	if (!currentTurnBasedActor ||
	    core->GetGame()->GetCurrentAction() ||
	    currentTurnBasedActor->InAttack()) {
		return;
	}

	if (currentTurnBasedActorOld) {
		if (currentTurnBasedActor->IsPC()) {
			core->GetGame()->SelectActor(currentTurnBasedActor, false, SELECT_REPLACE);
		}

		currentTurnBasedActor = currentTurnBasedActorOld;
		currentTurnBasedList = currentTurnBasedListOld;
		currentTurnBasedSlot = currentTurnBasedSlotOld;
		currentTurnBasedActorOld = nullptr;
		currentTurnBasedListOld = 0;
		currentTurnBasedSlotOld = 0;
		currentTurnBasedActor->lastInit = core->GetGame()->GetGameTimeReal();

		if (currentTurnBasedActor->IsPC()) {
			core->GetGame()->SelectActor(currentTurnBasedActor, true, SELECT_REPLACE);
		}

		String rollLog = fmt::format(u">>> OPPORTUNITY FINISH <<<");
		displaymsg->DisplayString(std::move(rollLog), GUIColors::GOLD, 0);
		return;
	}

	Actor* actor = currentTurnBasedActor;

	if (actor->InInitiativeList()) {
		currentTurnBasedSlot = 0;
		for (size_t idx = 0; idx < initiatives[currentTurnBasedList].size(); idx++) {
			if (initiatives[currentTurnBasedList][idx].actor == currentTurnBasedActor) {
				currentTurnBasedSlot = idx;
				break;
			}
		}

		// delayed attack
		if (actor->GetStance() != IE_ANI_DIE && actor->GetStance() != IE_ANI_TWITCH && actor->GetStance() != IE_ANI_SLEEP &&
		    !(actor->Immobile() || (actor->Modified[IE_STATE_ID] & (STATE_CANTMOVE | STATE_PANIC))) && HasMainAction() &&
		    currentTurnBasedSlot < initiatives[currentTurnBasedList].size() - 1) {
			InitiativeSlot* slot = GetCurrentTurnBasedSlot();
			if (slot && !slot->delayaction) {
				InitiativeSlot delayedSlot = *slot;
				delayedSlot.delayaction = true;
				initiatives[currentTurnBasedList].erase(initiatives[currentTurnBasedList].begin() + currentTurnBasedSlot);
				initiatives[currentTurnBasedList].push_back(delayedSlot);
			}
		} else {
			currentTurnBasedSlot++;
		}
	}

	if (currentTurnBasedSlot >= initiatives[currentTurnBasedList].size()) {
		currentTurnBasedSlot = 0;

		while (currentTurnBasedList < MAX_ATTACK_LISTS) {
			currentTurnBasedList++;

			if (!timeTurnBasedNeed) {
				timeTurnBasedNeed = timeTurnBased;
			}

			timeTurnBasedNeed += (core->Time.defaultTicksPerSec * core->Time.round_sec) / 10;
			if (currentTurnBasedList == MAX_ATTACK_LISTS) {
				timeTurnBasedNeed += (core->Time.defaultTicksPerSec * core->Time.round_sec) % 10;
			}

			for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
				Actor* act = initiatives[0][idx].actor;
				// AuraCooldown
				if (act->AuraCooldown) {
					act->AuraCooldown -= (core->Time.defaultTicksPerSec * core->Time.round_sec) / 10;
					if (currentTurnBasedList == MAX_ATTACK_LISTS) {
						act->AuraCooldown -= (core->Time.defaultTicksPerSec * core->Time.round_sec) % 10;
					}
					if ((int) act->AuraCooldown < 0) {
						act->AuraCooldown = 0;
					}
				}
				// CurrentActionState
				if (act->CurrentActionState) {
					initiatives[0][idx].CurrentActionStateDescrease += (core->Time.defaultTicksPerSec * core->Time.round_sec) / 10;
					if (currentTurnBasedList == MAX_ATTACK_LISTS) {
						initiatives[0][idx].CurrentActionStateDescrease += (core->Time.defaultTicksPerSec * core->Time.round_sec) % 10;
					}
				}
				// IdleTicks
				act->IdleTicks += (core->Time.defaultTicksPerSec * core->Time.round_sec) / 10;
				if (currentTurnBasedList == MAX_ATTACK_LISTS) {
					act->IdleTicks += (core->Time.defaultTicksPerSec * core->Time.round_sec) % 10;
				}
			}

			if (initiatives[currentTurnBasedList].size()) {
				// Copy havefreeaction from previous phase for each actor
				for (auto& slot : initiatives[currentTurnBasedList]) {
					for (const auto& prevSlot : initiatives[currentTurnBasedList - 1]) {
						if (prevSlot.actor == slot.actor) {
							slot.havefreeaction = prevSlot.havefreeaction;
							break;
						}
					}
				}
				break;
			}
		}

		if (currentTurnBasedList == MAX_ATTACK_LISTS) {
			currentTurnBasedList = 0;
			currentTurnBasedSlot = 0;
			currentTurnBasedActor = nullptr;

			String rollLog = fmt::format(u">>> ENVIRONMENT PHASE <<<");
			displaymsg->DisplayString(std::move(rollLog), GUIColors::GOLD, 0);
			return;
		}
	}

	if (currentTurnBasedActor->IsPC()) {
		core->GetGame()->SelectActor(currentTurnBasedActor, false, SELECT_REPLACE);
	}

	InitTurnBasedSlot();

	if (currentTurnBasedActor->IsPC()) {
		core->GetGame()->SelectActor(currentTurnBasedActor, true, SELECT_REPLACE);
	}
}

void TurnBasedCombatManager::UpdateTurnBased()
{
	Game* game = core->GetGame();

	if (initiatives[0].size()) {
		if (pause_before_fight) {
			pause_before_fight--;
		}

		if (timeTurnBased <= timeTurnBasedNeed) {
			if (timeTurnBased == timeTurnBasedNeed) {
				timeTurnBasedNeed = 0;
			} else {
				timeTurnBased++;
			}
		}

		// remove dead actors
		if (currentTurnBasedActor && (currentTurnBasedActor->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP))) {
			EndTurn();
		}

		for (size_t list = 0; list < MAX_ATTACK_LISTS; list++) {
			if (!initiatives[list].size()) {
				break;
			}
			for (auto it = initiatives[list].begin(); it != initiatives[list].end();) {
				if (!it->actor || it->actor->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP)) {
					it = initiatives[list].erase(it);
				} else {
					++it;
				}
			}
		}

		// Validate currentTurnBasedSlot after removing dead actors
		if (currentTurnBasedList < 10 && currentTurnBasedSlot >= initiatives[currentTurnBasedList].size()) {
			currentTurnBasedSlot = initiatives[currentTurnBasedList].size() > 0 ? initiatives[currentTurnBasedList].size() - 1 : 0;
		}

		if (!(game->GetGameTimeReal() % 16)) {
			for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
				Actor* actor = initiatives[0][idx].actor;
				if (!actor) continue;
				if (actor->GetStance() == IE_ANI_DIE ||
				    actor->GetStance() == IE_ANI_TWITCH ||
				    actor->GetStance() == IE_ANI_SLEEP ||
				    actor->GetStance() == IE_ANI_CAST) {
					actor->RemoveFromAdditionInitiativeLists();
					continue;
				}

				if (actor->Immobile() ||
				    (actor->Modified[IE_STATE_ID] & (STATE_CANTMOVE | STATE_PANIC)) ||
				    (actor->GetBase(IE_STATE_ID) & (STATE_CANTMOVE | STATE_PANIC))) {
					actor->RemoveFromAdditionInitiativeLists();
					continue;
				}
			}
		}

		currentTurnBasedSlot = 0;
		for (size_t idx = 0; idx < initiatives[currentTurnBasedList].size(); idx++) {
			if (initiatives[currentTurnBasedList][idx].actor == currentTurnBasedActor) {
				currentTurnBasedSlot = idx;
				break;
			}
		}

		// opportunity attacks
		if (opportunity) {
			// target is dead?
			if (!game->GetActorByGlobalID(opportunity) ||
			    (game->GetActorByGlobalID(opportunity)->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP))) {
				opportunists.clear();
				opportunity = 0;
			} else if (!currentTurnBasedActorOld) { // no current opportunist
				Actor* opportunist = nullptr;
				while (opportunists.size()) {
					opportunist = game->GetActorByGlobalID(opportunists.back());
					opportunists.pop_back();
					if (!opportunist || opportunist->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP)) {
						continue;
					}
					int opportunistList = -1;
					int opportunistSlot = -1;
					for (size_t list = 0; list < MAX_ATTACK_LISTS; list++) {
						for (size_t idx = 0; idx < initiatives[list].size(); idx++) {
							if (initiatives[list][idx].actor != opportunist) {
								continue;
							}
							if (initiatives[list][idx].haveaction) {
								opportunistList = list;
								opportunistSlot = idx;
								break;
							}
						}
						if (opportunistSlot != -1) {
							break;
						}
					}
					if (opportunistSlot != -1) {
						if (currentTurnBasedActor->IsPC()) {
							game->SelectActor(currentTurnBasedActor, false, SELECT_REPLACE);
						}
						currentTurnBasedActorOld = currentTurnBasedActor;
						currentTurnBasedListOld = currentTurnBasedList;
						currentTurnBasedSlotOld = currentTurnBasedSlot;
						currentTurnBasedActor = opportunist;
						currentTurnBasedList = opportunistList;
						currentTurnBasedSlot = opportunistSlot;
						currentTurnBasedActor->lastInit = game->GetGameTimeReal();
						if (currentTurnBasedActor->IsPC()) {
							game->SelectActor(currentTurnBasedActor, true, SELECT_REPLACE);
							currentTurnBasedActor->ClearPath(true);
							currentTurnBasedActor->ReleaseCurrentAction();
						}
						break;
					}
				}
				if (currentTurnBasedActorOld) {
					String rollLog = fmt::format(u">>> OPPORTUNITY ATTACK <<<");
					displaymsg->DisplayString(std::move(rollLog), GUIColors::GOLD, 0);
					return;
				} else {
					opportunity = 0;
					opportunists.clear();
				}
			}
		}

		// move all party members in initiative list
		for (size_t idx = 0; idx < game->GetPCs().size(); idx++) {
			if (game->GetPCs()[idx]->InInitiativeList() == false) {
				game->GetPCs()[idx]->MoveToInitiativeList();
			}
		}

		// have enemy present?
		bool enemyPresent = false;
		bool pcPresent = false;
		for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
			Actor* actor = initiatives[0][idx].actor;
			if (actor->Modified[IE_EA] > EA_EVILCUTOFF && actor->GetCurrentArea()->IsVisible(actor->Pos)) {
				enemyPresent = true;
			}
			if (initiatives[0][idx].actor->IsPC() == true) {
				pcPresent = true;
			}
		}

		if (timeTurnBased >= timeTurnBasedNeed) {
			// first round start
			if (enemyPresent && pcPresent && currentTurnBasedActor == nullptr && pause_before_fight == 0) {
				game->PartyAttack = true;
				FirstRoundStart();
			}

			// end battle if no enemy present
			if (!turnBasedEnable || !enemyPresent || !pcPresent) {
				resetTurnBased();
			}
		}
	}
}

void TurnBasedCombatManager::resetTurnBased()
{
	if (core->GetGame() && timeTurnBased) {
		core->GetGame()->SetGameTime(timeTurnBased);
	}

	for (int i = 0; i < MAX_ATTACK_LISTS; i++) {
		initiatives[i].clear();
	}

	currentTurnBasedSlot = 0;
	currentTurnBasedSlotOld = 0;
	currentTurnBasedList = 0;
	currentTurnBasedListOld = 0;
	currentTurnBasedActor = nullptr;
	currentTurnBasedActorOld = nullptr;
	opportunity = 0;
	opportunists.clear();
	roundTurnBased = 0;
	timeTurnBased = 0;
	timeTurnBasedNeed = 0;
	pause_before_fight = 10;
}

bool TurnBasedCombatManager::HasFreeAction() const
{
	if (!IsTurnBased() || !currentTurnBasedActor) {
		return true;
	}
	const InitiativeSlot* slot = GetCurrentTurnBasedSlot();
	return slot ? slot->havefreeaction : true;
}

bool TurnBasedCombatManager::HasMainAction() const
{
	if (!IsTurnBased() || !currentTurnBasedActor) {
		return true;
	}
	const InitiativeSlot* slot = GetCurrentTurnBasedSlot();
	return slot ? slot->haveaction : true;
}

bool TurnBasedCombatManager::UseFreeAction()
{
	if (!IsTurnBased() || !currentTurnBasedActor) {
		return true;
	}
	InitiativeSlot* slot = GetCurrentTurnBasedSlot();
	if (slot && slot->havefreeaction) {
		slot->havefreeaction = false;
		return true;
	}
	return false;
}

bool TurnBasedCombatManager::UseMainAction()
{
	if (!IsTurnBased() || !currentTurnBasedActor) {
		return true;
	}
	InitiativeSlot* slot = GetCurrentTurnBasedSlot();
	if (slot && slot->haveaction) {
		slot->haveaction = false;
		return true;
	}
	return false;
}

bool TurnBasedCombatManager::UseAllMainActions()
{
	if (!IsTurnBased() || !currentTurnBasedActor) {
		return true;
	}
	// Spend main action in all phases for current actor
	for (size_t list = 0; list < 10; list++) {
		for (auto& slot : initiatives[list]) {
			if (slot.actor == currentTurnBasedActor) {
				slot.haveaction = false;
			}
		}
	}
	return true;
}

void TurnBasedCombatManager::ToggleTurnBased()
{
	turnBasedEnable = !turnBasedEnable;

	String text;
	if (turnBasedEnable) {
		text = fmt::format(u"Turn based mode enabled.");
	} else {
		text = fmt::format(u"Turn based mode disabled.");
	}
	displaymsg->DisplayString(std::move(text), GUIColors::GOLD, 0);
}

} // namespace GemRB
