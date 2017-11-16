
const char * usage =
"                                                               \n"
"daytime-server:                                                \n"
"                                                               \n"
"Simple server program that shows how to use socket calls       \n"
"in the server side.                                            \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   myhttpd <port>                                              \n"
"                                                               \n"
"Where 1024 < port < 65536.             \n"
"                                                               \n"
"In another window type:                                       \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where daytime-server  \n"
"is running. <port> is the port number you used when you run   \n"
"daytime-server.                                               \n"
"                                                               \n"
"Then type your name and return. You will get a greeting and   \n"
"the time of the day.                                          \n"
"                                                               \n";


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/timeb.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <link.h>


pthread_mutex_t mutex;
int QueueLength = 5;
const char * secretKey = "helpme";
const char * secretKey1 = "helpme/";
const char * serverType = "CS 252 lab5";
/*Content-types, change depending on the suffix of the hfile
* .html/.htm = text/html
* .txt = text/plain
* .jpg = image/jpeg
* .png = image/png
* .gif = image/gif
* .svn = image/svg+xml
*/

//stat and log global information
//clock_t startTime = clock();
//time_t startTime = time(0);
struct timeb start, end;
struct timeb pstart, pend;

int numRequests = 0;

double minTime = 1000.0;
double maxTime = 0.0;
char minAddress[100];
char maxAddress[100];
char * lastAddress = "";

char * lastIP;

//function for eliminating zombies
extern "C" void zombieKill(int sig) {
  while (waitpid(-1, NULL, WNOHANG) >= 1);
}

//function for loadable modules
typedef void (*httprunfunc)(int ssock, const char* querystring);

//file object for printing directories
typedef struct fileobj{
  char name[100];
  long size;
  time_t* modTime;
  char path[100];
} fileobj;

//sort functions for sorting directories
int sortNA(const void * a, const void * b);
int sortND(const void * a, const void * b);
int sortMA(const void * a, const void * b);
int sortMD(const void * a, const void * b);
int sortSA(const void * a, const void * b);
int sortSD(const void * a, const void * b);

// Processes time request
void processTimeRequest( int socket );
void processRequestThread( int socket );
void poolSlave( int socket );
void writeFileHtml (char * name, char * modified, long size, int fd, char * path);

int main( int argc, char ** argv ) {
  ftime(&start);

  struct sigaction sa;
  sa.sa_handler = zombieKill;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL)) {
    perror("sigaction");
    exit(2);
  }

  /* Print usage if not enough arguments
  if ( argc < 2 ) {
    fprintf( stderr, "%s", usage );
    exit( -1 );
  }*/
  
  //check number of args and assign concurrency and port
  int port;
  char * concurr;
  int poolSize = 5;

  if (argc == 1) {
    port = 7514;
    concurr = "d";
  }
  else if (argc == 2) {
    port = atoi( argv[1] );
    concurr = "d";
  }
  else if (argc == 3) {
    concurr = argv[1];
    port = atoi( argv[2] );
  }
  else if (argc == 4) {
    concurr = argv[1];
    port = atoi( argv[2] );
    poolSize = atoi( argv[3] );
  }
  else {
    port = 7514;
    concurr = "d";
  }

  printf("\nstuff: %d and %s\n", port, concurr);

  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);
  

    // Allocate a socket
    int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
    if ( masterSocket < 0) {
      perror("socket");
      exit( -1 );
    }

    // Set socket options to reuse port. Otherwise we will
    // have to wait about 2 minutes before reusing the sae port number
    int optval = 1; 
    int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );
   
    // Bind the socket to the IP address and port
    int error = bind( masterSocket,
		      (struct sockaddr *)&serverIPAddress,
		      sizeof(serverIPAddress) );
    if ( error ) {
      perror("bind");
      exit( -1 );
    }
  
    // Put socket in listening mode and set the 
    // size of the queue of unprocessed connections
    error = listen( masterSocket, QueueLength);
    if ( error ) {
      perror("listen");
      exit( -1 );
    }
  
//DEFAULT - NO CONCURRENCY
  if (strcmp(concurr, "d") == 0) {
    printf("no concurrency...\n");
    
    while ( 1 ) {
      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );
      int slaveSocket = accept( masterSocket,
			        (struct sockaddr *)&clientIPAddress,
			        (socklen_t*)&alen);

      //get ip address
      lastIP = inet_ntoa(clientIPAddress.sin_addr);

      if ( slaveSocket < 0 ) {
        perror( "accept" );
        exit( -1 );
      }

      ftime(&pstart);
      // Process request.
      processTimeRequest( slaveSocket );
      
      //calculate time for min/max
      ftime(&pend);
      double durTime = (double)(1000 * (pend.time - pstart.time) + (pend.millitm - pstart.millitm ));
      durTime = (durTime / 1000.0);
      if (durTime < minTime) {
        minTime = durTime;
        strcpy(minAddress, lastAddress);
      }
      if (durTime > maxTime) {
        maxTime = durTime;
        strcpy(maxAddress, lastAddress);
      }

      // Close socket
      close( slaveSocket );
    }
  }
