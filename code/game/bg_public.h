// Copyright (C) 1999-2000 Id Software, Inc.
//
// bg_public.h -- definitions shared by both the server game and client game modules

// because games can change separately from the main system version, we need a
// second version that must match between game and cgame

#define	GAME_VERSION		"cpma-1"

#define	DEFAULT_GRAVITY		800
#define	GIB_HEALTH			-40

#define	MAX_ITEMS			256

#define	RANK_RED_LEADS		0
#define	RANK_BLUE_LEADS		1
#define	RANK_TEAMS_TIED		2
#define	RANK_TIED_FLAG		0x4000

#define MAX_HITSCAN_RANGE	8192

#define DEFAULT_SHOTGUN_SPREAD	700
#define DEFAULT_SHOTGUN_COUNT	11

#define	ITEM_RADIUS			15		// item sizes are needed for client side pickup detection

#define	LIGHTNING_RANGE		768

#define	SCORE_NOT_PRESENT	-9999	// for the CS_SCORES[12] when only one player is present

#define	VOTE_TIME			20000

#define	DEFAULT_VIEWHEIGHT	26
#define CROUCH_VIEWHEIGHT	12
#define	DEAD_VIEWHEIGHT		-16

const enum {
	WATERLEVEL_NONE = 0,
	WATERLEVEL_SHALLOW,	// playerMins.z < water < player.origin, i.e. from feet to mid-thigh
	WATERLEVEL_DEEP,	// player.origin < water < player.viewheight, i.e. from mid-thigh to eyes
	WATERLEVEL_DROWN,	// player.viewheight < water
};


// config strings are a general means of communicating variable length strings
// from the server to all connected clients.

// OSP
// Position scrambling
#define OSPAUTH_POSNHACK	0x01

// Auto-actions
#define ACTION_STATSDUMP	0x01
#define ACTION_SCREENSHOT	0x02
#define ACTION_RECORD		0x04
#define ACTION_MULTIVIEW	0x08
#define ACTION_PLAYERONLY	0x10
#define ACTION_FOLLOW_POWERUP	0x20
#define ACTION_FOLLOW_KILLER	0x40
// !OSP

// CS_SERVERINFO and CS_SYSTEMINFO are defined in q_shared.h
#define	CS_MUSIC				2
#define	CS_MESSAGE				3		// from the map worldspawn's message field
#define	CS_MOTD					4		// g_motd string for server message of the day
#define	CS_WARMUP				5		// server time when the match will be restarted
#define	CS_SCORES1				6
#define	CS_SCORES2				7
#define CS_VOTE_TIME			8
#define CS_VOTE_STRING			9
#define	CS_VOTE_YES				10
#define	CS_VOTE_NO				11

#define CS_TEAMVOTE_TIME		12
#define CS_TEAMVOTE_STRING		14
#define	CS_TEAMVOTE_YES			16
#define	CS_TEAMVOTE_NO			18

#define	CS_GAME_VERSION			20
#define	CS_LEVEL_START_TIME		21		// so the timer only shows the current level
#define	CS_INTERMISSION			22		// when 1, fraglimit/timelimit has been hit and intermission will start in a second or two
#define CS_FLAGSTATUS			23		// string indicating flag status in CTF
#define CS_SHADERSTATE			24
#define CS_BOTINFO				25

#define	CS_ITEMS				27		// string of 0's and 1's that tell which items are present

#define	CS_MODELS				32
#define	CS_SOUNDS				(CS_MODELS+MAX_MODELS)
#define	CS_PLAYERS				(CS_SOUNDS+MAX_SOUNDS)
#define CS_LOCATIONS			(CS_PLAYERS+MAX_CLIENTS)

// CPMA
#define CPMA_MAX_ARENAS		8
#define CPMA_MAX_CLASSES	8
enum {
	// note: changing any of the CS_CPMA_ARENA_* values WILL fuck up old demos, so DON'T
	CS_FIRST_CPMA = (CS_LOCATIONS + MAX_LOCATIONS),
	CS_CPMA_ARENA_INFO = CS_FIRST_CPMA,

