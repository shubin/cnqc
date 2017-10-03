#define help_cl_timeNudge \
"id's crippled timenudge\n" \
"This still exists in CPMA, but should always be 0.\n" \
"All it really does now is mess up the automatic adaptive nudges."

#define help_cl_shownet \
"prints network info\n" \
"   -2 = commandTime\n" \
"   -1 = entity removed/changed events\n" \
"    0 = disabled\n" \
"    1 = message lengths\n" \
"    2 = message types, commandTime, etc\n" \
"    3 = 2 + entity parsing details\n" \
"    4 = 2 + player state details"

#define help_cl_showSend \
"prints client to server packet info\n" \
"format: (usercmd_count) packet_size\n" \
"each dot symbolizes a frame of delay"

#define help_cl_allowDownload \
"selects the download system\n" \
"   -1 = id's old download system\n" \
"    0 = downloads disabled\n" \
"    1 = CNQ3's new download system"

#define help_con_scaleMode \
"console text scaling mode\n" \
"    0 = text size scales with con_scale but not the resolution\n" \
"    1 = text size scales with con_scale and the resolution\n" \
"    2 = text size is always 8x12"

#define help_plus_minus \
"\nAbout commands starting with '+' or '-':\n" \
"- If '+cmdname' is called from a bind, the command is executed every frame until the bind key is released.\n" \
"- If '+cmdname' is not called from a bind, the command is executed every frame until '-cmdname' is called."

#define help_cl_debugMove \
"prints a graph of view angle deltas\n" \
"    0 = disabled\n" \
"    1 = horizontal axis\n" \
"    2 = vertical axis"
