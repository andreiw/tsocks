/*

   parser.c    - Parsing routines for tsocks.conf

*/

#include <arpa/inet.h>
#include <config.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "parser.h"
#include "common.h"

/* Global configuration variables */
#define MAXLINE BUFSIZ /* Max length of conf line  */
static struct serverent *currentcontext = NULL;

static int handle_line(struct parsedfile *, char *, int);
static int check_server(struct serverent *);
static int tokenize(char *, int, char *[]);
static int handle_path(struct parsedfile *, int, int, char *[]);
static int handle_endpath(struct parsedfile *, int, int, char *[]);
static int handle_reaches(struct parsedfile *, int, char *);
static int handle_server(struct parsedfile *, int, char *);
static int handle_type(struct parsedfile *config, int, char *);
static int handle_port(struct parsedfile *config, int, char *);
static int handle_local(struct parsedfile *, int, char *);
static int handle_defuser(struct parsedfile *, int, char *);
static int handle_defpass(struct parsedfile *, int, char *);
static int make_netent(char *value, struct netent **ent);

int read_config(char *filename, struct parsedfile *config) {
  FILE *conf;
  char line[MAXLINE];
  int rc = 0;
  int lineno = 1;
  struct serverent *server;

  /* Clear out the structure */
  memset(config, 0x0, sizeof(*config));

  /* Initialization */
  currentcontext = &(config->defaultserver);

  /* If a filename wasn't provided, use the default */
  if (filename == NULL) {
    strncpy(line, CONF_FILE, sizeof(line) - 1);
    /* Insure null termination */
    line[sizeof(line) - 1] = (char)0;
    filename = line;
  }

  /* Read the configuration file */
  if ((conf = fopen(filename, "r")) == NULL) {
    show_msg(MSGERR,
             "Could not open socks configuration file "
             "(%s), assuming all networks local\n",
             filename);
    handle_local(config, 0, "0.0.0.0/0.0.0.0");
    rc = 1; /* Severe errors reading configuration */
  } else {
    memset(&(config->defaultserver), 0x0, sizeof(config->defaultserver));

    while (NULL != fgets(line, MAXLINE, conf)) {
      /* This line _SHOULD_ end in \n so we  */
      /* just chop off the \n and hand it on */
      if (strlen(line) > 0)
        line[strlen(line) - 1] = '\0';
      handle_line(config, line, lineno);
      lineno++;
    }
    fclose(conf);

    /* Always add the 127.0.0.1/255.0.0.0 subnet to local */
    handle_local(config, 0, "127.0.0.0/255.0.0.0");

    /* Check default server */
    check_server(&(config->defaultserver));
    server = (config->paths);
    while (server != NULL) {
      check_server(server);
      server = server->next;
    }
  }

  return (rc);
}

/* Check server entries (and establish defaults) */
static int check_server(struct serverent *server) {

  /* Default to the default SOCKS port */
  if (server->port == 0) {
    server->port = 1080;
  }

  /* Default to SOCKS V4 */
  if (server->type == 0) {
    server->type = 4;
  }

  return (0);
}

static int handle_line(struct parsedfile *config, char *line, int lineno) {
  char *words[10];
  static char savedline[MAXLINE];
  int nowords = 0, i;

  /* Save the input string */
  strncpy(savedline, line, MAXLINE - 1);
  savedline[MAXLINE - 1] = (char)0;
  /* Tokenize the input string */
  nowords = tokenize(line, 10, words);

  /* Set the spare slots to an empty string to simplify */
  /* processing                                         */
  for (i = nowords; i < 10; i++)
    words[i] = "";

  if (nowords > 0) {
    /* Now this can either be a "path" block starter or */
    /* ender, otherwise it has to be a pair (<name> =   */
    /* <value>)                                         */
    if (!strcmp(words[0], "path")) {
      handle_path(config, lineno, nowords, words);
    } else if (!strcmp(words[0], "}")) {
      handle_endpath(config, lineno, nowords, words);
    } else {
      /* Has to be a pair */
      if ((nowords != 3) || (strcmp(words[1], "="))) {
        show_msg(MSGERR,
                 "Malformed configuration pair "
                 "on line %d in configuration "
                 "file, \"%s\"\n",
                 lineno, savedline);
      } else if (!strcmp(words[0], "reaches")) {
        handle_reaches(config, lineno, words[2]);
      } else if (!strcmp(words[0], "server")) {
        handle_server(config, lineno, words[2]);
      } else if (!strcmp(words[0], "server_port")) {
        handle_port(config, lineno, words[2]);
      } else if (!strcmp(words[0], "server_type")) {
        handle_type(config, lineno, words[2]);
      } else if (!strcmp(words[0], "default_user")) {
        handle_defuser(config, lineno, words[2]);
      } else if (!strcmp(words[0], "default_pass")) {
        handle_defpass(config, lineno, words[2]);
      } else if (!strcmp(words[0], "local")) {
        handle_local(config, lineno, words[2]);
      } else {
        show_msg(MSGERR,
                 "Invalid pair type (%s) specified "
                 "on line %d in configuration file, "
                 "\"%s\"\n",
                 words[0], lineno, savedline);
      }
    }
  }

  return (0);
}

