#define _GNU_SOURCE
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>



const char* term = "$$";
const char* home = "~";
const char* cd = "cd";
#define MAX_ARGS 512
/**********************************************
function and struct prototypes
***********************************************/
struct aCommand
{
    char* program;
    char* args[MAX_ARGS];
    char* input_file;
    char* output_file;
    int   is_background;
    int numTokens;
};

void cmdPrompt();
char* cmdInput(char* input);
int isComment(char* rawInput);
char** inputParse(char* rawInput, char** tokenList, struct aCommand* cmd);
char** getExpTokens(char** tokenList, int numTokens);
char* expandToken(char* tokenList);
char* get_pid_str();
char** findArgs(char** expTokenList, struct aCommand* cmd);
int isSpecialToken(char* token);
int findGT(char** expandedTokens, struct aCommand* cmd);
int findLT(char** expandedTokens, struct aCommand* cmd);
int findAmp(char** expandedTokens, struct aCommand* cmd);
//int isRedirect(char** expandedTokens, struct aCommand* cmd);
//int isBackground(char** expandedTokens, struct aCommand* cmd);
int redirectInput(struct aCommand* cmd);
int redirectOutput(struct aCommand* cmd);
int redirectOutputBG(struct aCommand* cmd);
int redirectInputBG(struct aCommand* cmd);
void runBackground(struct aCommand* cmd, int *exitStatus);
void printCommand(struct aCommand* cmd);
void changeDir(struct aCommand* cmd);
int getStatus(struct aCommand* lastCommand, int exitStatus);
void printStatus(int exitStatus);
int exitShell(struct aCommand* cmd);
void runCommand(struct aCommand *cmd, int *exitStatus);
void runBuiltInCommand(struct aCommand *cmd, int exitStatus);
int isBuiltInCommand(char* c);
void handleINTToggle();
void handleBGToggle();

//global
int allowBG = 1;
int allowINT = 1;
/**********************************************
*
* This block of functions regards the outer shell:
* cmd prompt and input, and comments or blank lines
*
***********************************************/
void cmdPrompt() {
    printf(":");
    fflush(stdout);
}

char* cmdInput(char* input) {
    char* rawInput = input;
    fgets(rawInput, 512, stdin);
    return rawInput;
}

int isComment(char* rawInput) {
    //use for input debugging
    /*char* s = rawInput;
    while(*s != '\0'){
        printf("'%c'|", *s);// switch %d and cast to int to see ASCII numbers of chars
        s++;
    }
    printf("\n");*/
    //return 1;
    //check the pointer
    if(!rawInput){
        return 1;
    }
    //check string length
    if(rawInput[0] == '\0'){
        return 1;
    }
    //check newline
    if (rawInput[0] == '\n'){
        return 1;
    }
    //check for comment #
    if (rawInput[0] == '#'){
        return 1;
    }
    //check for white space
    char* str = rawInput;
    int hasNonWhiteSpaceCharacters = 0;
    //while not null terminated string
    while(*str != '\0'){
        if(!isspace(*str)){
            hasNonWhiteSpaceCharacters = 1;
        }
        str++;
    }
    if(hasNonWhiteSpaceCharacters == 0){
        return 1;
    }
    // golden
    return 0;
}

/**********************************************
*
* This block of functions regards parsing user input
* searching for special characters, expanding tokens with pid, and initializing the cmd struct
*
***********************************************/
/*
* this function takes the list of raw tokens, a pointer to a token list, and the command struct,
* loops through list of raw tokens and parses at spaces, copies to tokenList
* then calls getExpTokens, updates the struct with numTokens, and returns the Expanded tokenList
*
*/
char**  inputParse(char* rawInput, char** tokenList, struct aCommand* cmd) {
    int inputLength = strlen(rawInput);
    //change newlline to null term
    if (rawInput[inputLength - 1] == '\n') {
        rawInput[inputLength - 1] = '\0';
    }
    
    // set to null values
    //memset(tokenList, '\0', sizeof(tokenList));//reset to null

    char* rest = rawInput;
    char* token;
    int numTokens = 0;
    // we grab token if not null
    while ((token = strtok_r(rest, " ", &rest))) {
        tokenList[numTokens] = strdup(token);//duplicate token into list
        numTokens++;
    }
    tokenList = getExpTokens(tokenList, numTokens);
    
    cmd->numTokens = numTokens;
    return tokenList;
   
}