//PROCESS BASED
  else if (strcmp(concurr, "-f") == 0) {
    printf("process based...\n");
    
    while ( 1 ) {
      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );
      int slaveSocket = accept( masterSocket,
			        (struct sockaddr *)&clientIPAddress,
			        (socklen_t*)&alen);
    
      //get ip address
      lastIP = inet_ntoa(clientIPAddress.sin_addr);
      
      if ( slaveSocket == -1 && errno == EINTR) {
        continue;
        perror( "accept" );
        exit( -1 );
      }

      ftime(&pstart);
      pid_t slave = fork();
      if (slave == 0) {
        // Process request.
        processTimeRequest( slaveSocket ); 
        
        // Close socket
        close( slaveSocket );
        exit(0);
      }

      //calculate time for min/max
      ftime(&pend);
      double durTime = (double)(1000 * (pend.time - pstart.time) + (pend.millitm - pstart.millitm ));
      durTime = (durTime / 1000.0);
      if (durTime < minTime) {
        minTime = durTime;
        strcpy(minAddress, lastAddress);
      }
      if (durTime > maxTime) {
        maxTime = durTime;
        strcpy(maxAddress, lastAddress);
      }

      // Close socket
      close( slaveSocket );
    }
  }
//THREAD BASED
  else if (strcmp(concurr, "-t") == 0) {
    printf("thread based...\n");

    while ( 1 ) {
      // Accept incoming connections
      struct sockaddr_in clientIPAddress;
      int alen = sizeof( clientIPAddress );
      int slaveSocket = accept( masterSocket,
			        (struct sockaddr *)&clientIPAddress,
			        (socklen_t*)&alen);

      //get ip address
      lastIP = inet_ntoa(clientIPAddress.sin_addr);
      
      if ( slaveSocket < 0 ) {
        perror( "accept" );
        exit( -1 );
      }

      pthread_t thrd;
      pthread_attr_t attr;
      pthread_attr_init( &attr );
      pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

      //pthread_mutex_init(&mutex, NULL);
      
      pthread_create(&thrd, &attr, 
                     (void*(*)(void*))processRequestThread,
                     (void*)slaveSocket);
    
      //pthread_join(thrd, NULL);
    
    }
  }
//POOL THREADS
  else if (strcmp(concurr, "-p") == 0) {
    printf("pool based...\n");

    pthread_t tid[poolSize];
    pthread_attr_t attr;
    pthread_attr_init( &attr );
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    pthread_mutex_init(&mutex, NULL);

    int j;
    for (j = 0; j < poolSize; j++) {
      pthread_create(&tid[j], &attr, 
                     (void*(*)(void*))poolSlave,
                     (void*)masterSocket);
    }
    for (j = 0; j < poolSize; j++) { 
      pthread_join(tid[j], NULL);
    }

  }

}

void processRequestThread(int socket) {
  //pthread_mutex_lock(&mutex);

  ftime(&pstart);

  processTimeRequest(socket);
  close(socket);

      //calculate time for min/max
      ftime(&pend);
      double durTime = (double)(1000 * (pend.time - pstart.time) + (pend.millitm - pstart.millitm ));
      durTime = (durTime / 1000.0);
      if (durTime < minTime) {
        minTime = durTime;
        strcpy(minAddress, lastAddress);
      }
      if (durTime > maxTime) {
        maxTime = durTime;
        strcpy(maxAddress, lastAddress);
      }
  //pthread_mutex_unlock(&mutex);
}

void poolSlave( int socket ) {
  while ( 1 ) {
    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
   
    pthread_mutex_lock(&mutex);

    int slaveSocket = accept( socket,
			      (struct sockaddr *)&clientIPAddress,
			      (socklen_t*)&alen);

    pthread_mutex_unlock(&mutex);
      
    //get ip address
    lastIP = inet_ntoa(clientIPAddress.sin_addr);
    
    printf("trying to join...\n");
    
    if ( slaveSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }

    ftime(&pstart);
    // Process request.
    processTimeRequest( slaveSocket );

      //calculate time for min/max
      ftime(&pend);
      double durTime = (double)(1000 * (pend.time - pstart.time) + (pend.millitm - pstart.millitm ));
      durTime = (durTime / 1000.0);
      if (durTime < minTime) {
        minTime = durTime;
        strcpy(minAddress, lastAddress);
      }
      if (durTime > maxTime) {
        maxTime = durTime;
        strcpy(maxAddress, lastAddress);
      }

    // Close socket
    close( slaveSocket );
  }
}

/*TODO
 * be able to sort the entries in directories
    * grab and assign query string variables
 * loadable modules
 * /logs page that has access info
 * README with features you have and have not implemented
 *TODO
 */

