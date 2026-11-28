/*
===========================================================================
WALKER: hangar.

The robots of the pilot account stand in five bays along the bottom of the
screen; an empty bay sells a new robot. The selected robot is shown large on the
left with the weapons in its slots and can be turned around every axis: drag
with the left mouse button (left/right - turn, up/down - tilt), drag with the
right button (roll), arrow keys and PgUp/PgDn; the key light (LIGHT: ON, with
shadows of the parts on each other and on the floor) moves with the middle
button or Shift and the left one; HOME or "RESET VIEW" puts it
back, the wheel zooms. With the key light on, the top left corner of the preview
has two sliders - the strength of the key light and of the ambient light (the
shadows are as dark as the ambient light is weak) - and a dial seen from above with
the camera at the bottom: the arrow goes from the light to the robot, dragging in
the dial puts the light there (at the rim low, at the centre overhead).

On the right: the characteristics of the robot, its weapon slots (a slot takes
a weapon of its class: light, medium or heavy, bg_robots.c) and the selected
slot's weapon. CHANGE WEAPON lists the weapons of that class the pilot owns
(free ones and those on other robots: taking one moves it here) and the ones
the shop sells. Robots and weapons are upgraded separately for development
points; an upgrade takes real time (10 minutes on the first levels to 24 hours
on the last) and can be finished at once for currency. A robot goes into
battles when neither it nor one of its weapons is being upgraded (ui_account.c).

Layout (virtual 640x480): top bar with the pilot and his money, preview on the
left, panel on the right, bays, bottom bar with BACK. Texts use the small 6x12
font (UI_TINYFONT); buttons are drawn by Hangar_DrawButton.

This file is part of WALKER, based on the Quake III Arena source code
(GPL v2, see COPYING.txt).
===========================================================================
*/

#include "ui_local.h"

#define ART_BACKGROUND		"menu/art/hangar"
#define ART_BAY				"menu/art/maps_select"
#define ART_BAY_SELECTED	"menu/art/maps_selected"

#define MAX_BAYS			ROBOT_HANGAR_SIZE
#define MAX_CHOICES			6		// rows of the weapon list

#define ID_BACK				10
#define ID_AUTOROTATE		12
#define ID_RESETVIEW		13
#define ID_LIGHT			21
#define ID_UPGRADE_ROBOT	14
#define ID_UPGRADE_WEAPON	15
#define ID_CHANGE_WEAPON	16
#define ID_BUY				17
#define ID_SHOP_PREV		18
#define ID_SHOP_NEXT		19
#define ID_CHOOSE_BACK		20
#define ID_SLOT0			30
#define ID_CHOICE0			40
#define ID_BAY0				60

#define ZOOM_STEP			1.12f	// one wheel notch
#define ZOOM_MIN			0.6f
#define ZOOM_MAX			2.6f
#define DRAG_SPEED			1.2f	// degrees per virtual pixel of mouse travel
#define LIGHT_DEFAULT_YAW	40		// the key light: from the camera's right, above
#define LIGHT_DEFAULT_PITCH	50
#define LIGHT_MIN_PITCH		25
#define LIGHT_MAX_PITCH		88

// light controls in the top left corner of the preview (with LIGHT: ON)
#define LIGHTUI_X			( PREVIEW_X + 8 )
#define LIGHTUI_Y			( PREVIEW_Y + 8 )
#define LIGHTUI_W			112
#define SLIDER_W			80
#define SLIDER_H			6
#define SLIDER_KEY_Y		( LIGHTUI_Y + 13 )
#define SLIDER_AMBIENT_Y	( LIGHTUI_Y + 37 )
#define DIAL_X				( LIGHTUI_X + 52 )
#define DIAL_Y				( LIGHTUI_Y + 96 )
#define DIAL_R				28
#define LIGHT_NONE			0
#define LIGHT_KEY			1		// dragging the key light slider
#define LIGHT_AMBIENT		2
#define LIGHT_DIAL			3
#define TURN_SPEED			20.0f	// degrees per second of the automatic turn
#define KEY_STEP			10.0f

#define TOPBAR_H			22

// big preview
#define PREVIEW_X			8
#define PREVIEW_Y			26
#define PREVIEW_W			384
#define PREVIEW_H			304

// panel
#define PANEL_X				400
#define PANEL_Y				30
#define PANEL_W				232
#define PANEL_H				292
#define PANEL_PAD			8
#define LINE				14
#define BUTTON_H			18
#define ROW_STEP			22
#define SLOTS_Y				( PANEL_Y + 107 )	// first slot row, under the characteristics

// bays
#define BAY_Y				338
#define BAY_W				104
#define BAY_H				80
#define BAY_GAP				8

// bottom bar
#define BOTTOM_Y			430

#define CHOICE_EMPTY		0		// take the weapon out of the slot
#define CHOICE_OWN			1		// value: weapon of the pilot
#define CHOICE_BUY			2		// value: weapon type

typedef struct {
	menubitmap_s	item;			// first: the menu code sees a bitmap item
	char			label[48];
	char			detail[48];		// right-aligned part (cost, time), may be empty
	qboolean		enabled;		// enough points / credits: bright, otherwise dim
	qboolean		marked;			// the selected one of a group (slot)
} hangarButton_t;

typedef struct {
	menuframework_s	menu;
	menubitmap_s	preview;
	menubitmap_s	bays[MAX_BAYS];
	hangarButton_t	back;
	hangarButton_t	autoRotate;
	hangarButton_t	resetView;
	hangarButton_t	light;
	hangarButton_t	slots[ROBOT_MAX_SLOTS];
	hangarButton_t	upgradeRobot;
	hangarButton_t	upgradeWeapon;
	hangarButton_t	changeWeapon;
	hangarButton_t	buy;
	hangarButton_t	shopPrev;
	hangarButton_t	shopNext;
	hangarButton_t	choices[MAX_CHOICES];
	hangarButton_t	chooseBack;

	int				choiceKind[MAX_CHOICES];
	int				choiceValue[MAX_CHOICES];
	int				numChoices;

	playerInfo_t	players[MAX_BAYS];
	playerInfo_t	shopPlayer;
	qhandle_t		weaponModels[RWEAPON_COUNT];
	int				shopType;		// robot shown in an empty bay
	int				shopLoaded;		// type loaded into shopPlayer, -1 none
	int				loadedType[MAX_BAYS];
	int				selected;		// bay
	int				selectedSlot;
	qboolean		choosing;		// the weapon list of the selected slot is open
	float			previewYaw;
	float			previewPitch;
	float			previewRoll;
	float			previewZoom;
	float			bayYaw;
	int				lastTime;
	qboolean		dragging;		// left button: turn and tilt
	qboolean		rolling;		// right button: roll
	qboolean		lighting;		// middle button or Shift + left: the key light
	float			lightYaw;
	float			lightPitch;
	int				lightControl;	// LIGHT_KEY, LIGHT_AMBIENT, LIGHT_DIAL: dragging it
	float			dragLastX;
	float			dragLastY;
	qhandle_t		backgroundShader;
	qhandle_t		bayShader;
	qhandle_t		baySelectedShader;

	char			message[96];
} hangarMenuInfo_t;

static hangarMenuInfo_t	hangarMenuInfo;

static vec4_t hangarDimColor = { 0.55f, 0.62f, 0.66f, 1.00f };
static vec4_t hangarTextColor = { 0.88f, 0.90f, 0.92f, 1.00f };
static vec4_t hangarAccentColor = { 1.00f, 0.55f, 0.15f, 1.00f };
static vec4_t hangarPanelColor = { 0.0f, 0.0f, 0.0f, 0.60f };
static vec4_t hangarBarColor = { 0.0f, 0.0f, 0.0f, 0.70f };
static vec4_t hangarRuleColor = { 0.55f, 0.62f, 0.66f, 0.35f };
static vec4_t hangarGreenColor = { 0.45f, 1.0f, 0.45f, 1.0f };
static vec4_t hangarMoneyColor = { 1.0f, 0.85f, 0.30f, 1.0f };
static vec4_t hangarButtonColor = { 0.06f, 0.08f, 0.10f, 0.85f };
static vec4_t hangarButtonFocusColor = { 0.30f, 0.15f, 0.04f, 0.90f };
static vec4_t hangarButtonMarkedColor = { 0.16f, 0.10f, 0.04f, 0.90f };
static vec4_t hangarBorderColor = { 0.40f, 0.46f, 0.50f, 1.0f };


/*
=================
Hangar_Duration
=================
*/
static const char *Hangar_Duration( int seconds ) {
	if ( seconds < 0 ) {
		seconds = 0;
	}
	if ( seconds >= 3600 ) {
		return va( "%ih %02im", seconds / 3600, ( seconds / 60 ) % 60 );
	}
	return va( "%im %02is", seconds / 60, seconds % 60 );
}