/*
* this function is called in inputParse()
* it takes the list of parsed tokens, loops through and calls expandToken() function on each token, 
* returns the full list
* 
*/
char** getExpTokens(char** tokenList, int numTokens) {
    char** expTokenList = calloc(numTokens + 1, sizeof(char*));
    //char** expTokenList = emptyTokenList;
    // set to null values
    //memset(expTokenList, '\0', sizeof(expTokenList));//reset to null

    // call function to check for $$ and expand any instances for each token
    char* expanded;
    //char* expanded = expandToken(tokenList);
    for (int i = 0; i < numTokens; i++) {
        char* token2 = tokenList[i];
        expanded = expandToken(token2);
        printf("token: %s expanded: %s\n", token2, expanded);
        expTokenList[i] = strdup(expanded);// duplicate into list
    }
    return expTokenList;
}

/* 
* this function takes a token and expands if term '$$' is found
* it is called in getExpTokens()
*/
char* expandToken(char* tokenList) {
    char* token = tokenList;
    // if there is no $$ just return
    if (!strstr(token, term)) {
        return token;
    }

    //allocate 
    char* buf = malloc(4096);
    char* p = NULL;
    char* rest = token;
    // while we are still finding da money
    while ((p = strstr(rest, term))) {
        strncat(buf, rest, (size_t)(p - rest));
        strcat(buf, get_pid_str());
        rest = p + 2;
    }
    // no more money, tack on what's left
    strcat(buf, rest);
    return buf;

}


//This function gets the pid of current process and returns it as a string **written by Michael Slater

char* get_pid_str() {
    int pid = (int)getpid();
    char* s = malloc(32);
    sprintf(s, "%d", pid); // should use safe version
    return s;
}

// this function takes a double pointer to an array of expanded tokens, aCommand struct, and the number of tokens
// loops through the array, calls isSpecialToken on each token. If the token is special and is either '<' or '>', 
// the corresponding function is called and the next token is handled as a correstponding input/output file
// if the token is special and is '&' a flag is added to the is_background member of the struct.
// if the token is not special, it is an argument and added to the struct member arguments list.
char** findArgs(char** expTokenList, struct aCommand* cmd) {
    int numTokens = cmd->numTokens;
    for(int a = 0; a < MAX_ARGS;a++){
        cmd->args[a] = NULL;
    }
    // isSpecial returns 0 if a special token is found
    // we found a special char
    
    //findGT() findeLT and findAmp() will return -1 if no match is found and no redirection is needed, 
    //or if a match is not found return the index of the '>' or '<' token (next token will be file name)

    int x = findGT(expTokenList, cmd);
    int y = findLT(expTokenList, cmd);
    int z = findAmp(expTokenList, cmd);

    // is it greater than ?
    if (x != -1) {
        
        cmd->output_file = expTokenList[x + 1];
        // redirectOutput(cmd);
    }
    // or is it less than ?
    if (y != -1) {
        
        cmd->input_file = expTokenList[y + 1];
    //     redirectInput(cmd);
    }
    // must be ampersand
    if (z != -1) {
        cmd->is_background = 1;
        // runBackground(cmd);
    }
    
    int argIndex = 0;
    // check array for special tokens
    for (int i = 0; i < numTokens; i++) {
        //printf("hi, I'm exptokenList[i]! %s\n", expTokenList[i]);

        // isSpecial returns 1 in case of no special token found
        //this evaluates to false here and adds the token to our argument  list
        if (isSpecialToken(expTokenList[i])) {
            //if its a speial token, continue
            continue;
        }
        //if it's an argument to a special token, continue
        if(x != -1 && i == x + 1){
            continue;
        }
        if(y != -1 && i == y + 1){
            continue;
        }
        //add arg at next open space in args[]-- this avoids gaps in char array
        cmd->args[argIndex] = expTokenList[i];
        argIndex++;
    }
            
    return cmd->args;
}

