#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fnmatch.h>
#include <getopt.h>
#include <CoreServices/CoreServices.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>

#ifdef DEBUG
#define LOG(...) fprintf(stderr, "DEBUG: " __VA_ARGS__)
#else
#define LOG(...)
#endif

extern char **environ;

//the command to run
static char *command;

static void printline();

static void exec_command()
{
    printline();

    pid_t pid = fork();
    
    if(pid < 0) {
        fprintf(stderr, "error: couldn't fork\n");
        exit(1);
    } else if (pid == 0) {
        char *args[4] = { "/bin/bash", "-c", command, 0 };
        if(execve(args[0], args, environ) < 0) {
            fprintf(stderr, "error: error executing\n");
            exit(1);
        }
    } else {
        int status;
        while(wait(&status) != pid);
    }
}

static void printline()
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    printf("\e[31m");
    for (int i = 0; i < w.ws_col; i++) putc('~', stdout);
    printf("\e[0m\n");
}

static void callback (ConstFSEventStreamRef streamRef,
                      void *clientCallBackInfo,
                      size_t numEvents,
                      void *eventPaths,
                      const FSEventStreamEventFlags eventFlags[],
                      const FSEventStreamEventId eventIds[])
{
    LOG("callback called\n");
    for (int i = 0; i < numEvents; i++) {
        LOG("  eventPaths[%d]=\"%s\"\n", i, ((char **)eventPaths)[i]);
    }
    
    exec_command();
}

static void print_usage_and_exit()
{
    fprintf(stderr, "Usage:\n fswatch command path [path...]\n");
    exit(1);
}


static struct option options[] = {
    { "ignore", required_argument, NULL, 'i' },
    { NULL, 0, NULL, 0 }
};

static char * CreateCStringFromCFString(CFStringRef input)
{
    CFStringEncoding encoding = kCFStringEncodingUTF8;
    CFIndex length = CFStringGetLength(input);
    CFIndex bufferSize = CFStringGetMaximumSizeForEncoding(length, encoding) + 1;
    char * buffer = malloc(bufferSize);
    Boolean success = CFStringGetCString(input, buffer, bufferSize, encoding);
    if (!success) {
        fprintf(stderr, "unable to convert CFString\n");
        exit(1);
    }
    return buffer;
}

static void PrintPathsBeingWatched(FSEventStreamRef stream)
{
    CFArrayRef pathsBeingWatched = FSEventStreamCopyPathsBeingWatched(stream);
    CFIndex count = CFArrayGetCount(pathsBeingWatched);
    printf("watching %ld directories:\n", count);
    for (CFIndex i = 0; i < count; i++) {
        CFStringRef cfPath = CFArrayGetValueAtIndex(pathsBeingWatched, i);
        char * cpath = CreateCStringFromCFString(cfPath);
        printf("  %s\n", cpath);
        free(cpath);
    }
    CFRelease(pathsBeingWatched);
}

static FSEventStreamRef CreateFSEventStream(CFArrayRef pathsToWatch,
                                            CFTimeInterval latency,
                                            CFArrayRef ignoredPaths)
{
    FSEventStreamRef stream = FSEventStreamCreate(NULL,
                                                  &callback,
                                                  (void *)NULL,
                                                  pathsToWatch,
                                                  kFSEventStreamEventIdSinceNow,
                                                  latency,
                                                  kFSEventStreamCreateFlagNoDefer | kFSEventStreamCreateFlagFileEvents);
    FSEventStreamScheduleWithRunLoop(stream,
                                     CFRunLoopGetCurrent(),
                                     kCFRunLoopDefaultMode);
    FSEventStreamStart(stream);
    return stream;
}

int main(int argc, char *argv[])
{
    CFTimeInterval latency = 0.5;
    
    CFMutableArrayRef ignoredPaths = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    int opt;
    while ((opt = getopt_long(argc, argv, "i:", options, NULL)) != -1) {
        switch (opt) {
            case 'i': {
                CFStringRef path = CFStringCreateWithCString(NULL, optarg, kCFStringEncodingUTF8);
                CFArrayAppendValue(ignoredPaths, path);
                CFRelease(path);
                break;
            }
            default:
                print_usage_and_exit();
        }
    }
    argc -= optind;
    argv += optind;
    
    if (argc < 2) {
        print_usage_and_exit();
    }
    
    // first non-option arg is the command to run
    command = argv[0];
    LOG("command is %s\n", command);
    
    // rest of the non-option args are paths
    CFMutableArrayRef pathsToWatch = CFArrayCreateMutable(NULL, argc - 1, &kCFTypeArrayCallBacks);
    for (int i = 1; i < argc; i++) {
        CFStringRef path = CFStringCreateWithCString(NULL, argv[i], kCFStringEncodingUTF8);
        CFArrayAppendValue(pathsToWatch, path);
        CFRelease(path);
    }
    
    FSEventStreamRef stream = CreateFSEventStream(pathsToWatch, latency, ignoredPaths);
    CFRelease(ignoredPaths);
    CFRelease(pathsToWatch);
    
    PrintPathsBeingWatched(stream);
    
    FSEventStreamRelease(stream);
    
    exec_command();
    
    CFRunLoopRun();
}
