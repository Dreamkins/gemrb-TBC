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

#ifndef TURNBASEDCOMBATMANAGER_H
#define TURNBASEDCOMBATMANAGER_H

#include "exports.h"
#include "globals.h"

#include "Holder.h"
#include "Sprite2D.h"

#include <vector>

namespace GemRB {

class Actor;

// =============================================================================
// Initiative Slot - represents one actor's turn in the initiative order
// =============================================================================

struct InitiativeSlot {
	Actor* actor = nullptr; // The actor for this slot
	Holder<Sprite2D> image; // Cached portrait image
	int initiative = 10; // Initiative roll result (lower = faster)
	float movesleft = 1.0f; // Remaining movement (0.0 to 1.0)
	bool haveaction = true; // Has action available this turn
	bool havefreeaction = true; // Has free action available this turn
	bool delayaction = false; // Action was delayed
	int CurrentActionStateDescrease = 0; // Timer for action state decrease
};

// =============================================================================
// Turn-Based Combat Manager
// Manages initiative order, turn progression, and opportunity attacks
// =============================================================================

class GEM_EXPORT TurnBasedCombatManager {
public:
	// -------------------------------------------------------------------------
	// Constants
	// -------------------------------------------------------------------------

	static constexpr int MAX_ATTACK_LISTS = 10; // Maximum APR supported

	// -------------------------------------------------------------------------
	// Initiative Lists
	// Array of initiative lists for phased attack system:
	// - initiatives[0]: All actors' first attack
	// - initiatives[1]: Actors with 2+ attacks per round
	// - initiatives[N]: Actors with N+1 attacks per round
	// -------------------------------------------------------------------------

	std::vector<InitiativeSlot> initiatives[MAX_ATTACK_LISTS];

	// -------------------------------------------------------------------------
	// Current Turn State
	// -------------------------------------------------------------------------

	int currentTurnBasedSlot = 0; // Index within current list
	int currentTurnBasedSlotOld = 0; // Saved slot for opportunity attacks
	int currentTurnBasedList = 0; // Current attack list (0-9)
	int currentTurnBasedListOld = 0; // Saved list for opportunity attacks
	int roundTurnBased = 0; // Current round number

	// -------------------------------------------------------------------------
	// Current Actor
	// -------------------------------------------------------------------------

	Actor* currentTurnBasedActor = nullptr; // Actor whose turn it is
	Actor* currentTurnBasedActorOld = nullptr; // Saved actor for opportunity attacks

	// -------------------------------------------------------------------------
	// Opportunity Attack State
	// -------------------------------------------------------------------------

	ieDword opportunity = 0; // GlobalID of opportunity attack target
	std::vector<ieDword> opportunists; // Actors who can make opportunity attacks
	Point lasOpportunityPos; // Last position for opportunity attack check

	// -------------------------------------------------------------------------
	// Timing
	// -------------------------------------------------------------------------

	uint32_t timeTurnBased = 0; // Current time in TBC mode
	uint32_t timeTurnBasedNeed = 0; // Time needed for current action
	int lastTurnBasedTarget = 0; // Last target ID

	// -------------------------------------------------------------------------
	// UI State
	// -------------------------------------------------------------------------

	int offsetPanelTurnBased = 0; // Mouse wheel scroll offset for panel
	int pause_before_fight = 10; // Countdown before combat starts
	bool turnBasedEnable = true; // runtime user toggle (via ToggleTurnBased); gated by config.EnableTurnBased (feature switch)

	// -------------------------------------------------------------------------
	// Constructor / Destructor
	// -------------------------------------------------------------------------

	TurnBasedCombatManager() = default;
	~TurnBasedCombatManager() = default;

	// -------------------------------------------------------------------------
	// Core Methods
	// -------------------------------------------------------------------------

	// Check if turn-based mode is active.
	// The second condition keeps TBC "on" during the inter-turn time advance
	// (timeTurnBased < timeTurnBasedNeed) even when no actor currently has the turn
	// (e.g. the environment phase), so the frozen-time abstraction stays in effect.
	bool IsTurnBased() const
	{
		return currentTurnBasedActor != nullptr || timeTurnBased < timeTurnBasedNeed;
	}

	// Get current slot info
	int GetCurrentTurnBasedSlotNum() const { return currentTurnBasedSlot; }
	int GetCurrentTurnBasedListNum() const { return currentTurnBasedList; }

	// Get initiative slot for current actor (nullptr if list empty / out of range)
	InitiativeSlot* GetCurrentTurnBasedSlot();
	const InitiativeSlot* GetCurrentTurnBasedSlot() const;

	// Get initiative slot for specific actor (nullptr if not in any list)
	InitiativeSlot* GetTurnBasedSlot(Actor* actor);

	// Get next available slot with action for actor
	InitiativeSlot* GetTurnBasedSlotWithAttack(Actor* actor);

	// Check if current actor has free action
	bool HasFreeAction() const;

	// Check if current actor has main action
	bool HasMainAction() const;

	// Use free action for current actor (returns true if had free action)
	bool UseFreeAction();

	// Use main action for current actor (returns true if had main action)
	bool UseMainAction();

	// Use all main actions in all phases for current actor (for spellcasting)
	bool UseAllMainActions();

	// -------------------------------------------------------------------------
	// Turn Management
	// -------------------------------------------------------------------------

	// Initialize slot at start of turn
	void InitTurnBasedSlot();

	// Start a new round
	void FirstRoundStart();

	// End current actor's turn
	void EndTurn();

	// Update TBC state (called each frame)
	void UpdateTurnBased();

	// Reset/clear TBC state
	void resetTurnBased();

	// Toggle TBC mode
	void ToggleTurnBased();
};

} // namespace GemRB

#endif // TURNBASEDCOMBATMANAGER_H