	CS_CPMA_FIRST_FREE_BLOCK = (CS_CPMA_ARENA_INFO + CPMA_MAX_ARENAS),

	CS_CPMA_ARENA_DYN = CS_FIRST_CPMA + 38,
	CS_CPMA_ARENA_NAME = (CS_CPMA_ARENA_DYN + CPMA_MAX_ARENAS),

	CS_CPMA_SECOND_FREE_BLOCK = (CS_CPMA_ARENA_NAME + CPMA_MAX_ARENAS),

	CS_RATEMIN = CS_CPMA_SECOND_FREE_BLOCK,
	CS_CPMA_CLASS_COUNT,
	CS_CPMA_CLASS_INFO,
	CS_LAST_CPMA = (CS_CPMA_CLASS_INFO + CPMA_MAX_CLASSES)
};
// !CPMA

// OSP custom confistrings
#define MAX_MOTD			8
#define MAX_MOTDLINE		60
#define MAX_CUSTOMGFX		16
#define DECAL_MAXCNT		32

enum {
	CS_FIRST_OSP = (CS_FIRST_CPMA + 70),
	CS_PROMODE = CS_FIRST_OSP,
	CS_OBSOLETE_CUSTOMCLIENT,	// truelightning/altgraphics allowances for clients
	CS_OSP_UNUSED,
	CS_OBSOLETE_PMOVE,			// enhanced client sampling
	CS_MXPKTSMIN,		// Min maxpackets (0 = disabled)
	CS_MXPKTSMAX,		// Max maxpackets (0 = disabled)
	CS_OBSOLETE_NUDGEMIN,		// Min timenudge  (0 = disabled)
	CS_OBSOLETE_NUDGEMAX,		// Max timenudge  (0 = disabled)
	CS_OSPINFO,			// OSP version info
	CS_OBSOLETE_BADLANDS,		// Badlands mode enabled
	CS_CUSTMOTD,		// MOTD lines
	CS_CUSTGFX = (CS_CUSTMOTD + MAX_MOTD),		// Custom gfx specs
	CS_DECALS = (CS_CUSTGFX  + MAX_CUSTOMGFX),	// Decal specs
	CS_OSPAUTH = (CS_DECALS   + DECAL_MAXCNT),	// Position scrambling
	CS_MAX
};


// rhea broke all the OSP CS id's in 99v1
#define CS_PARTICLES			(CS_LOCATIONS+MAX_LOCATIONS) 

#define OSP_PROMODE				(CS_PARTICLES + MAX_LOCATIONS + 70)	// Enable/disable promode
#define OSP_CUSTOMCLIENT		(OSP_PROMODE  + 1)	// truelightning/algraphics allowances for clients
#define OSP_PMOVE				(OSP_PROMODE  + 3)	// Enhanced client sampling
#define OSP_MXPKTSMIN			(OSP_PROMODE  + 4)	// Min maxpackets (0 = disabled)
#define OSP_MXPKTSMAX			(OSP_PROMODE  + 5)	// Max maxpackets (0 = disabled)
#define OSP_NUDGEMIN			(OSP_PROMODE  + 6)	// Min timenudge  (0 = disabled)
#define OSP_NUDGEMAX			(OSP_PROMODE  + 7)	// Max timenudge  (0 = disabled)
#define OSP_OSPINFO				(OSP_PROMODE  + 8)	// OSP version info
#define OSP_BADLANDS			(OSP_PROMODE  + 9)	// Badlands mode enabled
#define OSP_CUSTMOTD			(OSP_PROMODE  + 10)	// MOTD lines
#define OSP_CUSTGFX				(OSP_CUSTMOTD + MAX_MOTD)		// Custom gfx specs
#define OSP_DECALS				(OSP_CUSTGFX  + MAX_CUSTOMGFX)	// Decal specs
#define OSP_PSNAUTH				(OSP_DECALS   + DECAL_MAXCNT)	// Position scrambling


