/****************************************************************************
 Copyright (c) 2013-2014 Chukong Technologies Inc.

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "base/CCConsole.h"

#include <pthread.h>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <io.h>
#include <errno.h>
#include <assert.h>
#include <WS2tcpip.h>
#include <Winsock2.h>
#if defined(__MINGW32__)
#include "platform/win32/inet_pton_mingw.h"
#endif
#define bzero(a, b) memset(a, 0, b);
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
#include "inet_ntop_winrt.h"
#include "inet_pton_winrt.h"
#include "CCWinRTUtils.h"
#endif
#else
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#endif

#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "platform/CCPlatformConfig.h"
#include "base/CCConfiguration.h"
#include "2d/CCScene.h"
#include "platform/CCFileUtils.h"
#include "renderer/CCTextureCache.h"
#include "base/base64.h"
#include "base/ccUtils.h"
#include "base/allocator/CCAllocatorDiagnostics.h"
NS_CC_BEGIN

extern const char* cocos2dVersion(void);
//TODO: these general utils should be in a seperate class
//
// Trimming functions were taken from: http://stackoverflow.com/a/217605
//
// trim from start
static std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
}

// trim from end
static std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
}

// trim from both ends
static std::string &trim(std::string &s) {
    return ltrim(rtrim(s));
}

static std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}
//isFloat taken from http://stackoverflow.com/questions/447206/c-isfloat-function
static bool isFloat( std::string myString ) {
    std::istringstream iss(myString);
    float f;
    iss >> std::noskipws >> f; // noskipws considers leading whitespace invalid
    // Check the entire string was consumed and if either failbit or badbit is set
    return iss.eof() && !iss.fail(); 
}

// helper free functions

// dprintf() is not defined in Android
// so we add our own 'dpritnf'
static ssize_t mydprintf(int sock, const char *format, ...)
{
    va_list args;
	char buf[16386];

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	return send(sock, buf, strlen(buf),0);
}

static void sendPrompt(int fd)
{
    const char prompt[] = "> ";
    send(fd, prompt, strlen(prompt),0);
}

static int printSceneGraph(int fd, Node* node, int level)
{
    int total = 1;
    for(int i=0; i<level; ++i)
        send(fd, "-", 1,0);

    mydprintf(fd, " %s\n", node->getDescription().c_str());

    //for(const auto& child: node->getChildren())
	auto& children = node->getChildren();
	for (auto p_child = children.begin(); p_child != children.end(); ++p_child)
	{
		const auto& child = *p_child;
        total += printSceneGraph(fd, child, level+1);
	}

    return total;
}

static void printSceneGraphBoot(int fd)
{
    send(fd,"\n",1,0);
    auto scene = Director::getInstance()->getRunningScene();
    int total = printSceneGraph(fd, scene, 0);
    mydprintf(fd, "Total Nodes: %d\n", total);
    sendPrompt(fd);
}

static void printFileUtils(int fd)
{
    FileUtils* fu = FileUtils::getInstance();

    mydprintf(fd, "\nSearch Paths:\n");
    auto& list = fu->getSearchPaths();
    //for( const auto &item : list) {
	for (auto p_item = list.begin(); p_item != list.end(); ++p_item)
	{
		const auto& item = *p_item;
        mydprintf(fd, "%s\n", item.c_str());
    }

    mydprintf(fd, "\nResolution Order:\n");
    auto& list1 = fu->getSearchResolutionsOrder();
    //for( const auto &item : list1) {
	for (auto p_item = list1.begin(); p_item != list1.end(); ++p_item)
	{
		const auto& item = *p_item;
        mydprintf(fd, "%s\n", item.c_str());
    }

    mydprintf(fd, "\nWriteble Path:\n");
    mydprintf(fd, "%s\n", fu->getWritablePath().c_str());

    mydprintf(fd, "\nFull Path Cache:\n");
    auto& cache = fu->getFullPathCache();
    //for( const auto &item : cache) {
	for (auto p_item = cache.begin(); p_item != cache.end(); ++p_item)
	{
		const auto& item = *p_item;
        mydprintf(fd, "%s -> %s\n", item.first.c_str(), item.second.c_str());
    }
    sendPrompt(fd);
}


#if defined(__MINGW32__) || defined(_MSC_VER)
static const char* _inet_ntop(int af, const void* src, char* dst, int cnt)
{
    struct sockaddr_in srcaddr;

    memset(&srcaddr, 0, sizeof(struct sockaddr_in));
    memcpy(&(srcaddr.sin_addr), src, sizeof(srcaddr.sin_addr));

    srcaddr.sin_family = af;
    if (WSAAddressToStringA((struct sockaddr*) &srcaddr, sizeof(struct sockaddr_in), 0, dst, (LPDWORD) &cnt) != 0)
    {
        return nullptr;
    }
    return dst;
}



#define ERRNO         ((int)GetLastError())
#define SET_ERRNO(x)  (SetLastError((DWORD)(x)))
#ifndef INADDRSZ
#define INADDRSZ 4		/* IPv4 T_A */
#endif
#ifndef IN6ADDRSZ
#define IN6ADDRSZ	16	/* IPv6 T_AAAA */
#endif

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static int      _inet_pton4(const char *src, unsigned char *dst);
#ifdef ENABLE_IPV6
static int      _inet_pton6(const char *src, unsigned char *dst);
#endif

