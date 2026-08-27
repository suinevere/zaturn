/*----------------------
 | netbin_story.h
 | Description: The story image embedded in the netbin, which has no CD to read
 |   one from. It exists only so the typeahead layer has a dictionary and grammar
 |   to build a trie from -- no interpreter runs here, and the bytes are never
 |   executed or written. Returns NULL/0 in the CD build.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef NETBIN_STORY_H
#define NETBIN_STORY_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | netbin_story_data / netbin_story_size
 | Description: The embedded story bytes and their length. The pointer addresses
 |   .rodata directly, so callers must treat it as read-only and must not free it.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: netbin_story_bytes, netbin_story_len (generated)
 | Params: N/A
 | Returns: the bytes and length, or NULL and 0 when NETBIN is not defined
 ----------------------*/
const unsigned char *netbin_story_data(void);
unsigned int         netbin_story_size(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_STORY_H */
