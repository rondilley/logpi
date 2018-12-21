/*****
 *
 * Description: Main Functions
 *
 * Copyright (c) 2008-2018, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****/

/****
 *
 * includes
 *
 ****/

#include "spi_main.h"

/****
 *
 * local variables
 *
 ****/

/****
 *
 * global variables
 *
 ****/

PUBLIC int quit = FALSE;
PUBLIC int reload = FALSE;
PUBLIC Config_t *config = NULL;

/****
 *
 * external variables
 *
 ****/

extern int errno;
extern char **environ;

/****
 *
 * external functions
 *
 ****/

extern int searchFile(const char *fName);
extern int loadSearchFile(const char *fName);

/****
 *
 * main function
 *
 ****/

int main(int argc, char *argv[])
{
  FILE *inFile = NULL, *outFile = NULL;
  char inBuf[8192];
  char outFileName[PATH_MAX];
  PRIVATE int c = 0, i, ret;
  char *tok;
  struct searchTerm_s *searchPtr;

#ifndef DEBUG
  struct rlimit rlim;

  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);
#endif

  /* setup config */
  config = (Config_t *)XMALLOC(sizeof(Config_t));
  XMEMSET(config, 0, sizeof(Config_t));

  /* force mode to forground */
  config->mode = MODE_INTERACTIVE;

  /* store current pid */
  config->cur_pid = getpid();

  /* get real uid and gid in prep for priv drop */
  config->gid = getgid();
  config->uid = getuid();

  while (1)
  {
    int this_option_optind = optind ? optind : 1;
#ifdef HAVE_GETOPT_LONG
    int option_index = 0;
    static struct option long_options[] = {{"version", no_argument, 0, 'v'},
                                           {"debug", required_argument, 0, 'd'},
                                           {"file", required_argument, 0, 'f'},
                                           {"help", no_argument, 0, 'h'},
                                           {"quick", no_argument, 0, 'q'},
                                           {0, no_argument, 0, 0}};
    c = getopt_long(argc, argv, "vd:f:hq", long_options, &option_index);
#else
    c = getopt(argc, argv, "vd:f:hq");
#endif

    if (c EQ - 1)
      break;

    switch (c)
    {

    case 'v':
      /* show the version */
      print_version();
      return (EXIT_SUCCESS);

    case 'd':
      /* show debig info */
      config->debug = atoi(optarg);
      break;

    case 'f':
      /* load search terms from file */
      config->search_filename = (char *)XMALLOC(PATH_MAX + 1);
      XSTRNCPY(config->search_filename, optarg, strlen(optarg));
      break;

    case 'h':
      /* show help info */
      print_help();
      return (EXIT_SUCCESS);

    case 'q':
      /* enable quick mode, don't print matching lines */
      config->quick = TRUE;
      break;

    default:
      fprintf(stderr, "Unknown option code [0%o]\n", c);
    }
  }

  /* check dirs and files for danger */
  if (time(&config->current_time) EQ - 1)
  {
    display(LOG_ERR, "Unable to get current time");
    /* cleanup buffers */
    cleanup();
    return (EXIT_FAILURE);
  }

  /* initialize program wide config options */
  config->hostname = (char *)XMALLOC(MAXHOSTNAMELEN + 1);

  /* get processor hostname */
  if (gethostname(config->hostname, MAXHOSTNAMELEN) != 0)
  {
    display(LOG_ERR, "Unable to get hostname");
    strcpy(config->hostname, "unknown");
  }

  config->cur_pid = getpid();

  /* setup current time updater */
  signal(SIGALRM, ctime_prog);
  alarm(ALARM_TIMER);

  /*
   * get to work
   */

  if (config->search_filename != NULL)
  {
    /* load the search term(s) into an list */
    loadSearchFile(config->search_filename);
  }
  else
  {
    /* if no search term file specified, use the first argument */
    tok = strtok(argv[optind], ",");

    /* parse and store search terms */
    do
    {
      /* create new search term record */
      if ((searchPtr = XMALLOC(sizeof(struct searchTerm_s))) EQ NULL)
      {
        fprintf(stderr, "ERR - Unable to allocate memory for search term\n");
        exit(EXIT_FAILURE);
      }
      XMEMSET(searchPtr, '\0', sizeof(struct searchTerm_s));
      searchPtr->len = strlen(tok);
      searchPtr->term = XMALLOC(searchPtr->len+1);
      XMEMCPY(searchPtr->term, tok, searchPtr->len);

      /* store search term in the linked list */
      searchPtr->next = config->searchHead;
      if ( config->searchHead != NULL )
        config->searchHead->prev = searchPtr;
      config->searchHead = searchPtr;
    } while ((tok = strtok(NULL, ",")) != NULL);
    optind++;
  }

  /* exit if there are no search terms */
  if ( config->searchHead EQ NULL ) {
    fprintf( stderr, "No search terms specified, exiting\n" );
    return( EXIT_FAILURE );
  }
  
  /* XXX need to convert addresses to numbers to allow for range matches */
  fprintf(stderr, "Searching for ");
  searchPtr = config->searchHead;
  while (searchPtr != NULL)
  {
    fprintf(stderr, "%s ", searchPtr->term);
    searchPtr = searchPtr->next;
  }
  fprintf(stderr, "\n");

  /* process all the files */
  while (optind < argc)
  {
    searchFile(argv[optind++]);
  }

  /*
   * finished with the work
   */

  cleanup();

  return (EXIT_SUCCESS);
}