/*
=================
Hangar_Number

12345 -> "12 345"
=================
*/
static const char *Hangar_Number( int value ) {
	// own rotating buffers: va() has only two, and one line of the panel
	// formats up to three numbers (value, "+" and the gain)
	static char	buffers[4][32];
	static int	current;
	char		*text = buffers[current];

	current = ( current + 1 ) & 3;
	if ( value >= 1000 ) {
		Com_sprintf( text, sizeof( buffers[0] ), "%i %03i", value / 1000, value % 1000 );
	} else {
		Com_sprintf( text, sizeof( buffers[0] ), "%i", value );
	}
	return text;
}


/*
=================
Hangar_FaceYaw

points the whole robot (legs, torso, head) at one angle, without the swing
the player preview normally uses
=================
*/
static void Hangar_FaceYaw( playerInfo_t *player, float yaw ) {
	player->viewAngles[YAW] = yaw;
	player->moveAngles[YAW] = yaw;
	player->legs.yawAngle = yaw;
	player->torso.yawAngle = yaw;
	player->legs.yawing = qfalse;
	player->torso.yawing = qfalse;
}


/*
=================
Hangar_SetPlayer
=================
*/
static void Hangar_SetPlayer( playerInfo_t *player, int type ) {
	vec3_t	angles;

	VectorClear( angles );
	angles[YAW] = 150;
	memset( player, 0, sizeof( *player ) );
	UI_PlayerInfo_SetModel( player, (char *)bg_robots[type].model );
	UI_PlayerInfo_SetInfo( player, LEGS_IDLE, TORSO_STAND, angles, angles, WP_NONE, qfalse );
}


/*
=================
Hangar_SelectedRobot

NULL: an empty bay is selected (the shop)
=================
*/
static accountRobot_t *Hangar_SelectedRobot( void ) {
	if ( hangarMenuInfo.selected < uiAccount.numRobots ) {
		return &uiAccount.robots[hangarMenuInfo.selected];
	}
	return NULL;
}


/*
=================
Hangar_SlotWeapon

weapon of the pilot in a slot of the selected robot, NULL when empty
=================
*/
static accountWeapon_t *Hangar_SlotWeapon( int slot ) {
	accountRobot_t	*robot = Hangar_SelectedRobot();

	if ( !robot || slot < 0 || slot >= bg_robots[robot->type].numSlots ) {
		return NULL;
	}
	if ( robot->slots[slot] < 0 || robot->slots[slot] >= uiAccount.numWeapons ) {
		return NULL;
	}
	return &uiAccount.weapons[robot->slots[slot]];
}


/*
=================
Hangar_SyncModels

loads the models of the robots that came or changed and puts the weapons of
their slots on them
=================
*/
static void Hangar_SyncModels( void ) {
	int		index, slot;

	for ( index = 0; index < MAX_BAYS; index++ ) {
		int		type = index < uiAccount.numRobots ? uiAccount.robots[index].type : -1;
		if ( type >= 0 && hangarMenuInfo.loadedType[index] != type ) {
			Hangar_SetPlayer( &hangarMenuInfo.players[index], type );
			hangarMenuInfo.loadedType[index] = type;
		}
		if ( type < 0 ) {
			hangarMenuInfo.loadedType[index] = -1;
			continue;
		}
		for ( slot = 0; slot < ROBOT_MAX_SLOTS; slot++ ) {
			int		weapon = uiAccount.robots[index].slots[slot];
			hangarMenuInfo.players[index].slotModels[slot] = ( weapon >= 0 && weapon < uiAccount.numWeapons )
				? hangarMenuInfo.weaponModels[uiAccount.weapons[weapon].type] : 0;
		}
	}
	if ( hangarMenuInfo.shopLoaded != hangarMenuInfo.shopType ) {
		Hangar_SetPlayer( &hangarMenuInfo.shopPlayer, hangarMenuInfo.shopType );
		hangarMenuInfo.shopLoaded = hangarMenuInfo.shopType;
		for ( slot = 0; slot < bg_robots[hangarMenuInfo.shopType].numSlots; slot++ ) {
			hangarMenuInfo.shopPlayer.slotModels[slot] = hangarMenuInfo.weaponModels[bg_robots[hangarMenuInfo.shopType].defaultWeapon[slot]];
		}
	}
}


/*
=================
Hangar_Show
=================
*/
static void Hangar_Show( hangarButton_t *button, qboolean show ) {
	if ( show ) {
		button->item.generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	} else {
		button->item.generic.flags |= QMF_HIDDEN | QMF_INACTIVE;
	}
}


/*
=================
Hangar_SetButton
=================
*/
static void Hangar_SetButton( hangarButton_t *button, const char *label, const char *detail, qboolean enabled ) {
	Q_strncpyz( button->label, label, sizeof( button->label ) );
	Q_strncpyz( button->detail, detail, sizeof( button->detail ) );
	button->enabled = enabled;
	Hangar_Show( button, qtrue );
}


/*
=================
Hangar_PlaceButton

moves a button inside the panel (its row depends on the robot)
=================
*/
static void Hangar_PlaceButton( hangarButton_t *button, int y ) {
	button->item.generic.y = y;
	button->item.generic.top = y;
	button->item.generic.bottom = y + button->item.height;
}


/*
=================
Hangar_DetailsY / Hangar_ActionsY

rows under the slots: the weapon of the selected slot, then the actions
=================
*/
static int Hangar_DetailsY( int numSlots ) {
	return SLOTS_Y + numSlots * ROW_STEP + 2;
}

static int Hangar_ActionsY( int numSlots ) {
	return Hangar_DetailsY( numSlots ) + 3 * LINE + 8;
}


/*
=================
Hangar_BuildChoices

the weapon list of the selected slot: the weapons of its class the pilot owns
(this slot's own first), then the ones to buy, and "empty" when it has one
=================
*/
static void Hangar_BuildChoices( void ) {
	accountRobot_t	*robot = Hangar_SelectedRobot();
	int				weaponClass, index, count = 0, current;

	hangarMenuInfo.numChoices = 0;
	if ( !robot ) {
		return;
	}
	weaponClass = bg_robots[robot->type].slotClass[hangarMenuInfo.selectedSlot];
	current = robot->slots[hangarMenuInfo.selectedSlot];
	for ( index = 0; index < uiAccount.numWeapons && count < MAX_CHOICES - RWEAPON_COUNT / WCLASS_COUNT - 1; index++ ) {
		if ( index == current || bg_robotWeapons[uiAccount.weapons[index].type].weaponClass != weaponClass ) {
			continue;
		}
		hangarMenuInfo.choiceKind[count] = CHOICE_OWN;
		hangarMenuInfo.choiceValue[count] = index;
		count++;
	}
	for ( index = 0; index < RWEAPON_COUNT && count < MAX_CHOICES; index++ ) {
		if ( bg_robotWeapons[index].weaponClass != weaponClass ) {
			continue;
		}
		hangarMenuInfo.choiceKind[count] = CHOICE_BUY;
		hangarMenuInfo.choiceValue[count] = index;
		count++;
	}
	if ( current >= 0 && count < MAX_CHOICES ) {
		hangarMenuInfo.choiceKind[count] = CHOICE_EMPTY;
		hangarMenuInfo.choiceValue[count] = 0;
		count++;
	}
	hangarMenuInfo.numChoices = count;
}


