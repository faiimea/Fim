/** includes **/
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

/** define **/ 

#define define_section 1
#define FIM_VERSION "0.02"
#define FIM_TAB_STOP 8
// Ctrl+letter -> ASMII 1-26
// ox1f -> 00011111 (quit 3 bit of input)
// eg: 0100 0001 is A , 0110 0001 is a 
#define CTRL_KEY(k) ((k) & 0x1f)
enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT ,
  ARROW_UP ,
  ARROW_DOWN , 
  PAGE_UP , 
  PAGE_DOWN , 
  HOME_KEY , 
  END_KEY  , 
  DEL_KEY , 
};

/** data **/

#define data_section 1 
typedef struct erow{
  int size;
  int rsize;
  char *chars;
  // Render stands for real char in screen.
  char *render;
} erow;

struct Editor_Config{
  int cx,cy;
  int rx;
  int rowoff;
  int coloff;
  int Screen_Rows;
  int Screen_Cols;
  struct termios orig_termios;
  int num_rows;
  erow *row;
  char* File_Name;
  char status_msg[64];
  //time_t from <time.h>
  time_t status_msg_time;
};
 
struct Editor_Config E;

/** terminal **/ 
#define terminal_section 1 

//print error message
void End(const char *s)
{
  write(STDOUT_FILENO,"\x1b[2J",4);
  write(STDOUT_FILENO,"\x1b[H",3);
  perror(s);
  exit(1);
}

void Disable_Raw_Mode()
{
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&E.orig_termios) == -1) End("Fail to set attr");
}

void Enable_Raw_Mode()
{
  // Read the input now to struct
  // When use pipe(fim | a.c), program will end... :(
  if(tcgetattr(STDIN_FILENO,&E.orig_termios) == -1) End("Fail to get attr");
  // Atexit can registor the function, and call it automatically when program exit. 
  atexit(Disable_Raw_Mode);
  struct termios raw = E.orig_termios;
  // Close ECHO function for raw mode (use & way) 
  // ECHO is a bit flag -> bit compute
  raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
  raw.c_iflag &= ~(BRKINT|IXON|ICRNL|INPCK|ISTRIP);
  raw.c_oflag &= ~(OPOST);
  // Set the terminal attr without ECHO and ICANON(read as line)
  // Arg TCSAFLUSH is used to determine when to change attr of terminal
  // ISIG will stop the ctrl-c(end the program) and ctrl-z(stop the program)
  // I represennt input flag, XON is ctrl-s and ctrl-q
  // Get more information of termios in https://blog.csdn.net/flfihpv259/article/details/53786604  
  raw.c_cflag |= (CS8);
  // Read return after 0 byte and 100ms;
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw) == -1)  End("Fail to get attr");
}

void Fix_Raw_Mode()
  {
    struct termios raw;
    tcgetattr(STDIN_FILENO,&raw);
    raw.c_lflag &= (ECHO);
    tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw);
  }


int Editor_Read_Key()
{
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1))!= 1)
  {
    // If there is not input, the Editor_Read_Key will stop here.
    if(nread == -1 && errno != EAGAIN)  End("Fail to read");
  }
  // for -> , def a buffer for 3 bit.
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY; 
          }
        }
      } 
      else 
      {
      switch (seq[1]) 
        {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
        }
      }
    }
    else if (seq[0] == 'O') 
    {
      switch (seq[1]) 
      {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    return '\x1b';
  }
  else {
    return c;
  }
}

int Get_Cursor_Position(int *rows, int *cols)
{
  char buf[32];
  unsigned int i=0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

int Get_Window_Size(int *rows, int *cols)
{
  // Winsize is built in <sys/ioctl.h>
  struct winsize ws;
  // 999 will ensure the cursor arrive the bottom-right.
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return Get_Cursor_Position(rows,cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  } 
}

/** row operation **/ 
#define row_section 1

int Editor_Row_CxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (FIM_TAB_STOP - 1) - (rx % FIM_TAB_STOP);
    rx++;
  }
  return rx;
}

void Editor_Update_Row(erow *row) {
  // Deal with unshown char.
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*(FIM_TAB_STOP-1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % FIM_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void Editor_Append_Row(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow)*(E.num_rows+1));
  int at=E.num_rows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.num_rows ++;
  E.row[at].rsize=0;
  E.row[at].render=NULL;
  Editor_Update_Row(&E.row[at]);
}