/* int
 * inet_pton(af, src, dst)
 *      convert from presentation format (which usually means ASCII printable)
 *      to network format (which is usually some kind of binary format).
 * return:
 *      1 if the address was valid for the specified address family
 *      0 if the address wasn't valid (`dst' is untouched in this case)
 *      -1 if some other error occurred (`dst' is untouched in this case, too)
 * notice:
 *      On Windows we store the error in the thread errno, not
 *      in the winsock error code. This is to avoid losing the
 *      actual last winsock error. So use macro ERRNO to fetch the
 *      errno this function sets when returning (-1), not SOCKERRNO.
 * author:
 *      Paul Vixie, 1996.
 */

static int _inet_pton(int af, const char *src, void *dst)
{
  switch (af) {
  case AF_INET:
    return (_inet_pton4(src, (unsigned char *)dst));
#ifdef ENABLE_IPV6
  case AF_INET6:
    return (_inet_pton6(src, (unsigned char *)dst));
#endif
  default:
    SET_ERRNO(EAFNOSUPPORT);
    return (-1);
  }
  /* NOTREACHED */
}

/* int
 * inet_pton4(src, dst)
 *      like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *      1 if `src' is a valid dotted quad, else 0.
 * notice:
 *      does not touch `dst' unless it's returning 1.
 * author:
 *      Paul Vixie, 1996.
 */
static int
_inet_pton4(const char *src, unsigned char *dst)
{
  static const char digits[] = "0123456789";
  int saw_digit, octets, ch;
  unsigned char tmp[INADDRSZ], *tp;

  saw_digit = 0;
  octets = 0;
  tp = tmp;
  *tp = 0;
  while((ch = *src++) != '\0') {
    const char *pch;

    if((pch = strchr(digits, ch)) != NULL) {
      unsigned int val = *tp * 10 + (unsigned int)(pch - digits);

      if(saw_digit && *tp == 0)
        return (0);
      if(val > 255)
        return (0);
      *tp = (unsigned char)val;
      if(! saw_digit) {
        if(++octets > 4)
          return (0);
        saw_digit = 1;
      }
    }
    else if(ch == '.' && saw_digit) {
      if(octets == 4)
        return (0);
      *++tp = 0;
      saw_digit = 0;
    }
    else
      return (0);
  }
  if(octets < 4)
    return (0);
  memcpy(dst, tmp, INADDRSZ);
  return (1);
}

#ifdef ENABLE_IPV6
/* int
 * inet_pton6(src, dst)
 *      convert presentation level address to network order binary form.
 * return:
 *      1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *      (1) does not touch `dst' unless it's returning 1.
 *      (2) :: in a full address is silently ignored.
 * credit:
 *      inspired by Mark Andrews.
 * author:
 *      Paul Vixie, 1996.
 */
