#include <stdlib.h>
#include <unistd.h>
#include <termios.h>


struct termios orig_termios;

void disableRawMode(){
	tcsetattr(STDIN_FILENO, TCSAFLUSH,  &orig_termios);
}

void enableRawMode(){
	tcgetattr(STDIN_FILENO, &orig_termios); //read current terminal attributes into struct
	atexit(disableRawMode);

	struct termios raw = orig_termios;

	raw.c_cflag &= ~(ECHO); //turn off echo: c_cflag is basically a misc flag
	
	//load in struct. TCSAFLUSH will wait for output to be written to term before setting.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 
}


int main (){
	enableRawMode();

	char c;
	while(read(STDIN_FILENO,  &c, 1) == 1 && c != 'q'); //read 1 byte from input into c until there are no bytes left
	return 0;
}
