//goldchase.h, Version 1.2, 17 Apr 2012
#ifndef __goldchase_h
#define __goldchase_h

/* Player 0 */
#define G_PLR0  0x01

/* Player 1 */
#define G_PLR1  0x02

/* Player 2 */
#define G_PLR2  0x04

/* Player 3 */
#define G_PLR3  0x08

/* Player 4 */
#define G_PLR4  0x10

/* Wall */
#define G_WALL  0x20

/* Gold */
#define G_GOLD  0x40

/* Fool's Gold */
#define G_FOOL  0x80

/* Any player */
#define G_ANYP (G_PLR0|G_PLR1|G_PLR2|G_PLR3|G_PLR4)

/* A message for a player */
#define G_SOCKMSG  0x40

/* A player has joined/left the game */
#define G_SOCKPLR  0x80

#endif //__goldchase_h
