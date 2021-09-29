This program runs on Linux OS. 

This shell parses command line input, executes built in commands (cd, status, exit), and handles blank and comment lines (beginning with '#').
The shell also redirects input and output to files, and executes commands in the foreground or background (if indicated by trailing '&'). 
The program creates child processes to execute background commands, and uses signals to detect errors and alert the user.

Compile instructions--
gcc --std=gnu99 -o smallsh smallsh.c -Wall
smallsh