/** file i/0 **/ 
#define file_section 1 


void Editor_Open(char *filename) {
  // File from stdio.h 
  free(E.File_Name);
  // strdup will copy string to new memory;
  E.File_Name = strdup(filename);
  FILE *fp = fopen(filename,"r");
  if(!fp) End("Fail to open");
  char *line = NULL;
  size_t linecap=0;
  ssize_t linelen ;
  int totlen=0;
  while((linelen=getline(&line,&linecap,fp))!=-1)
  {
    totlen++;
  while(linelen>0 && (line[linelen-1]=='\n'||line[linelen-1]=='\r')){
    linelen--;
    Editor_Append_Row(line,linelen);
  }
  }
  //printf("%d\n",totlen);
  free(line);
  fclose(fp);
}

/** append buffer **/ 
#define buf_section 1 
// construct the dynamic string in c:
// use buffer to batch processing instead of tons of 'write'~

#define ABUF_INIT {NULL,0}

struct abuf{
  char *b;
  int len;
};

void ab_Append(struct abuf *ab, const char *s, int len){
  // Realloc is not equal to malloc
  // Realloc will expand existed memory block at the same location.
  char *new = realloc(ab->b,ab->len+len);
  if(!new)  return;
  memcpy(&new[ab->len],s,len);
  ab->b=new;
  ab->len+=len;
}

void ab_Free(struct abuf *ab){
  free(ab->b);
}

/** output **/
#define output_section 1

void Editor_Scroll() {
  // Determine the terminal by cursor
  E.rx = E.cx ;
  if (E.cy < E.rowoff) {
    E.rx = Editor_Row_CxToRx(&E.row[E.cy],E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.Screen_Rows) {
    E.rowoff = E.cy - E.Screen_Rows + 1;
  }
  if (E.rx < E.coloff){
    E.coloff = E.cx;
  }
  if(E.rx >= E.coloff + E.Screen_Cols){
    E.coloff = E.rx - E.Screen_Cols +1;
  }
}

void Editor_Draw_Rows(struct abuf *ab)
{
  // Draw
  int y;
  for(y=0; y<E.Screen_Rows; y++)
  {
    // rowoff means the row which terminal starts
    int filerow = y+E.rowoff;
    if(filerow>=E.num_rows)
    {
    // with no file content.
      if(E.num_rows==0 && y==E.Screen_Rows/3)
    {
      char welcome[80];
      //snprintf will write string to specific buffer
      int Welcome_len = snprintf(welcome,sizeof(welcome),"Fim editor --version %s",FIM_VERSION);
      if(Welcome_len > E.Screen_Cols) Welcome_len=E.Screen_Cols;
      int padding = (E.Screen_Cols - Welcome_len) /2;
      if(padding)
        {
          ab_Append(ab,"~",1);
          padding--;
        }
      while(padding--) ab_Append(ab," ",1);
      ab_Append(ab,welcome,Welcome_len);
    }
      else  ab_Append(ab,"~", 1);}
    else
    {
      // with file content.
      // coloff is started col of this row.
      int len=E.row[filerow].rsize - E.coloff;
      if(len < 0) len=0;
      if(len>E.Screen_Cols) len=E.Screen_Cols;
      ab_Append(ab,&E.row[filerow].render[E.coloff],len);
    }
    // \1n[K to refresh this row.
    ab_Append(ab,"\x1b[K",3);
    
      // The last row error
      ab_Append(ab,"\r\n", 2);
    
  }
}

void Editor_Draw_Status_Bar(struct abuf *ab) {
  // [7m will use reversal color
  ab_Append(ab, "\x1b[7m", 4);
  char status[64],rstatus[64];
  int len = snprintf(status,sizeof(status),"%.20s - %d lines", E.File_Name ? E.File_Name : "[NEW_FILE]",E.num_rows);
  int rlen = snprintf(rstatus, sizeof(rstatus),"%d/%d",E.cy+1,E.num_rows);

  if(len > E.Screen_Cols) len=E.Screen_Cols;
  ab_Append(ab,status,len);
  while (len < E.Screen_Cols) {
    if(E.Screen_Cols - len == rlen)
    {
      ab_Append(ab,rstatus,rlen);
      break;
    }
    else{
    ab_Append(ab, " ", 1);
    len++;
    }
  }
  // return color
  ab_Append(ab, "\x1b[m", 3);
  ab_Append(ab, "\r\n",2);
}

void Editor_Draw_Message_Bar(struct abuf *ab) {
  // Print msg 
  ab_Append(ab, "\x1b[K", 3);
  int msglen = strlen(E.status_msg);
  if (msglen > E.Screen_Cols) msglen = E.Screen_Cols;
  if (msglen && time(NULL) - E.status_msg_time < 5)
    ab_Append(ab, E.status_msg, msglen);
}


void Editor_Refresh_Screen()
{
  Editor_Scroll();
  // Here define the abuf
  struct abuf ab = ABUF_INIT;
  // Write will write sth to terminal.
  // \x1b change meaning of sequence, and [ will ask the terminal to perform the task
  // J will clean the terminal.
  // See more in VT100 manual.
  // 25l to hide the cursor
  ab_Append(&ab,"\x1b[?25l",6);
  // Clean terminal will change the location of cursor. So H will relocate cursor to upper left.
  ab_Append(&ab,"\x1b[H",3);
  Editor_Draw_Rows(&ab);
  Editor_Draw_Status_Bar(&ab);
  Editor_Draw_Message_Bar(&ab);
  char buf[32];
  // Which draw the location of cursor
  snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy-E.rowoff+1,E.rx-E.coloff+1);
  ab_Append(&ab,buf,strlen(buf));
  // 25h to show the cursor
  ab_Append(&ab,"\x1b[?25h",6);
  
  // Rewrite buf to STDOUT_FILENO
  write(STDOUT_FILENO,ab.b,ab.len);
  ab_Free(&ab);
}

