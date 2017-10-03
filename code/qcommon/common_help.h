#define help_pattern_matching \
"\n" \
"If no argument is passed, the list is unfiltered.\n" \
"If you pass an argument, it is used to filter the list.\n" \
"The star character (*) will match 0, 1 or more characters.\n" \
"\n"

#define help_toggle \
"toggles the boolean value of a variable\n" \
"non-0 becomes 0 and 0 becomes 1"

#define help_cvarlist \
"lists and filters all cvars\n" \
help_pattern_matching \
"         Argument   Matches\n" \
"Example: r_         cvars starting with 'r_'\n" \
"Example: *light     cvars containing 'light'\n" \
"Example: r_*light   cvars starting with 'r_' AND containing 'light'"

#define help_cmdlist \
"lists and filters all commands\n" \
help_pattern_matching \
"         Argument   Matches\n" \
"Example: fs_        cmds starting with 'fs_'\n" \
"Example: *list      cmds containing 'list'\n" \
"Example: fs_*list   cmds starting with 'fs_' AND containing 'list'"

#define help_com_logfile \
"console logging to qconsole.log\n" \
"    0 = disabled\n" \
"    1 = enabled\n" \
"    2 = enabled and flushes the file after every write"

#define help_com_viewlog \
"early console window visibility\n" \
"    0 = hidden\n" \
"    1 = visible\n" \
"    2 = minimized"

#define help_com_completionStyle \
"auto-completion style\n" \
"    0 = legacy, always print all results\n" \
"    1 = ET-style, print once then cycle"

#define help_qport \
"internal network port\n" \
"this allows more than one person to play from behind a NAT router by using only one IP address"

#define help_vm_load \
"\n" \
"    0 = shared library (native code)\n" \
"    1 = interpreted QVM\n" \
"    2 = JIT-compiled QVM"

#define help_com_maxfps \
"max. allowed framerate\n" \
"It's highly recommended to only use 125 or 250 with V-Sync disabled." \
"If you get the 'connection interruped' message with 250,\n" \
"set it back to 125."