/****
 *
 * display prog info
 *
 ****/

void show_info(void)
{
  fprintf(stderr, "%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
  fprintf(stderr, "By: Ron Dilley\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY.\n", PROGNAME);
  fprintf(stderr, "This is free software, and you are welcome\n");
  fprintf(stderr, "to redistribute it under certain conditions;\n");
  fprintf(stderr, "See the GNU General Public License for details.\n");
  fprintf(stderr, "\n");
}

/*****
 *
 * display version info
 *
 *****/

PRIVATE void print_version(void)
{
  printf("%s v%s [%s - %s]\n", PROGNAME, VERSION, __DATE__, __TIME__);
}

/*****
 *
 * print help info
 *
 *****/

PRIVATE void print_help(void)
{
  print_version();

  fprintf(stderr, "\n");
  fprintf(
      stderr,
      "syntax: spi [options] searchterm[,searchterm] filename [filename ...]\n");

#ifdef HAVE_GETOPT_LONG
  fprintf(stderr, " -d|--debug (0-9)       enable debugging info\n");
  fprintf(stderr,
          " -f|--file {fname}      use search terms stored in a file\n");
  fprintf(stderr, " -h|--help              this info\n");
  fprintf(stderr, " -q|--quick             quick mode, report matches and counts only\n");
  fprintf(stderr, " -v|--version           display version information\n");
  fprintf(stderr,
          " searchterm             a comma separated list of search terms\n");
  fprintf(stderr, " filename               one or more files to process, use "
                  "'-' to read from stdin\n");
#else
  fprintf(stderr, " -d {lvl}      enable debugging info\n");
  fprintf(stderr, " -f {fname}    use search terms stored in a file\n");
  fprintf(stderr, " -h            this info\n");
  fprintf(stderr, " -q            quick mode, report matches and counts only\n");
  fprintf(stderr, " -v            display version information\n");
  fprintf(stderr, " searchterm    a comma separated list of search terms\n");
  fprintf(stderr, " filename      one or more files to process, use '-' to "
                  "read from stdin\n");
#endif

  fprintf(stderr, "\n");
}

/****
 *
 * cleanup
 *
 ****/

PRIVATE void cleanup(void)
{
  struct searchTerm_s *tmpPtr;

  /* free any match templates */
  cleanMatchList();

  while ( config->searchHead != NULL ) {
    tmpPtr = config->searchHead;
    config->searchHead = config->searchHead->next;
    XFREE( tmpPtr->term );
    XFREE( tmpPtr );
  }

  if (config->outFile_st != NULL)
    fclose(config->outFile_st);
  XFREE(config->hostname);
#ifdef MEM_DEBUG
  XFREE_ALL();
#else
  XFREE(config);
#endif
}

/*****
 *
 * interrupt handler (current time)
 *
 *****/

void ctime_prog(int signo)
{
  time_t ret;

  /* disable SIGALRM */
  signal(SIGALRM, SIG_IGN);
  /* update current time */
  reload = TRUE;

  /* reset SIGALRM */
  signal(SIGALRM, ctime_prog);
  /* reset alarm */
  alarm(ALARM_TIMER);
}