/*
=================
Hangar_UpdateButtons
=================
*/
static void Hangar_UpdateButtons( void ) {
	accountRobot_t	*robot = Hangar_SelectedRobot();
	accountWeapon_t	*weapon;
	int				now = UI_AccountTime();
	int				index, numSlots, actionsY;

	Hangar_SetButton( &hangarMenuInfo.autoRotate,
		trap_Cvar_VariableValue( "ui_hangarAutoRotate" ) ? "AUTO ROTATE: ON" : "AUTO ROTATE: OFF", "", qtrue );
	Hangar_SetButton( &hangarMenuInfo.light,
		trap_Cvar_VariableValue( "ui_hangarLight" ) ? "LIGHT: ON" : "LIGHT: OFF", "", qtrue );

	numSlots = bg_robots[robot ? robot->type : hangarMenuInfo.shopType].numSlots;
	actionsY = Hangar_ActionsY( numSlots );
	for ( index = 0; index < ROBOT_MAX_SLOTS; index++ ) {
		Hangar_Show( &hangarMenuInfo.slots[index], qfalse );
	}
	for ( index = 0; index < MAX_CHOICES; index++ ) {
		Hangar_Show( &hangarMenuInfo.choices[index], qfalse );
	}
	Hangar_Show( &hangarMenuInfo.chooseBack, qfalse );
	Hangar_Show( &hangarMenuInfo.upgradeRobot, qfalse );
	Hangar_Show( &hangarMenuInfo.upgradeWeapon, qfalse );
	Hangar_Show( &hangarMenuInfo.changeWeapon, qfalse );
	Hangar_Show( &hangarMenuInfo.buy, qfalse );
	Hangar_Show( &hangarMenuInfo.shopPrev, qfalse );
	Hangar_Show( &hangarMenuInfo.shopNext, qfalse );

	if ( !robot ) {
		// the shop: the robot comes with the weapons drawn on it
		const robotInfo_t	*info = &bg_robots[hangarMenuInfo.shopType];
		hangarMenuInfo.choosing = qfalse;
		Hangar_SetButton( &hangarMenuInfo.buy, va( "BUY  %s CR", Hangar_Number( info->price ) ), "",
			uiAccount.currency >= info->price );
		Hangar_SetButton( &hangarMenuInfo.shopPrev, "<", "", qtrue );
		Hangar_SetButton( &hangarMenuInfo.shopNext, ">", "", qtrue );
		Hangar_PlaceButton( &hangarMenuInfo.shopPrev, actionsY );
		Hangar_PlaceButton( &hangarMenuInfo.buy, actionsY );
		Hangar_PlaceButton( &hangarMenuInfo.shopNext, actionsY );
		return;
	}

	if ( hangarMenuInfo.selectedSlot >= numSlots ) {
		hangarMenuInfo.selectedSlot = 0;
	}

	// the slots
	for ( index = 0; index < numSlots; index++ ) {
		accountWeapon_t	*slotWeapon = Hangar_SlotWeapon( index );
		const char		*className = bg_weaponClassNames[bg_robots[robot->type].slotClass[index]];
		if ( slotWeapon ) {
			Hangar_SetButton( &hangarMenuInfo.slots[index],
				va( "%c  %s  LV %i", className[0], bg_robotWeapons[slotWeapon->type].title, slotWeapon->level ),
				slotWeapon->upgrading ? "UPGRADING" : bg_robotWeapons[slotWeapon->type].description, qtrue );
		} else {
			Hangar_SetButton( &hangarMenuInfo.slots[index], va( "%c  EMPTY", className[0] ), va( "%s slot", className ), qfalse );
		}
		hangarMenuInfo.slots[index].marked = ( index == hangarMenuInfo.selectedSlot );
	}

	if ( hangarMenuInfo.choosing ) {
		// the weapon list in place of the details and actions
		Hangar_BuildChoices();
		for ( index = 0; index < hangarMenuInfo.numChoices; index++ ) {
			int		value = hangarMenuInfo.choiceValue[index];
			Hangar_PlaceButton( &hangarMenuInfo.choices[index], Hangar_DetailsY( numSlots ) + 18 + index * ( BUTTON_H + 2 ) );
			if ( hangarMenuInfo.choiceKind[index] == CHOICE_OWN ) {
				int		slot, carrier = UI_AccountWeaponRobot( value, &slot );
				Hangar_SetButton( &hangarMenuInfo.choices[index],
					va( "%s  LV %i", bg_robotWeapons[uiAccount.weapons[value].type].title, uiAccount.weapons[value].level ),
					carrier >= 0 ? va( "on %s (bay %i)", bg_robots[uiAccount.robots[carrier].type].title, carrier + 1 ) : "in store",
					qtrue );
			} else if ( hangarMenuInfo.choiceKind[index] == CHOICE_BUY ) {
				Hangar_SetButton( &hangarMenuInfo.choices[index], va( "BUY %s", bg_robotWeapons[value].title ),
					va( "%s CR", Hangar_Number( bg_robotWeapons[value].price ) ), uiAccount.currency >= bg_robotWeapons[value].price );
			} else {
				Hangar_SetButton( &hangarMenuInfo.choices[index], "TAKE OUT", "slot stays empty", qtrue );
			}
		}
		Hangar_SetButton( &hangarMenuInfo.chooseBack, "BACK", "", qtrue );
		Hangar_PlaceButton( &hangarMenuInfo.chooseBack, Hangar_DetailsY( numSlots ) - 1 );
		return;
	}

	// the actions: robot, weapon of the selected slot, change it
	if ( robot->upgrading ) {
		int		cost = BG_SpeedUpCost( robot->upgradeEnd - now );
		Hangar_SetButton( &hangarMenuInfo.upgradeRobot, va( "FINISH ROBOT  %s", Hangar_Duration( robot->upgradeEnd - now ) ),
			va( "%i CR", cost ), uiAccount.currency >= cost );
	} else if ( robot->level < ROBOT_LEVELS ) {
		int		cost = BG_RobotUpgradeCost( robot->level );
		Hangar_SetButton( &hangarMenuInfo.upgradeRobot, "UPGRADE ROBOT",
			va( "%i PTS  %s", cost, Hangar_Duration( BG_UpgradeTime( robot->level ) ) ), uiAccount.points >= cost );
	}
	weapon = Hangar_SlotWeapon( hangarMenuInfo.selectedSlot );
	if ( weapon && weapon->upgrading ) {
		int		cost = BG_SpeedUpCost( weapon->upgradeEnd - now );
		Hangar_SetButton( &hangarMenuInfo.upgradeWeapon, va( "FINISH WEAPON  %s", Hangar_Duration( weapon->upgradeEnd - now ) ),
			va( "%i CR", cost ), uiAccount.currency >= cost );
	} else if ( weapon && weapon->level < WEAPON_LEVELS ) {
		int		cost = BG_WeaponUpgradeCost( weapon->type, weapon->level );
		Hangar_SetButton( &hangarMenuInfo.upgradeWeapon, "UPGRADE WEAPON",
			va( "%i PTS  %s", cost, Hangar_Duration( BG_UpgradeTime( weapon->level ) ) ), uiAccount.points >= cost );
	}
	Hangar_SetButton( &hangarMenuInfo.changeWeapon, weapon ? "CHANGE WEAPON" : "PUT A WEAPON",
		va( "%s slot %i", bg_weaponClassNames[bg_robots[robot->type].slotClass[hangarMenuInfo.selectedSlot]],
			hangarMenuInfo.selectedSlot + 1 ), qtrue );
	Hangar_PlaceButton( &hangarMenuInfo.upgradeRobot, actionsY );
	Hangar_PlaceButton( &hangarMenuInfo.upgradeWeapon, actionsY + ROW_STEP + 2 );
	Hangar_PlaceButton( &hangarMenuInfo.changeWeapon, actionsY + 2 * ( ROW_STEP + 2 ) );
}


/*
=================
Hangar_Text
=================
*/
static void Hangar_Text( int x, int y, const char *text, int align, float *color ) {
	UI_DrawString( x, y, text, align | UI_TINYFONT, color );
}


/*
=================
Hangar_Rule

thin separator line across the panel
=================
*/
static void Hangar_Rule( int y ) {
	UI_FillRect( PANEL_X + PANEL_PAD, y, PANEL_W - 2 * PANEL_PAD, 1, hangarRuleColor );
}


/*
=================
Hangar_DrawLine

a characteristic: name on the left, value and the gain of the next level on the right
=================
*/
static void Hangar_DrawLine( int y, const char *name, const char *value, const char *next ) {
	Hangar_Text( PANEL_X + PANEL_PAD, y, name, UI_LEFT, hangarDimColor );
	Hangar_Text( PANEL_X + 168, y, value, UI_RIGHT, hangarTextColor );
	if ( next && next[0] ) {
		Hangar_Text( PANEL_X + PANEL_W - PANEL_PAD, y, next, UI_RIGHT, hangarGreenColor );
	}
}


/*
=================
Hangar_DrawWeaponLines

damage, rate and range of a weapon at its level, with the gain of the next one
=================
*/
static void Hangar_DrawWeaponLines( int y, int type, int level, qboolean showNext ) {
	int		next = level < WEAPON_LEVELS ? level + 1 : level;
	int		value;

	showNext = showNext && next > level;
	value = BG_WeaponDamage( type, level );
	Hangar_DrawLine( y, "DAMAGE", Hangar_Number( value ),
		showNext ? va( "+%s", Hangar_Number( BG_WeaponDamage( type, next ) - value ) ) : "" );
	y += LINE;
	value = BG_WeaponInterval( type, level );
	Hangar_DrawLine( y, "RATE", va( "%i.%i /s", 1000 / value, ( 10000 / value ) % 10 ),
		showNext ? va( "%i.%i /s", 1000 / BG_WeaponInterval( type, next ), ( 10000 / BG_WeaponInterval( type, next ) ) % 10 ) : "" );
	y += LINE;
	Hangar_DrawLine( y, "RANGE", va( "%i m", bg_robotWeapons[type].range ), "" );
}