#if (CS_LAST_CPMA > CS_FIRST_OSP)
#error CPMA and OSP configstrings overlap
#endif

#if (CS_MAX > MAX_CONFIGSTRINGS)
#error overflow: (CS_MAX > MAX_CONFIGSTRINGS)
#endif


typedef enum { GENDER_MALE, GENDER_FEMALE, GENDER_NEUTER } gender_t;


/*
===================================================================================

PMOVE MODULE

The pmove code takes a player_state_t and a usercmd_t and generates a new player_state_t
and some other output data.  Used for local prediction on the client game and true
movement on the server game.
===================================================================================
*/

typedef enum {
	PM_NORMAL,		// can accelerate and turn
	PM_NOCLIP,		// noclip movement
	PM_SPECTATOR,	// still run into walls
	PM_DEAD,		// no acceleration or turning, but free falling
	PM_FREEZE,		// stuck in place with no control
	PM_INTERMISSION,	// no movement or status bar
	PM_SPINTERMISSION,	// no movement or status bar
	PM_BLACKOUT,	// CPMA  no sight or sound
} pmtype_t;

typedef enum {
	WEAPON_READY, 
	WEAPON_RAISING,
	WEAPON_DROPPING,
	WEAPON_FIRING
} weaponstate_t;

// pmove->pm_flags
// this is only a 16-bit var over the wire
#define	PMF_DUCKED			1
#define	PMF_JUMP_HELD		2
#define	PMF_MVD				4		// CPMA  recording/watching an MVD
#define	PMF_BACKWARDS_JUMP	8		// go into backwards land
#define	PMF_BACKWARDS_RUN	16		// coast down to backwards run
#define	PMF_TIME_LAND		32		// pm_time is time before rejump
#define	PMF_TIME_KNOCKBACK	64		// pm_time is an air-accelerate only time
#define	PMF_TIME_WATERJUMP	256		// pm_time is waterjump
#define	PMF_RESPAWNED		512		// clear after attack button comes up
#define	PMF_USE_ITEM_HELD	1024
#define PMF_GRAPPLE_PULL	2048	// pull towards grapple location
#define PMF_FOLLOW			4096	// spectate following another player
#define PMF_SCOREBOARD		8192	// spectate as a scoreboard
#define PMF_INVULEXPAND		16384	// invulnerability sphere set to full size
#define PMF_CROUCHSLIDE     32768   // crouch slide CQ3

#define	PMF_ALL_TIMES	(PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK)

extern const vec3_t playerMins, playerMaxs;


// if a full pmove isn't done on the client, you can just update the angles
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd );

//===================================================================================


// player_state->stats[] indexes
// NOTE: may not have more than 16, and are only 16-bit over the wire
typedef enum {
	STAT_HEALTH,
	STAT_HOLDABLE_ITEM,
	STAT_WEAPONS,			// 16 bitfields
	STAT_ARMOR,				
	STAT_DEAD_YAW,			// look this direction when dead (FIXME: get rid of?)
	STAT_CLIENTS_READY,		// bit mask of clients wishing to exit the intermission (FIXME: configstring?)
	STAT_MAX_HEALTH_UNUSED,
	STAT_JUMPTIME,			// CPM: for double-jump
	STAT_DJUMPED,			// CPM: for double-jump
	STAT_ARMORTYPE,			// CPM: for armortypes (green, yellow, red)
	STAT_RAILTIME,	        // PMC fast rail timer, removed in 1.44. Back for 1.46
	STAT_COOLDOWN,			// CPM: for weapon cooldown to cut down on LG->RG cess
	STAT_WEAPSWITCHED,		// CPM: inflict a penalty cooldown after the first switch to sthing >MG
	STAT_CLIENTS_ALIVE_00_15,	// CPMA: for CA scoreboard - gay id code keeps breaking EF_DEAD/ET_INVISIBLE
	STAT_CLIENTS_ALIVE_16_31,	// CPMA: for CA scoreboard - gay id code keeps breaking EF_DEAD/ET_INVISIBLE
	STAT_MVD_CLIENTMASK		// 99x9a - now the client has true knowledge of available clients in an MVD
} statIndex_t;
// STAT_MVD_CLIENTMASK is the crucial one - moving it will break old demos
// but #if (STAT_MVD_CLIENTMASK != 15) doesn't friggin work because it's an enum not a define  :/