void processTimeRequest( int fd ) {
  numRequests++;

  // Buffer used to store the name received from the client
  int maxBuf = 1024;
  char readBuffer[ maxBuf + 1 ];
  int bufLength = 0;
  int n;
  char char0;
  char char1;
  char char2;
  char char3;
  int foundSpaces = 0;

  //variables for capturing the query string
  int queryFound = 0;
  int maxQuery = 512;
  char queryBuffer[ maxQuery + 1 ];
  int queryLength = 0;

  while ( bufLength < maxBuf && (n = read( fd, &char0, sizeof(char0))) > 0 ) {

    //at the end if the last four characters make <crlf>crlf>
    if ( char0 == '\n' && char1 == '\r' ) {
      if (char2 == '\n' && char3 == '\r') {
        // add null terminators to the buffers 
        readBuffer[bufLength] = '\0';
        queryBuffer[queryLength] = '\0';
        
        break;
      }
    }

    //only need to capture between first and second space
    if (char0 == ' ') {
      if (foundSpaces == 0) {
        foundSpaces++;
      }
      else if (foundSpaces == 1) {
        foundSpaces++;
      }
    }

    if (foundSpaces == 1) {
      if (char0 == '?') {
        queryFound++;
      }
      if (queryFound == 0) {
        //add charcter to buffer, increment length, and shift chars[] array
        readBuffer[bufLength] = char0;
        bufLength++;
      }
      else {
        if (char0 != '?') {
          queryBuffer[queryLength] = char0;
          queryLength++;
        }
      }
    }

    
   /* write( fd, &char0, sizeof(char0) );
    write( fd, &char1, sizeof(char0) );
    write( fd, &char2, sizeof(char0) );
    write( fd, &char3, sizeof(char0) );
   */ 
    char3 = char2;
    char2 = char1;
    char1 = char0;
  
  }
  lastAddress = readBuffer;

  //write to logs page
  FILE * logFile;
  logFile = fopen("logs.txt", "a");
  char * words = " requested ";
  fputs(lastIP, logFile);
  fputs(words, logFile);
  fputs(lastAddress, logFile); 
  words = "\n"; 
  fputs(words, logFile); 
  fclose(logFile);


  //capture secret key
  char attemptKey[strlen(secretKey) + 1];
  int i;
  int spot = 0;
  for (i = 0; i < strlen(secretKey1); i++) {
    attemptKey[i] = readBuffer[i + 2];
    spot++;
  }
  attemptKey[spot] = '\0';

  //if attempted key is no equal to secret key send 404 else proceed 
  if (strcmp(attemptKey, secretKey) == 0 || strcmp(attemptKey, secretKey1) == 0) {

    //get file location 
    char fileLocation[bufLength - spot + 1 + 1];
    int spot2 = 0;
    for (i = 0; i < bufLength - spot; i++) {
      fileLocation[i + 1] = readBuffer[i + spot + 1];
      spot2++;
    }
    fileLocation[spot2] = '\0';
    fileLocation[0] = '.';

    printf("fileLocation --%s--\n", fileLocation);
    printf("queryBuffer --%s--\n", queryBuffer);
    

    //some weird thing with .elpme/
    if (strcmp(fileLocation, ".elpme/") == 0)  {
      printf("how does this even happen?");
      //send 404
      char* header1 = "HTTP/1.1 404 Document follows\n\r";
      char* header2 = "Server: CS 252 lab5\n\r";
      char* header3 = "Content-type: text/plain\n\r\n\r";
    
      char * error = "File not Found";
    
      write( fd, header1, strlen(header1) );
      write( fd, header2, strlen(header2) );
      write( fd, header3, strlen(header3) );
    
      write( fd, error, strlen(error) );
    }
    //if nothing in file location go to index
    else if (strcmp(fileLocation, ".") == 0 || strcmp(fileLocation, "./") == 0) {

      //make headers
      char* header1 = "HTTP/1.1 200 Document follows\r\n";
      char* header2 = "Server: CS 252 lab5\r\n";
      char* header3 = "Content-type: text/html\r\n\r\n";
    
      FILE * f;
      f = fopen("./http-root-dir/htdocs/index.html", "r");
      fseek(f, 0, SEEK_END);
      long fsize = ftell(f);
      fseek(f, 0, SEEK_SET);

      char body[fsize + 1];
      fread(body, fsize, 1, f);
      fclose(f);
      body[fsize] = '\0';
 
      write( fd, header1, strlen(header1) );
      write( fd, header2, strlen(header2) );
      write( fd, header3, strlen(header3) );

      write( fd, body, strlen(body) );
      const char * newline="\n";
      write(fd, newline, strlen(newline));

    }
    else {



      //find up to the first / and make adjustments if its not the full length already
      char adjustedLocation[PATH_MAX + 1];
      
      char firstPart[20];
      int firstLength = 0;
      char0 = fileLocation[2];
      i = 0;

      while (char0 != '/') {
        firstPart[firstLength] = char0;
        firstLength++;
        i++;
        char0 = fileLocation[2 + i];
      }
      firstPart[firstLength] = '\0';

      if (strcmp(firstPart, "icons") == 0) {
        strcpy(adjustedLocation, "./http-root-dir/");
        strcat(adjustedLocation, fileLocation);
      }
      else if (strcmp(firstPart, "htdocs") == 0) {
        strcpy(adjustedLocation, "./http-root-dir/");
        strcat(adjustedLocation, fileLocation);
      }
      else if (strcmp(firstPart, "http-root-dir") == 0) {
        strcpy(adjustedLocation, fileLocation);
      }
      else if (strcmp(firstPart, "~") == 0) {
        strcpy(adjustedLocation, fileLocation);
      }
      //if we are going cgi-bin script stuff
      else if (strcmp(firstPart, "cgi-bin") == 0) {
     
        //fork child process
        pid_t slave = fork();
        if (slave == 0) {
          //set enviornment variables
          setenv("QUERY_STRING", queryBuffer, 1);
          setenv("REQUEST_METHOD", "GET", 1);
          
          //change default out
          int defaultout = dup(1);
          dup2(fd, 1);

          //write headers
          char* header1 = "HTTP/1.1 404 Document follows\n\r";
          char* header2 = "Server: CS 252 lab5\n\r";
          write( 1, header1, strlen(header1) );
          write( 1, header2, strlen(header2) );
          
          //get proper file location to call execv
          strcpy(adjustedLocation, "./http-root-dir/");
          strcat(adjustedLocation, fileLocation);
          char * cgiScript[1];
          cgiScript[0] = adjustedLocation;

          //check if the file even exists
          FILE * fi = fopen(adjustedLocation, "rb");
          
          if (fi != NULL) {
            fclose(fi);
            
            //check for so
            char * takeoff = "./http-root-dir/";
            int offset = strlen(fileLocation) + strlen(takeoff);
            char s = adjustedLocation[offset - 2];
            char o = adjustedLocation[offset -1];
           
            
            //TODO loadable module
            if (s == 's' && o == 'o') {

              //open the module
              void * module = dlopen(adjustedLocation, RTLD_LAZY);

              if (module == NULL) {
                fprintf(stderr, "file not found\n");
                perror("dlopen");
                exit(1);
              }
              
              //get function to act on module
              httprunfunc module_httprun;

              module_httprun = (httprunfunc)dlsym(module, "httprun");
              if ( module_httprun == NULL ) {
                perror("dlsym: httprun not found");
                exit(1);
              }

              module_httprun(fd, queryBuffer);
              
              //restore out
              dup2(defaultout, 1);
              //exit child process
              exit(0);
              return;
            }
            else {
              //execute script
              int fail = execv(cgiScript[0], cgiScript);
              
              if (fail == -1) {
                char* header3 = "Content-type: text/plain\n\r\n\r";
                char * error = "File not Found";
    
                write( fd, header3, strlen(header3) );
                write( fd, error, strlen(error) );
              }

              //restore out
              dup2(defaultout, 1);
            
              //exit child process
              exit(0);
            }
          }
          else {
            char* header3 = "Content-type: text/plain\n\r\n\r";
            char * error = "File not Found";
    
            write( fd, header3, strlen(header3) );
            write( fd, error, strlen(error) );
            exit(0);
          }
        }
        
        return;
      }
      else if (strcmp(firstPart, "stats") == 0) {
        char* header1 = "HTTP/1.1 404 Document follows\n\r";
        char* header2 = "Server: CS 252 lab5\n\r";
        char* header3 = "Content-type: text/plain\n\r\n\r"; 
        write( fd, header1, strlen(header1) );
        write( fd, header2, strlen(header2) );
        write( fd, header3, strlen(header3) );

        char * line1 = "stats\n";
        write( fd, line1, strlen(line1) );
        //name
        line1 = "Server Created By: Elijah Scherrer\n";
        write( fd, line1, strlen(line1) ); 
        
        //time the server has been up
        ftime(&end);
        double aliveTime = (double)(1000 * (end.time - start.time) + (end.millitm - start.millitm ));
        aliveTime = (aliveTime / 1000.0);

        char timeString[5];
        sprintf(timeString, "%.3f", aliveTime);
        char line2[100];
        line1 = "This server has been alive for ";
        strcpy(line2, line1);
        strcat(line2, timeString);
        line1 = " seconds\n";
        strcat(line2, line1);

        write( fd, line2, strlen(line2) );

        //number of requests since the server started
        line1 = "This server has processed ";
        write( fd, line1, strlen(line1) ); 
        char requestsString[5];
        sprintf(requestsString, "%d", numRequests);
        write( fd, requestsString, strlen(requestsString) ); 
        line1 =  " requests\n";
        write( fd, line1, strlen(line1) ); 

        //minimum service time and the url that took that time
        timeString[5];
        sprintf(timeString, "%.3f", minTime);
        line2[100];
        line1 = "The shortest time required to process a request was  ";
        strcpy(line2, line1);
        strcat(line2, timeString);
        line1 = " seconds ";
        strcat(line2, line1);
        write( fd, line2, strlen(line2) );
        
        line1 = "at";
        write( fd, line1, strlen(line1) );
        write( fd, minAddress, strlen(minAddress) );
        line1 = "\n";
        write( fd, line1, strlen(line1) );


        //maximum service time and the url that took that time
        timeString[5];
        sprintf(timeString, "%.3f", maxTime);
        line2[100];
        line1 = "The longest time required to process a request was  ";
        strcpy(line2, line1);
        strcat(line2, timeString);
        line1 = " seconds ";
        strcat(line2, line1);
        write( fd, line2, strlen(line2) );

        line1 = "at";
        write( fd, line1, strlen(line1) );
        write( fd, maxAddress, strlen(maxAddress) );
        line1 = "\n";
        write( fd, line1, strlen(line1) );

        return;
      }
      else if (strcmp(firstPart, "logs") == 0) {
        //write header
        char* header1 = "HTTP/1.1 404 Document follows\n\r";
        char* header2 = "Server: CS 252 lab5\n\r";
        char* header3 = "Content-type: text/plain\n\r\n\r"; 
        write( fd, header1, strlen(header1) );
        write( fd, header2, strlen(header2) );
        write( fd, header3, strlen(header3) );

        char * line1 = "logs\n";
        write( fd, line1, strlen(line1) );
        
        //open and read the log file
        logFile = fopen("logs.txt", "r");
        fseek(logFile, 0, SEEK_END);
        long logsize = ftell(logFile);
        rewind(logFile);

        char logbody[logsize + 1];
        fread(logbody, logsize, 1, logFile);
        fclose(logFile);
        logbody[logsize] = '\0';
    
        //write the body
        write( fd, logbody, logsize + 1 );
        line1 = "\n";
        write(fd, line1, strlen(line1));
        
         

        return;
      }
      else {
        strcpy(adjustedLocation, "./http-root-dir/htdocs/");
        strcat(adjustedLocation, fileLocation);
      }
    
      //expand the path in realPath
      char realLocation[PATH_MAX + 1];
      realpath(adjustedLocation, realLocation);
      printf("--%s--\n\n", realLocation);

      //check if its a directory first
      DIR * dir = opendir(realLocation); 
      
      //it is not a directory
      if (dir == NULL) {
        //get header based on the type
        //grab extension
        char extension[6];
        int noExtension = 0;

        //find where it starts
        char pz = ' ';
        int ez = 0;
        while (pz != '.') {
          pz = fileLocation[strlen(fileLocation) - ez];
          ez++;
          if (pz == '/') {
            //there is no extension
            noExtension = 1;
            break;
          }
        }
        ez--;

        //if there is an extension
        if (noExtension == 0) {
          //fill in extension
          i = 0;
          while (ez != 0) {
            extension[i] = fileLocation[strlen(fileLocation) - ez];
            ez--;
            i++;
          }
          extension[i] = '\0';
        }
        else {
          //put a dummy word in for extension
          extension[0] = 'n';
          extension[1] = 'o';
          extension[2] = 'p';
          extension[3] = 'e';
          extension[4] = '\0';
        }

        /*Content-types, change depending on the suffix of the hfile
        * .html/.htm = text/html
        * .txt = text/plain
        * .jpg = image/jpeg
        * .png = image/png
        * .gif = image/gif
        * .svg = image/svg+xml
        */
        //TODO for icons to appear just set the extension to .ico and then have it
        //catch and send that here
        //make headers
        char* header1 = "HTTP/1.1 200 Document follows\r\n";
        char* header2 = "Server: CS 252 lab5\r\n";
        char* header3;
        
        if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
          header3 = "Content-type: text/html\r\n\r\n";
        }
        else if (strcmp(extension, ".txt") == 0) {
          header3 = "Content-type: text/plain\r\n\r\n";
        }
        else if (strcmp(extension, ".jpg") == 0) {
          header3 = "Content-type: image/jpeg\r\n\r\n";
        }
        else if (strcmp(extension, ".png") == 0) {
         header3 = "Content-type: image/png\r\n\r\n";
        }
        else if (strcmp(extension, ".gif") == 0) {
          header3 = "Content-type: image/gif\r\n\r\n";
        }
        else if (strcmp(extension, ".svg") == 0) {
          header3 = "Content-type: image/svg+xml\r\n\r\n";
        }
        else if (strcmp(extension, "nope") == 0) {
          //if there is no extension what do we do???
        }
        //special case for icons in browsing directories
        else if (strcmp(extension, ".ico") == 0) {
          header3 = "Content-type: image/gif\r\n\r\n";
          
          FILE * ficon;
          ficon = fopen("/home/u89/escherre/cs252/lab5-src/http-root-dir/icons/menu.gif", "rb");
          
          fseek(ficon, 0, SEEK_END);
          long fsize = ftell(ficon);
          //fseek(f, 0, SEEK_SET);
          rewind(ficon);

          char body[fsize + 1];
          fread(body, fsize, 1, ficon);
          fclose(ficon);
          body[fsize] = '\0';
    
          //write the header
          write( fd, header1, strlen(header1) );
          write( fd, header2, strlen(header2) );
          write( fd, header3, strlen(header3) );

          //write the body
          int written = write( fd, body, fsize + 1 );
          const char * newline="\n";
          write(fd, newline, strlen(newline));
          
          return;
        }
        else if (strcmp(extension, ".icp") == 0) {
          header3 = "Content-type: image/gif\r\n\r\n";
          
          FILE * ficon;
          ficon = fopen("/home/u89/escherre/cs252/lab5-src/http-root-dir/icons/telnet.gif", "rb");
          
          fseek(ficon, 0, SEEK_END);
          long fsize = ftell(ficon);
          //fseek(f, 0, SEEK_SET);
          rewind(ficon);

          char body[fsize + 1];
          fread(body, fsize, 1, ficon);
          fclose(ficon);
          body[fsize] = '\0';
    
          //write the header
          write( fd, header1, strlen(header1) );
          write( fd, header2, strlen(header2) );
          write( fd, header3, strlen(header3) );

          //write the body
          int written = write( fd, body, fsize + 1 );
          const char * newline="\n";
          write(fd, newline, strlen(newline));
          
          return;
        }
        else {
          header3 = "Content-type: text/plain\r\n\r\n";
        }

        //printf("%s\n", header3);

        //open file, read it, store it in body
        FILE * f;

        //f = fopen("./http-root-dir/htdocs/index.html", "r");
        f = fopen(realLocation, "rb");
        
        //if you couldn't open the file return file not found
        if (f == NULL) {
          //make headers
          char* header1 = "HTTP/1.1 404 Document follows\n\r";
          char* header2 = "Server: CS 252 lab5\n\r";
          char* header3 = "Content-type: text/plain\n\r\n\r";
      
          char * error = "File not Found";
      
          write( fd, header1, strlen(header1) );
          write( fd, header2, strlen(header2) );
          write( fd, header3, strlen(header3) );
      
          write( fd, error, strlen(error) );
        }
        else { 
        
          fseek(f, 0, SEEK_END);
          long fsize = ftell(f);
          //fseek(f, 0, SEEK_SET);
          rewind(f);

          char body[fsize + 1];
          fread(body, fsize, 1, f);
          fclose(f);
          body[fsize] = '\0';
    
          //write the header
          write( fd, header1, strlen(header1) );
          write( fd, header2, strlen(header2) );
          write( fd, header3, strlen(header3) );

          //write the body
          int written = write( fd, body, fsize + 1 );
          const char * newline="\n";
          write(fd, newline, strlen(newline));
   
        } 
      }
      //it is a directory
      else {
        
        //write headers
        char* header1 = "HTTP/1.1 404 Document follows\n\r";
        char* header2 = "Server: CS 252 lab5\n\r";
        char* header3 = "Content-type: text/html\n\r\n\r";
       
        write( fd, header1, strlen(header1) );
        write( fd, header2, strlen(header2) );
        write( fd, header3, strlen(header3) );
      
        //write the top stuff
        char * html = "<html>";
        write( fd, html, strlen(html) );
        char * body = "<body>";
        write( fd, body, strlen(body) );

        html = "<h1>Index of "; 
        write( fd, html, strlen(html) );
        write( fd, fileLocation, strlen(fileLocation) );
        html = "</h1>"; 
        write( fd, html, strlen(html) );

        char column = ' ';
        char direction = ' ';
        //determine what the query string will look like when you click to sort
        if (strcmp(queryBuffer, "") == 0) {
          html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
        }
        else {
          column = queryBuffer[2];
          direction = queryBuffer[6];
          if (column == 'N' && direction == 'A') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=D\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
          }
          if (column == 'N' && direction == 'D') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
          }
          if (column == 'M' && direction == 'A') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=D\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
          }
          if (column == 'M' && direction == 'D') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
          }
          if (column == 'S' && direction == 'A') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=D\">Size</a></th></tr>";
          }
          if (column == 'S' && direction == 'D') {
            html = "<table><tr><th valign=\"top\"><img src=\"./menu.ico\" alt=\"[ICO]\"></th><th><a href=\"?C=N&O=A\">Name</a></th><th><a href=\"?C=M&O=A\">Last modified</a></th><th><a href=\"?C=S&O=A\">Size</a></th></tr>";
          }
        }
        
        
        //start printing the table
        write( fd, html, strlen(html) );
        
        html = "<tr><th colspan=\"5\"><hr></th></tr>";
        write( fd, html, strlen(html) );


        //count the number of items in the directory
        struct dirent * entry;
        int fileCount = 0; 
        while ( (entry = readdir(dir)) != NULL  ) {
          char * entryName = entry->d_name;
          //don't show hidden files
          if (entryName[0] != '.') {
            fileCount++;
          }  
        }
        rewinddir(dir);


        //make an array of file names
        char * filesMan[fileCount];
        int index = 0;
        printf("num files: %d\n", fileCount);

        //now collect the files
        while ( (entry = readdir(dir)) != NULL  ) {
          char * entryName = entry->d_name;
          //don't show hidden files
          if (entryName[0] != '.') {
            filesMan[index] = entryName;
            index++;
          }
        }

        //start of every path
        char * ryan = "/helpme/";
        
        //array of structs
        fileobj * fileObjects = (fileobj*)calloc(fileCount, sizeof(fileobj));

        //make structs of all the files
        for (i = 0; i < fileCount; i++) {
               
            //get mod time and file size
            struct stat fileInfo;

            char anotherFile[strlen(realLocation) + strlen(filesMan[i]) + 1];
            strcpy(anotherFile, realLocation);
            strcat(anotherFile, "/");
            strcat(anotherFile, filesMan[i]);
             
            stat(anotherFile, &fileInfo);
            time_t* mtime = &fileInfo.st_mtime;
            //printf("%ld\n", mtime);
            long fileSize = fileInfo.st_size;
          
            //create route for new file path  
            int passLength = strlen(filesMan[i] + strlen(adjustedLocation) + strlen(ryan) + 1);
            char passedPath[passLength];

            strcpy(passedPath, ryan);
            strcat(passedPath, adjustedLocation);
            strcat(passedPath, "/");
            strcat(passedPath, filesMan[i]);            

            //put all values in the struct
            strcpy(fileObjects[i].name, filesMan[i]);
            fileObjects[i].modTime = mtime;
            fileObjects[i].size = fileSize;
            strcpy(fileObjects[i].path, passedPath);
        }

