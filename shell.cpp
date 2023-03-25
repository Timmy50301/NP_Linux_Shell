#include <iostream>
#include <vector>
#include <string>
#include <sstream>  //istringstream
#include <unistd.h> //close(pipefd)
#include <stdio.h>  //fopen
#include <unistd.h> //execv
#include <cstring>  //strcpy
#include <sys/wait.h>   //wait

using namespace std;

struct Pipe
{
    int count;      //when pipe's count == 0 read by next cmd 
    int * pipefd;
    string sign;    //PIPE, NUM_PIPE, ERR_PIPE
};
//sign = {"NONE", "PIPE", "NUM_PIPE", "ERR_PIPE", "REDIR"}

struct Fd
{
    int in;
    int out;
    int error;
};


//Set argv to exec
char** SetArgv(string cmd, vector<string> arg_vec)
{   
    char ** argv = new char* [arg_vec.size()+2];
    argv[0] = new char [cmd.size()+1];
    strcpy(argv[0], cmd.c_str());
    for(int i=0; i<arg_vec.size(); i++)
    {
        argv[i+1] = new char [arg_vec[i].size()+1];
        strcpy(argv[i+1], arg_vec[i].c_str());
    }
    argv[arg_vec.size()+1] = NULL;
    return argv;
}

vector<string> SplitEnvPath(string path)
{
    vector<string> path_vec;
    string temp;
    stringstream ss(path);
    
    while(getline(ss, temp, ':')) path_vec.push_back(temp);

    return path_vec;
}

//
bool LegalCmd(string cmd, vector<string> arg_vec, vector<string> path_vec)
{
    for(int i=0 ; i<path_vec.size() ; i++)
    {
        string path_temp = path_vec[i] + "/" + cmd;
        FILE* file = fopen(path_temp.c_str(), "r");
        if(file != NULL)
        {
            fclose(file);
            return true;
        }
    }
    return false;
}



class NP_SHELL
{
private:
    string input;
    vector<string> input_vec;
    vector<string>::iterator iterLine;

    string path;
    vector<string> path_vec;
    vector<struct Pipe> pipe_vec;
    vector<string> arg_vec;

    Fd fd;
    string cmd;
    string sign;
    int count;
    int waittimes;      //how many child process needs to wait

public:
    NP_SHELL();
    void Execute();

    void SETUP();
    void DoBuildinCmd();
    void ConnectFD();
    void DoCmd();

    void CreatePipe();
    void ReduceCount_NUM_ERR();
    void ReduceCount_Ord();
    void ClosePipe();

    bool UseSamePipe();
    bool WaitChild();   //decide weather need to wait or not
    


};
NP_SHELL::NP_SHELL()
{
    setenv("PATH", "bin:.", 1);
    path = getenv("PATH");
    path_vec = SplitEnvPath(path);
    // for(int i=0 ; i<path_vec.size() ; i++){
    //     cout<<path_vec[i]<<endl;
    // }
    waittimes=0;
}