// CPMA 1.2  this was a STAT, which is fucking RETARDED
#define G_MAX_HEALTH	100


// player_state->persistant[] indexes
// these fields are the only part of player_state that isn't
// cleared on respawn
// NOTE: may not have more than 16
typedef enum {
	PERS_SCORE,						// ! MUST NOT CHANGE, SERVER AND GAME BOTH REFERENCE !
	PERS_HITS,						// total points damage inflicted so damage beeps can sound on change
	PERS_RANK,						// player rank or team rank
	PERS_TEAM,						// player team
	PERS_SPAWN_COUNT,				// incremented every respawn
	PERS_PLAYEREVENTS,				// 16 bits that can be flipped for events
	PERS_ATTACKER,					// clientnum of last damage inflicter
// CPMA
	PERS_ATTACKEE_ARMOR,			// we don't use this gayness, but we'll keep the ordering the same
// !CPMA
	PERS_KILLED,					// count of the number of times you died
	// player awards tracking
	PERS_IMPRESSIVE_COUNT,			// two railgun hits in a row
	PERS_EXCELLENT_COUNT,			// two successive kills in a short amount of time
	PERS_DEFEND_COUNT,				// defend awards
	PERS_ASSIST_COUNT,				// assist awards
	PERS_GAUNTLET_FRAG_COUNT,		// kills with the guantlet
	PERS_CAPTURES					// captures
} persEnum_t;


// entityState_t->eFlags
#define	EF_DEAD				0x00000001		// don't draw a foe marker over players with EF_DEAD
#define	EF_BACKPACK			0x00000002		// CPM: Backpack indicator bit
#define	EF_TELEPORT_BIT		0x00000004		// toggled every time the origin abruptly changes
#define	EF_AWARD_EXCELLENT	0x00000008		// draw an excellent sprite
#define	EF_BOUNCE			0x00000010		// for missiles
#define	EF_BOUNCE_HALF		0x00000020		// for missiles
#define	EF_AWARD_GAUNTLET	0x00000040		// draw a gauntlet sprite
#define	EF_NODRAW			0x00000080		// may have an event, but no model (unspawned items)
#define	EF_FIRING			0x00000100		// for lightning gun
#define	EF_MOVER_STOP		0x00000400		// will push otherwise
#define EF_AWARD_CAP		0x00000800		// draw the capture sprite
#define	EF_TALK				0x00001000		// draw a talk balloon
#define	EF_CONNECTION		0x00002000		// draw a connection trouble sprite
#define	EF_AWARD_IMPRESSIVE	0x00008000		// draw an impressive sprite
#define	EF_AWARD_DEFEND		0x00010000		// draw a defend sprite
#define	EF_AWARD_ASSIST		0x00020000		// draw a assist sprite
#define EF_AWARD_DENIED		0x00040000		// denied
// CPMA
#define	EF_AWARD_ALL		(EF_AWARD_IMPRESSIVE | EF_AWARD_EXCELLENT | EF_AWARD_GAUNTLET | EF_AWARD_ASSIST | EF_AWARD_DEFEND | EF_AWARD_CAP)
//#define EF_KAMIKAZE		0x00000200
#define EF_FROZEN			0x00000200		// FTAG - player is frozen
#define EF_SLIDING			0x00004000		// crouchslide overload
//#define EF_VOTED			0x00004000		// already cast a vote
//#define EF_TEAMVOTED		0x00080000		// already cast a team vote
// overload a couple of bits for HM spawnpoints
#define EF_SPAWNPOINT_ACTIVE	0x00000001
#define EF_SPAWNPOINT_INACTIVE	0x00000002
// and again to indicate a locked flag in CTF
#define EF_FLAG_LOCKED		0x00000001