/*
* This function checks if the token passed to it is indeed special
* it returns 1 for each of the special tokens
* and 0 for non special tokens
* it is called in findArgs() which loops through the expanded token list
*/
int isSpecialToken(char* token) {
    char* greaterThan = ">";
    char* lessThan = "<";
    char* ampersand = "&";

    //printf("comparing:  %s\n", token);

    // strcmp returns 0 in case of a match, not 0 in case of no match, so in case of not match x3, we return 1, no special char
    if ((!strcmp(token, lessThan))) {// case of <
        return 1;
    }
    else if ((!strcmp(token, greaterThan))) {// case of >
        return 1;
    }
    else if ((!strcmp(token, ampersand))) {// case of &
        return 1;
    }return 0;
}

/*****************************************************************************
*
* this block of functions identifies which special character has been found in isSpecialToken()
* these functions are called in findArgs()
*
******************************************************************************/

int findGT(char** expandedTokens, struct aCommand* cmd) {
    char* greaterThan = ">";
    int numTokens = cmd->numTokens;
    for (int x = 0; x < numTokens; x++)
    {
        if (!expandedTokens[x])
            break;
        if (!strcmp(expandedTokens[x], greaterThan))
            return x;
    }
    return -1;
}

int findLT(char** expandedTokens, struct aCommand* cmd) {
    char* lessThan = "<";
    int numTokens = cmd->numTokens;
    for (int x = 0; x < numTokens; x++)
    {
        if (!expandedTokens[x])
            break;
        if (!strcmp(expandedTokens[x], lessThan))
            return x;
    }
    return -1;
}

int findAmp(char** expandedTokens, struct aCommand* cmd) {
    char* amp = "&";
    int numTokens = cmd->numTokens;
    for (int x = 0; x < numTokens; x++)
    {
        if (!expandedTokens[x])
            break;
        if (!strcmp(expandedTokens[x], amp))
            return x;
    }
    return -1;
}
//print command structure
void printCommand(struct aCommand* cmd) {
    for(int a = 0; a < MAX_ARGS;a++){
        if(cmd->args[a] != NULL){
              fprintf(stderr, "arg %d: %s | ",a, cmd->args[a]);
        }          
    }
    fprintf(stderr,"\n");
    //printf("The struct args is %s\n", cmd->args[0]);
    fprintf(stderr,"The struct input file is %s\n", cmd->input_file);
    fprintf(stderr,"The struct output file is %s\n", cmd->output_file);
    fprintf(stderr,"The struct is background is %d\n", cmd->is_background);
    fprintf(stderr,"The struct num tokens is %d\n", cmd->numTokens);
    fprintf(stderr,"The struct program is %s\n", cmd->program);
}
/*****************************************************************************
* 
* this block of functions handles redirections and running commands in the background
* 
******************************************************************************/
// this function redirects stdin to the given file 
// this function was provided in Explorations module Process I/O https://canvas.oregonstate.edu/courses/1825887/pages/exploration-processes-and-i-slash-o?module_item_id=20268641

// DEV NOTES********** if we pass in the file name then we may be able to repourpose this function when running backround processes (see runCommand)
int redirectInput(struct aCommand* cmd) {
    
    	// Open source file
	int sourceFD = open(cmd->input_file, O_RDONLY);
	if (sourceFD == -1) { 
		perror("input redirect : source open()"); 
		exit(1); 
	}
	// Written to terminal
	fprintf(stderr,"sourceFD == %d\n", sourceFD); 

	// Redirect stdin to source file
	int result = dup2(sourceFD, 0);
	if (result == -1) { 
		perror("input redirect : source dup2()"); 
		exit(2); 
	}
    fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
    return 0;
}