static int
_inet_pton6(const char *src, unsigned char *dst)
{
  static const char xdigits_l[] = "0123456789abcdef",
    xdigits_u[] = "0123456789ABCDEF";
  unsigned char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
  const char *xdigits, *curtok;
  int ch, saw_xdigit;
  size_t val;

  memset((tp = tmp), 0, IN6ADDRSZ);
  endp = tp + IN6ADDRSZ;
  colonp = NULL;
  /* Leading :: requires some special handling. */
  if(*src == ':')
    if(*++src != ':')
      return (0);
  curtok = src;
  saw_xdigit = 0;
  val = 0;
  while((ch = *src++) != '\0') {
    const char *pch;

    if((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
      pch = strchr((xdigits = xdigits_u), ch);
    if(pch != NULL) {
      val <<= 4;
      val |= (pch - xdigits);
      if(++saw_xdigit > 4)
        return (0);
      continue;
    }
    if(ch == ':') {
      curtok = src;
      if(!saw_xdigit) {
        if(colonp)
          return (0);
        colonp = tp;
        continue;
      }
      if(tp + INT16SZ > endp)
        return (0);
      *tp++ = (unsigned char) (val >> 8) & 0xff;
      *tp++ = (unsigned char) val & 0xff;
      saw_xdigit = 0;
      val = 0;
      continue;
    }
    if(ch == '.' && ((tp + INADDRSZ) <= endp) &&
        _inet_pton4(curtok, tp) > 0) {
      tp += INADDRSZ;
      saw_xdigit = 0;
      break;    /* '\0' was seen by inet_pton4(). */
    }
    return (0);
  }
  if(saw_xdigit) {
    if(tp + INT16SZ > endp)
      return (0);
    *tp++ = (unsigned char) (val >> 8) & 0xff;
    *tp++ = (unsigned char) val & 0xff;
  }
  if(colonp != NULL) {
    /*
     * Since some memmove()'s erroneously fail to handle
     * overlapping regions, we'll do the shift by hand.
     */
    const ssize_t n = tp - colonp;
    ssize_t i;

    if(tp == endp)
      return (0);
    for(i = 1; i <= n; i++) {
      *(endp - i) = *(colonp + n - i);
      *(colonp + n - i) = 0;
    }
    tp = endp;
  }
  if(tp != endp)
    return (0);
  memcpy(dst, tmp, IN6ADDRSZ);
  return (1);
}
#endif /* ENABLE_IPV6 */



#endif


#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
static const int CCLOG_STRING_TAG = 1;
void SendLogToWindow(const char *log)
{
    // Send data as a message
    COPYDATASTRUCT myCDS;
    myCDS.dwData = CCLOG_STRING_TAG;
    myCDS.cbData = (DWORD)strlen(log) + 1;
    myCDS.lpData = (PVOID)log;
    if (Director::getInstance()->getOpenGLView())
    {
        HWND hwnd = Director::getInstance()->getOpenGLView()->getWin32Window();
        SendMessage(hwnd,
            WM_COPYDATA,
            (WPARAM)(HWND)hwnd,
            (LPARAM)(LPVOID)&myCDS);
    }
}
#else
void SendLogToWindow(const char *log)
{
}
#endif

//
// Free functions to log
//

static void _log(const char *format, va_list args)
{
    char buf[MAX_LOG_LENGTH];

    vsnprintf(buf, MAX_LOG_LENGTH-3, format, args);
    strcat(buf, "\n");

#if CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "cocos2d-x debug info",  "%s", buf);

#elif CC_TARGET_PLATFORM ==  CC_PLATFORM_WIN32 || CC_TARGET_PLATFORM == CC_PLATFORM_WINRT || CC_TARGET_PLATFORM == CC_PLATFORM_WP8
    WCHAR wszBuf[MAX_LOG_LENGTH] = {0};
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wszBuf, sizeof(wszBuf));
    OutputDebugStringW(wszBuf);
    WideCharToMultiByte(CP_ACP, 0, wszBuf, -1, buf, sizeof(buf), nullptr, FALSE);
    printf("%s", buf);
    SendLogToWindow(buf);
    fflush(stdout);
#else
    // Linux, Mac, iOS, etc
    fprintf(stdout, "%s", buf);
    fflush(stdout);
#endif

    Director::getInstance()->getConsole()->log(buf);

}

// FIXME: Deprecated
void CCLog(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    _log(format, args);
    va_end(args);
}

void log(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    _log(format, args);
    va_end(args);
}

//
// Console code
//

Console::Console()
: _listenfd(-1)
, _running(false)
, _endThread(false)
, _sendDebugStrings(false)
, _bindAddress("")
{
    // VS2012 doesn't support initializer list, so we create a new array and assign its elements to '_command'.
	Command commands[] = {     
        { "allocator", "Display allocator diagnostics for all allocators", std::bind(&Console::commandAllocator, this, std::placeholders::_1, std::placeholders::_2) },
        { "config", "Print the Configuration object", std::bind(&Console::commandConfig, this, std::placeholders::_1, std::placeholders::_2) },
        { "debugmsg", "Whether or not to forward the debug messages on the console. Args: [on | off]", std::bind(&Console::commandDebugmsg, this, std::placeholders::_1, std::placeholders::_2) },
        { "exit", "Close connection to the console", std::bind(&Console::commandExit, this, std::placeholders::_1, std::placeholders::_2) },
        { "fileutils", "Flush or print the FileUtils info. Args: [flush | ] ", std::bind(&Console::commandFileUtils, this, std::placeholders::_1, std::placeholders::_2) },
        { "fps", "Turn on / off the FPS. Args: [on | off] ", std::bind(&Console::commandFps, this, std::placeholders::_1, std::placeholders::_2) },
        { "help", "Print this message", std::bind(&Console::commandHelp, this, std::placeholders::_1, std::placeholders::_2) },
        { "projection", "Change or print the current projection. Args: [2d | 3d]", std::bind(&Console::commandProjection, this, std::placeholders::_1, std::placeholders::_2) },
        { "resolution", "Change or print the window resolution. Args: [width height resolution_policy | ]", std::bind(&Console::commandResolution, this, std::placeholders::_1, std::placeholders::_2) },
        { "scenegraph", "Print the scene graph", std::bind(&Console::commandSceneGraph, this, std::placeholders::_1, std::placeholders::_2) },
        { "texture", "Flush or print the TextureCache info. Args: [flush | ] ", std::bind(&Console::commandTextures, this, std::placeholders::_1, std::placeholders::_2) },
        { "director", "director commands, type -h or [director help] to list supported directives", std::bind(&Console::commandDirector, this, std::placeholders::_1, std::placeholders::_2) },
        { "touch", "simulate touch event via console, type -h or [touch help] to list supported directives", std::bind(&Console::commandTouch, this, std::placeholders::_1, std::placeholders::_2) },
        { "upload", "upload file. Args: [filename base64_encoded_data]", std::bind(&Console::commandUpload, this, std::placeholders::_1) },
        { "version", "print version string ", std::bind(&Console::commandVersion, this, std::placeholders::_1, std::placeholders::_2) },
    };

     ;
	for (int i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i)
	{
		_commands[commands[i].name] = commands[i];
	}
	_writablePath = FileUtils::getInstance()->getWritablePath();
	pthread_mutex_init(&_DebugStringsMutex, NULL);
}

void Console::commandDebugmsg(int fd, const std::string& args)
{
    if( args.compare("on")==0 || args.compare("off")==0) {
        _sendDebugStrings = (args.compare("on") == 0);
    } else {
        mydprintf(fd, "Debug message is: %s\n", _sendDebugStrings ? "on" : "off");
    }
}

void Console::commandFps(int fd, const std::string& args)
{
    if( args.compare("on")==0 || args.compare("off")==0) {
        bool state = (args.compare("on") == 0);
        Director *dir = Director::getInstance();
        Scheduler *sched = dir->getScheduler();
        sched->performFunctionInCocosThread( std::bind(&Director::setDisplayStats, dir, state));
    } else {
        mydprintf(fd, "FPS is: %s\n", Director::getInstance()->isDisplayStats() ? "on" : "off");
    }
}

void Console::commandVersion(int fd, const std::string& args)
{
	mydprintf(fd, "%s\n", cocos2dVersion());
}

Console::~Console()
{
    stop();
}

bool Console::listenOnTCP(int port)
{
    int listenfd = -1, n;
    const int on = 1;
    struct addrinfo hints, *res, *ressave;
    char serv[30];

    snprintf(serv, sizeof(serv)-1, "%d", port );

    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET; // AF_UNSPEC: Do we need IPv6 ?
    hints.ai_socktype = SOCK_STREAM;

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    WSADATA wsaData;
    n = WSAStartup(MAKEWORD(2, 2),&wsaData);

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WP8)
    CCLogIPAddresses();
#endif
#endif

    if ( (n = getaddrinfo(nullptr, serv, &hints, &res)) != 0) {
        fprintf(stderr,"net_listen error for %s: %s", serv, gai_strerror(n));
        return false;
    }

    ressave = res;

    do {
        listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (listenfd < 0)
            continue;       /* error, try next one */

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

        // bind address
        if (_bindAddress.length() > 0)
        {
            if (res->ai_family == AF_INET)
            {
                struct sockaddr_in *sin = (struct sockaddr_in*) res->ai_addr;
#if defined(__MINGW32__) || defined(_MSC_VER)
                _inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin_addr);
#else
				inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin_addr);
#endif
            }
            else if (res->ai_family == AF_INET6)
            {
                struct sockaddr_in6 *sin = (struct sockaddr_in6*) res->ai_addr;
#if defined(__MINGW32__) || defined(_MSC_VER)
                _inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin6_addr);
#else
				inet_pton(res->ai_family, _bindAddress.c_str(), (void*)&sin->sin6_addr);
#endif
            }
        }

        if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
            break;          /* success */