void NP_SHELL::Execute()                                //path, path_vec, pipe_vec, waittimes stay globally
{   
    cout<<"% ";
    while(getline(cin, input))                          //input = command line typed by user
    {
        input_vec.clear();
        istringstream ss(input);
        string temp;
        while(ss>>temp) input_vec.push_back(temp);      //input_vec = command line input seperated by " "

        iterLine = input_vec.begin();   
        
        while(iterLine != input_vec.end())
        {
            fd = {0, 1, 2};
            arg_vec.clear();
            sign = "NONE";
            count = 0;              //表示第一個碰到pipe sign( | |n !n ) 要pipe到幾個command之後
            
            SETUP();                //setup [cmd], [pipe_vec], [arg_vec], [sign], [count] untill first sign( < arg , | , |n , !n )
                                    //iterator pointing to next

            if(cmd == "setenv" || cmd == "printenv" || cmd == "exit" || cmd == "EOF")
            {
                DoBuildinCmd();
                ClosePipe();
                break;
            }
            
            //
            if(sign == "REDIR")
            {
                string fileName;
                fileName = arg_vec[arg_vec.size()-1];
                arg_vec.pop_back();
                FILE* file = fopen(fileName.c_str(), "w");
                fd.out = fileno(file);
            }

            ConnectFD();            //開始對接fd(暫存)與pipe的接口
            waittimes++;            //how many child process need to wait
            pid_t pid = fork();

            if(pid == 0)
                DoCmd();            //child process 將此process的stdin, stdout, stderr與暫存的fd對接，然後execv then it kill itself                               
            else
                if(waittimes%100 == 0) sleep(1);  //considering forking too quick

            if(fd.in != 0)          //??????close read from pipe, the other entrance is closed in ConnectFD
                close(fd.in);
            
            if(WaitChild())        //wait for child process only when after piping(count=0) & redirection
            {
                int status = 0;
                for(int i=1 ; i<=waittimes ; i++)       //need to wait for every child process
                    waitpid(-1, &status, 0);
                
                waittimes=0;
            }
            
            ClosePipe();                    //將count=0 的pipe關掉(pipefd[0], pipefd[1]) and also erase from pipe_vec

            if( (sign=="NUM_PIPE" || sign=="ERR_PIPE") && iterLine!=input_vec.end() )   //numpipe & errorpipe appear in the middle
                ReduceCount_NUM_ERR();      //將pipe_vec中所有 "NUM_PIPE", "ERR_PIPE" count--

            ReduceCount_Ord();              //將pipe_vec中所有 "PIPE" count--
        }


        if(input.size()!=0)                 //if input is not an enter ("\n" in UNIX) 
            ReduceCount_NUM_ERR(); 
        cout<<"% ";
    }
}

void NP_SHELL::SETUP()
{
    string temp;
    bool isCmd = true;

    while(iterLine != input_vec.end())
    {
        temp = *iterLine;

        if(temp[0] == '|' && temp.size() == 1)
        {
            sign = "PIPE";
            count = 1;
            iterLine++;
            CreatePipe();
            break;
        }
        else if(temp[0] == '|' && temp.size() > 1)
        {
            sign = "NUM_PIPE";
            count = stoi(temp.c_str()+1);
            iterLine++;
            if(!UseSamePipe())      //if read head connected to the same line with others, use the same pipe created before
                CreatePipe();
            break;
        }
        else if(temp[0] == '!')
        {
            sign = "ERR_PIPE";
            count = stoi(temp.c_str()+1);
            iterLine++;
            if(!UseSamePipe())      //if read head connected to the same line with others, use the same pipe created before
                CreatePipe();
            break;
        }
        else if(temp[0] == '>')
        {
            sign = "REDIR";
            iterLine++;
            arg_vec.push_back(*iterLine);

            iterLine++;
            break;
        }
        else if(isCmd)
        {
            cmd = temp;
            isCmd = false;
        }
        else
        {
            arg_vec.push_back(temp);
        }
        
        iterLine++;
    }
}

void NP_SHELL::DoBuildinCmd()
{
    if(cmd == "printenv")
    {
        if(getenv(arg_vec[0].c_str())!=NULL)                  //arg_vec[0]= env name
            cout<<getenv(arg_vec[0].c_str())<<endl;
    }
    else if(cmd == "setenv")
    {
        setenv(arg_vec[0].c_str(), arg_vec[1].c_str(), 1);    //arg_vec[0]= env name   arg_vec[1]= env value
        if(arg_vec[0] == "PATH")
        {
            path = getenv("PATH");
            path_vec.clear();
            path_vec = SplitEnvPath(path);
        }
    }
    else                                                     // exit or EOF
        exit(0);
}

