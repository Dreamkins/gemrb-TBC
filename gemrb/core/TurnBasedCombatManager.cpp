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
#include "Logging/Logging.h"
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

	// The delayed attack is being played out now, so consume the flag. Nothing else
	// ever clears it: leaving it set would keep the actor out of the movesleft /
	// free-action grant for the rest of the combat, and make it ineligible for a
	// further delay, because EndTurn refuses to re-delay a slot that is already marked.
	if (slot) {
		slot->delayaction = false;
	}

	// No slot means no current actor (empty list, or currentTurnBasedList out of range).
	// Everything below dereferences the actor, so stop here and let the caller's own
	// null check decide what to do; the environment phase reaches this legitimately.
	if (!currentTurnBasedActor) {
		return;
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
		// timeTurnBased counts GameTime ticks, so the delta baseline must start here
		// or the first update would add the whole elapsed game time in one step.
		lastRealTime = timeTurnBased;
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

	for (size_t attacks = 1; attacks < static_cast<size_t>(MAX_ATTACK_LISTS); attacks++) {
		initiatives[attacks].clear();
		for (size_t idx = 0; idx < initiatives[0].size(); idx++) {
			if (initiatives[0][idx].actor->attackcount >= static_cast<int>(attacks) + 1) {
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

	// currentTurnBasedActorOld is a raw Actor* held across frames while the opportunist
	// acts, and nothing purges it if that actor dies in the meantime. Only restore if
	// it is still alive; otherwise drop the saved state and end the turn normally.
	if (currentTurnBasedActorOld && !(currentTurnBasedActorOld->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP))) {
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
	currentTurnBasedActorOld = nullptr;

	Actor* actor = currentTurnBasedActor;

	if (actor->InInitiativeList()) {
		size_t listSize = initiatives[currentTurnBasedList].size();
		currentTurnBasedSlot = 0;
		bool foundInList = false;
		for (size_t idx = 0; idx < listSize; idx++) {
			if (initiatives[currentTurnBasedList][idx].actor == currentTurnBasedActor) {
				currentTurnBasedSlot = idx;
				foundInList = true;
				break;
			}
		}

		// delayed attack. Requires the actor to have actually been located in THIS list:
		// otherwise currentTurnBasedSlot stays 0 and we would delay a different actor's
		// slot. The listSize > 0 test also keeps `size() - 1` from wrapping on an empty list.
		if (foundInList && listSize > 0 && actor->GetStance() != IE_ANI_DIE && actor->GetStance() != IE_ANI_TWITCH &&
		    actor->GetStance() != IE_ANI_SLEEP &&
		    !(actor->Immobile() || (actor->Modified[IE_STATE_ID] & (STATE_CANTMOVE | STATE_PANIC))) && HasMainAction() &&
		    static_cast<size_t>(currentTurnBasedSlot) < listSize - 1) {
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

	if (currentTurnBasedSlot >= static_cast<int>(initiatives[currentTurnBasedList].size())) {
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

			// currentTurnBasedList was just incremented and can be MAX_ATTACK_LISTS here;
			// initiatives only has MAX_ATTACK_LISTS elements, so reading it unguarded is OOB.
			if (currentTurnBasedList < MAX_ATTACK_LISTS && initiatives[currentTurnBasedList].size()) {
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

	if (currentTurnBasedActor && currentTurnBasedActor->IsPC()) {
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

		// Advance timeTurnBased by the ticks that actually elapsed, not one per frame.
		// timeTurnBasedNeed is in GameTime ticks (round_sec * defaultTicksPerSec),
		// and GetGameTime() hands timeTurnBased to every tick-scheduled effect
		// (poison damage, detect evil pulses, screen shake), so a per-frame increment
		// both compressed every round and ran those effects off the frame rate.
		uint32_t realNow = game->GetGameTimeReal();
		if (realNow != lastRealTime) {
			timeTurnBased += realNow - lastRealTime;
			lastRealTime = realNow;
		}

		// The round is over once the clock reaches the deadline. Compare with >=, not ==:
		// the clock now moves by more than one tick per frame, so it would step over
		// an exact match and the deadline would never be released.
		if (timeTurnBased >= timeTurnBasedNeed) {
			timeTurnBasedNeed = 0;
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
		if (currentTurnBasedList < MAX_ATTACK_LISTS && currentTurnBasedSlot >= static_cast<int>(initiatives[currentTurnBasedList].size())) {
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

		// Re-find the current actor after the dead-slot erase above. If it is no longer in
		// this list, falling back to slot 0 would silently hand its action points to
		// whichever actor happens to be first, so say so and drop the current actor
		// instead of pointing the turn at the wrong slot.
		currentTurnBasedSlot = 0;
		bool foundCurrentActor = false;
		for (size_t idx = 0; idx < initiatives[currentTurnBasedList].size(); idx++) {
			if (initiatives[currentTurnBasedList][idx].actor == currentTurnBasedActor) {
				currentTurnBasedSlot = idx;
				foundCurrentActor = true;
				break;
			}
		}
		if (!foundCurrentActor && currentTurnBasedActor) {
			Log(WARNING, "TurnBasedCombatManager", "current actor is not in initiative list {} at slot {}; dropping it",
			    currentTurnBasedList, currentTurnBasedSlot);
			currentTurnBasedActor = nullptr;
		}

		// opportunity attacks
		if (opportunity) {
			// target is dead?
			if (!game->GetActorByGlobalID(opportunity) ||
			    (game->GetActorByGlobalID(opportunity)->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP))) {
				opportunists.clear();
				opportunity = 0;
				lasOpportunityPos.Invalidate();
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
					lasOpportunityPos.Invalidate();
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
	// Must be invalidated too: Movable::DoStep only re-checks for opportunity attacks
	// when lasOpportunityPos != Pos, so a leftover value from a previous fight would
	// suppress the check for that tile forever.
	lasOpportunityPos.Invalidate();
	roundTurnBased = 0;
	timeTurnBased = 0;
	timeTurnBasedNeed = 0;
	lastRealTime = 0;
	pause_before_fight = 10;
}

void TurnBasedCombatManager::RemoveActor(Actor* actor)
{
	if (!actor) return;

	for (int i = 0; i < MAX_ATTACK_LISTS; i++) {
		initiatives[i].erase(std::remove_if(initiatives[i].begin(), initiatives[i].end(),
						    [actor](const InitiativeSlot& slot) { return slot.actor == actor; }),
				     initiatives[i].end());
	}
	if (currentTurnBasedActor == actor) {
		currentTurnBasedActor = nullptr;
	}
	if (currentTurnBasedActorOld == actor) {
		currentTurnBasedActorOld = nullptr;
	}
	currentTurnBasedListOld = 0;
	currentTurnBasedSlotOld = 0;
	// opportunists holds global IDs, not pointers, so there is nothing to purge there.
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

bool TurnBasedCombatManager::SpendMainAction(Actor* actor)
{
	if (!core->IsTurnBased()) return true;
	// No actor on turn (environment phase, or nobody has moved yet): charging is a
	// no-op anyway, so only reject when someone else's turn is really being spent.
	if (currentTurnBasedActor && actor != currentTurnBasedActor) return false;
	return UseMainAction();
}

bool TurnBasedCombatManager::SpendFreeAction(Actor* actor)
{
	if (!core->IsTurnBased()) return true;
	if (currentTurnBasedActor && actor != currentTurnBasedActor) return false;
	return UseFreeAction();
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