/*
=================
Hangar_DrawPanel
=================
*/
static void Hangar_DrawPanel( void ) {
	accountRobot_t		*robot = Hangar_SelectedRobot();
	const robotInfo_t	*info;
	accountWeapon_t		*weapon;
	int					type, level, y, next, value, slot;
	qboolean			showNext;

	if ( robot ) {
		type = robot->type;
		level = robot->level;
	} else {
		type = hangarMenuInfo.shopType;
		level = 1;
	}
	info = &bg_robots[type];
	showNext = robot && !robot->upgrading;

	UI_FillRect( PANEL_X, PANEL_Y, PANEL_W, PANEL_H, hangarPanelColor );
	UI_FillRect( PANEL_X, PANEL_Y, PANEL_W, 2, hangarAccentColor );

	y = PANEL_Y + 8;
	UI_DrawString( PANEL_X + PANEL_PAD, y, info->title, UI_LEFT | UI_SMALLFONT, hangarAccentColor );
	Hangar_Text( PANEL_X + PANEL_W - PANEL_PAD, y + 3, robot ? va( "LEVEL %i / %i", level, ROBOT_LEVELS ) : "IN SHOP",
		UI_RIGHT, hangarAccentColor );
	y += 18;
	Hangar_Text( PANEL_X + PANEL_PAD, y, info->description, UI_LEFT, hangarDimColor );
	y += LINE + 4;
	Hangar_Rule( y - 3 );

	next = level < ROBOT_LEVELS ? level + 1 : level;
	value = BG_RobotStructure( type, level );
	Hangar_DrawLine( y, "STRUCTURE", Hangar_Number( value ),
		showNext && next > level ? va( "+%s", Hangar_Number( BG_RobotStructure( type, next ) - value ) ) : "" );
	y += LINE;
	value = BG_RobotArmor( type, level );
	Hangar_DrawLine( y, "ARMOR / HIT", value ? va( "%i", value ) : "-",
		showNext && next > level && value ? va( "+%i", BG_RobotArmor( type, next ) - value ) : "" );
	y += LINE;
	value = BG_RobotShield( type, level );
	Hangar_DrawLine( y, "SHIELD", value ? Hangar_Number( value ) : "-",
		showNext && next > level && value ? va( "+%s", Hangar_Number( BG_RobotShield( type, next ) - value ) ) : "" );
	y += LINE;
	value = BG_RobotSpeed( type, level );
	Hangar_DrawLine( y, "SPEED", va( "%i km/h", value ),
		showNext && next > level && BG_RobotSpeed( type, next ) > value ? va( "+%i", BG_RobotSpeed( type, next ) - value ) : "" );
	y += LINE + 3;
	Hangar_Rule( y - 3 );
	// y is SLOTS_Y here

	if ( !robot ) {
		// the shop: slots with the weapons the robot comes with
		for ( slot = 0; slot < info->numSlots; slot++ ) {
			Hangar_Text( PANEL_X + PANEL_PAD, SLOTS_Y + slot * ROW_STEP + 3,
				va( "%s SLOT", bg_weaponClassNames[info->slotClass[slot]] ), UI_LEFT, hangarDimColor );
			Hangar_Text( PANEL_X + PANEL_W - PANEL_PAD, SLOTS_Y + slot * ROW_STEP + 3,
				bg_robotWeapons[info->defaultWeapon[slot]].title, UI_RIGHT, hangarTextColor );
		}
		Hangar_Text( PANEL_X + PANEL_PAD, Hangar_DetailsY( info->numSlots ) + 2, "Comes with these weapons.", UI_LEFT, hangarDimColor );
		Hangar_Text( PANEL_X + PANEL_PAD, Hangar_DetailsY( info->numSlots ) + 2 + LINE, "Empty bay: buy a robot here.", UI_LEFT, hangarDimColor );
	} else if ( hangarMenuInfo.choosing ) {
		Hangar_Text( PANEL_X + PANEL_PAD, Hangar_DetailsY( info->numSlots ) + 2,
			va( "%s WEAPONS FOR SLOT %i", bg_weaponClassNames[info->slotClass[hangarMenuInfo.selectedSlot]],
				hangarMenuInfo.selectedSlot + 1 ), UI_LEFT, hangarAccentColor );
	} else {
		// the weapon of the selected slot
		y = Hangar_DetailsY( info->numSlots );
		weapon = Hangar_SlotWeapon( hangarMenuInfo.selectedSlot );
		if ( weapon ) {
			Hangar_DrawWeaponLines( y, weapon->type, weapon->level, !weapon->upgrading );
		} else {
			Hangar_Text( PANEL_X + PANEL_PAD, y, "Empty slot: put a weapon in it.", UI_LEFT, hangarDimColor );
		}
		Hangar_Rule( Hangar_ActionsY( info->numSlots ) - 5 );
	}
	if ( hangarMenuInfo.message[0] ) {
		Hangar_Text( PANEL_X + PANEL_W / 2, PANEL_Y + PANEL_H - 14, hangarMenuInfo.message, UI_CENTER, color_red );
	}
}


/*
=================
Hangar_DrawButton
=================
*/
static void Hangar_DrawButton( void *self ) {
	hangarButton_t	*button = (hangarButton_t *)self;
	menucommon_s	*item = &button->item.generic;
	qboolean		focus = ( item->parent->cursor == item->menuPosition );
	float			*color, *fill;
	int				textY = item->y + ( button->item.height - TINYFONT_HEIGHT ) / 2;

	if ( focus ) {
		fill = hangarButtonFocusColor;
	} else {
		fill = button->marked ? hangarButtonMarkedColor : hangarButtonColor;
	}
	UI_FillRect( item->x, item->y, button->item.width, button->item.height, fill );
	UI_DrawRect( item->x, item->y, button->item.width, button->item.height,
		( focus || button->marked ) ? hangarAccentColor : hangarBorderColor );
	if ( !button->enabled ) {
		color = hangarDimColor;
	} else {
		color = focus ? colorWhite : hangarAccentColor;
	}
	if ( button->detail[0] ) {
		Hangar_Text( item->x + 6, textY, button->label, UI_LEFT, color );
		Hangar_Text( item->x + button->item.width - 6, textY, button->detail, UI_RIGHT, button->enabled ? hangarTextColor : hangarDimColor );
	} else {
		Hangar_Text( item->x + button->item.width / 2, textY, button->label, UI_CENTER, color );
	}
}


/*
=================
Light controls

WALKER (28.11.2026): sliders for the key and ambient light (cvars ui_hangarKey 0..2,
ui_hangarAmbient 0..1, read by the renderer, tr_light.c) and the light direction dial
=================
*/
static float Hangar_SliderShare( int control ) {
	if ( control == LIGHT_KEY ) {
		return Com_Clamp( 0, 1, trap_Cvar_VariableValue( "ui_hangarKey" ) / 2 );
	}
	return Com_Clamp( 0, 1, trap_Cvar_VariableValue( "ui_hangarAmbient" ) );
}

static void Hangar_SetSlider( int control, float share ) {
	share = Com_Clamp( 0, 1, share );
	if ( control == LIGHT_KEY ) {
		trap_Cvar_SetValue( "ui_hangarKey", share * 2 );
	} else {
		trap_Cvar_SetValue( "ui_hangarAmbient", share );
	}
}

static void Hangar_DrawSlider( int control, int y, const char *label ) {
	float	share = Hangar_SliderShare( control );
	float	*color = hangarMenuInfo.lightControl == control ? colorWhite : hangarAccentColor;

	Hangar_Text( LIGHTUI_X, y - 11, label, UI_LEFT, hangarTextColor );
	Hangar_Text( LIGHTUI_X + SLIDER_W + 22, y - 3, va( "%i%%", (int)( share * ( control == LIGHT_KEY ? 200 : 100 ) + 0.5f ) ),
		UI_RIGHT, hangarDimColor );
	UI_FillRect( LIGHTUI_X, y, SLIDER_W, SLIDER_H, hangarBarColor );
	UI_FillRect( LIGHTUI_X, y, SLIDER_W * share, SLIDER_H, color );
	UI_DrawRect( LIGHTUI_X, y, SLIDER_W, SLIDER_H, hangarBorderColor );
	UI_FillRect( LIGHTUI_X + SLIDER_W * share - 2, y - 2, 4, SLIDER_H + 4, colorWhite );
}

