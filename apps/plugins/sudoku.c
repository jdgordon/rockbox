/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2005 Dave Chapman
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

/***
Sudoku by Dave Chapman

User instructions
-----------------

Use the arrow keys to move cursor, and press SELECT/ON/F2 to increment
the number under the cursor.

At any time during the game, press On to bring up the game menu with
further options:

 Save
 Reload
 Clear
 Solve

Sudoku is implemented as a "viewer" for a ".ss" file, as generated by
Simple Sudoku and other applications - http://angusj.com/sudoku/

In-progress game positions are saved in the original .ss file, with
A-I used to indicate numbers entered by the user.

Example ".ss" file, and one with a saved state:

...|...|...     ...|...|...
2..|8.4|9.1     2.C|8.4|9.1
...|1.6|32.     E..|1.6|32.
-----------     -----------
...|..5|.4.     ...|..5|.4.
8..|423|..6     8..|423|..6
.3.|9..|...     .3D|9..|A..
-----------     -----------
.63|7.9|...     .63|7.9|...
4.9|5.2|..8     4.9|5.2|.C8
...|...|...     ...|...|...

*/

#include "plugin.h"

#ifdef HAVE_LCD_BITMAP

PLUGIN_HEADER

#define STATE_FILE        PLUGIN_DIR "/sudoku.state"
#define GAMES_FILE        PLUGIN_DIR "/sudoku.levels"

/* variable button definitions */
#if CONFIG_KEYPAD == RECORDER_PAD
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_TOGGLE BUTTON_PLAY
#define SUDOKU_BUTTON_MENU BUTTON_F1
#define SUDOKU_BUTTON_POSSIBLE BUTTON_F2

#elif CONFIG_KEYPAD == ONDIO_PAD
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_ALTTOGGLE (BUTTON_MENU | BUTTON_DOWN)
#define SUDOKU_BUTTON_TOGGLE_PRE BUTTON_MENU
#define SUDOKU_BUTTON_TOGGLE (BUTTON_MENU | BUTTON_REL)
#define SUDOKU_BUTTON_MENU_PRE BUTTON_MENU
#define SUDOKU_BUTTON_MENU (BUTTON_MENU | BUTTON_REPEAT)
#define SUDOKU_BUTTON_POSSIBLE (BUTTON_MENU | BUTTON_LEFT)