// this function redirects stdin to the given file 
// this function was provided in Explorations module Process I/O https://canvas.oregonstate.edu/courses/1825887/pages/exploration-processes-and-i-slash-o?module_item_id=20268641
int redirectOutput(struct aCommand* cmd) {
    fprintf(stderr, "The struct output file is %s\n", cmd->output_file);
    
   /* if (argc == 1) {
        printf("Usage: ./main <filename to redirect stdout to>\n");
        exit(1);
    }*/

    int targetFD = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640); //*******************check permision num
    if (targetFD == -1) {
        perror("redirect output : open()");
        exit(1);
    }
    // Currently printf writes to the terminal
    fprintf(stderr,"The file descriptor for targetFD is %d\n", targetFD);

    // Use dup2 to point FD 1, i.e., standard output to targetFD
    int result = dup2(targetFD, 1);
    fprintf(stderr,"The result is %d\n", result);
    if (result == -1) {
        perror("redirect output : dup2");
        exit(2);
    }
    // Now whatever we write to standard out will be written to targetFD
    fprintf(stderr,"xxxxxxxxxxxxxxxAll of this is being written to the file using printf\n");
    //after process exits, close file
    fcntl(targetFD, F_SETFD, FD_CLOEXEC);
    return 0;  
}

/*
//redirect to null ********************************
*/

int redirectInputBG(struct aCommand* cmd) {
    
    	// Open source file
	int sourceFD = open("/dev/null", O_RDONLY);
	if (sourceFD == -1) { 
		perror("BG input redirect : source open()"); 
		exit(1); 
	}
	// Written to terminal
	fprintf(stderr,"sourceFD == %d\n", sourceFD); 

	// Redirect stdin to source file
	int result = dup2(sourceFD, 0);
	if (result == -1) { 
		perror("BG input redirect : source dup2()"); 
		exit(2); 
	}
    return 0;
}

// this function redirects stdin to the given file 
// this function was provided in Explorations module Process I/O https://canvas.oregonstate.edu/courses/1825887/pages/exploration-processes-and-i-slash-o?module_item_id=20268641
int redirectOutputBG(struct aCommand* cmd) {
    fprintf(stderr, "The struct output file is %s\n", cmd->output_file);
    
   /* if (argc == 1) {
        printf("Usage: ./main <filename to redirect stdout to>\n");
        exit(1);
    }*/

    int targetFD = open("/dev/null", O_WRONLY, 0640); //*******************check permision num
    if (targetFD == -1) {
        perror("BG redirect output : open()");
        exit(1);
    }
    // Currently printf writes to the terminal
    fprintf(stderr,"BG The file descriptor for targetFD is %d\n", targetFD);

    // Use dup2 to point FD 1, i.e., standard output to targetFD
    int result = dup2(targetFD, 1);
    fprintf(stderr,"The result is %d\n", result);
    if (result == -1) {
        perror("BG redirect output : dup2");
        exit(2);
    }
    // Now whatever we write to standard out will be written to targetFD
    fprintf(stderr,"xxxxxxxxxxxxxxxAll of this is being written to the file using printf\n");

    return 0;  
}


void runBackground(struct aCommand* cmd, int *exitStatus) {
    cmd->is_background = 1;

    //print pid
    printf("PID is %d:\n", getpid());
    // run process************************?????????????????????????????????
    runCommand(cmd, exitStatus);

    printf("PID %d has exited with Status %d:\n", getpid(), *exitStatus);
    
    return;
}

/*****************************************************************************
* 
* this block of functions handles built in functions: cd, status, and exit
* 
******************************************************************************/
/*
* This function takes a struct and changes the PWD to home or the passed relative or absolute file path, then checks for any errors
*/
void changeDir(struct aCommand* cmd){
    //case of no file path arg-- go to HOME
    if((strcmp(cmd->program, cd) == 0) && cmd->args[1] == NULL){
        //cd to users home env variable
        if(chdir(getenv("HOME")) !=0){
            // print any error message
            perror("Error, unable to change to home directory.");
        }       
         //test      
        char buf[MAX_ARGS];
        printf("%s is CWD\n", getcwd(buf, MAX_ARGS));
    }
    //case of args-- pass args to chdir()
    else if((strcmp(cmd->program, cd) == 0) && cmd->args[1] != NULL){
        //pass file path to chdir(),
         if(chdir(cmd->args[1]) !=0){
             // print any error message
            perror("Please enter a valid file path.");
        }
        //test
        char buf[MAX_ARGS];
        printf("%s is CWD\n", getcwd(buf, MAX_ARGS));
    }
    else{
        printf("CD not working, no error message from perror.\n");
    }
}

