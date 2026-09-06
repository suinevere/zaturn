/*----------------------
 | sentence_shape.h
 | Description: The sentence shapes a loaded v3 story's verb grammar allows, and
 |   the one question the command panel asks of them: given the words picked so
 |   far, what kind of word comes next. Built from the same story bytes the
 |   typeahead trie is built from and owned for as long as that trie is, because
 |   the online path frees the story the moment the trie exists. Implemented in
 |   sentence_shape.c.
 | Author: suinevere
 | Dependencies: typeahead.h (TrieNode, DictionaryWord), and the same
 |   TYPEAHEAD_MALLOC/FREE allocator the trie uses
 ----------------------*/
#ifndef SENTENCE_SHAPE_H
#define SENTENCE_SHAPE_H

#include "typeahead.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SHAPE_ROWS_MAX / SHAPE_PREP_MAX
 | Description: The row count one verb's grammar entry may hold -- the same
 |   1..12 bound typeahead_extract.c applies before it trusts an entry -- and so
 |   the most prepositions one slot can offer, since each row contributes at
 |   most one.
 | Author: suinevere
 ----------------------*/
#define SHAPE_ROWS_MAX 12
#define SHAPE_PREP_MAX 12

/*----------------------
 | ShapeRow
 | Description: One syntax line, bytes 0..4 of the story's 8-byte row: how many
 |   objects it takes, the dictionary id of the preposition before each (0 for
 |   none), and each object's search byte. The last two are carried for the
 |   noun-class ranking a later change may want and are not read today.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned char nobj;
    unsigned char prep1;
    unsigned char prep2;
    unsigned char attr1;
    unsigned char attr2;
} ShapeRow;

/*----------------------
 | SHAPE_PREP / SHAPE_NOUN / SHAPE_END / SHAPE_FREE
 | Description: What the next slot wants: a preposition, an object, nothing
 |   because the sentence is complete, or -- SHAPE_FREE -- no answer at all,
 |   which is what a verb outside the grammar and a sentence taken off it both
 |   give, and which means the caller keeps its own fallback chain.
 | Author: suinevere
 ----------------------*/
enum { SHAPE_PREP = 0, SHAPE_NOUN, SHAPE_END, SHAPE_FREE };

/*----------------------
 | ShapeSlot
 | Description: One answer from shape_next: the kind, and when the kind is
 |   SHAPE_PREP the prepositions the surviving rows allow here, in the trie's
 |   own order.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int kind;
    DictionaryWord *prep[SHAPE_PREP_MAX];
    int nprep;
} ShapeSlot;

/*----------------------
 | shape_next
 | Description: Matches the words picked so far against the first word's syntax
 |   rows, position by position, dropping every row that disagrees, and reports
 |   what the survivors want next. Survivors disagree in three ways -- object,
 |   preposition, or nothing more -- and the answer is the loosest one any
 |   survivor still wants: noun beats preposition beats end, since an object is
 |   always reachable and a bare verb like `put`, whose every one-object row is
 |   itself preposition-led (`put on OBJ`, `put out OBJ`), must still open on
 |   its noun tab rather than its preposition tab.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: picked -- the words picked so far, first being the verb; npicked --
 |   how many; out -- receives the answer, never left unwritten
 | Returns: N/A
 ----------------------*/
void shape_next(const char *const *picked, int npicked, ShapeSlot *out);

/*----------------------
 | shape_build
 | Description: Decodes the story's verb grammar into an owned table: every
 |   verb's syntax rows, an index from verb dictionary id to them, and the
 |   canonical word for each preposition id. Copies rather than referencing the
 |   story, which the online path frees as soon as the trie is built. A second
 |   call replaces the first. Silently builds nothing for a story that is not v3
 |   or for a null trie, which leaves every verb unknown and every caller on its
 |   own fallback. Also builds nothing, wholesale, for a v3 story whose grammar
 |   table is not Infocom-format: if any verb's grammar entry contains a row
 |   with an object count over 2, the whole story is refused, not just that
 |   verb -- the format is a property of the file, and a few entries decoding
 |   to a legal value by chance does not make the file half-readable.
 | Author: suinevere
 | Dependencies: typeahead.h
 | Globals: the module's own table
 | Params: story -- the loaded story bytes; len -- its length; root -- the trie
 |   already built from the same story, used to resolve preposition spellings
 | Returns: N/A
 ----------------------*/
void shape_build(const unsigned char *story, unsigned int len, TrieNode *root);

/*----------------------
 | shape_destroy
 | Description: Frees the table. Safe on a module that never built one, and
 |   safe to call twice. Call beside destroy_typeahead, never after the trie is
 |   gone and this is still being asked questions.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void shape_destroy(void);

/*----------------------
 | shape_verb_rows
 | Description: Copies one verb's syntax rows into `out`. The lookup is by
 |   spelling, since that is what the panel's assembled line holds.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: the module's own table
 | Params: verb -- the verb's spelling, may be null; out -- receives the rows;
 |   max -- out's capacity
 | Returns: rows written, 0 when the verb is unknown or has no entry
 ----------------------*/
int shape_verb_rows(const char *verb, ShapeRow *out, int max);

#ifdef __cplusplus
}
#endif

#endif // SENTENCE_SHAPE_H