// NOTE: may not have more than 16
typedef enum {
	PW_NONE,

	PW_QUAD,
	PW_BATTLESUIT,
	PW_HASTE,
	PW_INVIS,
	PW_REGEN,
	PW_FLIGHT,

	PW_FLAGS,

	PW_REDFLAG = PW_FLAGS,
	PW_BLUEFLAG,

	PW_NEUTRALFLAG,		// TA shit, but needed for bot altroutes to work

	PW_NUM_POWERUPS
} powerup_t;

#define PWF_CTF_FLAGS	((1 << PW_BLUEFLAG) | (1 << PW_REDFLAG))


typedef enum {
	HI_NONE,

	HI_TELEPORTER,
	HI_MEDKIT,
	HI_KAMIKAZE,
	HI_PORTAL,
	HI_INVULNERABILITY,

	HI_NUM_HOLDABLE
} holdable_t;


typedef enum {
	WP_NONE,

	WP_GAUNTLET,
	WP_MACHINEGUN,
	WP_SHOTGUN,
	WP_GRENADE_LAUNCHER,
	WP_ROCKET_LAUNCHER,
	WP_LIGHTNING,
	WP_RAILGUN,
	WP_PLASMAGUN,
	WP_BFG,
	WP_GRAPPLING_HOOK,

	WP_NUM_WEAPONS
} weapon_t;


// reward sounds (stored in ps->persistant[PERS_PLAYEREVENTS])
#define	PLAYEREVENT_DENIEDREWARD		0x0001
#define	PLAYEREVENT_GAUNTLETREWARD		0x0002
#define PLAYEREVENT_HOLYSHIT			0x0004

// entityState_t->event values
// entity events are for effects that take place reletive
// to an existing entities origin.  Very network efficient.

// two bits at the top of the entityState->event field
// will be incremented with each change in the event so
// that an identical event started twice in a row can
// be distinguished.  And off the value with ~EV_EVENT_BITS
// to retrieve the actual event number
#define	EV_EVENT_BIT1		0x00000100
#define	EV_EVENT_BIT2		0x00000200
#define	EV_EVENT_BITS		(EV_EVENT_BIT1|EV_EVENT_BIT2)

