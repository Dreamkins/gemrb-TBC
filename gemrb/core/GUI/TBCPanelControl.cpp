/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2024 The GemRB Project
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#include "TBCPanelControl.h"

#include "ie_stats.h"

#include "Game.h"
#include "GameControl.h"
#include "Interface.h"
#include "ScriptEngine.h"
#include "Sprite2D.h"
#include "TurnBasedCombatManager.h"

#include "Scriptable/Actor.h"
#include "Video/Video.h"

namespace GemRB {

namespace {
	// Slot dimensions
	constexpr int SLOT_WIDTH = 42; // Width of each actor slot
	constexpr int SLOT_HEIGHT = 60; // Height of each actor slot
	constexpr int PORTRAIT_WIDTH = 38; // Width of actor portrait area

	// Panel layout
	constexpr int PANEL_TOP = 40; // Y position of panel top
	constexpr int PANEL_PADDING = 5; // Padding around panel
	constexpr int LIST_SEPARATOR = 15; // Gap between initiative lists

	// Active actor offset
	constexpr int ACTIVE_ACTOR_OFFSET = 15; // Y offset for current actor highlight

	// Status indicators
	constexpr int STATUS_INDICATOR_SIZE = 10; // Size of action/move indicators
	constexpr int STATUS_INDICATOR_OFFSET = 15; // Y offset above slot

	// Screen margins (fraction of screen width)
	constexpr int SCREEN_MARGIN_DIVISOR = 8; // 1/8 of screen width

	// Animation orientation thresholds for sprite offset
	constexpr int ORIENTATION_LEFT_MIN = 2;
	constexpr int ORIENTATION_LEFT_MAX = 6;
	constexpr int ORIENTATION_RIGHT_MIN = 10;
	constexpr int ORIENTATION_RIGHT_MAX = 14;
	constexpr int SPRITE_ORIENTATION_OFFSET = 10;

	// Colors
	const Color COLOR_PANEL_BORDER(128, 128, 128, 255);
	const Color COLOR_WHITE(255, 255, 255, 255);
	const Color COLOR_BLACK(0, 0, 0, 255);
	const Color COLOR_SLOT_BG(64, 64, 64, 255);
	const Color COLOR_ACTION_AVAILABLE(128, 192, 128, 255);
	const Color COLOR_FREE_ACTION(255, 220, 64, 255);
	const Color COLOR_MOVEMENT(128, 128, 255, 255);
	const Color COLOR_HP_DAMAGE(128, 0, 0, 128);
	const Color COLOR_OPPORTUNITY_LINE(255, 0, 0, 255);

	size_t InitiativeListCount()
	{
		size_t count = 0;
		for (size_t list = 0; list < TurnBasedCombatManager::MAX_ATTACK_LISTS; list++) {
			if (core->tbcManager.initiatives[list].empty()) break;
			count = list + 1;
		}
		return count;
	}

	// Steps 1-2: total width and the (current-actor-aware) centered offset,
	// without any scroll applied yet.
	void ComputeBaseLayout(int screenWidth, int& totalWidth, int& xOffset)
	{
		totalWidth = 0;
		for (size_t list = 0; list < TurnBasedCombatManager::MAX_ATTACK_LISTS; list++) {
			if (core->tbcManager.initiatives[list].empty()) break;
			totalWidth += static_cast<int>(core->tbcManager.initiatives[list].size()) * SLOT_WIDTH + LIST_SEPARATOR;
		}

		int screenMargin = screenWidth / SCREEN_MARGIN_DIVISOR;
		xOffset = screenWidth / 2 - totalWidth / 2;

		int testOffset = xOffset;
		for (size_t list = 0; list < TurnBasedCombatManager::MAX_ATTACK_LISTS; list++) {
			if (core->tbcManager.initiatives[list].empty()) break;

			for (size_t idx = 0; idx < core->tbcManager.initiatives[list].size(); idx++) {
				Actor* actor = core->tbcManager.initiatives[list][idx].actor;
				if (!actor) continue;

				bool isCurrentActor = (actor == core->tbcManager.currentTurnBasedActor &&
						       core->tbcManager.currentTurnBasedList == static_cast<int>(list));
				if (isCurrentActor) {
					int actorPosX = testOffset + static_cast<int>(idx) * SLOT_WIDTH;
					if (actorPosX < screenMargin) {
						xOffset += screenMargin - actorPosX;
					} else if (actorPosX + SLOT_WIDTH + 10 > screenWidth - screenMargin) {
						xOffset -= (actorPosX + SLOT_WIDTH + 10) - (screenWidth - screenMargin);
					}
					break;
				}
			}
			testOffset += static_cast<int>(core->tbcManager.initiatives[list].size()) * SLOT_WIDTH + LIST_SEPARATOR;
		}
	}