static void Hangar_Dot( float x, float y, float size, float *color ) {
	UI_FillRect( x - size / 2, y - size / 2, size, size, color );
}

static void Hangar_Segment( float x0, float y0, float x1, float y1, float *color ) {
	float	length = sqrt( ( x1 - x0 ) * ( x1 - x0 ) + ( y1 - y0 ) * ( y1 - y0 ) );
	int		steps = (int)length + 1, step;

	for ( step = 0 ; step <= steps ; step++ ) {
		Hangar_Dot( x0 + ( x1 - x0 ) * step / steps, y0 + ( y1 - y0 ) * step / steps, 1.5f, color );
	}
}

/*
the dial: seen from above, the camera at the bottom, the robot in the middle; the
light at the yaw angle, as far from the middle as its pitch is low (overhead - the
middle, 25 degrees - nearly the rim)
*/
static void Hangar_LightPoint( float *x, float *y ) {
	float	reach = cos( DEG2RAD( hangarMenuInfo.lightPitch ) ) * DIAL_R;

	*x = DIAL_X + sin( DEG2RAD( hangarMenuInfo.lightYaw ) ) * reach;
	*y = DIAL_Y + cos( DEG2RAD( hangarMenuInfo.lightYaw ) ) * reach;
}

static void Hangar_SetLightFromDial( float x, float y ) {
	float	dx = x - DIAL_X, dy = y - DIAL_Y;
	float	share = sqrt( dx * dx + dy * dy ) / DIAL_R;

	if ( share > 1 ) {
		share = 1;
	}
	if ( dx * dx + dy * dy > 1 ) {
		hangarMenuInfo.lightYaw = AngleNormalize180( RAD2DEG( atan2( dx, dy ) ) );
	}
	hangarMenuInfo.lightPitch = Com_Clamp( LIGHT_MIN_PITCH, LIGHT_MAX_PITCH, RAD2DEG( atan2( sqrt( 1 - share * share ), share ) ) );	// acos: not in the QVM library
}

static void Hangar_DrawLightControls( void ) {
	static vec4_t	sunColor = { 1.0f, 0.92f, 0.45f, 1.0f };
	float			lightX, lightY, dx, dy, length;
	int				step;

	if ( !trap_Cvar_VariableValue( "ui_hangarLight" ) ) {
		return;
	}
	UI_FillRect( LIGHTUI_X - 5, LIGHTUI_Y - 4, LIGHTUI_W + 10, DIAL_Y + DIAL_R + 18 - LIGHTUI_Y, hangarPanelColor );
	Hangar_DrawSlider( LIGHT_KEY, SLIDER_KEY_Y, "KEY LIGHT" );
	Hangar_DrawSlider( LIGHT_AMBIENT, SLIDER_AMBIENT_Y, "AMBIENT" );
	Hangar_Text( LIGHTUI_X, DIAL_Y - DIAL_R - 14, "LIGHT FROM", UI_LEFT, hangarTextColor );
	for ( step = 0 ; step < 48 ; step++ ) {
		float	angle = 2 * M_PI * step / 48;
		Hangar_Dot( DIAL_X + cos( angle ) * DIAL_R, DIAL_Y + sin( angle ) * DIAL_R, 2,
			hangarMenuInfo.lightControl == LIGHT_DIAL ? colorWhite : hangarBorderColor );
	}
	Hangar_Dot( DIAL_X, DIAL_Y, 4, hangarDimColor );						// the robot
	Hangar_Text( DIAL_X, DIAL_Y + DIAL_R + 3, "CAMERA", UI_CENTER, hangarDimColor );
	Hangar_LightPoint( &lightX, &lightY );
	dx = DIAL_X - lightX; dy = DIAL_Y - lightY;
	length = sqrt( dx * dx + dy * dy );
	if ( length > 6 ) {
		// the arrow from the light to the robot, the head at the robot
		float	ux = dx / length, uy = dy / length;
		float	tipX = DIAL_X - ux * 3, tipY = DIAL_Y - uy * 3;
		Hangar_Segment( lightX, lightY, tipX, tipY, sunColor );
		Hangar_Segment( tipX, tipY, tipX - ux * 7 + uy * 4, tipY - uy * 7 - ux * 4, sunColor );
		Hangar_Segment( tipX, tipY, tipX - ux * 7 - uy * 4, tipY - uy * 7 + ux * 4, sunColor );
	}
	Hangar_Dot( lightX, lightY, 6, sunColor );
	Hangar_Text( LIGHTUI_X + LIGHTUI_W, DIAL_Y - DIAL_R - 14, va( "%i DEG", (int)hangarMenuInfo.lightPitch ), UI_RIGHT, hangarDimColor );
}

/*
a click on a light control: qtrue - taken (no turning of the robot)
*/
static qboolean Hangar_LightControlClick( void ) {
	float	dx, dy;

	if ( !trap_Cvar_VariableValue( "ui_hangarLight" ) ) {
		return qfalse;
	}
	if ( uis.cursorx >= LIGHTUI_X - 4 && uis.cursorx <= LIGHTUI_X + SLIDER_W + 4 ) {
		if ( uis.cursory >= SLIDER_KEY_Y - 5 && uis.cursory <= SLIDER_KEY_Y + SLIDER_H + 5 ) {
			hangarMenuInfo.lightControl = LIGHT_KEY;
			return qtrue;
		}
		if ( uis.cursory >= SLIDER_AMBIENT_Y - 5 && uis.cursory <= SLIDER_AMBIENT_Y + SLIDER_H + 5 ) {
			hangarMenuInfo.lightControl = LIGHT_AMBIENT;
			return qtrue;
		}
	}
	dx = uis.cursorx - DIAL_X; dy = uis.cursory - DIAL_Y;
	if ( dx * dx + dy * dy <= ( DIAL_R + 5 ) * ( DIAL_R + 5 ) ) {
		hangarMenuInfo.lightControl = LIGHT_DIAL;
		return qtrue;
	}
	return qfalse;
}

static void Hangar_UpdateLightControl( void ) {
	if ( hangarMenuInfo.lightControl == LIGHT_NONE ) {
		return;
	}
	if ( !trap_Key_IsDown( K_MOUSE1 ) ) {
		hangarMenuInfo.lightControl = LIGHT_NONE;
		return;
	}
	if ( hangarMenuInfo.lightControl == LIGHT_DIAL ) {
		Hangar_SetLightFromDial( uis.cursorx, uis.cursory );
	} else {
		Hangar_SetSlider( hangarMenuInfo.lightControl, ( uis.cursorx - LIGHTUI_X ) / (float)SLIDER_W );
	}
}


/*
=================
Hangar_DrawPreview
=================
*/
static void Hangar_DrawPreview( void *self ) {
	playerInfo_t	*player;

	player = Hangar_SelectedRobot() ? &hangarMenuInfo.players[hangarMenuInfo.selected] : &hangarMenuInfo.shopPlayer;
	Hangar_FaceYaw( player, hangarMenuInfo.previewYaw );
	uiPlayerOrbit = qtrue;
	uiPlayerOrbitPitch = hangarMenuInfo.previewPitch;
	uiPlayerOrbitRoll = hangarMenuInfo.previewRoll;
	uiPlayerOrbitZoom = hangarMenuInfo.previewZoom;
	uiPlayerStudioLight = trap_Cvar_VariableValue( "ui_hangarLight" ) != 0;
	uiPlayerLightYaw = hangarMenuInfo.lightYaw;
	uiPlayerLightPitch = hangarMenuInfo.lightPitch;
	UI_DrawPlayer( PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H - 24, player, uis.realtime / 2 );
	uiPlayerOrbit = qfalse;
	uiPlayerStudioLight = qfalse;
	Hangar_DrawLightControls();
}


/*
=================
Hangar_DrawBay
=================
*/
static void Hangar_DrawBay( void *self ) {
	menubitmap_s	*bay;
	int				index;
	qboolean		focus;
	float			*color;
	char			label[64];

	bay = (menubitmap_s *)self;
	index = bay->generic.id - ID_BAY0;
	focus = ( bay->generic.parent->cursor == bay->generic.menuPosition );

	UI_FillRect( bay->generic.x, bay->generic.y, bay->width, bay->height, hangarPanelColor );
	if ( index < uiAccount.numRobots ) {
		accountRobot_t	*robot = &uiAccount.robots[index];
		Hangar_FaceYaw( &hangarMenuInfo.players[index], hangarMenuInfo.bayYaw + index * 25 );
		UI_DrawPlayer( bay->generic.x + 4, bay->generic.y + 2, bay->width - 8, bay->height - 16, &hangarMenuInfo.players[index], uis.realtime / 2 );
		Com_sprintf( label, sizeof( label ), "%s  LV %i", bg_robots[robot->type].title, robot->level );
		if ( !UI_AccountRobotReady( robot ) ) {
			Hangar_Text( bay->generic.x + bay->width / 2, bay->generic.y + 4, robot->upgrading ? "UPGRADING" : "NOT READY",
				UI_CENTER, color_yellow );
		}
	} else {
		Q_strncpyz( label, "+ BUY", sizeof( label ) );
	}

	UI_DrawHandlePic( bay->generic.x, bay->generic.y, bay->width, bay->height,
		index == hangarMenuInfo.selected ? hangarMenuInfo.baySelectedShader : hangarMenuInfo.bayShader );

	color = ( index == hangarMenuInfo.selected ) ? hangarAccentColor : ( focus ? colorWhite : hangarDimColor );
	Hangar_Text( bay->generic.x + bay->width / 2, bay->generic.y + bay->height - 15, label, UI_CENTER, color );
}


