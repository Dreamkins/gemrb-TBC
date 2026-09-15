#GemRB - Infinity Engine Emulator
#Copyright (C) 2024 The GemRB Project
#
#This program is free software; you can redistribute it and/or
#modify it under the terms of the GNU General Public License
#as published by the Free Software Foundation; either version 2
#of the License, or (at your option) any later version.
#
#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#GNU General Public License for more details.
#
#You should have received a copy of the GNU General Public License
#along with this program; if not, write to the Free Software
#Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

# Turn-based combat initiative panel.
#
# Rendering (DrawSelf) and event dispatch (OnMouseDown/OnMouseWheelScroll)
# are handled by C++ TBCPanelControl. This module provides the Python-side
# interaction logic: actor selection on click (OnPanelClick) and panel scroll
# adjustment (OnPanelMouseWheelScroll). The panel window is auto-created by
# Interface::StartGameControl; Open() is available for explicit creation.

import GemRB
from GUIDefines import *

TBC_PANEL_WINDOW = 0x1000
TBC_PANEL_CONTROL = 0x1001

# Height of the top HUD band that the panel occupies (must match TBCPanelControl.cpp).
TBC_PANEL_HEIGHT = 130

Window = None
Panel = None

def Open():
	"""Create the turn-based combat initiative panel as a top-level HUD window."""
	global Window, Panel
	if Window is not None:
		return Window

	width = GemRB.GetSystemVariable(SV_WIDTH)
	height = GemRB.GetSystemVariable(SV_HEIGHT)

	# real-pixel window sitting above the game viewport
	Window = GemRB.CreateView(TBC_PANEL_WINDOW, IE_GUI_INVALID, (0, 0, width, height))
	Panel = Window.CreateSubview(TBC_PANEL_CONTROL, IE_GUI_TBCPANEL, (0, 0, width, TBC_PANEL_HEIGHT))
	return Window

def Close():
	"""Destroy the panel window."""
	global Window, Panel
	if Window is None:
		return
	GemRB.RemoveView(Window, True)
	Window = None
	Panel = None


# -- Python interaction handlers (called by C++ TBCPanelControl dispatch) --

def OnPanelClick(actorGlobalID, isCurrentActor, inParty):
	"""Handle an actor slot click in the initiative panel.

	Called from TBCPanelControl::OnMouseDown after hit-testing determines
	which actor slot was clicked. C++ provides the hit-testing result so that
	geometry/focus logic stays consistent with rendering.

	Parameters:
		actorGlobalID: Global ID of the clicked actor, or -1 if none.
		isCurrentActor: True if the clicked actor has the current turn.
		inParty: True if the clicked actor is a party member.
	"""
	if actorGlobalID == -1:
		return

	# Mark target: non-party actors and non-current actors become targets
	if not inParty and not isCurrentActor:
		GemRB.SetTurnBasedTarget(actorGlobalID)

	# Update cursor/selection to the clicked actor
	GemRB.GameControlLocateActor(actorGlobalID)


def OnPanelMouseWheelScroll(delta):
	"""Handle mouse wheel scrolling of the initiative panel.

	Called from TBCPanelControl::OnMouseWheelScroll after C++ determines
	that the panel is overflowed and the cursor is inside it. The offset
	adjustment logic lives here.

	Parameters:
		delta: Point dict {"x": int, "y": int} with scroll delta.
	"""
	if delta is None:
		return

	direction = 0
	if delta.get("x", 0) > 0 or delta.get("y", 0) > 0:
		direction = 1
	elif delta.get("x", 0) < 0 or delta.get("y", 0) < 0:
		direction = -1

	if direction == 0:
		return

	speed = GemRB.GetMouseScrollSpeed()
	current_offset = GemRB.GetTurnBasedScrollOffset()
	GemRB.SetTurnBasedScrollOffset(current_offset + direction * speed * 4)