/*
* This function
*/
int getStatus(struct aCommand* lastCommand, int exitStatus){
    //if no command has run, *****************************************************************handle this in commandRun function
    if(strcmp(lastCommand->program, "status") == 0){
        printStatus(exitStatus);
    }
    //if last command was built in, ignore
    if(strcmp(lastCommand->program, "exit") != 0 || strcmp(lastCommand->program, "cd") != 0 || strcmp(lastCommand->program, "status") != 0){
        printStatus(exitStatus);
    }
     printf("lastCommand is: \n");
    printCommand(lastCommand);
    return 0;
}

// small function that inspects the EXIT or TERMINATION SIGNAL status and prints accordingly
void printStatus(int exitStatus)
{
	// process exited via return from main / exit()
    if(WIFEXITED(exitStatus)) 
    {
        printf("exit value: %d\n", WEXITSTATUS(exitStatus));
    }
	// process was terminated / segfaulted / etc.
    else 
    {
        printf("terminated by signal: %d\n", WTERMSIG(exitStatus));
    }
    
}

/*
* This function terminates all processes and exits the shell
*/
int exitShell(struct aCommand* cmd){
    exit(0);
    return 0;
}

// basic run command that only runs commands in the foreground
// doesn't handle I/O redirection or ctrl-c 
void runCommand(struct aCommand *cmd, int *exitStatus)
{
    pid_t spawnPid = -100;

    spawnPid = fork(); 
    switch(spawnPid)
    {
        case -1: // fork was not successful
            perror("FATAL ERROR: fork() failed!");
            exit(1);
            break;
        
        case 0: // fork was successful and we are in the child process
			// execvp runs our command
 
                // DEV NOTES we may be able to remove some of the nesting here if we call input/output redirect and pass the reDirect variable to it
                //if foreground, redirect
                if(!cmd->is_background){
                    if(cmd->output_file){
                        redirectOutput(cmd);
                    }
                    if(cmd->input_file){
                        redirectInput(cmd);
                    }
                }
                //if background, redirect I/O to null if user hasnt's specified redirection
                else{
                    if(!cmd->output_file){
                        redirectOutputBG(cmd);
                    }
                    if(!cmd->input_file){
                        redirectInputBG(cmd);
                    }
                }


                
            if (execvp(cmd->args[0], cmd->args))
            {  
                // if we returned from execvp, it means an error occurred 
				// probably the command wasn't found
                printf("command not found: %s\n", cmd->args[0]);                
                exit(1);
            }
            break;
            
        default: // fork was successful and we are in the "parent" (smallsh) process
			// in our simple command runner, we just wait for the command to finish
            // if foreground, wait
            if(!cmd->is_background)
            {
				// waitpid will update our exitStatus variable that we passed in
                waitpid(spawnPid, exitStatus, 0);
            }
           // if background, wait and print backgound pid
            else{
                waitpid(spawnPid, exitStatus, WNOHANG);
                printf("BG PID : %d\n", spawnPid);
            }
    }
}

// runs one of our built in commands
void runBuiltInCommand(struct aCommand *cmd, int exitStatus)
{
	if (!strcmp(cmd->program, "status"))
	{
		printStatus(exitStatus);
	}
	else if (!strcmp(cmd->program, "cd"))
	{
        changeDir(cmd);
	}
	else if (!strcmp(cmd->program, "exit"))
	{
        exitShell(cmd);
	}
	else
	{
		fprintf(stderr, "How the F did we get here?!?... lol\n");
	}
}

int isBuiltInCommand(char* c)
{
	if (!strcmp(c, "cd") || !strcmp(c, "status") || !strcmp(c, "exit"))
	{
		return 1;
	}
	return 0;
}

void checkBG(){
    int pidStatus;
    int pid;
    while((pid = waitpid(-1, &pidStatus, WNOHANG)) > 0){
        printf("child with PID %d terminated\n", pid);
        printStatus(pidStatus);
    }
}

/*************************************************
 * this section has signal handlers
 * *********************************************/

// Handler for SIGNINT / Crtl + Z
void handleBGToggle(){
    if(allowBG == 1)
    {
        printf("Entering foreground only mode. & will now be ignored: \n");
        allowBG = 0;
    }
    else {
        printf("Exiting foreground only mode. & will now be enabled: \n");
        allowBG = 1;
    }
}