/*
=================
Hangar_Draw
=================
*/
static void Hangar_Draw( void ) {
	int		now;
	float	seconds;

	UI_AccountUpdate();		// upgrades whose time is up
	Hangar_SyncModels();
	Hangar_UpdateButtons();

	now = uis.realtime;
	seconds = hangarMenuInfo.lastTime ? ( now - hangarMenuInfo.lastTime ) * 0.001f : 0;
	hangarMenuInfo.lastTime = now;

	if ( hangarMenuInfo.dragging && !trap_Key_IsDown( K_MOUSE1 ) ) {
		hangarMenuInfo.dragging = qfalse;
	}
	if ( hangarMenuInfo.rolling && !trap_Key_IsDown( K_MOUSE2 ) ) {
		hangarMenuInfo.rolling = qfalse;
	}
	if ( hangarMenuInfo.lighting && !trap_Key_IsDown( K_MOUSE3 ) && !trap_Key_IsDown( K_MOUSE1 ) ) {
		hangarMenuInfo.lighting = qfalse;
	}
	Hangar_UpdateLightControl();
	if ( hangarMenuInfo.lighting ) {
		// the light goes the way the mouse does; not lower than 25 degrees (the
		// shadow volumes must reach the floor) and not past the top
		hangarMenuInfo.lightYaw = AngleNormalize180( hangarMenuInfo.lightYaw + ( uis.cursorx - hangarMenuInfo.dragLastX ) * DRAG_SPEED );
		hangarMenuInfo.lightPitch -= ( uis.cursory - hangarMenuInfo.dragLastY ) * DRAG_SPEED;
		if ( hangarMenuInfo.lightPitch < LIGHT_MIN_PITCH ) {
			hangarMenuInfo.lightPitch = LIGHT_MIN_PITCH;
		} else if ( hangarMenuInfo.lightPitch > LIGHT_MAX_PITCH ) {
			hangarMenuInfo.lightPitch = LIGHT_MAX_PITCH;
		}
	} else if ( hangarMenuInfo.dragging ) {
		hangarMenuInfo.previewYaw += ( uis.cursorx - hangarMenuInfo.dragLastX ) * DRAG_SPEED;
		hangarMenuInfo.previewPitch += ( uis.cursory - hangarMenuInfo.dragLastY ) * DRAG_SPEED;
	} else if ( hangarMenuInfo.rolling ) {
		hangarMenuInfo.previewRoll += ( uis.cursorx - hangarMenuInfo.dragLastX ) * DRAG_SPEED;
	} else if ( trap_Cvar_VariableValue( "ui_hangarAutoRotate" ) ) {
		hangarMenuInfo.previewYaw += TURN_SPEED * seconds;
	}
	hangarMenuInfo.dragLastX = uis.cursorx;
	hangarMenuInfo.dragLastY = uis.cursory;
	hangarMenuInfo.previewYaw = AngleMod( hangarMenuInfo.previewYaw );
	hangarMenuInfo.previewPitch = AngleNormalize180( hangarMenuInfo.previewPitch );
	hangarMenuInfo.previewRoll = AngleNormalize180( hangarMenuInfo.previewRoll );
	if ( trap_Cvar_VariableValue( "ui_hangarAutoRotate" ) ) {
		hangarMenuInfo.bayYaw = AngleMod( hangarMenuInfo.bayYaw + TURN_SPEED * 0.5f * seconds );
	}

	UI_DrawHandlePic( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, hangarMenuInfo.backgroundShader );

	// top bar: pilot, money
	UI_FillRect( 0, 0, SCREEN_WIDTH, TOPBAR_H, hangarBarColor );
	UI_FillRect( 0, TOPBAR_H, SCREEN_WIDTH, 1, hangarRuleColor );
	Hangar_Text( 10, 5, uiAccount.nick, UI_LEFT, hangarTextColor );
	Hangar_Text( 10 + ( strlen( uiAccount.nick ) + 2 ) * TINYFONT_WIDTH, 5, va( "PILOT LEVEL %i", uiAccount.level ), UI_LEFT, hangarDimColor );
	Hangar_Text( 630, 5, va( "%s PTS", Hangar_Number( uiAccount.points ) ), UI_RIGHT, hangarGreenColor );
	Hangar_Text( 630 - 14 * TINYFONT_WIDTH, 5, va( "%s CR", Hangar_Number( uiAccount.currency ) ), UI_RIGHT, hangarMoneyColor );

	Hangar_DrawPanel();

	// bottom bar
	UI_FillRect( 0, BOTTOM_Y, SCREEN_WIDTH, SCREEN_HEIGHT - BOTTOM_Y, hangarBarColor );
	UI_FillRect( 0, BOTTOM_Y, SCREEN_WIDTH, 1, hangarRuleColor );
	Hangar_Text( 630, BOTTOM_Y + 10, va( "%i of %i robots ready for battle", UI_AccountAvailableRobots(), uiAccount.numRobots ),
		UI_RIGHT, hangarDimColor );
	Hangar_Text( 630, BOTTOM_Y + 26, "LMB: turn  RMB: roll  MMB / SHIFT+LMB / dial: light  wheel: zoom", UI_RIGHT, hangarDimColor );

	Menu_Draw( &hangarMenuInfo.menu );
}


/*
=================
Hangar_Key
=================
*/
static sfxHandle_t Hangar_Key( int key ) {
	switch ( key ) {
	case K_MOUSE1:
	case K_MOUSE2:
	case K_MOUSE3:
		if ( uis.cursorx >= PREVIEW_X && uis.cursorx <= PREVIEW_X + PREVIEW_W &&
			uis.cursory >= PREVIEW_Y && uis.cursory <= PREVIEW_Y + PREVIEW_H - 24 ) {
			if ( key == K_MOUSE1 && Hangar_LightControlClick() ) {
				Hangar_UpdateLightControl();
				return 0;
			}
			if ( key == K_MOUSE3 || ( key == K_MOUSE1 && trap_Key_IsDown( K_SHIFT ) ) ) {
				hangarMenuInfo.lighting = qtrue;
			} else if ( key == K_MOUSE1 ) {
				hangarMenuInfo.dragging = qtrue;
			} else {
				hangarMenuInfo.rolling = qtrue;
			}
			hangarMenuInfo.dragLastX = uis.cursorx;
			hangarMenuInfo.dragLastY = uis.cursory;
			return 0;
		}
		break;
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		hangarMenuInfo.previewYaw -= KEY_STEP;
		return 0;
	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		hangarMenuInfo.previewYaw += KEY_STEP;
		return 0;
	case K_UPARROW:
	case K_KP_UPARROW:
		hangarMenuInfo.previewPitch -= KEY_STEP;
		return 0;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		hangarMenuInfo.previewPitch += KEY_STEP;
		return 0;
	case K_PGUP:
	case K_KP_PGUP:
		hangarMenuInfo.previewRoll -= KEY_STEP;
		return 0;
	case K_PGDN:
	case K_KP_PGDN:
		hangarMenuInfo.previewRoll += KEY_STEP;
		return 0;
	case K_HOME:
	case K_KP_HOME:
		hangarMenuInfo.previewYaw = 150;
		hangarMenuInfo.previewPitch = 0;
		hangarMenuInfo.previewRoll = 0;
		hangarMenuInfo.previewZoom = 1;
		hangarMenuInfo.lightYaw = LIGHT_DEFAULT_YAW;
		hangarMenuInfo.lightPitch = LIGHT_DEFAULT_PITCH;
		return 0;
	case K_MWHEELUP:
	case K_KP_PLUS:
	case '=':
		hangarMenuInfo.previewZoom *= ZOOM_STEP;
		if ( hangarMenuInfo.previewZoom > ZOOM_MAX ) {
			hangarMenuInfo.previewZoom = ZOOM_MAX;
		}
		return 0;
	case K_MWHEELDOWN:
	case K_KP_MINUS:
	case '-':
		hangarMenuInfo.previewZoom /= ZOOM_STEP;
		if ( hangarMenuInfo.previewZoom < ZOOM_MIN ) {
			hangarMenuInfo.previewZoom = ZOOM_MIN;
		}
		return 0;
	case K_ESCAPE:
		if ( hangarMenuInfo.choosing ) {
			hangarMenuInfo.choosing = qfalse;		// out of the weapon list first
			return 0;
		}
		break;
	}
	return Menu_DefaultKey( &hangarMenuInfo.menu, key );
}


