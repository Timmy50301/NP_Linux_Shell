# Linux-like Shell(Bash)

The shell support the following features:
1. Execution of commands
2. Pipe
3. File redirection

##  Usage

To make the sample program:
```bash
make
```

To run the sample program:
```bash
bash$ ./npshell
```

”% ” is the command line prompt.
```bash
% printenv PATH # initial PATH is bin/ and ./
```
```bash
% setenv PATH bin # set PATH to bin/ only
```
```bash
% cat test1.txt # basic command
```
```bash
% removetag test.html | number # pipe
```
```bash
% cat test.html > test1.txt # file redirection
```
```bash
% exit # the shell terminates after receiving the exit command or EOF
```

