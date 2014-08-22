#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

/*
 * Message types.
 */
#define MSG_LOGIN             1
#define MSG_HELLO             2
#define MSG_DENIED            3
#define MSG_GOODBYE           4
#define MSG_PING              5

#define MSG_PLAYER_NEW        10
#define MSG_PLAYER_LEFT       11

#define MSG_OPENGAME          20
#define MSG_GAME_PLAYER       21
#define MSG_CLOSE_GAME        22
#define MSG_JOIN              23
#define MSG_LEAVE             24
#define MSG_JOINACK           25
#define MSG_JOINNAK           26
#define MSG_CREATE            27
#define MSG_START             28
#define MSG_REMOVE            29
#define MSG_RESIGN            30
#define MSG_ADD_AI            31

#define MSG_STATUS_META       40
#define MSG_STATUS_PLAYER     41
#define MSG_STATUS_CARD       42
#define MSG_STATUS_GOAL       43
#define MSG_STATUS_MISC       44
#define MSG_LOG               45
#define MSG_CHAT              46
#define MSG_WAITING           47
#define MSG_SEAT              48
#define MSG_GAMECHAT          49
#define MSG_LOG_FORMAT        50

#define MSG_CHOOSE            60
#define MSG_PREPARE           61

#define MSG_GAMEOVER          70

/*
 * Connection states.
 */
#define CS_EMPTY              0
#define CS_INIT               1
#define CS_LOBBY              2
#define CS_PLAYING            3
#define CS_DISCONN            4

/*
 * Wait states.
 */
#define WAIT_READY            0
#define WAIT_BLOCKED          1
#define WAIT_OPTION           2


/* External functions */
extern void get_string(char *dest, char **msg);
extern int get_integer(char **msg);
extern void put_string(char *ptr, char **msg);
extern void put_integer(int x, char **msg);
extern void start_msg(char **msg, int type);
extern void finish_msg(char *start, char *end);
extern void send_msg(int fd, char *msg);
extern void send_msgf(int fd, int type, char *fmt, ...);