/*
typedef struct fileobj{
  char * name;
  long size;
  long modTime;
  char * path;
} fileobj;
*/        
        //TODO sort
        if (column == 'N' && direction == 'A') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortNA);
        }
        else if (column == 'N' && direction == 'D') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortND);
        }
        else if (column == 'M' && direction == 'A') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortMA);
        }
        else if (column == 'M' && direction == 'D') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortMD);
        }
        else if (column == 'S' && direction == 'A') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortSA);
        }
        else if (column == 'S' && direction == 'D') {
          qsort(fileObjects, fileCount, sizeof(fileobj), sortSD);
        }


        //print all the structs
        for (i = 0; i < fileCount; i++) {
          
            char * bbname = fileObjects[i].name;
            char * bbpath = fileObjects[i].path;
            long bbsize = fileObjects[i].size;
            time_t* bbmod = fileObjects[i].modTime;

            char modified[36];
            //long mtime = &fileInfo.st_mtime;
            //printf("%ld\n", mtime);
            strftime(modified, 36, "%m.%d.%Y %H:%M:%S", localtime(bbmod));
 
            printf("raw time: %ld\n", bbmod);
            printf("file: %s  modified: %s  size: %ld\n", bbname, modified, bbsize );
            writeFileHtml (bbname, modified, bbsize, fd, bbpath);

        }
       
        free(fileObjects);

        //finish the table
        html = "<tr><td valign=\"top\"><img src=\"./telnet.icp\" alt=\"[PARENTDIR]\"></td><td><a href=\"";
        write( fd, html, strlen(html) );
       
        //get parent directory path
        char parentPath[strlen(ryan) + strlen(adjustedLocation)];
        strcpy(parentPath, ryan);

        //cut off last part of directory
        index = 1;
        char0 = adjustedLocation[strlen(adjustedLocation) - index];
        while (char0 != '/') {
          index++;
          char0 = adjustedLocation[strlen(adjustedLocation) - index];
        }

        //append directory to secret
        int parentLength = strlen(ryan);
        for (i = 0; i < strlen(adjustedLocation) - index; i++ ) {
          parentPath[i + strlen(ryan)] = adjustedLocation[i];
          parentLength++;
        }
        parentPath[parentLength] = '\0';

        write( fd, parentPath, strlen(parentPath) );
        html = "\">Parent Directory</a>       </td><td>&nbsp;</td></tr>";
        write( fd, html, strlen(html) );



        html = "<tr><th colspan=\"5\"><hr></th></tr></table>";
        write( fd, html, strlen(html) );


        //write closing tags
        body = "</body>";
        write( fd, body, strlen(body) );
        html = "</html>";
        write( fd, html, strlen(html) );
        
        closedir(dir);
      }
    }
  
  } 
  //send 404 error
  else {
    
    //make headers
    char* header1 = "HTTP/1.1 404 Document follows\n\r";
    char* header2 = "Server: CS 252 lab5\n\r";
    char* header3 = "Content-type: text/plain\n\r\n\r";
    
    char * error = "File not Found";
    
    write( fd, header1, strlen(header1) );
    write( fd, header2, strlen(header2) );
    write( fd, header3, strlen(header3) );
    
    write( fd, error, strlen(error) );
  }
  

}