#elif (CONFIG_KEYPAD == IRIVER_H100_PAD) || \
      (CONFIG_KEYPAD == IRIVER_H300_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_OFF
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_ALTTOGGLE BUTTON_ON
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_MODE
#define SUDOKU_BUTTON_POSSIBLE BUTTON_REC

#elif (CONFIG_KEYPAD == IPOD_4G_PAD)
#define SUDOKU_BUTTON_QUIT (BUTTON_SELECT | BUTTON_MENU)
#define SUDOKU_BUTTON_UP BUTTON_SCROLL_BACK
#define SUDOKU_BUTTON_DOWN BUTTON_SCROLL_FWD
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_MENU
#define SUDOKU_BUTTON_POSSIBLE (BUTTON_SELECT | BUTTON_LEFT)

#elif (CONFIG_KEYPAD == IAUDIO_X5_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_POWER
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_PLAY
#define SUDOKU_BUTTON_POSSIBLE BUTTON_REC

#elif (CONFIG_KEYPAD == GIGABEAT_PAD)
#define SUDOKU_BUTTON_QUIT BUTTON_A
#define SUDOKU_BUTTON_UP BUTTON_UP
#define SUDOKU_BUTTON_DOWN BUTTON_DOWN
#define SUDOKU_BUTTON_TOGGLE BUTTON_SELECT
#define SUDOKU_BUTTON_MENU BUTTON_MENU
#define SUDOKU_BUTTON_POSSIBLE BUTTON_POWER

#elif
  #error SUDOKU: Unsupported keypad
#endif

/* The bitmaps */
extern const fb_data sudoku_normal[];
extern const fb_data sudoku_start[];
extern const fb_data sudoku_inverse[];

#if (LCD_HEIGHT==128) && (LCD_WIDTH==160)
/* For iriver H1x0 - 160x128, 9 cells @ 12x12 with 14 border lines*/

/* Internal dimensions of a cell */
#define CELL_WIDTH 12
#define CELL_HEIGHT 12

#define BOARD_WIDTH (CELL_WIDTH*9+10+4)
#define BOARD_HEIGHT (CELL_HEIGHT*9+10+4)

#define XOFS (((LCD_WIDTH-BOARD_WIDTH)/2)+10)
#define YOFS ((LCD_HEIGHT-BOARD_HEIGHT)/2)

#define XOFSSCRATCHPAD 3

/* Locations of each cell */
static unsigned char cellxpos[9]={ 2, 15, 28, 42, 55, 68, 82, 95, 108 };
static unsigned char cellypos[9]={ 2, 15, 28, 42, 55, 68, 82, 95, 108 };

/* The height of one cell in the bitmap */
#define BITMAP_HEIGHT 12
#define BITMAP_STRIDE 12

#elif (LCD_HEIGHT==64) && (LCD_WIDTH==112)
/* For Archos Recorder, FM and Ondio (112x64):
   9 cells @ 8x6 with 10 border lines
*/

/* Internal dimensions of a cell */
#define CELL_WIDTH 8
#define CELL_HEIGHT 6

#define BOARD_WIDTH (CELL_WIDTH*9+10)
#define BOARD_HEIGHT (CELL_HEIGHT*9+10)

#define XOFS (((LCD_WIDTH-BOARD_WIDTH)/2)+7)
#define YOFS ((LCD_HEIGHT-BOARD_HEIGHT)/2)

#define XOFSSCRATCHPAD 2

/* Locations of each cell */
static unsigned char cellxpos[9]={ 1, 10, 19, 28, 37, 46, 55, 64, 73 };
static unsigned char cellypos[9]={ 1,  8, 15, 22, 29, 36, 43, 50, 57 }; 

/* The height of one cell in the bitmap */
#define BITMAP_HEIGHT 8
#define BITMAP_STRIDE 8

#elif (LCD_HEIGHT>=176) && (LCD_WIDTH>=220)
/* iriver h300 */

/* Internal dimensions of a cell */
#define CELL_WIDTH 16
#define CELL_HEIGHT 16

#define BOARD_WIDTH (CELL_WIDTH*9+10+4)
#define BOARD_HEIGHT (CELL_HEIGHT*9+10+4)

#define XOFS (((LCD_WIDTH-BOARD_WIDTH)/2)+15)
#define YOFS ((LCD_HEIGHT-BOARD_HEIGHT)/2)

#define XOFSSCRATCHPAD 10

/* Locations of each cell */
static unsigned char cellxpos[9]={ 2, 19, 36, 54, 71, 88, 106, 123, 140 };
static unsigned char cellypos[9]={ 2, 19, 36, 54, 71, 88, 106, 123, 140 };

/* The height of one cell in the bitmap */
#define BITMAP_HEIGHT 16
#define BITMAP_STRIDE  16

#else
  #error SUDOKU: Unsupported LCD size
#endif

/* here is a global api struct pointer. while not strictly necessary,
   it's nice not to have to pass the api pointer in all function calls
   in the plugin */
static struct plugin_api* rb;

struct sudoku_state_t {
  char filename[MAX_PATH];  /* Filename */
  char startboard[9][9];    /* The initial state of the game */
  char currentboard[9][9];  /* The current state of the game */
  char savedboard[9][9];    /* Cached copy of saved state */
  int x,y;                  /* Cursor position */
  int editmode;             /* We are editing the start board */
#ifdef SUDOKU_BUTTON_POSSIBLE 
  short possiblevals[9][9];  /* possible values a cell could be, user sets them */
#endif
};

/****** Solver routine by Tom Shackell <shackell@cs.york.ac.uk>

Downloaded from:

http://www-users.cs.york.ac.uk/~shackell/sudoku/Sudoku.html

Released under GPLv2

*/

typedef unsigned int  Bitset;

#define BLOCK        3
#define SIZE         (BLOCK*BLOCK)

#define true  1
#define false 0

typedef struct _Sudoku {
  Bitset          table[SIZE][SIZE];
}Sudoku;

typedef struct _Stats {
  int             numTries;
  int             backTracks;
  int             numEmpty;
  bool            solutionFound;
}Stats;

typedef struct _Options {
  bool            allSolutions;
  bool            uniquenessCheck;
}Options;

void sudoku_init(Sudoku* sud);
void sudoku_set(Sudoku* sud, int x, int y, int num, bool original);
int  sudoku_get(Sudoku* sud, int x, int y, bool* original);

#define BIT(n)          ((Bitset)(1<<(n)))
#define BIT_TEST(v,n)   ((((Bitset)v) & BIT(n)) != 0)
#define BIT_CLEAR(v,n)  (v) &= ~BIT(n)
#define MARK_BIT        BIT(0)
#define ORIGINAL_BIT    BIT(SIZE+1)

#define ALL_BITS        (BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) | BIT(9))

/* initialize a sudoku problem, should be called before using set or get */
void sudoku_init(Sudoku* sud){
  int y, x;
  for (y = 0; y < SIZE; y++){
    for (x = 0; x < SIZE; x++){
      sud->table[x][y] = ALL_BITS;
    }
  }
}

/* set the number at a particular x and y column */
void sudoku_set(Sudoku* sud, int x, int y, int num, bool original){
  int i, j;
  int bx, by;
  Bitset orig;

  // clear the row and columns 
  for (i = 0; i < SIZE; i++){
    BIT_CLEAR(sud->table[i][y], num);
    BIT_CLEAR(sud->table[x][i], num);
  }
  // clear the block 
  bx = x - (x % BLOCK);
  by = y - (y % BLOCK);
  for (i = 0; i < BLOCK; i++){
    for (j = 0; j < BLOCK; j++){
      BIT_CLEAR(sud->table[bx+j][by+i], num);
    }
  }
  // mark the table
  orig = original ? ORIGINAL_BIT : 0;
  sud->table[x][y] = BIT(num) | MARK_BIT | orig;
}

/* get the number at a particular x and y column, if this
   is not unique return 0 */
int sudoku_get(Sudoku* sud, int x, int y, bool* original){
  Bitset val = sud->table[x][y];
  int result = 0;
  int i;

  if (original){
    *original = val & ORIGINAL_BIT;
  }
  for (i = 1; i <= SIZE; i++){
    if (BIT_TEST(val, i)){
      if (result != 0){
        return 0;
      }
      result = i;
    }
  }  
  return result;
}

/* returns true if this is a valid problem, this is necessary because the input
   problem might be degenerate which breaks the solver algorithm. */
static bool is_valid(const Sudoku* sud){
  int x, y;

  for (y = 0; y < SIZE; y++){
    for (x = 0; x < SIZE; x++){
      if ((sud->table[x][y] & ALL_BITS) == 0){
        return false;
      }
    }
  }
  return true;
}

/* scan the table for the most constrained item, giving all it's options,
   sets the best x and y coordinates, the number of options and the options for that coordinate and
   returns true if the puzzle is finished */
static bool scan(const Sudoku* sud, int* rX, int* rY, int *num, int* options){
  int x, y, i, j;
  int bestCount = SIZE+1;
  Bitset val;
  bool allMarked = true;

  for (y = 0; y < SIZE; y++){
    for (x = 0; x < SIZE; x++){
      Bitset val = sud->table[x][y];
      int i;
      int count = 0;

      if (val & MARK_BIT){
        // already set 
        continue;
      }
      allMarked = false;
      for (i = 1; i <= SIZE; i++){
        if (BIT_TEST(val, i)){
          count++;
        }
      }
      if (count < bestCount){
        bestCount = count;
        *rX = x;
        *rY = y;
        if (count == 0){
          // can't possibly be beaten 
          *num = 0;
          return false;
        }
      }
    }
  }
  // now copy into options 
  *num = bestCount;
  val = sud->table[*rX][*rY];
  for (i = 1, j = 0; i <= SIZE; i++){
    if (BIT_TEST(val, i)){
      options[j++] = i;
    }
  }
  return allMarked;
}

static bool solve(Sudoku* sud, Stats* stats, const Options* options);

/* try a particular option and return true if that gives a solution
   or false if it doesn't, restores board on backtracking */
static bool spawn_option(Sudoku* sud, Stats* stats, const Options* options, int x, int y, int num){
  Sudoku copy;

  rb->memcpy(&copy,sud,sizeof(Sudoku));
  sudoku_set(&copy, x, y, num, false);
  stats->numTries += 1;
  if (solve(&copy, stats, options)){
    if (!options->allSolutions && stats->solutionFound){
      rb->memcpy(sud,&copy,sizeof(Sudoku));
    }
    return true;
  }else{
    stats->backTracks++;
  }
  return false;
}

/* solve a sudoku problem, returns true if there is a solution and false otherwise.
   stats is used to track statisticss */
static bool solve(Sudoku* sud, Stats* stats, const Options* options){
  while (true){
    int x, y, i, num;
    int places[SIZE];

    if (scan(sud, &x, &y, &num, places)){
      // a solution was found!
      if (options->uniquenessCheck && stats->solutionFound){
        //printf("\n\t... But the solution is not unique!\n");
        return true;
      }
      stats->solutionFound = true;
      if (options->allSolutions || options->uniquenessCheck){
        //printf("\n\tSolution after %d iterations\n", stats->numTries);
        //sudoku_print(sud);
        return false;
      }else{
        return true;
      }
    }
    if (num == 0){
      // can't be satisfied 
      return false;
    }
    // try all the places (except the last one)
    for (i = 0; i < num-1; i++){
      if (spawn_option(sud, stats, options, x, y, places[i])){
        // solution found!
        if (!options->allSolutions && stats->solutionFound){
          return true;
        }
      }
    }
    // take the last place ourself
    stats->numTries += 1;
    sudoku_set(sud, x, y, places[num-1], false);    
  }
}

/******** END OF IMPORTED CODE */


/* A wrapper function between the Sudoku plugin and the above solver code */
void sudoku_solve(struct sudoku_state_t* state) {
  bool ret;
  Stats stats;
  Options options;
  Sudoku sud;
  bool original;
  int r,c;

  /* Initialise the parameters */
  sudoku_init(&sud);
  rb->memset(&stats,0,sizeof(stats));
  options.allSolutions=false;
  options.uniquenessCheck=false;

  /* Convert Rockbox format into format for solver */
  for (r=0;r<9;r++) {
    for (c=0;c<9;c++) {
      if (state->startboard[r][c]!='0') {
        sudoku_set(&sud, c, r, state->startboard[r][c]-'0', true);
      }
    }
  }

  // need to check for degenerate input problems ...
  if (is_valid(&sud)){
    ret = solve(&sud, &stats, &options);
  } else {
    ret = false;
  }

  if (ret) {
    /* Populate the board with the solution. */
    for (r=0;r<9;r++) {
      for (c=0;c<9;c++) {
        state->currentboard[r][c]='0'+sudoku_get(&sud, c, r, &original);
      }
    }
  } else {
    rb->splash(HZ*2, true, "Solve failed");
  }

  return;
}


void clear_state(struct sudoku_state_t* state)
{
  int r,c;

  state->filename[0]=0;
  for (r=0;r<9;r++) {
    for (c=0;c<9;c++) {
      state->startboard[r][c]='0';
      state->currentboard[r][c]='0';
#ifdef SUDOKU_BUTTON_POSSIBLE 
    state->possiblevals[r][c]=0;
#endif
    }
  }

  state->x=0;
  state->y=0;
  state->editmode=0;
}

/* Load game - only ".ss" is officially supported, but any sensible
   text representation (one line per row) may load.
*/
bool load_sudoku(struct sudoku_state_t* state, char* filename) {
  int fd;
  size_t n;
  int r = 0, c = 0;
  unsigned int i;
  int valid=0;
  char buf[300]; /* A buffer to read a sudoku board from */

  fd=rb->open(filename, O_RDONLY);
  if (fd < 0) {
    rb->splash(HZ*2, true, "Can not open");
    LOGF("Invalid sudoku file: %s\n",filename);
    return(false);
  }

  rb->strncpy(state->filename,filename,MAX_PATH);
  n=rb->read(fd,buf,300);
  if (n <= 0) {
    return(false);
  }
  rb->close(fd);

  r=0;
  c=0;
  i=0;
  while ((i < n) && (r < 9)) {
    switch (buf[i]){
    case ' ': case '\t':
      if (c > 0)
        valid=1;
      break;
    case '|':
    case '*':
    case '-':
    case '\r':
      break;
    case '\n':
      if (valid) {
        r++; 
        valid=0;
      }
      c = 0;
      break;
    case '_': case '.':
      valid=1;
      if (c >= SIZE || r >= SIZE){
        LOGF("ERROR: sudoku problem is the wrong size (%d,%d)\n", c, r);
        return(false);
      }
      c++;
      break;
    default:
      if (((buf[i]>='A') && (buf[i]<='I')) || ((buf[i]>='0') && (buf[i]<='9'))) {
        valid=1;
        if (r >= SIZE || c >= SIZE){
          LOGF("ERROR: sudoku problem is the wrong size (%d,%d)\n", c, r);
          return(false);
        }
        if ((buf[i]>='0') && (buf[i]<='9')) {
          state->startboard[r][c]=buf[i];
          state->currentboard[r][c]=buf[i];
        } else {
          state->currentboard[r][c]='1'+(buf[i]-'A');
        }
        c++;
      }
      /* Ignore any other characters */
      break;
    }
    i++;
  }

  /* Save a copy of the saved state - so we can reload without
     using the disk */
  rb->memcpy(state->savedboard,state->currentboard,81);
  return(true);
}

bool save_sudoku(struct sudoku_state_t* state) {
  int fd;
  int r,c;
  int i;
  char line[16];
  char sep[16];

  rb->memcpy(line,"...|...|...\r\n",13);
  rb->memcpy(sep,"-----------\r\n",13);

  if (state->filename[0]==0) {
    return false;
  }

  fd=rb->open(state->filename, O_WRONLY|O_CREAT);
  if (fd >= 0) {
    for (r=0;r<9;r++) {
      i=0;
      for (c=0;c<9;c++) {
        if (state->startboard[r][c]!='0') {
          line[i]=state->startboard[r][c];
        } else if (state->currentboard[r][c]!='0') {
          line[i]='A'+(state->currentboard[r][c]-'1');
        } else {
          line[i]='.';
        }
        i++;
        if ((c==2) || (c==5)) { i++; }
      }
      rb->write(fd,line,sizeof(line)-1);
      if ((r==2) || (r==5)) {
        rb->write(fd,sep,sizeof(sep)-1);
      }
    }
    /* Add a blank line at end */
    rb->write(fd,"\r\n",2);
    rb->close(fd);
    /* Save a copy of the saved state - so we can reload without
       using the disk */
    rb->memcpy(state->savedboard,state->currentboard,81);
    return true;
  } else {
    return false;
  }
}

void restore_state(struct sudoku_state_t* state)
{
  rb->memcpy(state->currentboard,state->savedboard,81);
}

void clear_board(struct sudoku_state_t* state)
{
  int r,c;

  for (r=0;r<9;r++) {
    for (c=0;c<9;c++) {
      state->currentboard[r][c]=state->startboard[r][c];
    }
  }
  state->x=0;
  state->y=0;
}

void update_cell(struct sudoku_state_t* state, int r, int c) 
{
      /* We have four types of cell:
         1) User-entered number
         2) Starting number
         3) Cursor in cell
      */

      if ((r==state->y) && (c==state->x)) {
        rb->lcd_bitmap_part(sudoku_inverse,0,BITMAP_HEIGHT*(state->currentboard[r][c]-'0'),BITMAP_STRIDE,
             XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
      } else {
        if (state->startboard[r][c]!='0') {
          rb->lcd_bitmap_part(sudoku_start,0,BITMAP_HEIGHT*(state->startboard[r][c]-'0'),BITMAP_STRIDE,
               XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
        } else {
          rb->lcd_bitmap_part(sudoku_normal,0,BITMAP_HEIGHT*(state->currentboard[r][c]-'0'),BITMAP_STRIDE,
               XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
        }
      }

  rb->lcd_update_rect(cellxpos[c],cellypos[r],CELL_WIDTH,CELL_HEIGHT);
}


void display_board(struct sudoku_state_t* state) 
{
  int r,c;

  /* Clear the display buffer */
  rb->lcd_clear_display();

  /* Draw the gridlines - differently for different targets */

#if LCD_HEIGHT > 64
  /* Large targets - draw single/double lines */
  for (r=0;r<9;r++) {
    rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[r]-1);
    rb->lcd_vline(XOFS+cellxpos[r]-1,YOFS,YOFS+BOARD_HEIGHT-1);
    if ((r % 3)==0) { 
      rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[r]-2);
      rb->lcd_vline(XOFS+cellxpos[r]-2,YOFS,YOFS+BOARD_HEIGHT-1);
    }
  }
  rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[8]+CELL_HEIGHT);
  rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[8]+CELL_HEIGHT+1);
  rb->lcd_vline(XOFS+cellxpos[8]+CELL_WIDTH,YOFS,YOFS+BOARD_HEIGHT-1);
  rb->lcd_vline(XOFS+cellxpos[8]+CELL_WIDTH+1,YOFS,YOFS+BOARD_HEIGHT-1);
#elif (LCD_HEIGHT==64)
  /* Small targets - draw dotted/single lines */
  for (r=0;r<9;r++) {
    if ((r % 3)==0) {
      /* Solid Line */
      rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[r]-1);
      rb->lcd_vline(XOFS+cellxpos[r]-1,YOFS,YOFS+BOARD_HEIGHT-1);
    } else {
      /* Dotted line */
      for (c=XOFS;c<XOFS+BOARD_WIDTH;c+=2) {
        rb->lcd_drawpixel(c,YOFS+cellypos[r]-1);
      }
      for (c=YOFS;c<YOFS+BOARD_HEIGHT;c+=2) {
        rb->lcd_drawpixel(XOFS+cellxpos[r]-1,c);
      }
    }
  }
  rb->lcd_hline(XOFS,XOFS+BOARD_WIDTH-1,YOFS+cellypos[8]+CELL_HEIGHT);
  rb->lcd_vline(XOFS+cellxpos[8]+CELL_WIDTH,YOFS,YOFS+BOARD_HEIGHT-1);
