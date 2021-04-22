#define _GNU_SOURCE
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>


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
void runBackground(struct aCommand* cmd);
void printCommand(struct aCommand* cmd);
void changeDir(struct aCommand* cmd);
int getStatus(struct aCommand* cmd, int exitStatus);
int exitShell(struct aCommand* cmd);

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
              printf("arg %d: %s | ",a, cmd->args[a]);
        }          
    }
    printf("\n");
    //printf("The struct args is %s\n", cmd->args[0]);
    printf("The struct input file is %s\n", cmd->input_file);
    printf("The struct output file is %s\n", cmd->output_file);
    printf("The struct is background is %d\n", cmd->is_background);
    printf("The struct num tokens is %d\n", cmd->numTokens);
    printf("The struct program is %s\n", cmd->program);
}
/*****************************************************************************
* 
* this block of functions handles redirections and running commands in the background
* 
******************************************************************************/
int redirectInput(struct aCommand* cmd) {
    return 0;
}

// this function redirects stdin to the given file 
// this function was provided in Explorations module Process I/O https://canvas.oregonstate.edu/courses/1825887/pages/exploration-processes-and-i-slash-o?module_item_id=20268641
int redirectOutput(struct aCommand* cmd) {
    char* fileName = cmd->output_file;
    printf("The struct args is %s\n", cmd->args[0]);
    printf("The struct input file is %s\n", cmd->input_file);
    printf("The struct is background is %d\n", cmd->is_background);
    printf("The struct num tokens is %d\n", cmd->numTokens);
    printf("The struct output file is %s\n", cmd->output_file);
    printf("The struct program is %s\n", cmd->program);
    
   /* if (argc == 1) {
        printf("Usage: ./main <filename to redirect stdout to>\n");
        exit(1);
    }*/

    int targetFD = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (targetFD == -1) {
        perror("open()");
        exit(1);
    }
    // Currently printf writes to the terminal
    printf("The file descriptor for targetFD is %d\n", targetFD);

    // Use dup2 to point FD 1, i.e., standard output to targetFD
    int result = dup2(targetFD, 1);
    printf("The result is %d\n", result);
    if (result == -1) {
        perror("dup2");
        exit(2);
    }
    // Now whatever we write to standard out will be written to targetFD
    printf("All of this is being written to the file using printf\n");
    return 0;
    
   
}
void runBackground(struct aCommand* cmd) {
    cmd->is_background = 1;
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

int getStatus(struct aCommand* lastCommand, int exitStatus){
    //if no command has run, 
    if(strcmp(lastCommand->program, "status") == 0){
        return 0;
    }
    //if last command was built in, ignore

    //get exit status for process

    //get SIG for process
    return 1;
}

/*
* This function terminates all processes and exits the shell, returns exit status*************************prob switch back to void ret type
*/
int exitShell(struct aCommand* cmd){
    //not sure if this is the right place for atexit()
    /*int xStat = atexit(getStatus);//some zombie checking im guessing????????????????????
    exit(xStat);
    return xStat;*/
    return 0;
}



int main(void) {
    int exitFlag = 0;
    int exitStatus = 0;
    // set to 1 in exit function
    while (exitFlag == 0) {
        //prompt user, store input string
        cmdPrompt();
        char rawInput[512];
        memset(rawInput, '\0', sizeof(rawInput));//reset to null
        char* cmdLine = cmdInput(rawInput);
        // if line is comment or empty, prompt user
        while (isComment(cmdLine)) {
            cmdPrompt();
            cmdLine = cmdInput(rawInput);
        }
        // parse input into tokens
        // empty array for tokens
        char* tokenList[512];
        memset(tokenList, '\0', sizeof(tokenList));//reset to null

        struct aCommand* cmd = malloc(sizeof(struct aCommand));
        
        char** expandedTokens = inputParse(cmdLine, tokenList, cmd);

        //initialize struct member
        //struct aCommand* cmd = malloc(sizeof(struct aCommand));
        cmd->program = strdup(expandedTokens[0]);

        char** args = findArgs(expandedTokens, cmd);
        
        //testing location only, will be called in runCommand()*********************************************************
        if(strcmp(cmd->program, cd) == 0){
            changeDir(cmd);
        }
        //store last cmd struct for status?????????????????????????????????????????
        struct aCommand* lastCommand = malloc(sizeof(struct aCommand));
        lastCommand = cmd;
        if(strcmp(cmd->program, "status") == 0){
            getStatus(lastCommand, exitStatus);
        }
        //strcpy(cmd->args, args);
        // cmd->args = args;
        printCommand(cmd);

    }

    return 0;
}