void writeFileHtml (char * name, char * modified, long size, int fd, char * path) {
  char * html = "<tr><td valightn=\"top\"></td><td><a href=\"";
  write( fd, html, strlen(html) );
  write( fd, path, strlen(path) ); //insert path
  html = "\">";
  write( fd, html, strlen(html) );
  write( fd, name, strlen(name) ); //insert file name  
  html = "</a></td>";
  write( fd, html, strlen(html) );

  html = "<td><label>";
  write( fd, html, strlen(html) );
  write( fd, modified, strlen(modified) ); //insert modified time
  html = "</label></td>";
  write( fd, html, strlen(html) );

  html = "<td><label>";
  write( fd, html, strlen(html) );
  //make size a string
  char tmp[250];
  sprintf(tmp, "%ld", size);
  write( fd, tmp, strlen(tmp) ); //insert size
  html = "</label></td>";
  write( fd, html, strlen(html) );

}

int sortNA(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;

  int val = strcmp(obj1->name, obj2->name);
  if (val > 0) {
    return 1;
  }
  else if (val < 0) {
    return -1;
  }
  return 0;
}
int sortND(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;

  int val = strcmp(obj1->name, obj2->name);
  if (val > 0) {
    return -1;
  }
  else if (val < 0) {
    return 1;
  }
  return 0;
}