/*
=================
Hangar_Spend

takes points or credits; qfalse with a message when there are not enough
=================
*/
static qboolean Hangar_Spend( int points, int credits ) {
	if ( uiAccount.points < points ) {
		Com_sprintf( hangarMenuInfo.message, sizeof( hangarMenuInfo.message ), "Not enough development points: %i more needed",
			points - uiAccount.points );
		return qfalse;
	}
	if ( uiAccount.currency < credits ) {
		Com_sprintf( hangarMenuInfo.message, sizeof( hangarMenuInfo.message ), "Not enough credits: %s more needed",
			Hangar_Number( credits - uiAccount.currency ) );
		return qfalse;
	}
	uiAccount.points -= points;
	uiAccount.currency -= credits;
	return qtrue;
}


/*
=================
Hangar_UpgradeRobot

starts an upgrade of the robot, or finishes the running one for credits
=================
*/
static void Hangar_UpgradeRobot( void ) {
	accountRobot_t	*robot = Hangar_SelectedRobot();
	int				now = UI_AccountTime();

	if ( !robot ) {
		return;
	}
	if ( robot->upgrading ) {
		if ( !Hangar_Spend( 0, BG_SpeedUpCost( robot->upgradeEnd - now ) ) ) {
			return;
		}
		robot->upgradeEnd = now;
	} else {
		if ( robot->level >= ROBOT_LEVELS || !Hangar_Spend( BG_RobotUpgradeCost( robot->level ), 0 ) ) {
			return;
		}
		robot->upgrading = qtrue;
		robot->upgradeEnd = now + BG_UpgradeTime( robot->level );
	}
	UI_AccountSave();
	UI_AccountUpdate();
}


/*
=================
Hangar_UpgradeWeapon

the same for the weapon in the selected slot
=================
*/
static void Hangar_UpgradeWeapon( void ) {
	accountWeapon_t	*weapon = Hangar_SlotWeapon( hangarMenuInfo.selectedSlot );
	int				now = UI_AccountTime();

	if ( !weapon ) {
		return;
	}
	if ( weapon->upgrading ) {
		if ( !Hangar_Spend( 0, BG_SpeedUpCost( weapon->upgradeEnd - now ) ) ) {
			return;
		}
		weapon->upgradeEnd = now;
	} else {
		if ( weapon->level >= WEAPON_LEVELS || !Hangar_Spend( BG_WeaponUpgradeCost( weapon->type, weapon->level ), 0 ) ) {
			return;
		}
		weapon->upgrading = qtrue;
		weapon->upgradeEnd = now + BG_UpgradeTime( weapon->level );
	}
	UI_AccountSave();
	UI_AccountUpdate();
}


/*
=================
Hangar_Choose

a row of the weapon list: put an owned weapon into the slot (taking it off the
robot that carries it), buy one and put it in, or empty the slot
=================
*/
static void Hangar_Choose( int index ) {
	accountRobot_t	*robot = Hangar_SelectedRobot();
	int				kind, value, slot, carrier;

	if ( !robot || index < 0 || index >= hangarMenuInfo.numChoices ) {
		return;
	}
	kind = hangarMenuInfo.choiceKind[index];
	value = hangarMenuInfo.choiceValue[index];
	if ( kind == CHOICE_BUY ) {
		if ( uiAccount.numWeapons >= MAX_ACCOUNT_WEAPONS ) {
			Q_strncpyz( hangarMenuInfo.message, "The weapon store is full", sizeof( hangarMenuInfo.message ) );
			return;
		}
		if ( !Hangar_Spend( 0, bg_robotWeapons[value].price ) ) {
			return;
		}
		value = UI_AccountAddWeapon( value, 1 );
		kind = CHOICE_OWN;
	}
	if ( kind == CHOICE_OWN && value >= 0 ) {
		carrier = UI_AccountWeaponRobot( value, &slot );
		if ( carrier >= 0 ) {
			uiAccount.robots[carrier].slots[slot] = -1;
		}
		robot->slots[hangarMenuInfo.selectedSlot] = value;
	} else if ( kind == CHOICE_EMPTY ) {
		robot->slots[hangarMenuInfo.selectedSlot] = -1;
	}
	hangarMenuInfo.choosing = qfalse;
	UI_AccountSave();
	UI_AccountUpdate();
}


/*
=================
Hangar_Buy
=================
*/
static void Hangar_Buy( void ) {
	const robotInfo_t	*info = &bg_robots[hangarMenuInfo.shopType];

	if ( uiAccount.numRobots >= ROBOT_HANGAR_SIZE ) {
		return;
	}
	if ( uiAccount.numWeapons + info->numSlots > MAX_ACCOUNT_WEAPONS ) {
		Q_strncpyz( hangarMenuInfo.message, "The weapon store is full", sizeof( hangarMenuInfo.message ) );
		return;
	}
	if ( !Hangar_Spend( 0, info->price ) ) {
		return;
	}
	hangarMenuInfo.selected = uiAccount.numRobots;
	UI_AccountAddRobot( hangarMenuInfo.shopType );
	UI_AccountSave();
	UI_AccountUpdate();
}


/*
=================
Hangar_Event
=================
*/
static void Hangar_Event( void *ptr, int event ) {
	int		id;

	if ( event != QM_ACTIVATED ) {
		return;
	}
	id = ( (menucommon_s *)ptr )->id;
	hangarMenuInfo.message[0] = 0;
	if ( id >= ID_BAY0 && id < ID_BAY0 + MAX_BAYS ) {
		hangarMenuInfo.selected = id - ID_BAY0;
		if ( hangarMenuInfo.selected > uiAccount.numRobots ) {
			hangarMenuInfo.selected = uiAccount.numRobots;		// the first empty bay sells
		}
		hangarMenuInfo.selectedSlot = 0;
		hangarMenuInfo.choosing = qfalse;
		return;
	}
	if ( id >= ID_SLOT0 && id < ID_SLOT0 + ROBOT_MAX_SLOTS ) {
		hangarMenuInfo.selectedSlot = id - ID_SLOT0;
		hangarMenuInfo.choosing = qfalse;
		return;
	}
	if ( id >= ID_CHOICE0 && id < ID_CHOICE0 + MAX_CHOICES ) {
		Hangar_Choose( id - ID_CHOICE0 );
		return;
	}
	switch ( id ) {
	case ID_AUTOROTATE:
		trap_Cvar_SetValue( "ui_hangarAutoRotate", !trap_Cvar_VariableValue( "ui_hangarAutoRotate" ) );
		break;
	case ID_RESETVIEW:
		hangarMenuInfo.previewYaw = 150;
		hangarMenuInfo.previewPitch = 0;
		hangarMenuInfo.previewRoll = 0;
		hangarMenuInfo.previewZoom = 1;
		hangarMenuInfo.lightYaw = LIGHT_DEFAULT_YAW;
		hangarMenuInfo.lightPitch = LIGHT_DEFAULT_PITCH;
		break;
	case ID_LIGHT:
		trap_Cvar_SetValue( "ui_hangarLight", !trap_Cvar_VariableValue( "ui_hangarLight" ) );
		break;
	case ID_UPGRADE_ROBOT:
		Hangar_UpgradeRobot();
		break;
	case ID_UPGRADE_WEAPON:
		Hangar_UpgradeWeapon();
		break;
	case ID_CHANGE_WEAPON:
		hangarMenuInfo.choosing = qtrue;
		break;
	case ID_CHOOSE_BACK:
		hangarMenuInfo.choosing = qfalse;
		break;
	case ID_BUY:
		Hangar_Buy();
		break;
	case ID_SHOP_PREV:
		hangarMenuInfo.shopType = ( hangarMenuInfo.shopType + ROBOT_COUNT - 1 ) % ROBOT_COUNT;
		break;
	case ID_SHOP_NEXT:
		hangarMenuInfo.shopType = ( hangarMenuInfo.shopType + 1 ) % ROBOT_COUNT;
		break;
	case ID_BACK:
		UI_PopMenu();
		break;
	}
}


