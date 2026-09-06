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
#include "panel_layout.h"
#include "room_model.h"
#include "saturn_keyboard.h"
#include "typeahead.h"

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

/*----------------------
 | cv_set_lobby / cv_lobby
 | Description: Switches the word module between the story's vocabulary and the
 |   short list a multizork session needs before its game exists. Online play
 |   opens on a login, a lobby and a waiting room, none of which the parser ever
 |   sees: what they accept is a name, a room number, yes or no, and go or quit.
 |   Offering the story's several hundred verbs and nouns there is offering words
 |   that cannot work, so lobby mode replaces the whole list with the handful
 |   those screens do take -- see CV_LOBBY_WORDS for where each came from -- and
 |   each of them submits on its own rather than opening a sentence.
 |
 |   The caller decides when the game has begun; online.cxx turns this off on the
 |   first room id, which multizorkd only sends once the Z-machine is asking a
 |   player for a command. Off is the default, so the CD build -- which has no
 |   lobby -- never has to say anything.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: on -- nonzero for the slim list
 | Returns: cv_lobby reports the current setting
 ----------------------*/
void cv_set_lobby(int on);
int  cv_lobby(void);

#endif /* COMMAND_VIEW_H */