	// Pure: the scroll offset delta that keeps the panel within the screen margins.
	int ClampedScrollOffset(int screenWidth, int totalWidth, int xOffset)
	{
		int screenMargin = screenWidth / SCREEN_MARGIN_DIVISOR;
		int off = core->tbcManager.offsetPanelTurnBased;
		if (xOffset + off > screenMargin) {
			off = screenMargin - xOffset;
		} else if (xOffset + off + totalWidth < screenWidth - screenMargin) {
			off = (screenWidth - screenMargin) - totalWidth - xOffset;
		}
		return off;
	}

	// Clamps the cached scroll offset AND persists it.
	// The persist matters: TBCPanel.py does a read-modify-write on
	// offsetPanelTurnBased, so if the clamp is only projected and never stored the
	// accumulator grows without bound and every wheel tick lands back on the edge,
	// which reads as a panel stuck in place.
	int ClampScrollOffset(int screenWidth, int totalWidth, int xOffset)
	{
		core->tbcManager.offsetPanelTurnBased = ClampedScrollOffset(screenWidth, totalWidth, xOffset);
		return core->tbcManager.offsetPanelTurnBased;
	}

	// Applies the cached scroll offset (read-only) and clamps it to keep the panel
	// within the screen margins. Used for hit-testing where no wheel is consumed.
	int ApplyScrollOffset(int screenWidth, int totalWidth, int xOffset)
	{
		return xOffset + ClampedScrollOffset(screenWidth, totalWidth, xOffset);
	}

	// True when the given actor occupies the slot at (list, idx) for the current turn.
	bool IsCurrentActorSlot(Actor* actor, size_t list)
	{
		return actor == core->tbcManager.currentTurnBasedActor &&
			core->tbcManager.currentTurnBasedList == static_cast<int>(list);
	}

	// Geometry helpers shared by DrawSelf and OnMouseDown so the two can't drift.
	int SlotX(int xOffset, size_t idx)
	{
		return xOffset + static_cast<int>(idx) * SLOT_WIDTH;
	}

	int SlotY(bool isCurrent)
	{
		return PANEL_TOP + (isCurrent ? ACTIVE_ACTOR_OFFSET : 0);
	}