typedef enum {
	EV_NONE,

	EV_FOOTSTEP,
	EV_FOOTSTEP_METAL,
	EV_FOOTSPLASH,
	EV_FOOTWADE,
	EV_SWIM,

	EV_STEP_4,
	EV_STEP_8,
	EV_STEP_12,
	EV_STEP_16,

	EV_FALL_SHORT,
	EV_FALL_MEDIUM,
	EV_FALL_FAR,

	EV_JUMP_PAD,			// boing sound at origin, jump sound on player

	EV_JUMP,
	EV_WATER_TOUCH,	// foot touches
	EV_WATER_LEAVE,	// foot leaves
	EV_WATER_UNDER,	// head touches
	EV_WATER_CLEAR,	// head leaves

	EV_ITEM_PICKUP,			// normal item pickups are predictable
	EV_GLOBAL_ITEM_PICKUP,	// powerup / team sounds are broadcast to everyone

	EV_NOAMMO,
	EV_CHANGE_WEAPON,
	EV_FIRE_WEAPON,

	EV_USE_ITEM0,
	EV_USE_ITEM1,
	EV_USE_ITEM2,
	EV_USE_ITEM3,
	EV_USE_ITEM4,
	EV_USE_ITEM5,
	EV_USE_ITEM6,
	EV_USE_ITEM7,
	EV_USE_ITEM8,
	EV_USE_ITEM9,
	EV_USE_ITEM10,
	EV_USE_ITEM11,
	EV_USE_ITEM12,
	EV_USE_ITEM13,
	EV_USE_ITEM14,
	EV_USE_ITEM15,

	EV_ITEM_RESPAWN,
	EV_ITEM_POP,
	EV_PLAYER_TELEPORT_IN,
	EV_PLAYER_TELEPORT_OUT,

	EV_GRENADE_BOUNCE,		// eventParm will be the soundindex

	EV_GENERAL_SOUND,
	EV_GLOBAL_SOUND,		// no attenuation
	EV_GLOBAL_TEAM_SOUND,

	EV_BULLET_HIT_FLESH,
	EV_BULLET_HIT_WALL,

	EV_MISSILE_HIT,
	EV_MISSILE_MISS,
	EV_MISSILE_MISS_METAL,
	EV_RAILTRAIL,
	EV_SHOTGUN,
	EV_BULLET,				// otherEntity is the shooter

	EV_PAIN,
	EV_DEATH1,
	EV_DEATH2,
	EV_DEATH3,
	EV_OBITUARY,

	EV_POWERUP_QUAD,
	EV_POWERUP_BATTLESUIT,
	EV_POWERUP_REGEN,

	EV_GIB_PLAYER,			// gib a previously living player
	EV_SCOREPLUM,			// score plum

	EV_STEP,				// CPMA  parameterised step
	EV_FIRE_GRAPPLE,		// CPMA  so offhand hook isn't silent

//#ifdef MISSIONPACK
	EV_PROXIMITY_MINE_STICK,
	EV_PROXIMITY_MINE_TRIGGER,
	EV_KAMIKAZE,			// kamikaze explodes
	EV_OBELISKEXPLODE,		// obelisk explodes
	EV_OBELISKPAIN,			// obelisk is in pain
	EV_INVUL_IMPACT,		// invulnerability sphere impact
	EV_JUICED,				// invulnerability juiced effect
	EV_LIGHTNINGBOLT,		// lightning bolt bounced of invulnerability sphere
//#endif

	EV_DEBUG_LINE,
	EV_STOPLOOPINGSOUND,
	EV_TAUNT,
	EV_TAUNT_YES,
	EV_TAUNT_NO,
	EV_TAUNT_FOLLOWME,
	EV_TAUNT_GETFLAG,
	EV_TAUNT_GUARDBASE,
	EV_TAUNT_PATROL

} entity_event_t;


typedef enum {
	GTS_RED_CAPTURE,
	GTS_BLUE_CAPTURE,
	GTS_RED_RETURN,
	GTS_BLUE_RETURN,
	GTS_RED_TAKEN,
	GTS_BLUE_TAKEN,
	GTS_REDTEAM_SCORED,
	GTS_BLUETEAM_SCORED,
	GTS_REDTEAM_TOOK_LEAD,
	GTS_BLUETEAM_TOOK_LEAD,
	GTS_TEAMS_ARE_TIED,
	// CPMA
	GTS_RED_WINS_ROUND,
	GTS_BLUE_WINS_ROUND,
} global_team_sound_t;