int sortMA(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;
  
  time_t * mod1 = obj1->modTime;
  time_t * mod2 = obj2->modTime;
  if (mod1 > mod2) {
    return 1;
  }
  else if (mod1 < mod2) {
    return -1;
  }
  
  return 0;
}
int sortMD(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;
  
  time_t * mod1 = obj1->modTime;
  time_t * mod2 = obj2->modTime;
  if (mod1 > mod2) {
    return -1;
  }
  else if (mod1 < mod2) {
    return 1;
  }
  
  return 0;
}

int sortSA(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;
  
  long size1 = obj1->size;
  long size2 = obj2->size;
  if (size1 > size2) {
    return 1;
  }
  else if (size1 < size2) {
    return -1;
  }
  
  return 0;
}
int sortSD(const void * a, const void * b) {
  fileobj* obj1 = (fileobj*)a;
  fileobj* obj2 = (fileobj*)b;
  
  long size1 = obj1->size;
  long size2 = obj2->size;
  if (size1 > size2) {
    return -1;
  }
  else if (size1 < size2) {
    return 1;
  }
  
  
  return 0;
}

//other ways for reading/ writing files
      
      /*
      int i = 0;
      char plz;
      char body[99999];
      f = fopen(fileLocation, "rb");
      
      while (1) { 
        if (feof(f) != 0) {
          break;
        }

        fread(&plz, sizeof(plz), 1, f);
        body[i] = plz;
        i++;
      }
      fclose(f);

      body[i] = '\0';
      write(fd, body, strlen(body));
      //const char * newline="\n";
      //write(fd, newline, strlen(newline));
*/

