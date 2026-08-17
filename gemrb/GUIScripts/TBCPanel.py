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
# The rendering is done in C++ by TBCPanelControl (control type IE_GUI_TBCPANEL);
# this module instantiates and lays out the control. The engine also creates a
# default panel automatically in Interface::StartGameControl.

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

