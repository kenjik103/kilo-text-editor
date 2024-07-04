#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>


struct termios orig_termios; //original terminal settings

void disableRawMode(){
	tcsetattr(STDIN_FILENO, TCSAFLUSH,  &orig_termios);
}

void enableRawMode(){
	tcgetattr(STDIN_FILENO, &orig_termios); //read current terminal attributes into struct
	atexit(disableRawMode);

	struct termios raw = orig_termios;


	//note: some of these flags aren't really used and won't have a tangible effect, but whoever designed "raw mode" decided
	//they should be turned off. I labeled the important ones.

	//i_flag: input flags
	//IXON: flow control (ctrl-s, ctrl-q)
	//ICRNL: carriage returns (ctrl-m)
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

	//oflag: output flag
	//OPOST: automatically prepends each \n with a carriage return
	raw.c_oflag &= ~(OPOST);

	raw.c_cflag |= (CS8);

	//c_lflag: misc flags; contains both echo and icanon
	//ECHO: prints typed keys to terminal
	//ICANON: Canonical mode. Waits before ENTER is hit before taking input
	//ISIG: contains SIGINT and SIGSTP, or the program killing commands (ctrl c, ctrl z)
	//IEXTEN: waits for input then sends that as literal char (ctrl-v)
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); 	

	//load in struct. TCSAFLUSH will wait for output to be written to term before setting.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); 
}


int main (){
	enableRawMode();

	char c;
	while(read(STDIN_FILENO,  &c, 1) == 1 && c != 'q'){//read 1 byte from input into c until there are no bytes left
		if (iscntrl(c)){ //if input is control type
			printf("%d\r\n", c); //print ascii value of control type
		} else {
			printf("%d '(%c)'\r\n", c, c); 
		}
	}
	return 0;
}