// animations
typedef enum {
	BOTH_DEATH1,
	BOTH_DEAD1,
	BOTH_DEATH2,
	BOTH_DEAD2,
	BOTH_DEATH3,
	BOTH_DEAD3,

	TORSO_GESTURE,

	TORSO_ATTACK,
	TORSO_ATTACK2,

	TORSO_DROP,
	TORSO_RAISE,

	TORSO_STAND,
	TORSO_STAND2,

	LEGS_WALKCR,
	LEGS_WALK,
	LEGS_RUN,
	LEGS_BACK,
	LEGS_SWIM,

	LEGS_JUMP,
	LEGS_LAND,

	LEGS_JUMPB,
	LEGS_LANDB,

	LEGS_IDLE,
	LEGS_IDLECR,

	LEGS_TURN,

	TORSO_GETFLAG,
	TORSO_GUARDBASE,
	TORSO_PATROL,
	TORSO_FOLLOWME,
	TORSO_AFFIRMATIVE,
	TORSO_NEGATIVE,

	MAX_ANIMATIONS,

	LEGS_BACKCR,
	LEGS_BACKWALK,
	FLAG_RUN,
	FLAG_STAND,
	FLAG_STAND2RUN,

	MAX_TOTALANIMATIONS
} animNumber_t;


typedef struct animation_s {
	int		firstFrame;
	int		numFrames;
	int		loopFrames;			// 0 to numFrames
	int		frameLerp;			// msec between frames
	int		initialLerp;		// msec to get to first frame
	int		reversed;			// true if animation is reversed
	int		flipflop;			// true if animation should flipflop back to base
} animation_t;


// flip the togglebit every time an animation
// changes so a restart of the same anim can be detected
#define	ANIM_TOGGLEBIT		128


typedef enum {
	TEAM_FREE,
	TEAM_RED,
	TEAM_BLUE,
	TEAM_SPECTATOR,

	TEAM_NUM_TEAMS
} team_t;

// Time between location updates
#define TEAM_LOCATION_UPDATE_TIME		1000

// How many players on the overlay
#define TEAM_MAXOVERLAY		32


// means of death
typedef enum {
	MOD_UNKNOWN,
	MOD_SHOTGUN,
	MOD_GAUNTLET,
	MOD_MACHINEGUN,
	MOD_GRENADE,
	MOD_GRENADE_SPLASH,
	MOD_ROCKET,
	MOD_ROCKET_SPLASH,
	MOD_PLASMA,
	MOD_PLASMA_SPLASH,
	MOD_RAILGUN,
	MOD_LIGHTNING,
	MOD_BFG,
	MOD_BFG_SPLASH,
	MOD_WATER,
	MOD_SLIME,
	MOD_LAVA,
	MOD_CRUSH,
	MOD_TELEFRAG,
	MOD_FALLING,
	MOD_SUICIDE,
	MOD_TARGET_LASER,
	MOD_TRIGGER_HURT,
	MOD_GRAPPLE,
	MOD_SWITCHTEAM,	// OSP
	MOD_THAW,		// CPMA FTAG
} meansOfDeath_t;


//---------------------------------------------------------

// gitem_t->type
typedef enum {
	IT_BAD,
	IT_WEAPON,				// EFX: rotate + upscale + minlight
	IT_AMMO,				// EFX: rotate
	IT_ARMOR,				// EFX: rotate + minlight
	IT_HEALTH,				// EFX: static external sphere + rotating internal
	IT_POWERUP,				// instant on, timer based
							// EFX: rotate + external ring that rotates
	IT_HOLDABLE,			// single use, holdable item
							// EFX: rotate + bob
	IT_PERSISTANT_POWERUP,
	IT_TEAM,
} itemType_t;

#define MAX_ITEM_MODELS 4

typedef struct gitem_s {
	char		*classname;	// spawning name
	char		*pickup_sound;
	char		*world_model[MAX_ITEM_MODELS];

	char		*icon;
	char		*pickup_name;	// for printing on pickup

	int			quantity;		// for ammo how much, or duration of powerup
	itemType_t  giType;			// IT_* flags

	int			giTag;

	char		*precaches;		// string of all models and images this item will use
	char		*sounds;		// string of all sounds this item will use
} gitem_t;

// included in both the game dll and the client
extern const gitem_t bg_itemlist[];
extern int bg_numItems;

