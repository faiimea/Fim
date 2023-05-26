#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

struct termios orig_termios;

void Disable_Raw_Mode()
{
  tcsetattr(STDIN_FILENO,TCSAFLUSH,&orig_termios);
}

void Enable_Raw_Mode()
{
  // Read the input now to struct
  tcgetattr(STDIN_FILENO,&orig_termios);
  // Atexit can registor the function, and call it automatically when program exit. 
  atexit(Disable_Raw_Mode);
  struct termios raw = orig_termios;
  // Close ECHO function for raw mode (use & way) 
  // ECHO is a bit flag -> bit compute
  raw.c_lflag &= ~(ECHO);
  // Set the terminal attr without ECHO
  // Arg TCSAFLUSH is used to determine when to change attr of terminal
  // Get more information of termios in https://blog.csdn.net/flfihpv259/article/details/53786604  
  tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw);
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
  char c;
  
  // Read from STDIN_FILENO for 1 byte.
  while (read(STDIN_FILENO,&c,1)==1 & c!='q')
  {
    // iscntrl test if a input char is control char which cannot printf.
    if (iscntrl(c))
    {
      printf("%d\n",c);
    }
    else
    {
      printf("%d('%c')\n",c,c);
    }
  }
  return 0;
}