/* bind error, close and try next one */
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
        closesocket(listenfd);
#else
        close(listenfd);
#endif
    } while ( (res = res->ai_next) != nullptr);
    
    if (res == nullptr) {
        perror("net_listen:");
        freeaddrinfo(ressave);
        return false;
    }

    listen(listenfd, 50);

    if (res->ai_family == AF_INET) {
        char buf[INET_ADDRSTRLEN] = "";
        struct sockaddr_in *sin = (struct sockaddr_in*) res->ai_addr;
#if defined(__MINGW32__) || defined(_MSC_VER)		
        if( _inet_ntop(res->ai_family, &sin->sin_addr, buf, sizeof(buf)) != nullptr )
#else
		if( inet_ntop(res->ai_family, &sin->sin_addr, buf, sizeof(buf)) != nullptr )
#endif		
            cocos2d::log("Console: listening on  %s : %d", buf, ntohs(sin->sin_port));
        else
            perror("inet_ntop");
    } else if (res->ai_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN] = "";
        struct sockaddr_in6 *sin = (struct sockaddr_in6*) res->ai_addr;
#if defined(__MINGW32__) || defined(_MSC_VER)		
        if( _inet_ntop(res->ai_family, &sin->sin6_addr, buf, sizeof(buf)) != nullptr )
#else
		if( inet_ntop(res->ai_family, &sin->sin6_addr, buf, sizeof(buf)) != nullptr )
#endif		
            cocos2d::log("Console: listening on  %s : %d", buf, ntohs(sin->sin6_port));
        else
            perror("inet_ntop");
    }


    freeaddrinfo(ressave);
    return listenOnFileDescriptor(listenfd);
}

bool Console::listenOnFileDescriptor(int fd)
{
    if(_running) {
        cocos2d::log("Console already started. 'stop' it before calling 'listen' again");
        return false;
    }

    _listenfd = fd;
    _thread = new pthread_t();
	pthread_create(_thread, NULL, &Console::loop_entry, this);

    return true;
}

void Console::stop()
{
    if( _running ) {
        _endThread = true;
        pthread_join(*_thread, NULL);
    }
}

void Console::addCommand(const Command& cmd)
{
    _commands[cmd.name]=cmd;
}

//
// commands
//