/*
      f = fopen(fileLocation, "rb");
      int i;
      char plz;
      while (i = read(fileno(f), &plz, sizeof(plz) )) {
        if ( write(fd, &plz, sizeof(plz)) != i ) {
          exit(2);
        }
        //if (feof(f) != 0) {
        //  break;
        //}
      }
      fclose(f);

      write(fd, '\0', sizeof(plz));
      const char * newline="\n";
      write(fd, newline, strlen(newline));
  */ 

/*old way of printing files on website

        //print all the structs
        for (i = 0; i < fileCount; i++) {
            //get mod time and file size
            struct stat fileInfo;

            char anotherFile[strlen(realLocation) + strlen(filesMan[i]) + 1];
            strcpy(anotherFile, realLocation);
            strcat(anotherFile, "/");
            strcat(anotherFile, filesMan[i]);
            //printf("file-> -%s-\n", anotherFile);
            
            
            stat(anotherFile, &fileInfo);
            char modified[36];
            //long mtime = &fileInfo.st_mtime;
            //printf("%ld\n", mtime);
            strftime(modified, 36, "%m.%d.%Y %H:%M:%S", localtime(&fileInfo.st_mtime));
            long fileSize = fileInfo.st_size;
            //printf("%ld\n", fileSize);
 
            printf("file: %s  modified: %s  size: %ld\n", filesMan[i], modified, fileSize );
          
            //create route for new file path  
            int passLength = strlen(filesMan[i] + strlen(adjustedLocation) + strlen(ryan) + 1);
            char passedPath[passLength];

            strcpy(passedPath, ryan);
            strcat(passedPath, adjustedLocation);
            strcat(passedPath, "/");
            strcat(passedPath, filesMan[i]);
            
            writeFileHtml (filesMan[i], modified, fileSize, fd, passedPath);
        }


*/