/* This routines breaks up input lines into tokens  */
/* and places these tokens into the array specified */
/* by tokens                                        */
static int tokenize(char *line, int arrsize, char *tokens[]) {
  int tokenno = -1;
  int finished = 0;

  /* Whitespace is ignored before and after tokens     */
  while ((tokenno < (arrsize - 1)) && (line = line + strspn(line, " \t")) &&
         (*line != (char)0) && (!finished)) {
    tokenno++;
    tokens[tokenno] = line;
    line = line + strcspn(line, " \t");
    *line = (char)0;
    line++;

    /* We ignore everything after a # */
    if (*tokens[tokenno] == '#') {
      finished = 1;
      tokenno--;
    }
  }

  return (tokenno + 1);
}

static int handle_path(struct parsedfile *config, int lineno, int nowords,
                       char *words[]) {
  struct serverent *newserver;

  if ((nowords != 2) || (strcmp(words[1], "{"))) {
    show_msg(MSGERR,
             "Badly formed path open statement on line %d "
             "in configuration file (should look like "
             "\"path {\")\n",
             lineno);
  } else if (currentcontext != &(config->defaultserver)) {
    /* You cannot nest path statements so check that */
    /* the current context is defaultserver          */
    show_msg(MSGERR,
             "Path statements cannot be nested on line %d "
             "in configuration file\n",
             lineno);
  } else {
    /* Open up a new serverent, put it on the list   */
    /* then set the current context                  */
    if ((newserver = (struct serverent *)malloc(sizeof(struct serverent))) ==
        NULL)
      exit(-1);

    /* Initialize the structure */
    show_msg(MSGDEBUG,
             "New server structure from line %d in configuration file going "
             "to 0x%08x\n",
             lineno, newserver);
    memset(newserver, 0x0, sizeof(*newserver));
    newserver->next = config->paths;
    newserver->lineno = lineno;
    config->paths = newserver;
    currentcontext = newserver;
  }

  return (0);
}

static int handle_endpath(struct parsedfile *config, int lineno, int nowords,
                          char *words[]) {

  if (nowords != 1) {
    show_msg(MSGERR,
             "Badly formed path close statement on line "
             "%d in configuration file (should look like "
             "\"}\")\n",
             lineno);
  } else {
    currentcontext = &(config->defaultserver);
  }

  /* We could perform some checking on the validty of data in */
  /* the completed path here, but thats what verifyconf is    */
  /* designed to do, no point in weighing down libtsocks      */

  return (0);
}