void Console::commandHelp(int fd, const std::string &args)
{
    const char help[] = "\nAvailable commands:\n";
    send(fd, help, sizeof(help),0);
    for(auto it=_commands.begin();it!=_commands.end();++it)
    {
        auto cmd = it->second;
        mydprintf(fd, "\t%s", cmd.name.c_str());
        ssize_t tabs = strlen(cmd.name.c_str()) / 8;
        tabs = 3 - tabs;
        for(int j=0;j<tabs;j++){
             mydprintf(fd, "\t");
        }
        mydprintf(fd,"%s\n", cmd.help.c_str());
    } 
}

void Console::commandExit(int fd, const std::string &args)
{
    FD_CLR(fd, &_read_set);
    _fds.erase(std::remove(_fds.begin(), _fds.end(), fd), _fds.end());
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
        closesocket(fd);
#else
        close(fd);
#endif
}

void Console::commandSceneGraph(int fd, const std::string &args)
{
    Scheduler *sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread( std::bind(&printSceneGraphBoot, fd) );
}

void Console::commandFileUtils(int fd, const std::string &args)
{
    Scheduler *sched = Director::getInstance()->getScheduler();

    if( args.compare("flush") == 0 )
    {
        FileUtils::getInstance()->purgeCachedEntries();
    }
    else if( args.length()==0)
    {
        sched->performFunctionInCocosThread( std::bind(&printFileUtils, fd) );
    }
    else
    {
        mydprintf(fd, "Unsupported argument: '%s'. Supported arguments: 'flush' or nothing", args.c_str());
    }
}

void Console::commandConfig(int fd, const std::string& args)
{
    Scheduler *sched = Director::getInstance()->getScheduler();
    sched->performFunctionInCocosThread( [=](){
        mydprintf(fd, "%s", Configuration::getInstance()->getInfo().c_str());
        sendPrompt(fd);
    }
                                        );
}

void Console::commandResolution(int fd, const std::string& args)
{
    if(args.length()==0) {
        auto director = Director::getInstance();
        Size points = director->getWinSize();
        Size pixels = director->getWinSizeInPixels();
        auto glview = director->getOpenGLView();
        Size design = glview->getDesignResolutionSize();
        ResolutionPolicy res = glview->getResolutionPolicy();
        Rect visibleRect = glview->getVisibleRect();

        mydprintf(fd, "Window Size:\n"
                        "\t%d x %d (points)\n"
                        "\t%d x %d (pixels)\n"
                        "\t%d x %d (design resolution)\n"
                        "Resolution Policy: %d\n"
                        "Visible Rect:\n"
                        "\torigin: %d x %d\n"
                        "\tsize: %d x %d\n",
                  (int)points.width, (int)points.height,
                  (int)pixels.width, (int)pixels.height,
                  (int)design.width, (int)design.height,
                  (int)res,
                  (int)visibleRect.origin.x, (int)visibleRect.origin.y,
                  (int)visibleRect.size.width, (int)visibleRect.size.height
                  );

    } else {
        int width, height, policy;

        std::istringstream stream( args );
        stream >> width >> height>> policy;

        Scheduler *sched = Director::getInstance()->getScheduler();
        sched->performFunctionInCocosThread( [=](){
            Director::getInstance()->getOpenGLView()->setDesignResolutionSize(width, height, static_cast<ResolutionPolicy>(policy));
        } );
    }
}

void Console::commandProjection(int fd, const std::string& args)
{
    auto director = Director::getInstance();
    Scheduler *sched = director->getScheduler();

    if(args.length()==0)
    {
        char buf[20];
        auto proj = director->getProjection();
        switch (proj) {
            case cocos2d::Director::Projection::_2D:
                sprintf(buf,"2d");
                break;
            case cocos2d::Director::Projection::_3D:
                sprintf(buf,"3d");
                break;
            case cocos2d::Director::Projection::CUSTOM:
                sprintf(buf,"custom");
                break;

            default:
                sprintf(buf,"unknown");
                break;
        }
        mydprintf(fd, "Current projection: %s\n", buf);
    }
    else if( args.compare("2d") == 0)
    {
        sched->performFunctionInCocosThread( [=](){
            director->setProjection(Director::Projection::_2D);
        } );
    }
    else if( args.compare("3d") == 0)
    {
        sched->performFunctionInCocosThread( [=](){
            director->setProjection(Director::Projection::_3D);
        } );
    }
    else
    {
        mydprintf(fd, "Unsupported argument: '%s'. Supported arguments: '2d' or '3d'\n", args.c_str());
    }
}

void Console::commandTextures(int fd, const std::string& args)
{
    Scheduler *sched = Director::getInstance()->getScheduler();

    if( args.compare("flush")== 0)
    {
        sched->performFunctionInCocosThread( [](){
            Director::getInstance()->getTextureCache()->removeAllTextures();
        }
                                            );
    }
    else if(args.length()==0)
    {
        sched->performFunctionInCocosThread( [=](){
            mydprintf(fd, "%s", Director::getInstance()->getTextureCache()->getCachedTextureInfo().c_str());
            sendPrompt(fd);
        }
                                            );
    }
    else
    {
        mydprintf(fd, "Unsupported argument: '%s'. Supported arguments: 'flush' or nothing", args.c_str());
    }
}


