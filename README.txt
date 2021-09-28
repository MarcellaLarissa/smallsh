This program runs on Linux OS. This shell parses command line input, opens files, executes commands in the foreground or background. 
The program creates child processes to execute commands in the background, and uses signals to detect errors and alert the user.

Compile instructions--
gcc --std=gnu99 -o smallsh smallsh.c -Wall
smallsh