#else
  #error SUDOKU: Unsupported LCD height
#endif

#ifdef SUDOKU_BUTTON_POSSIBLE
  rb->lcd_vline(XOFSSCRATCHPAD,YOFS,YOFS+BOARD_HEIGHT-1);
  rb->lcd_vline(XOFSSCRATCHPAD+CELL_WIDTH+1,YOFS,YOFS+BOARD_HEIGHT-1);
  for (r=0;r<9;r++) {
#if LCD_HEIGHT > 64
    /* Large targets - draw single/double lines */
    rb->lcd_hline(XOFSSCRATCHPAD,XOFSSCRATCHPAD+CELL_WIDTH+1,
                  YOFS+cellypos[r]-1);
    if ((r % 3)==0)
      rb->lcd_hline(XOFSSCRATCHPAD,XOFSSCRATCHPAD+CELL_WIDTH+1,
                    YOFS+cellypos[r]-2);
#elif LCD_HEIGHT == 64
    /* Small targets - draw dotted/single lines */
    if ((r % 3)==0) {
      /* Solid Line */
      rb->lcd_hline(XOFSSCRATCHPAD,XOFSSCRATCHPAD+CELL_WIDTH+1,
                    YOFS+cellypos[r]-1);
    } else {
      /* Dotted line */
      for (c=XOFSSCRATCHPAD;c<XOFSSCRATCHPAD+CELL_WIDTH+1;c+=2) {
        rb->lcd_drawpixel(c,YOFS+cellypos[r]-1);
      }
    }
#endif
    if ((r>0) && state->possiblevals[state->y][state->x]&(1<<(r)))
      rb->lcd_bitmap_part(sudoku_normal,0,BITMAP_HEIGHT*r,BITMAP_STRIDE,
                          XOFSSCRATCHPAD+1,YOFS+cellypos[r-1],
                         CELL_WIDTH,CELL_HEIGHT);
  }
  rb->lcd_hline(XOFSSCRATCHPAD,XOFSSCRATCHPAD+CELL_WIDTH+1,
                YOFS+cellypos[8]+CELL_HEIGHT);