void Console::commandDirector(int fd, const std::string& args)
{
     auto director = Director::getInstance();
    if(args =="help" || args == "-h")
    {
        const char help[] = "available director directives:\n"
                            "\tpause, pause all scheduled timers, the draw rate will be 4 FPS to reduce CPU consumption\n"
                            "\tend, exit this app.\n"
                            "\tresume, resume all scheduled timers\n"
                            "\tstop, Stops the animation. Nothing will be drawn.\n"
                            "\tstart, Restart the animation again, Call this function only if [director stop] was called earlier\n";
         send(fd, help, sizeof(help) - 1,0);
    }
    else if(args == "pause")
    {
        Scheduler *sched = director->getScheduler();
            sched->performFunctionInCocosThread( [](){
            Director::getInstance()->pause();
        }
                                        );

    }
    else if(args == "resume")
    {
        director->resume();
    }
    else if(args == "stop")
    {
        Scheduler *sched = director->getScheduler();
        sched->performFunctionInCocosThread( [](){
            Director::getInstance()->stopAnimation();
        }
                                        );
    }
    else if(args == "start")
    {
        director->startAnimation();
    }
    else if(args == "end")
    {
        director->end();
    }

}

void Console::commandTouch(int fd, const std::string& args)
{
    if(args =="help" || args == "-h")
    {
        const char help[] = "available touch directives:\n"
                            "\ttap x y: simulate touch tap at (x,y)\n"
                            "\tswipe x1 y1 x2 y2: simulate touch swipe from (x1,y1) to (x2,y2).\n";
         send(fd, help, sizeof(help) - 1,0);
    }
    else
    {
        auto argv = split(args,' ');
        
        if(argv.size() == 0)
        {
            return;
        }

        if(argv[0]=="tap")
        {
            if((argv.size() == 3) && (isFloat(argv[1]) && isFloat(argv[2])))
            {
                
                float x = utils::atof(argv[1].c_str());
                float y = utils::atof(argv[2].c_str());

                srand ((unsigned)time(nullptr));
                _touchId = rand();
                Scheduler *sched = Director::getInstance()->getScheduler();
                sched->performFunctionInCocosThread( [&](){
                    Director::getInstance()->getOpenGLView()->handleTouchesBegin(1, &_touchId, &x, &y);
                    Director::getInstance()->getOpenGLView()->handleTouchesEnd(1, &_touchId, &x, &y);
                });
            }
            else 
            {
                const char msg[] = "touch: invalid arguments.\n";
                send(fd, msg, sizeof(msg) - 1, 0);
            }
            return;
        }

        if(argv[0]=="swipe")
        {
            if((argv.size() == 5) 
                && (isFloat(argv[1])) && (isFloat(argv[2]))
                && (isFloat(argv[3])) && (isFloat(argv[4])))
            {
                
                float x1 = utils::atof(argv[1].c_str());
                float y1 = utils::atof(argv[2].c_str());
                float x2 = utils::atof(argv[3].c_str());
                float y2 = utils::atof(argv[4].c_str());

                srand ((unsigned)time(nullptr));
                _touchId = rand();

                Scheduler *sched = Director::getInstance()->getScheduler();
                sched->performFunctionInCocosThread( [=](){
                    float tempx = x1, tempy = y1;
                    Director::getInstance()->getOpenGLView()->handleTouchesBegin(1, &_touchId, &tempx, &tempy);
                });

                float dx = std::abs(x1 - x2);
                float dy = std::abs(y1 - y2);
                float _x_ = x1, _y_ = y1;
                if(dx > dy)
                {
                    while(dx > 1)
                    {
                        
                        if(x1 < x2)
                        {
                            _x_ += 1;
                        }
                        if(x1 > x2)
                        {
                            _x_ -= 1;
                        }
                        if(y1 < y2)
                        {
                            _y_ += dy/dx;
                        }
                        if(y1 > y2)
                        {
                            _y_ -= dy/dx;
                        }
                        sched->performFunctionInCocosThread( [=](){
                            float tempx = _x_, tempy = _y_;
                            Director::getInstance()->getOpenGLView()->handleTouchesMove(1, &_touchId, &tempx, &tempy);
                        });
                        dx -= 1;
                    }
                    
                }
                else
                {
                    while(dy > 1)
                    {
                        if(x1 < x2)
                        {
                            _x_ += dx/dy;
                        }
                        if(x1 > x2)
                        {
                            _x_ -= dx/dy;
                        }
                        if(y1 < y2)
                        {
                            _y_ += 1;
                        }
                        if(y1 > y2)
                        {
                            _y_ -= 1;
                        }
                        sched->performFunctionInCocosThread( [=](){
                            float tempx = _x_, tempy = _y_;
                            Director::getInstance()->getOpenGLView()->handleTouchesMove(1, &_touchId, &tempx, &tempy);
                        });
                       dy -= 1;
                    }
                    
                }

                sched->performFunctionInCocosThread( [=](){
                    float tempx = x2, tempy = y2;
                    Director::getInstance()->getOpenGLView()->handleTouchesEnd(1, &_touchId, &tempx, &tempy);
                });

            }
            else 
            {
                const char msg[] = "touch: invalid arguments.\n";
                send(fd, msg, sizeof(msg) - 1, 0);
            }
            
        }

    }
}