//
void NP_SHELL::ConnectFD()
{
    vector<struct Pipe>::iterator iter;
    iter = pipe_vec.begin();

    if(sign == "PIPE")
    {
        while(iter != pipe_vec.end())
        {
            if((*iter).count == 0)          //  |0 cmd or | cmd
            {
                close((*iter).pipefd[1]);   //close write to pipe
                fd.in = (*iter).pipefd[0];
            }
            if((*iter).count == 1 && (*iter).sign == "PIPE") //  cmd |
                fd.out = (*iter).pipefd[1];

            iter++;
        }
    }
    else if(sign == "NUM_PIPE")
    {
        while(iter != pipe_vec.end())
        {
            if((*iter).count == 0)          //  |0  cmd or | cmd 
            {
                close((*iter).pipefd[1]);   //close write to pipe
                fd.in = (*iter).pipefd[0];
            }
            
            if((*iter).count == count && (*iter).sign == "NUM_PIPE") //  |n
                fd.out = (*iter).pipefd[1];
                
            iter++;
        }
    }
    else if(sign == "ERR_PIPE")
    {
        while(iter != pipe_vec.end())
        {
            if((*iter).count == 0)          //  |0  cmd or | cmd
            {
                close((*iter).pipefd[1]);   //close write to pipe
                fd.in = (*iter).pipefd[0];
            }

            if((*iter).count == count && (*iter).sign == "ERR_PIPE") //  !n
            {
                fd.out = (*iter).pipefd[1];
                fd.error = (*iter).pipefd[1];
            }

            iter++;
        }
    }
    else if(sign == "REDIR")
    {
        while(iter != pipe_vec.end())
        {
            if((*iter).count == 0)          //  |0  cmd or | cmd
            {
                close((*iter).pipefd[1]);   //close write to pipe
                fd.in = (*iter).pipefd[0];
            }

            iter++;
        }
    }
    else
    {
        while(iter != pipe_vec.end())
        {
            if((*iter).count == 0)          //  |0  cmd or | cmd
            {
                close((*iter).pipefd[1]);   //close write to pipe
                fd.in = (*iter).pipefd[0];
            }

            iter++;
        }
    }
    
}

//
void NP_SHELL::DoCmd()
{   
    // cout<<"fd"<<fd.in<<" "<<fd.out<<" "<<fd.error<<endl;
    if(fd.in != 0) dup2(fd.in, STDIN_FILENO);
    if(fd.out != 1) dup2(fd.out, STDOUT_FILENO);
    if(fd.error != 2) dup2(fd.error, STDERR_FILENO);
    
    if(fd.in != 0) close(fd.in);
    if(fd.out != 1) close(fd.out);
    if(fd.error != 2) close(fd.error);
        
    
    /*  Test legal command  */
    if(!LegalCmd(cmd, arg_vec, path_vec))
    {
        cerr<<"Unknown command: ["<<cmd<<"]."<<endl;
        exit(1);
    }
    
    char **arg = SetArgv(cmd, arg_vec);

    for(int i=0 ; i<path_vec.size() ; i++)
    {
        string temp_path = path_vec[i] + "/" + cmd;
        execv(temp_path.c_str(), arg);
    }

    // cerr<<"Fail to exec"<<endl;
    exit(1);
}

void NP_SHELL::CreatePipe()
{
    struct Pipe newPipe;

    int* pipefd = new int [2];
    pipe(pipefd);

    newPipe.pipefd = pipefd;
    newPipe.sign = sign;
    newPipe.count = count;
    pipe_vec.push_back(newPipe);
}

void NP_SHELL::ReduceCount_NUM_ERR()
{
    for(int i=0; i<pipe_vec.size();i++)
    {
        if(pipe_vec[i].sign=="NUM_PIPE" || pipe_vec[i].sign=="ERR_PIPE")
            pipe_vec[i].count--;
    }
}

void NP_SHELL::ReduceCount_Ord()
{
    for(int i=0; i<pipe_vec.size();i++)
    {
        if(pipe_vec[i].sign=="PIPE")
            pipe_vec[i].count--;
    }
}

void NP_SHELL::ClosePipe()
{
    for(int i=0; i<pipe_vec.size(); i++)
    {
        if(pipe_vec[i].count == 0)
        {
            close(pipe_vec[i].pipefd[0]);
            close(pipe_vec[i].pipefd[1]);
            delete [] pipe_vec[i].pipefd;
            pipe_vec.erase(pipe_vec.begin()+i);
            // break;      //to be safe, there could be both NUM_PIPE & ERR_PIPE pipe to same line
        }
    }
}

bool NP_SHELL::UseSamePipe()
{
    for(int i=0 ; i<pipe_vec.size() ; i++)
        if(pipe_vec[i].count == count && pipe_vec[i].sign == sign) return true;

    return false;
}

bool NP_SHELL::WaitChild()
{
    if(sign == "REDIR" || sign =="NONE" ) return true;

    return false;
}



int main(int argc, char* const argv[]){
    signal(SIGCHLD, SIG_IGN);
    NP_SHELL shell;
    shell.Execute();

    return 0;
}