const gitem_t* BG_FindItem( const char *pickupName );
const gitem_t* BG_FindItemForWeapon( weapon_t weapon );
const gitem_t* BG_FindItemForPowerup( powerup_t pw );
const gitem_t* BG_FindItemForHoldable( holdable_t pw );
#define	ITEM_INDEX(x) ((x)-bg_itemlist)


// content masks
#define	MASK_ALL				(-1)
#define	MASK_SOLID				(CONTENTS_SOLID)
#define	MASK_PLAYERSOLID		(CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY)
#define	MASK_DEADSOLID			(CONTENTS_SOLID|CONTENTS_PLAYERCLIP)
#define	MASK_WATER				(CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define	MASK_OPAQUE				(CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define	MASK_SHOT				(CONTENTS_SOLID|CONTENTS_BODY|CONTENTS_CORPSE)


//
// entityState_t->eType
//
typedef enum {
	ET_GENERAL,
	ET_PLAYER,
	ET_ITEM,
	ET_MISSILE,
	ET_MOVER,
	ET_BEAM,
	ET_PORTAL,
	ET_SPEAKER,
	ET_PUSH_TRIGGER,
	ET_TELEPORT_TRIGGER,
	ET_INVISIBLE,
	ET_GRAPPLE,				// grapple hooked on wall
	ET_TEAM,

	ET_EVENTS,				// any of the EV_* events can be added freestanding
							// by setting eType to ET_EVENTS + eventNum
							// this avoids having to set eFlags and eventNum
	ET_NUM_ETYPES
} entityType_t;



void	BG_EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result );
void	BG_EvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result );

void	BG_AddPredictableEventToPlayerstate( int newEvent, int eventParm, playerState_t *ps );

//void	BG_TouchJumpPad( playerState_t *ps, entityState_t *jumppad );

void	BG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap );

unsigned int CreateMunge( const unsigned int base, const char* key );


#define ARENAS_PER_TIER		4
#define MAX_ARENAS			1024
#define	MAX_ARENAS_TEXT		8192

#define MAX_BOTS			1024
#define MAX_BOTS_TEXT		8192


// !! careful - there are some awkward dependencies here since C doesn't let you forward-reference

#include "../game/bg_cpma.h"


#define	MAXTOUCH	32
typedef struct {
	// state (in / out)
	playerState_t	*ps;

	// command (in)
	usercmd_t	cmd;
	int			tracemask;			// collide against these types of surfaces
	int			debugLevel;			// if set, diagnostic output will be printed
	qboolean	noFootsteps;		// if the game is setup for no footsteps by the server
	qboolean	gauntletHit;		// true if a gauntlet attack would actually hit something

	int			framecount;

	// results (out)
	int			numtouch;
	int			touchents[MAXTOUCH];

	vec3_t		mins, maxs;			// bounding box size

	int			watertype;
	int			waterlevel;

	float		xyspeed;

	// callbacks to test the world
	// these will be different functions during game and cgame
	void		(*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask );
	int			(*pointcontents)( const vec3_t point, int passEntityNum );

// CPMA
	// rather than flooding the pathetic event buffer with EV_STEPs
	// that are ignored by the server anyway, we just need the desired delta when we return from pmove
	// so that predicting clients can smooth their way up steps.
	// as it happens, this produces better results anyway, since it has pixel accuracy
	// whereas EV_STEP uses 4-pixel chunks. owned, and owned again.  :)
	int evstepHack;

	const float* gpvars;
	const CPMA_ClassInfo* pClass; // NTF physics depend on the class rather than the core gameplay rules
	int			arenaflags;
	int		promode;	
// !CPMA
} pmove_t;

void Pmove( pmove_t* pmove, const CPMA_ArenaInfo* pArena );
qboolean BG_PlayerTouchesItem( const CPMA_ArenaInfo* pArena, const playerState_t* ps, const entityState_t* item, int atTime );

#include "../game/bg_promode.h"