static int handle_reaches(struct parsedfile *config, int lineno, char *value) {
  int rc;
  struct netent *ent;

  rc = make_netent(value, &ent);
  switch (rc) {
  case 1:
    show_msg(MSGERR,
             "Local network specification (%s) is not validly "
             "constructed in reach statement on line "
             "%d in configuration "
             "file\n",
             value, lineno);
    return (0);
    break;
  case 2:
    show_msg(MSGERR,
             "IP in reach statement "
             "network specification (%s) is not valid on line "
             "%d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 3:
    show_msg(MSGERR,
             "SUBNET in reach statement "
             "network specification (%s) is not valid on "
             "line %d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 4:
    show_msg(MSGERR, "IP (%s) & ", inet_ntoa(ent->localip));
    show_msg(MSGERR,
             "SUBNET (%s) != IP on line %d in "
             "configuration file, ignored\n",
             inet_ntoa(ent->localnet), lineno);
    return (0);
    break;
  case 5:
    show_msg(MSGERR,
             "Start port in reach statement "
             "network specification (%s) is not valid on line "
             "%d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 6:
    show_msg(MSGERR,
             "End port in reach statement "
             "network specification (%s) is not valid on line "
             "%d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 7:
    show_msg(MSGERR,
             "End port in reach statement "
             "network specification (%s) is less than the start "
             "port on line %d in configuration file\n",
             value, lineno);
    return (0);
    break;
  }

  /* The entry is valid so add it to linked list */
  ent->next = currentcontext->reachnets;
  currentcontext->reachnets = ent;

  return (0);
}

static int handle_server(struct parsedfile *config, int lineno, char *value) {
  char *ip;

  ip = strsplit(NULL, &value, " ");

  /* We don't verify this ip/hostname at this stage, */
  /* its resolved immediately before use in tsocks.c */
  if (currentcontext->address == NULL)
    currentcontext->address = strdup(ip);
  else {
    if (currentcontext == &(config->defaultserver))
      show_msg(MSGERR,
               "Only one default SOCKS server "
               "may be specified at line %d in "
               "configuration file\n",
               lineno);
    else
      show_msg(MSGERR,
               "Only one SOCKS server may be specified "
               "per path on line %d in configuration "
               "file. (Path begins on line %d)\n",
               lineno, currentcontext->lineno);
  }

  return (0);
}

static int handle_port(struct parsedfile *config, int lineno, char *value) {

  if (currentcontext->port != 0) {
    if (currentcontext == &(config->defaultserver))
      show_msg(MSGERR,
               "Server port may only be specified "
               "once for default server, at line %d "
               "in configuration file\n",
               lineno);
    else
      show_msg(MSGERR,
               "Server port may only be specified "
               "once per path on line %d in configuration "
               "file. (Path begins on line %d)\n",
               lineno, currentcontext->lineno);
  } else {
    errno = 0;
    currentcontext->port =
        (unsigned short int)(strtol(value, (char **)NULL, 10));
    if ((errno != 0) || (currentcontext->port == 0)) {
      show_msg(MSGERR,
               "Invalid server port number "
               "specified in configuration file "
               "(%s) on line %d\n",
               value, lineno);
      currentcontext->port = 0;
    }
  }

  return (0);
}

static int handle_defuser(struct parsedfile *config, int lineno, char *value) {

  if (currentcontext->defuser != NULL) {
    if (currentcontext == &(config->defaultserver))
      show_msg(MSGERR,
               "Default username may only be specified "
               "once for default server, at line %d "
               "in configuration file\n",
               lineno);
    else
      show_msg(MSGERR,
               "Default username may only be specified "
               "once per path on line %d in configuration "
               "file. (Path begins on line %d)\n",
               lineno, currentcontext->lineno);
  } else {
    currentcontext->defuser = strdup(value);
  }

  return (0);
}

static int handle_defpass(struct parsedfile *config, int lineno, char *value) {

  if (currentcontext->defpass != NULL) {
    if (currentcontext == &(config->defaultserver))
      show_msg(MSGERR,
               "Default password may only be specified "
               "once for default server, at line %d "
               "in configuration file\n",
               lineno);
    else
      show_msg(MSGERR,
               "Default password may only be specified "
               "once per path on line %d in configuration "
               "file. (Path begins on line %d)\n",
               lineno, currentcontext->lineno);
  } else {
    currentcontext->defpass = strdup(value);
  }

  return (0);
}

static int handle_type(struct parsedfile *config, int lineno, char *value) {

  if (currentcontext->type != 0) {
    if (currentcontext == &(config->defaultserver))
      show_msg(MSGERR,
               "Server type may only be specified "
               "once for default server, at line %d "
               "in configuration file\n",
               lineno);
    else
      show_msg(MSGERR,
               "Server type may only be specified "
               "once per path on line %d in configuration "
               "file. (Path begins on line %d)\n",
               lineno, currentcontext->lineno);
  } else {
    errno = 0;
    currentcontext->type = (int)strtol(value, (char **)NULL, 10);
    if ((errno != 0) || (currentcontext->type == 0) ||
        ((currentcontext->type != 4) && (currentcontext->type != 5))) {
      show_msg(MSGERR,
               "Invalid server type (%s) "
               "specified in configuration file "
               "on line %d, only 4 or 5 may be "
               "specified\n",
               value, lineno);
      currentcontext->type = 0;
    }
  }

  return (0);
}

static int handle_local(struct parsedfile *config, int lineno, char *value) {
  int rc;
  struct netent *ent;

  if (currentcontext != &(config->defaultserver)) {
    show_msg(MSGERR,
             "Local networks cannot be specified in path "
             "block at like %d in configuration file. "
             "(Path block started at line %d)\n",
             lineno, currentcontext->lineno);
    return (0);
  }

  rc = make_netent(value, &ent);
  switch (rc) {
  case 1:
    show_msg(MSGERR,
             "Local network specification (%s) is not validly "
             "constructed on line %d in configuration "
             "file\n",
             value, lineno);
    return (0);
    break;
  case 2:
    show_msg(MSGERR,
             "IP for local "
             "network specification (%s) is not valid on line "
             "%d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 3:
    show_msg(MSGERR,
             "SUBNET for "
             "local network specification (%s) is not valid on "
             "line %d in configuration file\n",
             value, lineno);
    return (0);
    break;
  case 4:
    show_msg(MSGERR, "IP (%s) & ", inet_ntoa(ent->localip));
    show_msg(MSGERR,
             "SUBNET (%s) != IP on line %d in "
             "configuration file, ignored\n",
             inet_ntoa(ent->localnet), lineno);
    return (0);
  case 5:
  case 6:
  case 7:
    show_msg(MSGERR,
             "Port specification is invalid and "
             "not allowed in local network specification "
             "(%s) on line %d in configuration file\n",
             value, lineno);
    return (0);
    break;
  }

  if (ent->startport || ent->endport) {
    show_msg(MSGERR,
             "Port specification is "
             "not allowed in local network specification "
             "(%s) on line %d in configuration file\n",
             value, lineno);
    return (0);
  }

  /* The entry is valid so add it to linked list */
  ent->next = config->localnets;
  (config->localnets) = ent;

  return (0);
}

/* Construct a netent given a string like                             */
/* "198.126.0.1[:portno[-portno]]/255.255.255.0"                      */
int make_netent(char *value, struct netent **ent) {
  char *ip;
  char *subnet;
  char *startport = NULL;
  char *endport = NULL;
  char *badchar;
  char separator;
  static char buf[200];
  char *split;

  /* Get a copy of the string so we can modify it */
  strncpy(buf, value, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = (char)0;
  split = buf;

  /* Now rip it up */
  ip = strsplit(&separator, &split, "/:");
  if (separator == ':') {
    /* We have a start port */
    startport = strsplit(&separator, &split, "-/");
    if (separator == '-')
      /* We have an end port */
      endport = strsplit(&separator, &split, "/");
  }
  subnet = strsplit(NULL, &split, " \n");

  if ((ip == NULL) || (subnet == NULL)) {
    /* Network specification not validly constructed */
    return (1);
  }

  /* Allocate the new entry */
  if ((*ent = (struct netent *)malloc(sizeof(struct netent))) == NULL) {
    /* If we couldn't malloc some storage, leave */
    exit(1);
  }

  show_msg(MSGDEBUG, "New network entry for %s/%s going to 0x%08x\n", ip,
           subnet, *ent);

  if (!startport)
    (*ent)->startport = 0;
  if (!endport)
    (*ent)->endport = 0;

#ifdef HAVE_INET_ADDR
  if (((*ent)->localip.s_addr = inet_addr(ip)) == -1) {
#elif defined(HAVE_INET_ATON)
  if (!(inet_aton(ip, &((*ent)->localip)))) {
#endif
    /* Badly constructed IP */
    free(*ent);
    return (2);
  }
#ifdef HAVE_INET_ADDR
  else if (((*ent)->localnet.s_addr = inet_addr(subnet)) == -1) {
#elif defined(HAVE_INET_ATON)
  else if (!(inet_aton(subnet, &((*ent)->localnet)))) {
#endif
    /* Badly constructed subnet */
    free(*ent);
    return (3);
  } else if (((*ent)->localip.s_addr & (*ent)->localnet.s_addr) !=
             (*ent)->localip.s_addr) {
    /* Subnet and Ip != Ip */
    free(*ent);
    return (4);
  } else if (startport &&
             (!((*ent)->startport = strtol(startport, &badchar, 10)) ||
              (*badchar != 0) || ((*ent)->startport > 65535))) {
    /* Bad start port */
    free(*ent);
    return (5);
  } else if (endport && (!((*ent)->endport = strtol(endport, &badchar, 10)) ||
                         (*badchar != 0) || ((*ent)->endport > 65535))) {
    /* Bad end port */
    free(*ent);
    return (6);
  } else if (((*ent)->startport > (*ent)->endport) &&
             !(startport && !endport)) {
    /* End port is less than start port */
    free(*ent);
    return (7);
  }

  if (startport && !endport)
    (*ent)->endport = (*ent)->startport;

  return (0);
}

int is_local(struct parsedfile *config, struct in_addr *testip,
             unsigned int port) {
  struct netent *ent;
  struct serverent *sent;

  for (sent = (config->paths); sent != NULL; sent = sent->next) {
    ent = sent->reachnets;
    while (ent != NULL) {
      if ((testip->s_addr & ent->localnet.s_addr) ==
              (ent->localip.s_addr & ent->localnet.s_addr) &&
          (!port || !ent->startport ||
           ((ent->startport <= port) && (ent->endport >= port)))) {
        return 1;
      }

      ent = ent->next;
    }
  }

  for (ent = (config->localnets); ent != NULL; ent = ent->next) {
    if ((testip->s_addr & ent->localnet.s_addr) ==
        (ent->localip.s_addr & ent->localnet.s_addr)) {
      return (0);
    }
  }

  return (1);
}

/* Find the appropriate server to reach an ip */
int pick_server(struct parsedfile *config, struct serverent **ent,
                struct in_addr *ip, unsigned int port) {
  struct netent *net;
  char ipbuf[64];

  show_msg(MSGDEBUG, "Picking appropriate server for %s\n", inet_ntoa(*ip));
  *ent = (config->paths);
  while (*ent != NULL) {
    /* Go through all the servers looking for one */
    /* with a path to this network                */
    show_msg(MSGDEBUG, "Checking SOCKS server %s\n",
             ((*ent)->address ? (*ent)->address : "(No Address)"));
    net = (*ent)->reachnets;
    while (net != NULL) {
      strcpy(ipbuf, inet_ntoa(net->localip));
      show_msg(MSGDEBUG, "Server can reach %s/%s\n", ipbuf,
               inet_ntoa(net->localnet));
      if (((ip->s_addr & net->localnet.s_addr) ==
           (net->localip.s_addr & net->localnet.s_addr)) &&
          (!net->startport ||
           ((net->startport <= port) && (net->endport >= port)))) {
        show_msg(MSGDEBUG, "This server can reach target\n");
        /* Found the net, return */
        return (0);
      }
      net = net->next;
    }
    (*ent) = (*ent)->next;
  }

  *ent = &(config->defaultserver);

  return (0);
}

/* This function is very much like strsep, it looks in a string for */
/* a character from a list of characters, when it finds one it      */
/* replaces it with a \0 and returns the start of the string        */
/* (basically spitting out tokens with arbitrary separators). If no */
/* match is found the remainder of the string is returned and       */
/* the start pointer is set to be NULL. The difference between      */
/* standard strsep and this function is that this one will          */
/* set *separator to the character separator found if it isn't null */
char *strsplit(char *separator, char **text, const char *search) {
  int len;
  char *ret;

  ret = *text;

  if (*text == NULL) {
    if (separator)
      *separator = '\0';
    return (NULL);
  } else {
    len = strcspn(*text, search);
    if (len == strlen(*text)) {
      if (separator)
        *separator = '\0';
      *text = NULL;
    } else {
      *text = *text + len;
      if (separator)
        *separator = **text;
      **text = '\0';
      *text = *text + 1;
    }
  }

  return (ret);
}
