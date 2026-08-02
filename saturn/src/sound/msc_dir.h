/*----------------------
 | msc_dir.h
 | Description: Shared CD-directory-entry helper for the /MSC folder, where
 |   both PCM cues in this codebase (boot_music.cxx's SPLASH.PCM,
 |   loading_music.cxx's LOADCD.PCM) live. Extracted from boot_music.cxx's
 |   original private copy once a second module needed the identical
 |   directory-table dance.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MSC_DIR_H
#define MSC_DIR_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | cd_enter_msc
 | Description: Sets the CD current directory to /MSC.
 | Author: suinevere
 | Dependencies: SRL
 | Returns: true if /MSC was found and entered
 ----------------------*/
bool cd_enter_msc(void);

#ifdef __cplusplus
}
#endif
#endif /* MSC_DIR_H */