void Console::commandAllocator(int fd, const std::string& args)
{
#if CC_ENABLE_ALLOCATOR_DIAGNOSTICS
    auto info = allocator::AllocatorDiagnostics::instance()->diagnostics();
    mydprintf(fd, info.c_str());
#else
    mydprintf(fd, "allocator diagnostics not available. CC_ENABLE_ALLOCATOR_DIAGNOSTICS must be set to 1 in ccConfig.h");
#endif
}

static char invalid_filename_char[] = {':', '/', '\\', '?', '%', '*', '<', '>', '"', '|', '\r', '\n', '\t'};

void Console::commandUpload(int fd)
{
    ssize_t n, rc;
    char buf[512], c;
    char *ptr = buf;
    //read file name
    for( n = 0; n < sizeof(buf) - 1; n++ )
    {
        if( (rc = recv(fd, &c, 1, 0)) ==1 ) 
        {
            //for(char x : invalid_filename_char)
			for (int i = 0; i < sizeof(invalid_filename_char) / sizeof(invalid_filename_char[0]); ++i)
            {
				char x = invalid_filename_char[i];
                if(c == x)
                {
                    const char err[] = "upload: invalid file name!\n";
                    send(fd, err, sizeof(err),0);
                    return;
                }
            }
            if(c == ' ') 
            {
                break;
            }
            *ptr++ = c;
        } 
        else if( rc == 0 ) 
        {
            break;
        } 
        else if( errno == EINTR ) 
        {
            continue;
        } 
        else 
        {
            break;
        }
    }
    *ptr = 0;

    std::string filepath = _writablePath + std::string(buf);

    FILE* fp = fopen(filepath.c_str(), "wb");
    if(!fp)
    {
        const char err[] = "can't create file!\n";
        send(fd, err, sizeof(err),0);
        return;
    }
    
    while (true) 
    {
        char data[4];
        for(int i = 0; i < 4; i++)
        {
            data[i] = '=';
        }
        bool more_data;
        readBytes(fd, data, 4, &more_data);
        if(!more_data)
        {
            break;
        }
        unsigned char *decode;
        unsigned char *in = (unsigned char *)data;
        int dt = base64Decode(in, 4, &decode);
        for(int i = 0; i < dt; i++)
        {
            fwrite(decode+i, 1, 1, fp);
        }
        free(decode);
    }
    fclose(fp);
}

ssize_t Console::readBytes(int fd, char* buffer, size_t maxlen, bool* more)
{
    size_t n, rc;
    char c, *ptr = buffer;
    *more = false;
    for( n = 0; n < maxlen; n++ ) {
        if( (rc = recv(fd, &c, 1, 0)) ==1 ) {
            *ptr++ = c;
            if(c == '\n') {
                return n;
            }
        } else if( rc == 0 ) {
            return 0;
        } else if( errno == EINTR ) {
            continue;
        } else {
            return -1;
        }
    }
    *more = true;
    return n;
}

bool Console::parseCommand(int fd)
{
    char buf[512];
    bool more_data;
    auto h = readBytes(fd, buf, 6, &more_data);
    if( h < 0)
    {
        return false;
    }
    if(strncmp(buf, "upload", 6) == 0)
    {
        char c = '\0';
        recv(fd, &c, 1, 0);
        if(c == ' ')
        {
            commandUpload(fd);
            sendPrompt(fd);
            return true;
        }
        else
        {
            const char err[] = "upload: invalid args! Type 'help' for options\n";
            send(fd, err, sizeof(err),0);
            sendPrompt(fd);
            return true;
            
        }
    }
    if(!more_data)
    {
        buf[h] = 0;
    }
    else
    {
        char *pb = buf + 6;
        auto r = readline(fd, pb, sizeof(buf)-6);
        if(r < 0)
        {
            const char err[] = "Unknown error!\n";
            sendPrompt(fd);
            send(fd, err, sizeof(err),0);
            return false;
        }
    }
    std::string cmdLine;

    std::vector<std::string> args;
    cmdLine = std::string(buf);
   
    args = split(cmdLine, ' ');
    if(args.empty())
    {
        const char err[] = "Unknown command. Type 'help' for options\n";
        send(fd, err, sizeof(err),0);
        sendPrompt(fd);
        return true;
    }

    auto it = _commands.find(trim(args[0]));
    if(it != _commands.end())
    {
        std::string args2;
        for(size_t i = 1; i < args.size(); ++i)
        {   
            if(i > 1)
            {
                args2 += ' ';
            }
            args2 += trim(args[i]);
            
        }
        auto cmd = it->second;
        cmd.callback(fd, args2);
    }else if(strcmp(buf, "\r\n") != 0) {
        const char err[] = "Unknown command. Type 'help' for options\n";
        send(fd, err, sizeof(err),0);
    }
    sendPrompt(fd);

    return true;
}

