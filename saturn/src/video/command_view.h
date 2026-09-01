/*----------------------
 | command_view.h
 | Description: The command panel's rendering and its pad-driven editor -- the
 |   command-mode counterparts of render_keyboard and typeahead_edit. Draws the
 |   three-module strip below the input line, and turns pad input into panel
 |   picks that end up in the same KeyboardState the on-screen keyboard fills.
 | Author: suinevere
 | Dependencies: command_panel.h, room_model.h, keyboard.h, typeahead.h, SRL
 ----------------------*/
#ifndef COMMAND_VIEW_H
#define COMMAND_VIEW_H

#include "command_panel.h"
#include "keyboard.h"
#include "room_model.h"
#include "saturn_keyboard.h"
#include "typeahead.h"

/*----------------------
 | CV_TRAVEL_X / CV_WORD_X / CV_CMD_X / CV_STRIP_ROWS
 | Description: The inner starting column of each module and the strip's content
 |   height. The strip is 1 + 13 + 1 + 15 + 1 + 8 + 1 = 40 columns and seven
 |   rows, all seven of them content: the compass rose is that tall, and the word
 |   and command lists are five rows sitting one row in from either end of it.
 |   The two blank rows that used to pad a five-row rose out to the strip's
 |   height are gone, so the panel's overall height is unchanged.
 | Author: suinevere
 ----------------------*/
#define CV_TRAVEL_X    1
#define CV_WORD_X     15
#define CV_CMD_X      31
#define CV_STRIP_ROWS  7

/*----------------------
 | render_command_panel
 | Description: Draws the input line, the strip's borders and dividers, the
 |   compass rose, the word page, and the fixed command list dim, with the
 |   focused module's selected entry alone at full brightness. The borders stay
 |   at full brightness throughout -- they are the frame, not a row.
 | Author: suinevere
 | Dependencies: command_rose.h, text_map.h, console_view.h
 | Globals: N/A
 | Params: p -- panel state; m -- the room snapshot; w -- the current word page
 | Returns: N/A
 ----------------------*/
void render_command_panel(const CommandPanel &p, const RoomModel &m, const CommandWords &w);

/*----------------------
 | command_edit
 | Description: One frame of command-mode input. The D-pad walks the focused
 |   module and crosses into the next one when it runs off an edge, so the three
 |   modules read as one grid rather than three boxes with a separate control for
 |   moving between them; L and R still jump modules outright. Accept picks, Back
 |   unwinds. A completed command is copied into `k` and submitted, so it leaves
 |   through the same path a typed one does.
 | Author: suinevere
 | Dependencies: input.h, command_panel.h
 | Globals: g_pad
 | Params: k -- keyboard state the command is written into; p -- panel state;
 |   m -- the room snapshot; root -- the typeahead trie for ranking, may be null;
 |   ke -- the decoded key event, consumed as handled; w -- (out) the word window
 |   the renderer should draw
 | Returns: N/A
 ----------------------*/
void command_edit(KeyboardState &k, CommandPanel &p, const RoomModel &m,
                  TrieNode *root, SaturnKeyEvent &ke, CommandWords &w);

#endif /* COMMAND_VIEW_H */