/*
=================
UI_Hangar_Cache
=================
*/
void UI_Hangar_Cache( void ) {
	int		index;

	trap_R_RegisterShaderNoMip( ART_BACKGROUND );
	trap_R_RegisterShaderNoMip( ART_BAY );
	trap_R_RegisterShaderNoMip( ART_BAY_SELECTED );
	for ( index = 0; index < RWEAPON_COUNT; index++ ) {
		trap_R_RegisterModel( va( "models/robotweapons/%s.md3", bg_robotWeapons[index].id ) );
	}
}


/*
=================
Hangar_InitButton
=================
*/
static void Hangar_InitButton( hangarButton_t *button, int id, int x, int y, int width, int height ) {
	menubitmap_s	*item = &button->item;

	item->generic.type		= MTYPE_BITMAP;
	item->generic.flags		= QMF_NODEFAULTINIT;
	item->generic.id		= id;
	item->generic.callback	= Hangar_Event;
	item->generic.ownerdraw	= Hangar_DrawButton;
	item->generic.x			= x;
	item->generic.y			= y;
	item->width				= width;
	item->height			= height;
	item->generic.left		= x;
	item->generic.top		= y;
	item->generic.right		= x + width;
	item->generic.bottom	= y + height;
	button->enabled			= qtrue;
	Menu_AddItem( &hangarMenuInfo.menu, item );
}


/*
=================
Hangar_Init
=================
*/
static void Hangar_Init( void ) {
	int		index, buttonX, buttonW;

	memset( &hangarMenuInfo, 0, sizeof( hangarMenuInfo ) );
	hangarMenuInfo.menu.fullscreen = qtrue;
	hangarMenuInfo.menu.wrapAround = qtrue;
	hangarMenuInfo.menu.draw = Hangar_Draw;
	hangarMenuInfo.menu.key = Hangar_Key;

	UI_Hangar_Cache();
	hangarMenuInfo.backgroundShader = trap_R_RegisterShaderNoMip( ART_BACKGROUND );
	hangarMenuInfo.bayShader = trap_R_RegisterShaderNoMip( ART_BAY );
	hangarMenuInfo.baySelectedShader = trap_R_RegisterShaderNoMip( ART_BAY_SELECTED );
	for ( index = 0; index < RWEAPON_COUNT; index++ ) {
		hangarMenuInfo.weaponModels[index] = trap_R_RegisterModel( va( "models/robotweapons/%s.md3", bg_robotWeapons[index].id ) );
	}

	for ( index = 0; index < MAX_BAYS; index++ ) {
		hangarMenuInfo.loadedType[index] = -1;
	}
	hangarMenuInfo.shopLoaded = -1;
	hangarMenuInfo.shopType = ROBOT_STRIZH;
	UI_AccountUpdate();
	Hangar_SyncModels();
	hangarMenuInfo.previewYaw = 150;
	hangarMenuInfo.previewZoom = 1;

	hangarMenuInfo.preview.generic.type		= MTYPE_BITMAP;
	hangarMenuInfo.preview.generic.flags	= QMF_INACTIVE;
	hangarMenuInfo.preview.generic.ownerdraw = Hangar_DrawPreview;
	hangarMenuInfo.preview.generic.x		= PREVIEW_X;
	hangarMenuInfo.preview.generic.y		= PREVIEW_Y;
	hangarMenuInfo.preview.width			= PREVIEW_W;
	hangarMenuInfo.preview.height			= PREVIEW_H;
	Menu_AddItem( &hangarMenuInfo.menu, &hangarMenuInfo.preview );

	for ( index = 0; index < MAX_BAYS; index++ ) {
		hangarMenuInfo.bays[index].generic.type		= MTYPE_BITMAP;
		hangarMenuInfo.bays[index].generic.flags	= QMF_LEFT_JUSTIFY | QMF_NODEFAULTINIT;
		hangarMenuInfo.bays[index].generic.id		= ID_BAY0 + index;
		hangarMenuInfo.bays[index].generic.callback	= Hangar_Event;
		hangarMenuInfo.bays[index].generic.ownerdraw = Hangar_DrawBay;
		hangarMenuInfo.bays[index].generic.x		= ( SCREEN_WIDTH - ( MAX_BAYS * BAY_W + ( MAX_BAYS - 1 ) * BAY_GAP ) ) / 2 + index * ( BAY_W + BAY_GAP );
		hangarMenuInfo.bays[index].generic.y		= BAY_Y;
		hangarMenuInfo.bays[index].width			= BAY_W;
		hangarMenuInfo.bays[index].height			= BAY_H;
		hangarMenuInfo.bays[index].generic.left		= hangarMenuInfo.bays[index].generic.x;
		hangarMenuInfo.bays[index].generic.top		= hangarMenuInfo.bays[index].generic.y;
		hangarMenuInfo.bays[index].generic.right	= hangarMenuInfo.bays[index].generic.x + BAY_W;
		hangarMenuInfo.bays[index].generic.bottom	= hangarMenuInfo.bays[index].generic.y + BAY_H;
		Menu_AddItem( &hangarMenuInfo.menu, &hangarMenuInfo.bays[index] );
	}

	// the panel: slots, actions, weapon list, shop (rows are set by Hangar_UpdateButtons)
	buttonX = PANEL_X + PANEL_PAD;
	buttonW = PANEL_W - 2 * PANEL_PAD;
	for ( index = 0; index < ROBOT_MAX_SLOTS; index++ ) {
		Hangar_InitButton( &hangarMenuInfo.slots[index], ID_SLOT0 + index, buttonX, SLOTS_Y + index * ROW_STEP, buttonW, BUTTON_H );
	}
	Hangar_InitButton( &hangarMenuInfo.upgradeRobot, ID_UPGRADE_ROBOT, buttonX, SLOTS_Y, buttonW, BUTTON_H );
	Hangar_InitButton( &hangarMenuInfo.upgradeWeapon, ID_UPGRADE_WEAPON, buttonX, SLOTS_Y, buttonW, BUTTON_H );
	Hangar_InitButton( &hangarMenuInfo.changeWeapon, ID_CHANGE_WEAPON, buttonX, SLOTS_Y, buttonW, BUTTON_H );
	for ( index = 0; index < MAX_CHOICES; index++ ) {
		Hangar_InitButton( &hangarMenuInfo.choices[index], ID_CHOICE0 + index, buttonX, SLOTS_Y, buttonW, BUTTON_H );
	}
	Hangar_InitButton( &hangarMenuInfo.chooseBack, ID_CHOOSE_BACK, buttonX + buttonW - 48, SLOTS_Y, 48, 15 );
	Hangar_InitButton( &hangarMenuInfo.shopPrev, ID_SHOP_PREV, buttonX, SLOTS_Y, 24, BUTTON_H );
	Hangar_InitButton( &hangarMenuInfo.buy, ID_BUY, buttonX + 30, SLOTS_Y, buttonW - 60, BUTTON_H );
	Hangar_InitButton( &hangarMenuInfo.shopNext, ID_SHOP_NEXT, buttonX + buttonW - 24, SLOTS_Y, 24, BUTTON_H );

	// preview controls under the robot
	Hangar_InitButton( &hangarMenuInfo.autoRotate, ID_AUTOROTATE, PREVIEW_X + 8, PREVIEW_Y + PREVIEW_H - 20, 116, 16 );
	Hangar_InitButton( &hangarMenuInfo.resetView, ID_RESETVIEW, PREVIEW_X + 130, PREVIEW_Y + PREVIEW_H - 20, 80, 16 );
	Hangar_SetButton( &hangarMenuInfo.resetView, "RESET VIEW", "", qtrue );
	Hangar_InitButton( &hangarMenuInfo.light, ID_LIGHT, PREVIEW_X + 216, PREVIEW_Y + PREVIEW_H - 20, 80, 16 );
	hangarMenuInfo.lightYaw = LIGHT_DEFAULT_YAW;
	hangarMenuInfo.lightPitch = LIGHT_DEFAULT_PITCH;

	Hangar_InitButton( &hangarMenuInfo.back, ID_BACK, 10, BOTTOM_Y + 12, 96, 24 );
	Hangar_SetButton( &hangarMenuInfo.back, "BACK", "", qtrue );
	Hangar_UpdateButtons();
}


/*
=================
UI_HangarMenu
=================
*/
void UI_HangarMenu( void ) {
	if ( !uiAccount.loaded ) {
		UI_AccountMenu();
		return;
	}
	Hangar_Init();
	UI_PushMenu( &hangarMenuInfo.menu );
	Menu_SetCursorToItem( &hangarMenuInfo.menu, &hangarMenuInfo.bays[0] );
}