// Handler for SIGNINT / Crtl + C
void handleINTToggle(){
    if(allowINT == 1)
    {
        printf("Entering foreground only mode. Ctrl+C will now be ignored: \n");
        allowINT = 0;
    }
    else {
        printf("Exiting foreground only mode. Ctrl+C will now be enabled: \n");
        allowINT = 1;
    }
}


int main(void) {
    int exitFlag = 0;
    int exitStatus = 0;
    int* xStatPtr = &exitStatus;

    //initialize SIGNAL struct
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, ignore_action = {0};

    /*****************************
    //if command loop malfunctions, try clearerr(stdin);
    ******************************/
    //signal handle here for ctrl Z
    //toggles forground only mode
    	
    // Fill out the SIGINT_action struct
	// Register handle_SIGINT as the signal handler
	SIGTSTP_action.sa_handler = handleBGToggle;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGTSTP_action.sa_mask);
	// No flags set
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    /*****************************/
    //signal handle here for ctrl C
    //make shell ignore ctrl C
    
	// Fill out the SIGINT_action struct
	// Register handle_SIGINT as the signal handler
	SIGINT_action.sa_handler = handleINTToggle;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGINT_action.sa_mask);
	// No flags set
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

    // The ignore_action struct as SIG_IGN as its signal handler
	ignore_action.sa_handler = SIG_IGN;

    // Register the ignore_action as the handler for SIGTERM, SIGHUP, SIGQUIT.
	// So all three of these signals will be ignored.
    if(!allowBG){
	    sigaction(SIGTSTP, &ignore_action, NULL);
    
    }
    if(!allowINT){
    //if(!cmd->is_background){/********************************this might be better, but the struct isn't initialized yet
	    sigaction(SIGINT, &ignore_action, NULL);
    }
    // set to 1 in exit function
    while (exitFlag == 0) {
        /************************************ GETTING VALID INPUT ************************************/
        //prompt user, store input string
        checkBG();
        cmdPrompt();
        char rawInput[512];
        memset(rawInput, '\0', sizeof(rawInput));//reset to null
        char* cmdLine = cmdInput(rawInput);
        // loop prompt if line is comment or empty
        while (isComment(cmdLine)) {
            checkBG();
            cmdPrompt();
            cmdLine = cmdInput(rawInput);
        }

        /******************************* PARSING AND EXPANDING INPUT ************************************/
        // empty array for tokens
        char* tokenList[512];
        memset(tokenList, '\0', sizeof(tokenList));//reset to null

        struct aCommand* cmd = malloc(sizeof(struct aCommand));
        // parse input into tokens
        char** expandedTokens = inputParse(cmdLine, tokenList, cmd);

        //initialize struct member 'program'
        cmd->program = strdup(expandedTokens[0]);

        // input and output files are initialized
        char** args = findArgs(expandedTokens, cmd);
        
        /*/testing location only, will be called in runCommand()????????????????????????????????
        if(strcmp(cmd->program, cd) == 0){
            changeDir(cmd);
        }*/
        //store last cmd struct for status?????????????????????????????????????????
        struct aCommand* lastCommand = malloc(sizeof(struct aCommand));
        lastCommand = cmd;



        //testing runCommand
        if (!isBuiltInCommand(cmd->program))
		{
			runCommand(cmd, &exitStatus);
		}
		else // the user typed in a built in command, like "status" or "cd"
		{
			// built in commands do not update the exit status, so note it's not &exitStatus
			runBuiltInCommand(cmd, exitStatus);
		}
        /*
        //if last command was built in, ignore
        if(strcmp(cmd->program, "exit") != 0 || strcmp(cmd->program, "cd") != 0 || strcmp(cmd->program, "status") != 0){
            printf("not a built in command \n");
            runCommand(cmd, &exitStatus);
        }
        
        if(strcmp(cmd->program, "status") == 0){
            //getStatus(lastCommand, exitStatus);
            printf("this IS a built in command: status \n");
            printStatus(exitStatus);
        }
        */
        //strcpy(cmd->args, args);
        // cmd->args = args;
        printCommand(cmd);

    }

    return 0;
}