	// Clamps the cached scroll offset to keep the panel within the screen margins,
	// and resets it to 0 when the panel overflows the screen but the cursor is not
	// over it. Pure (no input consumption) - safe to call from DrawSelf.
	int ApplyScrollAndReset(int screenWidth, int totalWidth, int xOffset, GameControl* gc)
	{
		int screenMargin = screenWidth / SCREEN_MARGIN_DIVISOR;
		Region panelBounds(Point(xOffset - PANEL_PADDING, PANEL_TOP - PANEL_PADDING),
				   Size(totalWidth, SLOT_HEIGHT + PANEL_PADDING * 2));
		bool panelExceedsScreen = !(panelBounds.x > screenMargin &&
					    panelBounds.x + panelBounds.w < screenWidth - screenMargin);
		if (panelExceedsScreen && !panelBounds.PointInside(gc->ScreenMousePos())) {
			core->tbcManager.offsetPanelTurnBased = 0;
		}
		ClampScrollOffset(screenWidth, totalWidth, xOffset);
		return xOffset + core->tbcManager.offsetPanelTurnBased;
	}
}

TBCPanelControl::TBCPanelControl(const Region& frame)
	: Control(frame)
{
	ControlType = IE_GUI_TBCPANEL;
}

void TBCPanelControl::DrawSelf(const Region& /*drawFrame*/, const Region& /*clip*/)
{
	if (!core->IsTurnBased() || core->tbcManager.pause_before_fight) {
		return;
	}

	GameControl* gc = core->GetGameControl();
	if (!gc) return;

	int screenWidth = core->config.Width;
	int totalWidth = 0;
	int xOffset = 0;
	ComputeBaseLayout(screenWidth, totalWidth, xOffset);
	xOffset = ApplyScrollAndReset(screenWidth, totalWidth, xOffset, gc);

	Point opportunityTarget;
	Point opportunitySource;

	for (size_t list = 0; list < InitiativeListCount(); list++) {
		int listSize = static_cast<int>(core->tbcManager.initiatives[list].size());

		if (list > 0) {
			Region separator(Point(xOffset - PANEL_PADDING - 6, PANEL_TOP - PANEL_PADDING + 15),
					 Size(2, 40));
			VideoDriver->DrawRect(separator, COLOR_PANEL_BORDER, true, BlitFlags::BLENDED);
		}

		Region listBorder(Point(xOffset - PANEL_PADDING, PANEL_TOP - PANEL_PADDING),
				  Size(listSize * SLOT_WIDTH + PANEL_PADDING, SLOT_HEIGHT + PANEL_PADDING * 2));
		VideoDriver->DrawRect(listBorder, COLOR_PANEL_BORDER, false, BlitFlags::BLENDED);

		for (size_t idx = 0; idx < core->tbcManager.initiatives[list].size(); idx++) {
			Actor* actor = core->tbcManager.initiatives[list][idx].actor;
			if (!actor) continue;

			bool isCurrentActor = IsCurrentActorSlot(actor, list);
			bool isAlive = !(actor->GetInternalFlag() & (IF_JUSTDIED | IF_REALLYDIED | IF_CLEANUP));

			int slotX = SlotX(xOffset, idx);
			int slotY = SlotY(isCurrentActor);
			Point slotPos(slotX, slotY);
			Region slotRegion(slotPos, Size(PORTRAIT_WIDTH, SLOT_HEIGHT));

			if (core->tbcManager.opportunity) {
				Actor* oppTarget = core->GetGame()->GetActorByGlobalID(core->tbcManager.opportunity);
				if (!opportunityTarget.y && oppTarget == actor) {
					opportunityTarget = Point(slotX + PORTRAIT_WIDTH / 2, slotY + SLOT_HEIGHT);
				}
				if (!opportunitySource.y && core->tbcManager.currentTurnBasedActorOld &&
				    actor == core->tbcManager.currentTurnBasedActor) {
					opportunitySource = Point(slotX + PORTRAIT_WIDTH / 2, slotY + SLOT_HEIGHT);
				}
			}

			if (slotRegion.PointInside(gc->ScreenMousePos())) {
				gc->SetLastActor(actor);
				gc->UpdateCursor();
			}

			Region oldClip = VideoDriver->GetScreenClip();
			VideoDriver->SetScreenClip(&slotRegion);

			const auto& initSlot = core->tbcManager.initiatives[list][idx];

			if (initSlot.image) {
				int imgWidth = initSlot.image->Frame.w;
				int imgHeight = initSlot.image->Frame.h;

				Point imgPos = slotPos;
				imgPos.x += (PORTRAIT_WIDTH - imgWidth) / 2;
				imgPos.y += (SLOT_HEIGHT - imgHeight) / 2;

				VideoDriver->DrawRect(slotRegion, COLOR_BLACK, true, BlitFlags::BLENDED);
				VideoDriver->BlitSprite(initSlot.image, imgPos);
			} else if (isAlive) {
				using AnimationPart = std::pair<Animation*, Holder<Palette>>;
				std::vector<AnimationPart> anim = actor->GetCurrentStanceAnim();

				if (!anim.empty() && anim[0].first && anim[0].first->GetFrameCount()) {
					Animation* firstAnim = anim[0].first;
					int animHeight = firstAnim->animArea.h;
					int animOriginY = firstAnim->animArea.origin.y;
					int animWidth = firstAnim->animArea.w;

					int vertOffset = (SLOT_HEIGHT > animHeight) ? (SLOT_HEIGHT - animHeight) / 2 : 0;
					int spriteY = animHeight - (animHeight + animOriginY) + vertOffset;

					int horizOffset = 0;
					if (animWidth - PORTRAIT_WIDTH > 10) {
						int orientation = actor->GetOrientation();
						if (orientation >= ORIENTATION_LEFT_MIN && orientation <= ORIENTATION_LEFT_MAX) {
							horizOffset = SPRITE_ORIENTATION_OFFSET;
						} else if (orientation >= ORIENTATION_RIGHT_MIN && orientation <= ORIENTATION_RIGHT_MAX) {
							horizOffset = -SPRITE_ORIENTATION_OFFSET;
						}
					}

					VideoDriver->DrawRect(slotRegion, COLOR_SLOT_BG, true, BlitFlags::BLENDED);

					Region drawRegion(slotPos.x + PORTRAIT_WIDTH / 2 + horizOffset,
							  slotPos.y + spriteY + 4,
							  PORTRAIT_WIDTH, SLOT_HEIGHT);
					actor->Draw(drawRegion, COLOR_WHITE, COLOR_WHITE, BlitFlags::BLENDED, true);
				}
			}

			VideoDriver->SetScreenClip(&oldClip);

			Color borderColor = actor->GetCircleColor();
			VideoDriver->DrawRect(slotRegion, borderColor, false, BlitFlags::BLENDED);

			if (isCurrentActor && actor->IsPC()) {
				// currentTurnBasedActor is a cached pointer; the slot it names can still be
				// gone after a dead-slot erase, so resolve it once and tolerate null.
				const InitiativeSlot* curSlot = core->GetCurrentTurnBasedSlot();
				if (!curSlot) {
					continue;
				}
				float movesLeft = std::min(1.0f, std::max(0.0f, curSlot->movesleft));
				int moveBarWidth = static_cast<int>(PORTRAIT_WIDTH * movesLeft);
				Region moveRect(slotX, slotY - STATUS_INDICATOR_OFFSET, moveBarWidth, STATUS_INDICATOR_SIZE);
				VideoDriver->DrawRect(moveRect, COLOR_MOVEMENT, true, BlitFlags::BLENDED);

				int totalSquaresWidth = STATUS_INDICATOR_SIZE * 2 + 2;
				int squaresStartX = slotX + (SLOT_WIDTH - totalSquaresWidth) / 2;
				int squaresY = slotY + SLOT_HEIGHT + STATUS_INDICATOR_OFFSET - STATUS_INDICATOR_SIZE;

				bool hasAction = core->tbcManager.HasMainAction();
				Region actionRect(squaresStartX - 1, squaresY, STATUS_INDICATOR_SIZE, STATUS_INDICATOR_SIZE);
				VideoDriver->DrawRect(actionRect, COLOR_ACTION_AVAILABLE, hasAction, BlitFlags::BLENDED);

				bool hasFreeAction = curSlot->havefreeaction;
				Region freeActionRect(squaresStartX + STATUS_INDICATOR_SIZE + 2, squaresY,
						      STATUS_INDICATOR_SIZE, STATUS_INDICATOR_SIZE);
				VideoDriver->DrawRect(freeActionRect, COLOR_FREE_ACTION, hasFreeAction, BlitFlags::BLENDED);
			}

			int maxHP = actor->Modified[IE_MAXHITPOINTS];
			int currentHP = std::max(0, static_cast<int>(actor->Modified[IE_HITPOINTS]));

			if (maxHP > 0) {
				float hpPercent = static_cast<float>(currentHP) / static_cast<float>(maxHP);
				float damagePercent = 1.0f - hpPercent;

				int damageHeight = static_cast<int>((SLOT_HEIGHT - 2) * damagePercent);
				// At full health the overlay has zero height; a zero-height Region trips
				// an assert() in the SDL 1.2 backend, so never hand one to the driver.
				if (damageHeight > 0) {
					Region damageOverlay(slotRegion.x + 1,
							     slotRegion.y + 1 + static_cast<int>((SLOT_HEIGHT - 2) * hpPercent),
							     slotRegion.w - 2,
							     damageHeight);
					VideoDriver->DrawRect(damageOverlay, COLOR_HP_DAMAGE, true, BlitFlags::BLENDED);
				}
			}
		}

		xOffset += listSize * SLOT_WIDTH + LIST_SEPARATOR;
	}

	if (core->tbcManager.opportunity && opportunityTarget.y && opportunitySource.y) {
		VideoDriver->DrawLine(opportunitySource, opportunitySource + Point(0, -10),
				      COLOR_OPPORTUNITY_LINE, BlitFlags::BLENDED);
		VideoDriver->DrawLine(opportunitySource, opportunityTarget,
				      COLOR_OPPORTUNITY_LINE, BlitFlags::BLENDED);
		VideoDriver->DrawLine(opportunityTarget, opportunityTarget + Point(0, -25),
				      COLOR_OPPORTUNITY_LINE, BlitFlags::BLENDED);
	}
}

bool TBCPanelControl::OnMouseDown(const MouseEvent& me, unsigned short /*mod*/)
{
	if (!core->IsTurnBased() || core->tbcManager.pause_before_fight) {
		return false;
	}

	GameControl* gc = core->GetGameControl();
	if (!gc) return false;

	int screenWidth = core->config.Width;
	int totalWidth = 0;
	int xOffset = 0;
	ComputeBaseLayout(screenWidth, totalWidth, xOffset);
	xOffset = ApplyScrollOffset(screenWidth, totalWidth, xOffset);

	for (size_t list = 0; list < InitiativeListCount(); list++) {
		for (size_t idx = 0; idx < core->tbcManager.initiatives[list].size(); idx++) {
			Actor* actor = core->tbcManager.initiatives[list][idx].actor;
			if (!actor) continue;

			int slotX = SlotX(xOffset, idx);
			int slotY = SlotY(IsCurrentActorSlot(actor, list));
			Region slotRegion(Point(slotX, slotY), Size(PORTRAIT_WIDTH, SLOT_HEIGHT));

			if (slotRegion.PointInside(me.Pos())) {
				// Dispatch actor click to Python handler (action/target logic).
				ScriptEngine::FunctionParameters params;
				params.push_back(ScriptEngine::Parameter((long) actor->GetGlobalID()));
				params.push_back(ScriptEngine::Parameter(IsCurrentActorSlot(actor, list)));
				params.push_back(ScriptEngine::Parameter(actor->InParty));
				core->GetGUIScriptEngine()->RunFunction("TBCPanel", "OnPanelClick", params);
				return true; // consume the click so GameControl doesn't also act on the map
			}
		}
		xOffset += static_cast<int>(core->tbcManager.initiatives[list].size()) * SLOT_WIDTH + LIST_SEPARATOR;
	}

	// Click outside any slot: let GameControl handle it (normal map interaction).
	return false;
}

bool TBCPanelControl::OnMouseWheelScroll(const Point& delta)
{
	if (!core->IsTurnBased() || core->tbcManager.pause_before_fight) {
		return false;
	}

	GameControl* gc = core->GetGameControl();
	if (!gc) return false;

	int screenWidth = core->config.Width;
	int totalWidth = 0;
	int xOffset = 0;
	ComputeBaseLayout(screenWidth, totalWidth, xOffset);

	int screenMargin = screenWidth / SCREEN_MARGIN_DIVISOR;
	Region panelBounds(Point(xOffset - PANEL_PADDING, PANEL_TOP - PANEL_PADDING),
			   Size(totalWidth, SLOT_HEIGHT + PANEL_PADDING * 2));
	bool panelExceedsScreen = !(panelBounds.x > screenMargin &&
				    panelBounds.x + panelBounds.w < screenWidth - screenMargin);
	if (!panelExceedsScreen || !panelBounds.PointInside(gc->ScreenMousePos())) {
		return false;
	}

	// Dispatch scroll to Python handler (offset adjustment).
	core->GetGUIScriptEngine()->RunFunction("TBCPanel", "OnPanelMouseWheelScroll", delta);
	return true;
}

bool TBCPanelControl::IsAnimated() const
{
	return core->IsTurnBased() && !core->tbcManager.pause_before_fight;
}

}