void Editor_Set_Status_Message(const char *fmt, ...) {
  // Make num of var varies.
  // Let's recall Computer programming practice...
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
  va_end(ap);
  E.status_msg_time = time(NULL);
}

/** input **/

#define input_section 1


// Editor_Move_Cursor will change cursor location with key.
void Editor_Move_Cursor(int key) {
  erow *row = (E.cy >= E.num_rows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }
      else if(E.cy>0)
      {
        E.cy--;
        E.cx=E.row[E.cy].size;
    }
      break;
    case ARROW_RIGHT:
      // limit of cursor
      if(row && E.cx < row->size)
      {E.cx++;}
      else if(row && E.cx == row->size)
      {
        E.cy++;
        E.cx=0;
      // my vim doesn't have this part...
    }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      // Here cause a bug!!!!
      // cy<=Screen_Rows make the content wrong :(
      if (E.cy <= E.num_rows - 1) {
        E.cy++;
      }
      break;
  }
  row = (E.cy >= E.num_rows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  } 
}

void Editor_Process_Keypress()
{
  int c=Editor_Read_Key();
  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO,"\x1b[2J",4);
      write(STDOUT_FILENO,"\x1b[H",3);
      exit(0);
      break;
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.Screen_Cols - 1;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.Screen_Rows;
        while (times--)
          Editor_Move_Cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      Editor_Move_Cursor(c);
      break;
  }
}

/** init **/


#define init_section 1 
void Init_Editor(){
  E.cx = 0;
  E.cy = 0;
  E.cy = 0;
  E.num_rows=0;
  E.rowoff=0;
  E.coloff=0;
  E.row = NULL;
  if(Get_Window_Size(&E.Screen_Rows, &E.Screen_Cols)==-1) End("Fail to get windowssize!");
  // Init the screen size with status row;
  E.Screen_Rows -=2;
  E.File_Name = NULL;
}

int main(int argc, char *argv[])
{
  Enable_Raw_Mode();
  Init_Editor();
  if(argc>=2)
  {
    Editor_Open(argv[1]);
  }
  // Read from STDIN_FILENO for 1 byte.

  Editor_Set_Status_Message("HELP:");
  while (1)
  {
    Editor_Refresh_Screen();
    Editor_Process_Keypress();
  }
// see more in https://github.com/snaptoken/kilo-src/commits/master
  return 0;
}