//
// Helpers
//


ssize_t Console::readline(int fd, char* ptr, size_t maxlen)
{
    size_t n, rc;
    char c;

    for( n = 0; n < maxlen - 1; n++ ) {
        if( (rc = recv(fd, &c, 1, 0)) ==1 ) {
            *ptr++ = c;
            if(c == '\n') {
                break;
            }
        } else if( rc == 0 ) {
            return 0;
        } else if( errno == EINTR ) {
            continue;
        } else {
            return -1;
        }
    }

    *ptr = 0;
    return n;
}


void Console::addClient()
{
    struct sockaddr client;
    socklen_t client_len;

    /* new client */
    client_len = sizeof( client );
    int fd = accept(_listenfd, (struct sockaddr *)&client, &client_len );

    // add fd to list of FD
    if( fd != -1 ) {
        FD_SET(fd, &_read_set);
        _fds.push_back(fd);
        _maxfd = std::max(_maxfd,fd);

        sendPrompt(fd);
    }
}

void Console::log(const char* buf)
{
    if( _sendDebugStrings ) {
        pthread_mutex_lock(&_DebugStringsMutex);
        _DebugStrings.push_back(buf);
        pthread_mutex_unlock(&_DebugStringsMutex);
    }
}

//
// Main Loop
//
void Console::loop()
{
    fd_set copy_set;
    struct timeval timeout, timeout_copy;

    _running = true;

    FD_ZERO(&_read_set);
    FD_SET(_listenfd, &_read_set);
    _maxfd = _listenfd;

    timeout.tv_sec = 0;

    /* 0.016 seconds. Wake up once per frame at 60PFS */
    timeout.tv_usec = 16000;

    while(!_endThread) {

        copy_set = _read_set;
        timeout_copy = timeout;
        
        int nready = select(_maxfd+1, &copy_set, nullptr, nullptr, &timeout_copy);

        if( nready == -1 )
        {
            /* error */
            if(errno != EINTR)
                log("Abnormal error in select()\n");
            continue;
        }
        else if( nready == 0 )
        {
            /* timeout. do somethig ? */
        }
        else
        {
            /* new client */
            if(FD_ISSET(_listenfd, &copy_set)) {
                addClient();
                if(--nready <= 0)
                    continue;
            }

            /* data from client */
            std::vector<int> to_remove;
            //for(const auto &fd: _fds) {
			for (auto p_fd = _fds.begin(); p_fd != _fds.end(); ++p_fd)
			{
				const auto& fd = *p_fd;
                if(FD_ISSET(fd,&copy_set)) 
                {
                    //fix Bug #4302 Test case ConsoleTest--ConsoleUploadFile crashed on Linux
                    //On linux, if you send data to a closed socket, the sending process will 
                    //receive a SIGPIPE, which will cause linux system shutdown the sending process.
                    //Add this ioctl code to check if the socket has been closed by peer.
                    
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
                    u_long n = 0;
                    ioctlsocket(fd, FIONREAD, &n);
#else
                    int n = 0;
                    ioctl(fd, FIONREAD, &n);
#endif
                    if(n == 0)
                    {
                        //no data received, or fd is closed
                        continue;
                    }

                    if( ! parseCommand(fd) )
                    {
                        to_remove.push_back(fd);
                    }
                    if(--nready <= 0)
                        break;
                }
            }

            /* remove closed conections */
            //for(int fd: to_remove) {
			for (auto p_fd = to_remove.begin(); p_fd != to_remove.end(); ++p_fd)
			{
				const auto& fd = *p_fd;
                FD_CLR(fd, &_read_set);
                _fds.erase(std::remove(_fds.begin(), _fds.end(), fd), _fds.end());
            }
        }

        /* Any message for the remote console ? send it! */
        if( !_DebugStrings.empty() ) {
            pthread_mutex_lock(&_DebugStringsMutex);
            //for(const auto &str : _DebugStrings) {
			for (auto p_str = _DebugStrings.begin(); p_str != _DebugStrings.end(); ++p_str)
			{
				const auto& str = *p_str;
                //for(const auto &fd : _fds) {
				for (auto p_fd = _fds.begin(); p_fd != _fds.end(); ++p_fd)
				{
					const auto& fd = *p_fd;
                    send(fd, str.c_str(), str.length(),0);
                }
            }
            _DebugStrings.clear();
            pthread_mutex_unlock(&_DebugStringsMutex);
        }
    }

    // clean up: ignore stdin, stdout and stderr
    //for(const auto &fd: _fds )
    for (auto p_fd = _fds.begin(); p_fd != _fds.end(); ++p_fd)
	{
		const auto& fd = *p_fd;
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
        closesocket(fd);
#else
        close(fd);
#endif
    }
    
#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32) || (CC_TARGET_PLATFORM == CC_PLATFORM_WP8) || (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    closesocket(_listenfd);
	WSACleanup();
#else
    close(_listenfd);
#endif
    _running = false;
}

void Console::setBindAddress(const std::string &address)
{
    _bindAddress = address;
}

NS_CC_END