#if LCD_HEIGHT > 64
  rb->lcd_hline(XOFSSCRATCHPAD,XOFSSCRATCHPAD+CELL_WIDTH+1,
                YOFS+cellypos[8]+CELL_HEIGHT+1);
#endif
  if (state->possiblevals[state->y][state->x]&(1<<(r)))
      rb->lcd_bitmap_part(sudoku_normal,0,BITMAP_HEIGHT*r,BITMAP_STRIDE,
                          XOFSSCRATCHPAD+1,YOFS+cellypos[8],
                         CELL_WIDTH,CELL_HEIGHT);
#endif

  /* Draw the numbers */
  for (r=0;r<9;r++) {
    for (c=0;c<9;c++) {
      /* We have four types of cell:
         1) User-entered number
         2) Starting number
         3) Cursor in cell
      */

      if ((r==state->y) && (c==state->x)) {
        rb->lcd_bitmap_part(sudoku_inverse,0,BITMAP_HEIGHT*(state->currentboard[r][c]-'0'),BITMAP_STRIDE,
             XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
      } else {
        if (state->startboard[r][c]!='0') {
          rb->lcd_bitmap_part(sudoku_start,0,BITMAP_HEIGHT*(state->startboard[r][c]-'0'),BITMAP_STRIDE,
               XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
        } else {
          rb->lcd_bitmap_part(sudoku_normal,0,BITMAP_HEIGHT*(state->currentboard[r][c]-'0'),BITMAP_STRIDE,
               XOFS+cellxpos[c],YOFS+cellypos[r],CELL_WIDTH,CELL_HEIGHT);
        }
      }
    }
  }

  /* update the screen */
  rb->lcd_update();
}

/* Check the status of the board, assuming a change at the cursor location */
bool check_status(struct sudoku_state_t* state) {
  int check[9];
  int r,c;
  int r1,c1;
  int cell;

  /* First, check the column */
  for (cell=0;cell<9;cell++) { check[cell]=0; }
  for (r=0;r<9;r++) {
    cell=state->currentboard[r][state->x];
    if (cell!='0') {
      if (check[cell-'1']==1) {
        return true;
      }
      check[cell-'1']=1;
    }
  }

  /* Second, check the row */  
  for (cell=0;cell<9;cell++) { check[cell]=0; }
  for (c=0;c<9;c++) {
    cell=state->currentboard[state->y][c];
    if (cell!='0') {
      if (check[cell-'1']==1) {
        return true;
      }
      check[cell-'1']=1;
    }
  }

  /* Finally, check the 3x3 sub-grid */
  for (cell=0;cell<9;cell++) { check[cell]=0; }
  r1=(state->y/3)*3;
  c1=(state->x/3)*3;
  for (r=r1;r<r1+3;r++) {
    for (c=c1;c<c1+3;c++) {
      cell=state->currentboard[r][c];
      if (cell!='0') {
        if (check[cell-'1']==1) {
          return true;
        }
        check[cell-'1']=1;
      }
    }
  }

  /* We passed all the checks :) */

  return false;
}

int sudoku_menu_cb(int key, int m)
{
    (void)m;
    switch(key)
    {
#ifdef MENU_ENTER2
    case MENU_ENTER2:
#endif
    case MENU_ENTER:
        key = BUTTON_NONE; /* eat the downpress, next menu reacts on release */
        break;

#ifdef MENU_ENTER2
    case MENU_ENTER2 | BUTTON_REL:
#endif
    case MENU_ENTER | BUTTON_REL:
        key = MENU_ENTER; /* fake downpress, next menu doesn't like release */
        break;
    }

    return key;
}

bool sudoku_menu(struct sudoku_state_t* state)
{
  int m;
  int result;

  static const struct menu_item items[] = {
    { "Save", NULL },
    { "Reload", NULL },
    { "Clear", NULL },
    { "Solve", NULL },
    { "New", NULL },
  };

  m = rb->menu_init(items, sizeof(items) / sizeof(*items),
                sudoku_menu_cb, NULL, NULL, NULL);

  result=rb->menu_show(m);

  switch (result) {
    case 0: /* Save state */
      save_sudoku(state);
      break;

    case 1: /* Restore state */
      restore_state(state);
      break;

    case 2: /* Clear all */
      clear_board(state);
      break;

    case 3: /* Solve */
      sudoku_solve(state);
      break;

    case 4: /* Create a new game manually */
      clear_state(state);
      state->editmode=1;
      break;

    default:
      break;
  }

  rb->menu_exit(m);

  return (result==MENU_ATTACHED_USB);
}

void move_cursor(struct sudoku_state_t* state, int newx, int newy) {
  int oldx, oldy;

  /* Check that the character at the cursor position is legal */
  if (check_status(state)) {
    rb->splash(HZ*2, true, "Illegal move!");
    /* Ignore any button presses during the splash */
    rb->button_clear_queue();
    return;
  }

  /* Move Cursor */
  oldx=state->x;
  oldy=state->y;
  state->x=newx;
  state->y=newy;

  /* Redraw current and old cells */
  update_cell(state,oldx,oldy);
  update_cell(state,newx,newy);  
}

/* plugin entry point */
enum plugin_status plugin_start(struct plugin_api* api, void* parameter)
{
  bool exit;
  int button;
  int lastbutton = BUTTON_NONE;
  long ticks;
  struct sudoku_state_t state;

  /* plugin init */
  rb = api;
  /* end of plugin init */

  clear_state(&state);

  if (parameter==NULL) {
    state.editmode=1;
  } else {
    if (!load_sudoku(&state,(char*)parameter)) {
      rb->splash(HZ*2, true, "Load error");
      return(PLUGIN_ERROR);
    }
  }

  display_board(&state);

  /* The main game loop */
  exit=false;
  ticks=0;
  while(!exit) {
    button = rb->button_get(true);

    switch(button){
      /* Exit game */
      case SUDOKU_BUTTON_QUIT:
        exit=1;
        break;

      /* Increment digit */
#ifdef SUDOKU_BUTTON_ALTTOGGLE
      case SUDOKU_BUTTON_ALTTOGGLE | BUTTON_REPEAT:
#endif
      case SUDOKU_BUTTON_TOGGLE | BUTTON_REPEAT:
        /* Slow down the repeat speed to 1/3 second */
        if ((*rb->current_tick-ticks) < (HZ/3)) {
          break;
        }

#ifdef SUDOKU_BUTTON_ALTTOGGLE
      case SUDOKU_BUTTON_ALTTOGGLE:
#endif
      case SUDOKU_BUTTON_TOGGLE:
#ifdef SUDOKU_BUTTON_TOGGLE_PRE
        if ((button == SUDOKU_BUTTON_TOGGLE)
            && (lastbutton != SUDOKU_BUTTON_TOGGLE_PRE))
            break;
#endif
        /* Increment digit */
        ticks=*rb->current_tick;
        if (state.editmode) {
          if (state.startboard[state.y][state.x]=='9') { 
            state.startboard[state.y][state.x]='0';
            state.currentboard[state.y][state.x]='0';
          } else {
            state.startboard[state.y][state.x]++;
            state.currentboard[state.y][state.x]++;
          }
        } else {
          if (state.startboard[state.y][state.x]=='0') {
            if (state.currentboard[state.y][state.x]=='9') { 
              state.currentboard[state.y][state.x]='0';
            } else {
              state.currentboard[state.y][state.x]++;
            }
          }
        }
        update_cell(&state,state.y,state.x);
        break;

      /* move cursor left */
      case BUTTON_LEFT:
      case (BUTTON_LEFT | BUTTON_REPEAT):
        if (state.x==0) {
          move_cursor(&state,8,state.y);
        } else { 
          move_cursor(&state,state.x-1,state.y);
        }
        break;

      /* move cursor right */
      case BUTTON_RIGHT:
      case (BUTTON_RIGHT | BUTTON_REPEAT):
        if (state.x==8) {
          move_cursor(&state,0,state.y);
        } else { 
          move_cursor(&state,state.x+1,state.y);
        }
        break;

      /* move cursor up */
      case SUDOKU_BUTTON_UP:
      case (SUDOKU_BUTTON_UP | BUTTON_REPEAT):
        if (state.y==0) {
          move_cursor(&state,state.x,8);
        } else { 
          move_cursor(&state,state.x,state.y-1);
        }
        break;

      /* move cursor down */
      case SUDOKU_BUTTON_DOWN:
      case (SUDOKU_BUTTON_DOWN | BUTTON_REPEAT):
        if (state.y==8) {
          move_cursor(&state,state.x,0);
        } else { 
          move_cursor(&state,state.x,state.y+1);
        }
        break;

      case SUDOKU_BUTTON_MENU:
#ifdef SUDOKU_BUTTON_MENU_PRE
        if (lastbutton != SUDOKU_BUTTON_MENU_PRE)
            break;
#endif
        /* Don't let the user leave a game in a bad state */
        if (check_status(&state)) {
          rb->splash(HZ*2, true, "Illegal move!");
          /* Ignore any button presses during the splash */
          rb->button_clear_queue();
        } else {
          if (state.editmode) {
            rb->kbd_input(state.filename,MAX_PATH);
            if (save_sudoku(&state)) {
              state.editmode=0;
            } else {
              rb->splash(HZ*2, true, "Save failed");
            }
          } else {
            if (sudoku_menu(&state)) {
              return PLUGIN_USB_CONNECTED;
            }
          }
        }
        break;
#ifdef SUDOKU_BUTTON_POSSIBLE
    case SUDOKU_BUTTON_POSSIBLE:
      /* Toggle current number in the possiblevals structure */
      if (state.currentboard[state.y][state.x]!='0') {
          state.possiblevals[state.y][state.x]^=
             (1 << (state.currentboard[state.y][state.x] - '0'));
      }
      break;
#endif
    default:
        if (rb->default_event_handler(button) == SYS_USB_CONNECTED) {
          /* Quit if USB has been connected */
          return PLUGIN_USB_CONNECTED;
        }
        break;
    }
    if (button != BUTTON_NONE)
        lastbutton = button;

    display_board(&state);
  }

  return PLUGIN_OK;
}

#endif
