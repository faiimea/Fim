#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

struct termios orig_termios;

//print error message
void End(const char *s)
{
  perror(s);
  exit(1);
}

void Disable_Raw_Mode()
{
  if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios) == -1) End("Fail to set attr");
}

void Enable_Raw_Mode()
{
  // Read the input now to struct
  // When use pipe(fim | a.c), program will end... :(
  if(tcgetattr(STDIN_FILENO,&orig_termios) == -1) End("Fail to get attr");
  // Atexit can registor the function, and call it automatically when program exit. 
  atexit(Disable_Raw_Mode);
  struct termios raw = orig_termios;
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

  int main()
  {
    Enable_Raw_Mode();
    // Fix_Raw_Mode();
  
  // Read from STDIN_FILENO for 1 byte.
  while (1)
  {
    char c = '\0';
    if(read(STDIN_FILENO, &c, 1) == -1) End("Fail to read");
    // iscntrl test if a input char is control char which cannot printf.
    if (iscntrl(c))
    {
      printf("%d\r\n",c);
    }
    else
    {
      printf("%d('%c')\r\n",c,c);
    }
    if (c == 'q') break;
  }
  return 0;
}
